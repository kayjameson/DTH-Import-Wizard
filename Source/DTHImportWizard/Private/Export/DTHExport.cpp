#include "Export/DTHExport.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString FindManifestIn(const FString& Folder)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*.dth")), true, false);
		Found.Sort();
		return Found.Num() > 0 ? Folder / Found[0] : FString();
	}

	FString StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}

	const TArray<TSharedPtr<FJsonValue>>& ArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		return Object->TryGetArrayField(Field, Values) ? *Values : Empty;
	}
}

TSharedPtr<FDTHExport> FDTHExport::Load(const FString& InFolder, FString& OutError)
{
	FString Folder = FPaths::ConvertRelativePathToFull(InFolder);
	FPaths::NormalizeDirectoryName(Folder);

	FString Manifest = FindManifestIn(Folder);
	if (Manifest.IsEmpty())
	{
		// Also accept the export directory itself, which holds one folder per character.
		TArray<FString> SubFolders;
		IFileManager::Get().FindFiles(SubFolders, *(Folder / TEXT("*")), false, true);
		SubFolders.Sort();
		for (const FString& Sub : SubFolders)
		{
			Manifest = FindManifestIn(Folder / Sub);
			if (!Manifest.IsEmpty())
			{
				break;
			}
		}
	}
	if (Manifest.IsEmpty())
	{
		OutError = FString::Printf(TEXT("No DazToHue manifest (DTH_<Character>.dth) in %s."), *Folder);
		return nullptr;
	}

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Manifest))
	{
		OutError = FString::Printf(TEXT("Could not read %s."), *Manifest);
		return nullptr;
	}

	TSharedPtr<FDTHExport> Export = MakeShared<FDTHExport>();
	Export->ManifestFile = Manifest;
	Export->Folder = FPaths::GetPath(Manifest);
	if (!Export->Parse(Json, OutError))
	{
		return nullptr;
	}
	Export->AddUnlistedMeshes();
	return Export;
}

bool FDTHExport::Parse(const FString& Json, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("%s is not valid JSON."), *ManifestFile);
		return false;
	}

	Character = StringField(Root, TEXT("character_name"));
	if (Character.IsEmpty())
	{
		Character = FPaths::GetCleanFilename(Folder);
	}
	DthVersion = StringField(Root, TEXT("dth_version"));
	SourceSkeleton = StringField(Root, TEXT("source_skeleton"));
	TargetSkeleton = StringField(Root, TEXT("target_skeleton"));
	SkinningMethod = StringField(Root, TEXT("skinning_method"));

	for (const TSharedPtr<FJsonValue>& Value : ArrayField(Root, TEXT("skeletal_meshes")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (!Object.IsValid())
		{
			continue;
		}
		FDTHMeshEntry& Mesh = Meshes.AddDefaulted_GetRef();
		Mesh.Name = StringField(Object, TEXT("name"));
		Mesh.File = Resolve(StringField(Object, TEXT("file")), TEXT("Skeletal Meshes"));
		Mesh.bInManifest = true;
		for (const TSharedPtr<FJsonValue>& MaterialValue : ArrayField(Object, TEXT("materials")))
		{
			const TSharedPtr<FJsonObject> MaterialObject = MaterialValue->AsObject();
			if (!MaterialObject.IsValid())
			{
				continue;
			}
			FDTHMaterialSlot& Slot = Mesh.Materials.AddDefaulted_GetRef();
			Slot.Slot = StringField(MaterialObject, TEXT("slot"));
			Slot.Type = StringField(MaterialObject, TEXT("type"));
			const TSharedPtr<FJsonObject>* TextureObject = nullptr;
			if (MaterialObject->TryGetObjectField(TEXT("textures"), TextureObject))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Channel : (*TextureObject)->Values)
				{
					const FString TexturePath = Channel.Value.IsValid() ? Channel.Value->AsString() : FString();
					if (!TexturePath.IsEmpty())
					{
						Slot.Textures.Add(Channel.Key, TexturePath);
					}
				}
			}
		}
	}

	for (const TSharedPtr<FJsonValue>& Value : ArrayField(Root, TEXT("pose_assets")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid() && !StringField(Object, TEXT("file")).IsEmpty())
		{
			FDTHPoseAssetEntry& Entry = PoseAssets.AddDefaulted_GetRef();
			Entry.Category = StringField(Object, TEXT("type"));
			Entry.AnimationFile = Resolve(StringField(Object, TEXT("file")), TEXT("Pose Assets"));
			Entry.NamesFile = Resolve(StringField(Object, TEXT("names")), TEXT("Pose Assets"));
		}
	}

	for (const TSharedPtr<FJsonValue>& Value : ArrayField(Root, TEXT("animation_curves")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid() && !StringField(Object, TEXT("file")).IsEmpty())
		{
			FDTHCurveFile& Entry = CurveFiles.AddDefaulted_GetRef();
			Entry.Category = StringField(Object, TEXT("type"));
			Entry.File = Resolve(StringField(Object, TEXT("file")), TEXT("Animation Curves"));
		}
	}
	return true;
}

FString FDTHExport::Resolve(const FString& ManifestPath, const TCHAR* SubFolder) const
{
	if (ManifestPath.IsEmpty())
	{
		return FString();
	}
	const FString Local = Folder / SubFolder / FPaths::GetCleanFilename(ManifestPath);
	if (FPaths::FileExists(Local) || !FPaths::FileExists(ManifestPath))
	{
		return Local;
	}
	return FPaths::ConvertRelativePathToFull(ManifestPath);
}

void FDTHExport::AddUnlistedMeshes()
{
	// FBX files in the mesh folder that the manifest doesn't list (e.g. a merged outfit) are offered too, unticked.
	TArray<FString> Files;
	const FString MeshFolder = Folder / TEXT("Skeletal Meshes");
	IFileManager::Get().FindFiles(Files, *(MeshFolder / TEXT("*.fbx")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		const FString Full = MeshFolder / File;
		const bool bListed = Meshes.ContainsByPredicate([&Full](const FDTHMeshEntry& Mesh)
		{
			return FPaths::IsSamePath(Mesh.File, Full);
		});
		if (!bListed)
		{
			FDTHMeshEntry& Mesh = Meshes.AddDefaulted_GetRef();
			Mesh.File = Full;
		}
	}
}

bool FDTHExport::TargetsMannequin() const
{
	return TargetSkeleton.Contains(TEXT("UE5")) || TargetSkeleton.Contains(TEXT("Mannequin"));
}

bool FDTHExport::UsesDualQuaternionSkinning() const
{
	return SkinningMethod.StartsWith(TEXT("DQ"));
}

bool FDTHExport::IsGenesis9() const
{
	return SourceSkeleton.Contains(TEXT("9"));
}
