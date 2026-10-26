#include "DTHWardrobeTypes.h"

bool UDTHWardrobeItem::IsNamed(const FString& Other) const
{
	return Other.Equals(ItemName, ESearchCase::IgnoreCase) || Other.Equals(GetName(), ESearchCase::IgnoreCase);
}

bool UDTHOutfit::IsNamed(const FString& Other) const
{
	return Other.Equals(OutfitName, ESearchCase::IgnoreCase) || Other.Equals(GetName(), ESearchCase::IgnoreCase);
}

TArray<UDTHWardrobeItem*> UDTHWardrobe::AllItems() const
{
	TArray<UDTHWardrobeItem*> Items;
	for (UDTHWardrobeItem* Item : BaseItems)
	{
		if (Item) Items.AddUnique(Item);
	}
	for (const UDTHOutfit* Outfit : Outfits)
	{
		if (!Outfit) continue;
		for (UDTHWardrobeItem* Item : Outfit->Items)
		{
			if (Item) Items.AddUnique(Item);
		}
	}
	for (UDTHWardrobeItem* Item : ExtraItems)
	{
		if (Item) Items.AddUnique(Item);
	}
	return Items;
}

UDTHOutfit* UDTHWardrobe::FindOutfit(const FString& Name) const
{
	for (UDTHOutfit* Outfit : Outfits)
	{
		if (Outfit && Outfit->IsNamed(Name)) return Outfit;
	}
	return nullptr;
}

UDTHWardrobeItem* UDTHWardrobe::FindItem(const FString& Name) const
{
	for (UDTHWardrobeItem* Item : AllItems())
	{
		if (Item->IsNamed(Name)) return Item;
	}
	return nullptr;
}
