#pragma once
#include "CoreMinimal.h"
#include "Templates/Function.h"

struct FACEBuiltEnvCellMesh;

namespace ACEAmbientReach
{
    struct FResult
    {
        uint32 OutdoorCell = 0;
        FVector Entrance = FVector::ZeroVector;
        float DistanceAc = 0;
        float Gain = 0;
    };

    /** Retail Ambient::CalcWeight uses a 20m inner radius and 120m maximum. */
    float DistanceGain(float DistanceAc);

    /** Follow connected portals, independently of view direction. Never loads meshes. */
    FResult FindOutdoor(uint32 Cell, const FVector& Listener, float WorldScale,
        TFunctionRef<const FACEBuiltEnvCellMesh*(uint32)> FindResidentCell);
}
