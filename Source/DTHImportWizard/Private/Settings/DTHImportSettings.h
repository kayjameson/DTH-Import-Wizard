#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "DTHImportSettings.generated.h"

class USkeleton;

/**
 * Where things live in this project. Shown under Project Settings > Plugins > DTH Import Wizard
 * and on the wizard's Project Settings page. Stored per user, per project.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "DTH Import Wizard"))
class UDTHImportSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDTHImportSettings();

	/** The DazToHue Unreal content pack, as added to this project. */
	UPROPERTY(config, EditAnywhere, Category = "Project", meta = (ContentDir, DisplayName = "DazToHue Content Folder"))
	FDirectoryPath DazToHueContent;

	/** New characters are imported into a folder named after the character, inside this one. */
	UPROPERTY(config, EditAnywhere, Category = "Project", meta = (ContentDir, DisplayName = "Character Folder"))
	FDirectoryPath CharacterRoot;

	/** The skeleton used for exports that target the UE5 Mannequin. */
	UPROPERTY(config, EditAnywhere, Category = "Project", meta = (DisplayName = "UE5 Mannequin Skeleton"))
	TSoftObjectPtr<USkeleton> MannequinSkeleton;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** "Native/Genesis_9/SKL_DTH_Genesis9_Architype" -> "/Game/DazToHue/Native/Genesis_9/SKL_DTH_Genesis9_Architype". */
	FString DazToHueAsset(const FString& RelativePath) const;
};
