#pragma once
#include "ACEDatSubsystem.h"
#include "ACEOpcodes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

namespace ACEPluginCombat
{
inline TArray<TSharedPtr<FJsonValue>> PreferredElements(const FString& Name)
{
    static const TSharedPtr<FJsonObject> Catalog=[]()
    {
        TSharedPtr<FJsonObject> Result;FString Text;
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("ACEClient"));
        if(Plugin&&FFileHelper::LoadFileToString(Text,*(Plugin->GetBaseDir()/TEXT("ClientMods/ucm/monster-elements.json"))))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Result);
        return Result;
    }();
    const TSharedPtr<FJsonObject>* Monsters=nullptr;const TArray<TSharedPtr<FJsonValue>>* Elements=nullptr;
    if(Catalog&&Catalog->TryGetObjectField(TEXT("monsters"),Monsters)&&(*Monsters)->TryGetArrayField(Name,Elements))return *Elements;
    return {};
}
inline int32 AttackHeight(UACEDatSubsystem& Dat,const FACEWorldObject& Target,const FACEPosition& Player)
{
    float Step=0,Height=1.8f,Radius=0,SelectionRadius=0;uint32 Anim=0;
    FVector3f SelectionOrigin=FVector3f::ZeroVector;
    Dat.TryGetSetupPhysics(Target.SetupId,Step,Height,Radius,Anim,nullptr,&SelectionOrigin,&SelectionRadius);
    // Retail's setup selection origin includes low bodies and elevated flyers.
    // Apply the server's scale and relative elevation, rather than a species list.
    const float Center=SelectionRadius>0?SelectionOrigin.Z:Height*.6f;
    const double AimHeight=Center*Target.GetValidObjectScale()
        +(Target.Position.ToUnrealLocation().Z-Player.ToUnrealLocation().Z)/100.;
    return AimHeight<.65?ACEAttackHeight::Low:AimHeight>1.45?ACEAttackHeight::High:ACEAttackHeight::Medium;
}
}
