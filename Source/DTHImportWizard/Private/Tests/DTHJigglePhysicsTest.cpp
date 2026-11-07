#include "AnimGraphNode_PoseDriver.h"
#include "AnimGraphNode_SpringBone.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Export/DTHExport.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "PreviewScene.h"
#include "Steps/DTHImportRun.h"
#include "Steps/DTHJigglePhysics.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A real import of a physics export with the option on, then the character moved in a preview scene: at rest nothing
 * deforms, moving makes each region's PHY morphs respond, and after stopping they settle. The export comes from
 * -DTHPhysicsExport= or [DTHImportWizard.Tests] PhysicsExportFolder; assets go to /Game/DTH_Physics/_Test/Jiggle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHJigglePhysicsTest, "DTH.Import.JigglePhysics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace DTHJiggleTest
{
	const TCHAR* const Destination = TEXT("/Game/DTH_Physics/_Test/Jiggle");
	constexpr float Step = 1.f / 60.f;

	struct FRegion
	{
		const TCHAR* Bone;
		const TCHAR* MorphPrefix;
	};
	const FRegion Regions[] = {
		{ TEXT("breast_l"), TEXT("phyBreast") }, { TEXT("breast_r"), TEXT("phyBreast") }, { TEXT("glute_l"), TEXT("phyGlute") },
		{ TEXT("glute_r"), TEXT("phyGlute") }, { TEXT("stomach"), TEXT("phyStomach") },
	};

	bool IsRegionMorph(const FString& Morph, const FRegion& Region)
	{
		if (!Morph.StartsWith(Region.MorphPrefix))
		{
			return false;
		}
		const FString Bone = Region.Bone;
		return Bone == TEXT("stomach") || Morph.EndsWith(Bone.Right(2));
	}

	void Advance(USkeletalMeshComponent* Mesh)
	{
		Mesh->TickAnimation(Step, false);
		Mesh->RefreshBoneTransforms();
	}

	float MaxPhyWeight(USkeletalMeshComponent* Mesh, const TArray<FString>& Morphs)
	{
		float Max = 0.f;
		for (const FString& Morph : Morphs)
		{
			Max = FMath::Max(Max, FMath::Abs(Mesh->GetPostProcessInstance()->GetCurveValue(*Morph)));
		}
		return Max;
	}

	FVector PoseTargetOf(const UAnimBlueprint* Blueprint, FName Bone, FName Pose)
	{
		for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph->GetFName() != UEdGraphSchema_K2::GN_AnimGraph) continue;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				const UAnimGraphNode_PoseDriver* Driver = Cast<UAnimGraphNode_PoseDriver>(Node);
				if (!Driver || Driver->Node.SourceBones.IsEmpty() || Driver->Node.SourceBones[0].BoneName != Bone) continue;
				for (const FPoseDriverTarget& Target : Driver->Node.PoseTargets)
				{
					if (Target.DrivenName == Pose)
					{
						return Target.BoneTransforms[0].TargetTranslation;
					}
				}
			}
		}
		return FVector(BIG_NUMBER);
	}
}

