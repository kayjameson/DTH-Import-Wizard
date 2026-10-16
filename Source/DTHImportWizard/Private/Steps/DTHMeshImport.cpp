#include "Steps/DTHMeshImport.h"

#include "Animation/Skeleton.h"
#include "DTHImportLog.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeGenericAnimationPipeline.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	/**
	 * The pipeline Interchange runs instead of the project's defaults. It lives only for one import. Its settings
	 * are the ones the DTH importer's skeletal mesh pipeline changes from the Interchange defaults; everything
	 * else (morph targets on, skeletal meshes on, and so on) stays at the default, as there.
	 */
	UInterchangeGenericAssetsPipeline* MakeCharacterPipeline(USkeleton* Skeleton)
	{
		UInterchangeGenericAssetsPipeline* Pipeline = NewObject<UInterchangeGenericAssetsPipeline>(
			GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInterchangeGenericAssetsPipeline::StaticClass(), TEXT("DTHCharacterPipeline")),
			RF_Transient);

		Pipeline->CommonSkeletalMeshesAndAnimationsProperties->Skeleton = Skeleton;
		Pipeline->MeshPipeline->bCreatePhysicsAsset = false;
		Pipeline->AnimationPipeline->bImportAnimations = false;
		Pipeline->MaterialPipeline->TexturePipeline->bImportTextures = false;

		// Slots look for existing material instances named after them under the destination folder (where the
		// material step creates them) and never get new materials of their own.
		Pipeline->MaterialPipeline->bCreateNewMaterials = false;
		Pipeline->MaterialPipeline->SearchLocation = EInterchangeMaterialSearchLocation::UnderParent;
		return Pipeline;
	}
}

USkeletalMesh* DTHImport::ImportSkeletalMesh(const FString& FbxFile, const FString& DestinationFolder, USkeleton* Skeleton, FString& OutError)
{
	if (!FPaths::FileExists(FbxFile))
	{
		OutError = FString::Printf(TEXT("File not found: %s"), *FbxFile);
		return nullptr;
	}
	if (!Skeleton)
	{
		OutError = TEXT("No skeleton to import onto.");
		return nullptr;
	}

	TStrongObjectPtr<UInterchangeGenericAssetsPipeline> Pipeline(MakeCharacterPipeline(Skeleton));

	FImportAssetParameters Parameters;
	Parameters.bIsAutomated = true;
	Parameters.bReplaceExisting = true;
	Parameters.OverridePipelines.Add(FSoftObjectPath(Pipeline.Get()));

	UInterchangeSourceData* Source = UInterchangeManager::CreateSourceData(FbxFile);
	TArray<UObject*> Imported;
	UInterchangeManager::GetInterchangeManager().ImportAsset(DestinationFolder, Source, Parameters, Imported);

	USkeletalMesh* Mesh = nullptr;
	for (UObject* Object : Imported)
	{
		if (USkeletalMesh* Candidate = Cast<USkeletalMesh>(Object))
		{
			Mesh = Candidate;
			break;
		}
	}
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("%s produced no skeletal mesh (see the Output Log, category LogInterchange*)."), *FPaths::GetCleanFilename(FbxFile));
		return nullptr;
	}
	if (Mesh->GetSkeleton() != Skeleton)
	{
		OutError = FString::Printf(TEXT("%s ended up on skeleton %s instead of %s."), *Mesh->GetName(),
			Mesh->GetSkeleton() ? *Mesh->GetSkeleton()->GetPathName() : TEXT("(none)"), *Skeleton->GetPathName());
		return nullptr;
	}
	UE_LOG(LogDTHImport, Log, TEXT("Imported %s: %d material slots, %d morph targets."), *Mesh->GetPathName(),
		Mesh->GetMaterials().Num(), Mesh->GetMorphTargets().Num());
	return Mesh;
}
