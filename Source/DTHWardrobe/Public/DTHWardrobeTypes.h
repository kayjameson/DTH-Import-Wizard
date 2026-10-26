#pragma once

#include "Engine/DataAsset.h"
#include "DTHWardrobeTypes.generated.h"

class UGroomAsset;
class UGroomBindingAsset;
class USkeletalMesh;

/**
 * One thing a character can wear: a garment (skeletal mesh that follows the body) or a groom (bound to the body).
 * Its ItemName is what wardrobe states use ("Witch -Hat").
 */
UCLASS(BlueprintType)
class DTHWARDROBE_API UDTHWardrobeItem : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The garment. Imported on the character's skeleton; it follows the body's pose and corrective curves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<USkeletalMesh> Mesh;

	/** The groom, for hair, brows, lashes and other strands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UGroomAsset> Groom;

	/** The groom's binding to the character's body mesh, built once at import. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UGroomBindingAsset> GroomBinding;

	/**
	 * Where it's worn: Hair, Eyebrows, Eyelashes, Head, Torso, Legs, Feet, Arms, Neck, Underwear, Accessory... An item
	 * added with "+Name" replaces the worn items of its slot; "-slot:Feet" takes off every item of a slot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName Slot;

	/** The name wardrobe states use ("Hat" in "Witch -Hat"); empty: the asset name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FString ItemName;

	/** The DazToHue export it came from (informational). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	FString SourceExport;

	FString WardrobeName() const { return ItemName.IsEmpty() ? GetName() : ItemName; }
	/** True for Other equal to the item's name or asset name, ignoring case. */
	bool IsNamed(const FString& Other) const;
};

/** A set of items worn together. Its OutfitName is the first word of a wardrobe state. */
UCLASS(BlueprintType)
class DTHWARDROBE_API UDTHOutfit : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The name wardrobe states use ("Witch"); empty: the asset name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outfit")
	FString OutfitName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outfit")
	TArray<TObjectPtr<UDTHWardrobeItem>> Items;

	FString WardrobeName() const { return OutfitName.IsEmpty() ? GetName() : OutfitName; }
	bool IsNamed(const FString& Other) const;
};

/** Everything one character can wear, on one body. */
UCLASS(BlueprintType)
class DTHWARDROBE_API UDTHWardrobe : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The character's body. Every garment is imported on its skeleton and every groom is bound to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
	TObjectPtr<USkeletalMesh> Body;

	/** Worn with every outfit (brows, lashes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
	TArray<TObjectPtr<UDTHWardrobeItem>> BaseItems;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
	TArray<TObjectPtr<UDTHOutfit>> Outfits;

	/** Items that belong to no outfit (a hair-only export), worn with "+Name". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
	TArray<TObjectPtr<UDTHWardrobeItem>> ExtraItems;

	/** Every item the wardrobe knows, each once: base items, then outfit items, then extra items. */
	TArray<UDTHWardrobeItem*> AllItems() const;
	UDTHOutfit* FindOutfit(const FString& Name) const;
	UDTHWardrobeItem* FindItem(const FString& Name) const;
};
