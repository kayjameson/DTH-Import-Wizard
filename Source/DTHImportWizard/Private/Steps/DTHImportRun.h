#pragma once

#include "CoreMinimal.h"
#include "Steps/DTHImportReport.h"

class FDTHExport;

/** What to import, as chosen in the wizard. */
struct FDTHImportRequest
{
	TSharedPtr<FDTHExport> Export;
	FString DestinationFolder;
	/** Indexes into Export->Meshes. */
	TArray<int32> MeshIndices;
	/** Re-import meshes that already exist in the destination; otherwise they're reused as they are. */
	bool bReimportExistingMeshes = false;
	/** Import onto this skeleton instead of the one the export targets (empty: the export's). */
	FString SkeletonOverride;
};

struct FDTHMergeCheck;

namespace DTHImport
{
	/** Runs every step in order and saves what it created or changed. */
	FDTHImportReport RunImport(const FDTHImportRequest& Request);

	/** The material merge check's findings as report lines (warnings for problems, one summary line). */
	void ReportMaterialMerges(const FDTHMergeCheck& Check, FDTHImportReport& Report);
}
