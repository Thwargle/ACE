#pragma once
#include "CoreMinimal.h"

// Client-specific chat commands use acue. Retail/server names are not ours to
// rename. Keep old plugin spellings as aliases for saved metas and Lua tools.
namespace ACEClientChatCommands
{
inline FString CanonicalName(const FString& Name)
{
    const FString Lower=Name.ToLower();
    static const TMap<FString,FString> Aliases={
        {TEXT("aceperf"),TEXT("acueperf")},{TEXT("aceobject"),TEXT("acueobject")},
        {TEXT("plugins"),TEXT("acueplugins")},{TEXT("plugin"),TEXT("acueplugin")},
        {TEXT("ucm"),TEXT("acueucm")},{TEXT("items"),TEXT("acueitems")},{TEXT("it"),TEXT("acueitems")}};
    if(const auto* Canonical=Aliases.Find(Lower))return *Canonical;
    return Lower;
}

// Replace only the command token. Arguments may contain names, quoted chat,
// paths, or further command text and must not be rewritten.
inline FString CanonicalLine(const FString& Line)
{
    if(Line.IsEmpty() || (Line[0]!='/' && Line[0]!='@'))return Line;
    int32 End=1;while(End<Line.Len() && !FChar::IsWhitespace(Line[End]))++End;
    const FString Name=Line.Mid(1,End-1),Canonical=CanonicalName(Name);
    if(!Canonical.StartsWith(TEXT("acue")))return Line;
    return Line.Left(1)+Canonical+Line.Mid(End);
}
}
