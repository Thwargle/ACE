#include "ACERuntimeOptions.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
namespace ACERuntimeOptions
{
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
 for (const auto& O : Values) if (FCString::Strcmp(O.Key,Key)==0 && GConfig)
  GConfig->SetFloat(TEXT("ACE.Presentation"),Key,FMath::Clamp(Value,O.Min,O.Max),GGameUserSettingsIni);
}
void Apply()
{
 if (auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.TonemapperGamma"))) C->Set(Get(TEXT("Brightness")),ECVF_SetByGameSetting);
 if (auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaxAnisotropy"))) C->Set(FMath::RoundToInt(Get(TEXT("Anisotropy"))),ECVF_SetByGameSetting);
 if (GConfig) GConfig->Flush(false,GGameUserSettingsIni);
}
float SoundGain(bool bAmbient)
{
 if (Get(TEXT("ActiveSoundOnly"))>.5f && FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive()) return 0;
 return Get(TEXT("MasterVolume"))*Get(bAmbient?TEXT("AmbientVolume"):TEXT("EffectsVolume"));
}
}
