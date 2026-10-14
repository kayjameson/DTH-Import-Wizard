#include "DTHImportScripting.h"

#include "DTHImportLog.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "Misc/Paths.h"
#include "Steps/DTHMeshImport.h"
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

TArray<FString> UDTHImportScripting::ImportMeshes(const FString& ExportFolder, const FString& DestinationFolder, bool bIncludeUnlisted)
{
	TArray<FString> Result;
	FString Error;
	const TSharedPtr<FDTHExport> Export = FDTHExport::Load(ExportFolder, Error);
	USkeleton* Skeleton = Export ? DTHImport::LoadSkeletonFor(*Export, Error) : nullptr;
	if (!Skeleton)
	{
		UE_LOG(LogDTHImport, Error, TEXT("%s"), *Error);
		Result.Add(TEXT("ERROR: ") + Error);
		return Result;
	}
	for (const FDTHMeshEntry& Mesh : Export->Meshes)
	{
		if (!Mesh.bInManifest && !bIncludeUnlisted)
		{
			continue;
		}
		FString MeshError;
		if (USkeletalMesh* Imported = DTHImport::ImportSkeletalMesh(Mesh.File, DestinationFolder, Skeleton, MeshError))
		{
			Result.Add(Imported->GetPathName());
		}
		else
		{
			UE_LOG(LogDTHImport, Error, TEXT("%s"), *MeshError);
			Result.Add(TEXT("ERROR: ") + MeshError);
		}
	}
	return Result;
}
