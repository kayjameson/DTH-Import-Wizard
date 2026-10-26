#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "DTHCharacter.generated.h"

class UDTHOutfit;
class UDTHWardrobe;
class UDTHWardrobeItem;
class USkeletalMeshComponent;

/**
 * A DazToHue character with a wardrobe: the body, and every garment and groom the wardrobe knows, created once
 * (garments follow the body as leader pose, grooms are bound to it) and shown or hidden by WardrobeState.
 *
 * WardrobeState is a keyable string (Sequencer: a String track on Wardrobe State):
 *   Witch                 the outfit named Witch, plus the wardrobe's base items (brows, lashes)
 *   Witch -Hat -Heels     without those items
 *   Witch +Ponytail       with an item from anywhere in the wardrobe; it replaces the worn items of its slot
 *   Witch -slot:Feet      without every item of a slot
 *   -Hat                  keeps the current outfit, changes the overrides
 * Names are case-insensitive, separated by spaces or commas. Empty: the wardrobe's first outfit.
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
	TObjectPtr<UDTHWardrobe> Wardrobe;

	/** What is worn: an outfit name and overrides (see the class comment). Keyable in Sequencer. */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetWardrobeState, Category = "Wardrobe")
	FString WardrobeState;

	/**
	 * Applies a wardrobe state. Sequencer calls this for a keyed Wardrobe State, also while scrubbing in the editor
	 * (CallInEditor lets it run there).
	 */
	UFUNCTION(BlueprintSetter, Category = "Wardrobe", meta = (CallInEditor = "true"))
	void SetWardrobeState(const FString& NewState);

	/** The items worn now. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	TArray<UDTHWardrobeItem*> GetWornItems() const;

	/** The component that shows Item (a skeletal mesh or groom component), or null. */
	UFUNCTION(BlueprintCallable, Category = "Wardrobe")
	USceneComponent* GetItemComponent(const UDTHWardrobeItem* Item) const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	/** A component per wardrobe item: existing ones (from construction, saved with the level) are found, missing ones made. */
	void EnsureComponents(EComponentCreationMethod CreationMethod);
	USceneComponent* MakeComponent(UDTHWardrobeItem* Item, EComponentCreationMethod CreationMethod);
	/** Worn items for WardrobeState, then visibility to match. bLog: say so in the log (explicit sets, not construction). */
	void ApplyState(bool bLog);

	UPROPERTY(Transient)
	TMap<TObjectPtr<UDTHWardrobeItem>, TObjectPtr<USceneComponent>> ItemComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDTHWardrobeItem>> Worn;

	/** The outfit a state without an outfit name keeps. */
	UPROPERTY(Transient)
	TObjectPtr<UDTHOutfit> CurrentOutfit;

	FString AppliedState;
};
