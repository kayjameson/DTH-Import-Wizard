#include "Steps/DTHMergeCheck.h"

#include "Export/DTHExport.h"
#include "FbxImporter.h" // includes the FBX SDK with the engine's guards
#include "Misc/Paths.h"

namespace
{
	/**
	 * A polygon's identity across the two exports: the UV of each of its corners, quantised. Positions don't survive
	 * DTH (clothing is pushed out against poke-through) and polygons may be re-split, but UVs are carried through
	 * unchanged (measured on Vi 2026-10-16), so the key is per corner: a re-split polygon's corners still match.
	 */
	using FUvKey = TPair<int32, int32>;

	void CornerKeys(FbxMesh* Mesh, int32 Polygon, TArray<FUvKey>& OutKeys)
	{
		OutKeys.Reset();
		FbxStringList UvSets;
		Mesh->GetUVSetNames(UvSets);
		if (UvSets.GetCount() == 0) return;
		for (int32 Corner = 0; Corner < Mesh->GetPolygonSize(Polygon); ++Corner)
		{
			FbxVector2 Uv;
			bool bUnmapped = false;
			if (Mesh->GetPolygonVertexUV(Polygon, Corner, UvSets[0], Uv, bUnmapped) && !bUnmapped)
			{
				OutKeys.Add(FUvKey(FMath::RoundToInt32(Uv[0] * 100000.0), FMath::RoundToInt32(Uv[1] * 100000.0)));
			}
		}
	}

	/** "SC_Fantasy_Armour_Boots_82160.Shape" -> "sc_fantasy_armour_boots_82160". */
	FString ObjectName(FbxNode* Node)
	{
		FString Name = UTF8_TO_TCHAR(Node->GetName());
		Name.RemoveFromEnd(TEXT(".Shape"));
		return Name.ToLower();
	}

	/** Material index per polygon (FBX by-polygon or all-same mapping). */
	int32 PolygonMaterial(FbxMesh* Mesh, int32 Polygon)
	{
		FbxGeometryElementMaterial* Element = Mesh->GetElementMaterial(0);
		if (!Element) return 0;
		if (Element->GetMappingMode() == FbxGeometryElement::eAllSame) return Element->GetIndexArray().GetCount() > 0 ? Element->GetIndexArray().GetAt(0) : 0;
		return Polygon < Element->GetIndexArray().GetCount() ? Element->GetIndexArray().GetAt(Polygon) : 0;
	}

	/** Every texture file a Daz surface uses, across all its properties, sorted. */
	FString TextureSet(FbxSurfaceMaterial* Material, TArray<FString>& OutFiles)
	{
		for (FbxProperty Property = Material->GetFirstProperty(); Property.IsValid(); Property = Material->GetNextProperty(Property))
		{
			for (int32 i = 0; i < Property.GetSrcObjectCount<FbxFileTexture>(); ++i)
			{
				const FString File = FPaths::ConvertRelativePathToFull(UTF8_TO_TCHAR(Property.GetSrcObject<FbxFileTexture>(i)->GetFileName()));
				OutFiles.AddUnique(File.Replace(TEXT("\\"), TEXT("/")).ToLower());
			}
		}
		OutFiles.Sort();
		return FString::Join(OutFiles, TEXT("|"));
	}

	/** Loads an FBX scene; null with OutError set on failure. */
	FbxScene* LoadScene(FbxManager* Manager, const FString& File, FString& OutError)
	{
		FbxImporter* Importer = FbxImporter::Create(Manager, "");
		if (!Importer->Initialize(TCHAR_TO_UTF8(*File), -1, Manager->GetIOSettings()))
		{
			OutError = FString::Printf(TEXT("could not open %s (%s)"), *File, UTF8_TO_TCHAR(Importer->GetStatus().GetErrorString()));
			Importer->Destroy();
			return nullptr;
		}
		FbxScene* Scene = FbxScene::Create(Manager, "");
		const bool bOk = Importer->Import(Scene);
		Importer->Destroy();
		if (!bOk)
		{
			OutError = FString::Printf(TEXT("could not read %s"), *File);
			Scene->Destroy();
			return nullptr;
		}
		return Scene;
	}

	/** Calls Visit(mesh node, mesh) for every mesh in the scene. */
	template <typename FVisit>
	void ForEachMesh(FbxNode* Node, FVisit&& Visit)
	{
		if (!Node) return;
		if (FbxMesh* Mesh = Node->GetMesh()) Visit(Node, Mesh);
		for (int32 i = 0; i < Node->GetChildCount(); ++i) ForEachMesh(Node->GetChild(i), Visit);
	}

	FString ShortPath(const FString& File)
	{
		TArray<FString> Parts;
		File.ParseIntoArray(Parts, TEXT("/"));
		const int32 Keep = FMath::Min(3, Parts.Num());
		return FString::Join(TArrayView<const FString>(Parts.GetData() + Parts.Num() - Keep, Keep), TEXT("/"));
	}
}

