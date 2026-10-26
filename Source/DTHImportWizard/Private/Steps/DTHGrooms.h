#pragma once

#include "CoreMinimal.h"

class FDTHExport;
class USkeletalMesh;
struct FDTHImportReport;

/** One groom as the wizard's Grooms page sets it up. */
struct FDTHGroomPlan
{
	/** The Groom Name typed on the export node ("BaseHair"). */
	FString Name;
	FString File;
	bool bImport = true;
	/** The binding's Num Interpolation Points (Groom Guide: 100 for head hair, around 25 for brows and lashes). */
	int32 InterpolationPoints = 100;
	/** The body material slot whose element number is the binding's Matching Section (the head's, in the guide). */
	FString MatchingSlot;
};

namespace DTHImport
{
	/** A plan per groom in the export: imported, bound to the head slot, 25 points for brows and lashes, 100 otherwise. */
	TArray<FDTHGroomPlan> BuildGroomPlans(const FDTHExport& Export);

	/**
	 * The Groom Guide's "Unreal Engine - Basic setup" for each planned groom: imported at default options into
	 * DestinationFolder, a binding on Body, and a per-groom copy of MI_DTH_Hair (Materials/MI_<Character><Groom>) in the
	 * groom's material slot. Grooms that already exist are reused unless bReimportExisting. The Groom component on the
	 * character blueprint is left to the user (a manual step).
	 */
	void BuildGrooms(const FDTHExport& Export, const TArray<FDTHGroomPlan>& Plans, USkeletalMesh* Body,
		const FString& DestinationFolder, bool bReimportExisting, FDTHImportReport& Report);
}
