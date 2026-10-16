#include "Steps/DTHControlRig.h"

#include "AnimationRuntime.h"
#include "ControlRig.h"
#include "ControlRigBlueprintFactory.h"
#include "ControlRigBlueprintLegacy.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ModularRigController.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Settings/DTHImportSettings.h"
#include "Steps/DTHImportReport.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "DTHControlRig"

namespace
{
	/** A connector to resolve: "<Module>/<Connector>" onto a bone or socket. */
	struct FConnection
	{
		const TCHAR* Connector;
		ERigElementType TargetType;
		FString Target;
	};

	/** A DTH rig module and how it attaches. Parent is empty for modules under the rig root. */
	struct FModuleSpec
	{
		FName Name;
		const TCHAR* Asset;
		FName Parent;
		TArray<FConnection> Connections;
	};

	FString AssetSafe(const FString& Name)
	{
		FString Out;
		for (const TCHAR Char : Name)
		{
			Out.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
		}
		return Out;
	}

	TSet<FName> MeshBones(const USkeletalMesh* Mesh)
	{
		TSet<FName> Bones;
		const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			Bones.Add(Reference.GetBoneName(Index));
		}
		return Bones;
	}

	/** Bones missing from the rig, or not at the mesh's rest pose (a rig built from the Skeleton asset instead of the mesh). */
	TArray<FString> CheckHierarchy(URigHierarchy* Hierarchy, const USkeletalMesh* Mesh, int32& OutChecked)
	{
		TArray<FString> Problems;
		const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
		TArray<FString> Missing;
		TArray<FString> Moved;
		OutChecked = 0;
		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			const FRigElementKey Key(Reference.GetBoneName(Index), ERigElementType::Bone);
			if (!Hierarchy->Contains(Key))
			{
				Missing.Add(Key.Name.ToString());
				continue;
			}
			++OutChecked;
			const FTransform Rest = FAnimationRuntime::GetComponentSpaceTransformRefPose(Reference, Index);
			for (const bool bInitial : { true, false })
			{
				const FTransform Rig = Hierarchy->GetGlobalTransform(Key, bInitial);
				if (!Rig.GetLocation().Equals(Rest.GetLocation(), 0.01) || Rig.GetRotation().AngularDistance(Rest.GetRotation()) > 0.0175)
				{
					Moved.Add(Key.Name.ToString() + (bInitial ? TEXT(" (initial)") : TEXT(" (current)")));
					break;
				}
			}
		}
		if (Missing.Num() > 0)
		{
			Problems.Add(FString::Printf(TEXT("%d mesh bones are missing from the rig (e.g. %s)"), Missing.Num(),
				*FString::Join(TArrayView<FString>(Missing.GetData(), FMath::Min(4, Missing.Num())), TEXT(", "))));
		}
		if (Moved.Num() > 0)
		{
			Problems.Add(FString::Printf(TEXT("%d rig bones are not at the mesh's rest pose (e.g. %s)"), Moved.Num(),
				*FString::Join(TArrayView<FString>(Moved.GetData(), FMath::Min(4, Moved.Num())), TEXT(", "))));
		}
		return Problems;
	}
}

