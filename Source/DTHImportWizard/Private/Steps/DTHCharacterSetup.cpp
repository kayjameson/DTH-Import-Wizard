#include "Steps/DTHCharacterSetup.h"

#include "AnimGraphNode_PoseBlendNode.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimCurveMetadata.h"
#include "Animation/AnimSequence.h"
#include "Animation/MeshDeformer.h"
#include "Animation/PoseAsset.h"
#include "Animation/Skeleton.h"
#include "AssetToolsModule.h"
#include "DTHImportLog.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Export/DTHExport.h"
#include "Factories/PoseAssetFactory.h"
#include "HAL/IConsoleManager.h"
#include "InterchangeGenericAnimationPipeline.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Settings/DTHImportSettings.h"
#include "SkeletalMeshEditorSubsystem.h"
#include "Steps/DTHImportReport.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DTHCharacterSetup"

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

	FString JoinPath(FString Folder, const FString& Name)
	{
		Folder.RemoveFromEnd(TEXT("/"));
		return Folder / Name;
	}

	/** DTH's stock post-process blueprint, relative to the DazToHue content folder. Genesis 9 only, as the export reader enforces. */
	FString StockPostProcessBlueprint(const FDTHExport& Export)
	{
		const bool bDQS = Export.UsesDualQuaternionSkinning();
		if (Export.TargetsMannequin())
		{
			return bDQS ? TEXT("Mannequin/Genesis_9/ABP_DTH_DQS_Genesis9_UE5") : TEXT("Mannequin/Genesis_9/ABP_DTH_Genesis9_UE5");
		}
		return bDQS ? TEXT("Native/Genesis_9/ABP_DTH_DQS_Genesis9") : TEXT("Native/Genesis_9/ABP_DTH_Genesis9");
	}

	bool IsCoreCategory(const FString& Category)
	{
		return Category.Equals(TEXT("FAC"), ESearchCase::IgnoreCase) || Category.Equals(TEXT("JCM"), ESearchCase::IgnoreCase)
			|| Category.Equals(TEXT("RET"), ESearchCase::IgnoreCase);
	}

	bool IsStandardSkeleton(const USkeleton* Skeleton)
	{
		const UDTHImportSettings* Settings = GetDefault<UDTHImportSettings>();
		const FString Path = Skeleton->GetPackage()->GetName();
		return Path == Settings->MannequinSkeleton.ToSoftObjectPath().GetLongPackageName()
			|| Path == Settings->DazToHueAsset(TEXT("Native/Genesis_9/SKL_DTH_Genesis9_Architype"));
	}

	/** The material slot a section draws with, minus the "MI_<Character>" prefix DTH gives every slot. */
	FString ShortSlotName(const FDTHExport& Export, const USkeletalMesh* Mesh, int32 MaterialIndex)
	{
		if (!Mesh->GetMaterials().IsValidIndex(MaterialIndex))
		{
			return FString();
		}
		FString Slot = Mesh->GetMaterials()[MaterialIndex].MaterialSlotName.ToString();
		Slot.RemoveFromStart(TEXT("MI_") + Export.Character);
		Slot.RemoveFromStart(TEXT("MI_"));
		return Slot;
	}

	/** Compiles the post-process blueprint copy; errors go to the report and the result is false. */
	bool CompilePostProcess(UAnimBlueprint* Blueprint, FDTHImportReport& Report)
	{
		FCompilerResultsLog Results;
		Results.SetSourcePath(Blueprint->GetPathName());
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);
		Blueprint->MarkPackageDirty();
		if (Blueprint->Status != BS_Error && Results.NumErrors == 0)
		{
			return true;
		}
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				Report.Error(FText::Format(LOCTEXT("AbpCompileLine", "{0}: {1}"), FText::FromString(Blueprint->GetName()), Message->ToText()));
			}
		}
		Report.Error(FText::Format(LOCTEXT("AbpCompileFailed", "{0} did not compile."), FText::FromString(Blueprint->GetName())));
		return false;
	}

	/** The first pin of Node in Direction that carries a local-space pose. */
	UEdGraphPin* PosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && Pin->PinType.PinSubCategoryObject == FPoseLink::StaticStruct())
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/**
	 * A Pose Blend node for Pose (what dragging a pose asset into the AnimGraph creates) inserted between the node
	 * feeding Output Pose and Output Pose, as GP9 guide steps 124-125 do. A node for the same pose asset that is
	 * already there (an earlier import into the same folder) is left as it is.
	 */
	bool AddPoseAssetBeforeOutput(UAnimBlueprint* Blueprint, UPoseAsset* Pose, FString& OutError)
	{
		UEdGraph* AnimGraph = nullptr;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
			{
				AnimGraph = Graph;
				break;
			}
		}
		if (!AnimGraph)
		{
			OutError = TEXT("no AnimGraph");
			return false;
		}
		UAnimGraphNode_Root* Root = nullptr;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (UAnimGraphNode_PoseBlendNode* Existing = Cast<UAnimGraphNode_PoseBlendNode>(Node); Existing && Existing->GetAnimationAsset() == Pose)
			{
				return true;
			}
			Root = Root ? Root : Cast<UAnimGraphNode_Root>(Node);
		}
		UEdGraphPin* Result = Root ? PosePin(Root, EGPD_Input) : nullptr;
		if (!Result)
		{
			OutError = TEXT("no Output Pose node");
			return false;
		}
		UEdGraphPin* Upstream = Result->LinkedTo.Num() > 0 ? Result->LinkedTo[0] : nullptr;

		AnimGraph->Modify();
		FGraphNodeCreator<UAnimGraphNode_PoseBlendNode> Creator(*AnimGraph);
		UAnimGraphNode_PoseBlendNode* Blend = Creator.CreateNode();
		Blend->SetAnimationAsset(Pose);
		Blend->NodePosX = Root->NodePosX;
		Blend->NodePosY = Root->NodePosY;
		Creator.Finalize();
		Root->Modify();
		Root->NodePosX += 320;

		UEdGraphPin* Source = PosePin(Blend, EGPD_Input);
		UEdGraphPin* Out = PosePin(Blend, EGPD_Output);
		const UEdGraphSchema* Schema = AnimGraph->GetSchema();
		if (!Source || !Out)
		{
			AnimGraph->RemoveNode(Blend);
			OutError = TEXT("the Pose Blend node has no pose pins");
			return false;
		}
		if (Upstream)
		{
			Schema->BreakSinglePinLink(Upstream, Result);
			if (!Schema->TryCreateConnection(Upstream, Source))
			{
				Schema->TryCreateConnection(Upstream, Result);
				AnimGraph->RemoveNode(Blend);
				OutError = TEXT("the pose feeding Output Pose could not be connected to it");
				return false;
			}
		}
		if (!Schema->TryCreateConnection(Out, Result))
		{
			OutError = TEXT("it could not be connected to Output Pose");
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return true;
	}

	UAnimSequence* ImportPoseAnimation(const FString& File, const FString& Folder, USkeleton* Skeleton, FString& OutError)
	{
		TStrongObjectPtr<UInterchangeGenericAssetsPipeline> Pipeline(NewObject<UInterchangeGenericAssetsPipeline>(
			GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInterchangeGenericAssetsPipeline::StaticClass(), TEXT("DTHPosePipeline")),
			RF_Transient));
		Pipeline->CommonSkeletalMeshesAndAnimationsProperties->Skeleton = Skeleton;
		Pipeline->CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations = true;
		Pipeline->AnimationPipeline->bImportAnimations = true;
		Pipeline->MeshPipeline->bImportSkeletalMeshes = false;
		Pipeline->MeshPipeline->bImportStaticMeshes = false;
		Pipeline->MeshPipeline->bCreatePhysicsAsset = false;
		Pipeline->MaterialPipeline->bImportMaterials = false;

		FImportAssetParameters Parameters;
		Parameters.bIsAutomated = true;
		Parameters.bReplaceExisting = true;
		Parameters.OverridePipelines.Add(FSoftObjectPath(Pipeline.Get()));

		TArray<UObject*> Imported;
		UInterchangeManager::GetInterchangeManager().ImportAsset(Folder, UInterchangeManager::CreateSourceData(File), Parameters, Imported);
		for (UObject* Object : Imported)
		{
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Object))
			{
				return Sequence;
			}
		}
		OutError = FString::Printf(TEXT("%s did not import as an animation."), *FPaths::GetCleanFilename(File));
		return nullptr;
	}
}

