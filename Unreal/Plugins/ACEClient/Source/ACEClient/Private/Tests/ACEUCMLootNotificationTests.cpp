#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMLootNotificationTest,"ACE.Plugins.LootNotifications",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMLootNotificationTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
    Context.SetCurrentWorld(World);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto Parse=[](const TCHAR* Text){TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J);return J;};
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();auto P=Host->Find(TEXT("ucm"));
    if(!TestTrue(TEXT("Bundled UCM exists"),P.IsValid()))return false;
    P->Enabled=true;P->Running=false;
    P->Profile=Parse(TEXT(R"({"loot_rules":[{"name":"Ring","label":"Fine rings","action":"salvage","min_workmanship":7},{"label":"Fallback","action":"keep"}]})"));
    auto* Client=GI->GetSubsystem<UACEClientSubsystem>();Client->Session=MakeShared<FACESession>();
    auto& Session=*Client->Session;Session.State=EACESessionState::InWorld;Session.PlayerGuid=1;
    Session.PlayerVitals.bValid=true;Session.PlayerVitals.Health=100;
    FACEWorldObject Ring;Ring.Guid=701;Ring.WeenieClassId=701;Ring.ItemType=ACEItemType::Jewelry;Ring.Name=TEXT("Ring");
    Session.WorldObjects.Add(Ring.Guid,Ring);Session.SelectedObject.Guid=Ring.Guid;
    Client->ChatHistory.Reset();
    Host->TickLootNotifications();
    TestEqual(TEXT("Missing ID cannot fall through to a later rule"),Client->ChatHistory.Num(),0);
    TestTrue(TEXT("Read-only selection never requests an appraisal"),Host->AppraisalRequests.IsEmpty()&&Session.CachedC2SPackets.IsEmpty());
    FACEAppraisalInfo Info;Info.ObjectGuid=Ring.Guid;Info.bSuccess=true;Info.IntProperties.Add(105,8);Info.IntProperties.Add(170,1);
    Host->ObserveAppraisal(Info);Host->TickLootNotifications();
    if(TestEqual(TEXT("Assessment prints one match while UCM is stopped"),Client->ChatHistory.Num(),1))
    {
        TestTrue(TEXT("Message includes item, first rule and action"),Client->ChatHistory.Last().Text.Contains(TEXT("Ring matches rule \"Fine rings\" (salvage)")));
        TestEqual(TEXT("Item ID matches use the plugin chat filter"),Client->ChatHistory.Last().FilterType,ACEChatMessageType::Plugin);
    }
    const auto Serial=Host->LootMatchSerial;
    for(int32 I=0;I<20;++I)Host->TickLootNotifications();
    TestEqual(TEXT("Idle frames do not rebuild a snapshot or rerun rules"),Host->LootMatchSerial,Serial);
    Host->ObserveAppraisal(Info);Host->TickLootNotifications();
    TestEqual(TEXT("Repeated assessment suppresses duplicate chat"),Client->ChatHistory.Num(),1);
    Session.SelectedObject.Guid=0;Host->TickLootNotifications();Session.SelectedObject.Guid=Ring.Guid;Host->TickLootNotifications();
    TestEqual(TEXT("Explicit reselection can test the item again"),Client->ChatHistory.Num(),2);
    Ring.Guid=702;Session.WorldObjects.Add(Ring.Guid,Ring);Info.ObjectGuid=Ring.Guid;
    Host->ObserveAppraisal(Info);Host->TickLootNotifications();
    TestEqual(TEXT("Successfully assessing an unselected item also reports its rule"),Client->ChatHistory.Num(),3);
    Host->QueueLootMatch(Ring.Guid);
    P->Profile->SetBoolField(TEXT("loot_notify_match"),false);Host->TickLootNotifications();
    TestFalse(TEXT("Disabling notifications releases their VM"),Host->LootMatchVM.IsValid());
    TestTrue(TEXT("Disabling notifications discards already queued matches"),Host->LootMatchQueue.IsEmpty());
    Host->ObserveAppraisal(Info);Host->TickLootNotifications();
    TestEqual(TEXT("Disabled selected/assessed notifications stay silent"),Client->ChatHistory.Num(),3);
    P->Profile=Parse(TEXT(R"({"loot_rules":[{"label":"Ignore rings","action":"skip","name":"Ring"},{"label":"Later keep","action":"keep"}]})"));
    Host->TickLootNotifications();
    if(TestEqual(TEXT("New profile re-evaluates current selection"),Client->ChatHistory.Num(),4))
        TestTrue(TEXT("Skip is a real first-match result"),Client->ChatHistory.Last().Text.Contains(TEXT("Ignore rings\" (skip)")));
    // Large imported profiles must cooperate across callbacks and remain
    // bounded. No full inventory, routes or meta libraries enter this VM.
    TArray<TSharedPtr<FJsonValue>> Rules;
    for(int32 I=0;I<600;++I)Rules.Add(MakeShared<FJsonValueObject>(Parse(TEXT(R"({"name":"Not this item","action":"keep"})"))));
    Rules.Add(MakeShared<FJsonValueObject>(Parse(TEXT(R"({"label":"Last rule","action":"keep"})"))));
    P->Profile=MakeShared<FJsonObject>();P->Profile->SetArrayField(TEXT("loot_rules"),Rules);
    Host->TickLootNotifications();TestTrue(TEXT("Large scan yields with its cursor intact"),Host->LootMatchSnapshot.IsValid());
    TestEqual(TEXT("Incomplete scans do not print a result"),Client->ChatHistory.Num(),4);
    for(int32 I=0;I<10&&Host->LootMatchSnapshot;++I)Host->TickLootNotifications();
    TestFalse(TEXT("Cooperative scan completes"),Host->LootMatchSnapshot.IsValid());
    if(TestEqual(TEXT("Cooperative scan prints once"),Client->ChatHistory.Num(),5))
        TestTrue(TEXT("Cooperative scan reaches the final rule"),Client->ChatHistory.Last().Text.Contains(TEXT("Last rule")));
    for(int32 I=1000;I<2000;++I)Host->QueueLootMatch(I);
    TestTrue(TEXT("Assessment burst queue is bounded"),Host->LootMatchQueue.Num()<=32);
    Session.State=EACESessionState::Disconnected;Host->TickLootNotifications();
    TestTrue(TEXT("Logout releases jobs, history and VM"),Host->LootMatchQueue.IsEmpty()&&Host->LootMatchReported.IsEmpty()&&!Host->LootMatchVM);
    Session.State=EACESessionState::InWorld;

    FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto Snapshot=[&](){return Parse(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"action_serial":0,"action_error":0,"container":700,"container_is_corpse":true,"position":{"cell":2103705613,"x":25,"y":97,"z":12},"spells":[],"enchantments":[],"targets":[],"inventory":[],"contents":[{"id":701,"wcid":701,"name":"Ring","count":2,"identified":true,"type":8}]})"));};
    auto Profile=[&](){return Parse(TEXT(R"({"buffing":false,"recovery":false,"combat_enabled":false,"looting":true,"loot_rules":[{"label":"Jewelry","action":"keep","name":"Ring"}]})"));};
    auto Step=[&](FACEPluginVM& VM,const auto& S,const auto& Profile){TSharedPtr<FJsonObject> I;TestTrue(*Error,VM.Step(S,Profile,I,Error));return I?I:MakeShared<FJsonObject>();};
    for(bool Enabled:{true,false})
    {
        FACEPluginVM VM;if(!TestTrue(TEXT("Pickup policy loads"),VM.Load(Source,Error)))return false;
        auto S=Snapshot(),ProfileData=Profile();ProfileData->SetBoolField(TEXT("loot_notify_pickup"),Enabled);
        auto I=Step(VM,S,ProfileData);
        TestEqual(TEXT("Rule sends the normal loot action"),I->GetStringField(TEXT("action")),FString(TEXT("loot")));
        TestFalse(TEXT("Sending pickup is not a success notification"),I->HasField(TEXT("loot_notification")));
        S->SetNumberField(TEXT("time"),101);I=Step(VM,S,ProfileData);
        TestFalse(TEXT("Unconfirmed pickup remains silent"),I->HasField(TEXT("loot_notification")));
        S->SetArrayField(TEXT("inventory"),S->GetArrayField(TEXT("contents")));S->SetArrayField(TEXT("contents"),{});S->SetNumberField(TEXT("container"),0);
        I=Step(VM,S,ProfileData);TestEqual(TEXT("Confirmed receipt honors pickup toggle"),I->HasField(TEXT("loot_notification")),Enabled);
        if(Enabled)
        {
            TestTrue(TEXT("Receipt includes quantity and matched rule"),I->GetStringField(TEXT("loot_notification")).Contains(TEXT("2 x Ring (rule \"Jewelry\", keep)")));
            auto Notice=MakeShared<FJsonObject>();Notice->SetStringField(TEXT("loot_notification"),I->GetStringField(TEXT("loot_notification")));
            P->Profile=ProfileData;P->Profile->SetBoolField(TEXT("loot_notify_match"),false);
            const int32 Before=Client->ChatHistory.Num();Host->Execute(*P,Notice);
            TestEqual(TEXT("Policy receipt is printed in local chat"),Client->ChatHistory.Num(),Before+1);
            TestEqual(TEXT("Pickup receipts also use the plugin chat filter"),Client->ChatHistory.Last().FilterType,ACEChatMessageType::Plugin);
            P->Profile->SetBoolField(TEXT("loot_notify_pickup"),false);Host->Execute(*P,Notice);
            TestEqual(TEXT("Native dispatcher also honors pickup toggle"),Client->ChatHistory.Num(),Before+1);
        }
        I=Step(VM,S,ProfileData);TestFalse(TEXT("Confirmed pickup cannot notify twice"),I->HasField(TEXT("loot_notification")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),ProfileData=Profile();Step(VM,S,ProfileData);
        S->SetNumberField(TEXT("time"),111);auto I=Step(VM,S,ProfileData);
        TestFalse(TEXT("Timed-out transfer never claims pickup success"),I->HasField(TEXT("loot_notification")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),ProfileData=Profile();
        auto Owned=Parse(TEXT(R"({"id":702,"wcid":701,"name":"Ring","count":3,"identified":true,"type":8})"));
        S->SetArrayField(TEXT("inventory"),{MakeShared<FJsonValueObject>(Owned)});Step(VM,S,ProfileData);
        S->SetNumberField(TEXT("container"),0);S->SetArrayField(TEXT("contents"),{});
        TestFalse(TEXT("Corpse closing and a preexisting stack do not confirm pickup"),Step(VM,S,ProfileData)->HasField(TEXT("loot_notification")));
        Owned->SetNumberField(TEXT("count"),5);auto I=Step(VM,S,ProfileData);
        TestTrue(TEXT("Confirmed stack merge reports just the received amount"),I->HasField(TEXT("loot_notification"))&&I->GetStringField(TEXT("loot_notification")).Contains(TEXT("2 x Ring")));
        TestFalse(TEXT("Merged pickup reports only once"),Step(VM,S,ProfileData)->HasField(TEXT("loot_notification")));
    }
    TestTrue(TEXT("All notifications remain local, with no server packets"),Session.CachedC2SPackets.IsEmpty());
    return true;
}
#endif
