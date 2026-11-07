#pragma once

#include "CoreMinimal.h"

struct FDTHImportReport;
struct FDTHImportRequest;

/** An asset at a path the import writes, of a kind the import can't use there. */
struct FDTHPathClash
{
	/** The asset's object path. */
	FString Path;
	/** Its class, as the asset registry has it. */
	FString Found;
	/** What the import puts there. */
	FString Expected;
	/** In <Destination>/Wardrobe, which belongs to the import's wardrobe: an older wardrobe (e.g. the Python wizard's). */
	bool bOldWardrobe = false;
};

namespace DTHImport
{
	/**
	 * Every asset in the way of Request, found in the asset registry without loading anything: the meshes, the
	 * post-process blueprint, pose assets and control rig (base imports), the grooms and BP_<Character> where they
	 * hold something else, and anything in <Destination>/Wardrobe that isn't a DTH wardrobe asset. Unreal can't
	 * replace an asset with one of another class, so the import checks these before it starts.
	 */
	TArray<FDTHPathClash> FindPathClashes(const FDTHImportRequest& Request);

	/**
	 * Deletes the old-wardrobe assets among Clashes (bOldWardrobe), and reports each, and what referenced them
	 * outside the wardrobe folder (those references are cleared). False if any couldn't be deleted.
	 */
	bool DeleteOldWardrobe(const TArray<FDTHPathClash>& Clashes, FDTHImportReport& Report);
}
