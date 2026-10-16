#include "Steps/DTHMaterials.h"

#include "AssetToolsModule.h"
#include "DTHImportLog.h"
#include "Engine/Texture2D.h"
#include "Export/DTHExport.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"
#include "Settings/DTHImportSettings.h"
#include "Steps/DTHImportReport.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DTHMaterials"

namespace
{
	/** How a texture is imported, by the DTH importer's three texture pipelines. */
	enum class ETextureKind : uint8 { Colour, Normal, Mask };

	FString AssetSafe(const FString& Name)
	{
		FString Out;
		for (const TCHAR Char : Name)
		{
			Out.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
		}
		return Out;
	}

	/** DTH's master instance for a material type, relative to the DazToHue content folder, and the suffix of its per-character copy. */
	bool MasterFor(const FString& Type, FString& OutMaster, FString& OutCopySuffix, bool& bOutSlotMaterial)
	{
		struct FMaster { const TCHAR* Type; const TCHAR* Path; const TCHAR* Suffix; bool bSlotMaterial; };
		static const FMaster Masters[] = {
			{ TEXT("Skin"), TEXT("Common/Materials/Skin/MI_DTH_Skin"), TEXT("SkinMaster"), true },
			{ TEXT("Cloth"), TEXT("Common/Materials/Cloth/MI_DTH_Cloth"), TEXT("ClothMaster"), true },
			{ TEXT("Metal"), TEXT("Common/Materials/Metal/MI_DTH_Metal"), TEXT("MetalMaster"), true },
			{ TEXT("Plastic"), TEXT("Common/Materials/Plastic/MI_DTH_Plastic"), TEXT("PlasticMaster"), true },
			{ TEXT("Glass"), TEXT("Common/Materials/Glass/MI_DTH_Glass"), TEXT("GlassMaster"), true },
			{ TEXT("Eye"), TEXT("Common/Materials/Eyes/MI_DTH_Eye"), TEXT("Eyes"), false },
			{ TEXT("EyeOcclusion"), TEXT("Common/Materials/Eyes/MI_DTH_EyeOcclusion"), TEXT("EyeOcclusion"), false },
			{ TEXT("Tear"), TEXT("Common/Materials/Eyes/MI_DTH_LacrimalFluid"), TEXT("Tear"), false },
		};
		for (const FMaster& Master : Masters)
		{
			if (Type == Master.Type)
			{
				OutMaster = Master.Path;
				OutCopySuffix = Master.Suffix;
				bOutSlotMaterial = Master.bSlotMaterial;
				return true;
			}
		}
		return false;
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

		void Build(const FDTHMaterialSlot& Slot)
		{
			const FString SlotName = AssetSafe(Slot.Slot);
			if (SlotName.IsEmpty() || Exists(MaterialFolder / SlotName))
			{
				return;
			}

			FString MasterPath, CopySuffix;
			bool bSlotMaterial = false;
			if (!MasterFor(Slot.Type, MasterPath, CopySuffix, bSlotMaterial))
			{
				Report.Warning(FText::Format(LOCTEXT("UnknownType", "Material slot {0}: type \"{1}\" has no DTH material; the slot is left without one."),
					FText::FromString(Slot.Slot), FText::FromString(Slot.Type)));
				return;
			}
			UMaterialInstanceConstant* Master = PerCharacterMaster(MasterPath, CopySuffix);
			if (!Master || !bSlotMaterial)
			{
				// Eye, eye occlusion and tear slots use the per-character copy itself, which carries the slot's name.
				return;
			}

			// The texture channels the importer reads, in its order. Metallic is imported but never set on the material, as there.
			UTexture2D* Colour = ImportTexture(Slot, TEXT("colour"), ETextureKind::Colour);
			UTexture2D* Normal = ImportTexture(Slot, TEXT("normal"), ETextureKind::Normal);
			UTexture2D* Specular = ImportTexture(Slot, TEXT("specular"), ETextureKind::Mask);
			UTexture2D* Roughness = ImportTexture(Slot, TEXT("roughness"), ETextureKind::Mask);
			ImportTexture(Slot, TEXT("metallic"), ETextureKind::Mask);
			UTexture2D* Opacity = ImportTexture(Slot, TEXT("opacity"), ETextureKind::Mask);

			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Master;
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(
				AssetTools.CreateAsset(SlotName, MaterialFolder, UMaterialInstanceConstant::StaticClass(), Factory));
			if (!Instance)
			{
				Report.Error(FText::Format(LOCTEXT("InstanceFailed", "Could not create material {0}."), FText::FromString(MaterialFolder / SlotName)));
				return;
			}

			// The parameters the importer sets; a material without one of them is left as it is.
			SetTexture(Instance, TEXT("Use Base Colour Texture"), TEXT("Base Colour Texture"), Colour);
			SetTexture(Instance, nullptr, TEXT("Base Normal Texture"), Normal);
			SetTexture(Instance, TEXT("Use Base Specular Texture"), TEXT("Base Specular Texture"), Specular);
			SetTexture(Instance, TEXT("Use Base Roughness Texture"), TEXT("Base Roughness Texture"), Roughness);
			SetTexture(Instance, TEXT("Use Base Opacity Texture"), TEXT("Base Opacity Texture"), Opacity);
			UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
			Instance->MarkPackageDirty();
			++Built;
		}

		void Finish()
		{
			int32 MasterCount = 0, TextureCount = 0;
			for (const TPair<FString, UMaterialInstanceConstant*>& Pair : Masters)
			{
				MasterCount += Pair.Value ? 1 : 0;
			}
			for (const TPair<FString, UTexture2D*>& Pair : TextureAssets)
			{
				TextureCount += Pair.Value ? 1 : 0;
			}
			Report.Info(FText::Format(LOCTEXT("Built", "Materials: {0} slot materials and {1} DTH master copies in {2}, {3} textures in {4}."),
				Built, MasterCount, FText::FromString(MaterialFolder), TextureCount, FText::FromString(TextureFolder)));
		}

	private:
		static bool Exists(const FString& PackagePath)
		{
			return FPackageName::DoesPackageExist(PackagePath) || FindPackage(nullptr, *PackagePath);
		}

		/** MI_<Character><Suffix>: one copy of a DTH master instance per character, made the first time a slot needs it. */
		UMaterialInstanceConstant* PerCharacterMaster(const FString& MasterPath, const FString& CopySuffix)
		{
			if (UMaterialInstanceConstant** Found = Masters.Find(CopySuffix))
			{
				return *Found;
			}
			const FString CopyName = FString::Printf(TEXT("MI_%s%s"), *AssetSafe(Export.Character), *CopySuffix);
			const FString CopyPath = MaterialFolder / CopyName;
			UMaterialInstanceConstant* Copy = LoadObject<UMaterialInstanceConstant>(nullptr, *CopyPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Copy)
			{
				const FString StockPath = GetDefault<UDTHImportSettings>()->DazToHueAsset(MasterPath);
				UMaterialInstanceConstant* Stock = LoadObject<UMaterialInstanceConstant>(nullptr, *StockPath, nullptr, LOAD_NoWarn);
				if (!Stock)
				{
					Report.Error(FText::Format(LOCTEXT("MasterMissing", "DTH material not found: {0}"), FText::FromString(StockPath)));
					Masters.Add(CopySuffix, nullptr);
					return nullptr;
				}
				Copy = Cast<UMaterialInstanceConstant>(AssetTools.DuplicateAsset(CopyName, MaterialFolder, Stock));
				if (!Copy)
				{
					Report.Error(FText::Format(LOCTEXT("MasterCopyFailed", "Could not copy {0} to {1}."), FText::FromString(StockPath), FText::FromString(CopyPath)));
				}
			}
			Masters.Add(CopySuffix, Copy);
			return Copy;
		}

		UTexture2D* ImportTexture(const FDTHMaterialSlot& Slot, const TCHAR* Channel, ETextureKind Kind)
		{
			const FString* File = Slot.Textures.Find(Channel);
			if (!File || File->IsEmpty())
			{
				return nullptr;
			}
			const FString Source = FPaths::ConvertRelativePathToFull(*File);
			const FString Key = FString::Printf(TEXT("%d|%s"), static_cast<int32>(Kind), *Source.ToLower());
			if (UTexture2D** Imported = TextureAssets.Find(Key))
			{
				return *Imported;
			}
			if (!FPaths::FileExists(Source))
			{
				Report.Warning(FText::Format(LOCTEXT("TextureMissing", "Material slot {0}: {1} texture not found: {2}"),
					FText::FromString(Slot.Slot), FText::FromString(Channel), FText::FromString(Source)));
				TextureAssets.Add(Key, nullptr);
				return nullptr;
			}

			// Named after the file, as Interchange names it. Different files with the same name (outfit textures often share
			// names across folders) each keep their own asset rather than overwriting one another.
			const FString BaseName = AssetSafe(FPaths::GetBaseFilename(Source));
			FString AssetName = BaseName;
			for (int32 Suffix = 2; UsedTextureNames.Contains(AssetName); ++Suffix)
			{
				AssetName = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
			}
			UsedTextureNames.Add(AssetName);

			TStrongObjectPtr<UInterchangeGenericTexturePipeline> Pipeline(NewObject<UInterchangeGenericTexturePipeline>(
				GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInterchangeGenericTexturePipeline::StaticClass(), TEXT("DTHTexturePipeline")),
				RF_Transient));
			// The DTH importer's colour and mask pipelines turn normal-map detection off; its normal pipeline flips the green channel.
			Pipeline->AssetName = AssetName;
			Pipeline->bDetectNormalMapTexture = Kind == ETextureKind::Normal;
			Pipeline->bFlipNormalMapGreenChannel = Kind == ETextureKind::Normal;

			FImportAssetParameters Parameters;
			Parameters.bIsAutomated = true;
			Parameters.bReplaceExisting = true;
			Parameters.OverridePipelines.Add(FSoftObjectPath(Pipeline.Get()));

			TArray<UObject*> Imported;
			UInterchangeManager::GetInterchangeManager().ImportAsset(TextureFolder, UInterchangeManager::CreateSourceData(Source), Parameters, Imported);
			UTexture2D* Texture = nullptr;
			for (UObject* Object : Imported)
			{
				if ((Texture = Cast<UTexture2D>(Object)) != nullptr)
				{
					break;
				}
			}
			if (!Texture)
			{
				Report.Warning(FText::Format(LOCTEXT("TextureFailed", "Material slot {0}: {1} did not import as a texture."),
					FText::FromString(Slot.Slot), FText::FromString(Source)));
				TextureAssets.Add(Key, nullptr);
				return nullptr;
			}

			// The settings the importer gives each kind after import.
			Texture->Modify();
			switch (Kind)
			{
			case ETextureKind::Colour:
				Texture->LODGroup = TEXTUREGROUP_Character;
				break;
			case ETextureKind::Normal:
				Texture->CompressionSettings = TC_Normalmap;
				Texture->SRGB = false;
				Texture->LODGroup = TEXTUREGROUP_CharacterNormalMap;
				break;
			case ETextureKind::Mask:
				Texture->LODGroup = TEXTUREGROUP_Character;
				Texture->SRGB = false;
				Texture->CompressionSettings = TC_Grayscale;
				break;
			}
			Texture->PostEditChange();
			Texture->MarkPackageDirty();
			TextureAssets.Add(Key, Texture);
			return Texture;
		}

