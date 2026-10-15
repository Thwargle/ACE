#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Interfaces/IPluginManager.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEUCMProfiles.h"
#include "Mods/ACEPluginVM.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMSharedMetaTest,"ACE.Plugins.SharedMetaProfiles",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMSharedMetaTest::RunTest(const FString&)
{
    using namespace ACEUCMProfiles;
    auto Json=[](const TCHAR* Text){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;};
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/SharedMetaProfiles")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* GI=NewObject<UGameInstance>();GI->Init();
    ON_SCOPE_EXIT{GI->Shutdown();IFileManager::Get().DeleteDirectory(*Root,false,true);};
    auto* H=GI->GetSubsystem<UACEPluginSubsystem>();H->StorageRoot=Root;H->Settings=MakeShared<FJsonObject>();
    if(!TestTrue(TEXT("Initialize isolated current selection"),H->InitializeUCMSelection()))return false;
    auto P=H->Find(TEXT("ucm"));if(!P)return false;
    auto Profile=Copy(P->Profile);
    const auto Meta=Json(TEXT(R"({"met_source":"Quest.met","vt_meta":[{"state":"Default","condition":{"type":1},"action":{"type":4,"route_name":"Path","route":{"route":[{"cell":2139029505,"x":84,"y":84,"z":12}],"loop_route":true}}}],"vt_library":{"quest.usd":{"usd_keys":["skill_margin"],"skill_margin":17},"quest.utl":{"looting":false,"loot_rules":[{"label":"Copper","material":59}]},"quest.met":{"vt_meta":[]}}})"));
    for(const auto& Field:Meta->Values)Profile->Values.Add(Field.Key,Field.Value);
    Profile->GetObjectField(TEXT("vt_library"))->GetObjectField(TEXT("quest.usd"))->SetNumberField(TEXT("waypoint_radius"),1.25);
    if(!TestTrue(TEXT("Import creates reusable dependencies and embedded route"),H->SaveImportedProfile(TEXT("Quest"),Profile,TEXT("Quest.met")))){AddError(H->Notice);return false;}
    TestEqual(TEXT("Imports do not create named setups"),List(Root,TEXT("ucm")).Num(),0);
    auto Refs=P->Profile->GetObjectField(TEXT("meta_profile_refs"));
    TestEqual(TEXT("Character dependency gets familiar name"),Refs->GetObjectField(TEXT("quest.usd"))->GetStringField(TEXT("Character")),FString(TEXT("quest")));
    const auto Action=P->Profile->GetArrayField(TEXT("vt_meta"))[0]->AsObject()->GetObjectField(TEXT("action"));
    const FString RouteName=Action->GetStringField(TEXT("route_profile"));
    TestTrue(TEXT("Embedded route is available in route chooser"),H->UCMSectionProfiles(TEXT("Routes")).Contains(RouteName));
    P->Enabled=true;P->Running=true;
    auto RouteIntent=Json(TEXT(R"({"action":"notice","activity":"meta","status":"Embedded route","runtime_loop_route":true})"));
    RouteIntent->SetArrayField(TEXT("runtime_route"),Action->GetObjectField(TEXT("route"))->GetArrayField(TEXT("route")));
    RouteIntent->SetStringField(TEXT("runtime_route_profile"),RouteName);H->Execute(*P,RouteIntent);
    TestEqual(TEXT("Meta route is reflected in selected profile"),H->UCMSectionName(TEXT("Routes")),RouteName);
    TestTrue(TEXT("Native geometry receives embedded route"),H->RuntimeRoute.IsValid());
    auto NativeRoute=H->RuntimeRoute;
    for(const TCHAR* Kind:{TEXT("usd"),TEXT("utl")})
    {
        auto Load=Json(TEXT(R"({"action":"profile_load","activity":"meta"})"));Load->SetStringField(TEXT("kind"),Kind);Load->SetStringField(TEXT("profile"),FString(TEXT("quest."))+Kind);H->Execute(*P,Load);
        TestTrue(TEXT("Character and loot loads preserve native embedded route"),H->RuntimeRoute==NativeRoute);
        TestTrue(TEXT("Loading a dependency does not stop UCM"),P->Running);
    }
    TestEqual(TEXT("USD load selects shared Character profile"),H->UCMSectionName(TEXT("Character")),FString(TEXT("quest")));
    TestEqual(TEXT("USD settings become active"),P->Profile->GetNumberField(TEXT("skill_margin")),17.);
    TestEqual(TEXT("USD navigation options take effect without replacing route"),P->Profile->GetNumberField(TEXT("waypoint_radius")),1.25);
    TestEqual(TEXT("USD preserves selected embedded route name"),H->UCMSectionName(TEXT("Routes")),RouteName);
    TestEqual(TEXT("USD preserves current route points"),P->Profile->GetArrayField(TEXT("route")).Num(),1);
    TestEqual(TEXT("UTL load selects shared Loot profile"),H->UCMSectionName(TEXT("Loot")),FString(TEXT("quest")));
    TestEqual(TEXT("Both loads are acknowledged to Lua"),P->Profile->GetNumberField(TEXT("vt_revision")),2.);
    P->Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->SetStringField(TEXT("label"),TEXT("Edited Copper"));
    TestTrue(TEXT("Edit shared loot profile"),H->SaveProfile(P->Id,P->ProfileName,H->ProfileJson(P->Id),false));
    auto LoadLoot=Json(TEXT(R"({"action":"profile_load","activity":"meta","kind":"utl","profile":"quest.utl"})"));H->Execute(*P,LoadLoot);
    TestEqual(TEXT("Subsequent meta load honors shared edits"),P->Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->GetStringField(TEXT("label")),FString(TEXT("Edited Copper")));
    P->Profile->GetArrayField(TEXT("route"))[0]->AsObject()->SetNumberField(TEXT("x"),90);
    TestTrue(TEXT("Edit exported embedded route"),H->SaveProfile(P->Id,P->ProfileName,H->ProfileJson(P->Id),false));
    const int32 RouteCount=H->UCMSectionProfiles(TEXT("Routes")).Num();
    TestTrue(TEXT("Reload current selection"),H->LoadProfile(P->Id,ActiveName()));
    TestEqual(TEXT("Reload does not duplicate embedded profiles"),H->UCMSectionProfiles(TEXT("Routes")).Num(),RouteCount);
    TestEqual(TEXT("Lua embedded route receives shared edits"),P->Profile->GetArrayField(TEXT("vt_meta"))[0]->AsObject()->GetObjectField(TEXT("action"))->GetObjectField(TEXT("route"))->GetArrayField(TEXT("route"))[0]->AsObject()->GetNumberField(TEXT("x")),90.);
    TestEqual(TEXT("Self-load points at current named meta"),P->Profile->GetObjectField(TEXT("meta_profile_refs"))->GetObjectField(TEXT("quest.met"))->GetStringField(TEXT("Metas")),H->UCMSectionName(TEXT("Metas")));
    H->Execute(*P,Json(TEXT(R"({"action":"profile_load","activity":"meta","kind":"met","profile":"quest.met"})")));
    TestEqual(TEXT("Self-load uses editable meta rather than stale conversion"),P->Profile->GetArrayField(TEXT("vt_meta")).Num(),1);
    TestFalse(TEXT("Loading a meta deliberately invalidates previous native route"),H->RuntimeRoute.IsValid());
    TMap<FString,int32> Counts;for(const auto& Section:Sections())Counts.Add(Section,H->UCMSectionProfiles(Section).Num());
    TestTrue(TEXT("Repeat meta import reuses complete graph"),H->SaveImportedProfile(TEXT("Quest"),Profile,TEXT("Quest.met")));
    TestTrue(TEXT("Importing dependency on its own reuses named file"),H->ReuseUCMImport(TEXT("quest.utl")));
    TestEqual(TEXT("Dependency import retains edited native loot"),P->Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->GetStringField(TEXT("label")),FString(TEXT("Edited Copper")));
    for(const auto& Section:Sections())TestEqual(TEXT("Repeated imports create no duplicate section files"),H->UCMSectionProfiles(Section).Num(),Counts[Section]);
    TestTrue(TEXT("Delete embedded route like any shared profile"),H->DeleteUCMSection(TEXT("Routes"),RouteName));
    TestFalse(TEXT("Loading the meta does not resurrect deleted route"),H->UCMSectionProfiles(TEXT("Routes")).Contains(RouteName));
    TestEqual(TEXT("Embedded reference switches to Default"),P->Profile->GetArrayField(TEXT("vt_meta"))[0]->AsObject()->GetObjectField(TEXT("action"))->GetStringField(TEXT("route_profile")),FString(TEXT("Default")));
    TestTrue(TEXT("Delete referenced character profile"),H->DeleteUCMSection(TEXT("Character"),TEXT("quest")));
    TestEqual(TEXT("Dependency reference switches to Default"),P->Profile->GetObjectField(TEXT("meta_profile_refs"))->GetObjectField(TEXT("quest.usd"))->GetStringField(TEXT("Character")),FString(TEXT("Default")));
    TestTrue(TEXT("Deleted dependency is available for reimport, not aliased to Default"),H->ImportedUCMProfiles(TEXT("quest.usd"))->Values.IsEmpty());

    // Exercise the user's complete source graph through conversion, the Lua VM
    // and the native host; keep the fixture optional on other developers' PCs.
    FString Fixture;
    if(FParse::Value(FCommandLine::Get(),TEXT("UCMMetaFixture="),Fixture))
    {
        const FString Inbox=Root/TEXT("ImportInbox");IFileManager::Get().MakeDirectory(*Inbox,true);
        for(const TCHAR* Ext:{TEXT("met"),TEXT("usd"),TEXT("utl")})
            TestEqual(TEXT("Copy original fixture into isolated import folder"),IFileManager::Get().Copy(*(Inbox/(FString(TEXT("AunRalirea."))+Ext)),*(Fixture/(FString(TEXT("AunRalirea."))+Ext))),COPY_OK);
        if(!TestTrue(TEXT("Aun Ralirea imports successfully"),H->SelectProfileFile(TEXT("AunRalirea.met")))){AddError(H->Notice);return false;}
        TestEqual(TEXT("All Aun Ralirea rules preserved"),P->Profile->GetArrayField(TEXT("vt_meta")).Num(),47);
        const FString MetaName=H->UCMSectionName(TEXT("Metas"));
        P->Profile->SetBoolField(TEXT("meta_enabled"),false);
        TestTrue(TEXT("Selecting an already imported meta succeeds"),H->SelectUCMSection(TEXT("Metas"),MetaName));
        TestTrue(TEXT("Selecting a meta explicitly enables its rules"),P->Profile->GetBoolField(TEXT("meta_enabled")));
        P->Profile->SetBoolField(TEXT("meta_enabled"),false);
        TestTrue(TEXT("Repeat import selects the existing meta"),H->SelectProfileFile(TEXT("AunRalirea.met")));
        TestTrue(TEXT("Reusing an import also enables its rules"),P->Profile->GetBoolField(TEXT("meta_enabled")));
        FString Script,Error;FFileHelper::LoadFileToString(Script,*(P->Directory/TEXT("main.lua")));
        FACEPluginVM VM;if(!TestTrue(TEXT("Lua policy loads"),VM.Load(Script,Error)))return false;
        auto Snapshot=Json(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"position":{"cell":2139029505,"x":84,"y":84,"z":12},"inventory":[],"action_serial":0,"teleport_sequence":0,"chat_serial":0,"portal_serial":0,"chat_events":[],"commands":[]})"));
        P->Enabled=true;P->Running=true;int32 Loads=0,Routes=0;
        for(int32 Tick=0;Tick<24;++Tick)
        {
            if(Tick==10)
            {
                TestTrue(TEXT("Aun intentionally clears navigation before its fellowship start command"),H->RuntimeRoute&&H->RuntimeRoute->GetArrayField(TEXT("route")).IsEmpty());
                Snapshot->SetArrayField(TEXT("inventory"),{MakeShared<FJsonValueObject>(Json(TEXT(R"({"id":99,"name":"Town Network Portal Gem","count":10,"identified":true})")))});
                Snapshot->SetNumberField(TEXT("chat_serial"),1);
                Snapshot->SetArrayField(TEXT("chat_events"),{MakeShared<FJsonValueObject>(Json(TEXT(R"({"serial":1,"text":"[Fellowship] You say, \"!start\""})")))});
            }
            Snapshot->SetNumberField(TEXT("time"),100+Tick*.3);TSharedPtr<FJsonObject> Intent;
            auto TickProfile=Copy(P->Profile);TickProfile->RemoveField(TEXT("vt_library"));
            if(!TestTrue(TEXT("Aun Ralirea policy tick succeeds"),VM.Step(Snapshot,TickProfile,Intent,Error))){AddError(Error);break;}
            if(!Intent)continue;FString Op;Intent->TryGetStringField(TEXT("action"),Op);
            if(Tick<12){FString Status,State;Intent->TryGetStringField(TEXT("status"),Status);Intent->TryGetStringField(TEXT("meta_state"),State);
                AddInfo(FString::Printf(TEXT("Aun tick %d: %s, %s, state %s, route update %d"),Tick,*Op,*Status,*State,Intent->HasField(TEXT("runtime_route"))));}
            if(Intent->HasField(TEXT("runtime_route")))++Routes;
            if(Op==TEXT("profile_load"))++Loads;
            if(Tick==9)
            {
                TestFalse(TEXT("Waiting meta never asks the user to record a route"),Intent->GetStringField(TEXT("status")).Contains(TEXT("Record a route")));
                const FString Wait=Intent->GetStringField(TEXT("meta_status"));
                TestTrue(TEXT("Start trigger appears in meta diagnostics"),Wait.Contains(TEXT("!start")));
                TestTrue(TEXT("Required inventory appears in meta diagnostics"),Wait.Contains(TEXT("Town Network Portal Gem"))&&Wait.Contains(TEXT("have 0")));
            }
            H->Execute(*P,Intent);
        }
        TestTrue(TEXT("Real meta activates embedded route"),Routes>0);
        TestTrue(TEXT("Real meta loads its dependencies"),Loads>=2);
        TestTrue(TEXT("Real meta retains usable native route after dependencies"),H->RuntimeRoute&&H->RuntimeRoute->GetArrayField(TEXT("route")).Num()>0);
        TestTrue(TEXT("Actual route selection appears in chooser"),H->UCMSectionName(TEXT("Routes")).StartsWith(TEXT("AunRalirea")));
        TestEqual(TEXT("Displayed route matches native route after meta starts"),P->Profile->GetArrayField(TEXT("route")).Num(),H->RuntimeRoute?H->RuntimeRoute->GetArrayField(TEXT("route")).Num():-1);
        AddInfo(FString::Printf(TEXT("Aun Ralirea: %d profile loads, %d route activations, %d named routes"),Loads,Routes,H->UCMSectionProfiles(TEXT("Routes")).Num()));
        // Replay the quest's actual location/inventory state changes. This
        // verifies route handoffs and source settings without teleporting a
        // live player, claiming a quest reward, or altering their profiles.
        auto Replay=[&](uint32 Cell,const TCHAR* State=nullptr,double Seconds=.3)
        {
            Snapshot->SetNumberField(TEXT("time"),Snapshot->GetNumberField(TEXT("time"))+Seconds);
            Snapshot->GetObjectField(TEXT("position"))->SetNumberField(TEXT("cell"),Cell);
            auto TickProfile=Copy(P->Profile);TickProfile->RemoveField(TEXT("vt_library"));
            if(State){auto Command=MakeShared<FJsonObject>();Command->SetStringField(TEXT("op"),TEXT("state"));Command->SetStringField(TEXT("state"),State);TickProfile->SetArrayField(TEXT("ucm_commands"),{MakeShared<FJsonValueObject>(Command)});}
            TSharedPtr<FJsonObject> Intent;
            if(!TestTrue(TEXT("Quest state executes without script failure"),VM.Step(Snapshot,TickProfile,Intent,Error))){AddError(Error);return;}
            if(Intent)H->Execute(*P,Intent);
        };
        auto Points=[&](){return H->RuntimeRoute?H->RuntimeRoute->GetArrayField(TEXT("route")).Num():-1;};
        Replay(23855548);TestEqual(TEXT("TN arrival loads first town-network segment"),Points(),18);
        Replay(23855644);TestEqual(TEXT("TN interior loads next segment"),Points(),8);
        Replay(459077);TestEqual(TEXT("Exit cell loads search approach"),Points(),29);
        Replay(498466838);Replay(498466838);TestEqual(TEXT("Search landblock activates Back route"),Points(),129);
        Replay(498466838,TEXT("Hunting"));
        TestEqual(TEXT("First hunting state loads complete route"),Points(),711);
        TestEqual(TEXT("First hunting route enters its monitor state"),P->MetaState,FString(TEXT("Default2")));
        Replay(498466838,nullptr,301);TestEqual(TEXT("Five-minute timeout enters alternate return"),P->MetaState,FString(TEXT("Back2")));
        Replay(498466838);TestEqual(TEXT("Alternate return installs its route"),Points(),133);
        Replay(498466838,TEXT("Hunting2"));
        TestEqual(TEXT("Second hunting state loads complete route"),Points(),796);
        TestEqual(TEXT("Second hunting route enters its monitor state"),P->MetaState,FString(TEXT("Default3")));
        auto Items=Snapshot->GetArrayField(TEXT("inventory"));Items.Add(MakeShared<FJsonValueObject>(Json(TEXT(R"({"id":100,"name":"Clay Figurine","count":1,"identified":true})"))));Snapshot->SetArrayField(TEXT("inventory"),Items);
        Replay(498466838);TestEqual(TEXT("Looted quest item starts turn-in"),P->MetaState,FString(TEXT("TurnIn")));
        Replay(498466838);TestEqual(TEXT("Turn-in begins with its recall route"),Points(),2);
        Replay(459077);TestEqual(TEXT("Turn-in selects the quest-specific town route"),Points(),21);
        Replay(263782409);TestEqual(TEXT("Turn-in loads complete NPC approach"),Points(),87);
        Items.Pop();Snapshot->SetArrayField(TEXT("inventory"),Items);
        Replay(263782409);Replay(263782409);
        TestEqual(TEXT("Author's manual turn-in completion enters Finished"),P->MetaState,FString(TEXT("Finished")));
        TestEqual(TEXT("Finished deliberately clears native navigation"),Points(),0);
        TestFalse(TEXT("Finished does not prompt for route recording"),P->Status.Contains(TEXT("Record a route")));
    }
    return true;
}
#endif
