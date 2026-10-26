#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "DTHCharacter.generated.h"

class UDTHOutfit;
class UDTHWardrobe;
class UDTHWardrobeItem;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDTHWardrobeChangedSignature, const FString&, OldState, const FString&, NewState,
	const TArray<UDTHWardrobeItem*>&, WornItems);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDTHWardrobeItemSignature, UDTHWardrobeItem*, Item);

/**
 * A DazToHue character with a wardrobe: the body, and every garment and groom the wardrobe knows, created once
 * (garments follow the body as leader pose, grooms are bound to it) and shown or hidden by WardrobeState.
 *
 * WardrobeState is a keyable string (Sequencer: a String track on Wardrobe State):
 *   Witch                 the outfit named Witch, plus the wardrobe's base items (brows, lashes)
 *   Witch -Hat -Heels     without those items
 *   Witch -slot:Feet      without every item of a slot
 *   Witch +Ponytail       with an item from anywhere in the wardrobe; it replaces the worn items of its slot
 *   -Hat                  keeps the current outfit, changes the overrides
 * Names are case-insensitive, separated by spaces or commas. Empty: the wardrobe's first outfit. Removals apply
 * before additions, so "Witch -slot:Feet +Boots" wears the boots.
 *
 * The Blueprint functions (Set Outfit, Wear / Remove / Toggle Item, Remove Slot...) edit WardrobeState, so Blueprint,
 * Sequencer and the Details panel all go through it, and every change fires On Wardrobe Changed.
 */
UCLASS(Blueprintable, ClassGroup = DazToHue, meta = (DisplayName = "DTH Character"))
class DTHWARDROBE_API ADTHCharacter : public AActor
{
	GENERATED_BODY()

public:
	ADTHCharacter();

	/** The body. Animate this component (Sequencer, animation blueprint, Control Rig); everything worn follows it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character")
	TObjectPtr<USkeletalMeshComponent> Body;

	/** Everything this character can wear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
	TObjectPtr<UDTHWardrobe> Wardrobe;

	/** What is worn: an outfit name and overrides (see the class comment). Keyable in Sequencer. */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetWardrobeState, Category = "Wardrobe")
	FString WardrobeState;

	/** The outfit worn now (null for an unknown outfit name). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Wardrobe")
	TObjectPtr<UDTHOutfit> CurrentOutfit;

	/** The items worn now: base items, the outfit's, with the state's overrides. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Wardrobe")
	TArray<TObjectPtr<UDTHWardrobeItem>> WornItems;

	/** After every change of what is worn (Blueprint, Sequencer, Details panel). */
	UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
	FDTHWardrobeChangedSignature OnWardrobeChanged;

	/** For each item put on by a change. */
	UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
	FDTHWardrobeItemSignature OnItemWorn;

	/** For each item taken off by a change. */
	UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
	FDTHWardrobeItemSignature OnItemRemoved;

	/**
	 * Applies a wardrobe state. Sequencer calls this for a keyed Wardrobe State, also while scrubbing in the editor
	 * (CallInEditor lets it run there).
	 */
	UFUNCTION(BlueprintSetter, Category = "Wardrobe", meta = (CallInEditor = "true"))
	void SetWardrobeState(const FString& NewState);

	// Inventory

	/** The names of the wardrobe's outfits, in order. */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	TArray<FString> GetOutfitNames() const;

	/** The names of every item the wardrobe knows (base, outfit and extra items); only those of Slot unless it is None. */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	TArray<FString> GetItemNames(FName Slot) const;

	/** Every item the wardrobe knows; only those of Slot unless it is None. */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	TArray<UDTHWardrobeItem*> GetItems(FName Slot) const;

	/** The slots the wardrobe's items use. */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	TArray<FName> GetSlots() const;

	/** The item named Item (case-insensitive), or null. */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	UDTHWardrobeItem* FindItem(const FString& Item) const;

	/** True when the item named Item is worn now. */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	bool IsWearing(const FString& Item) const;

	/** The name of the outfit worn now (empty for none). */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	FString GetCurrentOutfitName() const;

	/** The items worn now. */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	TArray<UDTHWardrobeItem*> GetWornItems() const;

	/** The component that shows Item (a skeletal mesh or groom component), or null. */
	UFUNCTION(BlueprintPure, Category = "Wardrobe|Inventory")
	USceneComponent* GetItemComponent(const UDTHWardrobeItem* Item) const;

	// Changes (each edits Wardrobe State)

	/** Wears the outfit named Outfit, without overrides. False for an unknown outfit. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	bool SetOutfit(const FString& Outfit);

	/** Puts on the item named Item; an item the outfit doesn't wear replaces the worn items of its slot. False for an unknown item. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	bool WearItem(const FString& Item);

	/** Takes off the item named Item. False for an unknown item. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	bool RemoveItem(const FString& Item);

	/** Takes the item named Item off when it is worn, else puts it on. Returns whether it is worn afterwards. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	bool ToggleItem(const FString& Item);

	/** Takes off every item of Slot. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	void RemoveSlot(FName Slot);

	/** Back to the current outfit as it is, without overrides. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	void ResetToOutfit();

	/**
	 * Marks Component as the one that shows Item. The import tags the garment and groom components it adds to
	 * BP_<Character>; components the actor makes itself (items added to the wardrobe later) are tagged the same way.
	 */
	static void TagItemComponent(USceneComponent* Component, const UDTHWardrobeItem* Item);

	/** The item Component was tagged for, or null. */
	static UDTHWardrobeItem* TaggedItem(const USceneComponent* Component);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	/** Components found or made and the state applied, if this copy of the actor has none yet. */
	void EnsureReady();
	/** A component per wardrobe item: existing ones (from construction, saved with the level) are found, missing ones made. */
	void EnsureComponents(EComponentCreationMethod CreationMethod);
	USceneComponent* MakeComponent(UDTHWardrobeItem* Item, EComponentCreationMethod CreationMethod);
	/**
	 * Worn items for WardrobeState, then visibility to match. bNotify (explicit sets, not construction): log it and
	 * fire the events.
	 */
	void ApplyState(bool bNotify);

	UPROPERTY(Transient)
	TMap<TObjectPtr<UDTHWardrobeItem>, TObjectPtr<USceneComponent>> ItemComponents;

	FString AppliedState;
};
