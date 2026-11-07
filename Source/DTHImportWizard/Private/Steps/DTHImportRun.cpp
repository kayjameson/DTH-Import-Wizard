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
#include "Steps/DTHBodyMatch.h"
#include "Steps/DTHJigglePhysics.h"
#include "Steps/DTHPathCheck.h"
#include "Steps/DTHWardrobeBuild.h"
#include "DTHWardrobeTypes.h"
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
	const bool bAddOn = Request.bAddToCharacter;
	Report.Info(FText::Format(bAddOn ? LOCTEXT("StartAddOn", "Adding {0}'s garments and grooms to the character in {1}") : LOCTEXT("Start", "Importing {0} into {1}"),
		FText::FromString(Export.Character), FText::FromString(Request.DestinationFolder)));

	FString Error;
	USkeleton* Skeleton = Request.SkeletonOverride.IsEmpty()
		? LoadSkeletonFor(Export, Error)
		: LoadObject<USkeleton>(nullptr, *Request.SkeletonOverride, nullptr, LOAD_NoWarn);
	if (!Skeleton)
	{
		Report.Error(FText::FromString(Error.IsEmpty() ? TEXT("Skeleton not found: ") + Request.SkeletonOverride : Error));
		return Report;
	}

	// Before anything is made: Unreal can't replace an asset with one of another class, and stops the editor when asked to.
	{
		const TArray<FDTHPathClash> Clashes = FindPathClashes(Request);
		const TArray<FDTHPathClash> OldWardrobe = Clashes.FilterByPredicate([](const FDTHPathClash& Clash) { return Clash.bOldWardrobe; });
		const TArray<FDTHPathClash> Others = Clashes.FilterByPredicate([](const FDTHPathClash& Clash) { return !Clash.bOldWardrobe; });
		for (const FDTHPathClash& Clash : Others)
		{
			Report.Error(FText::Format(LOCTEXT("PathClash", "{0} is a {1}; the import puts a {2} there. Move or rename it."),
				FText::FromString(Clash.Path), FText::FromString(Clash.Found), FText::FromString(Clash.Expected)));
		}
		if (!OldWardrobe.IsEmpty() && !Request.bDeleteOldWardrobe)
		{
			for (const FDTHPathClash& Clash : OldWardrobe)
			{
				Report.Error(FText::Format(LOCTEXT("OldWardrobeAsset", "{0} ({1}) is not a DTH wardrobe asset."), FText::FromString(Clash.Path), FText::FromString(Clash.Found)));
			}
			Report.Error(FText::Format(LOCTEXT("OldWardrobe", "{0}/Wardrobe holds an older wardrobe ({1} assets above, e.g. from the Python wizard). Move it out of that folder, or tick \"Delete the old wardrobe\" to have the import delete it."),
				FText::FromString(Request.DestinationFolder), OldWardrobe.Num()));
		}
		if (!Others.IsEmpty() || (!OldWardrobe.IsEmpty() && !Request.bDeleteOldWardrobe))
		{
			Report.Error(LOCTEXT("StoppedClash", "Nothing was imported."));
			return Report;
		}
		if (!OldWardrobe.IsEmpty() && !DeleteOldWardrobe(OldWardrobe, Report))
		{
			Report.Error(LOCTEXT("StoppedOldWardrobe", "Nothing was imported."));
			return Report;
		}
	}

	// An add-on export (another outfit or hair for the same character) goes on the character's existing body: its own
	// body is only compared with it, never imported, and the body's steps (retargeting, pose assets, control rig) are
	// left as the base import made them.
	TArray<int32> MeshIndices = Request.MeshIndices;
	USkeletalMesh* Body = nullptr;
	if (bAddOn)
	{
		const UDTHWardrobe* Wardrobe = FindWardrobe(Request.DestinationFolder);
		const FDTHMeshEntry* ExportBody = Export.FindBody();
		Body = Wardrobe ? Wardrobe->Body.Get() : nullptr;
		if (!Body && ExportBody)
		{
			const FString BodyPath = Request.DestinationFolder / FPaths::GetBaseFilename(ExportBody->File);
			Body = LoadObject<USkeletalMesh>(nullptr, *BodyPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (!Body)
		{
			Report.Error(FText::Format(LOCTEXT("AddOnNoBody", "{0} has no character to add to (no wardrobe and no body mesh). Import the character there first, without \"Add to an existing character\"."),
				FText::FromString(Request.DestinationFolder)));
			return Report;
		}
		if (!ExportBody)
		{
			Report.Warning(LOCTEXT("AddOnNoExportBody", "The export has no body mesh, so it could not be checked against the character's body."));
		}
		else
		{
			const FDTHBodyMatch Match = CompareBodies(ExportBody->File, Body);
			if (!Match.bMatches)
			{
				Report.Error(FText::Format(LOCTEXT("AddOnBodyDiffers", "This export's body is not {0}'s body: {1}. Its garments and grooms were made for a different body, so nothing was imported. Export the outfit from a Houdini file with the same character, at the same subdivision level, without the occlusion node."),
					FText::FromString(Body->GetName()), FText::FromString(Match.Reason)));
				if (!Match.Compared.IsEmpty()) Report.Info(FText::FromString(TEXT("Compared ") + Match.Compared + TEXT(".")));
				return Report;
			}
			Report.Info(FText::Format(LOCTEXT("AddOnBodyMatches", "The export's body matches the character's: compared {0}."), FText::FromString(Match.Compared)));
		}
		MeshIndices.RemoveAll([&Export](int32 Index) { return Export.Meshes[Index].IsBody(); });
	}

	// Steps: materials, one per mesh, then blueprint, setup, retargeting, pose assets, control rig, grooms, wardrobe, save.
	FScopedSlowTask Task(MeshIndices.Num() + 9, LOCTEXT("Task", "Importing DazToHue character"));
	Task.MakeDialog(true);

	// Before the meshes, so their slots find the materials by name (DTH importer: process_fbx_materials ahead of each
	// FBX); assigned explicitly again after them, so reused meshes get them too.
	Task.EnterProgressFrame(1.f, LOCTEXT("TaskMaterials", "Materials and textures"));
	TMap<FString, UMaterialInstanceConstant*> Materials;
	{
		TArray<const FDTHMeshEntry*> MaterialMeshes;
		for (const int32 MeshIndex : MeshIndices)
		{
			MaterialMeshes.Add(&Export.Meshes[MeshIndex]);
		}
		ReportMaterialMerges(CheckMaterialMerges(Export, MaterialMeshes), Report);
		TArray<FDTHMaterialPlan> Plans;
		if (Request.MaterialPlans.IsSet())
		{
			// The wizard's edited plan, for the slots of the meshes being imported.
			TSet<FString> Slots;
			for (const FDTHMeshEntry* Mesh : MaterialMeshes)
			{
				for (const FDTHMaterialSlot& Slot : Mesh->Materials) Slots.Add(Slot.Slot);
			}
			Plans = Request.MaterialPlans.GetValue().FilterByPredicate([&](const FDTHMaterialPlan& Plan) { return Slots.Contains(Plan.Slot); });
		}
		else
		{
			Plans = BuildMaterialPlan(Export, MaterialMeshes);
		}
		Materials = BuildMaterials(Export, Plans, Request.DestinationFolder, Report);
	}

	TArray<USkeletalMesh*> Meshes;
	for (const int32 MeshIndex : MeshIndices)
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
				Body = Entry.bInManifest && Entry.IsBody() ? Existing : Body;
				continue;
			}
		}
		FString MeshError;
		if (USkeletalMesh* Mesh = ImportSkeletalMesh(Entry.File, Request.DestinationFolder, Skeleton, MeshError))
		{
			Report.Info(FText::Format(LOCTEXT("MeshImported", "Imported {0}: {1} material slots, {2} morph targets."),
				FText::FromString(Mesh->GetPathName()), Mesh->GetMaterials().Num(), Mesh->GetMorphTargets().Num()));
			Meshes.Add(Mesh);
			Body = Entry.bInManifest && Entry.IsBody() ? Mesh : Body;
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

	UControlRigBlueprint* Rig = nullptr;
	if (bAddOn)
	{
		Task.EnterProgressFrame(3.f, LOCTEXT("TaskBodySteps", "Body steps"));
		Report.Info(LOCTEXT("AddOnBodySteps", "Retargeting, pose assets and the control rig belong to the body; left as the character's import made them."));
	}
	else
	{
		Task.EnterProgressFrame(1.f, LOCTEXT("TaskRetarget", "Retargeting"));
		ApplyRetargeting(Export, Skeleton, Report);

		Task.EnterProgressFrame(1.f, LOCTEXT("TaskPoses", "Pose assets"));
		BuildPoseAssets(Export, Skeleton, Request.DestinationFolder, PostProcess, Report);
		if (Request.bJigglePhysics && Meshes.Num() > 0)
		{
			AddJigglePhysics(Export, Meshes[0], Request.DestinationFolder, PostProcess, Report);
		}

		Task.EnterProgressFrame(1.f, LOCTEXT("TaskRig", "Control rig"));
		// On the first mesh, whatever its name, as the DTH importer builds MCR_<Character>.
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
	}

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskGrooms", "Grooms"));
	const TArray<FDTHBuiltGroom> Grooms = BuildGrooms(Export, Request.GroomPlans.IsSet() ? Request.GroomPlans.GetValue() : BuildGroomPlans(Export), Body,
		Request.DestinationFolder, Request.bReimportExistingMeshes, Report);

	Task.EnterProgressFrame(1.f, LOCTEXT("TaskWardrobe", "Wardrobe and character blueprint"));
	{
		const FString OutfitName = Request.OutfitName.IsEmpty() ? DefaultOutfitName(Export, MeshIndices) : Request.OutfitName;
		const TArray<USkeletalMesh*> Garments = Meshes.FilterByPredicate([Body](const USkeletalMesh* Mesh) { return Mesh != Body; });
		BuildWardrobe(Export, Request.DestinationFolder, Body, Garments, Grooms, OutfitName, !bAddOn, Report);
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
	// Plus anything new under the destination (pose assets, Interchange's import data, the wardrobe). Nothing else the
	// user has open.
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
