#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/GameInstance.h"
#include "ACEDatSubsystem.h"
#include "Mods/ACEPluginVM.h"

namespace
{
TSharedPtr<FJsonObject> RecoveryJson(const TCHAR* Text)
{TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMRecoveryTest,"ACE.Plugins.Recovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMRecoveryTest::RunTest(const FString&)
{
    FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto Snapshot=[](){return RecoveryJson(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"nearest":0,"ready":true,"busy":false,"action_serial":0,"action_error":0,"combat_mode":8,"container":0,"components_required":false,"position":{"cell":2103705613,"x":25,"y":97,"z":12},"spells":[],"targets":[],"inventory":[{"id":99,"name":"Orb","type":32768,"equipped":true,"identified":true,"can_wield":true}]})"));};
    auto Profile=[](){return RecoveryJson(TEXT(R"({"buffing":false,"recovery":true,"combat":"off","skill_margin":30})"));};
    auto Step=[&](FACEPluginVM& VM,auto S,auto P){TSharedPtr<FJsonObject> I;TestTrue(*Error,VM.Step(S,P,I,Error));return I?I:MakeShared<FJsonObject>();};
    auto* Dat=NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    if(!TestTrue(TEXT("Retail DAT opens for real recovery spell metadata"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    TMap<FString,TSharedPtr<FJsonObject>> Spells;
    for(uint32 Id=1;Id<7000;++Id)
    {
        FString Name;uint32 Icon=0,School=0,Power=0,Category=0,Flags=0;double Duration=0;
        if(!Dat->TryGetSpellInfo(Id,Name,Icon)||!Dat->TryGetPluginSpellInfo(Id,School,Power,Category,Flags,Duration))continue;
        if(!(Flags&8))continue;
        auto Spell=MakeShared<FJsonObject>();Spell->SetNumberField(TEXT("id"),Id);Spell->SetStringField(TEXT("name"),Name);
        Spell->SetNumberField(TEXT("category"),Category);Spell->SetNumberField(TEXT("school"),School);Spell->SetNumberField(TEXT("power"),Power);
        Spell->SetNumberField(TEXT("skill"),600);Spell->SetNumberField(TEXT("duration"),Duration);Spell->SetBoolField(TEXT("caster_target"),true);
        Spell->SetBoolField(TEXT("self_buff"),(Flags&4)!=0&&Duration>0);
        Spell->SetBoolField(TEXT("beneficial"),(Flags&4)!=0);Spell->SetBoolField(TEXT("known"),true);
        uint32 Level=0;Dat->TryGetSpellSchoolAndLevel(Id,School,Level);Spell->SetNumberField(TEXT("level"),Level);
        TMap<FString,int32> Counts;auto Scarabs=MakeShared<FJsonObject>();
        Spell->SetBoolField(TEXT("components_known"),Dat->TryGetPluginSpellScarabs(Id,Counts));
        for(const auto& Count:Counts)Scarabs->SetNumberField(Count.Key,Count.Value);
        Spell->SetObjectField(TEXT("scarabs"),Scarabs);Spells.Add(Name,Spell);
    }
    for(const auto& Case:TArray<TPair<FString,FString>>{
        {TEXT("health"),TEXT("Heal Self I")},{TEXT("health"),TEXT("Heal Self II")},{TEXT("health"),TEXT("Heal Self III")},{TEXT("health"),TEXT("Heal Self IV")},{TEXT("health"),TEXT("Heal Self V")},{TEXT("health"),TEXT("Heal Self VI")},{TEXT("health"),TEXT("Adja's Intervention")},{TEXT("health"),TEXT("Incantation of Heal Self")},
        {TEXT("stamina"),TEXT("Revitalize Self I")},{TEXT("stamina"),TEXT("Revitalize Self II")},{TEXT("stamina"),TEXT("Revitalize Self III")},{TEXT("stamina"),TEXT("Revitalize Self IV")},{TEXT("stamina"),TEXT("Revitalize Self V")},{TEXT("stamina"),TEXT("Revitalize Self VI")},{TEXT("stamina"),TEXT("Robustification")},{TEXT("stamina"),TEXT("Incantation of Revitalize Self")},
        {TEXT("mana"),TEXT("Stamina to Mana Self I")},{TEXT("mana"),TEXT("Stamina to Mana Self II")},{TEXT("mana"),TEXT("Stamina to Mana Self III")},{TEXT("mana"),TEXT("Stamina to Mana Self IV")},{TEXT("mana"),TEXT("Stamina to Mana Self V")},{TEXT("mana"),TEXT("Stamina to Mana Self VI")},{TEXT("mana"),TEXT("Meditative Trance")},{TEXT("mana"),TEXT("Incantation of Stamina to Mana Self")}})
    {
        const auto* Spell=Spells.Find(Case.Value);if(!TestNotNull(*Case.Value,Spell))continue;
        for(bool Imported:{false,true})for(bool Combat:{false,true})
        {
            FACEPluginVM VM;TestTrue(TEXT("Recovery policy loads"),VM.Load(Source,Error));auto S=Snapshot(),P=Profile();S->SetNumberField(*Case.Key,20);
            S->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(*Spell)});
            if(Imported){auto H=RecoveryJson(TEXT(R"({"stance":1,"handler":"Regular Spell","min":0,"max":100})"));H->SetNumberField(TEXT("vital"),Case.Key==TEXT("health")?2:Case.Key==TEXT("stamina")?4:6);P->SetArrayField(TEXT("recharge_handlers"),{MakeShared<FJsonValueObject>(H)});}
            if(Combat){P->SetStringField(TEXT("combat"),TEXT("magic"));S->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueObject>(RecoveryJson(TEXT(R"({"id":500,"name":"Rat","distance":5,"line_of_sight":true,"identified":true})")))});}
            const auto I=Step(VM,S,P);double CastSpell=0;I->TryGetNumberField(TEXT("spell"),CastSpell);
            TestEqual(*(TEXT("Recovery precedes combat and supports imported handler: ")+Case.Value),CastSpell,(*Spell)->GetNumberField(TEXT("id")));
            TestEqual(TEXT("Recovery targets the player"),I->GetNumberField(TEXT("target")),1.);
        }
    }
    for(int Vital:{2,4,6})
    {
        FACEPluginVM V;V.Load(Source,Error);auto State=Snapshot(),Settings=Profile();State->SetNumberField(Vital==2?TEXT("health"):Vital==4?TEXT("stamina"):TEXT("mana"),20);
        auto Item=RecoveryJson(TEXT(R"({"id":42,"wcid":42,"name":"Potion","type":32,"usable":true,"identified":false,"count":10})"));
        State->SetArrayField(TEXT("inventory"),{MakeShared<FJsonValueObject>(Item)});
        auto I=Step(V,State,Settings);TestEqual(TEXT("Unappraised recovery supply is identified"),I->GetStringField(TEXT("action")),FString(TEXT("identify")));
        Item->SetBoolField(TEXT("identified"),true);Item->SetNumberField(TEXT("boost_vital"),Vital);Item->SetNumberField(TEXT("boost"),50);
        TestEqual(TEXT("Appraised supply is automatically used"),Step(V,State,Settings)->GetStringField(TEXT("action")),FString(TEXT("use_item")));
    }
    {
        auto State=Snapshot(),Settings=Profile();State->SetNumberField(TEXT("mana"),20);
        State->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Spells[TEXT("Meditative Trance")]),MakeShared<FJsonValueObject>(Spells[TEXT("Incantation of Stamina to Mana Self")])});
        auto Choose=[&](){FACEPluginVM V;V.Load(Source,Error);return Step(V,State,Settings);};
        TestEqual(TEXT("Strongest eligible recovery tier is selected"),Choose()->GetNumberField(TEXT("spell")),Spells[TEXT("Incantation of Stamina to Mana Self")]->GetNumberField(TEXT("id")));
        Settings->SetNumberField(TEXT("skill_margin"),250);
        TestEqual(TEXT("Skill buffer still restricts recovery tier"),Choose()->GetNumberField(TEXT("spell")),Spells[TEXT("Meditative Trance")]->GetNumberField(TEXT("id")));
        State->SetNumberField(TEXT("stamina"),20);TestFalse(TEXT("Mana conversion preserves depleted stamina"),Choose()->HasField(TEXT("action")));
        TestTrue(TEXT("No available recovery method explains the problem"),Choose()->GetStringField(TEXT("status")).StartsWith(TEXT("Recovery unavailable")));
        State->SetNumberField(TEXT("stamina"),100);Settings->SetBoolField(TEXT("recovery"),false);TestFalse(TEXT("Recovery toggle remains authoritative"),Choose()->HasField(TEXT("action")));
    }
    // Buff priority must not depend on DAT IDs or spellbook enumeration.
    // Match VT's opening sequence, then the remaining magic schools.
    for(bool Forced:{false,true})
    {
        const TArray<int32> Order={43,9,11,51,47,45,49,645,1};
        TArray<TSharedPtr<FJsonValue>> Book,Enchantments;
        TMap<int32,TSharedPtr<FJsonObject>> Families;
        for(int32 Category:Order)
        {
            for(const auto& Pair:Spells)
            {
                const auto V=Pair.Value;
                if(V->GetNumberField(TEXT("category"))==Category&&V->GetNumberField(TEXT("level"))==1
                    &&V->GetNumberField(TEXT("duration"))>=300&&V->GetNumberField(TEXT("school"))==4)
                {Families.Add(Category,V);break;}
            }
            if(!TestTrue(TEXT("Retail casting-priority buff metadata exists"),Families.Contains(Category)))return false;
        }
        for(int32 Index=Order.Num()-1;Index>=0;--Index)Book.Add(MakeShared<FJsonValueObject>(Families[Order[Index]]));
        auto State=Snapshot(),Settings=Profile();Settings->SetBoolField(TEXT("buffing"),true);
        State->SetArrayField(TEXT("spells"),Book);
        State->SetArrayField(TEXT("trained_skills"),{MakeShared<FJsonValueNumber>(31),MakeShared<FJsonValueNumber>(32),MakeShared<FJsonValueNumber>(33),MakeShared<FJsonValueNumber>(34),MakeShared<FJsonValueNumber>(16),MakeShared<FJsonValueNumber>(43)});
        if(Forced)State->SetNumberField(TEXT("force_buff_request"),1);
        FACEPluginVM Policy;Policy.Load(Source,Error);
        for(int32 Index=0;Index<Order.Num();++Index)
        {
            const auto Spell=Families[Order[Index]];auto Intent=Step(Policy,State,Settings);
            double Actual=0;Intent->TryGetNumberField(TEXT("spell"),Actual);
            TestEqual(*(TEXT("Casting support buffs precede ordinary buffs: ")+Spell->GetStringField(TEXT("name"))),Actual,Spell->GetNumberField(TEXT("id")));
            auto E=MakeShared<FJsonObject>();E->SetNumberField(TEXT("id"),Actual);E->SetNumberField(TEXT("category"),Order[Index]);
            E->SetNumberField(TEXT("power"),Spell->GetNumberField(TEXT("power")));E->SetNumberField(TEXT("remaining"),300);
            Enchantments.Add(MakeShared<FJsonValueObject>(E));State->SetArrayField(TEXT("enchantments"),Enchantments);
            State->SetNumberField(TEXT("action_serial"),Index+1);State->SetNumberField(TEXT("last_spell"),Actual);State->SetNumberField(TEXT("time"),101+Index);
        }
        auto Finished=Step(Policy,State,Settings);
        TestFalse(TEXT("Completed casting support families are not repeated"),Finished->HasField(TEXT("spell")));
    }
    {
        // Server-reported improvements can unlock a higher tier during the cycle.
        auto State=Snapshot(),Settings=Profile();Settings->SetBoolField(TEXT("buffing"),true);
        auto Low=RecoveryJson(TEXT(R"({"id":8001,"name":"Creature mastery VI","category":43,"school":4,"duration":1800,"skill":310,"power":250,"level":6,"self_buff":true})"));
        auto High=RecoveryJson(TEXT(R"({"id":8002,"name":"Creature mastery VII","category":43,"school":4,"duration":3600,"skill":310,"power":300,"level":7,"self_buff":true})"));
        auto Focus=RecoveryJson(TEXT(R"({"id":8003,"name":"Focus VI","category":9,"school":4,"duration":1800,"skill":310,"power":250,"level":6,"self_buff":true})"));
        State->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Focus),MakeShared<FJsonValueObject>(High),MakeShared<FJsonValueObject>(Low)});
        State->SetArrayField(TEXT("trained_skills"),{MakeShared<FJsonValueNumber>(31)});
        FACEPluginVM Policy;Policy.Load(Source,Error);
        TestEqual(TEXT("Begins with currently castable Creature mastery"),Step(Policy,State,Settings)->GetNumberField(TEXT("spell")),8001.);
        State->SetNumberField(TEXT("action_serial"),1);State->SetNumberField(TEXT("last_spell"),8001);State->SetNumberField(TEXT("time"),101);
        State->SetArrayField(TEXT("enchantments"),{MakeShared<FJsonValueObject>(RecoveryJson(TEXT(R"({"id":8001,"category":43,"power":250,"remaining":1799})")))});
        // The host refreshes spell skills from replicated effective skill values.
        Low->SetNumberField(TEXT("skill"),350);High->SetNumberField(TEXT("skill"),350);Focus->SetNumberField(TEXT("skill"),350);
        TestEqual(TEXT("Newly usable mastery upgrade does not wait out stale retry delay"),Step(Policy,State,Settings)->GetNumberField(TEXT("spell")),8002.);
    }
    // Use real DAT duration/power: Tusker Leap is stronger than Jump VIII,
    // but expires in 10s, inside the default 60s renewal window.
    {
        const auto Leap=Spells.FindRef(TEXT("Tusker Leap"));
        const auto Jump=Spells.FindRef(TEXT("Incantation of Jumping Mastery Self"));
        if(!TestTrue(TEXT("Real Jump and Tusker Leap metadata found"),Leap.IsValid()&&Jump.IsValid()))return false;
        TestEqual(TEXT("Tusker Leap is a ten-second burst"),Leap->GetNumberField(TEXT("duration")),10.);
        auto State=Snapshot(),Settings=Profile();Settings->SetBoolField(TEXT("buffing"),true);
        State->SetArrayField(TEXT("trained_skills"),{MakeShared<FJsonValueNumber>(22)});
        State->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Leap),MakeShared<FJsonValueObject>(Jump)});
        FACEPluginVM Policy;Policy.Load(Source,Error);
        TestEqual(TEXT("Automatic buff chooses sustained Jump instead of higher-power Tusker Leap"),Step(Policy,State,Settings)->GetNumberField(TEXT("spell")),Jump->GetNumberField(TEXT("id")));
        auto Active=RecoveryJson(TEXT(R"({"category":69,"power":400,"remaining":5399})"));Active->SetNumberField(TEXT("id"),Jump->GetNumberField(TEXT("id")));
        State->SetArrayField(TEXT("enchantments"),{MakeShared<FJsonValueObject>(Active)});
        State->SetNumberField(TEXT("action_serial"),1);State->SetNumberField(TEXT("last_spell"),Jump->GetNumberField(TEXT("id")));
        State->SetNumberField(TEXT("time"),101);
        TestFalse(TEXT("Acknowledged Jump completes this buff family"),Step(Policy,State,Settings)->HasField(TEXT("action")));
        Settings->SetBoolField(TEXT("auto_buffs"),false);Settings->SetArrayField(TEXT("buffs"),{MakeShared<FJsonValueNumber>(Leap->GetNumberField(TEXT("id")))});
        for(bool ItemConfirmation:{false,true})
        {
            FACEPluginVM Explicit;Explicit.Load(Source,Error);State->RemoveField(TEXT("enchantments"));State->RemoveField(TEXT("item_buffs"));
            TestEqual(TEXT("Explicit short buff remains available"),Step(Explicit,State,Settings)->GetNumberField(TEXT("spell")),Leap->GetNumberField(TEXT("id")));
            State->SetNumberField(TEXT("action_serial"),2);State->SetNumberField(TEXT("last_spell"),Leap->GetNumberField(TEXT("id")));
            auto Confirmed=RecoveryJson(TEXT(R"({"target":1,"category":69,"power":401,"duration":10,"remaining":9,"expires":111})"));Confirmed->SetNumberField(TEXT("id"),Leap->GetNumberField(TEXT("id")));
            State->SetArrayField(ItemConfirmation?TEXT("item_buffs"):TEXT("enchantments"),{MakeShared<FJsonValueObject>(Confirmed)});
            auto Next=Step(Explicit,State,Settings);
            TestFalse(TEXT("Fresh short buff is not immediately recast"),Next->HasField(TEXT("action")));
            TestFalse(TEXT("Fresh short buff does not stall remaining cycle"),Next->GetStringField(TEXT("status")).Contains(TEXT("Waiting for buff")));
            State->SetNumberField(TEXT("action_serial"),1);
        }
    }
    // Regular mana recovery prefers conversion even over a higher-tier boost;
    // replenished mana then pays for Revitalize when stamina falls below threshold.
    for(bool Imported:{false,true})
    {
        auto State=Snapshot(),Settings=Profile();State->SetNumberField(TEXT("mana"),20);
        const auto Convert=Spells.FindRef(TEXT("Stamina to Mana Self VI")),Revitalize=Spells.FindRef(TEXT("Robustification")),Boost=Spells.FindRef(TEXT("Incantation of Mana Boost Self"));
        if(!TestTrue(TEXT("Real recovery ordering spell metadata found"),Convert.IsValid()&&Revitalize.IsValid()&&Boost.IsValid()))return false;
        State->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Boost),MakeShared<FJsonValueObject>(Convert),MakeShared<FJsonValueObject>(Revitalize)});
        if(Imported)Settings->SetArrayField(TEXT("recharge_handlers"),{
            MakeShared<FJsonValueObject>(RecoveryJson(TEXT(R"({"vital":4,"stance":1,"handler":"Regular Spell","min":0,"max":100})"))),
            MakeShared<FJsonValueObject>(RecoveryJson(TEXT(R"({"vital":6,"stance":1,"handler":"Regular Spell","min":0,"max":100})")))});
        FACEPluginVM Policy;Policy.Load(Source,Error);
        TestEqual(TEXT("Conversion preferred to stronger Mana Boost"),Step(Policy,State,Settings)->GetNumberField(TEXT("spell")),Convert->GetNumberField(TEXT("id")));
        State->SetNumberField(TEXT("mana"),90);State->SetNumberField(TEXT("stamina"),20);State->SetNumberField(TEXT("time"),101);
        State->SetNumberField(TEXT("action_serial"),1);State->SetNumberField(TEXT("last_spell"),Convert->GetNumberField(TEXT("id")));
        TestEqual(TEXT("Revitalize follows successful Stamina to Mana"),Step(Policy,State,Settings)->GetNumberField(TEXT("spell")),Revitalize->GetNumberField(TEXT("id")));
        State->SetNumberField(TEXT("stamina"),90);State->SetNumberField(TEXT("mana"),70);State->SetNumberField(TEXT("time"),102);
        State->SetNumberField(TEXT("action_serial"),2);State->SetNumberField(TEXT("last_spell"),Revitalize->GetNumberField(TEXT("id")));
        TestFalse(TEXT("Recovered vitals stop the conversion/revitalize cycle"),Step(Policy,State,Settings)->HasField(TEXT("action")));
        State->SetNumberField(TEXT("mana"),20);State->SetNumberField(TEXT("stamina"),20);
        State->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Boost),MakeShared<FJsonValueObject>(Convert)});
        FACEPluginVM Fallback;Fallback.Load(Source,Error);
        TestEqual(TEXT("Low stamina preserves direct mana spell fallback"),Step(Fallback,State,Settings)->GetNumberField(TEXT("spell")),Boost->GetNumberField(TEXT("id")));
    }
    // The live inventory contains kits even when the player expects spell recovery.
    // Exercise actual DAT formula components, all stances, and both preference modes.
    for(int32 Mode:{1,2,4,8})for(bool SuppliesFirst:{false,true})
    {
        auto S=Snapshot(),P=Profile();S->SetNumberField(TEXT("health"),20);S->SetNumberField(TEXT("combat_mode"),Mode);
        P->SetBoolField(TEXT("recovery_supplies_first"),SuppliesFirst);S->SetBoolField(TEXT("components_required"),true);
        auto Heal=Spells[TEXT("Incantation of Heal Self")];S->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Heal)});
        auto Inventory=S->GetArrayField(TEXT("inventory"));
        Inventory.Add(MakeShared<FJsonValueObject>(RecoveryJson(TEXT(R"({"id":42,"name":"Kit","type":128,"usable":true,"identified":true,"healing_kit":true,"structure":50,"boost_vital":2,"boost":100})"))));
        S->SetArrayField(TEXT("trained_skills"),{MakeShared<FJsonValueNumber>(21)});
        for(const auto& C:Heal->GetObjectField(TEXT("scarabs"))->Values)
        {auto Item=MakeShared<FJsonObject>();Item->SetNumberField(TEXT("id"),1000+Inventory.Num());Item->SetStringField(TEXT("name"),C.Key);Item->SetNumberField(TEXT("count"),C.Value->AsNumber());Inventory.Add(MakeShared<FJsonValueObject>(Item));}
        S->SetArrayField(TEXT("inventory"),Inventory);
        FACEPluginVM VM;VM.Load(Source,Error);auto I=Step(VM,S,P);
        TestEqual(TEXT("Default spells-first recovery works with kits present in every stance; preference reverses it"),I->GetStringField(TEXT("action")),FString(SuppliesFirst?TEXT("use_item"):TEXT("cast")));
        if(SuppliesFirst)
        {
            // A failed or timed-out kit must yield to spells, not just retry the kit.
            for(bool Timeout:{false,true})
            {
                FACEPluginVM Fallback;Fallback.Load(Source,Error);
                TestEqual(TEXT("Kit preference starts with the kit"),Step(Fallback,S,P)->GetStringField(TEXT("action")),FString(TEXT("use_item")));
                if(Timeout)S->SetNumberField(TEXT("time"),109);
                else {S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("action_error"),1);}
                TestEqual(TEXT("Failed or unconfirmed kit falls back to a recovery spell"),Step(Fallback,S,P)->GetStringField(TEXT("action")),FString(TEXT("cast")));
                S->SetNumberField(TEXT("time"),100);S->SetNumberField(TEXT("action_serial"),0);S->SetNumberField(TEXT("action_error"),0);
            }
            Inventory[1]->AsObject()->SetNumberField(TEXT("structure"),0);
            FACEPluginVM Empty;Empty.Load(Source,Error);
            TestEqual(TEXT("Exhausted kit falls back to spell"),Step(Empty,S,P)->GetStringField(TEXT("action")),FString(TEXT("cast")));
            Inventory[1]->AsObject()->SetNumberField(TEXT("structure"),50);
        }
        // Explicit priority overrides an imported handler list, in both directions.
        P->SetArrayField(TEXT("recharge_handlers"),{MakeShared<FJsonValueObject>(RecoveryJson(TEXT(R"({"vital":2,"stance":1,"handler":"Regular Spell","min":0,"max":100})")))});
        P->SetBoolField(TEXT("use_imported_recovery_order"),false);
        FACEPluginVM Override;Override.Load(Source,Error);
        TestEqual(TEXT("Explicit preference applies to imported profiles"),Step(Override,S,P)->GetStringField(TEXT("action")),FString(SuppliesFirst?TEXT("use_item"):TEXT("cast")));
        P->RemoveField(TEXT("recharge_handlers"));
        // Remove components: ordinary players fall back to the kit; exempt players still cast.
        Inventory.SetNum(2);S->SetArrayField(TEXT("inventory"),Inventory);P->SetBoolField(TEXT("recovery_supplies_first"),false);
        FACEPluginVM Missing;Missing.Load(Source,Error);TestEqual(TEXT("Missing components retain kit fallback"),Step(Missing,S,P)->GetStringField(TEXT("action")),FString(TEXT("use_item")));
        S->SetBoolField(TEXT("components_required"),false);
        FACEPluginVM Exempt;Exempt.Load(Source,Error);TestEqual(TEXT("Server component exemption keeps recovery spells available"),Step(Exempt,S,P)->GetStringField(TEXT("action")),FString(TEXT("cast")));
        S->SetNumberField(TEXT("last_spell"),Heal->GetNumberField(TEXT("id")));S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("action_error"),1);
        TestEqual(TEXT("Rejected recovery spell falls back without waiting eight seconds or stopping"),Step(Exempt,S,P)->GetStringField(TEXT("action")),FString(TEXT("use_item")));
    }
    // Three consecutive successful recovery cycles must not exhaust a lifetime retry counter.
    FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();S->SetArrayField(TEXT("inventory"),{MakeShared<FJsonValueObject>(RecoveryJson(TEXT(R"({"id":42,"wcid":42,"name":"Potion","type":32,"usable":true,"identified":true,"boost_vital":2,"boost":100,"count":10})")))});
    for(int Cycle=0;Cycle<5;++Cycle)
    {
        S->SetNumberField(TEXT("health"),20);S->SetNumberField(TEXT("time"),100+Cycle*20);
        auto I=Step(VM,S,P);FString Action;I->TryGetStringField(TEXT("action"),Action);TestEqual(TEXT("Repeated recovery remains active"),Action,FString(TEXT("use_item")));
        S->SetNumberField(TEXT("health"),100);S->SetNumberField(TEXT("time"),110+Cycle*20);
        // Vitals and UseDone are independent network updates; observe vitals first.
        Step(VM,S,P);S->SetNumberField(TEXT("action_serial"),Cycle+1);
    }
    for(int32 School:{2,3,4})for(int32 Requested=0;Requested<=8;++Requested)
    {
        auto State=Snapshot(),Settings=Profile();Settings->SetBoolField(TEXT("buffing"),true);Settings->SetBoolField(TEXT("recovery"),false);
        const TCHAR* Key=School==2?TEXT("life_buff_level"):School==3?TEXT("item_buff_level"):TEXT("creature_buff_level");
        Settings->SetNumberField(Key,Requested);TArray<TSharedPtr<FJsonValue>> Book;
        for(int32 Level=8;Level>=1;--Level)
        {
            auto Spell=RecoveryJson(TEXT(R"({"category":1,"duration":300,"skill":600,"self_buff":true,"beneficial":true,"caster_target":true})"));
            Spell->SetNumberField(TEXT("id"),Level);Spell->SetNumberField(TEXT("level"),Level);
            Spell->SetNumberField(TEXT("power"),Level*40+17);Spell->SetNumberField(TEXT("school"),School);Book.Add(MakeShared<FJsonValueObject>(Spell));
        }
        State->SetArrayField(TEXT("spells"),Book);FACEPluginVM Policy;Policy.Load(Source,Error);
        TestEqual(TEXT("Each school selects exact formula tier or automatic highest"),Step(Policy,State,Settings)->GetNumberField(TEXT("spell")),double(Requested?Requested:8));
        if(Requested)
        {
            Book.RemoveAll([Requested](const auto& Spell){return Spell->AsObject()->GetNumberField(TEXT("level"))==Requested;});
            State->SetArrayField(TEXT("spells"),Book);FACEPluginVM Missing;Missing.Load(Source,Error);
            TestFalse(TEXT("Missing explicit tier is skipped instead of casting a different level"),Step(Missing,State,Settings)->HasField(TEXT("action")));
        }
    }
    {
        auto State=Snapshot(),Settings=Profile();State->SetNumberField(TEXT("force_buff_request"),1);
        auto Bad=RecoveryJson(TEXT(R"({"id":1,"name":"Failing buff","category":1,"school":4,"duration":300,"skill":500,"power":100,"self_buff":true})"));
        auto Good=RecoveryJson(TEXT(R"({"id":2,"name":"Working buff","category":2,"school":2,"duration":300,"skill":500,"power":100,"self_buff":true})"));
        State->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Bad),MakeShared<FJsonValueObject>(Good)});
        FACEPluginVM Policy;Policy.Load(Source,Error);
        for(int32 Attempt=0;Attempt<3;++Attempt)
        {State->SetNumberField(TEXT("time"),100+13*Attempt);TestEqual(TEXT("Unconfirmed buff has bounded retries"),Step(Policy,State,Settings)->GetNumberField(TEXT("spell")),1.);}
        State->SetNumberField(TEXT("time"),140);auto I=Step(Policy,State,Settings);
        TestFalse(TEXT("Exhausted buff is skipped without stopping UCM"),I->HasField(TEXT("action")));
        TestEqual(TEXT("Next family casts after skipped buff"),Step(Policy,State,Settings)->GetNumberField(TEXT("spell")),2.);
        State->SetNumberField(TEXT("action_serial"),1);State->SetNumberField(TEXT("last_spell"),2);I=Step(Policy,State,Settings);
        TestEqual(TEXT("Failed family does not prevent forced cycle completion"),I->GetStringField(TEXT("action")),FString(TEXT("force_buff_done")));
        TestTrue(TEXT("Completion discloses skipped buffs"),I->GetStringField(TEXT("status")).Contains(TEXT("skipped")));
        // A wholly unsupplied family must not block the available family or stop the run.
        State=Snapshot();State->SetNumberField(TEXT("force_buff_request"),2);State->SetBoolField(TEXT("components_required"),true);
        Bad->SetBoolField(TEXT("components_known"),false);State->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Bad),MakeShared<FJsonValueObject>(Good)});
        FACEPluginVM Missing;Missing.Load(Source,Error);
        TestEqual(TEXT("Missing components skip only the affected family"),Step(Missing,State,Settings)->GetNumberField(TEXT("spell")),2.);
        State->SetNumberField(TEXT("action_serial"),1);State->SetNumberField(TEXT("last_spell"),2);I=Step(Missing,State,Settings);
        TestEqual(TEXT("Unavailable family still allows completion"),I->GetStringField(TEXT("action")),FString(TEXT("force_buff_done")));
        TestTrue(TEXT("Unavailable completion reports skips"),I->GetStringField(TEXT("status")).Contains(TEXT("skipped")));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMResourcesTest,"ACE.Plugins.ResourceMaintenance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMResourcesTest::RunTest(const FString&)
{
    FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto Snapshot=[](){return RecoveryJson(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"combat_mode":1,"ready":true,"busy":false,"action_serial":0,"action_error":0,"spells":[],"enchantments":[],"targets":[],"inventory":[{"id":20,"wcid":20,"name":"Essence","type":128,"identified":true,"usable":true,"structure":2,"max_structure":50,"int_properties":{"280":213}},{"id":21,"wcid":49485,"name":"Encapsulated Spirit","type":128,"usable":true},{"id":30,"wcid":30,"name":"Mana Stone","type":524288,"identified":true,"usable":true,"mana_empty":true,"mana":0},{"id":31,"wcid":31,"name":"Loot Ring","type":8,"identified":true,"mana":5000,"max_mana":5000},{"id":32,"wcid":31,"name":"Keep Ring","type":8,"identified":true,"mana":5000,"max_mana":5000},{"id":33,"wcid":33,"name":"Armor","type":2,"identified":true,"equipped":true,"mana":10,"max_mana":100}]})"));};
    auto Profile=[](){return RecoveryJson(TEXT(R"({"buffing":false,"recovery":false,"combat":"off","looting":false,"refill_summons":true,"summon_refill_charges":5,"weapon_items":[20],"item_mana":false,"fill_mana_stones":false,"mana_source_items":[31]})"));};
    auto Step=[&](FACEPluginVM& VM,auto S,auto P){TSharedPtr<FJsonObject>I;TestTrue(*Error,VM.Step(S,P,I,Error));return I?I:MakeShared<FJsonObject>();};
    {
        FACEPluginVM VM;TestTrue(TEXT("Resource maintenance policy loads"),VM.Load(Source,Error));auto S=Snapshot(),P=Profile();
        auto I=Step(VM,S,P);TestEqual(TEXT("Low essence gets a spirit independently of automatic combat/summoning"),I->GetStringField(TEXT("action")),FString(TEXT("apply_item")));
        TestEqual(TEXT("Spirit is source"),I->GetNumberField(TEXT("item")),21.);TestEqual(TEXT("Essence is target"),I->GetNumberField(TEXT("target")),20.);
        S->SetNumberField(TEXT("time"),101);S->SetNumberField(TEXT("action_serial"),1);
        TestFalse(TEXT("UseDone alone cannot consume another spirit"),Step(VM,S,P)->HasField(TEXT("action")));
        S->GetArrayField(TEXT("inventory"))[0]->AsObject()->SetNumberField(TEXT("structure"),50);
        TestFalse(TEXT("Confirmed full essence stops refilling"),Step(VM,S,P)->HasField(TEXT("action")));
    }
    for(int Case=0;Case<5;++Case)
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();auto Inv=S->GetArrayField(TEXT("inventory"));
        if(Case==0)P->SetBoolField(TEXT("refill_summons"),false);
        if(Case==1)Inv[0]->AsObject()->SetNumberField(TEXT("structure"),6);
        if(Case==2)Inv[1]->AsObject()->SetNumberField(TEXT("wcid"),999);
        if(Case==3)P->SetArrayField(TEXT("weapon_items"),{MakeShared<FJsonValueNumber>(99)});
        if(Case==4)Inv[0]->AsObject()->SetBoolField(TEXT("identified"),false);
        TestFalse(TEXT("Disabled, sufficient, missing spirit, excluded or unassessed essences are not refilled"),Step(VM,S,P)->HasField(TEXT("action")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();Step(VM,S,P);
        S->SetNumberField(TEXT("time"),101);S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("action_error"),1);
        TestFalse(TEXT("Rejected refill backs off without stopping UCM"),Step(VM,S,P)->HasField(TEXT("action")));
        S->SetNumberField(TEXT("time"),132);S->SetNumberField(TEXT("action_error"),0);
        TestEqual(TEXT("Refill can retry after backoff"),Step(VM,S,P)->GetStringField(TEXT("action")),FString(TEXT("apply_item")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();P->SetBoolField(TEXT("refill_summons"),false);P->SetBoolField(TEXT("fill_mana_stones"),true);P->SetBoolField(TEXT("item_mana"),true);
        auto I=Step(VM,S,P);TestEqual(TEXT("Empty stone fills via normal targeted use"),I->GetStringField(TEXT("action")),FString(TEXT("apply_item")));
        TestEqual(TEXT("Only specifically selected item is consumed"),I->GetNumberField(TEXT("target")),31.);
        S->SetNumberField(TEXT("time"),101);S->SetNumberField(TEXT("action_serial"),1);
        TestFalse(TEXT("Stone filling waits for actual contents"),Step(VM,S,P)->HasField(TEXT("action")));
        auto Stone=S->GetArrayField(TEXT("inventory"))[2]->AsObject();Stone->SetNumberField(TEXT("mana"),5000);Stone->SetBoolField(TEXT("mana_empty"),false);
        I=Step(VM,S,P);TestEqual(TEXT("Newly filled stone recharges gear through player-targeting use"),I->GetStringField(TEXT("action")),FString(TEXT("use_item")));
        TestEqual(TEXT("Uses the filled stone"),I->GetNumberField(TEXT("item")),30.);
        S->GetArrayField(TEXT("inventory"))[5]->AsObject()->SetNumberField(TEXT("mana"),100);
        TestFalse(TEXT("Full gear does not consume another charge"),Step(VM,S,P)->HasField(TEXT("action")));
    }
    for(int Case=0;Case<6;++Case)
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),P=Profile();P->SetBoolField(TEXT("refill_summons"),false);P->SetBoolField(TEXT("fill_mana_stones"),true);
        auto Inv=S->GetArrayField(TEXT("inventory"));auto Donor=Inv[3]->AsObject();
        if(Case==0)P->SetArrayField(TEXT("mana_source_items"),{});
        if(Case==1)Donor->SetBoolField(TEXT("equipped"),true);
        if(Case==2)Donor->SetBoolField(TEXT("retained"),true);
        if(Case==3)Donor->SetBoolField(TEXT("resource_available"),false);
        if(Case==4)Inv[2]->AsObject()->SetBoolField(TEXT("mana_empty"),false);
        if(Case==5)Donor->SetBoolField(TEXT("identified"),false);
        TestFalse(TEXT("No unselected/equipped/retained/traded/unidentified donor or nonempty stone is consumed"),Step(VM,S,P)->HasField(TEXT("action")));
    }
    return !HasAnyErrors();
}
#endif
