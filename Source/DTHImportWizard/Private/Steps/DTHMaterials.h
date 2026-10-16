#pragma once

#include "CoreMinimal.h"

class FDTHExport;
struct FDTHImportReport;
struct FDTHMeshEntry;

namespace DTHImport
{
	/**
	 * Materials/<slot> for every manifest material slot of Meshes, built before the meshes are imported so their
	 * slots pick them up by name, as the DTH importer does:
	 * - Skin, Cloth, Metal, Plastic and Glass slots: the slot's textures imported into Textures/, and a material
	 *   instance under a per-character copy of DTH's master instance for the type (MI_<Character><Type>Master),
	 *   with the textures set.
	 * - Eye, EyeOcclusion and Tear slots: a per-character copy of DTH's eye, eye occlusion or lacrimal fluid
	 *   instance (MI_<Character>Eyes, ...EyeOcclusion, ...Tear), which the slot of that name uses.
	 * A slot whose material already exists is left alone.
	 */
	void BuildMaterials(const FDTHExport& Export, const TArray<const FDTHMeshEntry*>& Meshes, const FString& DestinationFolder, FDTHImportReport& Report);
}
