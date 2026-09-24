#pragma once
#include "CoreMinimal.h"

namespace ACERetailUILayout
{
    struct FWindow { const TCHAR* Tag; const TCHAR* Element; };
    // gmGamePlayUI::SaveScreenLayout / LoadScreenLayout (retail 004EA8F0).
    inline constexpr FWindow Windows[] = {
        {TEXT("SBOX"), TEXT("RootGameplay_SmartBox_Field")},
        {TEXT("CHAT"), TEXT("RootGameplay_FloatyMainChat_Field")},
        {TEXT("FCH1"), TEXT("RootGameplay_FloatyChat1_Field")},
        {TEXT("FCH2"), TEXT("RootGameplay_FloatyChat2_Field")},
        {TEXT("FCH3"), TEXT("RootGameplay_FloatyChat3_Field")},
        {TEXT("FCH4"), TEXT("RootGameplay_FloatyChat4_Field")},
        {TEXT("EXAM"), TEXT("RootGameplay_FloatyExamination_Field")},
        {TEXT("VITS"), TEXT("RootGameplay_FloatyVitals_Field")},
        {TEXT("SVIT"), TEXT("RootGameplay_FloatySideVitals_Field")},
        {TEXT("ENVP"), TEXT("RootGameplay_FloatyEnvPanel_Field")},
        {TEXT("PANS"), TEXT("RootGameplay_FloatyPanel_Field")},
        {TEXT("TBAR"), TEXT("RootGameplay_FloatyToolbar_Field")},
        {TEXT("INDI"), TEXT("RootGameplay_FloatyIndicators_Field")},
        {TEXT("PBAR"), TEXT("RootGameplay_PowerBar_Field")},
        {TEXT("COMB"), TEXT("RootGameplay_FloatyCombatPanel_Field")},
        {TEXT("RADA"), TEXT("RootGameplay_Radar_Field")}
    };
    struct FRect { FString Tag; int32 X, Y, W, H; };
    bool Parse(const FString& Text, TArray<FRect>& Rects, FString& Error);
    bool NamedFile(const FString& Arguments, FString& Filename, FString& Error);
    FString AutoFile(const FString& Server, const FString& Character, FIntPoint Size);
    bool Save(const FString& Path, const FString& Text, FString& Error);
    bool Load(const FString& Path, FString& Text, FString& Error);
}
