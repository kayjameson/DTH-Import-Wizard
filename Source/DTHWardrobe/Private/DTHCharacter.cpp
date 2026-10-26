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

TArray<UDTHWardrobeItem*> ADTHCharacter::GetWornItems() const
{
	TArray<UDTHWardrobeItem*> Items;
	for (UDTHWardrobeItem* Item : Worn)
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
	// Made here, they are construction components: a rerun (any property change) destroys and remakes them.
	EnsureComponents(EComponentCreationMethod::UserConstructionScript);
	ApplyState(false);
}

void ADTHCharacter::BeginPlay()
{
	Super::BeginPlay();
	// A level-loaded actor keeps its construction components but not this actor's transient bookkeeping.
	if (ItemComponents.IsEmpty())
	{
		AppliedState.Reset();
		EnsureComponents(EComponentCreationMethod::Instance);
		ApplyState(false);
	}
}

void ADTHCharacter::EnsureComponents(EComponentCreationMethod CreationMethod)
{
	if (!Wardrobe) return;
	TInlineComponentArray<USceneComponent*> Existing(this);
	for (USceneComponent* Component : Existing)
	{
		if (Component && Component->ComponentTags.Num() >= 2 && Component->ComponentTags[0] == WardrobeTag)
		{
			if (UDTHWardrobeItem* Item = FindObject<UDTHWardrobeItem>(nullptr, *Component->ComponentTags[1].ToString()))
			{
				ItemComponents.Add(Item, Component);
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
	Component->ComponentTags = { WardrobeTag, FName(*Item->GetPathName()) };
	Component->CreationMethod = CreationMethod;
	Component->SetVisibility(false);
	Component->RegisterComponent();
	if (USkeletalMeshComponent* Garment = Cast<USkeletalMeshComponent>(Component))
	{
		Garment->SetLeaderPoseComponent(Body, true, false);
	}
	return Component;
}

void ADTHCharacter::ApplyState(bool bLog)
{
	if (!Wardrobe || (AppliedState == WardrobeState && !Worn.IsEmpty())) return;
	const FParsedState Parsed = Parse(WardrobeState);

	if (!Parsed.Outfit.IsEmpty())
	{
		CurrentOutfit = Wardrobe->FindOutfit(Parsed.Outfit);
		if (!CurrentOutfit)
		{
			UE_LOG(LogDTHWardrobe, Warning, TEXT("%s: no outfit named %s; wearing the base items only."), *GetName(), *Parsed.Outfit);
		}
	}
	else if (!CurrentOutfit && Wardrobe->Outfits.Num() > 0)
	{
		CurrentOutfit = Wardrobe->Outfits[0];
	}

	TArray<UDTHWardrobeItem*> Wearing;
	for (UDTHWardrobeItem* Item : Wardrobe->BaseItems)
	{
		if (Item) Wearing.AddUnique(Item);
	}
	if (CurrentOutfit)
	{
		for (UDTHWardrobeItem* Item : CurrentOutfit->Items)
		{
			if (Item) Wearing.AddUnique(Item);
		}
	}
	for (const FString& Name : Parsed.Add)
	{
		UDTHWardrobeItem* Item = Wardrobe->FindItem(Name);
		if (!Item)
		{
			UE_LOG(LogDTHWardrobe, Warning, TEXT("%s: no wardrobe item named %s."), *GetName(), *Name);
			continue;
		}
		if (!Item->Slot.IsNone())
		{
			Wearing.RemoveAll([Item](const UDTHWardrobeItem* Other) { return Other != Item && Other->Slot == Item->Slot; });
		}
		Wearing.AddUnique(Item);
	}
	Wearing.RemoveAll([&Parsed](const UDTHWardrobeItem* Item)
	{
		return Parsed.RemoveSlots.Contains(Item->Slot)
			|| Parsed.Remove.ContainsByPredicate([Item](const FString& Name) { return Item->IsNamed(Name); });
	});

	Worn.Reset();
	for (UDTHWardrobeItem* Item : Wearing)
	{
		Worn.Add(Item);
	}
	for (const TPair<TObjectPtr<UDTHWardrobeItem>, TObjectPtr<USceneComponent>>& Entry : ItemComponents)
	{
		if (Entry.Value)
		{
			Entry.Value->SetVisibility(Wearing.Contains(Entry.Key.Get()));
		}
	}
	if (bLog && AppliedState != WardrobeState)
	{
		UE_LOG(LogDTHWardrobe, Log, TEXT("%s: wardrobe state \"%s\": outfit %s, %d items worn."), *GetName(), *WardrobeState,
			CurrentOutfit ? *CurrentOutfit->WardrobeName() : TEXT("none"), Worn.Num());
	}
	AppliedState = WardrobeState;
}
