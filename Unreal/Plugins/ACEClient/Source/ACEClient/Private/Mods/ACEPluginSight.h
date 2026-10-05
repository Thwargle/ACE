#pragma once

#include "ACEVisibleObjectPick.h"

// One context per decision, shared by the bounded monster/corpse lists. Use
// physical scenery, not selection capsules: doors intentionally ignore Camera.
class FACEPluginSightQuery
{
public:
    FACEPluginSightQuery(UWorld& InWorld, APawn* Self)
        : World(InWorld), Params(SCENE_QUERY_STAT(ACEPluginSight), true)
    {
        if (Self) Params.AddIgnoredActor(Self);
        for (TActorIterator<AACEWorldEntityActor> It(&World); It; ++It)
            if ((It->ItemType & ACEItemType::Creature) || It->IsCorpse() || It->IsAttachedToParent())
                Params.AddIgnoredActor(*It);
        for (TActorIterator<AACEEnvCellActor> It(&World); It; ++It)
            if (It->CellMesh) Rooms.Add(It->CellMesh);
    }

    bool Clear(const FVector& Start, const FVector& End) const
    {
        if (World.LineTraceTestByChannel(Start, End, ECC_Pawn, Params)) return false;
        // Loaded cells can have geometry before movement PhysicsBSP is active.
        // Camera/portal culling must not make their walls transparent to combat.
        double Distance = FVector::Distance(Start, End);
        for (auto* Mesh : Rooms)
            if (FMath::LineBoxIntersection(Mesh->Bounds.GetBox(), Start, End, End-Start)
                && ACEVisibleObjectPick::TraceMesh(*Mesh, Start, End, Distance, nullptr, true, true)) return false;
        return true;
    }
private:
    UWorld& World;
    FCollisionQueryParams Params;
    TArray<UProceduralMeshComponent*, TInlineAllocator<64>> Rooms;
};
