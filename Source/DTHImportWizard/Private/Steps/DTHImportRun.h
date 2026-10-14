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

namespace DTHImport
{
	/** Runs every step in order and saves what it created or changed. */
	FDTHImportReport RunImport(const FDTHImportRequest& Request);
}
