#include "DTHCharacter.h"

#include "Components/SkeletalMeshComponent.h"
#include "DTHWardrobeLog.h"
#include "DTHWardrobeTypes.h"
#include "GroomComponent.h"

namespace
{
	/** Tag on every component the wardrobe makes; the next tag is the item's object path. */
	const FName WardrobeTag(TEXT("DTHWardrobe"));

	struct FParsedState
	{
		FString Outfit;
		TArray<FString> Add;
		TArray<FString> Remove;
		TArray<FName> RemoveSlots;
	};

	FParsedState Parse(const FString& State)
	{
		FString Spaced = State.Replace(TEXT(","), TEXT(" "));
		TArray<FString> Tokens;
		Spaced.ParseIntoArrayWS(Tokens);
		FParsedState Parsed;
		for (const FString& Token : Tokens)
		{
			if (Token.StartsWith(TEXT("+")))
			{
				if (Token.Len() > 1) Parsed.Add.Add(Token.Mid(1));
			}
			else if (Token.StartsWith(TEXT("-")))
			{
				FString Name = Token.Mid(1);
				if (Name.StartsWith(TEXT("slot:"), ESearchCase::IgnoreCase))
				{
					Parsed.RemoveSlots.Add(FName(*Name.Mid(5)));
				}
				else if (!Name.IsEmpty())
				{
					Parsed.Remove.Add(Name);
				}
			}
			else if (Parsed.Outfit.IsEmpty())
			{
				Parsed.Outfit = Token;
			}
		}
		return Parsed;
	}

	FString Compose(const FParsedState& State)
	{
		TArray<FString> Tokens;
		if (!State.Outfit.IsEmpty()) Tokens.Add(State.Outfit);
		for (const FString& Name : State.Remove) Tokens.Add(TEXT("-") + Name);
		for (const FName& Slot : State.RemoveSlots) Tokens.Add(TEXT("-slot:") + Slot.ToString());
		for (const FString& Name : State.Add) Tokens.Add(TEXT("+") + Name);
		return FString::Join(Tokens, TEXT(" "));
	}

	bool RemoveName(TArray<FString>& Names, const UDTHWardrobeItem* Item)
	{
		return Names.RemoveAll([Item](const FString& Name) { return Item->IsNamed(Name); }) > 0;
	}

	/**
	 * What State wears: the base items and the outfit's, minus removals, plus additions (each replacing the worn items
	 * of its slot). Current is the outfit a state without an outfit name keeps. Warns about unknown names when Owner is set.
	 */
	TArray<UDTHWardrobeItem*> Evaluate(const UDTHWardrobe& Wardrobe, UDTHOutfit* Current, const FParsedState& State, UDTHOutfit*& OutOutfit,
		const AActor* Owner)
	{
		OutOutfit = Current;
		if (!State.Outfit.IsEmpty())
		{
			OutOutfit = Wardrobe.FindOutfit(State.Outfit);
			if (!OutOutfit && Owner)
			{
				UE_LOG(LogDTHWardrobe, Warning, TEXT("%s: no outfit named %s; wearing the base items only."), *Owner->GetName(), *State.Outfit);
			}
		}
		else if (!OutOutfit && Wardrobe.Outfits.Num() > 0)
		{
			OutOutfit = Wardrobe.Outfits[0];
		}

		TArray<UDTHWardrobeItem*> Wearing;
		for (UDTHWardrobeItem* Item : Wardrobe.BaseItems)
		{
			if (Item) Wearing.AddUnique(Item);
		}
		if (OutOutfit)
		{
			for (UDTHWardrobeItem* Item : OutOutfit->Items)
			{
				if (Item) Wearing.AddUnique(Item);
			}
		}
		Wearing.RemoveAll([&State](const UDTHWardrobeItem* Item)
		{
			return State.RemoveSlots.Contains(Item->Slot)
				|| State.Remove.ContainsByPredicate([Item](const FString& Name) { return Item->IsNamed(Name); });
		});
		for (const FString& Name : State.Add)
		{
			UDTHWardrobeItem* Item = Wardrobe.FindItem(Name);
			if (!Item)
			{
				if (Owner) UE_LOG(LogDTHWardrobe, Warning, TEXT("%s: no wardrobe item named %s."), *Owner->GetName(), *Name);
				continue;
			}
			if (!Item->Slot.IsNone())
			{
				Wearing.RemoveAll([Item](const UDTHWardrobeItem* Other) { return Other != Item && Other->Slot == Item->Slot; });
			}
			Wearing.AddUnique(Item);
		}
		return Wearing;
	}
}