bool FDTHJigglePhysicsTest::RunTest(const FString& Parameters)
{
	using namespace DTHJiggleTest;
	FString Folder;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-DTHPhysicsExport="), Folder))
	{
		GConfig->GetString(TEXT("DTHImportWizard.Tests"), TEXT("PhysicsExportFolder"), Folder, GEditorPerProjectIni);
	}
	if (Folder.IsEmpty())
	{
		AddInfo(TEXT("Skipped: no physics export given."));
		return true;
	}

	FDTHImportRequest Request;
	FString Error;
	Request.Export = FDTHExport::Load(Folder, Error);
	if (!TestTrue(TEXT("Export loads: ") + Error, Request.Export.IsValid())) return false;
	TestTrue(TEXT("Export offers physics"), Request.Export->HasPhysics());
	Request.DestinationFolder = Destination;
	Request.bReimportExistingMeshes = true;
	Request.bJigglePhysics = true;
	for (int32 Index = 0; Index < Request.Export->Meshes.Num(); ++Index)
	{
		if (Request.Export->Meshes[Index].IsBody())
		{
			Request.MeshIndices.Add(Index);
		}
	}
	FDTHImportReport Report;
	if (FParse::Param(FCommandLine::Get(), TEXT("DTHPhysicsSkipImport")))
	{
		Report.bSucceeded = true;
	}
	else
	{
		Report = DTHImport::RunImport(Request);
	}
	for (const FDTHImportMessage& Message : Report.Messages)
	{
		if (Message.Severity == FDTHImportMessage::ESeverity::Error)
		{
			AddError(Message.Text.ToString());
		}
		else
		{
			AddInfo(Message.Text.ToString());
		}
	}
	TestTrue(TEXT("Import succeeds"), Report.bSucceeded);
	TestEqual(TEXT("No manual steps"), Report.ManualSteps.Num(), 0);

	const FString Name = FPaths::GetBaseFilename(Request.Export->FindBody()->File);
	USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, *(FString(Destination) / Name));
	UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *(FString(Destination) / TEXT("Blueprints/ABP_") + Request.Export->Character));
	if (!TestNotNull(TEXT("Body mesh"), Body) || !TestNotNull(TEXT("Blueprint"), Blueprint)) return false;

	int32 Springs = 0, Drivers = 0, Empty = 0;
	for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() != UEdGraphSchema_K2::GN_AnimGraph) continue;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			Springs += Node->IsA<UAnimGraphNode_SpringBone>() ? 1 : 0;
			if (const UAnimGraphNode_PoseDriver* Driver = Cast<UAnimGraphNode_PoseDriver>(Node); Driver && Driver->Node.DriveSource == EPoseDriverSource::Translation)
			{
				++Drivers;
				TestNotNull(TEXT("Driver has the pose asset"), Driver->Node.PoseAsset.Get());
				for (const FPoseDriverTarget& Target : Driver->Node.PoseTargets)
				{
					Empty += Target.BoneTransforms[0].TargetTranslation.IsNearlyZero() ? 1 : 0;
				}
			}
		}
	}
	TestEqual(TEXT("Spring controllers"), Springs, 5);
	TestEqual(TEXT("PHY pose drivers"), Drivers, 5);
	TestEqual(TEXT("Empty PHY targets"), Empty, 0);

	// Against what the transforms read from Houdini give: parent axes applied to the offset, then Y flipped as the export does.
	const FVector BreastOut = PoseTargetOf(Blueprint, TEXT("breast_l"), TEXT("pasPhyBreastOut_l"));
	const FVector BreastUp = PoseTargetOf(Blueprint, TEXT("breast_l"), TEXT("pasPhyBreastUp_l"));
	const FVector GluteOut = PoseTargetOf(Blueprint, TEXT("glute_l"), TEXT("pasPhyGluteOut_l"));
	AddInfo(FString::Printf(TEXT("breast_l Out %s, Up %s; glute_l Out %s"), *BreastOut.ToString(), *BreastUp.ToString(), *GluteOut.ToString()));
	TestTrue(TEXT("breast_l Out target"), BreastOut.Equals(FVector(-1.793, 10.689, 0.001), 0.05));
	TestTrue(TEXT("breast_l Up target"), BreastUp.Equals(FVector(3.207, 10.689, 5.001), 0.05));
	TestTrue(TEXT("glute_l Out target"), GluteOut.Equals(FVector(-0.265, -13.221, 3.885), 0.25));

	IConsoleVariable* ParallelEval = IConsoleManager::Get().FindConsoleVariable(TEXT("a.ParallelAnimEvaluation"));
	IConsoleVariable* ParallelUpdate = IConsoleManager::Get().FindConsoleVariable(TEXT("a.ParallelAnimUpdate"));
	const int32 OldEval = ParallelEval ? ParallelEval->GetInt() : 1;
	const int32 OldUpdate = ParallelUpdate ? ParallelUpdate->GetInt() : 1;
	if (ParallelEval) ParallelEval->Set(0);
	if (ParallelUpdate) ParallelUpdate->Set(0);

	{
		FPreviewScene Scene;
		ASkeletalMeshActor* Actor = Scene.GetWorld()->SpawnActor<ASkeletalMeshActor>();
		USkeletalMeshComponent* Mesh = Actor->GetSkeletalMeshComponent();
		Actor->SetActorHiddenInGame(false);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetSkeletalMesh(Body);
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->InitAnim(true);
		if (!TestNotNull(TEXT("Post-process instance runs"), Mesh->GetPostProcessInstance()))
		{
			if (ParallelEval) ParallelEval->Set(OldEval);
			if (ParallelUpdate) ParallelUpdate->Set(OldUpdate);
			return false;
		}

		TArray<FString> PhyMorphs;
		for (const FString& Morph : Body->K2_GetAllMorphTargetNames())
		{
			if (Morph.StartsWith(TEXT("phy")))
			{
				PhyMorphs.Add(Morph);
			}
		}
		TestEqual(TEXT("PHY morphs"), PhyMorphs.Num(), 43);

		float RestMax = 0.f;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Advance(Mesh);
			RestMax = Frame > 30 ? FMath::Max(RestMax, MaxPhyWeight(Mesh, PhyMorphs)) : RestMax;
		}
		AddInfo(FString::Printf(TEXT("At rest: largest PHY morph weight %.4f"), RestMax));
		TestTrue(TEXT("Nothing deforms at rest"), RestMax < 0.02f);

		// Shake along each axis in turn.
		TMap<FString, float> Peak;
		TMap<FString, double> PeakTravel;
		int32 DirectionWrong = 0, DirectionSamples = 0, ActiveMorphPeak = 0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (int32 Frame = 0; Frame < 180; ++Frame)
			{
				FVector Location = FVector::ZeroVector;
				Location[Axis] = 30.0 * FMath::Sin(Frame * Step * UE_TWO_PI * 2.0);
				Actor->SetActorLocation(Location);
				Advance(Mesh);
				if (Axis == 0)
				{
					// Pushed toward In, the left side reads In and the mirrored right side reads Out; pushed the other way, the reverse.
					const FVector BreastLocal = Mesh->GetBoneTransform(Mesh->GetBoneIndex(TEXT("breast_l")), FTransform::Identity).GetRelativeTransform(Mesh->GetBoneTransform(Mesh->GetBoneIndex(TEXT("clavicle_pec_l")), FTransform::Identity)).GetTranslation();
					const double Push = BreastLocal.X - Body->GetRefSkeleton().GetRefBonePose()[Body->GetRefSkeleton().FindBoneIndex(TEXT("breast_l"))].GetTranslation().X;
					UAnimInstance* Post = Mesh->GetPostProcessInstance();
					const float In = Post->GetCurveValue(TEXT("phyBreastIn_l")), Out = Post->GetCurveValue(TEXT("phyBreastOut_l"));
					const float InRight = Post->GetCurveValue(TEXT("phyBreastIn_r")), OutRight = Post->GetCurveValue(TEXT("phyBreastOut_r"));
					if (FMath::Abs(Push) > 2.0)
					{
						const bool bCorrect = Push > 0.0 ? (In > Out && OutRight > InRight) : (Out > In && InRight > OutRight);
						DirectionWrong += bCorrect ? 0 : 1;
						++DirectionSamples;
					}
					ActiveMorphPeak = FMath::Max(ActiveMorphPeak, Mesh->ActiveMorphTargets.Num());
				}
				for (const FString& Morph : PhyMorphs)
				{
					float& Value = Peak.FindOrAdd(Morph);
					Value = FMath::Max(Value, Mesh->GetPostProcessInstance()->GetCurveValue(*Morph));
				}
				for (const FRegion& Region : Regions)
				{
					const int32 Index = Mesh->GetBoneIndex(Region.Bone);
					const FVector Local = Mesh->GetBoneTransform(Index, FTransform::Identity).GetRelativeTransform(Mesh->GetBoneTransform(Mesh->GetBoneIndex(Mesh->GetParentBone(Region.Bone)), FTransform::Identity)).GetTranslation();
					const FVector RestLocal = Body->GetRefSkeleton().GetRefBonePose()[Body->GetRefSkeleton().FindBoneIndex(Region.Bone)].GetTranslation();
					double& Travel = PeakTravel.FindOrAdd(Region.Bone);
					Travel = FMath::Max(Travel, FVector::Dist(Local, RestLocal));
				}
			}
		}
		for (const FRegion& Region : Regions)
		{
			float Best = 0.f;
			FString BestMorph;
			for (const TPair<FString, float>& Pair : Peak)
			{
				if (IsRegionMorph(Pair.Key, Region) && Pair.Value > Best)
				{
					Best = Pair.Value;
					BestMorph = Pair.Key;
				}
			}
			AddInfo(FString::Printf(TEXT("%s: moved up to %.2f cm; strongest morph %s at %.3f"), Region.Bone, PeakTravel.FindRef(Region.Bone), *BestMorph, Best));
			TestTrue(FString::Printf(TEXT("%s bone moves"), Region.Bone), PeakTravel.FindRef(Region.Bone) > 0.25);
			TestTrue(FString::Printf(TEXT("%s morphs respond"), Region.Bone), Best > 0.05f);
		}

		AddInfo(FString::Printf(TEXT("Direction: %d of %d samples wrong; up to %d active morphs on the mesh"), DirectionWrong, DirectionSamples, ActiveMorphPeak));
		TestTrue(TEXT("Direction was sampled"), DirectionSamples > 20);
		TestEqual(TEXT("Morphs follow the bone direction"), DirectionWrong, 0);
		TestTrue(TEXT("Morphs reach the mesh"), ActiveMorphPeak > 0);

		Actor->SetActorLocation(FVector::ZeroVector);
		for (int32 Frame = 0; Frame < 300; ++Frame)
		{
			Advance(Mesh);
		}
		const float Settled = MaxPhyWeight(Mesh, PhyMorphs);
		AddInfo(FString::Printf(TEXT("After stopping: largest PHY morph weight %.4f"), Settled));
		TestTrue(TEXT("Morphs settle"), Settled < 0.02f);
	}

	if (ParallelEval) ParallelEval->Set(OldEval);
	if (ParallelUpdate) ParallelUpdate->Set(OldUpdate);
	return true;
}

#endif
