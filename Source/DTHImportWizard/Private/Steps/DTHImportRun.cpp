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
#include "Materials/MaterialInstanceConstant.h"
#include "Steps/DTHMaterials.h"
#include "Steps/DTHMergeCheck.h"
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

void DTHImport::ReportMaterialMerges(const FDTHMergeCheck& Check, FDTHImportReport& Report)
{
	if (!Check.Skipped.IsEmpty())
	{
		Report.Warning(FText::FromString(Check.Skipped));
		return;
	}
	for (const FString& Mesh : Check.Unmatched)
	{
		Report.Warning(FText::Format(LOCTEXT("MergeUnmatched", "Material merge check: {0} could not be traced back to Daz's export, so its slots were not checked."), FText::FromString(Mesh)));
	}
	for (const FDTHMergeProblem& Problem : Check.Problems)
	{
		Report.Warning(FText::FromString(Problem.Describe()));
	}
	Report.Info(FText::Format(LOCTEXT("MergeChecked", "Material merge check against {0}: {1} slots checked, {2} baked (skipped), {3} with mismatched textures."),
		FText::FromString(FPaths::GetCleanFilename(Check.DazFbx)), Check.SlotsChecked, Check.SlotsBaked, Check.Problems.Num()));
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

	// Steps: materials, one per mesh, then blueprint, setup, retargeting, pose assets, control rig, save.
	FScopedSlowTask Task(Request.MeshIndices.Num() + 7, LOCTEXT("Task", "Importing DazToHue character"));
	Task.MakeDialog(true);

	// Before the meshes, so their slots find the materials by name (DTH importer: process_fbx_materials ahead of each
	// FBX); assigned explicitly again after them, so reused meshes get them too.
	Task.EnterProgressFrame(1.f, LOCTEXT("TaskMaterials", "Materials and textures"));
	TMap<FString, UMaterialInstanceConstant*> Materials;
	{
		TArray<const FDTHMeshEntry*> MaterialMeshes;
		for (const int32 MeshIndex : Request.MeshIndices)
		{
			MaterialMeshes.Add(&Export.Meshes[MeshIndex]);
		}
		ReportMaterialMerges(CheckMaterialMerges(Export, MaterialMeshes), Report);
		Materials = BuildMaterials(Export, BuildMaterialPlan(Export, MaterialMeshes), Request.DestinationFolder, Report);
	}

	TArray<USkeletalMesh*> Meshes;
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
				continue;
			}
		}
		FString MeshError;
		if (USkeletalMesh* Mesh = ImportSkeletalMesh(Entry.File, Request.DestinationFolder, Skeleton, MeshError))
		{
			Report.Info(FText::Format(LOCTEXT("MeshImported", "Imported {0}: {1} material slots, {2} morph targets."),
				FText::FromString(Mesh->GetPathName()), Mesh->GetMaterials().Num(), Mesh->GetMorphTargets().Num()));
			Meshes.Add(Mesh);
		}
		else
		{
			// Every later step assumes each mesh is on the import skeleton.
			Report.Error(FText::FromString(MeshError));
			Report.Error(LOCTEXT("StoppedMesh", "Import stopped; nothing after this point was done."));
			return Report;
		}
	}

	AssignMaterials(Meshes, Materials, Report);

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskAbp", "Post-process blueprint"));
	UAnimBlueprint* PostProcess = PreparePostProcessBlueprint(Export, Skeleton, Request.DestinationFolder, Report);
	if (!PostProcess)
	{
		Report.Error(LOCTEXT("StoppedAbp", "Import stopped; nothing after this point was done."));
		return Report;
	}

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskSetup", "Mesh setup and curves"));
	for (USkeletalMesh* Mesh : Meshes)
	{
		SetUpMesh(Export, Mesh, PostProcess, Report);
		RegisterCurves(Export, Mesh, Report);
	}

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskRetarget", "Retargeting"));
	ApplyRetargeting(Export, Skeleton, Report);

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskPoses", "Pose assets"));
	BuildPoseAssets(Export, Skeleton, Request.DestinationFolder, PostProcess, Report);

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskRig", "Control rig"));
	// On the first mesh, whatever its name, as the DTH importer builds MCR_<Character>.
	UControlRigBlueprint* Rig = nullptr;
	if (Meshes.Num() > 0)
	{
		Rig = BuildControlRig(Export, Meshes[0], Request.DestinationFolder, Report);
		if (!Rig)
		{
			Report.Error(LOCTEXT("NoRig", "The control rig was not built (see above)."));
		}
	}
	else
	{
		Report.Error(LOCTEXT("NoMeshes", "No meshes were imported, so there is nothing to build the control rig on."));
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
