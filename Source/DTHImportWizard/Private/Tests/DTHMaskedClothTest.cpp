#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Steps/DTHImportRun.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Cloth with an opacity map: the import puts it under the per-character dithered copy of DTH's masked cloth, with the
 * opacity on "Mask Texture" and the mask switched on; cloth without one stays on the opaque Cloth parent.
 *
 * Reads [DTHImportWizard.Tests] ExportFolder (an export with opacity-mapped clothing) and Destination from
 * EditorPerProjectUserSettings; skipped when ExportFolder isn't set. Imports the body and every mesh whose name
 * contains [DTHImportWizard.Tests] MeshFilter, without grooms.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHMaskedClothTest, "DTH.Import.MaskedCloth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHMaskedClothTest::RunTest(const FString& Parameters)
{
	FString ExportFolder, Destination, MeshFilter;
	GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("ExportFolder"), ExportFolder, GEditorPerProjectIni);
	GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("Destination"), Destination, GEditorPerProjectIni);
	GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("MeshFilter"), MeshFilter, GEditorPerProjectIni);
	if (ExportFolder.IsEmpty() || Destination.IsEmpty())
	{
		AddInfo(TEXT("Skipped: [DTHImportWizard.Tests] ExportFolder and Destination are not set."));
		return true;
	}

	FString Error;
	FDTHImportRequest Request;
	Request.Export = FDTHExport::Load(ExportFolder, Error);
	if (!TestNotNull(TEXT("Export loads: ") + Error, Request.Export.Get())) return false;
	Request.DestinationFolder = Destination;
	Request.GroomPlans = TArray<FDTHGroomPlan>();
	for (int32 Index = 0; Index < Request.Export->Meshes.Num(); ++Index)
	{
		const FDTHMeshEntry& Mesh = Request.Export->Meshes[Index];
		if (Mesh.bInManifest && (Mesh.IsBody() || MeshFilter.IsEmpty() || Mesh.Name.Contains(MeshFilter)))
		{
			Request.MeshIndices.Add(Index);
		}
	}
	const FDTHImportReport Report = DTHImport::RunImport(Request);
	for (const FDTHImportMessage& Message : Report.Messages)
	{
		AddInfo(Message.Text.ToString());
	}
	TestTrue(TEXT("Import succeeds"), Report.bSucceeded);

	// Expected parent per slot, from the plan's own rule: Cloth with an opacity texture is masked.
	int32 Masked = 0;
	for (const int32 Index : Request.MeshIndices)
	{
		for (const FDTHMaterialSlot& Slot : Request.Export->Meshes[Index].Materials)
		{
			if (Slot.Type != TEXT("Cloth")) continue;
			const FString* Opacity = Slot.Textures.Find(TEXT("opacity"));
			const bool bWantMasked = Opacity && !Opacity->IsEmpty();
			const FString Path = Destination / TEXT("Materials") / Slot.Slot;
			const UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *Path, nullptr, LOAD_NoWarn);
			if (!TestNotNull(Slot.Slot + TEXT(" exists"), Instance)) continue;
			const UMaterial* Base = Instance->GetMaterial();
			AddInfo(FString::Printf(TEXT("%s: parent %s, base %s, blend %d"), *Slot.Slot, *GetNameSafe(Instance->Parent), *GetNameSafe(Base),
				int32(Instance->GetBlendMode())));
			if (!bWantMasked)
			{
				TestEqual(Slot.Slot + TEXT(" is opaque"), Instance->GetBlendMode(), BLEND_Opaque);
				continue;
			}
			++Masked;
			TestEqual(Slot.Slot + TEXT(" is masked"), Instance->GetBlendMode(), BLEND_Masked);
			TestEqual(Slot.Slot + TEXT(" base material"), GetNameSafe(Base), TEXT("M_") + Request.Export->Character + TEXT("ClothMasked"));
			UTexture* Mask = nullptr;
			Instance->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Mask Texture")), Mask);
			TestTrue(Slot.Slot + TEXT(" has the opacity map on Mask Texture"), Mask && Mask->GetName().EndsWith(TEXT("_Opacity")));
			for (const TCHAR* Switch : { TEXT("Enable Mask"), TEXT("Use Mask Texture") })
			{
				bool bValue = false;
				FGuid Id;
				Instance->GetStaticSwitchParameterValue(FMaterialParameterInfo(Switch), bValue, Id);
				TestTrue(Slot.Slot + TEXT(" ") + Switch, bValue);
			}
			if (Base)
			{
				const UMaterialExpression* Feed = Base->GetEditorOnlyData()->OpacityMask.Expression;
				const UMaterialExpressionAdd* Add = Cast<UMaterialExpressionAdd>(Feed);
				TestTrue(Slot.Slot + TEXT(" opacity mask is dithered"), Add != nullptr);
				if (Add)
				{
					TestEqual(Slot.Slot + TEXT(" dither adds the clip value"), Add->ConstB, Base->OpacityMaskClipValue);
				}
			}
		}
	}
	TestTrue(TEXT("At least one opacity-mapped cloth slot"), Masked > 0);
	return true;
}

#endif
