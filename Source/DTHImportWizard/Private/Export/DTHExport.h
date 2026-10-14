#pragma once

#include "CoreMinimal.h"

/** One material slot as the manifest describes it. */
struct FDTHMaterialSlot
{
	FString Slot;
	FString Type;
	/** Manifest texture channel ("colour", "normal", ...) -> original file path. Empty channels are left out. */
	TMap<FString, FString> Textures;
};

/** A skeletal mesh FBX in the export. */
struct FDTHMeshEntry
{
	/** Name the manifest gives it ("Body", "FantasyArmorBoots"), empty when the file isn't in the manifest. */
	FString Name;
	FString File;
	bool bInManifest = false;
	TArray<FDTHMaterialSlot> Materials;

	bool IsBody() const { return Name.Equals(TEXT("Body"), ESearchCase::IgnoreCase); }
};

/** A pose asset animation exported by the pose asset node, with its pose name list. */
struct FDTHPoseAssetEntry
{
	FString Category;
	FString AnimationFile;
	FString NamesFile;
};

/** An AnimCurveViewer paste file from "Animation Curves". */
struct FDTHCurveFile
{
	FString File;
	FString Category;
};

/**
 * A DazToHue export folder, read from its DTH_<Character>.dth manifest.
 * Paths in the manifest are absolute and go stale when an export is moved, so every
 * file is resolved inside the export folder that was actually opened.
 */
class FDTHExport
{
public:
	/** Reads the manifest in Folder (or in its single character sub-folder). */
	static TSharedPtr<FDTHExport> Load(const FString& Folder, FString& OutError);

	FString Folder;
	FString ManifestFile;

	FString Character;
	FString DthVersion;
	FString SourceSkeleton;
	FString TargetSkeleton;
	FString SkinningMethod;

	TArray<FDTHMeshEntry> Meshes;
	TArray<FDTHPoseAssetEntry> PoseAssets;
	TArray<FDTHCurveFile> CurveFiles;

	bool TargetsMannequin() const;
	bool UsesDualQuaternionSkinning() const;
	bool IsGenesis9() const;

private:
	FString Resolve(const FString& ManifestPath, const TCHAR* SubFolder) const;
	bool Parse(const FString& Json, FString& OutError);
	void AddUnlistedMeshes();
};
