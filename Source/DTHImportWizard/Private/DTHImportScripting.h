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
	 * Opens the wizard tab, loads ExportFolder (if given) as Browse does, and shows Page (Settings, Meshes, Materials,
	 * Grooms or Import; empty: as loading left it). Returns "Wizard open." or a line starting with "ERROR:".
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Import Wizard")
	static FString OpenWizard(const FString& ExportFolder = TEXT(""), const FString& Page = TEXT(""));

	/**
	 * The material merge check on its own: every manifest slot traced back to the Daz surfaces it was made from, and
	 * each slot that merges surfaces with different textures (unbaked) reported. DazExportFbx empty: found from the
	 * Character Studio project layout. Problems start with "WARNING:".
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Import Wizard")
	static TArray<FString> CheckMaterialMerges(const FString& ExportFolder, const FString& DazExportFbx = TEXT(""));

	/**
	 * Runs the whole import as the wizard's Import button does: the export's manifest meshes (plus unlisted FBX
	 * files if bIncludeUnlisted), post-process blueprint, deformer, curves, retargeting, pose assets and grooms, into
	 * DestinationFolder (e.g. /Game/Characters/Vi). Existing meshes are reused unless bReimportExistingMeshes.
	 * SkeletonOverride imports onto another skeleton (empty: the one the export targets). bAddToCharacter adds the
	 * export's garments and grooms to the character already in DestinationFolder as outfit OutfitName (empty: from the
	 * garment names), after checking its body is the character's. Everything goes into the character's wardrobe and
	 * BP_<Character>. Before anything is made, assets of another kind at the paths it writes stop the import;
	 * bDeleteOldWardrobe deletes those in DestinationFolder/Wardrobe (an older wardrobe) instead.
	 * bJigglePhysics adds morph-driven jiggle physics to the post-process blueprint, for exports that have it.
	 * Returns the run's messages; errors start with "ERROR:", warnings with "WARNING:".
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Import Wizard")
	static TArray<FString> ImportCharacter(const FString& ExportFolder, const FString& DestinationFolder, bool bIncludeUnlisted = false,
		bool bReimportExistingMeshes = false, const FString& SkeletonOverride = TEXT(""), bool bAddToCharacter = false,
		const FString& OutfitName = TEXT(""), bool bDeleteOldWardrobe = false, bool bJigglePhysics = false);

	/**
	 * What the import set up on a skeletal mesh: post-process blueprint, deformer, registered curves (with their
	 * morph/material flags), its skeleton's translation retargeting for the DTH bones, and pose assets in PoseFolder.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Import Wizard")
	static TArray<FString> DescribeCharacter(const FString& SkeletalMeshPath, const FString& PoseFolder = TEXT(""));
};