FString FDTHMergeProblem::Describe() const
{
	return FString::Printf(TEXT("%s: slot %s merges Daz surfaces with different textures - %s. One material shows one texture set, so all but one of these will look wrong: give each its own slot in Houdini's DazToHueMaterial node, or bake this slot."),
		*Mesh, *Slot, *FString::Join(Groups, TEXT("; ")));
}

FString DTHImport::FindDazExportFbx(const FDTHExport& Export)
{
	FString Dir = Export.Folder;
	for (int32 Up = 0; Up < 6 && !Dir.IsEmpty(); ++Up)
	{
		const FString Candidate = Dir / TEXT("houdini") / TEXT("daz-export") / Export.Character / (Export.Character + TEXT(".fbx"));
		if (FPaths::FileExists(Candidate)) return Candidate;
		const FString Parent = FPaths::GetPath(Dir);
		if (Parent == Dir) break;
		Dir = Parent;
	}
	return FString();
}

FDTHMergeCheck DTHImport::CheckMaterialMerges(const FDTHExport& Export, const TArray<const FDTHMeshEntry*>& Meshes, const FString& DazFbx)
{
	FDTHMergeCheck Result;
	Result.DazFbx = DazFbx.IsEmpty() ? FindDazExportFbx(Export) : DazFbx;
	if (Result.DazFbx.IsEmpty() || !FPaths::FileExists(Result.DazFbx))
	{
		Result.Skipped = TEXT("Daz's own export (houdini/daz-export/<Character>/<Character>.fbx) was not found, so merged material slots could not be checked.");
		return Result;
	}

	FbxManager* Manager = FbxManager::Create();
	FbxIOSettings* Settings = FbxIOSettings::Create(Manager, IOSROOT);
	// Only geometry, UVs and materials are needed: skip blend shapes, skinning and animation (most of these files).
	Settings->SetBoolProp(IMP_FBX_SHAPE, false);
	Settings->SetBoolProp(IMP_FBX_LINK, false);
	Settings->SetBoolProp(IMP_FBX_ANIMATION, false);
	Settings->SetBoolProp(IMP_FBX_GOBO, false);
	Manager->SetIOSettings(Settings);
	ON_SCOPE_EXIT { Manager->Destroy(); };

	// Daz side, per object: every corner UV -> the surface (Daz material) it belongs to, with that surface's textures.
	struct FSurface { FString Label; FString Set; TArray<FString> Files; };
	TArray<FSurface> Surfaces;
	TMap<FString, TMap<FUvKey, int32>> SurfaceOfCorner; // object name -> corner UV -> surface
	{
		FString Error;
		FbxScene* Daz = LoadScene(Manager, Result.DazFbx, Error);
		if (!Daz)
		{
			Result.Skipped = TEXT("Daz's export ") + Error + TEXT("; merged material slots were not checked.");
			return Result;
		}
		TArray<FUvKey> Keys;
		ForEachMesh(Daz->GetRootNode(), [&](FbxNode* Node, FbxMesh* Mesh)
		{
			TArray<int32> SurfaceOfMaterial;
			const FString Object = ObjectName(Node);
			for (int32 m = 0; m < Node->GetMaterialCount(); ++m)
			{
				FSurface Surface;
				Surface.Label = UTF8_TO_TCHAR(Node->GetMaterial(m)->GetName());
				Surface.Set = TextureSet(Node->GetMaterial(m), Surface.Files);
				SurfaceOfMaterial.Add(Surfaces.Add(Surface));
			}
			TMap<FUvKey, int32>& Corners = SurfaceOfCorner.FindOrAdd(Object);
			for (int32 p = 0; p < Mesh->GetPolygonCount(); ++p)
			{
				const int32 m = PolygonMaterial(Mesh, p);
				if (!SurfaceOfMaterial.IsValidIndex(m)) continue;
				CornerKeys(Mesh, p, Keys);
				for (const FUvKey& Key : Keys)
				{
					// A UV two surfaces share can't say which one a corner came from: mark it and ignore it later.
					int32& Surface = Corners.FindOrAdd(Key, SurfaceOfMaterial[m]);
					if (Surface != SurfaceOfMaterial[m]) Surface = INDEX_NONE;
				}
			}
		});
	}

	// Houdini side: every exported slot -> the Daz surfaces its polygons came from.
	for (const FDTHMeshEntry* Entry : Meshes)
	{
		FString Error;
		FbxScene* Scene = LoadScene(Manager, Entry->File, Error);
		if (!Scene)
		{
			Result.Unmatched.Add(FPaths::GetBaseFilename(Entry->File) + TEXT(" (") + Error + TEXT(")"));
			continue;
		}
		// The Daz objects this mesh can come from: the one of the same name, else (the figure, which DTH names "Body"
		// and which gathers the figure, eyes, mouth, tear and grafts) every object no other exported mesh claims.
		TArray<const TMap<FUvKey, int32>*> Sources;
		const FString Own = Entry->Name.ToLower();
		if (const TMap<FUvKey, int32>* Found = SurfaceOfCorner.Find(Own))
		{
			Sources.Add(Found);
		}
		else
		{
			for (const TPair<FString, TMap<FUvKey, int32>>& Object : SurfaceOfCorner)
			{
				const bool bClaimed = Export.Meshes.ContainsByPredicate([&](const FDTHMeshEntry& Other) { return Other.Name.ToLower() == Object.Key; });
				if (!bClaimed) Sources.Add(&Object.Value);
			}
		}

		// Each slot votes its corners onto Daz surfaces; a surface counts when it holds a real share of the slot.
		TMap<FString, TMap<int32, int32>> VotesOfSlot;
		int32 Corners = 0, Matched = 0;
		TArray<FUvKey> Keys;
		ForEachMesh(Scene->GetRootNode(), [&](FbxNode* Node, FbxMesh* Mesh)
		{
			for (int32 p = 0; p < Mesh->GetPolygonCount(); ++p)
			{
				const int32 m = PolygonMaterial(Mesh, p);
				FbxSurfaceMaterial* Material = m < Node->GetMaterialCount() ? Node->GetMaterial(m) : nullptr;
				CornerKeys(Mesh, p, Keys);
				for (const FUvKey& Key : Keys)
				{
					++Corners;
					for (const TMap<FUvKey, int32>* Source : Sources)
					{
						if (const int32* Surface = Source->Find(Key))
						{
							++Matched;
							if (Material && *Surface != INDEX_NONE) VotesOfSlot.FindOrAdd(UTF8_TO_TCHAR(Material->GetName())).FindOrAdd(*Surface)++;
							break;
						}
					}
				}
			}
		});
		Scene->Destroy();
		// Geometry whose UVs changed (re-unwrapped, baked onto new UVs) can't be traced back: say so instead of guessing.
		if (Corners == 0 || Matched < Corners * 0.8)
		{
			Result.Unmatched.Add(FString::Printf(TEXT("%s (%d of %d corners matched Daz's export by UV)"), *FPaths::GetBaseFilename(Entry->File), Matched, Corners));
			continue;
		}

		TMap<FString, TSet<int32>> SurfacesOfSlot;
		for (const TPair<FString, TMap<int32, int32>>& Votes : VotesOfSlot)
		{
			int32 Total = 0;
			for (const TPair<int32, int32>& Vote : Votes.Value) Total += Vote.Value;
			TSet<int32>& Slot = SurfacesOfSlot.Add(Votes.Key);
			for (const TPair<int32, int32>& Vote : Votes.Value)
			{
				// Shared UV positions on seams land on a neighbour now and then; 2% of a slot is a real surface.
				if (Vote.Value >= Total * 0.02) Slot.Add(Vote.Key);
			}
		}

		for (const TPair<FString, TSet<int32>>& Slot : SurfacesOfSlot)
		{
			++Result.SlotsChecked;
			// Baked: none of the slot's manifest textures is a file any of its surfaces uses.
			const FDTHMaterialSlot* Manifest = Entry->Materials.FindByPredicate([&](const FDTHMaterialSlot& S) { return S.Slot == Slot.Key; });
			if (Manifest && Manifest->Textures.Num() > 0)
			{
				bool bFromSurfaces = false;
				for (const TPair<FString, FString>& Texture : Manifest->Textures)
				{
					const FString File = FPaths::ConvertRelativePathToFull(Texture.Value).Replace(TEXT("\\"), TEXT("/")).ToLower();
					for (const int32 Surface : Slot.Value) bFromSurfaces |= Surfaces[Surface].Files.Contains(File);
				}
				if (!bFromSurfaces)
				{
					++Result.SlotsBaked;
					continue;
				}
			}
			TMap<FString, TArray<int32>> SurfacesBySet;
			for (const int32 Surface : Slot.Value) SurfacesBySet.FindOrAdd(Surfaces[Surface].Set).Add(Surface);
			if (SurfacesBySet.Num() < 2) continue;

			FDTHMergeProblem& Problem = Result.Problems.AddDefaulted_GetRef();
			Problem.Mesh = FPaths::GetBaseFilename(Entry->File);
			Problem.Slot = Slot.Key;
			for (const TPair<FString, TArray<int32>>& Set : SurfacesBySet)
			{
				TArray<FString> Labels, Files;
				for (const int32 Surface : Set.Value) Labels.Add(Surfaces[Surface].Label);
				for (const FString& File : Surfaces[Set.Value[0]].Files) Files.Add(ShortPath(File));
				Labels.Sort();
				Problem.Groups.Add(FString::Printf(TEXT("%s (%s)"), *FString::Join(Labels, TEXT(", ")),
					Files.Num() > 0 ? *FString::Join(Files, TEXT(", ")) : TEXT("no textures")));
			}
		}
	}
	return Result;
}