ADTHCharacter::ADTHCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	// Curves the post-process blueprint computes (joint correctives) reach the garments even when the body has no
	// main animation instance (an idle actor).
	Body->bPropagateCurvesToFollowers = true;
	RootComponent = Body;
}

void ADTHCharacter::SetWardrobeState(const FString& NewState)
{
	WardrobeState = NewState;
	ApplyState(true);
}

TArray<FString> ADTHCharacter::GetOutfitNames() const
{
	TArray<FString> Names;
	if (!Wardrobe) return Names;
	for (const UDTHOutfit* Outfit : Wardrobe->Outfits)
	{
		if (Outfit) Names.Add(Outfit->WardrobeName());
	}
	return Names;
}

TArray<FString> ADTHCharacter::GetItemNames(FName Slot) const
{
	TArray<FString> Names;
	for (const UDTHWardrobeItem* Item : GetItems(Slot))
	{
		Names.Add(Item->WardrobeName());
	}
	return Names;
}

TArray<UDTHWardrobeItem*> ADTHCharacter::GetItems(FName Slot) const
{
	TArray<UDTHWardrobeItem*> Items = Wardrobe ? Wardrobe->AllItems() : TArray<UDTHWardrobeItem*>();
	if (!Slot.IsNone())
	{
		Items.RemoveAll([Slot](const UDTHWardrobeItem* Item) { return Item->Slot != Slot; });
	}
	return Items;
}

TArray<FName> ADTHCharacter::GetSlots() const
{
	TArray<FName> Slots;
	for (const UDTHWardrobeItem* Item : GetItems(NAME_None))
	{
		if (!Item->Slot.IsNone()) Slots.AddUnique(Item->Slot);
	}
	return Slots;
}

UDTHWardrobeItem* ADTHCharacter::FindItem(const FString& Item) const
{
	return Wardrobe ? Wardrobe->FindItem(Item) : nullptr;
}

bool ADTHCharacter::IsWearing(const FString& Item) const
{
	const UDTHWardrobeItem* Found = FindItem(Item);
	return Found && WornItems.Contains(Found);
}

FString ADTHCharacter::GetCurrentOutfitName() const
{
	return CurrentOutfit ? CurrentOutfit->WardrobeName() : FString();
}

TArray<UDTHWardrobeItem*> ADTHCharacter::GetWornItems() const
{
	TArray<UDTHWardrobeItem*> Items;
	for (UDTHWardrobeItem* Item : WornItems)
	{
		Items.Add(Item);
	}
	return Items;
}

USceneComponent* ADTHCharacter::GetItemComponent(const UDTHWardrobeItem* Item) const
{
	const TObjectPtr<USceneComponent>* Found = ItemComponents.Find(const_cast<UDTHWardrobeItem*>(Item));
	return Found ? Found->Get() : nullptr;
}

bool ADTHCharacter::SetOutfit(const FString& Outfit)
{
	EnsureReady();
	if (!Wardrobe || !Wardrobe->FindOutfit(Outfit))
	{
		UE_LOG(LogDTHWardrobe, Warning, TEXT("%s: Set Outfit: no outfit named %s."), *GetName(), *Outfit);
		return false;
	}
	SetWardrobeState(Outfit);
	return true;
}

bool ADTHCharacter::WearItem(const FString& Item)
{
	EnsureReady();
	UDTHWardrobeItem* Found = FindItem(Item);
	if (!Found)
	{
		UE_LOG(LogDTHWardrobe, Warning, TEXT("%s: Wear Item: no item named %s."), *GetName(), *Item);
		return false;
	}
	FParsedState State = Parse(WardrobeState);
	if (State.Outfit.IsEmpty()) State.Outfit = GetCurrentOutfitName();
	RemoveName(State.Remove, Found);
	RemoveName(State.Add, Found);
	// Without its removal it may be worn already (an outfit item); else it's added, replacing its slot.
	UDTHOutfit* Outfit = nullptr;
	if (!Evaluate(*Wardrobe, CurrentOutfit, State, Outfit, nullptr).Contains(Found))
	{
		// It replaces its slot, so earlier additions to that slot no longer do anything.
		if (!Found->Slot.IsNone())
		{
			State.Add.RemoveAll([this, Found](const FString& Name)
			{
				const UDTHWardrobeItem* Other = Wardrobe->FindItem(Name);
				return Other && Other->Slot == Found->Slot;
			});
		}
		State.Add.Add(Found->WardrobeName());
	}
	SetWardrobeState(Compose(State));
	return true;
}

