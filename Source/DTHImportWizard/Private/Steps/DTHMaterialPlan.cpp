#include "Steps/DTHMaterialPlan.h"

#include "Export/DTHExport.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* const TextureExtensions[] = { TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg"), TEXT(".tif"), TEXT(".tiff"), TEXT(".tga"), TEXT(".bmp"), TEXT(".exr"), TEXT(".psd") };

	/** What a texture is. Each role has an import kind and a suffix for the default asset name. */
	const TCHAR* const Roles[] = { TEXT("colour"), TEXT("normal"), TEXT("roughness"), TEXT("specular"), TEXT("scattering"),
		TEXT("metallic"), TEXT("opacity"), TEXT("ao"), TEXT("height") };

	const TMap<FString, FString>& RoleSuffix()
	{
		static const TMap<FString, FString> Map = {
			{ TEXT("colour"), TEXT("Color") }, { TEXT("normal"), TEXT("NM") }, { TEXT("roughness"), TEXT("R") },
			{ TEXT("specular"), TEXT("Spec") }, { TEXT("scattering"), TEXT("SSS") }, { TEXT("metallic"), TEXT("Metal") },
			{ TEXT("opacity"), TEXT("Opacity") }, { TEXT("ao"), TEXT("AO") }, { TEXT("height"), TEXT("Height") } };
		return Map;
	}

	EDTHTextureKind RoleKind(const FString& Role)
	{
		if (Role == TEXT("colour") || Role == TEXT("scattering")) return EDTHTextureKind::Color;
		if (Role == TEXT("normal")) return EDTHTextureKind::Normal;
		return EDTHTextureKind::Grayscale;
	}

	/** File-name tokens that say what a texture is. */
	const TMap<FString, FString>& TokenRole()
	{
		static const TMap<FString, FString> Map = {
			{ TEXT("d"), TEXT("colour") }, { TEXT("diffuse"), TEXT("colour") }, { TEXT("col"), TEXT("colour") },
			{ TEXT("color"), TEXT("colour") }, { TEXT("colour"), TEXT("colour") }, { TEXT("basecolor"), TEXT("colour") },
			{ TEXT("albedo"), TEXT("colour") },
			{ TEXT("nm"), TEXT("normal") }, { TEXT("n"), TEXT("normal") }, { TEXT("normal"), TEXT("normal") },
			{ TEXT("normalgl"), TEXT("normal") }, { TEXT("nrm"), TEXT("normal") }, { TEXT("normalmap"), TEXT("normal") },
			{ TEXT("r"), TEXT("roughness") }, { TEXT("rough"), TEXT("roughness") }, { TEXT("roughness"), TEXT("roughness") },
			{ TEXT("spec"), TEXT("specular") }, { TEXT("specular"), TEXT("specular") }, { TEXT("specularity"), TEXT("specular") },
			{ TEXT("sss"), TEXT("scattering") }, { TEXT("translucency"), TEXT("scattering") }, { TEXT("trans"), TEXT("scattering") },
			{ TEXT("m"), TEXT("metallic") }, { TEXT("metal"), TEXT("metallic") }, { TEXT("metallic"), TEXT("metallic") },
			{ TEXT("metalness"), TEXT("metallic") },
			{ TEXT("o"), TEXT("opacity") }, { TEXT("opacity"), TEXT("opacity") }, { TEXT("cutout"), TEXT("opacity") },
			{ TEXT("alpha"), TEXT("opacity") },
			{ TEXT("ao"), TEXT("ao") }, { TEXT("occlusion"), TEXT("ao") },
			{ TEXT("b"), TEXT("height") }, { TEXT("bump"), TEXT("height") }, { TEXT("height"), TEXT("height") },
			{ TEXT("displacement"), TEXT("height") }, { TEXT("disp"), TEXT("height") } };
		return Map;
	}

	/** The parent family's texture parameter for each role (read from the DTH master instances in UE 5.8). */
	const TMap<FString, FString>& RoleParameters(const FString& Parent)
	{
		static const TMap<FString, FString> Skin = {
			{ TEXT("colour"), TEXT("Base Colour Texture") }, { TEXT("normal"), TEXT("Base Normal Texture") },
			{ TEXT("roughness"), TEXT("Base Roughness Texture") }, { TEXT("specular"), TEXT("Base Specular Texture") },
			{ TEXT("scattering"), TEXT("Base Scattering Texture") }, { TEXT("metallic"), TEXT("Base Metallic Texture") },
			{ TEXT("ao"), TEXT("Base Ambient Occlusion Texture") } };
		static const TMap<FString, FString> Cloth = {
			{ TEXT("colour"), TEXT("Base Colour Texture") }, { TEXT("normal"), TEXT("Normal Texture") },
			{ TEXT("roughness"), TEXT("Roughness Texture") }, { TEXT("specular"), TEXT("Specular Texture") },
			{ TEXT("metallic"), TEXT("Metallic Texture") } };
		static const TMap<FString, FString> Base = {
			{ TEXT("colour"), TEXT("Base Colour Texture") }, { TEXT("normal"), TEXT("Base Normal Texture") },
			{ TEXT("roughness"), TEXT("Base Roughness Texture") }, { TEXT("specular"), TEXT("Base Specular Texture") },
			{ TEXT("metallic"), TEXT("Base Metallic Texture") }, { TEXT("opacity"), TEXT("Base Opacity Texture") } };
		static const TMap<FString, FString> Hair = { { TEXT("colour"), TEXT("Root Colour Texture") } };
		static const TMap<FString, FString> None;
		if (Parent == TEXT("Skin")) return Skin;
		if (Parent == TEXT("Cloth")) return Cloth;
		if (Parent == TEXT("Metal") || Parent == TEXT("Plastic") || Parent == TEXT("Glass")) return Base;
		if (Parent == TEXT("Hair")) return Hair;
		return None;
	}

	FString ParentForType(const FString& Type)
	{
		static const TMap<FString, FString> Map = {
			{ TEXT("Skin"), TEXT("Skin") }, { TEXT("Cloth"), TEXT("Cloth") }, { TEXT("Eye"), TEXT("Eye") },
			{ TEXT("EyeOcclusion"), TEXT("EyeOcclusion") }, { TEXT("Tear"), TEXT("Tear") }, { TEXT("Metal"), TEXT("Metal") },
			{ TEXT("Plastic"), TEXT("Plastic") }, { TEXT("Glass"), TEXT("Glass") }, { TEXT("Hair"), TEXT("Hair") } };
		const FString* Found = Map.Find(Type);
		return Found ? *Found : FString(TEXT("Cloth"));
	}

	/** DTH types most clothing as Skin; only slots named after a body part really are skin. */
	bool IsBodyPart(const FString& ShortSlot)
	{
		static const TCHAR* const Parts[] = { TEXT("head"), TEXT("body"), TEXT("arms"), TEXT("legs"), TEXT("nails"), TEXT("mouth"),
			TEXT("genitalia"), TEXT("face"), TEXT("torso"), TEXT("lips"), TEXT("ears"), TEXT("anatomy"), TEXT("gp9"), TEXT("dk9") };
		const FString Lower = ShortSlot.ToLower();
		for (const TCHAR* Part : Parts)
		{
			if (Lower.Contains(Part)) return true;
		}
		return false;
	}

	FString Sanitize(const FString& Name)
	{
		FString Out;
		for (const TCHAR Char : Name)
		{
			Out.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
		}
		return Out;
	}

	TArray<FString> StemTokens(const FString& Path)
	{
		FString Stem = FPaths::GetBaseFilename(Path).ToLower();
		for (TCHAR& Char : Stem)
		{
			if (Char == TEXT('-') || Char == TEXT(' ') || Char == TEXT('.')) Char = TEXT('_');
		}
		TArray<FString> Tokens;
		Stem.ParseIntoArray(Tokens, TEXT("_"), true);
		return Tokens;
	}

	/** A texture's role from its file name, or empty. */
	FString RoleFromFilename(const FString& Path)
	{
		const FString Stem = FPaths::GetBaseFilename(Path).ToLower();
		static const TPair<const TCHAR*, const TCHAR*> Phrases[] = {
			{ TEXT("base_color"), TEXT("colour") }, { TEXT("normal_opengl"), TEXT("normal") }, { TEXT("normal_directx"), TEXT("normal") } };
		for (const auto& Phrase : Phrases)
		{
			if (Stem.Contains(Phrase.Key)) return Phrase.Value;
		}
		const TArray<FString> Tokens = StemTokens(Path);
		// The last role-like token decides: 'g9feminine04_head_nm_1001' is a normal map.
		for (int32 i = Tokens.Num() - 1; i >= 0; --i)
		{
			if (const FString* Role = TokenRole().Find(Tokens[i])) return *Role;
		}
		static const TCHAR* const Keys[] = { TEXT("normal"), TEXT("rough"), TEXT("specular"), TEXT("metal"), TEXT("opacity"), TEXT("height"), TEXT("color"), TEXT("colour") };
		for (int32 i = Tokens.Num() - 1; i >= 0; --i)
		{
			for (const TCHAR* Key : Keys)
			{
				if (Tokens[i].Contains(Key)) return TokenRole()[Key];
			}
		}
		return FString();
	}

	/** OpenGL-style normal maps need their green channel flipped for Unreal; DirectX-style ones don't. */
	bool WantsGreenFlip(const FString& Path)
	{
		const FString Name = FPaths::GetCleanFilename(Path).ToLower();
		return Name.Contains(TEXT("opengl")) || Name.Contains(TEXT("normalgl")) || FPaths::GetBaseFilename(Name).EndsWith(TEXT("_gl"));
	}

	bool IsTextureFile(const FString& Name)
	{
		for (const TCHAR* Extension : TextureExtensions)
		{
			if (Name.EndsWith(Extension, ESearchCase::IgnoreCase)) return true;
		}
		return false;
	}

	class FPlanner
	{
	public:
		explicit FPlanner(const FDTHExport& InExport) : Export(InExport)
		{
			// The old wizard's only source, kept as the fallback: textures the Houdini export copied next to it.
			ExportTextures = Export.Folder / TEXT("Textures") / TEXT("Source");
			if (!IFileManager::Get().DirectoryExists(*ExportTextures))
			{
				ExportTextures = Export.Folder / TEXT("Textures");
			}
		}

		FString ShortSlot(const FString& Slot) const
		{
			FString Short = Slot;
			if (!Short.RemoveFromStart(TEXT("MI_") + Export.Character))
			{
				Short.RemoveFromStart(TEXT("MI_"));
			}
			return Sanitize(Short.IsEmpty() ? Slot : Short);
		}

		/** Texture files in Dir by lower-case name (cached). */
		const TMap<FString, FString>& FilesIn(const FString& Dir)
		{
			if (const TMap<FString, FString>* Cached = DirFiles.Find(Dir))
			{
				return *Cached;
			}
			TMap<FString, FString>& Files = DirFiles.Add(Dir);
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *(Dir / TEXT("*.*")), true, false);
			for (const FString& Name : Names)
			{
				if (IsTextureFile(Name)) Files.Add(Name.ToLower(), Dir / Name);
			}
			return Files;
		}

		/** The file for a manifest path: the path itself, else the same name in the export's Textures folder. */
		bool Resolve(const FString& ManifestPath, FString& OutFile, FString& OutProblem)
		{
			const FString Full = FPaths::ConvertRelativePathToFull(ManifestPath);
			if (FPaths::FileExists(Full))
			{
				OutFile = Full;
				return true;
			}
			if (const FString* InExport = FilesIn(ExportTextures).Find(FPaths::GetCleanFilename(ManifestPath).ToLower()))
			{
				OutFile = *InExport;
				return true;
			}
			OutProblem = FString::Printf(TEXT("%s is listed in the manifest but the file is missing (not at %s, nor in the export's Textures folder)."),
				*FPaths::GetCleanFilename(ManifestPath), *ManifestPath);
			return false;
		}

		/** Files that share every token with Path except one: the same texture set's other maps. */
		TArray<FString> Siblings(const FString& Path)
		{
			TArray<FString> Out;
			const TArray<FString> Tokens = StemTokens(Path);
			const FString Self = FPaths::GetCleanFilename(Path).ToLower();
			for (const TPair<FString, FString>& File : FilesIn(FPaths::GetPath(Path)))
			{
				if (File.Key == Self) continue;
				const TArray<FString> Other = StemTokens(File.Value);
				if (Other.Num() != Tokens.Num()) continue;
				int32 Differ = 0;
				for (int32 i = 0; i < Tokens.Num(); ++i)
				{
					Differ += Tokens[i] != Other[i] ? 1 : 0;
				}
				if (Differ == 1) Out.Add(File.Value);
			}
			Out.Sort();
			return Out;
		}

		struct FFound { FString Source; FString Why; bool bProblem = false; };

		/** Role -> file for one manifest material, with sibling maps added. */
		TMap<FString, FFound> Textures(const FDTHMaterialSlot& Material)
		{
			TMap<FString, FFound> Found;
			static const TCHAR* const ManifestRoles[] = { TEXT("colour"), TEXT("normal"), TEXT("roughness"), TEXT("specular"), TEXT("metallic"), TEXT("opacity") };
			for (const TCHAR* Role : ManifestRoles)
			{
				const FString* Path = Material.Textures.Find(Role);
				if (!Path || Path->IsEmpty()) continue;
				FFound Entry;
				FString Problem;
				if (Resolve(*Path, Entry.Source, Problem))
				{
					Entry.Why = FString::Printf(TEXT("DTH manifest lists it as \"%s\""), Role);
				}
				else
				{
					Entry.Why = Problem;
					Entry.bProblem = true;
				}
				Found.Add(Role, Entry);
			}
			// Maps the manifest doesn't carry (SSS, specular...) from the same texture sets.
			TArray<FString> Listed;
			for (const TPair<FString, FFound>& Entry : Found)
			{
				if (!Entry.Value.Source.IsEmpty()) Listed.Add(Entry.Value.Source);
			}
			for (const FString& Source : Listed)
			{
				for (const FString& Sibling : Siblings(Source))
				{
					const FString Role = RoleFromFilename(Sibling);
					if (!Role.IsEmpty() && !Found.Contains(Role))
					{
						FFound Entry;
						Entry.Source = Sibling;
						Entry.Why = FString::Printf(TEXT("not listed in the manifest: same texture set as %s, and its name says %s"),
							*FPaths::GetCleanFilename(Source), *Role);
						Found.Add(Role, Entry);
					}
				}
			}
			return Found;
		}

		const FDTHExport& Export;
		FString ExportTextures;
		TMap<FString, TMap<FString, FString>> DirFiles;
	};
}

