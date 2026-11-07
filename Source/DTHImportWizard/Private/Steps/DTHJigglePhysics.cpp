#include "Steps/DTHJigglePhysics.h"

#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_PoseDriver.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SpringBone.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/PoseAsset.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Steps/DTHImportReport.h"

#define LOCTEXT_NAMESPACE "DTHJigglePhysics"

namespace
{
	// DTH 1.2 jiggle video: the values the developer settled on [46:27]. Max Displacement matches the 5 cm offset distance.
	constexpr double MaxDisplacement = 5.0;
	constexpr double DefaultStiffness = 350.0;
	constexpr double DefaultDamping = 6.0;
	constexpr double ErrorResetThreshold = 256.0;
	constexpr double Diagonal = 3.5; // [23:46]: 3.5 on both axes puts a diagonal target on a circle of 5.
	// Tuned from the exported 5 ([45:40]: Radius is the first setting to adjust). Measured on Genesis 9: at 5, PHY morphs sit at 0.10 at rest and
	// peak at 0.33; at 2, 0.007 at rest and 0.94 at peak, so the character is not deformed at rest.
	constexpr float PoseDriverRadius = 2.f;

	const TCHAR* const PhysicsBones[] = { TEXT("breast_l"), TEXT("breast_r"), TEXT("glute_l"), TEXT("glute_r"), TEXT("stomach") };

	/**
	 * A pose's offset as the Genesis 9 physics preset gives it on the left side: Houdini world axes, Y up, X side to side
	 * (video [21:21]). The stomach's Left/Right are the breasts' Out/In.
	 */
	bool PresetOffset(const FString& Direction, FVector& OutOffset)
	{
		static const TMap<FString, FVector> Offsets = {
			{ TEXT("Out"), FVector(-5.0, 0.0, 0.0) },
			{ TEXT("OutUp"), FVector(-Diagonal, Diagonal, 0.0) },
			{ TEXT("Up"), FVector(0.0, 5.0, 0.0) },
			{ TEXT("InUp"), FVector(Diagonal, Diagonal, 0.0) },
			{ TEXT("In"), FVector(5.0, 0.0, 0.0) },
			{ TEXT("InDown"), FVector(Diagonal, -Diagonal, 0.0) },
			{ TEXT("Down"), FVector(0.0, -5.0, 0.0) },
			{ TEXT("OutDown"), FVector(-Diagonal, -Diagonal, 0.0) },
			{ TEXT("HangForward"), FVector(0.0, 0.0, -5.0) },
			{ TEXT("Left"), FVector(-5.0, 0.0, 0.0) },
			{ TEXT("LeftUp"), FVector(-Diagonal, Diagonal, 0.0) },
			{ TEXT("RightUp"), FVector(Diagonal, Diagonal, 0.0) },
			{ TEXT("Right"), FVector(5.0, 0.0, 0.0) },
			{ TEXT("RightDown"), FVector(Diagonal, -Diagonal, 0.0) },
			{ TEXT("LeftDown"), FVector(-Diagonal, -Diagonal, 0.0) },
		};
		const FVector* Found = Offsets.Find(Direction);
		if (Found)
		{
			OutOffset = *Found;
		}
		return Found != nullptr;
	}

	/** "pasPhyBreastOutUp_l" -> "OutUp". */
	FString PoseDirection(const FString& PoseName)
	{
		FString Name = PoseName;
		int32 Underscore = INDEX_NONE;
		if (Name.FindLastChar(TEXT('_'), Underscore))
		{
			Name.LeftInline(Underscore);
		}
		for (const TCHAR* Region : { TEXT("Breast"), TEXT("Glute"), TEXT("Stomach") })
		{
			const int32 Found = Name.Find(Region);
			if (Found != INDEX_NONE)
			{
				return Name.Mid(Found + FCString::Strlen(Region));
			}
		}
		return FString();
	}

	FQuat ReferenceRotation(const FReferenceSkeleton& Skeleton, int32 Bone)
	{
		FQuat Rotation = FQuat::Identity;
		for (; Bone != INDEX_NONE; Bone = Skeleton.GetParentIndex(Bone))
		{
			Rotation = Skeleton.GetRefBonePose()[Bone].GetRotation() * Rotation;
		}
		return Rotation;
	}

