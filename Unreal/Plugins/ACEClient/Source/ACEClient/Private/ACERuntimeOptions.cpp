#include "ACERuntimeOptions.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/UserInterfaceSettings.h"
namespace ACERuntimeOptions
{
namespace
{
 uint64 SoundSettingsFrame=MAX_uint64;
 float AmbientGain=1.f,EffectsGain=1.f;
}
float Get(const TCHAR* Key)
{
 for (const auto& O : Values) if (FCString::Strcmp(O.Key,Key)==0) {
  float V=O.Default; if (GConfig) GConfig->GetFloat(TEXT("ACE.Presentation"),Key,V,GGameUserSettingsIni);
  return FMath::IsFinite(V)?FMath::Clamp(V,O.Min,O.Max):O.Default;
 }
 return 1;
}
void Set(const TCHAR* Key,float Value)
{
 if (!FMath::IsFinite(Value)) return;
 if (FCString::Strcmp(Key,TEXT("DesktopUIScale"))==0) Value=FMath::RoundToFloat(Value*4.f)/4.f;
 SoundSettingsFrame=MAX_uint64;
 for (const auto& O : Values) if (FCString::Strcmp(O.Key,Key)==0 && GConfig)
  GConfig->SetFloat(TEXT("ACE.Presentation"),Key,FMath::Clamp(Value,O.Min,O.Max),GGameUserSettingsIni);
}
void Apply()
{
 SoundSettingsFrame=MAX_uint64;
 if (auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.TonemapperGamma"))) C->Set(Get(TEXT("Brightness")),ECVF_SetByGameSetting);
 if (auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaxAnisotropy"))) C->Set(FMath::RoundToInt(Get(TEXT("Anisotropy"))),ECVF_SetByGameSetting);
 if (GConfig) GConfig->Flush(false,GGameUserSettingsIni);
}
float SoundGain(bool bAmbient)
{
 // Every live emitter asks for this gain each tick. Read the shared settings
 // once per frame, with immediate invalidation when the player moves a slider.
 if (SoundSettingsFrame!=GFrameCounter)
 {
  SoundSettingsFrame=GFrameCounter;
  const bool Muted=Get(TEXT("ActiveSoundOnly"))>.5f && FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive();
  const float Master=Muted ? 0.f : Get(TEXT("MasterVolume"));
  AmbientGain=Master*Get(TEXT("AmbientVolume"));EffectsGain=Master*Get(TEXT("EffectsVolume"));
 }
 return bAmbient?AmbientGain:EffectsGain;
}
float DesktopUIScale(FIntPoint Size,bool bVR)
{
 if (bVR || Size.X<=0 || Size.Y<=0) return 1.f;
 // Whole quarter steps, with room for the complete native 800x600 interface.
 const float Fit=FMath::Max(1.f,FMath::FloorToFloat(FMath::Min(Size.X/800.f,Size.Y/600.f)*4.f)/4.f);
 return FMath::Min(FMath::RoundToFloat(Get(TEXT("DesktopUIScale"))*4.f)/4.f,Fit);
}
void ApplyDesktopUIScale(FIntPoint Size,bool bVR)
{
 // Slate applies this once to the whole viewport, including child controls,
 // text rasterization and input coordinates. VR surfaces have their own scale.
 GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale=DesktopUIScale(Size,bVR);
}
}
