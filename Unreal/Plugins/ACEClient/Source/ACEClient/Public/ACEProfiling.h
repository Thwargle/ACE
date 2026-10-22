#pragma once
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DECLARE_STATS_GROUP(TEXT("ACE Client"), STATGROUP_ACE, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Network / dispatch"), STAT_ACENetwork, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("World presentation"), STAT_ACEWorld, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Terrain presentation"), STAT_ACETerrain, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Portal visibility"), STAT_ACEVisibility, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Remote movement / ground"), STAT_ACERemote, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Character animation"), STAT_ACEAnimation, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Appearance construction"), STAT_ACEAppearance, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Scripts / particles / lights"), STAT_ACEEffects, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("HUD refresh"), STAT_ACEHUD, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Plugin tick"), STAT_ACEPlugins, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Plugin snapshot"), STAT_ACEPluginSnapshot, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Plugin Lua"), STAT_ACEPluginLua, STATGROUP_ACE, ACECLIENT_API);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Sky presentation"), STAT_ACESky, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Sky fog / world materials"), STAT_ACESkyFog, STATGROUP_ACE, ACECLIENT_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Sky world lighting"), STAT_ACESkyLighting, STATGROUP_ACE, ACECLIENT_API);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Player movement / input"), STAT_ACEPlayer, STATGROUP_ACE, ACECLIENT_API);

namespace ACEProfiling
{
enum class EScope : uint8 { Network,World,Terrain,Visibility,Remote,Animation,Appearance,Effects,HUD,Sky,SkyFog,SkyLighting,Player,Plugins,PluginSnapshot,PluginLua,Count };
// Fixed storage, no history or per-frame allocation. These are inclusive CPU
// times; nested categories must not be added together as a total frame time.
struct FFrameCosts
{
 uint64 Frame=MAX_uint64;
 uint64 Cycles[uint8(EScope::Count)]={};
 void Add(EScope Scope,uint64 Duration) { Cycles[uint8(Scope)]+=Duration; }
};
struct FFrameHistory
{
 FFrameCosts Current,Previous;
 void Add(uint64 Frame,EScope Scope,uint64 Duration)
 {
  if(Current.Frame!=Frame){Previous=Current;Current={};Current.Frame=Frame;}
  Current.Add(Scope,Duration);
 }
 const FFrameCosts* Completed(uint64 Frame) const
 {
  if(Current.Frame!=MAX_uint64 && Current.Frame+1==Frame)return &Current;
  if(Previous.Frame!=MAX_uint64 && Previous.Frame+1==Frame)return &Previous;
  return nullptr;
 }
};
class ACECLIENT_API FScope
{
public:
 explicit FScope(EScope InScope);
 ~FScope();
private:
 EScope Scope;
 uint64 Start=0,Frame=0;
};
ACECLIENT_API FString DescribeLastFrame();
}
#define ACE_PROFILE_SCOPE(Name) SCOPE_CYCLE_COUNTER(STAT_ACE##Name); TRACE_CPUPROFILER_EVENT_SCOPE(ACE_##Name); ACEProfiling::FScope UE_JOIN(ACEFrameScope_,__LINE__)(ACEProfiling::EScope::Name)