UControlRigBlueprint* DTHImport::BuildControlRig(const FDTHExport& Export, USkeletalMesh* BodyMesh, const FString& DestinationFolder, FDTHImportReport& Report)
{
	const UDTHImportSettings* Settings = GetDefault<UDTHImportSettings>();
	const FString RigName = FString::Printf(TEXT("MCR_%s"), *AssetSafe(Export.Character));
	FString Folder = DestinationFolder;
	Folder.RemoveFromEnd(TEXT("/"));
	const FString RigPath = Folder / RigName;

	UControlRigBlueprint* Rig = LoadObject<UControlRigBlueprint>(nullptr, *RigPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Rig)
	{
		Report.Info(FText::Format(LOCTEXT("RigUpdate", "Control rig {0} exists; updating it."), FText::FromString(RigName)));
	}
	else
	{
		Rig = UControlRigBlueprintFactory::CreateNewControlRigAsset(RigPath, true);
		if (!Rig)
		{
			Report.Error(FText::Format(LOCTEXT("RigCreateFailed", "Could not create the modular control rig {0}."), FText::FromString(RigPath)));
			return nullptr;
		}
	}

	// An open rig editor rebuilds the hierarchy from the mesh's Skeleton asset when the preview mesh changes,
	// which brings in bones at the Skeleton's proportions. Work on the asset with no editor open.
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Rig);

	IControlRigEditorAssetInterface* RigAsset = Rig;
	URigHierarchy* Hierarchy = Rig->GetHierarchy();
	URigHierarchyController* HierarchyController = Hierarchy->GetController(true);
	HierarchyController->ImportPreviewSkeletalMesh(BodyMesh, true, true, false, false);
	RigAsset->SetPreviewMesh(BodyMesh, true);

	UModularRigController* Modules = RigAsset->GetModularRigController();
	const TSet<FName> Bones = MeshBones(BodyMesh);

	// Golden Palace's graft root is "gp9_root" in DTH 2.5 exports ("pelvis2" in older ones).
	const FString Gp9Root = Bones.Contains(TEXT("gp9_root")) ? TEXT("gp9_root") : Bones.Contains(TEXT("pelvis2")) ? TEXT("pelvis2") : FString();
	const FString Dk9Root = Bones.Contains(TEXT("dk9_root")) ? TEXT("dk9_root") : FString();

	TArray<FModuleSpec> Specs;
	Specs.Add({ TEXT("Body"), TEXT("Common/ControlRigs/Modules/DTH_Body"), NAME_None, {
		{ TEXT("Body/Spine Primary"), ERigElementType::Socket, TEXT("Root/pelvis_socket") },
		{ TEXT("Body/Spine End Bone"), ERigElementType::Bone, TEXT("spine_04") } } });
	Specs.Add({ TEXT("Face"), TEXT("Common/ControlRigs/Modules/DTH_Face_G9"), TEXT("Body"), {
		{ TEXT("Face/Head Primary"), ERigElementType::Socket, TEXT("Body/head_socket") } } });
	if (!Gp9Root.IsEmpty())
	{
		Specs.Add({ TEXT("GP9"), TEXT("Common/ControlRigs/Modules/DTH_GP9"), TEXT("Body"), {
			{ TEXT("GP9/Genitalia Primary"), ERigElementType::Socket, TEXT("Body/genitalia_socket") },
			{ TEXT("GP9/Pelvis Bone"), ERigElementType::Bone, TEXT("pelvis") },
			{ TEXT("GP9/Genitalia Root Bone"), ERigElementType::Bone, Gp9Root } } });
	}
	if (!Dk9Root.IsEmpty())
	{
		Specs.Add({ TEXT("DK9"), TEXT("Common/ControlRigs/Modules/DTH_DK9"), TEXT("Body"), {
			{ TEXT("DK9/Genitalia Primary"), ERigElementType::Socket, TEXT("Body/genitalia_socket") },
			{ TEXT("DK9/Pelvis Bone"), ERigElementType::Bone, TEXT("pelvis") },
			{ TEXT("DK9/Genitalia Root Bone"), ERigElementType::Bone, Dk9Root } } });
	}

	auto Connect = [&](const FConnection& Connection)
	{
		const FRigElementKey ConnectorKey(Connection.Connector, ERigElementType::Connector);
		const FRigElementKey TargetKey(*Connection.Target, Connection.TargetType);
		if (RigAsset->GetModularRigModel().Connections.FindTargetFromConnector(ConnectorKey) == TargetKey)
		{
			return;
		}
		// Sockets only exist once the rig has run, so only bone targets can be validated up front.
		const bool bValidate = Connection.TargetType == ERigElementType::Bone;
		if (!Modules->ConnectConnectorToElement(ConnectorKey, TargetKey, false, true, bValidate))
		{
			Report.Warning(FText::Format(LOCTEXT("ConnectFailed", "Control rig: could not connect {0} to {1}."),
				FText::FromString(Connection.Connector), FText::FromString(Connection.Target)));
		}
	};

	Connect({ TEXT("Root/Root"), ERigElementType::Bone, TEXT("root") });
	const TArray<FName> Existing = Modules->GetAllModules();
	for (const FModuleSpec& Spec : Specs)
	{
		if (!Existing.Contains(Spec.Name))
		{
			const FString ModulePath = Settings->DazToHueAsset(Spec.Asset);
			const UControlRigBlueprint* ModuleAsset = LoadObject<UControlRigBlueprint>(nullptr, *ModulePath, nullptr, LOAD_NoWarn);
			if (!ModuleAsset || !ModuleAsset->GeneratedClass)
			{
				Report.Error(FText::Format(LOCTEXT("ModuleMissing", "Control rig module not found: {0}"), FText::FromString(ModulePath)));
				continue;
			}
			const FName Added = Modules->AddModule(Spec.Name, ModuleAsset->GeneratedClass.Get(), Spec.Parent, false);
			if (Added.IsNone())
			{
				Report.Error(FText::Format(LOCTEXT("ModuleAddFailed", "Could not add control rig module {0}."), FText::FromName(Spec.Name)));
				continue;
			}
			Report.Info(FText::Format(LOCTEXT("ModuleAdded", "Control rig: added module {0} ({1})."), FText::FromName(Added), FText::FromString(FPaths::GetBaseFilename(ModulePath))));
		}
		for (const FConnection& Connection : Spec.Connections)
		{
			Connect(Connection);
		}
	}

	// The face board's shapes (Quick Start guide steps 138-140).
	const FString ShapeLibraryPath = Settings->DazToHueAsset(TEXT("Common/ControlRigs/Controls/CRSL_DazToHue"));
	TArray<TSoftObjectPtr<UControlRigShapeLibrary>>& Libraries = RigAsset->GetShapeLibraries();
	const bool bHasLibrary = Libraries.ContainsByPredicate([&](const TSoftObjectPtr<UControlRigShapeLibrary>& Library)
	{
		return Library.ToSoftObjectPath().GetLongPackageName() == ShapeLibraryPath;
	});
	if (!bHasLibrary)
	{
		if (UControlRigShapeLibrary* Library = LoadObject<UControlRigShapeLibrary>(nullptr, *ShapeLibraryPath, nullptr, LOAD_NoWarn))
		{
			static_cast<UObject*>(Rig)->Modify();
			Libraries.Add(Library);
		}
		else
		{
			Report.Warning(FText::Format(LOCTEXT("ShapesMissing", "Control shape library not found: {0}"), FText::FromString(ShapeLibraryPath)));
		}
	}

	// The mesh's own bones at its rest pose have the last word, then rebuild the modules on top of them.
	HierarchyController->ImportPreviewSkeletalMesh(BodyMesh, true, true, false, false);
	Hierarchy->ResetPoseToInitial(ERigElementType::All);
	RigAsset->RecompileModularRig();
	FKismetEditorUtilities::CompileBlueprint(Rig);

	TArray<FString> OpenConnectors;
	for (const FRigConnectorElement* Connector : Hierarchy->GetConnectors())
	{
		if (!Connector->IsOptional() && !RigAsset->GetModularRigModel().Connections.HasConnection(Connector->GetKey()))
		{
			OpenConnectors.Add(Connector->GetKey().Name.ToString());
		}
	}
	if (OpenConnectors.Num() > 0)
	{
		Report.Warning(FText::Format(LOCTEXT("OpenConnectors", "Control rig connectors left unconnected (connect them in the rig editor): {0}"),
			FText::FromString(FString::Join(OpenConnectors, TEXT(", ")))));
	}

	int32 Checked = 0;
	const TArray<FString> Problems = CheckHierarchy(Hierarchy, BodyMesh, Checked);
	if (Problems.Num() > 0)
	{
		Report.Error(FText::Format(LOCTEXT("RigHierarchyBad", "Control rig hierarchy does not match {0}, so the rig would deform the mesh: {1}"),
			FText::FromString(BodyMesh->GetName()), FText::FromString(FString::Join(Problems, TEXT("; ")))));
	}
	else
	{
		Report.Info(FText::Format(LOCTEXT("RigHierarchyOk", "Control rig hierarchy verified: {0} bones, all at {1}'s rest pose."), Checked, FText::FromString(BodyMesh->GetName())));
	}

	BodyMesh->Modify();
	BodyMesh->SetDefaultAnimatingRig(TSoftObjectPtr<UObject>(Rig));
	BodyMesh->MarkPackageDirty();
	Rig->MarkPackageDirty();
	Report.Info(FText::Format(LOCTEXT("RigDone", "{0}: default animating rig is {1}."), FText::FromString(BodyMesh->GetName()), FText::FromString(RigName)));
	return Rig;
}

#undef LOCTEXT_NAMESPACE
