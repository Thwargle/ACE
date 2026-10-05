#include "ACEVTRegex.h"
#include "Containers/StringConv.h"
#if UE_ENABLE_ICU
THIRD_PARTY_INCLUDES_START
#include <unicode/uregex.h>
THIRD_PARTY_INCLUDES_END
#endif

namespace ACEVTRegex
{
#if UE_ENABLE_ICU
static URegularExpression* Compile(const FString& Input,FString& Error,FString* Normalized=nullptr)
{
    FString Pattern;bool InClass=false;
    for(int At=0;At<Input.Len();++At)
    {
        const TCHAR C=Input[At];
        if(C=='\\'){Pattern+=C;if(At+1<Input.Len())Pattern+=Input[++At];continue;}
        if(C=='[')InClass=true;else if(C==']')InClass=false;
        if(!InClass&&Input.Mid(At,3)==TEXT("(?'"))
        {
            const int End=Input.Find(TEXT("'"),ESearchCase::CaseSensitive,ESearchDir::FromStart,At+3);
            if(End==INDEX_NONE){Error=TEXT("Unterminated named capture");return nullptr;}
            Pattern+=TEXT("(?<")+Input.Mid(At+3,End-At-3)+TEXT(">");At=End;continue;
        }
        if(!InClass&&Input.Mid(At,2)==TEXT("(?"))
        {
            // Extended-mode comments change capture parsing; reject at import,
            // not only on a match. Other ordinary inline flags are supported.
            for(int Scan=At+2;Scan<Input.Len()&&(FChar::IsAlpha(Input[Scan])||Input[Scan]=='-');++Scan)
                if(Input[Scan]=='x'){Error=TEXT("Extended-mode regex is unsupported");return nullptr;}
            if(Input.Mid(At,3)==TEXT("(?#")){Error=TEXT("Regex comment groups are unsupported");return nullptr;}
        }
        Pattern+=C;
    }
    if(Normalized)*Normalized=Pattern;
    if(Pattern.Len()>2048){Error=TEXT("Pattern exceeds 2048 characters");return nullptr;}
    // .NET-only balancing groups, conditional groups and named backreferences
    // cannot be reinterpreted as ICU syntax without changing their meaning.
    if(Pattern.Contains(TEXT("(?('"))||Pattern.Contains(TEXT("(?<="))||Pattern.Contains(TEXT("(?<!")))
    {Error=TEXT("Lookbehind/conditional regex is not supported");return nullptr;}
    const FString Nonempty=Pattern.IsEmpty()?TEXT("(?:)"):Pattern;
    const auto Utf16=StringCast<UTF16CHAR>(*Nonempty,Nonempty.Len());UErrorCode Status=U_ZERO_ERROR;UParseError Parse{};
    auto* Regex=uregex_open(reinterpret_cast<const UChar*>(Utf16.Get()),Utf16.Length(),0,&Parse,&Status);
    if(U_FAILURE(Status)){Error=FString::Printf(TEXT("Invalid pattern at %d: %s"),Parse.offset,UTF8_TO_TCHAR(u_errorName(Status)));return nullptr;}
    if(uregex_groupCount(Regex,&Status)>32){uregex_close(Regex);Error=TEXT("Pattern exceeds 32 capture groups");return nullptr;}
    // .NET numbers unnamed captures first, then named captures. ICU numbers
    // in source order. Reject numeric backreferences with named groups until
    // that syntax can be translated; match result numbering is remapped below.
    if(Pattern.Contains(TEXT("(?<")))for(int At=0;At+1<Pattern.Len();++At)if(Pattern[At]=='\\')
    {if(Pattern[At+1]>='1'&&Pattern[At+1]<='9'){uregex_close(Regex);Error=TEXT("Numeric backreferences mixed with named groups are unsupported");return nullptr;}++At;}
    uregex_setTimeLimit(Regex,2,&Status);uregex_setStackLimit(Regex,65536,&Status);
    if(U_FAILURE(Status)){uregex_close(Regex);Error=TEXT("Cannot bound regex execution");return nullptr;}
    return Regex;
}
#endif
bool Validate(const FString& Pattern,FString& Error)
{
    Error.Empty();
#if UE_ENABLE_ICU
    if(auto* Regex=Compile(Pattern,Error)){uregex_close(Regex);return true;}
#else
    Error=TEXT("This build lacks ICU regex support");
#endif
    return false;
}
bool Match(const FString& Pattern,const FString& Text,TMap<FString,FString>& Captures,FString& Error)
{
    Captures.Empty();Error.Empty();if(Text.Len()>4096){Error=TEXT("Regex input exceeds 4096 characters");return false;}
#if UE_ENABLE_ICU
    FString Normalized;auto* Regex=Compile(Pattern,Error,&Normalized);if(!Regex)return false;
    const auto Utf16=StringCast<UTF16CHAR>(*Text,Text.Len());UErrorCode Status=U_ZERO_ERROR;
    uregex_setText(Regex,reinterpret_cast<const UChar*>(Utf16.Get()),Utf16.Length(),&Status);
    const bool Found=uregex_find(Regex,0,&Status)!=0;
    if(Found&&U_SUCCESS(Status))
    {
        const int Count=uregex_groupCount(Regex,&Status);
        if(Count>32)Status=U_REGEX_STACK_OVERFLOW;
        TArray<int> Unnamed,Named;TMap<int,FString> Names;int SourceGroup=0;bool InClass=false;
        for(int At=0;At<Normalized.Len();++At)
        {
            if(Normalized[At]=='\\'){++At;continue;}
            if(Normalized[At]=='['){InClass=true;continue;}if(Normalized[At]==']'){InClass=false;continue;}
            if(InClass||Normalized[At]!='(')continue;
            if(At+1==Normalized.Len()||Normalized[At+1]!='?')Unnamed.Add(++SourceGroup);
            else if(Normalized.Mid(At,3)==TEXT("(?<")&&At+3<Normalized.Len()&&Normalized[At+3]!='='&&Normalized[At+3]!='!')
            {const int End=Normalized.Find(TEXT(">"),ESearchCase::CaseSensitive,ESearchDir::FromStart,At+3);if(End!=INDEX_NONE){Named.Add(++SourceGroup);Names.Add(SourceGroup,Normalized.Mid(At+3,End-At-3));}}
        }
        TArray<int> Order{0};Order.Append(Unnamed);Order.Append(Named);
        // Extended-mode comments or inline capture modifiers require a parser
        // we don't have. Never return silently misnumbered captures.
        if(SourceGroup!=Count)Status=U_UNSUPPORTED_ERROR;
        if(U_SUCCESS(Status))for(int I=0;I<=Count;++I)
        {
            const int Group=Order[I],Start=uregex_start(Regex,Group,&Status),End=uregex_end(Regex,Group,&Status);
            if(Start>=0&&End>=Start){const auto Value=StringCast<TCHAR>(Utf16.Get()+Start,End-Start);const FString Result(Value.Length(),Value.Get());Captures.Add(FString::FromInt(I),Result);if(const auto* Name=Names.Find(Group))Captures.Add(*Name,Result);}
        }
    }
    uregex_close(Regex);
    if(U_FAILURE(Status)){Error=FString::Printf(TEXT("Regex failed within its work limit: %s"),UTF8_TO_TCHAR(u_errorName(Status)));Captures.Empty();return false;}
    return Found;
#else
    Error=TEXT("This build lacks ICU regex support");return false;
#endif
}
}
