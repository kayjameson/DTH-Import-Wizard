#include "Steps/DTHWardrobeBuild.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DTHCharacter.h"
#include "DTHImportLog.h"
#include "DTHWardrobeTypes.h"
#include "Engine/Blueprint.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "GroomAsset.h"
#include "GroomComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Steps/DTHImportReport.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "DTHWardrobeBuild"

namespace
{
	FString Sanitize(const FString& Name)
	{
		FString Out;
		for (const TCHAR Char : Name)
		{
			Out.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
		}
		return Out;
	}

	/** Name tokens per slot (comma-separated), checked in order: underwear before legs, as "panties" contains "pant". */
	const TPair<const TCHAR*, const TCHAR*> GarmentSlots[] = {
		{ TEXT("Underwear"), TEXT("panty,panties,thong,brief,underwear,lingerie") },
		{ TEXT("Feet"), TEXT("boot,shoe,heel,sandal,sock,stocking,sneaker") },
		{ TEXT("Legs"), TEXT("skirt,pant,trouser,short,legging,jean") },
		{ TEXT("Arms"), TEXT("bracer,glove,gauntlet,sleeve,bracelet") },
		{ TEXT("Shoulders"), TEXT("pauldron,shoulder,cape,cloak") },
		{ TEXT("Neck"), TEXT("choker,necklace,collar,scarf") },
		{ TEXT("Torso"), TEXT("corset,shirt,top,vest,jacket,dress,bra,bodysuit,coat,robe") },
		{ TEXT("Head"), TEXT("helm,hat,crown,hood,mask,tiara") },
	};

	const TPair<const TCHAR*, const TCHAR*> GroomSlots[] = {
		{ TEXT("brow"), TEXT("Eyebrows") }, { TEXT("lash"), TEXT("Eyelashes") }, { TEXT("vellus"), TEXT("Vellus") },
		{ TEXT("fuzz"), TEXT("Vellus") }, { TEXT("peach"), TEXT("Vellus") }, { TEXT("beard"), TEXT("Beard") },
		{ TEXT("moustache"), TEXT("Beard") }, { TEXT("mustache"), TEXT("Beard") }, { TEXT("stubble"), TEXT("Beard") } };

