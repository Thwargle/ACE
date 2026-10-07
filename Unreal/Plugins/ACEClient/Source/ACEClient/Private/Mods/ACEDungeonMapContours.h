#pragma once
#include "CoreMinimal.h"

// Cached map contours in AC cell coordinates (meters). Shared walkable edges
// disappear, except where a PhysicsBSP wall actually separates the floors.
// Work is spatially indexed and budgeted; painting never rebuilds topology.
class FACEDungeonMapContours
{
public:
    struct FEdge { FVector A,B; };
    TArray<FEdge> Lines;
    int32 InputEdgeCount() const { return Edges.Num(); }
    bool IsComplete() const { return NextEdge==Edges.Num(); }
    void AddPolygon(const TArray<FVector>& P)
    {
        if(P.Num()<3 || Edges.Num()>200000 || Walls.Num()>100000)return;
        const FVector N=FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).GetSafeNormal();
        if(N.IsNearlyZero())return;
        if(N.Z>.35)
        {
            for(int32 I=0;I<P.Num();++I)
            {
                const FEdge E{P[I],P[(I+1)%P.Num()]};if(E.A.Equals(E.B,.001))continue;
                const int32 Id=Edges.Add(E);FBox Bounds(E.A,E.A);Bounds+=E.B;
                Visit(Bounds,[&](FIntVector Key){EdgeGrid.FindOrAdd(Key).Add(Id);});
            }
        }
        else if(FMath::Abs(N.Z)<.95)
        {
            const int32 Id=Walls.Add(P);FBox Bounds(ForceInit);for(const auto& V:P)Bounds+=V;
            Visit(Bounds,[&](FIntVector Key){WallGrid.FindOrAdd(Key).Add(Id);});
        }
    }
    void BuildStep(double Seconds=.002)
    {
        const double Until=FPlatformTime::Seconds()+Seconds;
        while(NextEdge<Edges.Num() && FPlatformTime::Seconds()<Until)BuildEdge(NextEdge++);
    }
private:
    static constexpr double Epsilon=.005;
    TArray<FEdge> Edges;
    TArray<TArray<FVector>> Walls;
    TMap<FIntVector,TArray<int32>> EdgeGrid,WallGrid;
    int32 NextEdge=0;
    template<class F> static void Visit(const FBox& Bounds,F&& Fn)
    {
        const auto B=Bounds.ExpandBy(Epsilon);
        const FIntVector A(FMath::FloorToInt(B.Min.X/10),FMath::FloorToInt(B.Min.Y/10),FMath::FloorToInt(B.Min.Z/10));
        const FIntVector Z(FMath::FloorToInt(B.Max.X/10),FMath::FloorToInt(B.Max.Y/10),FMath::FloorToInt(B.Max.Z/10));
        for(int32 X=A.X;X<=Z.X;++X)for(int32 Y=A.Y;Y<=Z.Y;++Y)for(int32 H=A.Z;H<=Z.Z;++H)Fn({X,Y,H});
    }
    // Clip a seam just above its walking surface against a coplanar convex wall.
    // A doorway lintel above the floor must not turn its open passage into a wall.
    static bool WallInterval(const FEdge& Edge,const TArray<FVector>& P,FVector2D& Out)
    {
        const FVector N=FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).GetSafeNormal();
        if(FMath::Abs(FVector::DotProduct(Edge.A-P[0],N))>Epsilon || FMath::Abs(FVector::DotProduct(Edge.B-P[0],N))>Epsilon)return false;
        // Step upward along the wall plane, including tilted collision walls.
        const FVector Rise=FVector(0,0,.05)-N*(N.Z*.05);
        const FVector A=Edge.A+Rise,B=Edge.B+Rise;
        double Lo=0,Hi=1;
        for(int32 I=0;I<P.Num();++I)
        {
            const FVector Side=(P[(I+1)%P.Num()]-P[I]).GetSafeNormal();
            const double DA=FVector::DotProduct(FVector::CrossProduct(Side,A-P[I]),N);
            const double DB=FVector::DotProduct(FVector::CrossProduct(Side,B-P[I]),N);
            if(DA< -Epsilon && DB< -Epsilon)return false;
            if(DA< -Epsilon)Lo=FMath::Max(Lo,DA/(DA-DB));
            if(DB< -Epsilon)Hi=FMath::Min(Hi,DA/(DA-DB));
        }
        Out={Lo,Hi};return Hi>Lo;
    }
    void BuildEdge(int32 Id)
    {
        const FEdge& E=Edges[Id];const FVector D=E.B-E.A;const double Length=D.Size();const FVector Axis=D/Length;
        FBox Bounds(E.A,E.A);Bounds+=E.B;TSet<int32> Candidates,Blockers;
        Visit(Bounds,[&](FIntVector Key){if(const auto* Found=EdgeGrid.Find(Key))for(int32 Other:*Found)Candidates.Add(Other);});
        TArray<FVector2D> Open;
        for(int32 Other:Candidates)
        {
            if(Other==Id)continue;const auto& O=Edges[Other];
            // Only opposite sides of a floor boundary join. Duplicate geometry
            // on the same side must not erase the room's outside perimeter.
            if(FVector::DotProduct(Axis,(O.B-O.A).GetSafeNormal())>-.99999)continue;
            if(FVector::CrossProduct(O.A-E.A,Axis).Size()>Epsilon || FVector::CrossProduct(O.B-E.A,Axis).Size()>Epsilon)continue;
            const double T0=FVector::DotProduct(O.A-E.A,Axis)/Length,T1=FVector::DotProduct(O.B-E.A,Axis)/Length;
            const double Lo=FMath::Max(0.,FMath::Min(T0,T1)),Hi=FMath::Min(1.,FMath::Max(T0,T1));
            if(Hi-Lo>Epsilon/Length)Open.Add({Lo,Hi});
        }
        if(Open.IsEmpty()){Lines.Add(E);return;}
        Bounds+=E.A+FVector(0,0,.05);Bounds+=E.B+FVector(0,0,.05);
        Visit(Bounds,[&](FIntVector Key){if(const auto* Found=WallGrid.Find(Key))for(int32 Other:*Found)Blockers.Add(Other);});
        TArray<FVector2D> Solid;
        for(int32 Wall:Blockers){FVector2D Interval;if(WallInterval(E,Walls[Wall],Interval))Solid.Add(Interval);}
        TArray<double> Cuts{0,1};for(auto I:Open){Cuts.Add(I.X);Cuts.Add(I.Y);}for(auto I:Solid){Cuts.Add(I.X);Cuts.Add(I.Y);}Cuts.Sort();
        for(int32 I=1;I<Cuts.Num();++I)
        {
            const double A=Cuts[I-1],B=Cuts[I],Mid=(A+B)*.5;if((B-A)*Length<Epsilon)continue;
            const bool Connected=Open.ContainsByPredicate([&](FVector2D V){return Mid>=V.X && Mid<=V.Y;});
            const bool Wall=Solid.ContainsByPredicate([&](FVector2D V){return Mid>=V.X && Mid<=V.Y;});
            if(!Connected || Wall)Lines.Add({E.A+D*A,E.A+D*B});
        }
    }
};
