#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Bounded, host-rendered terrain aid. Lua owns configuration, never geometry. */
struct FACEPluginTerrain
{
    bool Slopes=false, Water=false, Radar=false, Dungeon=false;
    float Opacity=.45f, Range=120.f, Brightness=.3f;
    FLinearColor SlopeColor=FLinearColor(1,.08f,.02f), WaterColor=FLinearColor(.15f,.25f,1);
    FLinearColor RadarColor=FLinearColor(.1f,.15f,.25f), LightColor=FLinearColor::White;
    bool SameSurface(const FACEPluginTerrain& Other) const
    {return Slopes==Other.Slopes && Water==Other.Water && Opacity==Other.Opacity && Range==Other.Range
        && SlopeColor==Other.SlopeColor && WaterColor==Other.WaterColor;}
    bool Active() const { return Slopes||Water||Radar||Dungeon; }
};
namespace ACEPluginTerrain
{
    bool Parse(const TSharedPtr<FJsonObject>& Json, FACEPluginTerrain& Out, FString& Error);
    // Retail PhysicsGlobals::floor_z and CLandCell::find_env_collisions.
    constexpr float WalkableZ=.6641741f;
    FLinearColor Shade(float NormalZ, float DepthAc, uint8 BlockWaterType, const FACEPluginTerrain& Settings);
}