	/**
	 * A new asset of class T at Folder/Name, or the existing one. Null (and an error) when an asset of another class
	 * is there: Unreal can't replace it, and stops the editor when asked to.
	 */
	template <typename T>
	T* FindOrCreate(const FString& Folder, const FString& Name, bool& bOutCreated, FDTHImportReport& Report)
	{
		const FString PackageName = Folder / Name;
		bOutCreated = false;
		if (UObject* Found = StaticLoadObject(UObject::StaticClass(), nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			if (T* Existing = Cast<T>(Found))
			{
				return Existing;
			}
			Report.Error(FText::Format(LOCTEXT("OtherClass", "{0} is a {1}, not a {2}; it was left as it is."),
				FText::FromString(Found->GetPathName()), FText::FromString(Found->GetClass()->GetName()), FText::FromString(T::StaticClass()->GetName())));
			return nullptr;
		}
		UPackage* Package = CreatePackage(*PackageName);
		T* Asset = NewObject<T>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Package->MarkPackageDirty();
		bOutCreated = true;
		return Asset;
	}

	/** Every wardrobe item asset in Folder (also ones no wardrobe lists). */
	TArray<UDTHWardrobeItem*> ItemsIn(const FString& Folder)
	{
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get()
			.GetAssetsByClass(UDTHWardrobeItem::StaticClass()->GetClassPathName(), Assets);
		TArray<UDTHWardrobeItem*> Items;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.PackagePath.ToString() == Folder)
			{
				if (UDTHWardrobeItem* Item = Cast<UDTHWardrobeItem>(Asset.GetAsset())) Items.Add(Item);
			}
		}
		return Items;
	}

	class FWardrobeBuilder
	{
	public:
		FWardrobeBuilder(const FString& InFolder, const FString& InCharacter, const FString& InSource, FDTHImportReport& InReport)
			: Folder(InFolder / TEXT("Wardrobe")), Character(InCharacter), Source(InSource), Report(InReport), Known(ItemsIn(Folder))
		{
		}

		/** The item for a mesh or groom: an existing one (any name), else DA_Item_<Character>_<Name>. */
		UDTHWardrobeItem* Item(USkeletalMesh* Mesh, UGroomAsset* Groom, UGroomBindingAsset* Binding, const FString& Name, FName Slot)
		{
			for (UDTHWardrobeItem* Item : Known)
			{
				if ((Mesh && Item->Mesh == Mesh) || (Groom && Item->Groom == Groom))
				{
					if (Groom && Binding && Item->GroomBinding != Binding)
					{
						Item->Modify();
						Item->GroomBinding = Binding;
					}
					return Item;
				}
			}
			bool bCreated = false;
			UDTHWardrobeItem* Item = FindOrCreate<UDTHWardrobeItem>(Folder, FString::Printf(TEXT("DA_Item_%s_%s"), *Character, *Sanitize(Name)), bCreated, Report);
			if (!Item) return nullptr;
			Item->Modify();
			Item->Mesh = Mesh;
			Item->Groom = Groom;
			Item->GroomBinding = Binding;
			Item->Slot = Slot;
			Item->ItemName = Name;
			Item->SourceExport = Source;
			Item->MarkPackageDirty();
			Known.Add(Item);
			++Created;
			return Item;
		}

		UDTHOutfit* Outfit(const FString& Name, const TArray<UDTHWardrobeItem*>& Items)
		{
			bool bCreated = false;
			UDTHOutfit* Outfit = FindOrCreate<UDTHOutfit>(Folder, FString::Printf(TEXT("DA_Outfit_%s_%s"), *Character, *Sanitize(Name)), bCreated, Report);
			if (!Outfit) return nullptr;
			Outfit->Modify();
			Outfit->OutfitName = Name;
			int32 Added = 0;
			for (UDTHWardrobeItem* Item : Items)
			{
				if (!Outfit->Items.Contains(Item))
				{
					Outfit->Items.Add(Item);
					++Added;
				}
			}
			Outfit->MarkPackageDirty();
			Report.Info(FText::Format(bCreated ? LOCTEXT("OutfitMade", "Outfit {0}: {1} items.") : LOCTEXT("OutfitUpdated", "Outfit {0} exists; {1} items added."),
				FText::FromString(Name), bCreated ? Outfit->Items.Num() : Added));
			return Outfit;
		}

		UDTHWardrobe* Wardrobe(USkeletalMesh* Body)
		{
			bool bCreated = false;
			UDTHWardrobe* Wardrobe = FindOrCreate<UDTHWardrobe>(Folder, FString::Printf(TEXT("DA_Wardrobe_%s"), *Character), bCreated, Report);
			if (!Wardrobe) return nullptr;
			Wardrobe->Modify();
			if (!Wardrobe->Body)
			{
				Wardrobe->Body = Body;
			}
			Wardrobe->MarkPackageDirty();
			return Wardrobe;
		}

		const FString Folder;
		const FString Character;
		const FString Source;
		FDTHImportReport& Report;
		TArray<UDTHWardrobeItem*> Known;
		int32 Created = 0;
	};

	/** A component variable name for Item that the Blueprint doesn't use yet: Wardrobe_<Name>, _2, _3... */
	FName ComponentName(const USimpleConstructionScript& Construction, const UDTHWardrobeItem& Item)
	{
		FString Base;
		for (const TCHAR Char : Item.WardrobeName())
		{
			Base.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
		}
		Base = TEXT("Wardrobe_") + Base;
		FString Name = Base;
		for (int32 Suffix = 2; Construction.FindSCSNode(FName(*Name)); ++Suffix)
		{
			Name = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
		}
		return FName(*Name);
	}

	/**
	 * A component under Body for every wardrobe item the Blueprint doesn't have one for yet: a skeletal mesh for a
	 * garment, a groom (with its binding) for a groom, hidden until worn and tagged for the actor to find. They make
	 * the Blueprint a full one (it opens with its viewport) and list what the character can wear in its Components
	 * panel, where each can be adjusted. Returns how many were added.
	 */
	int32 AddItemComponents(UBlueprint* Blueprint, const UDTHWardrobe* Wardrobe)
	{
		USimpleConstructionScript* Construction = Blueprint->SimpleConstructionScript;
		ADTHCharacter* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject<ADTHCharacter>() : nullptr;
		if (!Construction || !Defaults) return 0;
		TSet<const UDTHWardrobeItem*> Present;
		for (const USCS_Node* Node : Construction->GetAllNodes())
		{
			if (const USceneComponent* Template = Cast<USceneComponent>(Node->ComponentTemplate))
			{
				if (const UDTHWardrobeItem* Item = ADTHCharacter::TaggedItem(Template)) Present.Add(Item);
			}
		}
		int32 Added = 0;
		for (UDTHWardrobeItem* Item : Wardrobe->AllItems())
		{
			if (Present.Contains(Item) || (!Item->Mesh && !Item->Groom)) continue;
			UClass* Class = Item->Mesh ? USkeletalMeshComponent::StaticClass() : UGroomComponent::StaticClass();
			USCS_Node* Node = Construction->CreateNode(Class, ComponentName(*Construction, *Item));
			if (USkeletalMeshComponent* Garment = Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
			{
				Garment->SetSkeletalMeshAsset(Item->Mesh);
			}
			else if (UGroomComponent* Groom = Cast<UGroomComponent>(Node->ComponentTemplate))
			{
				Groom->BindingAsset = Item->GroomBinding;
				Groom->GroomAsset = Item->Groom;
			}
			USceneComponent* Template = CastChecked<USceneComponent>(Node->ComponentTemplate);
			Template->SetVisibility(false);
			ADTHCharacter::TagItemComponent(Template, Item);
			Node->SetParent(Defaults->Body.Get());
			Construction->AddNode(Node);
			++Added;
		}
		if (Added > 0)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
		return Added;
	}

	/**
	 * BP_<Character> in Folder: a DTH Character on Wardrobe, wearing StartState, with the body's mesh and a component
	 * per wardrobe item. An existing one keeps its settings and gets components for new items.
	 */
	void BuildBlueprint(const FString& Folder, const FString& Character, UDTHWardrobe* Wardrobe, const FString& StartState, FDTHImportReport& Report)
	{
		const FString Name = TEXT("BP_") + Character;
		const FString PackageName = Folder / Name;
		UObject* Found = StaticLoadObject(UObject::StaticClass(), nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		UBlueprint* Blueprint = Cast<UBlueprint>(Found);
		if (Found && !Blueprint)
		{
			Report.Error(FText::Format(LOCTEXT("BlueprintOtherClass", "{0} is a {1}, not a Blueprint; no character blueprint was made."),
				FText::FromString(PackageName), FText::FromString(Found->GetClass()->GetName())));
			return;
		}
		const bool bExisted = Blueprint != nullptr;
		if (!Blueprint)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Blueprint = FKismetEditorUtilities::CreateBlueprint(ADTHCharacter::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			if (!Blueprint)
			{
				Report.Error(FText::Format(LOCTEXT("BlueprintFailed", "Could not create {0}."), FText::FromString(PackageName)));
				return;
			}
			FAssetRegistryModule::AssetCreated(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}
		ADTHCharacter* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject<ADTHCharacter>() : nullptr;
		if (!Defaults)
		{
			Report.Error(FText::Format(LOCTEXT("BlueprintNotCharacter", "{0} is not a DTH Character; its wardrobe was not set up."), FText::FromString(PackageName)));
			return;
		}
		Defaults->Modify();
		if (!bExisted || !Defaults->Wardrobe)
		{
			Defaults->Wardrobe = Wardrobe;
		}
		if (!bExisted)
		{
			Defaults->WardrobeState = StartState;
		}
		// The body in the defaults too, so the Blueprint's viewport, Components panel and thumbnail show it.
		if (Defaults->Body && !Defaults->Body->GetSkeletalMeshAsset())
		{
			Defaults->Body->Modify();
			Defaults->Body->SetSkeletalMeshAsset(Wardrobe->Body);
		}
		const int32 Added = AddItemComponents(Blueprint, Wardrobe);
		// Compiled after the defaults are set: a Blueprint class's new instances copy only the properties that differed
		// from its parent's when it was compiled.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		if (bExisted)
		{
			Report.Info(FText::Format(LOCTEXT("BlueprintKept", "{0} exists; kept its settings and added components for {1} new wardrobe items."), FText::FromString(Name), Added));
		}
		else
		{
			Report.Info(FText::Format(LOCTEXT("BlueprintMade", "{0}: the character with its wardrobe ({1} garment and groom components), wearing {2}. Place it in a level; key its Wardrobe State in Sequencer to change outfits."),
				FText::FromString(Name), Added, FText::FromString(StartState)));
		}
	}
}

FName DTHImport::GarmentSlot(const FString& Name)
{
	const FString Lower = Name.ToLower();
	for (const auto& Slot : GarmentSlots)
	{
		TArray<FString> Tokens;
		FString(Slot.Value).ParseIntoArray(Tokens, TEXT(","));
		for (const FString& Token : Tokens)
		{
			if (Lower.Contains(Token)) return FName(Slot.Key);
		}
	}
	return FName(TEXT("Accessory"));
}

FName DTHImport::GroomSlot(const FString& Name)
{
	const FString Lower = Name.ToLower();
	for (const auto& Slot : GroomSlots)
	{
		if (Lower.Contains(Slot.Key)) return FName(Slot.Value);
	}
	return FName(TEXT("Hair"));
}

bool DTHImport::IsBaseSlot(FName Slot)
{
	return Slot == TEXT("Eyebrows") || Slot == TEXT("Eyelashes") || Slot == TEXT("Vellus");
}

FString DTHImport::DefaultOutfitName(const FDTHExport& Export, const TArray<int32>& MeshIndices)
{
	const FString Prefix = TEXT("SKM_") + Sanitize(Export.Character) + TEXT("_");
	FString Shared;
	bool bFirst = true;
	for (const int32 Index : MeshIndices)
	{
		const FDTHMeshEntry& Mesh = Export.Meshes[Index];
		if (Mesh.IsBody()) continue;
		FString Name = FPaths::GetBaseFilename(Mesh.File);
		Name.RemoveFromStart(Prefix);
		FString First;
		if (!Name.Split(TEXT("_"), &First, nullptr)) First = Name;
		if (bFirst)
		{
			Shared = First;
			bFirst = false;
		}
		else if (!Shared.Equals(First, ESearchCase::IgnoreCase))
		{
			Shared.Reset();
			break;
		}
	}
	return Shared.IsEmpty() ? Sanitize(Export.Character) : Shared;
}

UDTHWardrobe* DTHImport::FindWardrobe(const FString& Folder)
{
	FString Root = Folder;
	Root.RemoveFromEnd(TEXT("/"));
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get()
		.GetAssetsByClass(UDTHWardrobe::StaticClass()->GetClassPathName(), Assets);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.PackagePath.ToString() == Root / TEXT("Wardrobe"))
		{
			return Cast<UDTHWardrobe>(Asset.GetAsset());
		}
	}
	return nullptr;
}

