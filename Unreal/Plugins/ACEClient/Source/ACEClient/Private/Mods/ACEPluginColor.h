#pragma once
#include "CoreMinimal.h"

namespace ACEPluginColor
{
inline bool Hex(const FString& Text, FLinearColor& Out)
{
    if (!Text.StartsWith(TEXT("#")) || (Text.Len()!=7 && Text.Len()!=9)) return false;
    for (int32 I=1; I<Text.Len(); ++I) if (!FChar::IsHexDigit(Text[I])) return false;
    Out=FLinearColor(FColor::FromHex(Text)); return true;
}
inline bool Parse(const FString& Text, FLinearColor& Out)
{
    // Preserve the existing panel palette. Hex is sRGB RGB plus linear alpha.
    static const TMap<FString,FLinearColor> Names={
        {TEXT("green"),{.2f,1,.4f}}, {TEXT("red"),{1,.3f,.2f}},
        {TEXT("yellow"),{1,.85f,.25f}}, {TEXT("blue"),{.25f,.7f,1}},
        {TEXT("purple"),{.8f,.3f,1}}, {TEXT("white"),FLinearColor::White},
        {TEXT("black"),FLinearColor::Black}, {TEXT("transparent"),FLinearColor::Transparent}};
    if (const auto* Color=Names.Find(Text)) { Out=*Color; return true; }
    return Hex(Text,Out);
}
inline FString Encode(const FLinearColor& Color) { return TEXT("#")+Color.ToFColorSRGB().ToHex(); }
}