	UEdGraphPin* PosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && (Pin->PinType.PinSubCategoryObject == FPoseLink::StaticStruct()
				|| Pin->PinType.PinSubCategoryObject == FComponentSpacePoseLink::StaticStruct()))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	template <typename NodeType>
	NodeType* AddNode(UEdGraph* Graph, int32 X, int32 Y)
	{
		FGraphNodeCreator<NodeType> Creator(*Graph);
		NodeType* Node = Creator.CreateNode();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	}

	/** Stiffness and damping as blueprint variables, one pair per region, as the video binds them [46:27]. */
	FName RegionVariable(const FString& Bone, const TCHAR* Setting)
	{
		FString Region = Bone;
		Region.RemoveFromEnd(TEXT("_l"));
		Region.RemoveFromEnd(TEXT("_r"));
		Region[0] = FChar::ToUpper(Region[0]);
		return FName(*(Region + Setting));
	}

	void BindToVariable(UAnimBlueprint* Blueprint, UEdGraph* Graph, UAnimGraphNode_SpringBone* Spring, FName PinName, FName Variable, double Default)
	{
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Variable) == INDEX_NONE)
		{
			FEdGraphPinType Type;
			Type.PinCategory = UEdGraphSchema_K2::PC_Real;
			Type.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			FBlueprintEditorUtils::AddMemberVariable(Blueprint, Variable, Type, LexToString(Default));
		}
		UK2Node_VariableGet* Get = AddNode<UK2Node_VariableGet>(Graph, Spring->NodePosX - 240, Spring->NodePosY + (PinName == TEXT("SpringStiffness") ? 0 : 80));
		Get->VariableReference.SetSelfMember(Variable);
		Get->ReconstructNode();
		UEdGraphPin* Out = Get->FindPin(Variable, EGPD_Output);
		UEdGraphPin* In = Spring->FindPin(PinName, EGPD_Input);
		if (Out && In)
		{
			Graph->GetSchema()->TryCreateConnection(Out, In);
		}
	}

	bool Fail(const FDTHExport& Export, const UAnimBlueprint* PostProcess, const FString& Why, FDTHImportReport& Report)
	{
		Report.Warning(FText::Format(LOCTEXT("JiggleFailed", "Jiggle physics could not be added to {0} ({1}); it is a manual step."),
			FText::FromString(GetNameSafe(PostProcess)), FText::FromString(Why)));
		FDTHManualStep& Step = Report.ManualSteps.AddDefaulted_GetRef();
		Step.Title = LOCTEXT("JiggleManualTitle", "Add jiggle physics to the post-process blueprint");
		Step.Instructions.Add(FText::Format(LOCTEXT("JiggleManual", "Follow Part 5 of the DTH jiggle video in {0}: Spring Controllers on the physics bones, then the Pose Drivers from {1}."),
			FText::FromString(GetNameSafe(PostProcess)), FText::FromString(Export.Folder / TEXT("Pose Drivers/Phys"))));
		return false;
	}
}

bool DTHImport::JigglePoseTarget(const USkeletalMesh* Body, FName Bone, const FString& PoseName, const FVector& RestTranslation, FVector& OutTarget)
{
	FVector Offset = FVector::ZeroVector;
	if (!Body || !PresetOffset(PoseDirection(PoseName), Offset))
	{
		return false;
	}
	const FReferenceSkeleton& Skeleton = Body->GetRefSkeleton();
	const int32 BoneIndex = Skeleton.FindBoneIndex(Bone);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}
	// Right-side groups are the left's, mirrored on X (the video's Mirror Down, [27:05]).
	if (Bone.ToString().EndsWith(TEXT("_r")))
	{
		Offset.X = -Offset.X;
	}
	// Houdini (X side, Y up, Z forward) in Unreal's component space (X side, Y forward, Z up).
	const FVector ComponentOffset(Offset.X, Offset.Z, Offset.Y);
	const FQuat ParentRotation = ReferenceRotation(Skeleton, Skeleton.GetParentIndex(BoneIndex));
	OutTarget = RestTranslation + ParentRotation.UnrotateVector(ComponentOffset);
	return true;
}

