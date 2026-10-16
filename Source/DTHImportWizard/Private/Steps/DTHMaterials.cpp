#include "Steps/DTHMaterials.h"

#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "DTHImportLog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Export/DTHExport.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Settings/DTHImportSettings.h"
#include "Steps/DTHImportReport.h"

#define LOCTEXT_NAMESPACE "DTHMaterials"

namespace
{
	FString AssetSafe(const FString& Name)
	{
		FString Out;
		for (const TCHAR Char : Name)
		{
			Out.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
		}
		return Out;
	}

	/** The slot material IS the per-character copy for these families, as the DTH importer does. */
	bool SlotIsCopy(const FString& Parent)
	{
		return Parent == TEXT("Eye") || Parent == TEXT("EyeOcclusion") || Parent == TEXT("Tear");
	}

	/** Import settings per kind (the old wizard's, which gave correct results). */
	void ApplyKind(UTexture2D* Texture, EDTHTextureKind Kind, bool bFlipGreen)
	{
		Texture->Modify();
		switch (Kind)
		{
		case EDTHTextureKind::Color:
			Texture->SRGB = true;
			Texture->CompressionSettings = TC_Default;
			break;
		case EDTHTextureKind::Normal:
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Normalmap;
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
			break;
		case EDTHTextureKind::Grayscale:
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Grayscale;
			break;
		case EDTHTextureKind::Linear:
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Default;
			break;
		}
		Texture->bFlipGreenChannel = bFlipGreen && Kind == EDTHTextureKind::Normal;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
	}

	/** "Use Base Colour Texture" for "Base Colour Texture", "Use Normal Texture" for "Normal Texture"... */
	FName SwitchFor(const FString& Parameter, const TArray<FName>& Switches)
	{
		FString Without = Parameter;
		Without.RemoveFromStart(TEXT("Base "));
		for (const FString& Candidate : { TEXT("Use ") + Parameter, TEXT("Use ") + Without })
		{
			if (Switches.Contains(FName(*Candidate))) return FName(*Candidate);
		}
		return NAME_None;
	}

	class FMaterialBuilder
	{
	public:
		FMaterialBuilder(const FDTHExport& InExport, const FString& DestinationFolder, FDTHImportReport& InReport)
			: Export(InExport)
			, MaterialFolder(DestinationFolder / TEXT("Materials"))
			, TextureFolder(DestinationFolder / TEXT("Textures"))
			, Report(InReport)
			, AssetTools(FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get())
		{
		}

