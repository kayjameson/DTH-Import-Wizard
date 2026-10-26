#include "Steps/DTHBodyMatch.h"

#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "FbxImporter.h" // includes the FBX SDK with the engine's guards
#include "MeshDescription.h"
#include "Misc/Paths.h"

namespace
{
	/** What an FBX body is made of. */
	struct FBodyShape
	{
		int32 Vertices = 0;
		/** Rest-pose bounds of all mesh control points, extents sorted so axis conventions don't matter. */
		FVector Extents = FVector::ZeroVector;
		TSet<FString> Bones;
	};

	bool ReadFbx(const FString& File, FBodyShape& Out, FString& OutError)
	{
		FbxManager* Manager = FbxManager::Create();
		ON_SCOPE_EXIT { Manager->Destroy(); };
		FbxIOSettings* Settings = FbxIOSettings::Create(Manager, IOSROOT);
		// Geometry and skinning only: blend shapes and animation are most of a body file.
		Settings->SetBoolProp(IMP_FBX_SHAPE, false);
		Settings->SetBoolProp(IMP_FBX_ANIMATION, false);
		Settings->SetBoolProp(IMP_FBX_MATERIAL, false);
		Settings->SetBoolProp(IMP_FBX_TEXTURE, false);
		Manager->SetIOSettings(Settings);
		FbxImporter* Importer = FbxImporter::Create(Manager, "");
		if (!Importer->Initialize(TCHAR_TO_UTF8(*File), -1, Manager->GetIOSettings()))
		{
			OutError = FString::Printf(TEXT("could not open %s"), *File);
			return false;
		}
		FbxScene* Scene = FbxScene::Create(Manager, "");
		const bool bOk = Importer->Import(Scene);
		Importer->Destroy();
		if (!bOk)
		{
			OutError = FString::Printf(TEXT("could not read %s"), *File);
			return false;
		}
		FBox Box(ForceInit);
		TArray<FbxNode*> Nodes = { Scene->GetRootNode() };
		while (Nodes.Num() > 0)
		{
			FbxNode* Node = Nodes.Pop();
			for (int32 i = 0; i < Node->GetChildCount(); ++i) Nodes.Add(Node->GetChild(i));
			FbxMesh* Mesh = Node->GetMesh();
			if (!Mesh) continue;
			Out.Vertices += Mesh->GetControlPointsCount();
			const FbxVector4* Points = Mesh->GetControlPoints();
			for (int32 i = 0; i < Mesh->GetControlPointsCount(); ++i)
			{
				Box += FVector(Points[i][0], Points[i][1], Points[i][2]);
			}
			for (int32 d = 0; d < Mesh->GetDeformerCount(FbxDeformer::eSkin); ++d)
			{
				const FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(d, FbxDeformer::eSkin));
				for (int32 c = 0; c < Skin->GetClusterCount(); ++c)
				{
					if (const FbxNode* Link = Skin->GetCluster(c)->GetLink()) Out.Bones.Add(UTF8_TO_TCHAR(Link->GetName()));
				}
			}
		}
		if (Box.IsValid)
		{
			TArray<double> Sizes = { Box.GetSize().X, Box.GetSize().Y, Box.GetSize().Z };
			Sizes.Sort();
			Out.Extents = FVector(Sizes[0], Sizes[1], Sizes[2]);
		}
		return true;
	}

	/** Bones in A missing from B, at most a few, for the report. */
	FString Missing(const TSet<FString>& A, const TSet<FString>& B)
	{
		TArray<FString> Out;
		for (const FString& Bone : A)
		{
			if (!B.Contains(Bone)) Out.Add(Bone);
		}
		Out.Sort();
		const int32 Total = Out.Num();
		if (Out.Num() > 5) Out.SetNum(5);
		return FString::Join(Out, TEXT(", ")) + (Total > 5 ? FString::Printf(TEXT(" (+%d more)"), Total - 5) : FString());
	}
}

FDTHBodyMatch DTHImport::CompareBodies(const FString& ExportBodyFbx, const USkeletalMesh* CharacterBody)
{
	FDTHBodyMatch Match;
	FBodyShape Export;
	FString Error;
	if (!ReadFbx(ExportBodyFbx, Export, Error))
	{
		Match.Reason = Error;
		return Match;
	}
	const UAssetImportData* ImportData = CharacterBody->GetAssetImportData();
	const FString Source = ImportData ? ImportData->GetFirstFilename() : FString();
	if (!Source.IsEmpty() && FPaths::FileExists(Source))
	{
		FBodyShape Character;
		if (!ReadFbx(Source, Character, Error))
		{
			Match.Reason = Error;
			return Match;
		}
		Match.Compared = FString::Printf(TEXT("%s against %s (the file %s was imported from)"), *FPaths::GetCleanFilename(ExportBodyFbx), *Source, *CharacterBody->GetName());
		if (Export.Vertices != Character.Vertices)
		{
			Match.Reason = FString::Printf(TEXT("the export's body has %d vertices, the character's %d"), Export.Vertices, Character.Vertices);
		}
		else if (!Export.Extents.Equals(Character.Extents, FMath::Max(0.01, Character.Extents.GetMax() * 1e-4)))
		{
			Match.Reason = FString::Printf(TEXT("the bodies have different sizes (%s against %s)"), *Export.Extents.ToString(), *Character.Extents.ToString());
		}
		else if (!Export.Bones.Includes(Character.Bones) || !Character.Bones.Includes(Export.Bones))
		{
			Match.Reason = FString::Printf(TEXT("the bodies are skinned to different bones (only in the export: %s; only in the character: %s)"),
				*Missing(Export.Bones, Character.Bones), *Missing(Character.Bones, Export.Bones));
		}
		Match.bMatches = Match.Reason.IsEmpty();
		return Match;
	}

	// The character's source file is gone: vertex count and bones against the imported body.
	Match.Compared = FString::Printf(TEXT("%s against %s (its source file is gone; vertex count and bones only)"), *FPaths::GetCleanFilename(ExportBodyFbx), *CharacterBody->GetName());
	const FMeshDescription* Description = CharacterBody->GetMeshDescription(0);
	const int32 Vertices = Description ? Description->Vertices().Num() : -1;
	TSet<FString> Bones;
	for (const FMeshBoneInfo& Bone : CharacterBody->GetRefSkeleton().GetRefBoneInfo())
	{
		Bones.Add(Bone.Name.ToString());
	}
	if (Vertices != Export.Vertices)
	{
		Match.Reason = FString::Printf(TEXT("the export's body has %d vertices, the character's %d"), Export.Vertices, Vertices);
	}
	else if (!Bones.Includes(Export.Bones))
	{
		Match.Reason = FString::Printf(TEXT("the export's body is skinned to bones the character doesn't have: %s"), *Missing(Export.Bones, Bones));
	}
	Match.bMatches = Match.Reason.IsEmpty();
	return Match;
}
