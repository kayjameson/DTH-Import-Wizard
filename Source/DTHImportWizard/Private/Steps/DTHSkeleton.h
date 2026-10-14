#pragma once

#include "CoreMinimal.h"

class FDTHExport;
class USkeleton;

namespace DTHImport
{
	/** Asset path of the skeleton an export imports onto: the UE5 Mannequin or the DTH native Genesis 9 skeleton. */
	FString SkeletonPathFor(const FDTHExport& Export, FString& OutError);

	USkeleton* LoadSkeletonFor(const FDTHExport& Export, FString& OutError);
}
