#include "ControlRig.h"
#include "ControlRigBlueprintLegacy.h"
#include "ControlRigGizmoLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "ObjectTools.h"
#include "Rigs/RigHierarchy.h"
#include "Steps/DTHControlRig.h"
#include "Steps/DTHImportReport.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A rig the import builds shows its control shapes in Sequencer: a new rig instance, as Sequencer makes one, finds a
 * shape for every control that has one. Built fresh (create path) and again (update path) in
 * /Game/DTH_Import/_Test/RigShapes on the test character's body from [DTHImportWizard.Tests] (ExportFolder,
 * Destination), which the wardrobe base test imports.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHControlRigShapesTest, "DTH.Import.ControlRigShapes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace DTHControlRigShapesTest
{
	/** Controls with a shape, and how many of them a fresh instance of Rig finds no shape for. */
	void CountShapes(UControlRigBlueprint* Rig, int32& OutWithShape, int32& OutMissing, FString& OutFirstMissing)
	{
		OutWithShape = OutMissing = 0;
		UControlRig* Instance = NewObject<UControlRig>(GetTransientPackage(), Rig->GeneratedClass);
		Instance->Initialize(true);
		Instance->Evaluate_AnyThread();
		Instance->GetHierarchy()->ForEach<FRigControlElement>([&](FRigControlElement* Control) -> bool
		{
			if (Control->Settings.SupportsShape())
			{
				++OutWithShape;
				if (!UControlRigShapeLibrary::GetShapeByName(Control->Settings.ShapeName, Instance->GetShapeLibraries(), Instance->GetShapeLibraryNameMap(), false))
				{
					++OutMissing;
					if (OutFirstMissing.IsEmpty()) OutFirstMissing = Control->GetName() + TEXT(" (") + Control->Settings.ShapeName.ToString() + TEXT(")");
				}
			}
			return true;
		});
		Instance->MarkAsGarbage();
	}
}

bool FDTHControlRigShapesTest::RunTest(const FString& Parameters)
{
	FString ExportFolder, Destination;
	GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("ExportFolder"), ExportFolder, GEditorPerProjectIni);
	GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("Destination"), Destination, GEditorPerProjectIni);
	if (ExportFolder.IsEmpty() || Destination.IsEmpty())
	{
		AddInfo(TEXT("Skipped: [DTHImportWizard.Tests] ExportFolder and Destination are not set."));
		return true;
	}
	FString Error;
	const TSharedPtr<FDTHExport> Export = FDTHExport::Load(ExportFolder, Error);
	if (!TestTrue(TEXT("Export loads: ") + Error, Export.IsValid())) return false;
	const FDTHMeshEntry* BodyEntry = Export->FindBody();
	if (!TestNotNull(TEXT("Export has a body"), BodyEntry)) return false;
	USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, *(Destination / FPaths::GetBaseFilename(BodyEntry->File)), nullptr, LOAD_NoWarn);
	if (!TestNotNull(TEXT("Test character's body (run DTH.Import.Wardrobe.1_Base first)"), Body)) return false;

	// Optionally, an existing rig to report on as well (RigToCheck), read only.
	FString RigToCheck;
	GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("RigToCheck"), RigToCheck, GEditorPerProjectIni);
	if (UControlRigBlueprint* Existing = RigToCheck.IsEmpty() ? nullptr : LoadObject<UControlRigBlueprint>(nullptr, *RigToCheck, nullptr, LOAD_NoWarn))
	{
		int32 WithShape = 0, Missing = 0;
		FString FirstMissing;
		DTHControlRigShapesTest::CountShapes(Existing, WithShape, Missing, FirstMissing);
		AddInfo(FString::Printf(TEXT("%s as saved: %d controls with shapes, %d without a shape in Sequencer%s%s."), *RigToCheck, WithShape, Missing,
			FirstMissing.IsEmpty() ? TEXT("") : TEXT(", e.g. "), *FirstMissing));
	}

	// The body's own rig link is put back afterwards; the test rig is deleted.
	const TSoftObjectPtr<UObject> BodyRig = Body->GetDefaultAnimatingRig();
	const FString Folder = TEXT("/Game/DTH_Import/_Test/RigShapes");
	const FString RigPath = Folder / TEXT("MCR_") + Export->Character;
	if (UControlRigBlueprint* Old = LoadObject<UControlRigBlueprint>(nullptr, *RigPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		ObjectTools::ForceDeleteObjects({ Old }, false);
	}
	for (const TCHAR* Pass : { TEXT("New rig"), TEXT("Updated rig") })
	{
		FDTHImportReport Report;
		UControlRigBlueprint* Rig = DTHImport::BuildControlRig(*Export, Body, Folder, Report);
		if (!TestNotNull(Pass, Rig)) return false;
		for (const FDTHImportMessage& Message : Report.Messages)
		{
			if (Message.Severity == FDTHImportMessage::ESeverity::Error) AddError(Message.Text.ToString());
		}
		int32 WithShape = 0, Missing = 0;
		FString FirstMissing;
		DTHControlRigShapesTest::CountShapes(Rig, WithShape, Missing, FirstMissing);
		AddInfo(FString::Printf(TEXT("%s: %d controls with shapes, %d without a shape in Sequencer%s%s."), Pass, WithShape, Missing,
			FirstMissing.IsEmpty() ? TEXT("") : TEXT(", e.g. "), *FirstMissing));
		TestTrue(FString(Pass) + TEXT(": the rig has controls with shapes"), WithShape > 100);
		TestEqual(FString(Pass) + TEXT(": every control finds its shape"), Missing, 0);
	}

	Body->SetDefaultAnimatingRig(BodyRig);
	if (UControlRigBlueprint* Rig = LoadObject<UControlRigBlueprint>(nullptr, *RigPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		ObjectTools::ForceDeleteObjects({ Rig }, false);
	}
	return true;
}

#endif
