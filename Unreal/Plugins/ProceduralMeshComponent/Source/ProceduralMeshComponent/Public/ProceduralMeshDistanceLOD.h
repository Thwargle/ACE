#pragma once

#include "CoreMinimal.h"

/** Authored distance interval and the draw sections belonging to one detail level.
 * Empty ranges are intentional (the final retail level is an invisible sentinel). */
struct FProcMeshDistanceLevel
{
    float MinDistance = 0;
    float IdealDistance = 0;
    float MaxDistance = 0;
    uint32 DrawMode = 1;
    int32 FirstSection = 0;
    int32 SectionCount = 0;
};

namespace ProceduralMeshDistanceLOD
{
    // Retail Render::s_rDegradeDistance (headers/global_vars.h) starts at 50 AC
    // meters. GfxObjDegradeInfo::get_degrade subtracts it AFTER normalizing by
    // object/part scale. A zero default swaps clothing at its raw 4-8m thresholds.
    inline constexpr float DefaultDistanceOffset = 50.f;

    // GfxObjDegradeInfo::get_degrade: equality advances to the next level.
    inline int32 Select(TConstArrayView<FProcMeshDistanceLevel> Levels, float Distance,
        bool bForceHighest = false, float Bias = 0, float DistanceOffset = DefaultDistanceOffset)
    {
        if (Levels.IsEmpty()) return INDEX_NONE;
        if (bForceHighest) return 0;
        Distance = FMath::Max(0.f, FMath::Abs(Distance) - DistanceOffset);
        for (int32 I = 0; I < Levels.Num(); ++I)
        {
            const auto& L = Levels[I];
            const float Threshold = L.IdealDistance + Bias * (Bias >= 0
                ? L.MaxDistance - L.IdealDistance : L.IdealDistance - L.MinDistance);
            if (Distance < Threshold) return I;
        }
        return Levels.Num() - 1;
    }

    inline float Distance(const FTransform& Transform, const FVector& SortCenter,
        const FVector& Viewer, float UnitsPerMeter)
    {
        // CPhysicsPart::UpdateViewerDistance uses the close model's sort center
        // and divides by the part's Z scale, including the object's scale.
        return FVector::Distance(Transform.TransformPosition(SortCenter), Viewer)
            / FMath::Max(UE_SMALL_NUMBER, UnitsPerMeter * FMath::Abs(float(Transform.GetScale3D().Z)));
    }

    inline bool ContainsSection(const FProcMeshDistanceLevel& Level, int32 MeshSectionIndex)
    {
        return MeshSectionIndex >= Level.FirstSection && MeshSectionIndex < Level.FirstSection + Level.SectionCount;
    }
}
