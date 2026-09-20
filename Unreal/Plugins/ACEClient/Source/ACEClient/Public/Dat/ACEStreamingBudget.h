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
}