		static void SetTexture(UMaterialInstanceConstant* Instance, const TCHAR* Switch, const TCHAR* Parameter, UTexture2D* Texture)
		{
			if (!Texture)
			{
				return;
			}
			if (Switch)
			{
				UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(Instance, Switch, true, EMaterialParameterAssociation::GlobalParameter, false);
			}
			UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, Parameter, Texture);
		}

		const FDTHExport& Export;
		const FString MaterialFolder;
		const FString TextureFolder;
		FDTHImportReport& Report;
		IAssetTools& AssetTools;

		TMap<FString, UMaterialInstanceConstant*> Masters;
		TMap<FString, UTexture2D*> TextureAssets;
		TSet<FString> UsedTextureNames;
		int32 Built = 0;
	};
}

void DTHImport::BuildMaterials(const FDTHExport& Export, const TArray<const FDTHMeshEntry*>& Meshes, const FString& DestinationFolder, FDTHImportReport& Report)
{
	FString Folder = DestinationFolder;
	Folder.RemoveFromEnd(TEXT("/"));
	FMaterialBuilder Builder(Export, Folder, Report);
	for (const FDTHMeshEntry* Mesh : Meshes)
	{
		for (const FDTHMaterialSlot& Slot : Mesh->Materials)
		{
			Builder.Build(Slot);
		}
	}
	Builder.Finish();
}

#undef LOCTEXT_NAMESPACE
