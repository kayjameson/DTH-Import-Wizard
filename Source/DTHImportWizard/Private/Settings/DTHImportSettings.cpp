#include "Settings/DTHImportSettings.h"

#include "Animation/Skeleton.h"

UDTHImportSettings::UDTHImportSettings()
{
	DazToHueContent.Path = TEXT("/Game/DazToHue");
	CharacterRoot.Path = TEXT("/Game/DTH_Characters");
	MannequinSkeleton = TSoftObjectPtr<USkeleton>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin")));
}

FString UDTHImportSettings::DazToHueAsset(const FString& RelativePath) const
{
	FString Root = DazToHueContent.Path;
	Root.RemoveFromEnd(TEXT("/"));
	return RelativePath.IsEmpty() ? Root : Root / RelativePath;
}
