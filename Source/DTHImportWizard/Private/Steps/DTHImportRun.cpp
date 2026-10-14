#include "Steps/DTHImportRun.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "DTHImportLog.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "FileHelpers.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Steps/DTHCharacterSetup.h"
#include "Steps/DTHControlRig.h"
#include "ControlRigBlueprintLegacy.h"
#include "Steps/DTHMeshImport.h"
#include "Steps/DTHSkeleton.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "DTHImportRun"

void FDTHImportReport::Info(const FText& Text)
{
	Messages.Add({ FDTHImportMessage::ESeverity::Info, Text });
	UE_LOG(LogDTHImport, Log, TEXT("%s"), *Text.ToString());
}

void FDTHImportReport::Warning(const FText& Text)
{
	Messages.Add({ FDTHImportMessage::ESeverity::Warning, Text });
	UE_LOG(LogDTHImport, Warning, TEXT("%s"), *Text.ToString());
}

void FDTHImportReport::Error(const FText& Text)
{
	Messages.Add({ FDTHImportMessage::ESeverity::Error, Text });
	bSucceeded = false;
	UE_LOG(LogDTHImport, Error, TEXT("%s"), *Text.ToString());
}

FDTHImportReport DTHImport::RunImport(const FDTHImportRequest& Request)
{
	FDTHImportReport Report;
	if (!Request.Export)
	{
		Report.Error(LOCTEXT("NoExport", "No export loaded."));
		return Report;
	}
	const FDTHExport& Export = *Request.Export;
	Report.Info(FText::Format(LOCTEXT("Start", "Importing {0} into {1}"), FText::FromString(Export.Character), FText::FromString(Request.DestinationFolder)));

	FString Error;
	USkeleton* Skeleton = Request.SkeletonOverride.IsEmpty()
		? LoadSkeletonFor(Export, Error)
		: LoadObject<USkeleton>(nullptr, *Request.SkeletonOverride, nullptr, LOAD_NoWarn);
	if (!Skeleton)
	{
		Report.Error(FText::FromString(Error.IsEmpty() ? TEXT("Skeleton not found: ") + Request.SkeletonOverride : Error));
		return Report;
	}

	// Steps: one per mesh, then blueprint, setup, retargeting, pose assets, save.
	FScopedSlowTask Task(Request.MeshIndices.Num() + 6, LOCTEXT("Task", "Importing DazToHue character"));
	Task.MakeDialog(true);

	TArray<USkeletalMesh*> Meshes;
	USkeletalMesh* Body = nullptr;
	for (const int32 MeshIndex : Request.MeshIndices)
	{
		if (Task.ShouldCancel())
		{
			Report.Warning(LOCTEXT("Cancelled", "Cancelled; nothing after this point was done."));
			return Report;
		}
		const FDTHMeshEntry& Entry = Export.Meshes[MeshIndex];
		Task.EnterProgressFrame(1.f, FText::FromString(FPaths::GetCleanFilename(Entry.File)));

		const FString AssetPath = Request.DestinationFolder / FPaths::GetBaseFilename(Entry.File);
		if (!Request.bReimportExistingMeshes)
		{
			if (USkeletalMesh* Existing = LoadObject<USkeletalMesh>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Report.Info(FText::Format(LOCTEXT("MeshReused", "{0} exists; reused (tick Reimport to replace it)."), FText::FromString(AssetPath)));
				Meshes.Add(Existing);
				Body = Entry.IsBody() ? Existing : Body;
				continue;
			}
		}
		FString MeshError;
		if (USkeletalMesh* Mesh = ImportSkeletalMesh(Entry.File, Request.DestinationFolder, Skeleton, MeshError))
		{
			Report.Info(FText::Format(LOCTEXT("MeshImported", "Imported {0}: {1} material slots, {2} morph targets."),
				FText::FromString(Mesh->GetPathName()), Mesh->GetMaterials().Num(), Mesh->GetMorphTargets().Num()));
			Meshes.Add(Mesh);
			Body = Entry.IsBody() ? Mesh : Body;
		}
		else
		{
			Report.Error(FText::FromString(MeshError));
		}
	}

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskAbp", "Post-process blueprint"));
	UAnimBlueprint* PostProcess = PreparePostProcessBlueprint(Export, Skeleton, Request.DestinationFolder, Report);

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskSetup", "Mesh setup and curves"));
	for (USkeletalMesh* Mesh : Meshes)
	{
		SetUpMesh(Export, Mesh, PostProcess, Report);
		RegisterCurves(Export, Mesh, Report);
	}

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskRetarget", "Retargeting"));
	ApplyRetargeting(Export, Skeleton, Report);

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskPoses", "Pose assets"));
	BuildPoseAssets(Export, Skeleton, Request.DestinationFolder, Report);

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskRig", "Control rig"));
	UControlRigBlueprint* Rig = nullptr;
	if (Body)
	{
		Rig = BuildControlRig(Export, Body, Request.DestinationFolder, Report);
	}
	else
	{
		Report.Warning(LOCTEXT("NoBody", "No body mesh among the imported meshes; control rig skipped."));
	}

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskSave", "Saving"));
	TArray<UPackage*> Packages;
	Packages.Add(Skeleton->GetPackage());
	if (PostProcess)
	{
		Packages.Add(PostProcess->GetPackage());
	}
	for (USkeletalMesh* Mesh : Meshes)
	{
		Packages.Add(Mesh->GetPackage());
	}
	if (Rig)
	{
		Packages.Add(Rig->GetPackage());
	}
	// Plus anything new under the destination (pose assets, Interchange's import data). Nothing else the user has open.
	const FString Prefix = Request.DestinationFolder / TEXT("");
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		if (It->IsDirty() && It->GetName().StartsWith(Prefix))
		{
			Packages.AddUnique(*It);
		}
	}
	UEditorLoadingAndSavingUtils::SavePackages(Packages, true);

	Report.Info(Report.bSucceeded ? LOCTEXT("Done", "Done.") : LOCTEXT("DoneErrors", "Finished with errors (see above)."));
	return Report;
}

#undef LOCTEXT_NAMESPACE