bool DTHImport::AddJigglePhysics(const FDTHExport& Export, const USkeletalMesh* Body, const FString& DestinationFolder, UAnimBlueprint* PostProcess, FDTHImportReport& Report)
{
	if (!PostProcess)
	{
		return Fail(Export, PostProcess, TEXT("no post-process blueprint"), Report);
	}
	const FDTHPoseAssetEntry* PhyEntry = Export.PoseAssets.FindByPredicate([](const FDTHPoseAssetEntry& Entry)
	{
		return Entry.Category.Equals(TEXT("PHY"), ESearchCase::IgnoreCase);
	});
	if (!PhyEntry || Export.PhysicsDriverFiles.IsEmpty())
	{
		return Fail(Export, PostProcess, TEXT("the export has no PHY pose asset or pose driver files"), Report);
	}
	FString PoseFolder = DestinationFolder;
	PoseFolder.RemoveFromEnd(TEXT("/"));
	FString SafeCharacter;
	for (const TCHAR Char : Export.Character)
	{
		SafeCharacter.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
	}
	const FString PoseName = FString::Printf(TEXT("PA_%s%s"), *SafeCharacter, *PhyEntry->Category);
	UPoseAsset* PhyPose = LoadObject<UPoseAsset>(nullptr, *(PoseFolder / TEXT("PoseAssets") / PoseName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!PhyPose)
	{
		return Fail(Export, PostProcess, FString::Printf(TEXT("%s was not built"), *PoseName), Report);
	}

	UEdGraph* Graph = nullptr;
	for (UEdGraph* Candidate : PostProcess->FunctionGraphs)
	{
		if (Candidate && Candidate->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			Graph = Candidate;
			break;
		}
	}
	if (!Graph)
	{
		return Fail(Export, PostProcess, TEXT("no AnimGraph"), Report);
	}
	UAnimGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Cast<UAnimGraphNode_SpringBone>(Node))
		{
			Report.Info(FText::Format(LOCTEXT("JiggleExists", "{0} already has jiggle physics; left as it is."), FText::FromString(PostProcess->GetName())));
			return true;
		}
		Root = Root ? Root : Cast<UAnimGraphNode_Root>(Node);
	}
	UEdGraphPin* Result = Root ? PosePin(Root, EGPD_Input) : nullptr;
	if (!Result)
	{
		return Fail(Export, PostProcess, TEXT("no Output Pose node"), Report);
	}
	const FReferenceSkeleton& Skeleton = Body->GetRefSkeleton();
	for (const TCHAR* Bone : PhysicsBones)
	{
		if (Skeleton.FindBoneIndex(Bone) == INDEX_NONE)
		{
			return Fail(Export, PostProcess, FString::Printf(TEXT("the mesh has no %s bone; tick the physics bones on DazToHueSkeleton and export again"), Bone), Report);
		}
	}

	Graph->Modify();
	PostProcess->Modify();
	const UEdGraphSchema* Schema = Graph->GetSchema();
	UEdGraphPin* Upstream = Result->LinkedTo.Num() > 0 ? Result->LinkedTo[0] : nullptr;

	int32 X = Root->NodePosX;
	const int32 Y = Root->NodePosY;
	TArray<UEdGraphNode*> Chain;

	// Springs work in component space, so they sit between the two converters [40:13].
	Chain.Add(AddNode<UAnimGraphNode_LocalToComponentSpace>(Graph, X, Y));
	X += 280;
	for (const TCHAR* BoneName : PhysicsBones)
	{
		UAnimGraphNode_SpringBone* Spring = AddNode<UAnimGraphNode_SpringBone>(Graph, X, Y);
		Spring->Node.SpringBone.BoneName = BoneName;
		Spring->Node.bLimitDisplacement = true;
		Spring->Node.MaxDisplacement = MaxDisplacement;
		Spring->Node.SpringStiffness = DefaultStiffness;
		Spring->Node.SpringDamping = DefaultDamping;
		Spring->Node.ErrorResetThresh = ErrorResetThreshold;
		Spring->Node.bTranslateX = Spring->Node.bTranslateY = Spring->Node.bTranslateZ = true;
		Spring->Node.bRotateX = Spring->Node.bRotateY = Spring->Node.bRotateZ = false;
		for (FOptionalPinFromProperty& Optional : Spring->ShowPinForProperties)
		{
			if (Optional.PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_SpringBone, SpringStiffness)
				|| Optional.PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_SpringBone, SpringDamping))
			{
				Optional.bShowPin = true;
			}
		}
		Spring->ReconstructNode();
		BindToVariable(PostProcess, Graph, Spring, GET_MEMBER_NAME_CHECKED(FAnimNode_SpringBone, SpringStiffness), RegionVariable(BoneName, TEXT("Stiffness")), DefaultStiffness);
		BindToVariable(PostProcess, Graph, Spring, GET_MEMBER_NAME_CHECKED(FAnimNode_SpringBone, SpringDamping), RegionVariable(BoneName, TEXT("Damping")), DefaultDamping);
		Chain.Add(Spring);
		X += 280;
	}
	Chain.Add(AddNode<UAnimGraphNode_ComponentToLocalSpace>(Graph, X, Y));
	X += 280;

	// Pose drivers read local space, so they come after Component To Local [44:30]. The text is DTH's own, pasted as is.
	int32 TargetsSet = 0;
	for (const FString& File : Export.PhysicsDriverFiles)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File) || !FEdGraphUtilities::CanImportNodesFromText(Graph, Text))
		{
			return Fail(Export, PostProcess, FString::Printf(TEXT("%s is not pose driver text"), *FPaths::GetCleanFilename(File)), Report);
		}
		TSet<UEdGraphNode*> Pasted;
		FEdGraphUtilities::ImportNodesFromText(Graph, Text, Pasted);
		UAnimGraphNode_PoseDriver* Driver = nullptr;
		for (UEdGraphNode* Node : Pasted)
		{
			Driver = Driver ? Driver : Cast<UAnimGraphNode_PoseDriver>(Node);
		}
		if (!Driver || Driver->Node.SourceBones.IsEmpty() || Driver->Node.PoseTargets.IsEmpty())
		{
			return Fail(Export, PostProcess, FString::Printf(TEXT("%s made no usable pose driver"), *FPaths::GetCleanFilename(File)), Report);
		}
		Driver->CreateNewGuid();
		Driver->NodePosX = X;
		Driver->NodePosY = Y;
		Driver->SetAnimationAsset(PhyPose);
		Driver->Node.RBFParams.Radius = PoseDriverRadius;

		const FName Bone = Driver->Node.SourceBones[0].BoneName;
		const FVector Rest = Driver->Node.PoseTargets[0].BoneTransforms[0].TargetTranslation;
		for (FPoseDriverTarget& Target : Driver->Node.PoseTargets)
		{
			if (Target.DrivenName == TEXT("RestPose") || !Target.BoneTransforms[0].TargetTranslation.IsNearlyZero())
			{
				continue;
			}
			if (JigglePoseTarget(Body, Bone, Target.DrivenName.ToString(), Rest, Target.BoneTransforms[0].TargetTranslation))
			{
				++TargetsSet;
			}
			else
			{
				Report.Warning(FText::Format(LOCTEXT("JiggleTargetUnknown", "{0}: {1} isn't a Genesis 9 preset pose, so its target is still empty. Set it on the Pose Driver."),
					FText::FromName(Bone), FText::FromName(Target.DrivenName)));
			}
		}
		Chain.Add(Driver);
		X += 280;
	}

	if (Upstream)
	{
		Schema->BreakSinglePinLink(Upstream, Result);
	}
	UEdGraphPin* Previous = Upstream;
	for (UEdGraphNode* Node : Chain)
	{
		UEdGraphPin* In = PosePin(Node, EGPD_Input);
		if (Previous && (!In || !Schema->TryCreateConnection(Previous, In)))
		{
			return Fail(Export, PostProcess, FString::Printf(TEXT("%s could not be connected"), *Node->GetName()), Report);
		}
		Previous = PosePin(Node, EGPD_Output);
	}
	Root->Modify();
	Root->NodePosX = X;
	if (!Previous || !Schema->TryCreateConnection(Previous, Result))
	{
		return Fail(Export, PostProcess, TEXT("the last pose driver could not be connected to Output Pose"), Report);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(PostProcess);
	FCompilerResultsLog Results;
	Results.SetSourcePath(PostProcess->GetPathName());
	Results.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(PostProcess, EBlueprintCompileOptions::None, &Results);
	PostProcess->MarkPackageDirty();
	if (PostProcess->Status == BS_Error || Results.NumErrors > 0)
	{
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				Report.Error(FText::Format(LOCTEXT("JiggleCompileLine", "{0}: {1}"), FText::FromString(PostProcess->GetName()), Message->ToText()));
			}
		}
		return Fail(Export, PostProcess, TEXT("it did not compile"), Report);
	}
	Report.Info(FText::Format(LOCTEXT("JiggleAdded", "{0}: jiggle physics added, {1} spring controllers and {2} pose drivers on {3} ({4} empty targets set)."),
		FText::FromString(PostProcess->GetName()), FText::AsNumber(UE_ARRAY_COUNT(PhysicsBones)), FText::AsNumber(Export.PhysicsDriverFiles.Num()), FText::FromString(PoseName), FText::AsNumber(TargetsSet)));
	return true;
}

#undef LOCTEXT_NAMESPACE
