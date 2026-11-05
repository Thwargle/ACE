#pragma once

#include "ACETypes.h"

namespace ACEPluginEquipment
{
// The same four requirement triplets are checked by ACE Player.CheckWieldRequirement,
// including ILT's Int64Stat extension (13), keyed by PropertyInt64 rather than Int.
// Return the failed check so a custom item never disappears behind a generic
// loadout error. Unknown kinds remain blocked. ILT does not replicate all Int64
// counters at login, so unreported counts must be checked by the server on equip.
inline FString WieldFailure(const FACEAppraisalInfo& Appraisal,const FACEPlayerVitals& Vitals,bool* RequiresServerCheck=nullptr)
{
    if(RequiresServerCheck)*RequiresServerCheck=false;
    const int32 Keys[]={158,270,273,276};
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Keys);++Index)
    {
        const int32 Key=Keys[Index],Kind=Appraisal.IntProperties.FindRef(Key);
        const int32 Id=Appraisal.IntProperties.FindRef(Key+1),Required=Appraisal.IntProperties.FindRef(Key+2);
        if(!Kind)continue;
        int64 Actual=0;bool Equal=false;FString Label;
        switch(Kind)
        {
        case 1: case 2: case 8:
            Label=FString::Printf(TEXT("%s skill %d"),Kind==1?TEXT("current"):Kind==2?TEXT("base"):TEXT("training for"),Id);
            for(const auto& Skill:Vitals.Skills)if(Skill.SkillId==Id)
            {Actual=Kind==1?Skill.Current:Kind==2?Skill.Base:Skill.AdvancementClass;break;}
            break;
        case 3: case 4:
            Label=FString::Printf(TEXT("%s attribute %d"),Kind==3?TEXT("current"):TEXT("base"),Id);
            Actual=Kind==3?Vitals.GetAttributeCurrent(Id):Vitals.GetAttributeBase(Id);break;
        case 5: case 6:
            Label=FString::Printf(TEXT("%s vital %d"),Kind==5?TEXT("maximum"):TEXT("base"),Id);
            Actual=Kind==5?(Id==1?Vitals.MaxHealth:Id==3?Vitals.MaxStamina:Id==5?Vitals.MaxMana:0):Vitals.GetVitalBase(Id);break;
        case 7: Label=TEXT("level");Actual=Vitals.Level;break;
        case 9: Label=FString::Printf(TEXT("integer property %d"),Id);Actual=Vitals.StatQualityInts.FindRef(Id);break;
        case 10:
            Label=FString::Printf(TEXT("boolean property %d"),Id);Actual=Vitals.QualityBools.FindRef(Id)?1:0;
            if(Actual==(Required!=0?1:0))continue;
            return FString::Printf(TEXT("Wield requirement %d: %s must be %s (have %s)"),Index+1,*Label,Required?TEXT("true"):TEXT("false"),Actual?TEXT("true"):TEXT("false"));
        case 11: Label=TEXT("creature type");Actual=Vitals.StatQualityInts.FindRef(2);Equal=true;break;
        case 12: Label=TEXT("heritage");Actual=Vitals.HeritageGroup;Equal=true;break;
        case 13:
            if(const int64* Count=Vitals.QualityInt64s.Find(Id))Actual=*Count;
            else {if(RequiresServerCheck)*RequiresServerCheck=true;continue;}
            Label=FString::Printf(TEXT("64-bit integer property %d"),Id);break;
        default:
            return FString::Printf(TEXT("Wield requirement %d: unsupported type %d (property %d, value %d)"),Index+1,Kind,Id,Required);
        }
        if(Equal?Actual!=Required:Actual<Required)
            return FString::Printf(TEXT("Wield requirement %d: %s requires %d (have %lld)"),Index+1,*Label,Required,Actual);
    }
    return FString();
}
}
