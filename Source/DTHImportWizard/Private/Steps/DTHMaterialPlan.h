#pragma once

#include "CoreMinimal.h"

class FDTHExport;
struct FDTHMeshEntry;

/** How Unreal should treat a texture file. */
enum class EDTHTextureKind : uint8
{
	Color,     // sRGB, default compression
	Normal,    // normal map, optionally green-flipped
	Grayscale, // linear, grayscale compression (roughness, specular, metallic, opacity...)
	Linear,    // linear colour, default compression
};

/** One texture parameter of a material instance and the file that fills it. */
struct FDTHTextureRow
{
	/** Texture parameter on the parent material ("Base Colour Texture", "Normal Texture"...). */
	FString Parameter;
	/** File to import, empty when none could be found (see Why). */
	FString Source;
	/** Texture asset name under the Textures folder. */
	FString AssetName;
	EDTHTextureKind Kind = EDTHTextureKind::Color;
	bool bFlipGreen = false;
	/** Why this file was chosen, or what's wrong (shown in the wizard and the report). */
	FString Why;
	/** The manifest listed this channel but no usable file was found. */
	bool bProblem = false;
};

/** The material one slot gets. */
struct FDTHMaterialPlan
{
	/** Material slot name, also the material instance name (MI_<Character><Slot>). */
	FString Slot;
	/** The manifest's material type ("Skin", "Cloth"...). */
	FString Type;
	/** DTH parent family ("Skin", "Cloth", "Metal", "Eye"...), "Skip" for none. */
	FString Parent;
	TArray<FDTHTextureRow> Textures;
};

namespace DTHImport
{
	/**
	 * The default material plan for every manifest slot of Meshes (each slot once, manifest order). Ported from the
	 * old Python wizard's material planner:
	 * - parent from the manifest type, except clothing DTH types as Skin, which becomes Cloth unless the slot is a
	 *   body part;
	 * - each manifest texture under the parent's own parameter name (DTH's Cloth master has "Normal Texture" where
	 *   Skin has "Base Normal Texture"), metallic included;
	 * - maps the manifest doesn't list (SSS, specular, AO, height) found next to the listed files by name;
	 * - texture kind per role, green flip for OpenGL normal maps only;
	 * - one texture asset per distinct file, named T_<Character>_<Slot>_<Role> (after the file when 3+ slots share it).
	 * Files are taken from the manifest's absolute paths, else from the export's Textures folder.
	 */
	TArray<FDTHMaterialPlan> BuildMaterialPlan(const FDTHExport& Export, const TArray<const FDTHMeshEntry*>& Meshes);

	/** DTH material instance a parent family copies, relative to the DazToHue content folder ("" for Skip). */
	FString ParentMaterialPath(const FString& Parent);
}
