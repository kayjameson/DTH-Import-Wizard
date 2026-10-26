#pragma once

#include "UObject/Object.h"
#include "DTHWardrobeTestListener.generated.h"

class UDTHWardrobeItem;

/** Records a DTH Character's wardrobe events for the automation tests (dynamic delegates bind to UFUNCTIONs). */
UCLASS(Transient)
class UDTHWardrobeTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void OnChanged(const FString& OldState, const FString& NewState, const TArray<UDTHWardrobeItem*>& WornItems)
	{
		Changes.Add(OldState + TEXT(" => ") + NewState);
		LastWornCount = WornItems.Num();
	}

	UFUNCTION()
	void OnWorn(UDTHWardrobeItem* Item) { Worn.Add(Item); }

	UFUNCTION()
	void OnRemoved(UDTHWardrobeItem* Item) { Removed.Add(Item); }

	void Reset()
	{
		Changes.Reset();
		Worn.Reset();
		Removed.Reset();
	}

	TArray<FString> Changes;
	TArray<UDTHWardrobeItem*> Worn;
	TArray<UDTHWardrobeItem*> Removed;
	int32 LastWornCount = 0;
};
