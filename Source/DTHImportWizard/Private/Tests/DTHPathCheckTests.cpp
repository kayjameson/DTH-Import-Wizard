#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "Export/DTHExport.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "ObjectTools.h"
#include "Steps/DTHImportRun.h"
#include "Steps/DTHPathCheck.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Assets of another kind where the import writes: an old wardrobe asset at the new wardrobe's path (as the Python
 * wizard left for Vi) and a stand-in at a mesh's path. The import must stop before making anything, and the override
 * must delete only the old wardrobe. Uses [DTHImportWizard.Tests] ExportFolder; stand-ins go in
 * /Game/DTH_Import/_Test/PathClash and are deleted afterwards.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPathClashTest, "DTH.Import.PathClash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace DTHPathClashTest
{
	/** A float curve: any class the import never puts at these paths. */
	UObject* StandIn(const FString& PackageName)
	{
		UPackage* Package = CreatePackage(*PackageName);
		UCurveFloat* Curve = NewObject<UCurveFloat>(Package, FName(*FPackageName::GetShortName(PackageName)), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Curve);
		return Curve;
	}
}

bool FDTHPathClashTest::RunTest(const FString& Parameters)
{
	FString ExportFolder;
	GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("ExportFolder"), ExportFolder, GEditorPerProjectIni);
	if (ExportFolder.IsEmpty())
	{
		AddInfo(TEXT("Skipped: [DTHImportWizard.Tests] ExportFolder is not set."));
		return true;
	}
	FDTHImportRequest Request;
	FString Error;
	Request.Export = FDTHExport::Load(ExportFolder, Error);
	if (!TestTrue(TEXT("Export loads: ") + Error, Request.Export.IsValid())) return false;
	const FString Destination = TEXT("/Game/DTH_Import/_Test/PathClash");
	Request.DestinationFolder = Destination;
	const FDTHMeshEntry* Body = Request.Export->FindBody();
	if (!TestNotNull(TEXT("Export has a body"), Body)) return false;
	Request.MeshIndices.Add(UE_PTRDIFF_TO_INT32(Body - Request.Export->Meshes.GetData()));

	FString Character;
	for (const TCHAR Char : Request.Export->Character)
	{
		Character.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
	}
	UObject* OldWardrobe = DTHPathClashTest::StandIn(Destination / TEXT("Wardrobe") / TEXT("DA_Wardrobe_") + Character);
	UObject* OldItem = DTHPathClashTest::StandIn(Destination / TEXT("Wardrobe") / TEXT("Items") / TEXT("DA_Item_Old"));
	UObject* OnMesh = DTHPathClashTest::StandIn(Destination / FPaths::GetBaseFilename(Body->File));
	const FSoftObjectPath OldWardrobePath(OldWardrobe), OldItemPath(OldItem), OnMeshPath(OnMesh);

	const TArray<FDTHPathClash> Clashes = DTHImport::FindPathClashes(Request);
	TestEqual(TEXT("Clashes found"), Clashes.Num(), 3);
	TestEqual(TEXT("Old wardrobe clashes"), Clashes.FilterByPredicate([](const FDTHPathClash& Clash) { return Clash.bOldWardrobe; }).Num(), 2);
	TestTrue(TEXT("The mesh path's stand-in is a clash, not the wardrobe's"), Clashes.ContainsByPredicate([&](const FDTHPathClash& Clash)
		{ return Clash.Path == OnMeshPath.ToString() && !Clash.bOldWardrobe; }));

	// The refusals this test causes are logged as errors.
	AddExpectedError(TEXT("Move or rename it."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("is not a DTH wardrobe asset."), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("holds an older wardrobe"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Nothing was imported."), EAutomationExpectedErrorFlags::Contains, 0);

	// Stops before making anything, with or without the override (the mesh path is not the wardrobe's to delete).
	Request.bDeleteOldWardrobe = false;
	FDTHImportReport Report = DTHImport::RunImport(Request);
	TestFalse(TEXT("Import refused"), Report.bSucceeded);
	TestTrue(TEXT("Old wardrobe kept without the override"), OldWardrobePath.ResolveObject() != nullptr);
	TestTrue(TEXT("Nothing imported"), Report.Messages.ContainsByPredicate([](const FDTHImportMessage& Message)
		{ return Message.Text.ToString() == TEXT("Nothing was imported."); }));

	Request.bDeleteOldWardrobe = true;
	Report = DTHImport::RunImport(Request);
	TestFalse(TEXT("Import still refused for the mesh path"), Report.bSucceeded);
	TestTrue(TEXT("Old wardrobe kept when something else is in the way"), OldWardrobePath.ResolveObject() != nullptr);

	// The override alone deletes the old wardrobe and nothing else.
	Report = FDTHImportReport();
	TestTrue(TEXT("Old wardrobe deleted"), DTHImport::DeleteOldWardrobe(Clashes, Report));
	TestNull(TEXT("Old wardrobe gone"), OldWardrobePath.ResolveObject());
	TestNull(TEXT("Old item gone"), OldItemPath.ResolveObject());
	TestNotNull(TEXT("Mesh path stand-in kept"), OnMeshPath.ResolveObject());
	TestEqual(TEXT("Only the mesh clash remains"), DTHImport::FindPathClashes(Request).Num(), 1);

	if (UObject* Left = OnMeshPath.ResolveObject())
	{
		ObjectTools::ForceDeleteObjects({ Left }, false);
	}
	return true;
}

#endif
