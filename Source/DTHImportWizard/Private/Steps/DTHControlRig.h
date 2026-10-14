#pragma once

#include "CoreMinimal.h"

class FDTHExport;
class UControlRigBlueprint;
class USkeletalMesh;
struct FDTHImportReport;

namespace DTHImport
{
	/**
	 * The character's modular control rig: the body mesh's own bones, DTH's Body and Face modules (plus the
	 * Golden Palace / Dicktator modules when the mesh has their bones), their connectors, DTH's control shapes,
	 * and the rig set as the mesh's default animating rig. The bone hierarchy is checked against the mesh's rest
	 * pose afterwards, because a rig whose bones differ from the mesh deforms it.
	 * An existing rig with the same name is updated in place.
	 */
	UControlRigBlueprint* BuildControlRig(const FDTHExport& Export, USkeletalMesh* BodyMesh, const FString& DestinationFolder, FDTHImportReport& Report);
}
