#include "Components/SkeletalMeshComponent.h"
#include "DTHCharacter.h"
#include "DTHWardrobeTypes.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Export/DTHExport.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "PreviewScene.h"
#include "Steps/DTHImportRun.h"
#include "Steps/DTHWardrobeBuild.h"
#include "Tests/DTHWardrobeTestListener.h"
#include "TrackInstancePropertyBindings.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The wardrobe and add-on imports, run in order on one test character. Inputs from EditorPerProjectUserSettings,
 * [DTHImportWizard.Tests]: ExportFolder (a character export with garments), Destination (test content folder),
 * MeshFilter (garments whose names contain it), OtherExportFolder (a different character's export). Skipped when
 * they aren't set. Only the export's small grooms (brows, lashes) are imported.
 */
namespace DTHWardrobeTest
{
	struct FInputs
	{
		FString ExportFolder, Destination, MeshFilter, OtherExportFolder;

		bool Read(FAutomationTestBase& Test)
		{
			const TCHAR* Section = TEXT("DTHImportWizard.Tests");
			GConfig->GetString(Section, TEXT("ExportFolder"), ExportFolder, GEditorPerProjectIni);
			GConfig->GetString(Section, TEXT("Destination"), Destination, GEditorPerProjectIni);
			GConfig->GetString(Section, TEXT("MeshFilter"), MeshFilter, GEditorPerProjectIni);
			GConfig->GetString(Section, TEXT("OtherExportFolder"), OtherExportFolder, GEditorPerProjectIni);
			if (ExportFolder.IsEmpty() || Destination.IsEmpty())
			{
				Test.AddInfo(TEXT("Skipped: [DTHImportWizard.Tests] ExportFolder and Destination are not set."));
				return false;
			}
			return true;
		}
	};

	/** A request for Folder's body (unless bAddOn) and the manifest meshes whose names contain Filter. */
	FDTHImportRequest Request(FAutomationTestBase& Test, const FString& Folder, const FString& Destination, const FString& Filter, bool bAddOn)
	{
		FDTHImportRequest Request;
		FString Error;
		Request.Export = FDTHExport::Load(Folder, Error);
		Test.TestTrue(TEXT("Export loads: ") + Error, Request.Export.IsValid());
		if (!Request.Export) return Request;
		Request.DestinationFolder = Destination;
		Request.bAddToCharacter = bAddOn;
		for (int32 Index = 0; Index < Request.Export->Meshes.Num(); ++Index)
		{
			const FDTHMeshEntry& Mesh = Request.Export->Meshes[Index];
			if (Mesh.bInManifest && (Mesh.IsBody() || Filter.IsEmpty() || Mesh.Name.Contains(Filter)))
			{
				Request.MeshIndices.Add(Index);
			}
		}
		TArray<FDTHGroomPlan> Grooms = DTHImport::BuildGroomPlans(*Request.Export);
		for (FDTHGroomPlan& Groom : Grooms)
		{
			Groom.bImport = DTHImport::IsBaseSlot(DTHImport::GroomSlot(Groom.Name));
		}
		Request.GroomPlans = Grooms;
		return Request;
	}

	bool HasMessage(const FDTHImportReport& Report, const TCHAR* Text)
	{
		return Report.Messages.ContainsByPredicate([Text](const FDTHImportMessage& Message) { return Message.Text.ToString().Contains(Text); });
	}

	/**
	 * A transient wardrobe on the imported meshes, shaped to exercise every part of the state grammar: base Choker
	 * (Neck); outfit One = Dress (Torso), Hat (Head), Heels (Feet); outfit Two = Panties (Underwear); extra AltHat (Head).
	 */
	UDTHWardrobe* StandInWardrobe(FAutomationTestBase& Test, const UDTHWardrobe& Imported)
	{
		auto Garment = [&](const TCHAR* Token) -> USkeletalMesh*
		{
			for (const UDTHWardrobeItem* Item : Imported.AllItems())
			{
				if (Item->Mesh && Item->Mesh->GetName().Contains(Token)) return Item->Mesh;
			}
			return nullptr;
		};
		auto MakeItem = [](const TCHAR* Name, USkeletalMesh* Mesh, const TCHAR* Slot)
		{
			UDTHWardrobeItem* Item = NewObject<UDTHWardrobeItem>(GetTransientPackage());
			Item->ItemName = Name;
			Item->Mesh = Mesh;
			Item->Slot = Slot;
			return Item;
		};
		UDTHWardrobeItem* Dress = MakeItem(TEXT("Dress"), Garment(TEXT("Dress")), TEXT("Torso"));
		UDTHWardrobeItem* Hat = MakeItem(TEXT("Hat"), Garment(TEXT("Hat")), TEXT("Head"));
		UDTHWardrobeItem* Heels = MakeItem(TEXT("Heels"), Garment(TEXT("Heels")), TEXT("Feet"));
		UDTHWardrobeItem* Choker = MakeItem(TEXT("Choker"), Garment(TEXT("Choker")), TEXT("Neck"));
		UDTHWardrobeItem* Panties = MakeItem(TEXT("Panties"), Garment(TEXT("Panties")), TEXT("Underwear"));
		UDTHWardrobeItem* AltHat = MakeItem(TEXT("AltHat"), Garment(TEXT("Earrings")), TEXT("Head"));
		for (const UDTHWardrobeItem* Item : { Dress, Hat, Heels, Choker, Panties, AltHat })
		{
			if (!Test.TestNotNull(Item->ItemName + TEXT(" mesh found"), Item->Mesh.Get())) return nullptr;
		}
		UDTHOutfit* One = NewObject<UDTHOutfit>(GetTransientPackage());
		One->OutfitName = TEXT("One");
		One->Items = { Dress, Hat, Heels };
		UDTHOutfit* Two = NewObject<UDTHOutfit>(GetTransientPackage());
		Two->OutfitName = TEXT("Two");
		Two->Items = { Panties };
		UDTHWardrobe* Wardrobe = NewObject<UDTHWardrobe>(GetTransientPackage());
		Wardrobe->Body = Imported.Body;
		Wardrobe->BaseItems = { Choker };
		Wardrobe->Outfits = { One, Two };
		Wardrobe->ExtraItems = { AltHat };
		return Wardrobe;
	}

	/** The items whose components are visible, sorted, space-separated. */
	FString Shown(const ADTHCharacter& Actor)
	{
		TArray<FString> Names;
		for (const UDTHWardrobeItem* Item : Actor.Wardrobe->AllItems())
		{
			const USceneComponent* Component = Actor.GetItemComponent(Item);
			if (Component && Component->IsVisible()) Names.Add(Item->ItemName);
		}
		Names.Sort();
		return FString::Join(Names, TEXT(" "));
	}

	FString Names(const TArray<UDTHWardrobeItem*>& Items)
	{
		TArray<FString> Out;
		for (const UDTHWardrobeItem* Item : Items) Out.Add(Item->ItemName);
		Out.Sort();
		return FString::Join(Out, TEXT(" "));
	}

	/** The components of Blueprint's own that were tagged for a wardrobe item. */
	int32 ItemComponentsIn(const UBlueprint& Blueprint)
	{
		int32 Count = 0;
		for (const USCS_Node* Node : Blueprint.SimpleConstructionScript->GetAllNodes())
		{
			Count += ADTHCharacter::TaggedItem(Cast<USceneComponent>(Node->ComponentTemplate)) ? 1 : 0;
		}
		return Count;
	}

	void Log(FAutomationTestBase& Test, const FDTHImportReport& Report)
	{
		for (const FDTHImportMessage& Message : Report.Messages)
		{
			Test.AddInfo(Message.Text.ToString());
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHWardrobeBaseTest, "DTH.Import.Wardrobe.1_Base",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHWardrobeBaseTest::RunTest(const FString& Parameters)
{
	using namespace DTHWardrobeTest;
	FInputs In;
	if (!In.Read(*this)) return true;
	const FDTHImportRequest Req = Request(*this, In.ExportFolder, In.Destination, In.MeshFilter, false);
	if (!Req.Export) return false;
	const FDTHImportReport Report = DTHImport::RunImport(Req);
	Log(*this, Report);
	TestTrue(TEXT("Import succeeds"), Report.bSucceeded);
	TestFalse(TEXT("No manual groom step"), Report.ManualSteps.ContainsByPredicate([](const FDTHManualStep& Step) { return Step.Title.ToString().Contains(TEXT("groom")); }));

	UDTHWardrobe* Wardrobe = DTHImport::FindWardrobe(In.Destination);
	if (!TestNotNull(TEXT("Wardrobe"), Wardrobe)) return false;
	const FDTHMeshEntry* Body = Req.Export->FindBody();
	TestEqual(TEXT("Wardrobe body"), GetNameSafe(Wardrobe->Body), FPaths::GetBaseFilename(Body->File));

	const FString Outfit = DTHImport::DefaultOutfitName(*Req.Export, Req.MeshIndices);
	const UDTHOutfit* Main = Wardrobe->FindOutfit(Outfit);
	if (!TestNotNull(TEXT("Outfit ") + Outfit, Main)) return false;
	TestEqual(TEXT("Outfit items: every imported garment"), Main->Items.Num(), Req.MeshIndices.Num() - 1);
	TestNotNull(TEXT("Nude outfit"), Wardrobe->FindOutfit(TEXT("Nude")));
	TestTrue(TEXT("First outfit is the export's"), Wardrobe->Outfits.Num() > 0 && Wardrobe->Outfits[0].Get() == Main);
	for (const UDTHWardrobeItem* Item : Main->Items)
	{
		AddInfo(FString::Printf(TEXT("Item %s (%s): %s"), *Item->WardrobeName(), *Item->Slot.ToString(), *GetNameSafe(Item->Mesh)));
		TestTrue(Item->GetName() + TEXT(" has a mesh"), Item->Mesh != nullptr);
	}
	TestTrue(TEXT("Base items: the export's brows and lashes"), Wardrobe->BaseItems.Num() > 0);
	for (const UDTHWardrobeItem* Item : Wardrobe->BaseItems)
	{
		AddInfo(FString::Printf(TEXT("Base item %s (%s): %s, binding %s"), *Item->WardrobeName(), *Item->Slot.ToString(), *GetNameSafe(Item->Groom), *GetNameSafe(Item->GroomBinding)));
		TestTrue(Item->GetName() + TEXT(" is bound to the body"), Item->GroomBinding && Item->GroomBinding->GetTargetSkeletalMesh() == Wardrobe->Body);
	}

	const FString Character = Wardrobe->GetName().RightChop(FString(TEXT("DA_Wardrobe_")).Len());
	const UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *(In.Destination / TEXT("BP_") + Character), nullptr, LOAD_NoWarn);
	if (!TestNotNull(TEXT("BP_") + Character, Blueprint)) return false;
	const ADTHCharacter* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject<ADTHCharacter>() : nullptr;
	if (!TestNotNull(TEXT("Blueprint is a DTH Character"), Defaults)) return false;
	TestEqual(TEXT("Blueprint wardrobe"), Defaults->Wardrobe.Get(), Wardrobe);
	TestFalse(TEXT("Blueprint is a full Blueprint (opens with its viewport)"), FBlueprintEditorUtils::IsDataOnlyBlueprint(Blueprint));
	TestEqual(TEXT("Blueprint has a component per wardrobe item"), ItemComponentsIn(*Blueprint), Wardrobe->AllItems().Num());
	TestEqual(TEXT("Blueprint's Body has the body mesh"), Defaults->Body->GetSkeletalMeshAsset(), Wardrobe->Body.Get());

	// What a placed BP_<Character> gets: the wardrobe and the export's outfit, worn.
	FPreviewScene Scene(FPreviewScene::ConstructionValues().SetEditor(true));
	ADTHCharacter* Placed = Scene.GetWorld()->SpawnActor<ADTHCharacter>(Blueprint->GeneratedClass.Get(), FTransform::Identity);
	if (!TestNotNull(TEXT("Placed blueprint"), Placed)) return false;
	TestEqual(TEXT("Placed blueprint's wardrobe"), Placed->Wardrobe.Get(), Wardrobe);
	TestEqual(TEXT("Placed blueprint's state"), Placed->WardrobeState, Outfit);
	TestEqual(TEXT("Placed blueprint wears the outfit and base items"), Placed->GetWornItems().Num(), Main->Items.Num() + Wardrobe->BaseItems.Num());
	for (const UDTHWardrobeItem* Item : Wardrobe->AllItems())
	{
		const USceneComponent* Component = Placed->GetItemComponent(Item);
		TestTrue(Item->GetName() + TEXT(" is shown by the Blueprint's component"), Component && Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript);
		TestTrue(Item->GetName() + TEXT(" is under Body"), Component && Component->GetAttachParent() == Placed->Body);
		TestEqual(Item->GetName() + TEXT(" visible when worn"), Component && Component->IsVisible(), Placed->GetWornItems().Contains(Item));
		if (const USkeletalMeshComponent* Garment = Cast<USkeletalMeshComponent>(Component))
		{
			TestTrue(Item->GetName() + TEXT(" follows the body"), Garment->LeaderPoseComponent.Get() == Placed->Body);
		}
	}
	Placed->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHWardrobeAddOnTest, "DTH.Import.Wardrobe.2_AddOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHWardrobeAddOnTest::RunTest(const FString& Parameters)
{
	using namespace DTHWardrobeTest;
	FInputs In;
	if (!In.Read(*this)) return true;
	UDTHWardrobe* Wardrobe = DTHImport::FindWardrobe(In.Destination);
	if (!TestNotNull(TEXT("Wardrobe from 1_Base"), Wardrobe)) return false;
	USkeletalMesh* BodyBefore = Wardrobe->Body;
	const int32 ItemsBefore = Wardrobe->AllItems().Num();
	const FString Character = Wardrobe->GetName().RightChop(FString(TEXT("DA_Wardrobe_")).Len());
	const UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *(In.Destination / TEXT("BP_") + Character), nullptr, LOAD_NoWarn);
	if (!TestNotNull(TEXT("BP_") + Character, Blueprint)) return false;
	const int32 ComponentsBefore = ItemComponentsIn(*Blueprint);

	// The same export again as an add-on, one garment only: its body matches by construction.
	FDTHImportRequest Req = Request(*this, In.ExportFolder, In.Destination, TEXT("Hat"), true);
	if (!Req.Export) return false;
	Req.OutfitName = TEXT("HatOnly");
	Req.GroomPlans = TArray<FDTHGroomPlan>();
	const FDTHImportReport Report = DTHImport::RunImport(Req);
	Log(*this, Report);
	TestTrue(TEXT("Add-on succeeds"), Report.bSucceeded);
	TestTrue(TEXT("Body was checked"), HasMessage(Report, TEXT("matches the character's")));
	TestFalse(TEXT("Body not imported again"), HasMessage(Report, TEXT("_Body.SKM_")));
	TestFalse(TEXT("No pose assets"), HasMessage(Report, TEXT("Built PA_")));
	TestFalse(TEXT("No control rig"), HasMessage(Report, TEXT("Control rig hierarchy")));
	TestEqual(TEXT("Same body"), Wardrobe->Body.Get(), BodyBefore);

	const UDTHOutfit* HatOnly = Wardrobe->FindOutfit(TEXT("HatOnly"));
	if (!TestNotNull(TEXT("Outfit HatOnly"), HatOnly)) return false;
	TestEqual(TEXT("HatOnly items"), HatOnly->Items.Num(), 1);
	TestEqual(TEXT("No new items (the hat's item is reused)"), Wardrobe->AllItems().Num(), ItemsBefore);
	TestEqual(TEXT("No new Blueprint components (the hat's is reused)"), ItemComponentsIn(*Blueprint), ComponentsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHWardrobeRefuseTest, "DTH.Import.Wardrobe.3_RefuseOtherBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHWardrobeRefuseTest::RunTest(const FString& Parameters)
{
	using namespace DTHWardrobeTest;
	FInputs In;
	if (!In.Read(*this)) return true;
	if (In.OtherExportFolder.IsEmpty())
	{
		AddInfo(TEXT("Skipped: [DTHImportWizard.Tests] OtherExportFolder is not set."));
		return true;
	}
	UDTHWardrobe* Wardrobe = DTHImport::FindWardrobe(In.Destination);
	if (!TestNotNull(TEXT("Wardrobe from 1_Base"), Wardrobe)) return false;
	const int32 OutfitsBefore = Wardrobe->Outfits.Num();

	FDTHImportRequest Req = Request(*this, In.OtherExportFolder, In.Destination, FString(), true);
	if (!Req.Export) return false;
	Req.GroomPlans = TArray<FDTHGroomPlan>();
	// The refusal is reported as an error, which is what this test wants.
	AddExpectedError(TEXT("body is not"), EAutomationExpectedErrorFlags::Contains, 1);
	const FDTHImportReport Report = DTHImport::RunImport(Req);
	Log(*this, Report);
	TestFalse(TEXT("Another character's export is refused"), Report.bSucceeded);
	TestTrue(TEXT("The reason names the body"), HasMessage(Report, TEXT("body is not")));
	TestEqual(TEXT("Wardrobe unchanged"), Wardrobe->Outfits.Num(), OutfitsBefore);
	for (const FDTHMeshEntry& Mesh : Req.Export->Meshes)
	{
		const FString Path = In.Destination / FPaths::GetBaseFilename(Mesh.File);
		TestFalse(Path + TEXT(" not imported"), FPackageName::DoesPackageExist(Path));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHWardrobeActorTest, "DTH.Import.Wardrobe.4_Actor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHWardrobeActorTest::RunTest(const FString& Parameters)
{
	using namespace DTHWardrobeTest;
	FInputs In;
	if (!In.Read(*this)) return true;
	const UDTHWardrobe* Imported = DTHImport::FindWardrobe(In.Destination);
	if (!TestNotNull(TEXT("Wardrobe from 1_Base"), Imported)) return false;

	UDTHWardrobe* Wardrobe = StandInWardrobe(*this, *Imported);
	if (!Wardrobe) return false;
	UDTHWardrobeItem* Dress = Wardrobe->FindItem(TEXT("Dress"));

	FPreviewScene Scene(FPreviewScene::ConstructionValues().SetEditor(true));
	ADTHCharacter* Actor = Scene.GetWorld()->SpawnActorDeferred<ADTHCharacter>(ADTHCharacter::StaticClass(), FTransform::Identity);
	Actor->Wardrobe = Wardrobe;
	Actor->FinishSpawning(FTransform::Identity);
	TestEqual(TEXT("Body mesh from the wardrobe"), Actor->Body->GetSkeletalMeshAsset(), Wardrobe->Body.Get());
	TestTrue(TEXT("Body passes curves to the garments"), Actor->Body->bPropagateCurvesToFollowers != 0);

	TestEqual(TEXT("Every item has a component"), Wardrobe->AllItems().FilterByPredicate([&](const UDTHWardrobeItem* Item) { return Actor->GetItemComponent(Item) != nullptr; }).Num(), 6);
	const USkeletalMeshComponent* DressComponent = Cast<USkeletalMeshComponent>(Actor->GetItemComponent(Dress));
	TestTrue(TEXT("Garments follow the body"), DressComponent && DressComponent->LeaderPoseComponent.Get() == Actor->Body);

	struct FCase { const TCHAR* State; const TCHAR* Expected; };
	const FCase Cases[] = {
		{ TEXT(""), TEXT("Choker Dress Hat Heels") },
		{ TEXT("one -hat"), TEXT("Choker Dress Heels") },
		{ TEXT("One, -slot:Feet"), TEXT("Choker Dress Hat") },
		{ TEXT("One +AltHat"), TEXT("AltHat Choker Dress Heels") },
		{ TEXT("-Heels"), TEXT("Choker Dress Hat") },
		{ TEXT("Two"), TEXT("Choker Panties") },
		{ TEXT("Two -Choker +Hat"), TEXT("Hat Panties") },
		{ TEXT("Bogus"), TEXT("Choker") },
	};
	for (const FCase& Case : Cases)
	{
		Actor->SetWardrobeState(Case.State);
		TestEqual(FString::Printf(TEXT("State \"%s\""), Case.State), Shown(*Actor), FString(Case.Expected));
	}

	// The way Sequencer applies a keyed Wardrobe State: the property's setter, found by name, through ProcessEvent.
	FTrackInstancePropertyBindings Bindings(TEXT("WardrobeState"), TEXT("WardrobeState"));
	Bindings.CallFunction<FString>(*Actor, TEXT("One -Hat"));
	TestEqual(TEXT("Sequencer setter path"), Shown(*Actor), FString(TEXT("Choker Dress Heels")));
	TestEqual(TEXT("Sequencer setter stores the state"), Actor->WardrobeState, FString(TEXT("One -Hat")));

	Actor->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHWardrobeInterfaceTest, "DTH.Import.Wardrobe.5_BlueprintInterface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHWardrobeInterfaceTest::RunTest(const FString& Parameters)
{
	using namespace DTHWardrobeTest;
	FInputs In;
	if (!In.Read(*this)) return true;
	UDTHWardrobe* Imported = DTHImport::FindWardrobe(In.Destination);
	if (!TestNotNull(TEXT("Wardrobe from 1_Base"), Imported)) return false;

	// What the Blueprint editor offers on the imported BP_<Character>: callable functions and assignable events.
	const FString Character = Imported->GetName().RightChop(FString(TEXT("DA_Wardrobe_")).Len());
	const UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *(In.Destination / TEXT("BP_") + Character), nullptr, LOAD_NoWarn);
	if (!TestNotNull(TEXT("BP_") + Character, Blueprint)) return false;
	const UClass* Class = Blueprint->GeneratedClass;
	for (const TCHAR* Name : { TEXT("SetOutfit"), TEXT("WearItem"), TEXT("RemoveItem"), TEXT("ToggleItem"), TEXT("RemoveSlot"),
		TEXT("ResetToOutfit"), TEXT("SetWardrobeState"), TEXT("GetOutfitNames"), TEXT("GetItemNames"), TEXT("GetItems"), TEXT("GetSlots"),
		TEXT("FindItem"), TEXT("IsWearing"), TEXT("GetCurrentOutfitName"), TEXT("GetWornItems"), TEXT("GetItemComponent") })
	{
		const UFunction* Function = Class->FindFunctionByName(Name);
		TestTrue(FString::Printf(TEXT("Blueprint can call %s"), Name), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure));
	}
	for (const TCHAR* Name : { TEXT("OnWardrobeChanged"), TEXT("OnItemWorn"), TEXT("OnItemRemoved") })
	{
		const FProperty* Event = Class->FindPropertyByName(Name);
		TestTrue(FString::Printf(TEXT("Blueprint can bind %s"), Name), Event && Event->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	}
	for (const TCHAR* Name : { TEXT("Wardrobe"), TEXT("WardrobeState"), TEXT("CurrentOutfit"), TEXT("WornItems") })
	{
		const FProperty* Variable = Class->FindPropertyByName(Name);
		TestTrue(FString::Printf(TEXT("Blueprint can read %s"), Name), Variable && Variable->HasAnyPropertyFlags(CPF_BlueprintVisible));
	}

	UDTHWardrobe* Wardrobe = StandInWardrobe(*this, *Imported);
	if (!Wardrobe) return false;
	FPreviewScene Scene(FPreviewScene::ConstructionValues().SetEditor(true));
	ADTHCharacter* Actor = Scene.GetWorld()->SpawnActorDeferred<ADTHCharacter>(ADTHCharacter::StaticClass(), FTransform::Identity);
	Actor->Wardrobe = Wardrobe;
	Actor->FinishSpawning(FTransform::Identity);
	UDTHWardrobeTestListener* Listener = NewObject<UDTHWardrobeTestListener>();
	Actor->OnWardrobeChanged.AddDynamic(Listener, &UDTHWardrobeTestListener::OnChanged);
	Actor->OnItemWorn.AddDynamic(Listener, &UDTHWardrobeTestListener::OnWorn);
	Actor->OnItemRemoved.AddDynamic(Listener, &UDTHWardrobeTestListener::OnRemoved);

	// Inventory.
	TestEqual(TEXT("Outfit names"), FString::Join(Actor->GetOutfitNames(), TEXT(" ")), FString(TEXT("One Two")));
	TestEqual(TEXT("All item names"), Actor->GetItemNames(NAME_None).Num(), 6);
	TestEqual(TEXT("Head items"), Names(Actor->GetItems(TEXT("Head"))), FString(TEXT("AltHat Hat")));
	TestTrue(TEXT("Slots"), Actor->GetSlots().Contains(TEXT("Feet")) && Actor->GetSlots().Contains(TEXT("Underwear")) && Actor->GetSlots().Num() == 5);
	TestEqual(TEXT("Current outfit"), Actor->GetCurrentOutfitName(), FString(TEXT("One")));
	TestEqual(TEXT("Current Outfit variable"), Actor->CurrentOutfit.Get(), Wardrobe->Outfits[0].Get());
	TestTrue(TEXT("Wearing the hat"), Actor->IsWearing(TEXT("hat")));
	TestEqual(TEXT("Worn Items variable"), Actor->WornItems.Num(), 4);

	struct FStep
	{
		const TCHAR* What;
		TFunction<bool()> Do;
		bool bReturns;
		const TCHAR* State;
		const TCHAR* Shown;
		const TCHAR* Worn;
		const TCHAR* Removed;
	};
	const FStep Steps[] = {
		{ TEXT("Toggle Item Hat (off)"), [&] { return Actor->ToggleItem(TEXT("Hat")); }, false, TEXT("One -Hat"), TEXT("Choker Dress Heels"), TEXT(""), TEXT("Hat") },
		{ TEXT("Toggle Item Hat (on)"), [&] { return Actor->ToggleItem(TEXT("Hat")); }, true, TEXT("One"), TEXT("Choker Dress Hat Heels"), TEXT("Hat"), TEXT("") },
		{ TEXT("Wear Item AltHat"), [&] { return Actor->WearItem(TEXT("AltHat")); }, true, TEXT("One +AltHat"), TEXT("AltHat Choker Dress Heels"), TEXT("AltHat"), TEXT("Hat") },
		{ TEXT("Wear Item Hat"), [&] { return Actor->WearItem(TEXT("Hat")); }, true, TEXT("One +Hat"), TEXT("Choker Dress Hat Heels"), TEXT("Hat"), TEXT("AltHat") },
		{ TEXT("Remove Slot Feet"), [&] { Actor->RemoveSlot(TEXT("Feet")); return true; }, true, TEXT("One -slot:Feet +Hat"), TEXT("Choker Dress Hat"), TEXT(""), TEXT("Heels") },
		{ TEXT("Wear Item Heels"), [&] { return Actor->WearItem(TEXT("Heels")); }, true, TEXT("One -slot:Feet +Hat +Heels"), TEXT("Choker Dress Hat Heels"), TEXT("Heels"), TEXT("") },
		{ TEXT("Remove Item Choker"), [&] { return Actor->RemoveItem(TEXT("Choker")); }, true, TEXT("One -Choker -slot:Feet +Hat +Heels"), TEXT("Dress Hat Heels"), TEXT(""), TEXT("Choker") },
		{ TEXT("Reset To Outfit"), [&] { Actor->ResetToOutfit(); return true; }, true, TEXT("One"), TEXT("Choker Dress Hat Heels"), TEXT("Choker"), TEXT("") },
		{ TEXT("Set Outfit Two"), [&] { return Actor->SetOutfit(TEXT("two")); }, true, TEXT("two"), TEXT("Choker Panties"), TEXT("Panties"), TEXT("Dress Hat Heels") },
	};
	for (const FStep& Step : Steps)
	{
		Listener->Reset();
		TestEqual(FString(Step.What) + TEXT(": returns"), Step.Do(), Step.bReturns);
		TestEqual(FString(Step.What) + TEXT(": state"), Actor->WardrobeState, FString(Step.State));
		TestEqual(FString(Step.What) + TEXT(": shown"), Shown(*Actor), FString(Step.Shown));
		TestEqual(FString(Step.What) + TEXT(": On Wardrobe Changed once"), Listener->Changes.Num(), 1);
		TestEqual(FString(Step.What) + TEXT(": its worn items"), Listener->LastWornCount, Actor->WornItems.Num());
		TestEqual(FString(Step.What) + TEXT(": On Item Worn"), Names(Listener->Worn), FString(Step.Worn));
		TestEqual(FString(Step.What) + TEXT(": On Item Removed"), Names(Listener->Removed), FString(Step.Removed));
	}
	TestEqual(TEXT("Current outfit after Set Outfit"), Actor->GetCurrentOutfitName(), FString(TEXT("Two")));

	// Unknown names change nothing and fire nothing; the same state again fires nothing.
	Listener->Reset();
	TestFalse(TEXT("Set Outfit Bogus"), Actor->SetOutfit(TEXT("Bogus")));
	TestFalse(TEXT("Wear Item Bogus"), Actor->WearItem(TEXT("Bogus")));
	Actor->SetWardrobeState(TEXT("two"));
	TestEqual(TEXT("No events for no change"), Listener->Changes.Num(), 0);
	TestEqual(TEXT("State kept"), Actor->WardrobeState, FString(TEXT("two")));

	// Sequencer's path fires the same events.
	FTrackInstancePropertyBindings Bindings(TEXT("WardrobeState"), TEXT("WardrobeState"));
	Bindings.CallFunction<FString>(*Actor, TEXT("One"));
	TestEqual(TEXT("Sequencer change fires On Wardrobe Changed"), Listener->Changes.Num(), 1);

	Actor->Destroy();
	return true;
}

#endif
