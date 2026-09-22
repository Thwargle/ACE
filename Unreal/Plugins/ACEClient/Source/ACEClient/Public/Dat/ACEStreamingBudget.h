#pragma once
#include "CoreMinimal.h"

namespace ACEStreamingBudget
{
    // Full-resolution uncompressed blends are much larger than retail's old
    // managed textures. Bound native Quest residency, including the unload ring.
    // PC clients retain the retail terrain range. Source resolution is unchanged.
    inline int32 TerrainRadius(int32 Requested, bool NativeQuest = PLATFORM_ANDROID)
    {
        return FMath::Clamp(Requested,0,NativeQuest ? 3 : MAX_int32);
    }

    inline int32 EnvCellWorkers(bool NativeQuest = PLATFORM_ANDROID)
    {
        // Each worker unpacks its own Environment and BSP trees. A long queue
        // should not increase peak memory precisely when streaming is busiest.
        return NativeQuest ? 2 : 4;
    }
}
