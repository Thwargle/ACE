#include "ACEUCMLootPresentation.h"
#include "Serialization/JsonSerializer.h"

namespace ACEUCMLootPresentation
{
namespace
{
#include "ACEUCMLootProperties.inl"
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
    return IntProperties;
}
FString PropertyLabel(const FString& Field,int32 Id)
{
    for(const auto& P:Properties(Field))if(P.Id==Id)return FString::Printf(TEXT("%s (%d)"),P.Name,Id);
    return FString::Printf(TEXT("Custom / unknown property (%d)"),Id);
}
const TMap<int32,FCalculated>& Calculated()
{
    static const TMap<int32,FCalculated> Types={
        {6,{TEXT("Damage percentage (retired in Classic; never matches)"),{TEXT("Minimum percentage")}}},
        {7,{TEXT("Object class"),{TEXT("Object class ID")}}},
        {10,{TEXT("Minimum weapon damage"),{TEXT("Minimum damage")}}},
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
        return Result;
    }
    if(Field==TEXT("spells"))return TEXT("Matching spells >= ")+Number(N(C,TEXT("value")))
        +TEXT("\nInclude regex: ")+(S(C,TEXT("pattern")).IsEmpty()?TEXT("any spell"):S(C,TEXT("pattern")))
        +TEXT("\nExclude regex: ")+(S(C,TEXT("exclude")).IsEmpty()?TEXT("none"):S(C,TEXT("exclude")));
    if(Field==TEXT("string"))return PropertyLabel(Field,Key)+TEXT(" matches regex: ")+S(C,TEXT("pattern"));
    const FString Op=S(C,TEXT("op"));
    const FString Symbol=Op==TEXT("ge")?TEXT(">="):Op==TEXT("le")?TEXT("<="):Op==TEXT("eq")?TEXT("="):Op==TEXT("ne")?TEXT("!="):Op==TEXT("bits")?TEXT("has any flag in"):Op;
    bool Zero=false;C->TryGetBoolField(TEXT("missing_zero"),Zero);
    return PropertyLabel(Field,Key)+TEXT(" ")+Symbol+TEXT(" ")+Number(N(C,TEXT("value")))
        +(Zero?TEXT(" | Missing value = 0"):TEXT(" | Missing value does not match"));
}
FString DataRequirement(const TSharedPtr<FJsonObject>& C)
{
    const FString Field=S(C,TEXT("field"));const int Key=int(N(C,TEXT("key")));
    if(Field==TEXT("legacy")&&Key>=1000&&Key<=1004)return TEXT("Character state");
    if(Field==TEXT("legacy")&&Key==6)return TEXT("Retired: never matches");
    if((Field==TEXT("string")&&Key==1)||(Field==TEXT("float")&&Key==167772169)
        ||(Field==TEXT("legacy")&&(Key==7||Key==17))
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
