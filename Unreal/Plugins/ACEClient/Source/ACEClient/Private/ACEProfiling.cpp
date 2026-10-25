#include "ACEProfiling.h"
DEFINE_STAT(STAT_ACENetwork);
DEFINE_STAT(STAT_ACEWorld);
DEFINE_STAT(STAT_ACETerrain);
DEFINE_STAT(STAT_ACEVisibility);
DEFINE_STAT(STAT_ACERemote);
DEFINE_STAT(STAT_ACEAnimation);
DEFINE_STAT(STAT_ACEAppearance);
DEFINE_STAT(STAT_ACEEffects);
DEFINE_STAT(STAT_ACEHUD);
DEFINE_STAT(STAT_ACEPlugins);
DEFINE_STAT(STAT_ACEPluginSnapshot);
DEFINE_STAT(STAT_ACEPluginLua);
DEFINE_STAT(STAT_ACESky);
DEFINE_STAT(STAT_ACESkyFog);
DEFINE_STAT(STAT_ACESkyLighting);
DEFINE_STAT(STAT_ACEPlayer);

DEFINE_STAT(STAT_ACEDatMaintenance);
DEFINE_STAT(STAT_ACEEffectScripts);
DEFINE_STAT(STAT_ACEEffectSimulation);
DEFINE_STAT(STAT_ACEEffectLights);
DEFINE_STAT(STAT_ACEParticleUpload);

namespace ACEProfiling
{
static FFrameHistory History;
FScope::FScope(EScope InScope) : Scope(InScope)
{
 if(IsInGameThread()){Frame=GFrameCounter;Start=FPlatformTime::Cycles64();}
}
FScope::~FScope()
{
 if(Start && Frame==GFrameCounter)History.Add(Frame,Scope,FPlatformTime::Cycles64()-Start);
}
FString DescribeLastFrame()
{
 const auto* Costs=IsInGameThread()?History.Completed(GFrameCounter):nullptr;
 if(!Costs)return {};
 auto Ms=[&](EScope S){return FPlatformTime::ToMilliseconds64(Costs->Cycles[uint8(S)]);};
 return FString::Printf(TEXT("client CPU (previous frame, inclusive): player=%.2f remote=%.2f network=%.2f world=%.2f terrain=%.2f visibility=%.2f animation=%.2f appearance=%.2f effects=%.2f HUD=%.2f sky=%.2f plugins=%.2f snapshot=%.2f Lua=%.2f cache=%.2f effectHooks=%.2f particles=%.2f lightUpdate=%.2f uploads=%.2f ms"),
  Ms(EScope::Player),Ms(EScope::Remote),Ms(EScope::Network),Ms(EScope::World),Ms(EScope::Terrain),Ms(EScope::Visibility),
  Ms(EScope::Animation),Ms(EScope::Appearance),Ms(EScope::Effects),Ms(EScope::HUD),Ms(EScope::Sky),Ms(EScope::Plugins),Ms(EScope::PluginSnapshot),Ms(EScope::PluginLua),Ms(EScope::DatMaintenance),Ms(EScope::EffectScripts),Ms(EScope::EffectSimulation),Ms(EScope::EffectLights),Ms(EScope::ParticleUpload));
}
}