bool ADTHCharacter::RemoveItem(const FString& Item)
{
	EnsureReady();
	UDTHWardrobeItem* Found = FindItem(Item);
	if (!Found)
	{
		UE_LOG(LogDTHWardrobe, Warning, TEXT("%s: Remove Item: no item named %s."), *GetName(), *Item);
		return false;
	}
	FParsedState State = Parse(WardrobeState);
	if (State.Outfit.IsEmpty()) State.Outfit = GetCurrentOutfitName();
	RemoveName(State.Add, Found);
	UDTHOutfit* Outfit = nullptr;
	if (Evaluate(*Wardrobe, CurrentOutfit, State, Outfit, nullptr).Contains(Found))
	{
		State.Remove.Add(Found->WardrobeName());
	}
	SetWardrobeState(Compose(State));
	return true;
}

bool ADTHCharacter::ToggleItem(const FString& Item)
{
	EnsureReady();
	if (IsWearing(Item))
	{
		RemoveItem(Item);
	}
	else
	{
		WearItem(Item);
	}
	return IsWearing(Item);
}

void ADTHCharacter::RemoveSlot(FName Slot)
{
	EnsureReady();
	if (!Wardrobe || Slot.IsNone()) return;
	FParsedState State = Parse(WardrobeState);
	if (State.Outfit.IsEmpty()) State.Outfit = GetCurrentOutfitName();
	State.Add.RemoveAll([this, Slot](const FString& Name)
	{
		const UDTHWardrobeItem* Item = Wardrobe->FindItem(Name);
		return Item && Item->Slot == Slot;
	});
	State.RemoveSlots.AddUnique(Slot);
	SetWardrobeState(Compose(State));
}

void ADTHCharacter::ResetToOutfit()
{
	EnsureReady();
	SetWardrobeState(GetCurrentOutfitName());
}

void ADTHCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (Wardrobe && Wardrobe->Body && Body->GetSkeletalMeshAsset() != Wardrobe->Body)
	{
		Body->SetSkeletalMeshAsset(Wardrobe->Body);
	}
	ItemComponents.Reset();
	AppliedState.Reset();
	CurrentOutfit = nullptr;
	WornItems.Reset();
	// Made here, they are construction components: a rerun (any property change) destroys and remakes them.
	EnsureComponents(EComponentCreationMethod::UserConstructionScript);
	ApplyState(false);
}

void ADTHCharacter::BeginPlay()
{
	// Before Super, which runs the Blueprint's Begin Play: that may already call the Wardrobe functions.
	EnsureReady();
	Super::BeginPlay();
}

void ADTHCharacter::EnsureReady()
{
	// A level-loaded or play-in-editor copy keeps its construction components but not this actor's transient state.
	if (Wardrobe && ItemComponents.IsEmpty())
	{
		AppliedState.Reset();
		WornItems.Reset();
		EnsureComponents(EComponentCreationMethod::Instance);
		ApplyState(false);
	}
}

void ADTHCharacter::TagItemComponent(USceneComponent* Component, const UDTHWardrobeItem* Item)
{
	Component->ComponentTags.RemoveAll([](const FName& Tag) { return Tag == WardrobeTag || Tag.ToString().StartsWith(TEXT("/")); });
	Component->ComponentTags.Insert({ WardrobeTag, FName(*Item->GetPathName()) }, 0);
}

