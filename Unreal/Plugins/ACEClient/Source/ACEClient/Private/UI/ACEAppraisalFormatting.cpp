#include "ACEAppraisalFormatting.h"
#include "UI/ACEUIResourceResolver.h"
namespace ACEAppraisalFormatting::Titles
{
#include "Protocol/ACECharacterTitleNames.inl"
}

ACEAppraisalFormatting::FCreatureHeadings ACEAppraisalFormatting::CreatureHeadings(
    const FACEAppraisalInfo& Info, UACEUIResourceResolver* Resources, const FACEWorldObject* Subject)
{
    FCreatureHeadings Result;
    if (Info.CreatureType && Resources) Result.Type = Resources->ResolveEnumString(0x10000005, Info.CreatureType);
    Result.Type.ReplaceInline(TEXT("_"), TEXT(" "));
    if (!UsesCharacterExamination(Info)) return Result;
    FString Gender = Resources ? Resources->ResolveEnumString(0x10000001, Info.IntProperties.FindRef(113)) : FString();
    FString Heritage = Resources ? Resources->ResolveEnumString(0x10000002, Info.IntProperties.FindRef(188)) : FString();
    if (!Info.IntProperties.FindRef(113)) Gender.Reset();
    if (!Info.IntProperties.FindRef(188)) Heritage = Result.Type;
    Gender.ReplaceInline(TEXT("_"), TEXT(" ")); Heritage.ReplaceInline(TEXT("_"), TEXT(" "));
    Result.Heritage = (Gender + TEXT(" ") + Heritage).TrimStartAndEnd();
    Result.Profession = Info.StringProperties.FindRef(5);
    if (const int32* Title = Info.IntProperties.Find(261))
    {
        const FString Name = Titles::GetCharacterTitleName(*Title);
        if (Name != TEXT("(Unknown Title)")) Result.Profession = Name;
    }
    const int32 PK = Info.IntProperties.FindRef(134);
    const bool bPK = Subject ? (Subject->ObjectDescriptionFlags & ACEObjectDescFlag::PlayerKiller) != 0 : (PK & ACEPlayerKillerStatus::PK) != 0;
    const bool bPKLite = Subject ? (Subject->ObjectDescriptionFlags & ACEObjectDescFlag::PkLiteStatus) != 0 : (PK & ACEPlayerKillerStatus::PKLite) != 0;
    Result.PlayerKiller = bPK ? TEXT("Player Killer") : bPKLite ? TEXT("Player Killer Lite") : TEXT("Non-Player Killer");
    if (Info.IntProperties.FindRef(30) > 0) Result.Allegiance = Info.StringProperties.FindRef(47);
    return Result;
}
