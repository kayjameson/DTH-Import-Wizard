#pragma once

#include "CoreMinimal.h"
#include "Steps/DTHMaterialPlan.h"

class FDTHExport;
class UMaterialInstanceConstant;
class USkeletalMesh;
struct FDTHImportReport;

namespace DTHImport
{
	/**
	 * Materials/<slot> for every planned slot, built before the meshes are imported so their slots pick them up by
	 * name. Each slot's instance sits under a per-character copy of its family's DTH master (MI_<Character><Family>Master,
	 * as the DTH importer copies them); Eye, EyeOcclusion and Tear slots ARE the copy, named after the slot. Textures
	 * are imported into Textures/ under the plan's asset names with the plan's kind and green flip, set on the plan's
	 * parameters with their "Use ..." switches. Existing instances are updated, existing textures reused.
	 * Returns slot name -> material instance.
	 */
	TMap<FString, UMaterialInstanceConstant*> BuildMaterials(const FDTHExport& Export, const TArray<FDTHMaterialPlan>& Plans,
		const FString& DestinationFolder, FDTHImportReport& Report);

	/** Puts each built material on every mesh slot of the same name (also on meshes that were reused, not imported). */
	void AssignMaterials(const TArray<USkeletalMesh*>& Meshes, const TMap<FString, UMaterialInstanceConstant*>& Materials, FDTHImportReport& Report);
}
