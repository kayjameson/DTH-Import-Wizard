#pragma once

#include "CoreMinimal.h"

class FDTHExport;
class UAnimBlueprint;
class USkeletalMesh;
struct FDTHImportReport;

namespace DTHImport
{
	/**
	 * Adds morph-driven jiggle physics to the character's post-process blueprint copy, as the DTH 1.2 jiggle video
	 * sets it up by hand (Part 5): Local To Component, one Spring Controller per physics bone, Component To Local, then
	 * the exported Pose Drivers, on the PHY pose asset BuildPoseAssets made. Pose driver targets the export leaves at
	 * zero are set here ([29:53]: the offsets can be set in Unreal). Failures become a manual step. A blueprint
	 * that already has the springs is left as it is.
	 */
	bool AddJigglePhysics(const FDTHExport& Export, const USkeletalMesh* Body, const FString& DestinationFolder, UAnimBlueprint* PostProcess, FDTHImportReport& Report);

	/**
	 * Where a PHY pose's bone should be: RestTranslation plus the pose's offset on the driver's circle, converted into
	 * the local space of the physics bone's parent. False if PoseName isn't one of the Genesis 9 preset's poses.
	 */
	bool JigglePoseTarget(const USkeletalMesh* Body, FName Bone, const FString& PoseName, const FVector& RestTranslation, FVector& OutTarget);
}
