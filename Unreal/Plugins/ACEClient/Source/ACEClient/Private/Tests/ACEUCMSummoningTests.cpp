#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"

namespace
{
TSharedPtr<FJsonObject> SummonJson(const TCHAR* Source)
{
    TSharedPtr<FJsonObject> Object;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Object);
    return Object;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMSummoningTest,"ACE.Plugins.Summoning",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMSummoningTest::RunTest(const FString&)
{
    FString Source,Error;
    if(!TestTrue(TEXT("Bundled UCM policy exists"),FFileHelper::LoadFileToString(Source,
        *(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")))))return false;
    const auto Snapshot=[](){return SummonJson(TEXT(R"({
        "time":100,"player":1,"level":100,"health":100,"max_health":100,"stamina":100,"max_stamina":100,
        "mana":100,"max_mana":100,"ready":true,"busy":false,"action_serial":0,"action_error":0,"combat_mode":8,
        "components_required":false,"skills":{"54":{"training":2,"current":615}},"cooldowns":{},"owned_pet":false,
        "spells":[{"id":30,"name":"Nether Bolt","category":640,"school":5,"power":300,"skill":500}],
        "inventory":[
            {"id":20,"wcid":20,"name":"Legendary Hideslasher Essence","type":128,"identified":true,"usable":true,
             "structure":35,"max_structure":50,"int_properties":{"280":213,"366":54,"367":615,"369":100}},
            {"id":50,"wcid":50,"name":"Nether Wand","type":32768,"identified":true,"can_wield":true,"equipped":true},
            {"id":51,"wcid":51,"name":"Other Wand","type":32768,"identified":true,"can_wield":true}],
        "targets":[{"id":500,"name":"Enemy","distance":10,"identified":true,"line_of_sight":true}]
    })"));};
    const auto Profile=[](){return SummonJson(TEXT(R"({"buffing":false,"recovery":false,"combat":"auto","looting":false,
        "summon_pets":true,"weapon_items":[20]})"));};
    const auto Step=[&](FACEPluginVM& VM,auto S,auto P)
    {
        TSharedPtr<FJsonObject> Intent;
        TestTrue(*FString::Printf(TEXT("Policy step: %s"),*Error),VM.Step(S,P,Intent,Error));
        return Intent?Intent:MakeShared<FJsonObject>();
    };
    const auto Action=[](auto I){FString Value;I->TryGetStringField(TEXT("action"),Value);return Value;};
    const auto ExpectCast=[&](auto I,const TCHAR* Message)
    {
        TestEqual(Message,Action(I),FString(TEXT("cast")));
        if(Action(I)==TEXT("cast"))TestEqual(TEXT("Uses the learned void attack"),I->GetNumberField(TEXT("spell")),30.);
    };
    {
        FACEPluginVM VM;TestTrue(TEXT("Summoning policy loads"),VM.Load(Source,Error));auto S=Snapshot(),P=Profile();
        auto I=Step(VM,S,P);
        TestEqual(TEXT("Level 100 essence is used at exactly 615 Summoning"),Action(I),FString(TEXT("use_item")));
        if(Action(I)==TEXT("use_item"))TestEqual(TEXT("Selected essence is the summon source"),I->GetNumberField(TEXT("item")),20.);
        S->SetNumberField(TEXT("time"),101);
        ExpectCast(Step(VM,S,P),TEXT("Combat continues while summon confirmation is pending"));
        S->SetNumberField(TEXT("time"),103);S->SetBoolField(TEXT("owned_pet"),true);
        S->GetObjectField(TEXT("cooldowns"))->SetNumberField(TEXT("213"),120);
        ExpectCast(Step(VM,S,P),TEXT("Combat continues during the shared summoning cooldown"));
        S->SetNumberField(TEXT("time"),224);S->GetObjectField(TEXT("cooldowns"))->SetNumberField(TEXT("213"),0);
        ExpectCast(Step(VM,S,P),TEXT("Expired cooldown does not replace a still-living pet"));
        S->SetNumberField(TEXT("time"),226);S->SetBoolField(TEXT("owned_pet"),false);
        TestEqual(TEXT("Summons again when both the pet and cooldown are gone"),Action(Step(VM,S,P)),FString(TEXT("use_item")));
    }
    for(int32 Case=0;Case<10;++Case)
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();
        auto Essence=S->GetArrayField(TEXT("inventory"))[0]->AsObject();
        if(Case==0)S->SetNumberField(TEXT("level"),99);
        if(Case==1)S->GetObjectField(TEXT("skills"))->GetObjectField(TEXT("54"))->SetNumberField(TEXT("current"),614);
        if(Case==2)S->GetObjectField(TEXT("skills"))->GetObjectField(TEXT("54"))->SetNumberField(TEXT("training"),1);
        if(Case==3)Essence->SetNumberField(TEXT("structure"),0);
        if(Case==4)S->GetObjectField(TEXT("cooldowns"))->SetNumberField(TEXT("213"),1);
        if(Case==5)S->SetBoolField(TEXT("owned_pet"),true);
        if(Case==6)Essence->SetBoolField(TEXT("usable"),false);
        if(Case==7)Essence->GetObjectField(TEXT("int_properties"))->SetNumberField(TEXT("362"),1);
        if(Case==8)P->SetBoolField(TEXT("summon_pets"),false);
        if(Case==9)P->SetNumberField(TEXT("pet_min_targets"),2);
        ExpectCast(Step(VM,S,P),*FString::Printf(TEXT("Unavailable or disabled pet never blocks combat (case %d)"),Case));
    }
    for(int32 Case=0;Case<7;++Case)
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();S->SetBoolField(TEXT("owned_pet"),true);
        if(Case==0){P->RemoveField(TEXT("weapon_items"));P->SetArrayField(TEXT("weapons"),{MakeShared<FJsonValueNumber>(20)});}
        if(Case==1)P->SetArrayField(TEXT("weapons"),{MakeShared<FJsonValueNumber>(20)});
        if(Case==2)P->SetArrayField(TEXT("weapon_items"),{MakeShared<FJsonValueNumber>(20),MakeShared<FJsonValueNumber>(51)});
        if(Case==3)P->SetArrayField(TEXT("weapons"),{MakeShared<FJsonValueNumber>(51)});
        if(Case==4)P->SetArrayField(TEXT("weapon_items"),{MakeShared<FJsonValueNumber>(20),MakeShared<FJsonValueNumber>(999)});
        if(Case==5)P->SetArrayField(TEXT("weapons"),{MakeShared<FJsonValueNumber>(999)});
        if(Case==6){P->SetArrayField(TEXT("weapons"),{MakeShared<FJsonValueNumber>(50)});P->SetArrayField(TEXT("weapon_items"),{MakeShared<FJsonValueNumber>(20),MakeShared<FJsonValueNumber>(51)});}
        auto I=Step(VM,S,P);
        if(Case<2)ExpectCast(I,TEXT("Essence-only class/GUID pools leave weapon selection automatic"));
        else if(Case<4)
        {
            TestEqual(TEXT("Explicit weapon selection stays restrictive"),Action(I),FString(TEXT("equip")));
            if(Action(I)==TEXT("equip"))TestEqual(TEXT("Only the selected weapon is equipped"),I->GetNumberField(TEXT("item")),51.);
        }
        else TestEqual(TEXT("Missing or conflicting weapon selections never broaden the pool"),Action(I),FString(TEXT("activity_failed")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();
        P->SetObjectField(TEXT("ucm_activity_failure"),SummonJson(TEXT(R"({"activity":"combat","status":"No eligible combat loadout"})")));
        TestEqual(TEXT("Combat failure is scoped"),Action(Step(VM,S,P)),FString(TEXT("activity_failed")));
        P->RemoveField(TEXT("ucm_activity_failure"));S->SetNumberField(TEXT("time"),101);
        TestEqual(TEXT("Combat retry delay cannot suppress an eligible summon"),Action(Step(VM,S,P)),FString(TEXT("use_item")));
    }
    for(bool NativeFailure:{false,true})
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();Step(VM,S,P);
        S->SetNumberField(TEXT("time"),101);
        if(NativeFailure)
        {
            P->SetObjectField(TEXT("ucm_activity_failure"),SummonJson(TEXT(R"({"activity":"pets","status":"Use rejected"})")));
            TestEqual(TEXT("Native pet rejection is scoped"),Action(Step(VM,S,P)),FString(TEXT("activity_failed")));
            P->RemoveField(TEXT("ucm_activity_failure"));
        }
        else {S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("action_error"),1);}
        ExpectCast(Step(VM,S,P),TEXT("Rejected summon cannot hang casting"));
        S->SetNumberField(TEXT("time"),132);S->SetNumberField(TEXT("action_error"),0);
        TestEqual(TEXT("Summon resumes after rejection backoff"),Action(Step(VM,S,P)),FString(TEXT("use_item")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();P->SetStringField(TEXT("combat"),TEXT("off"));
        TestEqual(TEXT("Summoning runs independently with combat disabled"),Action(Step(VM,S,P)),FString(TEXT("use_item")));
        S->SetNumberField(TEXT("time"),101);S->GetObjectField(TEXT("cooldowns"))->SetNumberField(TEXT("213"),1);
        TestTrue(TEXT("Independent summoning still obeys the server cooldown"),Action(Step(VM,S,P)).IsEmpty());
        S->SetNumberField(TEXT("time"),222);S->GetObjectField(TEXT("cooldowns"))->SetNumberField(TEXT("213"),0);
        TestEqual(TEXT("Independent summoning resumes after cooldown"),Action(Step(VM,S,P)),FString(TEXT("use_item")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();
        P->SetArrayField(TEXT("weapon_items"),{MakeShared<FJsonValueNumber>(50)});
        ExpectCast(Step(VM,S,P),TEXT("Unselected essences remain excluded from an explicit equipment pool"));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();
        P->SetArrayField(TEXT("weapons"),{MakeShared<FJsonValueNumber>(50)});
        TestEqual(TEXT("An explicitly selected essence works alongside a weapon class filter"),Action(Step(VM,S,P)),FString(TEXT("use_item")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();
        P->SetBoolField(TEXT("refill_summons"),true);P->SetArrayField(TEXT("weapons"),{MakeShared<FJsonValueNumber>(50)});
        auto Inv=S->GetArrayField(TEXT("inventory"));Inv[0]->AsObject()->SetNumberField(TEXT("structure"),0);
        Inv.Add(MakeShared<FJsonValueObject>(SummonJson(TEXT(R"({"id":21,"wcid":49485,"name":"Encapsulated Spirit","type":128,"usable":true})"))));
        S->SetArrayField(TEXT("inventory"),Inv);auto I=Step(VM,S,P);
        TestEqual(TEXT("Selected essence refill uses the same pool as summoning"),Action(I),FString(TEXT("apply_item")));
        if(Action(I)==TEXT("apply_item"))TestEqual(TEXT("Refill targets the selected essence"),I->GetNumberField(TEXT("target")),20.);
    }
    return true;
}
#endif