		UMaterialInstanceConstant* Build(const FDTHMaterialPlan& Plan)
		{
			if (Plan.Parent == TEXT("Skip")) return nullptr;
			const FString SlotName = AssetSafe(Plan.Slot);
			const FString StockPath = DTHImport::ParentMaterialPath(Plan.Parent);
			if (StockPath.IsEmpty())
			{
				Report.Warning(FText::Format(LOCTEXT("UnknownParent", "{0}: no DTH material for \"{1}\"; the slot is left as it is."),
					FText::FromString(Plan.Slot), FText::FromString(Plan.Parent)));
				return nullptr;
			}
			for (const FDTHTextureRow& Row : Plan.Textures)
			{
				if (Row.bProblem)
				{
					Report.Warning(FText::Format(LOCTEXT("TextureProblem", "{0}: {1} - {2}"), FText::FromString(Plan.Slot), FText::FromString(Row.Parameter), FText::FromString(Row.Why)));
				}
			}

			if (SlotIsCopy(Plan.Parent))
			{
				return CopyOf(StockPath, SlotName);
			}

			UMaterialInstanceConstant* Master = CopyOf(StockPath, FString::Printf(TEXT("MI_%s%sMaster"), *AssetSafe(Export.Character), *Plan.Parent));
			if (!Master) return nullptr;

			const FString Path = MaterialFolder / SlotName;
			UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Instance)
			{
				UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = Master;
				Instance = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(SlotName, MaterialFolder, UMaterialInstanceConstant::StaticClass(), Factory));
				if (!Instance)
				{
					Report.Error(FText::Format(LOCTEXT("InstanceFailed", "Could not create material {0}."), FText::FromString(Path)));
					return nullptr;
				}
			}
			else if (Instance->Parent != Master)
			{
				UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Master);
			}

			TArray<FName> Parameters, Switches;
			UMaterialEditingLibrary::GetTextureParameterNames(Master, Parameters);
			UMaterialEditingLibrary::GetStaticSwitchParameterNames(Master, Switches);
			int32 Set = 0;
			for (const FDTHTextureRow& Row : Plan.Textures)
			{
				UTexture2D* Texture = Row.Source.IsEmpty() ? nullptr : TextureFor(Row);
				if (!Texture) continue;
				if (!Parameters.Contains(FName(*Row.Parameter)))
				{
					Report.Warning(FText::Format(LOCTEXT("NoParameter", "{0}: {1} has no parameter \"{2}\"."),
						FText::FromString(Plan.Slot), FText::FromString(Master->GetName()), FText::FromString(Row.Parameter)));
					continue;
				}
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, FName(*Row.Parameter), Texture);
				const FName Switch = SwitchFor(Row.Parameter, Switches);
				if (!Switch.IsNone())
				{
					UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(Instance, Switch, true, EMaterialParameterAssociation::GlobalParameter, false);
				}
				++Set;
			}
			UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
			Instance->MarkPackageDirty();
			++Built;
			UE_LOG(LogDTHImport, Log, TEXT("%s: parent %s, %d textures."), *SlotName, *Master->GetName(), Set);
			return Instance;
		}

		void Finish() const
		{
			Report.Info(FText::Format(LOCTEXT("Built", "Materials: {0} slot materials in {1}; {2} textures imported, {3} reused, in {4}."),
				Built, FText::FromString(MaterialFolder), Imported, Reused, FText::FromString(TextureFolder)));
		}

	private:
		/** MI_<...> in the material folder: a copy of DTH's instance at StockPath, made once per character. */
		UMaterialInstanceConstant* CopyOf(const FString& StockPath, const FString& CopyName)
		{
			if (UMaterialInstanceConstant** Found = Copies.Find(CopyName)) return *Found;
			UMaterialInstanceConstant* Copy = LoadObject<UMaterialInstanceConstant>(nullptr, *(MaterialFolder / CopyName), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Copy)
			{
				const FString FullStock = GetDefault<UDTHImportSettings>()->DazToHueAsset(StockPath);
				UMaterialInstanceConstant* Stock = LoadObject<UMaterialInstanceConstant>(nullptr, *FullStock, nullptr, LOAD_NoWarn);
				Copy = Stock ? Cast<UMaterialInstanceConstant>(AssetTools.DuplicateAsset(CopyName, MaterialFolder, Stock)) : nullptr;
				if (!Copy)
				{
					Report.Error(FText::Format(LOCTEXT("CopyFailed", "Could not copy DTH material {0} to {1}."), FText::FromString(FullStock), FText::FromString(CopyName)));
				}
			}
			Copies.Add(CopyName, Copy);
			return Copy;
		}

		/** The plan's texture: the existing asset (settings re-applied), else imported under the plan's name. */
		UTexture2D* TextureFor(const FDTHTextureRow& Row)
		{
			if (UTexture2D** Done = Textures.Find(Row.AssetName)) return *Done;
			const FString Path = TextureFolder / Row.AssetName;
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Texture)
			{
				++Reused;
			}
			else if (!FPaths::FileExists(Row.Source))
			{
				Report.Warning(FText::Format(LOCTEXT("TextureMissing", "Texture file missing, skipped: {0}"), FText::FromString(Row.Source)));
			}
			else
			{
				// An import task with an explicit destination name: Interchange's texture pipeline names the asset after
				// the file, so same-named files from different folders overwrote one another.
				UAssetImportTask* Task = NewObject<UAssetImportTask>();
				Task->Filename = Row.Source;
				Task->DestinationPath = TextureFolder;
				Task->DestinationName = Row.AssetName;
				Task->bReplaceExisting = true;
				Task->bAutomated = true;
				Task->bSave = false;
				AssetTools.ImportAssetTasks({ Task });
				Texture = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
				if (Texture)
				{
					++Imported;
				}
				else
				{
					Report.Warning(FText::Format(LOCTEXT("TextureFailed", "{0} did not import as a texture."), FText::FromString(Row.Source)));
				}
			}
			if (Texture)
			{
				ApplyKind(Texture, Row.Kind, Row.bFlipGreen);
			}
			Textures.Add(Row.AssetName, Texture);
			return Texture;
		}

		const FDTHExport& Export;
		const FString MaterialFolder;
		const FString TextureFolder;
		FDTHImportReport& Report;
		IAssetTools& AssetTools;
		TMap<FString, UMaterialInstanceConstant*> Copies;
		TMap<FString, UTexture2D*> Textures;
		int32 Built = 0, Imported = 0, Reused = 0;
	};
}

TMap<FString, UMaterialInstanceConstant*> DTHImport::BuildMaterials(const FDTHExport& Export, const TArray<FDTHMaterialPlan>& Plans,
	const FString& DestinationFolder, FDTHImportReport& Report)
{
	FString Folder = DestinationFolder;
	Folder.RemoveFromEnd(TEXT("/"));
	FMaterialBuilder Builder(Export, Folder, Report);
	TMap<FString, UMaterialInstanceConstant*> Made;
	for (const FDTHMaterialPlan& Plan : Plans)
	{
		if (UMaterialInstanceConstant* Instance = Builder.Build(Plan))
		{
			Made.Add(Plan.Slot, Instance);
		}
	}
	Builder.Finish();
	return Made;
}

void DTHImport::AssignMaterials(const TArray<USkeletalMesh*>& Meshes, const TMap<FString, UMaterialInstanceConstant*>& Materials, FDTHImportReport& Report)
{
	for (USkeletalMesh* Mesh : Meshes)
	{
		TArray<FSkeletalMaterial> Slots = Mesh->GetMaterials();
		int32 Changed = 0, Missing = 0;
		for (FSkeletalMaterial& Slot : Slots)
		{
			UMaterialInstanceConstant* const* Material = Materials.Find(Slot.MaterialSlotName.ToString());
			if (!Material)
			{
				++Missing;
				continue;
			}
			if (Slot.MaterialInterface != *Material)
			{
				Slot.MaterialInterface = *Material;
				++Changed;
			}
		}
		if (Changed > 0)
		{
			Mesh->Modify();
			Mesh->SetMaterials(Slots);
			Mesh->PostEditChange();
			Mesh->MarkPackageDirty();
		}
		if (Missing > 0)
		{
			Report.Warning(FText::Format(LOCTEXT("SlotsWithout", "{0}: {1} of {2} material slots have no material from the export."),
				FText::FromString(Mesh->GetName()), Missing, Slots.Num()));
		}
	}
}

#undef LOCTEXT_NAMESPACE
