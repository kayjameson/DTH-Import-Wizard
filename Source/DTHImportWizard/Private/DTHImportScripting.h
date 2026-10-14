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
	 * Runs the whole import as the wizard's Import button does: the export's manifest meshes (plus unlisted FBX
	 * files if bIncludeUnlisted), post-process blueprint, deformer, curves, retargeting and pose assets, into
	 * DestinationFolder (e.g. /Game/Characters/Vi). Existing meshes are reused unless bReimportExistingMeshes.
	 * SkeletonOverride imports onto another skeleton (empty: the one the export targets).
	 * Returns the run's messages; errors start with "ERROR:", warnings with "WARNING:".
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Import Wizard")
	static TArray<FString> ImportCharacter(const FString& ExportFolder, const FString& DestinationFolder, bool bIncludeUnlisted = false,
		bool bReimportExistingMeshes = false, const FString& SkeletonOverride = TEXT(""));

	/**
	 * What the import set up on a skeletal mesh: post-process blueprint, deformer, registered curves (with their
	 * morph/material flags), its skeleton's translation retargeting for the DTH bones, and pose assets in PoseFolder.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Import Wizard")
	static TArray<FString> DescribeCharacter(const FString& SkeletalMeshPath, const FString& PoseFolder = TEXT(""));
};
