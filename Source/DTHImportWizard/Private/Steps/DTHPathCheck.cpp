#include "Steps/DTHPathCheck.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ControlRigBlueprintLegacy.h"
#include "DTHCharacter.h"
#include "DTHWardrobeTypes.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "GroomAsset.h"
#include "ObjectTools.h"
#include "Steps/DTHGrooms.h"
#include "Steps/DTHImportReport.h"
#include "Steps/DTHImportRun.h"

#define LOCTEXT_NAMESPACE "DTHPathCheck"

namespace
{
	/** The same character token the steps name their assets with. */
	FString AssetSafe(const FString& Name)
	{
		FString Out;
		for (const TCHAR Char : Name)
		{
			Out.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
		}
		return Out;
	}

	IAssetRegistry& Registry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	}

	bool IsA(const FAssetData& Asset, const UClass* Expected)
	{
		// Classes of unloaded Blueprint assets don't resolve; those are never one of the import's native classes.
		const UClass* Class = Asset.GetClass();
		return Class && Class->IsChildOf(Expected);
	}

	/** A Blueprint whose native parent is the DTH Character (BP_<Character>, or one made from it). */
	bool IsCharacterBlueprint(const FAssetData& Asset)
	{
		FString Parent;
		return IsA(Asset, UBlueprint::StaticClass()) && Asset.GetTagValue(FBlueprintTags::NativeParentClassPath, Parent)
			&& Parent.Contains(ADTHCharacter::StaticClass()->GetPathName());
	}

	void Check(const FString& PackageName, const UClass* Expected, TArray<FDTHPathClash>& Clashes)
	{
		TArray<FAssetData> Assets;
		Registry().GetAssetsByPackageName(FName(*PackageName), Assets);
		for (const FAssetData& Asset : Assets)
		{
			const bool bFits = Expected == UBlueprint::StaticClass() ? IsCharacterBlueprint(Asset) : IsA(Asset, Expected);
			if (!bFits)
			{
				Clashes.Add({ Asset.GetObjectPathString(), Asset.AssetClassPath.ToString(),
					Expected == UBlueprint::StaticClass() ? ADTHCharacter::StaticClass()->GetPathName() + TEXT(" Blueprint") : Expected->GetPathName(), false });
			}
		}
	}
}

TArray<FDTHPathClash> DTHImport::FindPathClashes(const FDTHImportRequest& Request)
{
	TArray<FDTHPathClash> Clashes;
	if (!Request.Export) return Clashes;
	const FDTHExport& Export = *Request.Export;
	FString Root = Request.DestinationFolder;
	Root.RemoveFromEnd(TEXT("/"));
	const FString Character = AssetSafe(Export.Character);
	const bool bBase = !Request.bAddToCharacter;

	for (const int32 Index : Request.MeshIndices)
	{
		const FDTHMeshEntry& Mesh = Export.Meshes[Index];
		if (bBase || !Mesh.IsBody())
		{
			Check(Root / FPaths::GetBaseFilename(Mesh.File), USkeletalMesh::StaticClass(), Clashes);
		}
	}
	Check(Root / TEXT("Blueprints") / TEXT("ABP_") + Character, UAnimBlueprint::StaticClass(), Clashes);
	if (bBase)
	{
		for (const FDTHPoseAssetEntry& Entry : Export.PoseAssets)
		{
			Check(Root / TEXT("PoseAssets") / FPaths::GetBaseFilename(Entry.AnimationFile), UAnimSequence::StaticClass(), Clashes);
			Check(Root / TEXT("PoseAssets") / TEXT("PA_") + Character + AssetSafe(Entry.Category), UPoseAsset::StaticClass(), Clashes);
		}
		Check(Root / TEXT("MCR_") + Character, UControlRigBlueprint::StaticClass(), Clashes);
	}
	for (const FDTHGroomPlan& Groom : Request.GroomPlans.IsSet() ? Request.GroomPlans.GetValue() : BuildGroomPlans(Export))
	{
		if (Groom.bImport)
		{
			Check(Root / FPaths::GetBaseFilename(Groom.File), UGroomAsset::StaticClass(), Clashes);
		}
	}

	// The wardrobe folder is the import's: anything there but its wardrobe, items and outfits is an older wardrobe.
	const FString WardrobeFolder = Root / TEXT("Wardrobe");
	TArray<FAssetData> InWardrobe;
	Registry().GetAssetsByPath(FName(*WardrobeFolder), InWardrobe, true);
	FString WardrobeName = TEXT("DA_Wardrobe_") + Character;
	for (const FAssetData& Asset : InWardrobe)
	{
		if (IsA(Asset, UDTHWardrobe::StaticClass()) && Asset.PackagePath.ToString() == WardrobeFolder)
		{
			// The blueprint is named after the existing wardrobe (add-on exports may name the character differently).
			WardrobeName = Asset.AssetName.ToString();
		}
		else if (!IsA(Asset, UDTHWardrobe::StaticClass()) && !IsA(Asset, UDTHWardrobeItem::StaticClass()) && !IsA(Asset, UDTHOutfit::StaticClass()))
		{
			Clashes.Add({ Asset.GetObjectPathString(), Asset.AssetClassPath.ToString(), TEXT("DTH wardrobe assets only"), true });
		}
	}
	WardrobeName.RemoveFromStart(TEXT("DA_Wardrobe_"));
	Check(Root / TEXT("BP_") + WardrobeName, UBlueprint::StaticClass(), Clashes);
	return Clashes;
}

bool DTHImport::DeleteOldWardrobe(const TArray<FDTHPathClash>& Clashes, FDTHImportReport& Report)
{
	TArray<UObject*> Objects;
	TSet<FName> Packages;
	for (const FDTHPathClash& Clash : Clashes)
	{
		if (!Clash.bOldWardrobe) continue;
		if (UObject* Object = FSoftObjectPath(Clash.Path).TryLoad())
		{
			Objects.Add(Object);
			Packages.Add(Object->GetPackage()->GetFName());
		}
	}
	if (Objects.IsEmpty()) return true;

	// What uses the old wardrobe from outside it loses those references; say so before they're gone.
	TSet<FName> Referencers;
	for (const FName Package : Packages)
	{
		TArray<FName> Found;
		Registry().GetReferencers(Package, Found);
		for (const FName Referencer : Found)
		{
			if (!Packages.Contains(Referencer)) Referencers.Add(Referencer);
		}
	}

	const int32 Deleted = ObjectTools::ForceDeleteObjects(Objects, false);
	for (const FDTHPathClash& Clash : Clashes)
	{
		if (Clash.bOldWardrobe)
		{
			Report.Info(FText::Format(LOCTEXT("OldDeleted", "Deleted the old wardrobe's {0} ({1})."), FText::FromString(Clash.Path), FText::FromString(Clash.Found)));
		}
	}
	for (const FName Referencer : Referencers)
	{
		Report.Warning(FText::Format(LOCTEXT("OldReferencer", "{0} used the old wardrobe; those references are now empty."), FText::FromName(Referencer)));
	}
	if (Deleted < Objects.Num())
	{
		Report.Error(FText::Format(LOCTEXT("OldNotDeleted", "Only {0} of the old wardrobe's {1} assets could be deleted."), Deleted, Objects.Num()));
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
