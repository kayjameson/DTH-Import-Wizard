#pragma once

#include "CoreMinimal.h"

class USkeleton;
class USkeletalMesh;

namespace DTHImport
{
	/**
	 * Imports one character FBX as a skeletal mesh on Skeleton, through Interchange.
	 * Morph targets on; no animations, materials or physics asset (materials are built by the
	 * wizard's own material step). Returns the mesh, or nullptr with OutError set.
	 */
	USkeletalMesh* ImportSkeletalMesh(const FString& FbxFile, const FString& DestinationFolder, USkeleton* Skeleton, FString& OutError);
}
