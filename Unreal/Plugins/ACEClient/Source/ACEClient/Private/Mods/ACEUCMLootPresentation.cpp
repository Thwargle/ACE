#include "ACEUCMLootPresentation.h"
#include "Serialization/JsonSerializer.h"
#include "ACEVTProfile.h"

namespace ACEUCMLootPresentation
{
namespace
{
#include "ACEUCMLootProperties.inl"
// Virindi's Weapon Skill Ids reference. These are comparison values, not
// property keys; custom servers can still enter an ID outside this list.
const TArray<FProperty> WeaponSkills={
    {31,TEXT("Creature Enchantment")},{46,TEXT("Finesse Weapons")},
    {44,TEXT("Heavy Weapons")},{32,TEXT("Item Enchantment")},
    {33,TEXT("Life Magic")},{45,TEXT("Light Weapons")},
    {16,TEXT("Mana Conversion")},{47,TEXT("Missile Weapons")},
    {54,TEXT("Summoning")},{41,TEXT("Two-Handed Combat")},
    {43,TEXT("Void Magic")},{34,TEXT("War Magic")}
};
const TArray<FProperty> WeaponTypes={{0,TEXT("Undefined")},{1,TEXT("Unarmed")},{2,TEXT("Sword")},
    {3,TEXT("Axe")},{4,TEXT("Mace")},{5,TEXT("Spear")},{6,TEXT("Dagger")},{7,TEXT("Staff")},
    {8,TEXT("Bow")},{9,TEXT("Crossbow")},{10,TEXT("Thrown")},{11,TEXT("Two-handed")},{12,TEXT("Magic")}};
FString S(const TSharedPtr<FJsonObject>& O,const TCHAR* K){FString V;O->TryGetStringField(K,V);return V;}
double N(const TSharedPtr<FJsonObject>& O,const TCHAR* K){double V=0;O->TryGetNumberField(K,V);return V;}
FString Number(double V){return FString::Printf(TEXT("%.15g"),V);}
}
const TArray<FProperty>& Properties(const FString& Field)
{
    if(Field==TEXT("float"))return FloatProperties;
    if(Field==TEXT("string"))return StringProperties;
    if(Field==TEXT("class"))return ClassProperties;
    if(Field==TEXT("skill"))return SkillProperties;
    if(Field==TEXT("weapon_skill"))return WeaponSkills;
    if(Field==TEXT("weapon_type"))return WeaponTypes;
    return IntProperties;
}
FString PropertyLabel(const FString& Field,int32 Id)
{
    for(const auto& P:Properties(Field))if(P.Id==Id)return FString::Printf(TEXT("%s (%d)"),P.Name,Id);
    return FString::Printf(TEXT("%s (%d)"),Field==TEXT("weapon_skill")?TEXT("Custom / other skill"):TEXT("Custom / unknown property"),Id);
}
bool IsWeaponSkillProperty(const FString& Field,int32 Id)
{
    return Field==TEXT("int")&&(Id==218103840||Id==159||Id==48);
}
FString ValueKind(const FString& Field,int32 Id)
{
    if(IsWeaponSkillProperty(Field,Id))return TEXT("weapon_skill");
    return Field==TEXT("int")&&Id==353?TEXT("weapon_type"):FString();
}
bool ValidTextMatch(const TSharedPtr<FJsonObject>& C)
{
    const FString Field=S(C,TEXT("field")),Mode=C->HasField(TEXT("match_mode"))?S(C,TEXT("match_mode")):TEXT("regex");
    if(!TArray<FString>{TEXT("regex"),TEXT("contains"),TEXT("exact"),TEXT("prefix"),TEXT("any"),TEXT("all")}.Contains(Mode))return false;
    if(C->HasField(TEXT("ignore_case"))&&!C->HasTypedField<EJson::Boolean>(TEXT("ignore_case")))return false;
    for(const TCHAR* Key:{TEXT("pattern"),TEXT("exclude")})
    {
        if(C->HasField(Key)&&(!C->HasTypedField<EJson::String>(Key)||S(C,Key).Len()>2048))return false;
        if((Field!=TEXT("string")||Mode==TEXT("regex"))&&!ACEVTProfile::IsSupportedPattern(S(C,Key)))return false;
    }
    return true;
}
bool ValidColorMatch(const TSharedPtr<FJsonObject>& C)
{
    const int Key=int(N(C,TEXT("key")));const TArray<TSharedPtr<FJsonValue>>* Args=nullptr;
    if(!C->TryGetArrayField(TEXT("args"),Args)||Args->Num()!=(Key==16?6:5))return false;
    for(int I=0;I<Args->Num();++I)
    {
        if((*Args)[I]->Type!=EJson::Number)return false;const double V=(*Args)[I]->AsNumber();
        if(!FMath::IsFinite(V)||V<0||V>(I<3||I==5?255:I==3?360:2))return false;
        if((I<3||I==5)&&V!=FMath::FloorToDouble(V))return false;
    }
    if(Key==15)
    {
        const TArray<TSharedPtr<FJsonValue>>* Slots=nullptr;
        if(!C->TryGetArrayField(TEXT("slots"),Slots)||Slots->IsEmpty()||Slots->Num()>256||S(C,TEXT("group")).Len()>256)return false;
        for(const auto& Slot:*Slots)if(Slot->Type!=EJson::Number||Slot->AsNumber()<0||Slot->AsNumber()>255||Slot->AsNumber()!=FMath::FloorToDouble(Slot->AsNumber()))return false;
    }
    return true;
}
const TMap<FString,TArray<int32>>& ColorGroups()
{
    // Palette slot definitions shipped with VTClassic ColorSlots.Default.xml.
    static const TMap<FString,TArray<int32>> Groups={
#include "ACEVTColorGroups.inl"
    };
    return Groups;
}
const TMap<int32,FCalculated>& Calculated()
{
    static const TMap<int32,FCalculated> Types={
        {6,{TEXT("Damage percentage (retired in Classic; never matches)"),{TEXT("Minimum percentage")}}},
        {7,{TEXT("Object class"),{TEXT("Object class ID")}}},
        {10,{TEXT("Minimum weapon damage"),{TEXT("Minimum damage")}}},
        {14,{TEXT("Any similar palette color"),{TEXT("Red (0-255)"),TEXT("Green (0-255)"),TEXT("Blue (0-255)"),TEXT("Hue tolerance (degrees)"),TEXT("Saturation/value tolerance (0-1.42)")}}},
        {15,{TEXT("Armor group palette color"),{TEXT("Red (0-255)"),TEXT("Green (0-255)"),TEXT("Blue (0-255)"),TEXT("Hue tolerance (degrees)"),TEXT("Saturation/value tolerance (0-1.42)")}}},
        {16,{TEXT("Palette slot color"),{TEXT("Red (0-255)"),TEXT("Green (0-255)"),TEXT("Blue (0-255)"),TEXT("Hue tolerance (degrees)"),TEXT("Saturation/value tolerance (0-1.42)"),TEXT("Palette slot (starts at 0)")}}},
        {17,{TEXT("Exact palette"),{TEXT("Palette slot (starts at 0)"),TEXT("Palette ID")}}},
        {1000,{TEXT("Current character skill"),{TEXT("Minimum skill"),TEXT("Skill ID")}}},
        {1001,{TEXT("Main pack space"),{TEXT("Minimum empty slots")}}},
        {1002,{TEXT("Minimum character level"),{TEXT("Level")}}},
        {1003,{TEXT("Maximum character level"),{TEXT("Level")}}},
        {1004,{TEXT("Base character skill range"),{TEXT("Skill ID"),TEXT("Minimum"),TEXT("Maximum")}}},
        {2000,{TEXT("Buffed average melee damage"),{TEXT("Minimum average damage")}}},
        {2001,{TEXT("Buffed missile damage"),{TEXT("Minimum missile damage score")}}},
        {2003,{TEXT("Buffed integer property"),{TEXT("Minimum value"),TEXT("Property ID")}}},
        {2005,{TEXT("Buffed decimal property"),{TEXT("Minimum value"),TEXT("Property ID")}}},
        {2006,{TEXT("Potential tinkered melee damage"),{TEXT("Minimum damage score")}}},
        {2007,{TEXT("Total ratings"),{TEXT("Minimum combined ratings")}}},
        {2008,{TEXT("Potential tinkered melee targets"),{TEXT("Minimum damage score"),TEXT("Minimum melee defense multiplier"),TEXT("Minimum attack multiplier")}}}
    };
    return Types;
}
FString ConditionSummary(const TSharedPtr<FJsonObject>& C)
{
    if(!C)return TEXT("Invalid condition");
    const FString Field=S(C,TEXT("field"));const int Key=int(N(C,TEXT("key")));
    if(Field==TEXT("legacy"))
    {
        const auto* Type=Calculated().Find(Key);
        FString Result=Type?Type->Name:FString::Printf(TEXT("Requirement %d"),Key);
        const TArray<TSharedPtr<FJsonValue>>* Args=nullptr;
        if(C->TryGetArrayField(TEXT("args"),Args))for(int I=0;I<Args->Num();++I)
        {
            FString Value=Number((*Args)[I]->AsNumber());
            if(Key==7&&I==0)Value=PropertyLabel(TEXT("class"),int((*Args)[I]->AsNumber()));
            if((Key==1000&&I==1)||(Key==1004&&I==0))Value=PropertyLabel(TEXT("skill"),int((*Args)[I]->AsNumber()));
            if((Key==2003||Key==2005)&&I==1)Value=PropertyLabel(Key==2005?TEXT("float"):TEXT("int"),int((*Args)[I]->AsNumber()));
            Result+=TEXT("\n")+(Type&&Type->Arguments.IsValidIndex(I)?Type->Arguments[I]:FString::Printf(TEXT("Argument %d"),I+1))+TEXT(": ")+Value;
        }
        if(Key==15)Result+=TEXT("\nArmor group: ")+S(C,TEXT("group"));
        return Result;
    }
    if(Field==TEXT("spells"))return TEXT("Matching spells >= ")+Number(N(C,TEXT("value")))
        +TEXT("\nInclude regex: ")+(S(C,TEXT("pattern")).IsEmpty()?TEXT("any spell"):S(C,TEXT("pattern")))
        +TEXT("\nExclude regex: ")+(S(C,TEXT("exclude")).IsEmpty()?TEXT("none"):S(C,TEXT("exclude")));
    if(Field==TEXT("string"))
    {
        const FString Mode=C->HasField(TEXT("match_mode"))?S(C,TEXT("match_mode")):TEXT("regex");
        bool Ignore=Mode!=TEXT("regex");C->TryGetBoolField(TEXT("ignore_case"),Ignore);
        return PropertyLabel(Field,Key)+TEXT(" ")+Mode+TEXT(": ")+S(C,TEXT("pattern"))+(Ignore?TEXT(" (ignore case)"):TEXT(" (case sensitive)"));
    }
    const FString Op=S(C,TEXT("op"));
    const FString Symbol=Op==TEXT("ge")?TEXT(">="):Op==TEXT("le")?TEXT("<="):Op==TEXT("eq")?TEXT("="):Op==TEXT("ne")?TEXT("!="):Op==TEXT("bits")?TEXT("has any flag in"):Op;
    bool Zero=false;C->TryGetBoolField(TEXT("missing_zero"),Zero);
    const double Value=N(C,TEXT("value"));
    FString ValueLabel=Number(Value);
    const FString Kind=ValueKind(Field,Key);
    if(!Kind.IsEmpty())for(const auto& Skill:Properties(Kind))
        if(Value==Skill.Id){ValueLabel=PropertyLabel(Kind,Skill.Id);break;}
    if(Op==TEXT("in"))
    {
        ValueLabel.Reset();const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if(C->TryGetArrayField(TEXT("values"),Values))for(const auto& V:*Values)
        {if(!ValueLabel.IsEmpty())ValueLabel+=TEXT(" OR ");ValueLabel+=Kind.IsEmpty()?Number(V->AsNumber()):PropertyLabel(Kind,int32(V->AsNumber()));}
    }
    return PropertyLabel(Field,Key)+TEXT(" ")+Symbol+TEXT(" ")+ValueLabel
        +(Zero?TEXT(" | Missing value = 0"):TEXT(" | Missing value does not match"));
}
FString DataRequirement(const TSharedPtr<FJsonObject>& C)
{
    const FString Field=S(C,TEXT("field"));const int Key=int(N(C,TEXT("key")));
    if(Field==TEXT("legacy")&&Key>=1000&&Key<=1004)return TEXT("Character state");
    if(Field==TEXT("legacy")&&Key==6)return TEXT("Retired: never matches");
    if((Field==TEXT("string")&&(Key==1||Key==184549376))||(Field==TEXT("float")&&(Key==167772168||Key==167772169))
        ||(Field==TEXT("legacy")&&(Key==7||(Key>=14&&Key<=17)))
        ||(Field==TEXT("int")&&(Key==5||Key==19||Key==131
            ||TArray<int>{218103808,218103809,218103810,218103811,218103812,218103813,218103814,218103815,218103816,218103818,218103819,218103821,218103822,218103823,218103824,218103825,218103826,218103834,218103835,218103849,218103850}.Contains(Key))))
        return TEXT("Public item data");
    return TEXT("Appraisal may be needed");
}
TSharedPtr<FJsonObject> Clone(const TSharedPtr<FJsonObject>& Object)
{
    if(!Object)return nullptr;FString Json;TSharedPtr<FJsonObject> Copy;
    FJsonSerializer::Serialize(Object.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Copy);return Copy;
}
}
