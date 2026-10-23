#include "ACEFrameDiagnostics.h"
#include "ACEProfiling.h"
#include "ACEClientBuild.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEWorldEntityActor.h"
#include "ACEScriptComponent.h"
#include "ACEParticleUpdateSubsystem.h"
#include "ACEEffectLightSubsystem.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "RHI.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"

TArray<FString> ACEFrameDiagnostics::Describe(UWorld* World,double FrameMs)
{
 TArray<FString> Lines;
 if(!World || !World->GetGameInstance())return Lines;
 auto* Client=World->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
 const uint32 GpuCycles=GDynamicRHI?RHIGetGPUFrameCycles():0;
 const FString Gpu=GpuCycles?FString::Printf(TEXT("%.1f ms"),FPlatformTime::ToMilliseconds(GpuCycles)):TEXT("unavailable");
 FVector2D Size=FVector2D::ZeroVector;
 if(auto* Viewport=World->GetGameInstance()->GetGameViewportClient())Viewport->GetViewportSize(Size);
 Lines.Add(FString::Printf(TEXT("%s %s %s %.0fx%.0f cell=%08X frame=%.1f ms; game=%.1f render=%.1f RHI=%.1f GPU=%s (recent asynchronous samples)"),
  ACEClientBuild::Version,PLATFORM_LINUX?TEXT("Linux"):PLATFORM_WINDOWS?TEXT("Windows"):TEXT("Other"),GDynamicRHI?GDynamicRHI->GetName():TEXT("NoRHI"),Size.X,Size.Y,
  Client?uint32(Client->GetPlayerPosition().CellId):0,FrameMs,FPlatformTime::ToMilliseconds(GGameThreadTime),
  FPlatformTime::ToMilliseconds(GRenderThreadTime),FPlatformTime::ToMilliseconds(GRHIThreadTime),*Gpu));
 int32 Creatures=0,Dying=0,Corpses=0,Projectiles=0,Emitters=0,Degraded=0,Particles=0,Lights=0;
 for(TActorIterator<AActor> It(World);It;++It)
 {
  if(const auto* Entity=Cast<AACEWorldEntityActor>(*It))
  {
   Corpses+=Entity->IsCorpse();Dying+=Entity->HasReceivedDeathMotion();
   Creatures+=(Entity->ItemType&ACEItemType::Creature)!=0;
   Projectiles+=(Entity->PhysicsState&ACEPhysicsState::Missile)!=0;
  }
  TInlineComponentArray<UACEScriptComponent*,2> Scripts(*It);
  for(const auto* Script:Scripts)Script->AccumulateParticleAudit(Emitters,Degraded,Particles,Lights);
 }
 const auto* Updates=World->GetSubsystem<UACEParticleUpdateSubsystem>();
 const auto* FX=World->GetSubsystem<UACEEffectLightSubsystem>();
 bool UCM=false;
 if(auto* Plugins=World->GetGameInstance()->GetSubsystem<UACEPluginSubsystem>())
  for(const auto& P:Plugins->Plugins)if(P.IsValid() && P->Id==TEXT("ucm"))UCM=P->Running;
 Lines.Add(FString::Printf(TEXT("creatures=%d dying=%d corpses=%d projectiles=%d; emitters=%d degraded=%d particles=%d effectLights=%d; batches=%d vertices=%lld UCM=%d"),
  Creatures,Dying,Corpses,Projectiles,Emitters,Degraded,Particles,FX?FX->GetVisibleLightCount():0,
  Updates?Updates->GetLastBatchCount():0,Updates?Updates->GetLastVertexCount():0,UCM));
 if(Client)if(auto Session=Client->GetSession())Lines.Add(Session->DescribeNetworkPump());
 const FString Costs=ACEProfiling::DescribeLastFrame();
 if(!Costs.IsEmpty())Lines.Add(Costs);
 // Report effective settings, not just saved preferences: PIE/editor and
 // command-line overrides can differ. Read only when a report is requested.
 const auto Quality=Scalability::GetQualityLevels();
 const auto CVar=[](const TCHAR* Name,float Fallback)
 {
  const auto* Value=IConsoleManager::Get().FindConsoleVariable(Name);
  return Value?Value->GetFloat():Fallback;
 };
 Lines.Add(FString::Printf(TEXT("render settings: %s screen=%.0f%% texture=%d shadow=%d effects=%d lightBudget=%.0f fpsLimit=%.0f vsync=%.0f; RHI texture allocations=%.1f MiB"),
  World->IsPlayInEditor()?TEXT("editor play"):TEXT("game"),CVar(TEXT("r.ScreenPercentage"),100),Quality.TextureQuality,Quality.ShadowQuality,Quality.EffectsQuality,
  CVar(TEXT("ace.FXLightBudget"),0),CVar(TEXT("t.MaxFPS"),0),CVar(TEXT("r.VSync"),0),
  (GRHIGlobals.StreamingTextureMemorySizeInKB+GRHIGlobals.NonStreamingTextureMemorySizeInKB)/1024.));
 return Lines;
}
