#pragma once

#include "CoreMinimal.h"
#include "Steps/DTHGrooms.h"
#include "Steps/DTHImportReport.h"
#include "Steps/DTHMaterialPlan.h"

class FDTHExport;

/** What to import, as chosen in the wizard. */
struct FDTHImportRequest
{
	TSharedPtr<FDTHExport> Export;
	FString DestinationFolder;
	/** Indexes into Export->Meshes. */
	TArray<int32> MeshIndices;
	/** Re-import meshes and grooms that already exist in the destination; otherwise they're reused as they are. */
	bool bReimportExistingMeshes = false;
	/**
	 * Add this export's garments and grooms to the character already in DestinationFolder (another outfit or hair
	 * from its own Houdini file): its body is compared with the character's and never imported, and the body's steps
	 * are skipped. Otherwise a full (base) import.
	 */
	bool bAddToCharacter = false;
	/** The wardrobe outfit the export's garments become (empty: DefaultOutfitName). */
	FString OutfitName;
	/**
	 * Delete what else is in <DestinationFolder>/Wardrobe (an older wardrobe, e.g. the Python wizard's) before
	 * importing. Otherwise the import stops when it finds one.
	 */
	bool bDeleteOldWardrobe = false;
	/** Add morph-driven jiggle physics to the post-process blueprint copy. Needs FDTHExport::HasPhysics. */
	bool bJigglePhysics = false;
	/** Import onto this skeleton instead of the one the export targets (empty: the export's). */
	FString SkeletonOverride;
	/** The material plan as edited on the wizard's Materials page; unset: the default plan (BuildMaterialPlan). */
	TOptional<TArray<FDTHMaterialPlan>> MaterialPlans;
	/** The grooms as set up on the wizard's Grooms page; unset: the default plan (BuildGroomPlans). */
	TOptional<TArray<FDTHGroomPlan>> GroomPlans;
};

struct FDTHMergeCheck;

namespace DTHImport
{
	/** Runs every step in order and saves what it created or changed. */
	FDTHImportReport RunImport(const FDTHImportRequest& Request);

	/** The material merge check's findings as report lines (warnings for problems, one summary line). */
	void ReportMaterialMerges(const FDTHMergeCheck& Check, FDTHImportReport& Report);
}
