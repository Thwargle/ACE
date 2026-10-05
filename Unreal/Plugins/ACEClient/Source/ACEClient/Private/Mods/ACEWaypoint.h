#pragma once
#include "CoreMinimal.h"
#include "ACETypes.h"

namespace ACEWaypoint
{
    struct FLink { int32 Begin=0, End=0; FVector2D Coordinates; };
    // Coordinates are signed E/W (X), N/S (Y), in the retail map's 240m units.
    TArray<FLink> FindCoordinates(const FString& Text);
    bool Parse(const FString& Text, FVector2D& Out);
    FString Format(FVector2D Coordinates);
    FVector2D Coordinates(const FACEPosition& Position);
    double RelativeBearing(FVector2D From, FVector2D To, FVector2D Forward);
    // Screen basis: north-up by default, or the normalized facing vector up.
    inline FVector2D ToMap(FVector2D Delta,FVector2D Up)
    {return {Delta.X*Up.Y-Delta.Y*Up.X,-FVector2D::DotProduct(Delta,Up)};}
    inline FVector2D FromMap(FVector2D Delta,FVector2D Up)
    {return {Delta.X*Up.Y-Delta.Y*Up.X,-Delta.X*Up.X-Delta.Y*Up.Y};}
}
