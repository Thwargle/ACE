#include "Dat/ACEAmbientReach.h"
#include "Dat/ACEEnvCellMeshBuilder.h"

namespace ACEAmbientReach
{
    float DistanceGain(float DistanceAc)
    {
        if (DistanceAc >= 120.f) return 0.f;
        return DistanceAc <= 20.f ? 1.f : 400.f / FMath::Square(DistanceAc);
    }

    FResult FindOutdoor(uint32 Cell, const FVector& Listener, float WorldScale,
        TFunctionRef<const FACEBuiltEnvCellMesh*(uint32)> FindResidentCell)
    {
        FResult Result;
        if (!Cell || WorldScale <= KINDA_SMALL_NUMBER) return Result;
        if ((Cell & 0xFFFF) < 0x100)
        {
            Result.OutdoorCell=Cell; Result.Entrance=Listener; Result.Gain=1;
            return Result;
        }
        struct FVisit { uint32 Cell; uint16 Entry; FVector Point; float Distance; };
        TArray<FVisit, TInlineAllocator<64>> Pending;
        TMap<uint64, float, TInlineSetAllocator<128>> Best;
        Pending.Add({Cell,0xFFFF,Listener,0});
        float Shortest=120.f*WorldScale;
        // Bounded work over resident collision/portal metadata, never a mesh build
        // or the camera's PView list (sound must survive turning away from a door).
        for (int32 Count=0; !Pending.IsEmpty() && Count<128; ++Count)
        {
            int32 Nearest=0;
            for (int32 I=1; I<Pending.Num(); ++I)
                if (Pending[I].Distance<Pending[Nearest].Distance) Nearest=I;
            const FVisit Visit=Pending[Nearest]; Pending.RemoveAtSwap(Nearest);
            if (Visit.Distance>=Shortest) continue;
            const auto* Mesh=FindResidentCell(Visit.Cell);
            if (!Mesh) continue;
            const FVector Origin=FACEPosition::AceVectorToUnreal(
                FVector((Visit.Cell>>24)*192.f,((Visit.Cell>>16)&255)*192.f,0),WorldScale);
            const FTransform Transform=Mesh->GetCellLocalToLandblock(WorldScale)*FTransform(Origin);
            const FVector Local=Transform.InverseTransformPosition(Visit.Point);
            for (int32 I=0; I<Mesh->CellPortals.Num(); ++I)
            {
                if (I==Visit.Entry || !Mesh->PortalApertureLocalVerts.IsValidIndex(I)) continue;
                const auto& Verts=Mesh->PortalApertureLocalVerts[I];
                if (Verts.Num()<3) continue;
                FVector Closest=Verts[0]; double MinSquared=MAX_dbl;
                for (int32 J=1; J+1<Verts.Num(); ++J)
                {
                    const FVector P=FMath::ClosestPointOnTriangleToPoint(Local,Verts[0],Verts[J],Verts[J+1]);
                    const double D=FVector::DistSquared(Local,P);
                    if (D<MinSquared) { MinSquared=D; Closest=P; }
                }
                const float Distance=Visit.Distance+FMath::Sqrt(MinSquared);
                if (Distance>=Shortest) continue;
                const FVector Point=Transform.TransformPosition(Closest);
                const auto& Portal=Mesh->CellPortals[I];
                if (Portal.IsOutsidePortal())
                {
                    Shortest=Distance;
                    FACEPosition Outside; Outside.CellId=(Visit.Cell&0xFFFF0000)|1;
                    Outside.SetLocationFromUnreal(Point,WorldScale);
                    Result.OutdoorCell=Outside.CellId; Result.Entrance=Point;
                    Result.DistanceAc=Distance/WorldScale; Result.Gain=DistanceGain(Result.DistanceAc);
                }
                else if (Portal.OtherCellId>=0x100 && Pending.Num()<128)
                {
                    const uint32 Next=(Visit.Cell&0xFFFF0000)|Portal.OtherCellId;
                    const uint64 Key=(uint64(Next)<<16)|Portal.OtherPortalId;
                    const float* Previous=Best.Find(Key);
                    if (Previous && *Previous<=Distance) continue;
                    Best.Add(Key,Distance);
                    Pending.Add({Next,Portal.OtherPortalId,Point,Distance});
                }
            }
        }
        return Result;
    }
}