UDTHWardrobeItem* ADTHCharacter::TaggedItem(const USceneComponent* Component)
{
	if (!Component || Component->ComponentTags.Num() < 2 || Component->ComponentTags[0] != WardrobeTag) return nullptr;
	const FString Path = Component->ComponentTags[1].ToString();
	UDTHWardrobeItem* Item = FindObject<UDTHWardrobeItem>(nullptr, *Path);
	return Item ? Item : LoadObject<UDTHWardrobeItem>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void ADTHCharacter::EnsureComponents(EComponentCreationMethod CreationMethod)
{
	if (!Wardrobe) return;
	// The Blueprint's own components (the import adds one per item) and earlier made ones.
	TInlineComponentArray<USceneComponent*> Existing(this);
	for (USceneComponent* Component : Existing)
	{
		UDTHWardrobeItem* Item = TaggedItem(Component);
		if (!Item || ItemComponents.Contains(Item)) continue;
		ItemComponents.Add(Item, Component);
		if (USkeletalMeshComponent* Garment = Cast<USkeletalMeshComponent>(Component))
		{
			if (Garment->LeaderPoseComponent.Get() != Body)
			{
				Garment->SetLeaderPoseComponent(Body, true, false);
			}
		}
	}
	for (UDTHWardrobeItem* Item : Wardrobe->AllItems())
	{
		if (!ItemComponents.Contains(Item))
		{
			if (USceneComponent* Component = MakeComponent(Item, CreationMethod))
			{
				ItemComponents.Add(Item, Component);
			}
		}
	}
}

USceneComponent* ADTHCharacter::MakeComponent(UDTHWardrobeItem* Item, EComponentCreationMethod CreationMethod)
{
	FString Safe = Item->WardrobeName();
	for (TCHAR& Char : Safe)
	{
		if (!FChar::IsAlnum(Char)) Char = TEXT('_');
	}
	const FName Name = MakeUniqueObjectName(this, USceneComponent::StaticClass(), FName(*(TEXT("Wardrobe_") + Safe)));
	USceneComponent* Component = nullptr;
	if (Item->Mesh)
	{
		USkeletalMeshComponent* Garment = NewObject<USkeletalMeshComponent>(this, Name, RF_Transactional);
		Garment->SetSkeletalMeshAsset(Item->Mesh);
		Garment->SetupAttachment(Body);
		Component = Garment;
	}
	else if (Item->Groom)
	{
		// Attach first, binding before groom: the groom initializes once, against the attach parent's mesh.
		UGroomComponent* Groom = NewObject<UGroomComponent>(this, Name, RF_Transactional);
		Groom->SetupAttachment(Body);
		Groom->SetGroomAsset(Item->Groom, Item->GroomBinding);
		Component = Groom;
	}
	else
	{
		UE_LOG(LogDTHWardrobe, Warning, TEXT("%s: wardrobe item %s has neither a mesh nor a groom."), *GetName(), *Item->GetName());
		return nullptr;
	}
	TagItemComponent(Component, Item);
	Component->CreationMethod = CreationMethod;
	Component->SetVisibility(false);
	Component->RegisterComponent();
	if (USkeletalMeshComponent* Garment = Cast<USkeletalMeshComponent>(Component))
	{
		Garment->SetLeaderPoseComponent(Body, true, false);
	}
	return Component;
}

void ADTHCharacter::ApplyState(bool bNotify)
{
	if (!Wardrobe || (AppliedState == WardrobeState && !WornItems.IsEmpty())) return;
	UDTHOutfit* Outfit = nullptr;
	const TArray<UDTHWardrobeItem*> Wearing = Evaluate(*Wardrobe, CurrentOutfit, Parse(WardrobeState), Outfit, this);
	CurrentOutfit = Outfit;

	const TArray<TObjectPtr<UDTHWardrobeItem>> Before = WornItems;
	WornItems.Reset();
	for (UDTHWardrobeItem* Item : Wearing)
	{
		WornItems.Add(Item);
	}
	for (const TPair<TObjectPtr<UDTHWardrobeItem>, TObjectPtr<USceneComponent>>& Entry : ItemComponents)
	{
		if (Entry.Value)
		{
			Entry.Value->SetVisibility(Wearing.Contains(Entry.Key.Get()));
		}
	}

	const FString OldState = AppliedState;
	AppliedState = WardrobeState;
	if (!bNotify || OldState == WardrobeState) return;
	UE_LOG(LogDTHWardrobe, Log, TEXT("%s: wardrobe state \"%s\": outfit %s, %d items worn."), *GetName(), *WardrobeState,
		CurrentOutfit ? *CurrentOutfit->WardrobeName() : TEXT("none"), WornItems.Num());
	for (UDTHWardrobeItem* Item : Before)
	{
		if (Item && !Wearing.Contains(Item)) OnItemRemoved.Broadcast(Item);
	}
	for (UDTHWardrobeItem* Item : Wearing)
	{
		if (!Before.Contains(Item)) OnItemWorn.Broadcast(Item);
	}
	OnWardrobeChanged.Broadcast(OldState, WardrobeState, Wearing);
}
