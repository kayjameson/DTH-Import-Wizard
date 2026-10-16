#pragma once

#include "CoreMinimal.h"

class FDTHExport;
struct FDTHMeshEntry;

/** One exported material slot that merges Daz surfaces whose textures differ. */
struct FDTHMergeProblem
{
	FString Mesh;
	FString Slot;
	/** One line per distinct texture set: the surfaces that use it and its files. */
	TArray<FString> Groups;

	FString Describe() const;
};

/** What the merge check found. */
struct FDTHMergeCheck
{
	/** Daz's own export the slots were compared against; empty if none was found. */
	FString DazFbx;
	/** Why the check could not run (no Daz export, unreadable file...); empty when it ran. */
	FString Skipped;
	/** Slots checked, and slots left out because their textures were baked. */
	int32 SlotsChecked = 0;
	int32 SlotsBaked = 0;
	/** Meshes whose geometry could not be matched to Daz's export (not checked). */
	TArray<FString> Unmatched;
	TArray<FDTHMergeProblem> Problems;
};

namespace DTHImport
{
	/**
	 * Daz's own FBX export for this character: <project>/houdini/daz-export/<Character>/<Character>.fbx, found by
	 * walking up from the export folder (DTH Character Studio's project layout). Empty if there is none.
	 */
	FString FindDazExportFbx(const FDTHExport& Export);

	/**
	 * Without baking, one material slot can only show one texture set, so Houdini may merge Daz surfaces into a slot
	 * only when they share their textures (Quick Start guide, merge criteria 2 and 3; the Texture Baking guide lifts
	 * them for baked slots). Each exported slot is traced back to the Daz surfaces it was made from by matching its
	 * rest-pose polygons to Daz's export, and every slot whose surfaces use different textures is reported. Slots whose
	 * manifest textures are none of their surfaces' files were baked and are skipped. DazFbx empty: FindDazExportFbx.
	 */
	FDTHMergeCheck CheckMaterialMerges(const FDTHExport& Export, const TArray<const FDTHMeshEntry*>& Meshes, const FString& DazFbx = FString());
}
