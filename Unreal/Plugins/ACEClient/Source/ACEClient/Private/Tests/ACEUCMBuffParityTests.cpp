#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"

namespace
{
TSharedPtr<FJsonObject> BuffJson(const TCHAR* Text)
{TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMBuffParityTest,"ACE.Plugins.BuffParity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMBuffParityTest::RunTest(const FString&)
{
    FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto Snapshot=[](){return BuffJson(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"action_serial":0,"action_error":0,"combat_mode":8,"components_required":false,"inventory":[{"id":99,"type":32768,"equipped":true,"can_wield":true}],"spells":[{"id":10,"category":1,"power":100,"skill":300,"school":4,"duration":1800,"self_buff":true,"beneficial":true,"caster_target":true}],"enchantments":[]})"));};
    auto Profile=[](){return BuffJson(TEXT(R"({"buffing":true,"recovery":false,"combat":"off","refresh_seconds":60})"));};
    auto Step=[&](FACEPluginVM& VM,auto S,auto P){TSharedPtr<FJsonObject> I;TestTrue(*Error,VM.Step(S,P,I,Error));return I?I:MakeShared<FJsonObject>();};
    auto Start=[&](FACEPluginVM& VM){TestTrue(TEXT("Buff policy loads"),VM.Load(Source,Error));};
    auto Enchant=[](auto S,double Remaining,int Power=100)
    {auto E=BuffJson(TEXT(R"({"id":10,"category":1,"power":100,"duration":1800})"));E->SetNumberField(TEXT("remaining"),Remaining);E->SetNumberField(TEXT("power"),Power);S->SetArrayField(TEXT("enchantments"),{MakeShared<FJsonValueObject>(E)});};
    // A fresh VM represents both restarting UCM and loading after login.
    for(double Remaining:{1800.,301.,61.,60.,59.,1.,0.})
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();Enchant(S,Remaining);
        TestEqual(TEXT("Configured refresh threshold is honored before expiry on fresh login"),Step(VM,S,P)->HasField(TEXT("spell")),Remaining<=60.);
    }
    for(int Power:{99,100,200})
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();Enchant(S,1000,Power);
        TestEqual(TEXT("Equal and stronger buffs suppress recasting; weaker buffs are upgraded"),Step(VM,S,P)->HasField(TEXT("spell")),Power<100);
    }
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();P->RemoveField(TEXT("refresh_seconds"));Enchant(S,299);
        TestTrue(TEXT("New configuration uses VT's five-minute refresh default"),Step(VM,S,P)->HasField(TEXT("spell")));
    }
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();Enchant(S,1000);
        S->SetNumberField(TEXT("force_buff_request"),1);
        TestTrue(TEXT("Explicit Force Buff still refreshes a healthy timer"),Step(VM,S,P)->HasField(TEXT("spell")));
        S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("last_spell"),10);S->SetBoolField(TEXT("last_spell_confirmed"),true);
        TestEqual(TEXT("Force Buff completes after confirmation instead of starting again"),Step(VM,S,P)->GetStringField(TEXT("action")),FString(TEXT("force_buff_done")));
        S->SetNumberField(TEXT("force_buff_request"),0); // Host consumes force_buff_done.
        TestFalse(TEXT("The same force request is not repeated"),Step(VM,S,P)->HasField(TEXT("spell")));
    }
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();Step(VM,S,P);
        S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("last_spell"),10);S->SetBoolField(TEXT("last_spell_confirmed"),true);
        TestFalse(TEXT("Result before timer packet cannot immediately recast the same buff"),Step(VM,S,P)->HasField(TEXT("spell")));
        Enchant(S,1799);S->SetNumberField(TEXT("time"),101);
        TestFalse(TEXT("Delayed authoritative timer completes the buff"),Step(VM,S,P)->HasField(TEXT("spell")));
        S->SetArrayField(TEXT("enchantments"),{});S->SetNumberField(TEXT("time"),103);
        TestTrue(TEXT("A removed or dispelled character buff is not masked by a guessed timer"),Step(VM,S,P)->HasField(TEXT("spell")));
    }
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();Step(VM,S,P);Enchant(S,1799);
        auto Spells=S->GetArrayField(TEXT("spells"));auto Next=MakeShared<FJsonObject>(*Spells[0]->AsObject());Next->SetNumberField(TEXT("id"),11);Next->SetNumberField(TEXT("category"),3);Spells.Add(MakeShared<FJsonValueObject>(Next));S->SetArrayField(TEXT("spells"),Spells);
        auto Active=S->GetArrayField(TEXT("enchantments"));auto E=BuffJson(TEXT(R"({"id":11,"category":3,"power":100,"remaining":80,"duration":1800})"));Active.Add(MakeShared<FJsonValueObject>(E));S->SetArrayField(TEXT("enchantments"),Active);
        S->SetNumberField(TEXT("time"),101);
        TestEqual(TEXT("An ongoing buff cycle groups a neighbouring expiration"),Step(VM,S,P)->GetNumberField(TEXT("spell")),11.);
        FACEPluginVM Fresh;Start(Fresh);TestFalse(TEXT("The grouping window does not survive restart"),Step(Fresh,S,P)->HasField(TEXT("spell")));
        S->SetNumberField(TEXT("time"),140);TestFalse(TEXT("Grouping ends thirty seconds after the last cast"),Step(VM,S,P)->HasField(TEXT("spell")));
    }
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();auto Spell=S->GetArrayField(TEXT("spells"))[0]->AsObject();
        Spell->SetNumberField(TEXT("category"),69);S->SetArrayField(TEXT("usable_skills"),{MakeShared<FJsonValueNumber>(22)});
        TestFalse(TEXT("An untrained usable skill does not add automatic buffs"),Step(VM,S,P)->HasField(TEXT("spell")));
        S->SetArrayField(TEXT("trained_skills"),{MakeShared<FJsonValueNumber>(22)});
        TestTrue(TEXT("Training the skill enables its automatic buff"),Step(VM,S,P)->HasField(TEXT("spell")));
        Spell->SetNumberField(TEXT("category"),999);S->SetNumberField(TEXT("time"),120);
        TestFalse(TEXT("Unrelated beneficial families are not automatically cast"),Step(VM,S,P)->HasField(TEXT("spell")));
        P->SetArrayField(TEXT("buffs"),{MakeShared<FJsonValueNumber>(10)});
        TestTrue(TEXT("Explicitly included custom families still work"),Step(VM,S,P)->HasField(TEXT("spell")));
    }
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();auto Spell=S->GetArrayField(TEXT("spells"))[0]->AsObject();Spell->SetNumberField(TEXT("flags"),0x2000);
        S->SetBoolField(TEXT("in_fellowship"),true);
        TestFalse(TEXT("A fellowship spell never silently replaces an automatic self buff"),Step(VM,S,P)->HasField(TEXT("spell")));
        P->SetArrayField(TEXT("buffs"),{MakeShared<FJsonValueNumber>(10)});
        TestTrue(TEXT("Explicit fellowship buff is allowed in a fellowship"),Step(VM,S,P)->HasField(TEXT("spell")));
    }
    // Consumable maintenance must use the same capped window as NeedBuff.
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();P->SetBoolField(TEXT("auto_buffs"),false);P->SetNumberField(TEXT("refresh_seconds"),300);
        P->SetArrayField(TEXT("buff_items"),{MakeShared<FJsonValueObject>(BuffJson(TEXT(R"({"name":"Buff gem","spell":10})")))});
        auto Items=S->GetArrayField(TEXT("inventory"));Items.Add(MakeShared<FJsonValueObject>(BuffJson(TEXT(R"({"id":50,"name":"Buff gem","usable":true,"count":10})"))));S->SetArrayField(TEXT("inventory"),Items);
        S->GetArrayField(TEXT("spells"))[0]->AsObject()->SetNumberField(TEXT("duration"),120);Enchant(S,119);S->GetArrayField(TEXT("enchantments"))[0]->AsObject()->SetNumberField(TEXT("duration"),120);
        TestFalse(TEXT("Fresh short-duration consumable is not consumed again"),Step(VM,S,P)->HasField(TEXT("action")));
        FACEPluginVM Augmented;Start(Augmented);
        Enchant(S,100);S->GetArrayField(TEXT("enchantments"))[0]->AsObject()->SetNumberField(TEXT("duration"),240);
        TestEqual(TEXT("Consumable refresh uses the active server duration, not its shorter base duration"),Step(Augmented,S,P)->GetNumberField(TEXT("item")),50.);
        Enchant(S,59);S->GetArrayField(TEXT("enchantments"))[0]->AsObject()->SetNumberField(TEXT("duration"),120);
        TestEqual(TEXT("Consumable refreshes at its actual maintenance threshold"),Step(VM,S,P)->GetNumberField(TEXT("item")),50.);
    }
    for(bool Excluded:{false,true})for(int Margin:{5,30})
    {
        FACEPluginVM VM;Start(VM);auto S=Snapshot(),P=Profile();P->SetBoolField(TEXT("auto_buffs"),false);P->SetBoolField(TEXT("buff_others"),true);P->SetNumberField(TEXT("buff_skill_margin"),Margin);P->SetNumberField(TEXT("skill_margin"),60);
        if(Excluded)P->SetArrayField(TEXT("excluded_buff_spells"),{MakeShared<FJsonValueNumber>(10)});
        S->SetObjectField(TEXT("buff_request"),BuffJson(TEXT(R"({"id":1,"player":2,"name":"Visitor","present":true,"distance":5,"expires":1000,"started":true,"role":"mage"})")));
        auto Spell=S->GetArrayField(TEXT("spells"))[0]->AsObject();Spell->SetBoolField(TEXT("self_buff"),false);Spell->SetBoolField(TEXT("caster_target"),false);Spell->SetNumberField(TEXT("skill"),110);
        auto I=Step(VM,S,P);TestEqual(TEXT("Other buffs honor the buff margin and spell-family exclusions"),I->HasField(TEXT("spell")),!Excluded&&Margin==5);
        if(I->HasField(TEXT("spell")))
        {
            TestEqual(TEXT("Other buff targets the requester"),I->GetNumberField(TEXT("target")),2.);
            TestFalse(TEXT("Other buff waits for server confirmation"),Step(VM,S,P)->HasField(TEXT("spell")));
            S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("last_spell"),10);S->SetBoolField(TEXT("last_spell_confirmed"),true);
            TestEqual(TEXT("Other buff does not repeat a confirmed family"),Step(VM,S,P)->GetStringField(TEXT("action")),FString(TEXT("buff_request_done")));
        }
    }
    return true;
}
#endif
