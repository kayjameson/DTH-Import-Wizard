#pragma once

#include "CoreMinimal.h"

class FDTHExport;
class UAnimBlueprint;
class USkeletalMesh;
class USkeleton;
struct FDTHImportReport;

/**
 * The steps that turn imported meshes into a working DazToHue character. Each follows the DTH developer's
 * importer, or the Quick Start guide where the importer doesn't cover it.
 */
namespace DTHImport
{
	/**
	 * A per-character copy of DTH's post-process animation blueprint for this figure, skeleton and skinning
	 * method, pointed at Skeleton. The copy is never edited beyond that. An existing copy is reused.
	 */
	UAnimBlueprint* PreparePostProcessBlueprint(const FDTHExport& Export, USkeleton* Skeleton, const FString& DestinationFolder, FDTHImportReport& Report);

	/** Post-process blueprint, plus the DQS deformer graph (DQS) or recomputed head/face tangents (linear). */
	void SetUpMesh(const FDTHExport& Export, USkeletalMesh* Mesh, UAnimBlueprint* PostProcess, FDTHImportReport& Report);

	/** Every curve in the export's curve files, registered on the mesh as a morph curve. */
	void RegisterCurves(const FDTHExport& Export, USkeletalMesh* Mesh, FDTHImportReport& Report);

	/** The translation retargeting DTH sets on the UE5 Mannequin skeleton. Only applies to Mannequin exports. */
	void ApplyRetargeting(const FDTHExport& Export, USkeleton* Skeleton, FDTHImportReport& Report);

	/**
	 * PoseAssets/PA_<Character><Type>: one pose asset per exported category, additive on its first (rest) pose,
	 * as the DTH importer builds them. Add-on categories (not FAC/JCM/RET) on the UE5 Mannequin or a DTH archetype
	 * skeleton are added to the character's post-process blueprint copy just before Output Pose, as the GP9 guide
	 * does by hand (steps 123-125); if that fails they become a manual step.
	 */
	void BuildPoseAssets(const FDTHExport& Export, USkeleton* Skeleton, const FString& DestinationFolder, UAnimBlueprint* PostProcess, FDTHImportReport& Report);

	/** The curve names in an AnimCurveViewer paste file. */
	TArray<FName> ReadCurveNames(const FString& File);
}
