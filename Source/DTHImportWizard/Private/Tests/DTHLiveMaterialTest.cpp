#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Export/DTHExport.h"
#include "LevelEditorViewport.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "RenderingThread.h"
#include "Steps/DTHMaterialPlan.h"
#include "Steps/DTHMaterials.h"
#include "Steps/DTHImportReport.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Reimporting a character that is placed in the open level, whose slot materials sit under an older parent (Vi_gen,
 * imported before the per-character masters existed: MI_Vi_genHead under DTH's MI_DTH_Skin, with a dynamic instance of
 * it on the placed actor). The material step moves each instance under its MI_<Character><Family>Master; before the
 * fix the dynamic instance kept uniform expressions cached against the old shader map and the renderer stopped the
 * editor ("UniformExpressionCache should be up to date, RenderProxy=MID_MI_Vi_genHead_0").
 *
 * Builds the body's materials from [DTHImportWizard.Tests] ExportFolder into /Game/DTH_Characters/_Test/LiveMaterial,
 * moves one instance back under its master's DTH parent, puts a dynamic instance of it on a transient sphere in front
 * of the level viewport, draws, builds the materials again and draws again. Passing = the editor is still running and
 * the instance is back under its master.
 */
namespace DTHLiveMaterialTest
{
	const TCHAR* Destination = TEXT("/Game/DTH_Characters/_Test/LiveMaterial");

	struct FState
	{
		TSharedPtr<FDTHExport> Export;
		TArray<FDTHMaterialPlan> Plans;
		TWeakObjectPtr<UMaterialInstanceConstant> Instance;
		TWeakObjectPtr<UMaterialInterface> Master;
		TWeakObjectPtr<AStaticMeshActor> Actor;
	};

	void Draw()
	{
		FlushRenderingCommands();
		if (GEditor)
		{
			GEditor->RedrawAllViewports(true);
		}
		FlushRenderingCommands();
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDTHLiveMaterialDraw, int32, Frames);
bool FDTHLiveMaterialDraw::Update()
{
	DTHLiveMaterialTest::Draw();
	return --Frames <= 0;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FDTHLiveMaterialRebuild, FAutomationTestBase*, Test, TSharedPtr<DTHLiveMaterialTest::FState>, State);
bool FDTHLiveMaterialRebuild::Update()
{
	FDTHImportReport Report;
	DTHImport::BuildMaterials(*State->Export, State->Plans, DTHLiveMaterialTest::Destination, Report);
	Test->TestTrue(TEXT("Instance is still loaded"), State->Instance.IsValid());
	if (State->Instance.IsValid())
	{
		Test->TestEqual(TEXT("Instance is back under its master"), State->Instance->Parent.Get(), State->Master.Get());
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FDTHLiveMaterialFinish, FAutomationTestBase*, Test, TSharedPtr<DTHLiveMaterialTest::FState>, State);
bool FDTHLiveMaterialFinish::Update()
{
	if (AStaticMeshActor* Actor = State->Actor.Get())
	{
		UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Actor->GetStaticMeshComponent()->GetMaterial(0));
		Test->TestTrue(TEXT("The dynamic instance still derives from the rebuilt instance"),
			Dynamic && State->Instance.IsValid() && Dynamic->Parent == State->Instance.Get());
		Actor->Destroy();
	}
	Test->AddInfo(TEXT("Drew the dynamic instance after the rebuild without the renderer stopping the editor."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHLiveMaterialTest, "DTH.Import.LiveMaterialReparent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHLiveMaterialTest::RunTest(const FString& Parameters)
{
	using namespace DTHLiveMaterialTest;
	FString ExportFolder;
	GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("ExportFolder"), ExportFolder, GEditorPerProjectIni);
	if (ExportFolder.IsEmpty())
	{
		AddInfo(TEXT("Skipped: [DTHImportWizard.Tests] ExportFolder is not set."));
		return true;
	}

	TSharedPtr<FState> State = MakeShared<FState>();
	FString Error;
	State->Export = FDTHExport::Load(ExportFolder, Error);
	if (!TestNotNull(TEXT("Export loads: ") + Error, State->Export.Get())) return false;

	TArray<const FDTHMeshEntry*> Body;
	for (const FDTHMeshEntry& Mesh : State->Export->Meshes)
	{
		if (Mesh.bInManifest && Mesh.IsBody()) Body.Add(&Mesh);
	}
	if (!TestTrue(TEXT("Export has a body"), Body.Num() > 0)) return false;
	State->Plans = DTHImport::BuildMaterialPlan(*State->Export, Body);

	FDTHImportReport Report;
	const TMap<FString, UMaterialInstanceConstant*> Built = DTHImport::BuildMaterials(*State->Export, State->Plans, Destination, Report);

	// An instance under a per-character master (not one of the slot-is-the-copy families).
	for (const TPair<FString, UMaterialInstanceConstant*>& Pair : Built)
	{
		UMaterialInstanceConstant* Master = Cast<UMaterialInstanceConstant>(Pair.Value->Parent);
		if (Master && Master->GetName().EndsWith(TEXT("Master")) && Master->Parent)
		{
			State->Instance = Pair.Value;
			State->Master = Master;
			break;
		}
	}
	if (!TestTrue(TEXT("Found an instance under a per-character master"), State->Instance.IsValid())) return false;
	AddInfo(FString::Printf(TEXT("Instance %s, master %s, older parent %s"), *State->Instance->GetName(),
		*State->Master->GetName(), *GetNameSafe(Cast<UMaterialInstanceConstant>(State->Master.Get())->Parent)));

	// As an older import left it: straight under DTH's material.
	{
		FMaterialUpdateContext Context;
		State->Instance->SetParentEditorOnly(Cast<UMaterialInstanceConstant>(State->Master.Get())->Parent);
		Context.AddMaterialInstance(State->Instance.Get());
	}

	// Placed and drawing, with a dynamic instance of it, in front of the level viewport (the camera is not moved).
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world"), World)) return false;
	FVector Location = FVector::ZeroVector;
	if (GCurrentLevelEditingViewportClient)
	{
		Location = GCurrentLevelEditingViewportClient->GetViewLocation() + GCurrentLevelEditingViewportClient->GetViewRotation().Vector() * 150.0;
	}
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags = RF_Transient;
	Spawn.bNoFail = true;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, Spawn);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestNotNull(TEXT("Sphere mesh"), Sphere)) return false;
	Actor->GetStaticMeshComponent()->SetStaticMesh(Sphere);
	UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(State->Instance.Get(), Actor);
	Actor->GetStaticMeshComponent()->SetMaterial(0, Dynamic);
	State->Actor = Actor;

	ADD_LATENT_AUTOMATION_COMMAND(FDTHLiveMaterialDraw(5));
	ADD_LATENT_AUTOMATION_COMMAND(FDTHLiveMaterialRebuild(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FDTHLiveMaterialDraw(10));
	ADD_LATENT_AUTOMATION_COMMAND(FDTHLiveMaterialFinish(this, State));
	return true;
}

#endif
