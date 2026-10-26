#include "Steps/DTHGrooms.h"

#include "AssetCompilingManager.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "DTHImportLog.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "HairStrandsCore.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"
#include "Settings/DTHImportSettings.h"
#include "Steps/DTHImportReport.h"
#include "Steps/DTHMaterialPlan.h"

#define LOCTEXT_NAMESPACE "DTHGrooms"

namespace
{
	/** Groom Guide, "Creating the groom binding": 100 for head hair, around 25 for smaller grooms such as brows and lashes. */
	int32 DefaultInterpolationPoints(const FString& GroomName)
	{
		return GroomName.Contains(TEXT("brow")) || GroomName.Contains(TEXT("lash")) ? 25 : 100;
	}

	/** Right-click > Import with nothing changed in the dialog ("Importing the groom"). */
	UGroomAsset* ImportGroom(const FDTHGroomPlan& Plan, const FString& Folder, IAssetTools& AssetTools)
	{
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = Plan.File;
		Task->DestinationPath = Folder;
		Task->bReplaceExisting = true;
		Task->bAutomated = true;
		Task->bSave = false;
		AssetTools.ImportAssetTasks({ Task });
		for (const FString& Path : Task->ImportedObjectPaths)
		{
			if (UGroomAsset* Groom = LoadObject<UGroomAsset>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Groom;
			}
		}
		return nullptr;
	}

	/** "Assigning a material to your groom": a copy of MI_DTH_Hair under the character's folder, in the groom's material slot. */
	UMaterialInstanceConstant* AssignHairMaterial(const FDTHExport& Export, const FDTHGroomPlan& Plan, UGroomAsset* Groom,
		const FString& Folder, IAssetTools& AssetTools, FDTHImportReport& Report)
	{
		const FString MaterialFolder = Folder / TEXT("Materials");
		const FString Name = FString::Printf(TEXT("MI_%s%s"), *Export.Character, *Plan.Name);
		UMaterialInstanceConstant* Material = LoadObject<UMaterialInstanceConstant>(nullptr, *(MaterialFolder / Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Material)
		{
			const FString Stock = GetDefault<UDTHImportSettings>()->DazToHueAsset(DTHImport::ParentMaterialPath(TEXT("Hair")));
			UMaterialInstanceConstant* Source = LoadObject<UMaterialInstanceConstant>(nullptr, *Stock, nullptr, LOAD_NoWarn);
			Material = Source ? Cast<UMaterialInstanceConstant>(AssetTools.DuplicateAsset(Name, MaterialFolder, Source)) : nullptr;
			if (!Material)
			{
				Report.Warning(FText::Format(LOCTEXT("HairCopyFailed", "{0}: could not copy {1}; the groom keeps the default (blonde) hair material."),
					FText::FromString(Groom->GetName()), FText::FromString(Stock)));
				return nullptr;
			}
		}

		// The groom editor's Materials tab names the slot after the material it's given. Groups without a slot of
		// their own draw with slot 0.
		TArray<FHairGroupsMaterial>& Slots = Groom->GetHairGroupsMaterials();
		if (Slots.Num() > 0 && Slots[0].Material == Material)
		{
			return Material;
		}
		Groom->Modify();
		if (Slots.Num() == 0)
		{
			Slots.AddDefaulted();
		}
		Slots[0].Material = Material;
		Slots[0].SlotName = Material->GetFName();
		FPropertyChangedEvent Changed(FindFProperty<FProperty>(UGroomAsset::StaticClass(), UGroomAsset::GetHairGroupsMaterialsMemberName()));
		Groom->PostEditChangeProperty(Changed);
		Groom->MarkPackageDirty();
		return Material;
	}

	/** "Creating the groom binding": target and source the body mesh, the head slot's element as Matching Section. */
	UGroomBindingAsset* BindGroom(const FDTHGroomPlan& Plan, UGroomAsset* Groom, USkeletalMesh* Body, const FString& Folder, FDTHImportReport& Report)
	{
		const int32 Section = Body->GetMaterials().IndexOfByPredicate([&Plan](const FSkeletalMaterial& Slot)
		{
			return Slot.MaterialSlotName.ToString().Equals(Plan.MatchingSlot, ESearchCase::IgnoreCase);
		});
		if (Section == INDEX_NONE)
		{
			Report.Warning(FText::Format(LOCTEXT("NoMatchingSlot", "{0}: {1} has no material slot \"{2}\" to use as the Matching Section; no binding was made."),
				FText::FromString(Groom->GetName()), FText::FromString(Body->GetName()), FText::FromString(Plan.MatchingSlot)));
			return nullptr;
		}

		// Named as the editor's Create Binding names it, so a re-run updates the same asset.
		const FString Path = Folder / FString::Printf(TEXT("%s_%s_%s_Binding"), *Groom->GetName(), *Body->GetName(), *Body->GetName());
		UGroomBindingAsset* Binding = LoadObject<UGroomBindingAsset>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Binding)
		{
			Binding->Modify();
			Binding->SetGroomBindingType(EGroomBindingMeshType::SkeletalMesh);
			Binding->SetGroom(Groom);
			Binding->SetSourceSkeletalMesh(Body);
			Binding->SetTargetSkeletalMesh(Body);
			Binding->SetNumInterpolationPoints(Plan.InterpolationPoints);
			Binding->SetMatchingSection(Section);
			Binding->MarkPackageDirty();
		}
		else
		{
			Binding = FHairStrandsCore::CreateGroomBindingAsset(EGroomBindingMeshType::SkeletalMesh, Path, nullptr, Groom, Body, Body,
				Plan.InterpolationPoints, Section);
		}
		if (!Binding)
		{
			Report.Warning(FText::Format(LOCTEXT("BindingFailed", "{0}: the binding could not be created."), FText::FromString(Groom->GetName())));
			return nullptr;
		}
		Binding->Build();
		FAssetCompilingManager::Get().FinishCompilationForObjects({ Binding });
		if (!Binding->IsValid())
		{
			Report.Warning(FText::Format(LOCTEXT("BindingInvalid", "{0}: the binding was built but is not valid (see the Output Log, category LogHairStrands)."),
				FText::FromString(Binding->GetName())));
		}
		UE_LOG(LogDTHImport, Log, TEXT("%s: %s on %s, section %d (%s), %d interpolation points."), *Binding->GetName(), *Groom->GetName(),
			*Body->GetName(), Section, *Plan.MatchingSlot, Plan.InterpolationPoints);
		return Binding;
	}
}

