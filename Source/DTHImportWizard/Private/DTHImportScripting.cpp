#include "DTHImportScripting.h"

#include "Animation/AnimCurveMetadata.h"
#include "Animation/MeshDeformer.h"
#include "Animation/PoseAsset.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "Misc/Paths.h"
#include "Steps/DTHImportRun.h"
#include "Steps/DTHMergeCheck.h"
#include "Steps/DTHSkeleton.h"

TArray<FString> UDTHImportScripting::DescribeExport(const FString& ExportFolder)
{
	TArray<FString> Lines;
	FString Error;
	const TSharedPtr<FDTHExport> Export = FDTHExport::Load(ExportFolder, Error);
	if (!Export)
	{
		Lines.Add(TEXT("ERROR: ") + Error);
		return Lines;
	}
	Lines.Add(FString::Printf(TEXT("Character: %s (DTH %s)"), *Export->Character, *Export->DthVersion));
	Lines.Add(FString::Printf(TEXT("Manifest: %s"), *Export->ManifestFile));
	Lines.Add(FString::Printf(TEXT("Figure: %s, target skeleton: %s, skinning: %s"), *Export->SourceSkeleton, *Export->TargetSkeleton, *Export->SkinningMethod));
	FString SkeletonError;
	const FString Skeleton = DTHImport::SkeletonPathFor(*Export, SkeletonError);
	Lines.Add(Skeleton.IsEmpty() ? TEXT("ERROR: ") + SkeletonError : TEXT("Imports onto: ") + Skeleton);
	for (const FDTHMeshEntry& Mesh : Export->Meshes)
	{
		Lines.Add(FString::Printf(TEXT("Mesh: %s [%s] %d material slots%s%s"), *FPaths::GetCleanFilename(Mesh.File),
			Mesh.bInManifest ? *Mesh.Name : TEXT("not in manifest"), Mesh.Materials.Num(),
			Mesh.IsBody() ? TEXT(", body") : TEXT(""), FPaths::FileExists(Mesh.File) ? TEXT("") : TEXT(", FILE MISSING")));
	}
	for (const FDTHPoseAssetEntry& Pose : Export->PoseAssets)
	{
		Lines.Add(FString::Printf(TEXT("Pose asset: %s %s"), *Pose.Category, *FPaths::GetCleanFilename(Pose.AnimationFile)));
	}
	for (const FDTHCurveFile& Curve : Export->CurveFiles)
	{
		Lines.Add(FString::Printf(TEXT("Curve file: %s %s"), *Curve.Category, *FPaths::GetCleanFilename(Curve.File)));
	}
	return Lines;
}

TArray<FString> UDTHImportScripting::CheckMaterialMerges(const FString& ExportFolder, const FString& DazExportFbx)
{
	TArray<FString> Lines;
	FString Error;
	const TSharedPtr<FDTHExport> Export = FDTHExport::Load(ExportFolder, Error);
	if (!Export)
	{
		Lines.Add(TEXT("ERROR: ") + Error);
		return Lines;
	}
	TArray<const FDTHMeshEntry*> Meshes;
	for (const FDTHMeshEntry& Mesh : Export->Meshes)
	{
		if (Mesh.bInManifest) Meshes.Add(&Mesh);
	}
	FDTHImportReport Report;
	DTHImport::ReportMaterialMerges(DTHImport::CheckMaterialMerges(*Export, Meshes, DazExportFbx), Report);
	for (const FDTHImportMessage& Message : Report.Messages)
	{
		Lines.Add((Message.Severity == FDTHImportMessage::ESeverity::Warning ? TEXT("WARNING: ") : TEXT("")) + Message.Text.ToString());
	}
	return Lines;
}

TArray<FString> UDTHImportScripting::ImportCharacter(const FString& ExportFolder, const FString& DestinationFolder, bool bIncludeUnlisted,
	bool bReimportExistingMeshes, const FString& SkeletonOverride)
{
	TArray<FString> Result;
	FString Error;
	FDTHImportRequest Request;
	Request.Export = FDTHExport::Load(ExportFolder, Error);
	if (!Request.Export)
	{
		Result.Add(TEXT("ERROR: ") + Error);
		return Result;
	}
	Request.DestinationFolder = DestinationFolder;
	Request.bReimportExistingMeshes = bReimportExistingMeshes;
	Request.SkeletonOverride = SkeletonOverride;
	for (int32 Index = 0; Index < Request.Export->Meshes.Num(); ++Index)
	{
		if (Request.Export->Meshes[Index].bInManifest || bIncludeUnlisted)
		{
			Request.MeshIndices.Add(Index);
		}
	}
	const FDTHImportReport Report = DTHImport::RunImport(Request);
	for (const FDTHImportMessage& Message : Report.Messages)
	{
		const TCHAR* Prefix = Message.Severity == FDTHImportMessage::ESeverity::Error ? TEXT("ERROR: ")
			: Message.Severity == FDTHImportMessage::ESeverity::Warning ? TEXT("WARNING: ") : TEXT("");
		Result.Add(Prefix + Message.Text.ToString());
	}
	return Result;
}

