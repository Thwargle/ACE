#pragma once
#include "CoreMinimal.h"

namespace ACERetailCommandSyntax
{
// NextArg / JoinArgsAsName accept whitespace between tokens and quoted names.
// Consume only the command token: message text keeps its original case/spacing.
inline FString Take(FString& Rest)
{
    Rest.TrimStartAndEndInline();
    int32 End=0;
    while (End<Rest.Len() && !FChar::IsWhitespace(Rest[End])) ++End;
    FString Word=Rest.Left(End).ToLower();
    Rest=Rest.Mid(End).TrimStartAndEnd();
    return Word;
}
inline FString Name(FString Text)
{
    Text.TrimStartAndEndInline();
    Text.TrimQuotesInline();
    return Text.TrimStartAndEnd();
}
inline uint32 HouseType(const FString& Text)
{
    FString Rest=Text;
    const FString Type=Take(Rest);
    if (Type==TEXT("cottage")) return 1;
    if (Type==TEXT("villa")) return 2;
    if (Type==TEXT("mansion")) return 3;
    if (Type==TEXT("apartment")) return 4;
    return 0;
}
inline FString CorpseCoordinates(uint32 Cell)
{
    // LandDefs::gid_to_lcoord / CellidToCoordinateString use the outdoor
    // cell, not the precise xyz (same displayed one-decimal retail location).
    const uint32 Local=(Cell&0xffff)-1;
    const double EW=(int32((Cell>>24)*8+Local/8)-1024)*.1+.5;
    const double NS=(int32(((Cell>>16)&255)*8+Local%8)-1024)*.1+.5;
    return FString::Printf(TEXT("%.1f%s, %.1f%s"), FMath::Abs(NS), NS<0?TEXT("S"):NS>0?TEXT("N"):TEXT(""),
        FMath::Abs(EW), EW<0?TEXT("W"):EW>0?TEXT("E"):TEXT(""));
}
}
