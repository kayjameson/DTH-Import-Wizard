#pragma once

#include "CoreMinimal.h"
#include "Steps/DTHGrooms.h"

class FDTHExport;
class UDTHWardrobe;
class USkeletalMesh;
struct FDTHImportReport;

namespace DTHImport
{
	/** Wardrobe slot from a garment's name ("SKM_Milly_Witch_Heels" -> Feet); Accessory when nothing matches. */
	FName GarmentSlot(const FString& Name);

	/** Wardrobe slot from a groom's name: Eyebrows, Eyelashes, Vellus, Beard, else Hair. */
	FName GroomSlot(const FString& Name);

	/** Eyebrows, eyelashes and vellus are part of the face: worn with every outfit. */
	bool IsBaseSlot(FName Slot);

	/**
	 * An outfit name for the export's garments: the first name token they share after SKM_<Character>_
	 * ("Witch_Dress", "Witch_Hat" -> "Witch"), else the character's name.
	 */
	FString DefaultOutfitName(const FDTHExport& Export, const TArray<int32>& MeshIndices);

	/** The character's wardrobe in <Folder>/Wardrobe, if the folder has one. */
	UDTHWardrobe* FindWardrobe(const FString& Folder);

	/**
	 * <Folder>/Wardrobe: an item per garment and groom (existing items for the same mesh or groom are reused), the
	 * outfit OutfitName with this export's garments and head hair, and DA_Wardrobe_<Character> on Body. Brows, lashes
	 * and vellus become base items. A base import also makes the Nude outfit (its hair only) and BP_<Character>
	 * (a DTH Character on the wardrobe) in Folder. An export with grooms but no garments adds its grooms to the
	 * wardrobe's extra items instead of making an outfit. Existing assets are kept and only added to.
	 */
	void BuildWardrobe(const FDTHExport& Export, const FString& Folder, USkeletalMesh* Body, const TArray<USkeletalMesh*>& Garments,
		const TArray<FDTHBuiltGroom>& Grooms, const FString& OutfitName, bool bBaseImport, FDTHImportReport& Report);
}