FString DTHImport::ParentMaterialPath(const FString& Parent)
{
	static const TMap<FString, FString> Paths = {
		{ TEXT("Skin"), TEXT("Common/Materials/Skin/MI_DTH_Skin") },
		{ TEXT("Cloth"), TEXT("Common/Materials/Cloth/MI_DTH_Cloth") },
		{ TEXT("Metal"), TEXT("Common/Materials/Metal/MI_DTH_Metal") },
		{ TEXT("Plastic"), TEXT("Common/Materials/Plastic/MI_DTH_Plastic") },
		{ TEXT("Glass"), TEXT("Common/Materials/Glass/MI_DTH_Glass") },
		{ TEXT("Eye"), TEXT("Common/Materials/Eyes/MI_DTH_Eye") },
		{ TEXT("EyeOcclusion"), TEXT("Common/Materials/Eyes/MI_DTH_EyeOcclusion") },
		{ TEXT("Tear"), TEXT("Common/Materials/Eyes/MI_DTH_LacrimalFluid") },
		{ TEXT("Hair"), TEXT("Common/Materials/Hair/MI_DTH_Hair") } };
	const FString* Found = Paths.Find(Parent);
	return Found ? *Found : FString();
}

TArray<FDTHMaterialPlan> DTHImport::BuildMaterialPlan(const FDTHExport& Export, const TArray<const FDTHMeshEntry*>& Meshes)
{
	FPlanner Planner(Export);
	const FString Token = Sanitize(Export.Character);

	struct FGuess { const FDTHMaterialSlot* Material; FString Parent; TMap<FString, FPlanner::FFound> Found; };
	TArray<FGuess> Guesses;
	TSet<FString> SeenSlots;
	for (const FDTHMeshEntry* Mesh : Meshes)
	{
		for (const FDTHMaterialSlot& Material : Mesh->Materials)
		{
			if (Material.Slot.IsEmpty() || SeenSlots.Contains(Material.Slot)) continue;
			SeenSlots.Add(Material.Slot);
			FString Parent = ParentForType(Material.Type);
			if (Parent == TEXT("Skin") && !IsBodyPart(Planner.ShortSlot(Material.Slot)))
			{
				Parent = TEXT("Cloth");
			}
			Guesses.Add({ &Material, Parent, Planner.Textures(Material) });
		}
	}

	// Files used by 3+ slots (blank normals and the like) are named after the file, not a slot.
	TMap<FString, int32> Uses;
	for (const FGuess& Guess : Guesses)
	{
		for (const TPair<FString, FPlanner::FFound>& Entry : Guess.Found)
		{
			if (!Entry.Value.Source.IsEmpty()) Uses.FindOrAdd(Entry.Value.Source.ToLower())++;
		}
	}

	TMap<FString, FString> NameForSource; // lower-case source -> asset name, so a shared file imports once
	TMap<FString, FString> SourceForName; // asset name -> lower-case source, so two files never share a name
	TArray<FDTHMaterialPlan> Plans;
	for (const FGuess& Guess : Guesses)
	{
		FDTHMaterialPlan& Plan = Plans.AddDefaulted_GetRef();
		Plan.Slot = Guess.Material->Slot;
		Plan.Type = Guess.Material->Type;
		Plan.Parent = Guess.Parent;
		const FString Short = Planner.ShortSlot(Plan.Slot);
		const TMap<FString, FString>& Parameters = RoleParameters(Plan.Parent);
		for (const TCHAR* Role : Roles)
		{
			const FPlanner::FFound* Found = Guess.Found.Find(Role);
			const FString* Parameter = Parameters.Find(Role);
			if (!Found || !Parameter) continue;
			FDTHTextureRow& Row = Plan.Textures.AddDefaulted_GetRef();
			Row.Parameter = *Parameter;
			Row.Kind = RoleKind(Role);
			Row.Why = Found->Why;
			Row.bProblem = Found->bProblem;
			const FString SlotName = FString::Printf(TEXT("T_%s_%s_%s"), *Token, *Short, *RoleSuffix()[Role]);
			if (Found->Source.IsEmpty())
			{
				Row.AssetName = SlotName;
				continue;
			}
			Row.Source = Found->Source;
			Row.bFlipGreen = Row.Kind == EDTHTextureKind::Normal && WantsGreenFlip(Found->Source);
			const FString Key = Found->Source.ToLower();
			if (const FString* Existing = NameForSource.Find(Key))
			{
				Row.AssetName = *Existing;
				continue;
			}
			FString Name = Uses[Key] >= 3 ? FString::Printf(TEXT("T_%s_%s"), *Token, *Sanitize(FPaths::GetBaseFilename(Found->Source))) : SlotName;
			// A different file already took this name: give this one its own.
			for (int32 Suffix = 2; SourceForName.Contains(Name); ++Suffix)
			{
				Name = FString::Printf(TEXT("%s_%d"), *SlotName, Suffix);
			}
			NameForSource.Add(Key, Name);
			SourceForName.Add(Name, Key);
			Row.AssetName = Name;
		}
	}
	return Plans;
}
