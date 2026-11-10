#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPluginEquipment.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"

namespace
{
// Only combat qualities are retained. No salvage rules, spells, mana, charges,
// inventory locations or player eligibility can be restored from this cache.
constexpr uint32 IntKeys[]={45,47,158,159,160,166,179,263,270,271,272,273,274,275,276,277,278,353};
constexpr uint32 FloatKeys[]={136,138,147,152,155};
bool CombatEquipment(const FACEWorldObject& Item)
{
    return (Item.ItemType&(ACEItemType::Caster|ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon))
        || (Item.ValidLocations&(0x200000|0x800000));
}
FString CachePath(const FString& Root,const FString& Server,int32 Player)
{
    return Root/TEXT("EquipmentAssessments")/(FMD5::HashAnsiString(*Server)+FString::Printf(TEXT(".%08X.json"),uint32(Player)));
}
double Number(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,double Default=0)
{
    double Value=Default;J->TryGetNumberField(Key,Value);return FMath::IsFinite(Value)?Value:Default;
}
}

const FACEAppraisalInfo* UACEPluginSubsystem::GetEquipmentAppraisal(const FACEWorldObject& Item) const
{
    if(const auto* Fresh=Appraisals.Find(Item.Guid);Fresh&&Fresh->bSuccess)return Fresh;
    const auto* Saved=EquipmentAssessments.Find(Item.Guid);
    if(!Saved||!CombatEquipment(Item)||!IsOwnedPluginItem(Item)
        ||Saved->WCID!=Item.WeenieClassId||Saved->Name!=Item.Name||Saved->Type!=Item.ItemType
        ||Saved->Setup!=Item.SetupId||Saved->Icon!=Item.IconId||Saved->Material!=Item.MaterialType
        ||FDateTime::UtcNow().ToUnixTimestampDecimal()-Saved->SavedAt>30*86400)return nullptr;
    return &Saved->Info;
}

void UACEPluginSubsystem::RememberEquipmentAppraisal(const FACEWorldObject& Item,const FACEAppraisalInfo& Info)
{
    if(!Info.bSuccess||!CombatEquipment(Item)||!IsOwnedPluginItem(Item))return;
    if(!EquipmentAssessments.Contains(Item.Guid)&&EquipmentAssessments.Num()>=512)
    {
        int32 Oldest=0;double Time=MAX_dbl;
        for(const auto& Pair:EquipmentAssessments)if(Pair.Value.SavedAt<Time){Oldest=Pair.Key;Time=Pair.Value.SavedAt;}
        EquipmentAssessments.Remove(Oldest);
    }
    FEquipmentAssessment Record;
    Record.Name=Item.Name;Record.WCID=Item.WeenieClassId;Record.Type=Item.ItemType;
    Record.Setup=Item.SetupId;Record.Icon=Item.IconId;Record.Material=Item.MaterialType;
    Record.SavedAt=FDateTime::UtcNow().ToUnixTimestampDecimal();
    auto& A=Record.Info;A.ObjectGuid=Item.Guid;A.bSuccess=true;A.Name=Item.Name;
    A.Damage=Info.Damage;A.DamageType=Info.DamageType;A.WeaponTime=Info.WeaponTime;
    A.WeaponSkill=Info.WeaponSkill;A.WeaponDamageMod=Info.WeaponDamageMod;A.DamageVariance=Info.DamageVariance;
    for(uint32 Key:IntKeys)if(const auto* Value=Info.IntProperties.Find(Key))A.IntProperties.Add(Key,*Value);
    for(uint32 Key:FloatKeys)if(const auto* Value=Info.FloatProperties.Find(Key))A.FloatProperties.Add(Key,*Value);
    EquipmentAssessments.Add(Item.Guid,MoveTemp(Record));EquipmentAssessmentsDirty=true;
}

