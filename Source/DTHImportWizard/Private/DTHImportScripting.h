#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "DTHImportScripting.generated.h"

/**
 * The wizard's steps for editor scripting (Python: unreal.DTHImportScripting) and automation.
 * Each call does exactly what the wizard's button does for that step.
 */
UCLASS()
class UDTHImportScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** One line per fact read from the export's manifest, or a line starting with "ERROR:". */
	UFUNCTION(BlueprintCallable, Category = "DTH Import Wizard")
	static TArray<FString> DescribeExport(const FString& ExportFolder);

	/**
	 * Imports the export's skeletal meshes into DestinationFolder (e.g. /Game/Characters/Vi).
	 * Unlisted FBX files are skipped unless bIncludeUnlisted. Returns the imported asset paths;
	 * failures are logged and reported as "ERROR: ..." entries.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Import Wizard")
	static TArray<FString> ImportMeshes(const FString& ExportFolder, const FString& DestinationFolder, bool bIncludeUnlisted = false);
};