void DTHImport::BuildWardrobe(const FDTHExport& Export, const FString& Folder, USkeletalMesh* Body, const TArray<USkeletalMesh*>& Garments,
	const TArray<FDTHBuiltGroom>& Grooms, const FString& OutfitName, bool bBaseImport, FDTHImportReport& Report)
{
	FString Root = Folder;
	Root.RemoveFromEnd(TEXT("/"));
	if (!Body)
	{
		Report.Error(LOCTEXT("NoBodyForWardrobe", "No body mesh, so no wardrobe was made."));
		return;
	}
	// The character's token: the existing wardrobe's (add-on exports may name the character differently), else the export's.
	UDTHWardrobe* Existing = FindWardrobe(Root);
	FString Character = Sanitize(Export.Character);
	if (Existing)
	{
		Character = Existing->GetName();
		Character.RemoveFromStart(TEXT("DA_Wardrobe_"));
		if (Existing->Body && Existing->Body != Body)
		{
			Report.Error(FText::Format(LOCTEXT("OtherBody", "{0} is on {1}, not {2}; nothing was added to it."),
				FText::FromString(Existing->GetName()), FText::FromString(Existing->Body->GetName()), FText::FromString(Body->GetName())));
			return;
		}
	}
	FWardrobeBuilder Builder(Root, Character, FPaths::GetCleanFilename(Export.Folder), Report);

	// Short item names: the garment's name after SKM_<Character>_, without the outfit's own prefix when that is unambiguous.
	const FString MeshPrefix = TEXT("SKM_") + Sanitize(Export.Character) + TEXT("_");
	TArray<UDTHWardrobeItem*> OutfitItems, BaseItems, HairItems;
	for (USkeletalMesh* Garment : Garments)
	{
		if (!Garment || Garment == Body) continue;
		FString Name = Garment->GetName();
		Name.RemoveFromStart(MeshPrefix);
		FString Short = Name;
		if (Short.RemoveFromStart(OutfitName + TEXT("_")) && !Short.IsEmpty())
		{
			const bool bTaken = Builder.Known.ContainsByPredicate([&](const UDTHWardrobeItem* Item) { return Item->Mesh != Garment && Item->IsNamed(Short); });
			if (!bTaken) Name = Short;
		}
		if (UDTHWardrobeItem* Item = Builder.Item(Garment, nullptr, nullptr, Name, DTHImport::GarmentSlot(Garment->GetName())))
		{
			OutfitItems.Add(Item);
		}
	}
	for (const FDTHBuiltGroom& Groom : Grooms)
	{
		if (!Groom.Groom) continue;
		const FName Slot = DTHImport::GroomSlot(Groom.Name);
		UDTHWardrobeItem* Item = Builder.Item(nullptr, Groom.Groom, Groom.Binding, Groom.Name, Slot);
		if (!Item) continue;
		(DTHImport::IsBaseSlot(Slot) ? BaseItems : HairItems).Add(Item);
		if (!Groom.Binding)
		{
			Report.Warning(FText::Format(LOCTEXT("ItemUnbound", "Wardrobe item {0} has no groom binding, so it won't follow the body."), FText::FromString(Item->GetName())));
		}
	}

	UDTHWardrobe* Wardrobe = Builder.Wardrobe(Body);
	if (!Wardrobe)
	{
		Report.Error(LOCTEXT("NoWardrobe", "No wardrobe or character blueprint was made (see above)."));
		return;
	}
	for (UDTHWardrobeItem* Item : BaseItems)
	{
		Wardrobe->BaseItems.AddUnique(Item);
	}
	FString StartState;
	if (OutfitItems.Num() > 0)
	{
		TArray<UDTHWardrobeItem*> Items = OutfitItems;
		Items.Append(HairItems);
		if (UDTHOutfit* Outfit = Builder.Outfit(OutfitName, Items))
		{
			Wardrobe->Outfits.AddUnique(Outfit);
			StartState = OutfitName;
		}
	}
	else if (HairItems.Num() > 0)
	{
		for (UDTHWardrobeItem* Item : HairItems)
		{
			Wardrobe->ExtraItems.AddUnique(Item);
		}
		Report.Info(FText::Format(LOCTEXT("ExtraItems", "No garments in this export: its grooms ({0}) were added to the wardrobe on their own. Wear one with +Name, e.g. \"<Outfit> +{1}\"."),
			HairItems.Num(), FText::FromString(HairItems[0]->WardrobeName())));
	}
	if (bBaseImport)
	{
		if (UDTHOutfit* Nude = Builder.Outfit(TEXT("Nude"), HairItems))
		{
			Wardrobe->Outfits.AddUnique(Nude);
			if (StartState.IsEmpty()) StartState = TEXT("Nude");
		}
	}
	Wardrobe->MarkPackageDirty();
	Report.Info(FText::Format(LOCTEXT("WardrobeDone", "Wardrobe {0}: {1} outfits, {2} base items, {3} extra items; {4} new items."),
		FText::FromString(Wardrobe->GetName()), Wardrobe->Outfits.Num(), Wardrobe->BaseItems.Num(), Wardrobe->ExtraItems.Num(), Builder.Created));

	BuildBlueprint(Root, Character, Wardrobe, StartState, Report);
}

#undef LOCTEXT_NAMESPACE
