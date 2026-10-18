#include "ACEBodySweep.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/TriangleMeshImplicitObject.h"

TOptional<bool> ACEBodySweep::HasMeshWalkableFootprint(const FHitResult& Hit, const FVector& Feet,
    float Radius, float MaxDown, float MaxUp, TOptional<float>* FlatSupportZ)
{
    auto* Component=Hit.GetComponent();
    // CLandCell/OBJECTINFO::validate_walkable uses the terrain low point and
    // water depth. It does not use BSP polygon edge support.
    if(!Component || Component->ComponentHasTag(TEXT("ACEOutdoorTerrain")))return {};
    auto* Box=Cast<UBoxComponent>(Component);
    auto* Setup=Component->GetBodySetup();
    if(!Box && (!Setup || Setup->TriMeshGeometries.IsEmpty()))return {};
    FTransform Transform=Component->GetComponentTransform();
    if(auto* Instances=Cast<UInstancedStaticMeshComponent>(Component))
        if(!Instances->GetInstanceTransform(Hit.Item,Transform,true))return {};

    constexpr double FloorZ=.66417414618662751, Epsilon=.02;
    // A vertical contact inside a walkable face already proves the projection.
    // Ordinary level-floor travel does not need an additional BVH query.
    if(!Hit.bStartPenetrating && Hit.ImpactNormal.Z>=FloorZ
        && FVector::DistSquared2D(Hit.ImpactPoint,Feet)<=Epsilon*Epsilon)
    {
        if(FlatSupportZ && Hit.ImpactNormal.Z>1.-UE_KINDA_SMALL_NUMBER)*FlatSupportZ=Hit.ImpactPoint.Z;
        return true;
    }
    bool Found=false;
    auto Triangle=[&](FVector A,FVector B,FVector C,bool DoubleSided)
    {
        if(Found)return;
        // Keep arithmetic near the feet: distant landblocks must not change
        // the 24 cm footprint of the human 48 cm lower sphere.
        A=Transform.TransformPosition(A)-Feet;
        B=Transform.TransformPosition(B)-Feet;
        C=Transform.TransformPosition(C)-Feet;
        FVector Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        if(Transform.GetDeterminant()<0)Normal=-Normal;
        if(DoubleSided && Normal.Z<0)Normal=-Normal;
        if(Normal.Z<FloorZ)return;
        const double PlaneZ=FVector::DotProduct(Normal,A)/Normal.Z;
        // Only consider planes reachable by this support query. Collision
        // clearance and stepping retain the full lower sphere independently.
        const double FootZ=PlaneZ+Radius/Normal.Z-Radius;
        if(FootZ>MaxUp+.5 || FootZ < -MaxDown-Epsilon)return;
        // SPHEREPATH::check_walkables and CPolygon::check_small_walkable
        // project along world up and use half the lower sphere's radius.
        const FVector Projected(0,0,PlaneZ);
        const FVector Closest=FMath::ClosestPointOnTriangleToPoint(Projected,A,B,C);
        if(FVector::DistSquared(Projected,Closest)>FMath::Square(Radius*.5+Epsilon))return;
        // CPolygon::adjust_sphere_to_plane retains the authored floor plane
        // at a flat edge; Chaos's rounded rim contact must not lower the feet
        // each frame and progressively admit the balcony underneath.
        if(FlatSupportZ && Normal.Z>1.-UE_KINDA_SMALL_NUMBER)*FlatSupportZ=Feet.Z+PlaneZ;
        Found=true;
    };
    if(Box)
    {
        const FVector E=Box->GetUnscaledBoxExtent();
        const FVector V[]={FVector(-E.X,-E.Y,-E.Z),FVector(E.X,-E.Y,-E.Z),
            FVector(E.X,E.Y,-E.Z),FVector(-E.X,E.Y,-E.Z),FVector(-E.X,-E.Y,E.Z),
            FVector(E.X,-E.Y,E.Z),FVector(E.X,E.Y,E.Z),FVector(-E.X,E.Y,E.Z)};
        const int32 Faces[][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
        for(const auto& F:Faces){Triangle(V[F[0]],V[F[1]],V[F[2]],false);Triangle(V[F[0]],V[F[2]],V[F[3]],false);}
    }
    else
    {
        // Query the cooked collision BVH, not visual sections or a scan of
        // every dungeon triangle. Instance transforms include authored scale.
        const FBox WorldBounds(Feet-FVector(Radius,Radius,MaxDown+.5f),
            Feet+FVector(Radius,Radius,MaxUp+2*Radius+.5f));
        const FBox LocalBounds=WorldBounds.TransformBy(Transform.ToInverseMatrixWithScale());
        const Chaos::FAABB3 Bounds(LocalBounds.Min,LocalBounds.Max);
        for(const auto& Mesh:Setup->TriMeshGeometries)
            if(Mesh)Mesh->VisitTriangles(Bounds,Chaos::FRigidTransform3::Identity,
                [&](const Chaos::FTriangle& T,int32,int32,int32,int32)
                {Triangle(FVector(T[0]),FVector(T[1]),FVector(T[2]),Setup->bDoubleSidedGeometry);});
    }
    return Found;
}