TArray<FDTHGroomPlan> DTHImport::BuildGroomPlans(const FDTHExport& Export)
{
	FString HeadSlot;
	if (const FDTHMeshEntry* Body = Export.FindBody())
	{
		if (const FDTHMaterialSlot* Head = Body->Materials.FindByPredicate([](const FDTHMaterialSlot& Slot) { return Slot.Slot.EndsWith(TEXT("Head")); }))
		{
			HeadSlot = Head->Slot;
		}
	}
	TArray<FDTHGroomPlan> Plans;
	for (const FDTHGroomEntry& Groom : Export.Grooms)
	{
		FDTHGroomPlan& Plan = Plans.AddDefaulted_GetRef();
		Plan.Name = Groom.Name;
		Plan.File = Groom.File;
		Plan.InterpolationPoints = DefaultInterpolationPoints(Groom.Name);
		Plan.MatchingSlot = HeadSlot;
	}
	return Plans;
}

TArray<FDTHBuiltGroom> DTHImport::BuildGrooms(const FDTHExport& Export, const TArray<FDTHGroomPlan>& Plans, USkeletalMesh* Body,
	const FString& DestinationFolder, bool bReimportExisting, FDTHImportReport& Report)
{
	TArray<FDTHBuiltGroom> Built;
	FString Folder = DestinationFolder;
	Folder.RemoveFromEnd(TEXT("/"));
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<FString> Made;
	for (const FDTHGroomPlan& Plan : Plans)
	{
		if (!Plan.bImport)
		{
			continue;
		}
		const FString AssetPath = Folder / FPaths::GetBaseFilename(Plan.File);
		UGroomAsset* Groom = bReimportExisting ? nullptr : LoadObject<UGroomAsset>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Groom)
		{
			Report.Info(FText::Format(LOCTEXT("GroomReused", "{0} exists; reused (tick Reimport to replace it)."), FText::FromString(AssetPath)));
		}
		else if (!FPaths::FileExists(Plan.File))
		{
			Report.Warning(FText::Format(LOCTEXT("GroomMissing", "Groom file missing, skipped: {0}"), FText::FromString(Plan.File)));
			continue;
		}
		else if (!(Groom = ImportGroom(Plan, Folder, AssetTools)))
		{
			Report.Warning(FText::Format(LOCTEXT("GroomFailed", "{0} did not import as a groom (see the Output Log, category LogHairStrands)."),
				FText::FromString(Plan.File)));
			continue;
		}

		UMaterialInstanceConstant* Material = AssignHairMaterial(Export, Plan, Groom, Folder, AssetTools, Report);
		UGroomBindingAsset* Binding = Body ? BindGroom(Plan, Groom, Body, Folder, Report) : nullptr;
		Report.Info(FText::Format(LOCTEXT("GroomDone", "Groom {0}: {1} groups, material {2}, binding {3}."), FText::FromString(Groom->GetName()),
			Groom->GetNumHairGroups(), FText::FromString(Material ? Material->GetName() : TEXT("none")),
			FText::FromString(Binding ? Binding->GetName() : TEXT("none"))));
		Made.Add(Groom->GetName() + (Binding ? TEXT(" + ") + Binding->GetName() : FString()));
		Built.Add({ Plan.Name, Groom, Binding });
	}

	if (!Body && Made.Num() > 0)
	{
		Report.Warning(LOCTEXT("NoBody", "No body mesh was imported, so the grooms were not bound."));
	}
	return Built;
}

#undef LOCTEXT_NAMESPACE