TArray<FString> UDTHImportScripting::DescribeCharacter(const FString& SkeletalMeshPath, const FString& PoseFolder)
{
	TArray<FString> Lines;
	const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath, nullptr, LOAD_NoWarn);
	if (!Mesh)
	{
		Lines.Add(TEXT("ERROR: not a skeletal mesh: ") + SkeletalMeshPath);
		return Lines;
	}
	const UClass* PostProcess = Mesh->GetPostProcessAnimBlueprint().Get();
	Lines.Add(TEXT("PostProcess: ") + (PostProcess ? PostProcess->GetPathName() : FString(TEXT("none"))));
	const UMeshDeformer* Deformer = Mesh->GetDefaultMeshDeformer();
	Lines.Add(TEXT("Deformer: ") + (Deformer ? Deformer->GetPathName() : FString(TEXT("none"))));

	if (const UAnimCurveMetaData* MetaData = const_cast<USkeletalMesh*>(Mesh)->GetAssetUserData<UAnimCurveMetaData>())
	{
		TArray<FName> Names;
		MetaData->GetCurveMetaDataNames(Names);
		int32 Morph = 0;
		int32 Material = 0;
		for (const FName Name : Names)
		{
			if (const FCurveMetaData* Curve = MetaData->GetCurveMetaData(Name))
			{
				Morph += Curve->Type.bMorphtarget ? 1 : 0;
				Material += Curve->Type.bMaterial ? 1 : 0;
			}
		}
		Lines.Add(FString::Printf(TEXT("MeshCurves: %d (morph %d, material %d)"), Names.Num(), Morph, Material));
	}
	else
	{
		Lines.Add(TEXT("MeshCurves: none"));
	}

	if (const USkeleton* Skeleton = Mesh->GetSkeleton())
	{
		Lines.Add(TEXT("Skeleton: ") + Skeleton->GetPathName());
		const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
		const UEnum* ModeEnum = StaticEnum<EBoneTranslationRetargetingMode::Type>();
		TMap<FString, int32> Counts;
		const int32 Pelvis = Reference.FindBoneIndex(TEXT("pelvis"));
		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			const bool bUnderPelvis = Pelvis != INDEX_NONE && Index != Pelvis && Reference.BoneIsChildOf(Index, Pelvis);
			if (bUnderPelvis)
			{
				Counts.FindOrAdd(ModeEnum->GetNameStringByValue(Skeleton->GetBoneTranslationRetargetingMode(Index)))++;
			}
		}
		FString Summary;
		for (const TPair<FString, int32>& Pair : Counts)
		{
			Summary += FString::Printf(TEXT(" %s=%d"), *Pair.Key, Pair.Value);
		}
		Lines.Add(TEXT("RetargetBelowPelvis:") + Summary);
		for (const TCHAR* Bone : { TEXT("pelvis"), TEXT("clavicle_pec_l"), TEXT("clavicle_pec_r") })
		{
			const int32 Index = Reference.FindBoneIndex(Bone);
			Lines.Add(FString::Printf(TEXT("Retarget %s: %s"), Bone,
				Index == INDEX_NONE ? TEXT("(no bone)") : *ModeEnum->GetNameStringByValue(Skeleton->GetBoneTranslationRetargetingMode(Index))));
		}
	}

	if (!PoseFolder.IsEmpty())
	{
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetsByPath(FName(*PoseFolder), Assets, true);
		for (const FAssetData& Data : Assets)
		{
			if (const UPoseAsset* Pose = Cast<UPoseAsset>(Data.GetAsset()))
			{
				const TArray<FName>& Names = Pose->GetPoseFNames();
				Lines.Add(FString::Printf(TEXT("PoseAsset %s: %d poses, additive %s, base pose %d, first %s, skeleton %s"),
					*Pose->GetName(), Pose->GetNumPoses(), Pose->IsValidAdditive() ? TEXT("yes") : TEXT("no"), Pose->GetBasePoseIndex(),
					Names.Num() > 0 ? *Names[0].ToString() : TEXT("-"), Pose->GetSkeleton() ? *Pose->GetSkeleton()->GetName() : TEXT("-")));
			}
		}
	}
	return Lines;
}
