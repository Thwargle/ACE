#pragma once

#include "ACEWorldEntityActor.h"
#include "ACEEnvCellActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "ProceduralMeshComponent.h"

namespace ACEVisibleObjectPick
{
    // Render::GfxObjUnderSelectionRay / GetMouseSelectionObjectID: a polygon
    // hit takes precedence over broad selection bounds, including nearer bounds.
    inline bool TraceMesh(UProceduralMeshComponent& Mesh, const FVector& Start,
        const FVector& End, double& ClosestDistance, bool* HasGeometry = nullptr)
    {
        if (!Mesh.IsVisible() || Mesh.bHiddenInGame || !Mesh.IsRegistered()) return false;
        const FTransform Transform = Mesh.GetComponentTransform();
        const FVector A = Transform.InverseTransformPosition(Start);
        const FVector B = Transform.InverseTransformPosition(End);
        bool bHit = false;
        for (int32 S = 0; S < Mesh.GetNumSections(); ++S)
        {
            const FProcMeshSection* Section = Mesh.GetProcMeshSection(S);
            if (!Section || !Section->bSectionVisible || Section->ProcIndexBuffer.Num()<3) continue;
            if (HasGeometry) *HasGeometry = true;
            if (!FMath::LineBoxIntersection(Section->SectionLocalBox, A, B, B-A)) continue;
            const auto& V = Section->ProcVertexBuffer;
            const auto& I = Section->ProcIndexBuffer;
            for (int32 T = 0; T+2 < I.Num(); T += 3)
            {
                if (!V.IsValidIndex(I[T]) || !V.IsValidIndex(I[T+1]) || !V.IsValidIndex(I[T+2])) continue;
                FVector Point, Normal;
                if (FMath::SegmentTriangleIntersection(A, B, V[I[T]].Position,
                    V[I[T+1]].Position, V[I[T+2]].Position, Point, Normal))
                {
                    const double Distance = FVector::Distance(Start, Transform.TransformPosition(Point));
                    if (Distance < ClosestDistance) { ClosestDistance = Distance; bHit = true; }
                }
            }
        }
        return bHit;
    }

    inline AActor* Trace(UWorld& World, const FVector& Start, const FVector& End, APawn* Self,
        FVector* Impact = nullptr, bool IncludeSelf = true)
    {
        QUICK_SCOPE_CYCLE_COUNTER(STAT_ACEVisibleObjectPick);
        TArray<AActor*, TInlineAllocator<128>> Candidates;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(ACEVisibleObjectPick), true);
        TArray<AActor*> Held;
        if (Self)
        {
            Params.AddIgnoredActor(Self);
            if (IncludeSelf) Candidates.Add(Self);
            else { Self->GetAttachedActors(Held,true,true); Params.AddIgnoredActors(Held); }
        }
        // Query the scene's spatial index instead of walking every entity and
        // every component in a dense landblock for each pointer frame.
        TArray<FHitResult> Broad;
        World.LineTraceMultiByObjectType(Broad, Start, End, FCollisionObjectQueryParams::AllObjects, Params);
        TSet<AActor*> Seen;
        for (const auto& Hit : Broad)
        {
            auto* Entity = Cast<AACEWorldEntityActor>(Hit.GetActor());
            if (!Entity || Seen.Contains(Entity)) continue;
            Seen.Add(Entity); Params.AddIgnoredActor(Entity);
            if (!Entity->IsHidden() && Entity->IsCellVisible() && Entity->GetACEGuid() != 0
                && (Entity->PhysicsState & (ACEPhysicsState::Missile | ACEPhysicsState::ParticleEmitter | ACEPhysicsState::NoDraw)) == 0
                && (!Entity->bReceivedDeathMotion || Entity->IsCorpse())) Candidates.Add(Entity);
        }
        FHitResult Wall;
        // Terrain, building shells and EnvCell walls deliberately ignore the
        // object-picking channel. Camera collision includes their true surfaces
        // without the movement-only doorway/ceiling helper slabs.
        const bool bOccluded = World.LineTraceSingleByChannel(Wall, Start, End, ECC_Camera, Params);
        double Closest = bOccluded ? Wall.Distance : FVector::Distance(Start, End);
        AActor* Selected = nullptr;
        TArray<AActor*, TInlineAllocator<16>> Fallback;
        for (AActor* Actor : Candidates)
        {
            if (Actor->IsHidden()) continue;
            bool HasGeometry = false;
            TInlineComponentArray<UProceduralMeshComponent*> Meshes(Actor);
            for (UProceduralMeshComponent* Mesh : Meshes)
                if (TraceMesh(*Mesh, Start, End, Closest, &HasGeometry)) Selected = Actor;
            if (!HasGeometry) Fallback.Add(Actor);
        }
        // Effect-only portals and still-loading models need a fallback. A
        // rendered statue/sign/corpse must never select empty space around it.
        if (!Selected) for (AActor* Actor : Fallback)
        {
            if (Actor->IsHidden()) continue;
            FHitResult Hit;
            if (Actor->ActorLineTraceSingle(Hit, Start, End, ECC_Visibility, FCollisionQueryParams())
                && Hit.Distance < Closest)
            { Closest = Hit.Distance; Selected = Actor; }
        }
        if (Selected)
        {
            // Visible rooms beyond the current movement neighborhood may not
            // have cooked/active PhysicsBSP. Their drawn walls still occlude a
            // click. Test only intersecting room bounds after finding a target;
            // do not enable distant movement collision or cook extra meshes.
            const FVector TargetPoint=Start+(End-Start).GetSafeNormal()*Closest;
            for (TActorIterator<AACEEnvCellActor> It(&World);It;++It)
            {
                auto* Mesh=It->CellMesh.Get();
                if (It->IsHidden() || !Mesh || !FMath::LineBoxIntersection(Mesh->Bounds.GetBox(),Start,TargetPoint,TargetPoint-Start)) continue;
                if (TraceMesh(*Mesh,Start,TargetPoint,Closest)) Selected=nullptr;
            }
        }
        if (Impact) *Impact = Start + (End-Start).GetSafeNormal() * Closest;
        return Selected;
    }
}
