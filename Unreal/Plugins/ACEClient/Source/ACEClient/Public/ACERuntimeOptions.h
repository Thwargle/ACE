#pragma once
#include "CoreMinimal.h"
namespace ACERuntimeOptions
{
struct FOption { const TCHAR* Key; const TCHAR* Label; float Default, Min, Max; };
inline constexpr FOption Values[] = {
 {TEXT("MasterVolume"),TEXT("Master Volume"),1,0,1},
 {TEXT("EffectsVolume"),TEXT("Sound Effects"),1,0,1},
 {TEXT("AmbientVolume"),TEXT("Ambient Sounds"),1,0,1},
 {TEXT("ActiveSoundOnly"),TEXT("Play Sounds Only When Active"),1,0,1},
 {TEXT("CameraStiffness"),TEXT("Camera Stiffness"),1,.25f,3},
 {TEXT("CameraAdjustment"),TEXT("Camera Adjustment Speed"),1,.25f,3},
 {TEXT("FieldOfView"),TEXT("Field of View"),1,.75f,1.25f},
 {TEXT("Brightness"),TEXT("Screen Brightness"),2.2f,1.6f,2.8f},
 {TEXT("Anisotropy"),TEXT("Texture Filtering"),16,1,16},
 {TEXT("DesktopUIScale"),TEXT("Desktop UI Scale"),1,1,3},
 {TEXT("ShowFrameRate"),TEXT("Show FPS overlay"),0,0,1},
 {TEXT("ChatFontFace"),TEXT("Chat Font"),2,0,4},
 {TEXT("ChatFontSize"),TEXT("Chat Font Size"),1,0,4}
};
ACECLIENT_API float Get(const TCHAR* Key);
ACECLIENT_API void Set(const TCHAR* Key, float Value);
ACECLIENT_API void Apply();
ACECLIENT_API float SoundGain(bool bAmbient);
/** Retail EnumIDMap 25000012; actual glyph atlases from the user's DAT on every platform. */
ACECLIENT_API uint32 ChatFontId();
ACECLIENT_API float DesktopUIScale(FIntPoint ViewportSize, bool bVR);
ACECLIENT_API void ApplyDesktopUIScale(FIntPoint ViewportSize, bool bVR);
}
