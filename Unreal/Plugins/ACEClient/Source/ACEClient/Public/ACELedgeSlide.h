#pragma once
#include "CoreMinimal.h"

namespace ACELedgeSlide
{
    // Find the support boundary, then keep only the input's component along it.
    // A perpendicular-to-input nudge alternated sides and moved even when the
    // player was pushing straight into an edge. Never generate reverse motion.
    template<class FSupported>
    FVector2D Resolve(const FVector2D& Start, const FVector2D& Move, FSupported Supported)
    {
        if (Move.IsNearlyZero() || !Supported(Start)) return Start;
        if (Supported(Start+Move)) return Start+Move;
        double Low=0, High=1;
        for (int32 I=0; I<7; ++I)
        {
            const double Mid=(Low+High)*0.5;
            if (Supported(Start+Move*Mid)) Low=Mid; else High=Mid;
        }
        const FVector2D Edge=Start+Move*Low;
        const double Radius=FMath::Max(1.0,Move.Size()*0.5);
        FVector2D Outward=FVector2D::ZeroVector;
        for (int32 I=0; I<16; ++I)
        {
            const double Angle=I*UE_DOUBLE_PI/8;
            const FVector2D Direction(FMath::Cos(Angle),FMath::Sin(Angle));
            if (!Supported(Edge+Direction*Radius)) Outward+=Direction;
        }
        Outward.Normalize();
        const FVector2D Remaining=Move*(1-Low);
        const FVector2D Tangent=Remaining-Outward*FMath::Max(0.0,FVector2D::DotProduct(Remaining,Outward));
        if (FVector2D::DotProduct(Tangent,Move)<=0) return Edge;
        for (double Amount : {1.0,0.5,0.25})
        {
            const FVector2D End=Edge+Tangent*Amount;
            if (Supported(End)) return End;
        }
        return Edge;
    }
}
