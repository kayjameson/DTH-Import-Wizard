#include "Steps/DTHSkeleton.h"

#include "Animation/Skeleton.h"
#include "Export/DTHExport.h"
#include "Settings/DTHImportSettings.h"

FString DTHImport::SkeletonPathFor(const FDTHExport& Export, FString& OutError)
{
	if (!Export.IsGenesis9())
	{
		OutError = FString::Printf(TEXT("%s is a %s export; only Genesis 9 is supported so far."), *Export.Character, *Export.SourceSkeleton);
		return FString();
	}
	const UDTHImportSettings* Settings = GetDefault<UDTHImportSettings>();
	if (Export.TargetsMannequin())
	{
		const FString Path = Settings->MannequinSkeleton.ToSoftObjectPath().GetLongPackageName();
		if (Path.IsEmpty())
		{
			OutError = TEXT("The export targets the UE5 Mannequin, but no UE5 Mannequin Skeleton is set in the project settings.");
		}
		return Path;
	}
	return Settings->DazToHueAsset(TEXT("Native/Genesis_9/SKL_DTH_Genesis9_Architype"));
}

USkeleton* DTHImport::LoadSkeletonFor(const FDTHExport& Export, FString& OutError)
{
	const FString Path = SkeletonPathFor(Export, OutError);
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *Path, nullptr, LOAD_NoWarn);
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("Skeleton not found: %s"), *Path);
	}
	return Skeleton;
}
