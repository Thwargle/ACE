#pragma once

#include "CoreMinimal.h"

/** Runtime World Composition landscape tile streaming for baked Dereth WC maps. */
namespace ACEWcTerrainRuntime
{
	ACECLIENT_API void ReleaseWcStreamingFlagsHold(UWorld* World);
	ACECLIENT_API void SetWcDistanceStreamingEnabled(UWorld* World, bool bEnabled);
	ACECLIENT_API void HideWcLandscapesForLogin(UWorld* World);
	ACECLIENT_API void ShowWcLandscapesAfterLogin(UWorld* World);
	ACECLIENT_API void RequestTerrainAround(UWorld* World, const FVector& WorldLocation, int32 RingTiles = 1);
	ACECLIENT_API void RequestTerrainAroundBlocking(UWorld* World, const FVector& WorldLocation, int32 RingTiles = 1);
	ACECLIENT_API bool IsTerrainReadyAround(UWorld* World, const FVector& WorldLocation, int32 RingTiles = 1);
	ACECLIENT_API void MaintainLandscapePresentation(UWorld* World);
}