void UACEPluginSubsystem::SaveEquipmentAppraisals()
{
    if(!DataPlayer||!EquipmentAssessmentsDirty)return;
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("version"),1);
    TArray<TSharedPtr<FJsonValue>> Entries;
    for(const auto& Pair:EquipmentAssessments)
    {
        const auto& R=Pair.Value;const auto& A=R.Info;auto J=MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("id"),uint32(Pair.Key));J->SetStringField(TEXT("name"),R.Name);
        J->SetNumberField(TEXT("wcid"),R.WCID);J->SetNumberField(TEXT("type"),R.Type);
        J->SetNumberField(TEXT("setup"),R.Setup);J->SetNumberField(TEXT("icon"),R.Icon);J->SetNumberField(TEXT("material"),R.Material);
        J->SetNumberField(TEXT("saved"),R.SavedAt);J->SetNumberField(TEXT("damage"),A.Damage);
        J->SetNumberField(TEXT("damage_type"),A.DamageType);J->SetNumberField(TEXT("time"),A.WeaponTime);
        J->SetNumberField(TEXT("skill"),A.WeaponSkill);J->SetNumberField(TEXT("modifier"),A.WeaponDamageMod);
        J->SetNumberField(TEXT("variance"),A.DamageVariance);
        auto Ints=MakeShared<FJsonObject>(),Floats=MakeShared<FJsonObject>();
        for(const auto& P:A.IntProperties)Ints->SetNumberField(FString::FromInt(P.Key),P.Value);
        for(const auto& P:A.FloatProperties)Floats->SetNumberField(FString::FromInt(P.Key),P.Value);
        J->SetObjectField(TEXT("ints"),Ints);J->SetObjectField(TEXT("floats"),Floats);
        Entries.Add(MakeShared<FJsonValueObject>(J));
    }
    Root->SetArrayField(TEXT("items"),Entries);FString Text;
    if(!FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text)))return;
    const FString Path=CachePath(UserDirectory(),DataServer,DataPlayer),Temp=Path+TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    if(FFileHelper::SaveStringToFile(Text,*Temp)&&IFileManager::Get().Move(*Path,*Temp,true,true))EquipmentAssessmentsDirty=false;
    NextEquipmentAssessmentSave=FPlatformTime::Seconds()+5;
}

void UACEPluginSubsystem::LoadEquipmentAppraisals()
{
    EquipmentAssessments.Empty();EquipmentAssessmentsDirty=false;
    const FString Path=CachePath(UserDirectory(),DataServer,DataPlayer);FString Text;
    if(IFileManager::Get().FileSize(*Path)>8*1024*1024||!FFileHelper::LoadFileToString(Text,*Path))return;
    TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root
        ||Number(Root,TEXT("version"))!=1||!Root->TryGetArrayField(TEXT("items"),Entries)||Entries->Num()>512)return;
    const double Now=FDateTime::UtcNow().ToUnixTimestampDecimal();
    for(const auto& Value:*Entries)
    {
        if(Value->Type!=EJson::Object)continue;auto J=Value->AsObject();
        const double Id=Number(J,TEXT("id")),Saved=Number(J,TEXT("saved"));
        if(Id<1||Id>MAX_uint32||Id!=FMath::FloorToDouble(Id)||Saved<Now-30*86400||Saved>Now+300)continue;
        FEquipmentAssessment R;J->TryGetStringField(TEXT("name"),R.Name);if(R.Name.IsEmpty()||R.Name.Len()>256)continue;
        // Identity and combat integers must be integral int32 values.
        bool Valid=true;
        auto Integer=[&](const TSharedPtr<FJsonObject>& O,const TCHAR* Key)
        {const double N=Number(O,Key);if(N<MIN_int32||N>MAX_int32||N!=FMath::FloorToDouble(N)){Valid=false;return 0;}return int32(N);};
        R.WCID=Integer(J,TEXT("wcid"));R.Type=Integer(J,TEXT("type"));R.Setup=Integer(J,TEXT("setup"));
        R.Icon=Integer(J,TEXT("icon"));R.Material=Integer(J,TEXT("material"));R.SavedAt=Saved;
        auto& A=R.Info;A.ObjectGuid=int32(uint32(Id));A.Name=R.Name;A.bSuccess=true;
        A.Damage=Integer(J,TEXT("damage"));A.DamageType=Integer(J,TEXT("damage_type"));A.WeaponTime=Integer(J,TEXT("time"));
        A.WeaponSkill=Integer(J,TEXT("skill"));A.WeaponDamageMod=Number(J,TEXT("modifier"));A.DamageVariance=FMath::Clamp(Number(J,TEXT("variance")),0.,1.);
        const TSharedPtr<FJsonObject>* Ints=nullptr,*Floats=nullptr;
        if(!J->TryGetObjectField(TEXT("ints"),Ints)||!J->TryGetObjectField(TEXT("floats"),Floats))continue;
        for(uint32 Key:IntKeys){const FString K=FString::FromInt(Key);if((*Ints)->HasField(K))A.IntProperties.Add(Key,Integer(*Ints,*K));}
        for(uint32 Key:FloatKeys){const FString K=FString::FromInt(Key);if((*Floats)->HasField(K))A.FloatProperties.Add(Key,Number(*Floats,*K));}
        if(Valid)EquipmentAssessments.Add(A.ObjectGuid,MoveTemp(R));
    }
}

