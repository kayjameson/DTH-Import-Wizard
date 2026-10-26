#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

/** Whether an export's body is the character's body, so its garments and grooms fit the character. */
struct FDTHBodyMatch
{
	bool bMatches = false;
	/** What differs (empty when they match). */
	FString Reason;
	/** What was compared, for the report. */
	FString Compared;
};

namespace DTHImport
{
	/**
	 * Compares ExportBodyFbx with the FBX CharacterBody was imported from: vertex count, rest-pose bounds and the
	 * bones the mesh is skinned to. When that file is gone, the vertex count and bones are compared with the imported
	 * body instead. An add-on export whose body differs (another character, another subdivision level, or body
	 * polygons removed by the occlusion node) would put garments and grooms on a body they weren't made for.
	 */
	FDTHBodyMatch CompareBodies(const FString& ExportBodyFbx, const USkeletalMesh* CharacterBody);
}