UAnimBlueprint* DTHImport::PreparePostProcessBlueprint(const FDTHExport& Export, USkeleton* Skeleton, const FString& DestinationFolder, FDTHImportReport& Report)
{
	const FString StockPath = GetDefault<UDTHImportSettings>()->DazToHueAsset(StockPostProcessBlueprint(Export));
	// Named and placed as the DTH importer does: Blueprints/ABP_<Character>.
	const FString CopyFolder = JoinPath(DestinationFolder, TEXT("Blueprints"));
	const FString CopyName = FString::Printf(TEXT("ABP_%s"), *AssetSafe(Export.Character));
	const FString CopyPath = JoinPath(CopyFolder, CopyName);

	if (UAnimBlueprint* Existing = LoadObject<UAnimBlueprint>(nullptr, *CopyPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Report.Info(FText::Format(LOCTEXT("AbpReused", "Post-process blueprint {0} exists; reused as is."), FText::FromString(CopyPath)));
		return Existing;
	}

	UAnimBlueprint* Stock = LoadObject<UAnimBlueprint>(nullptr, *StockPath, nullptr, LOAD_NoWarn);
	if (!Stock)
	{
		Report.Error(FText::Format(LOCTEXT("AbpMissing", "DTH post-process blueprint not found: {0}"), FText::FromString(StockPath)));
		return nullptr;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UAnimBlueprint* Copy = Cast<UAnimBlueprint>(AssetTools.DuplicateAsset(CopyName, CopyFolder, Stock));
	if (!Copy)
	{
		Report.Error(FText::Format(LOCTEXT("AbpCopyFailed", "Could not copy {0} to {1}."), FText::FromString(StockPath), FText::FromString(CopyPath)));
		return nullptr;
	}
	if (Copy->TargetSkeleton != Skeleton)
	{
		Copy->Modify();
		Copy->TargetSkeleton = Skeleton;
	}
	if (!CompilePostProcess(Copy, Report))
	{
		Report.Error(FText::Format(LOCTEXT("AbpCompileOnSkeleton", "{0} did not compile on {1}."), FText::FromString(CopyName), FText::FromString(Skeleton->GetName())));
		return nullptr;
	}
	Report.Info(FText::Format(LOCTEXT("AbpCopied", "Post-process blueprint {0}: copy of {1} on {2}."),
		FText::FromString(CopyName), FText::FromString(Stock->GetName()), FText::FromString(Skeleton->GetName())));
	return Copy;
}

void DTHImport::SetUpMesh(const FDTHExport& Export, USkeletalMesh* Mesh, UAnimBlueprint* PostProcess, FDTHImportReport& Report)
{
	Mesh->Modify();
	if (PostProcess && PostProcess->GeneratedClass)
	{
		Mesh->SetPostProcessAnimBlueprint(PostProcess->GeneratedClass.Get());
	}

	if (Export.UsesDualQuaternionSkinning())
	{
		const FString DeformerPath = GetDefault<UDTHImportSettings>()->DazToHueAsset(TEXT("Common/Deformers/DG_DTH_DQS"));
		if (UMeshDeformer* Deformer = LoadObject<UMeshDeformer>(nullptr, *DeformerPath, nullptr, LOAD_NoWarn))
		{
			Mesh->SetDefaultMeshDeformer(Deformer);
		}
		else
		{
			Report.Error(FText::Format(LOCTEXT("DeformerMissing", "DQS deformer graph not found: {0}"), FText::FromString(DeformerPath)));
		}
	}
	else if (FSkeletalMeshModel* Model = Mesh->GetImportedModel(); Model && Model->LODModels.Num() > 0)
	{
		// Quick Start guide, "Recomputing Tangents": the face or head section, for linear skinning.
		USkeletalMeshEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();
		const TArray<FSkelMeshSection>& Sections = Model->LODModels[0].Sections;
		for (int32 Index = 0; Index < Sections.Num(); ++Index)
		{
			const FString Slot = ShortSlotName(Export, Mesh, Sections[Index].MaterialIndex);
			if ((Slot.Equals(TEXT("Head"), ESearchCase::IgnoreCase) || Slot.Equals(TEXT("Face"), ESearchCase::IgnoreCase))
				&& Subsystem && Subsystem->SetSectionRecomputeTangent(Mesh, 0, Index, true))
			{
				Report.Info(FText::Format(LOCTEXT("Tangents", "{0}: Recompute Tangents on section {1} ({2})."),
					FText::FromString(Mesh->GetName()), Index, FText::FromString(Slot)));
			}
		}
		const IConsoleVariable* SkinCache = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkinCache.CompileShaders"));
		if (SkinCache && SkinCache->GetInt() == 0)
		{
			Report.Warning(LOCTEXT("SkinCacheOff", "Recompute Tangents needs Project Settings > Support Compute Skin Cache, which is off."));
		}
	}

	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
}

TArray<FName> DTHImport::ReadCurveNames(const FString& File)
{
	TArray<FName> Names;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		return Names;
	}
	static const FString Marker(TEXT("CurveName=\""));
	int32 Start = Text.Find(Marker, ESearchCase::CaseSensitive);
	while (Start != INDEX_NONE)
	{
		const int32 NameStart = Start + Marker.Len();
		const int32 NameEnd = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
		if (NameEnd == INDEX_NONE)
		{
			break;
		}
		if (NameEnd > NameStart)
		{
			Names.AddUnique(FName(*Text.Mid(NameStart, NameEnd - NameStart)));
		}
		Start = Text.Find(Marker, ESearchCase::CaseSensitive, ESearchDir::FromStart, NameEnd);
	}
	return Names;
}

void DTHImport::RegisterCurves(const FDTHExport& Export, USkeletalMesh* Mesh, FDTHImportReport& Report)
{
	UAnimCurveMetaData* MetaData = Mesh->GetAssetUserData<UAnimCurveMetaData>();
	if (!MetaData)
	{
		MetaData = NewObject<UAnimCurveMetaData>(Mesh, NAME_None, RF_Public | RF_Transactional);
		Mesh->AddAssetUserData(MetaData);
	}
	MetaData->Modify();

	int32 Total = 0;
	for (const FDTHCurveFile& File : Export.CurveFiles)
	{
		const TArray<FName> Names = ReadCurveNames(File.File);
		if (Names.IsEmpty())
		{
			Report.Warning(FText::Format(LOCTEXT("NoCurves", "No curves read from {0}."), FText::FromString(File.File)));
		}
		for (const FName Name : Names)
		{
			MetaData->AddCurveMetaData(Name, false);
			MetaData->SetCurveMetaDataMorphTarget(Name, true);
			MetaData->SetCurveMetaDataMaterial(Name, false);
		}
		Total += Names.Num();
	}
	Mesh->MarkPackageDirty();
	Report.Info(FText::Format(LOCTEXT("Curves", "{0}: {1} curves from {2} files registered as morph curves."),
		FText::FromString(Mesh->GetName()), Total, Export.CurveFiles.Num()));
}

void DTHImport::ApplyRetargeting(const FDTHExport& Export, USkeleton* Skeleton, FDTHImportReport& Report)
{
	if (!Export.TargetsMannequin())
	{
		return;
	}
	const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
	auto Set = [&](const TCHAR* Bone, EBoneTranslationRetargetingMode::Type Mode, bool bChildren)
	{
		const int32 Index = Reference.FindBoneIndex(Bone);
		if (Index == INDEX_NONE)
		{
			Report.Warning(FText::Format(LOCTEXT("RetargetBoneMissing", "Retargeting: {0} has no bone {1}."), FText::FromString(Skeleton->GetName()), FText::FromString(Bone)));
			return;
		}
		Skeleton->SetBoneTranslationRetargetingMode(Index, Mode, bChildren);
	};

	Skeleton->Modify();
	Set(TEXT("pelvis"), EBoneTranslationRetargetingMode::AnimationRelative, true);
	Set(TEXT("pelvis"), EBoneTranslationRetargetingMode::AnimationScaled, false);
	Set(TEXT("clavicle_pec_l"), EBoneTranslationRetargetingMode::Skeleton, true);
	Set(TEXT("clavicle_pec_r"), EBoneTranslationRetargetingMode::Skeleton, true);
	Skeleton->MarkPackageDirty();
	Report.Info(FText::Format(LOCTEXT("Retarget", "{0}: translation retargeting set (pelvis and below Animation Relative, pelvis Animation Scaled, pec bones Skeleton)."),
		FText::FromString(Skeleton->GetName())));
}

void DTHImport::BuildPoseAssets(const FDTHExport& Export, USkeleton* Skeleton, const FString& DestinationFolder, UAnimBlueprint* PostProcess, FDTHImportReport& Report)
{
	if (Export.PoseAssets.IsEmpty())
	{
		return;
	}
	TArray<FString> AddedToPostProcess;
	// Every exported category is built, FAC, JCM and RET included, as the DTH importer does.
	const bool bStandard = IsStandardSkeleton(Skeleton);
	const FString Folder = JoinPath(DestinationFolder, TEXT("PoseAssets"));
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	for (const FDTHPoseAssetEntry& Entry : Export.PoseAssets)
	{
		FString Error;
		UAnimSequence* Animation = ImportPoseAnimation(Entry.AnimationFile, Folder, Skeleton, Error);
		if (!Animation)
		{
			Report.Error(FText::FromString(Error));
			continue;
		}

		TArray<FString> PoseNames;
		if (FFileHelper::LoadFileToStringArray(PoseNames, *Entry.NamesFile))
		{
			PoseNames.RemoveAll([](FString& Line) { Line.TrimStartAndEndInline(); return Line.IsEmpty(); });
		}
		const FString Name = FString::Printf(TEXT("PA_%s%s"), *AssetSafe(Export.Character), *AssetSafe(Entry.Category));
		UPoseAsset* PoseAsset = LoadObject<UPoseAsset>(nullptr, *JoinPath(Folder, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (PoseAsset)
		{
			// From an earlier import: rebuilt in place from this export, so anything referencing it keeps working.
			TArray<FName> Names;
			for (const FString& PoseName : PoseNames)
			{
				Names.AddUnique(FName(*PoseName));
			}
			PoseAsset->Modify();
			PoseAsset->CreatePoseFromAnimation(Animation, &Names);
			PoseAsset->SetSkeleton(Skeleton);
		}
		else
		{
			UPoseAssetFactory* Factory = NewObject<UPoseAssetFactory>();
			Factory->SourceAnimation = Animation;
			Factory->PoseNames = PoseNames;
			PoseAsset = Cast<UPoseAsset>(AssetTools.CreateAsset(Name, Folder, UPoseAsset::StaticClass(), Factory));
		}
		if (!PoseAsset)
		{
			Report.Error(FText::Format(LOCTEXT("PoseCreateFailed", "Could not create pose asset {0}."), FText::FromString(Name)));
			continue;
		}
		// The first pose is the rest pose; the others become offsets from it.
		if (!PoseAsset->ConvertSpace(true, 0))
		{
			Report.Warning(FText::Format(LOCTEXT("PoseAdditiveFailed", "{0}: could not make it additive on its rest pose."), FText::FromString(Name)));
		}
		PoseAsset->MarkPackageDirty();
		if (PoseNames.Num() != PoseAsset->GetNumPoses())
		{
			Report.Warning(FText::Format(LOCTEXT("PoseNameCount", "{0} has {1} poses but its names file lists {2}."),
				FText::FromString(Name), PoseAsset->GetNumPoses(), PoseNames.Num()));
		}
		ObjectTools::ForceDeleteObjects({ Animation }, false);
		Report.Info(FText::Format(LOCTEXT("PoseBuilt", "Built {0}: {1} poses, additive on {2}."),
			FText::FromString(Name), PoseAsset->GetNumPoses(), FText::FromString(PoseNames.Num() > 0 ? PoseNames[0] : FString(TEXT("pose 0")))));
		if (bStandard && !IsCoreCategory(Entry.Category))
		{
			// Golden Palace 9 guide steps 123-125: an add-on category (GEN, FBM) is evaluated only once its pose
			// asset sits in the character's post-process blueprint copy, "just before the Output Pose node". DTH's
			// stock blueprint drives JCM and FAC itself; the add-on poses' curves (pasGp9*) reach nothing otherwise.
			FString WireError;
			if (PostProcess && AddPoseAssetBeforeOutput(PostProcess, PoseAsset, WireError))
			{
				AddedToPostProcess.Add(Name);
			}
			else
			{
				Report.Warning(FText::Format(LOCTEXT("PoseWireFailed", "{0} could not be added to the post-process blueprint ({1}); it is a manual step."),
					FText::FromString(Name), FText::FromString(PostProcess ? WireError : FString(TEXT("no post-process blueprint")))));
				FDTHManualStep& Step = Report.ManualSteps.AddDefaulted_GetRef();
				Step.Title = FText::Format(LOCTEXT("PoseManualTitle", "Add {0} to the post-process blueprint"), FText::FromString(Name));
				Step.Instructions.Add(FText::Format(LOCTEXT("PoseManual1", "Open ABP_{0} in {1}."), FText::FromString(AssetSafe(Export.Character)), FText::FromString(JoinPath(DestinationFolder, TEXT("Blueprints")))));
				Step.Instructions.Add(FText::Format(LOCTEXT("PoseManual2", "Drag {0} (in {1}) into the AnimGraph and connect it just before Output Pose."), FText::FromString(Name), FText::FromString(Folder)));
				Step.Instructions.Add(LOCTEXT("PoseManual3", "Compile and save."));
			}
		}
	}

	if (AddedToPostProcess.Num() > 0 && CompilePostProcess(PostProcess, Report))
	{
		Report.Info(FText::Format(LOCTEXT("PoseWired", "{0}: added {1} before Output Pose (GP9 guide 123-125)."),
			FText::FromString(PostProcess->GetName()), FText::FromString(FString::Join(AddedToPostProcess, TEXT(", ")))));
	}
}

#undef LOCTEXT_NAMESPACE