void UACEPluginSubsystem::AddEquipmentSnapshot(const FACEWorldObject& Item,const TSharedPtr<FJsonObject>& Out) const
{
    const auto* A=CombatEquipment(Item)?GetEquipmentAppraisal(Item):nullptr;
    Out->SetBoolField(TEXT("combat_identified"),A!=nullptr);
    const auto* Fresh=Appraisals.Find(Item.Guid);const bool Cached=A&&(!Fresh||!Fresh->bSuccess);
    Out->SetBoolField(TEXT("combat_appraisal_cached"),Cached);
    if(!Cached)return;
    auto Ints=MakeShared<FJsonObject>(),Floats=MakeShared<FJsonObject>();
    for(const auto& P:A->IntProperties)Ints->SetNumberField(FString::FromInt(P.Key),P.Value);
    for(const auto& P:A->FloatProperties)Floats->SetNumberField(FString::FromInt(P.Key),P.Value);
    Out->SetObjectField(TEXT("combat_int_properties"),Ints);Out->SetObjectField(TEXT("combat_float_properties"),Floats);
    auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();bool ServerCheck=false;
    const FString Failure=Out->GetBoolField(TEXT("equipped"))?FString():ACEPluginEquipment::WieldFailure(*A,Client->GetPlayerVitalsView(),&ServerCheck);
    Out->SetBoolField(TEXT("can_wield"),Failure.IsEmpty());Out->SetBoolField(TEXT("wield_requires_server_check"),ServerCheck);
    if(!Failure.IsEmpty())Out->SetStringField(TEXT("wield_reason"),Failure);
    Out->SetNumberField(TEXT("damage_type"),(Item.ItemType&ACEItemType::Caster)?A->IntProperties.FindRef(45):A->DamageType);
    Out->SetNumberField(TEXT("damage"),A->Damage);Out->SetNumberField(TEXT("damage_mod"),A->WeaponDamageMod);
    Out->SetNumberField(TEXT("weapon_time"),A->WeaponTime);Out->SetNumberField(TEXT("weapon_skill"),A->WeaponSkill);
    Out->SetNumberField(TEXT("variance"),A->DamageVariance);Out->SetNumberField(TEXT("weapon_type"),A->IntProperties.FindRef(353));
    Out->SetNumberField(TEXT("attack_type"),A->IntProperties.FindRef(47));Out->SetNumberField(TEXT("slayer"),A->IntProperties.FindRef(166));
    Out->SetNumberField(TEXT("slayer_mod"),A->FloatProperties.FindRef(138));Out->SetNumberField(TEXT("element_mod"),A->FloatProperties.FindRef(152));
}
