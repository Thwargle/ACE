#pragma once

#include "Dat/ACEOutdoorPortalPlan.h"

// Each record starts with a plane count, followed by outward planes (N.P <= W).
// Plane coordinates are relative to the camera, retaining precision in distant landblocks.
struct FACEPortalViewMask
{
    static constexpr int32 TextureWidth = 64;
    FVector Origin = FVector::ZeroVector;
    TArray<FVector4f> Records;
    int32 ViewCount = 0;

    static FACEPortalViewMask Build(const FVector& Eye,
        const TArray<ACEOutdoorPortalPlan::FAdmittedAperture>& Apertures);
    bool Contains(const FVector& WorldPoint) const;
    bool ContainsFromEye(const FVector& WorldPoint, const FVector& ViewEye) const;
};
