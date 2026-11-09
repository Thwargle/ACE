#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMLootHandoffTest,"ACE.Plugins.LootSalvageHandoff",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMLootHandoffTest::RunTest(const FString&)
{
    auto Json=[](const TCHAR* Text){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;};
    FString Source,Error;
    FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    FACEPluginVM VM;if(!TestTrue(TEXT("UCM policy loads"),VM.Load(Source,Error)))return false;
    auto S=Json(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"action_serial":0,"action_error":0,"container":0,"contents_ready":true,"position":{"cell":30998795,"x":30,"y":-34,"z":0},"spells":[],"targets":[],"inventory":[{"id":9,"object_class":40,"salvage_tool_available":true}],"corpses":[{"id":700,"name":"First corpse","distance":1,"identified":true},{"id":701,"name":"Second corpse","distance":2,"identified":true}]})"));
    auto P=Json(TEXT(R"({"combat":"off","buffing":false,"recovery":false,"looting":true,"idle_peace":false,"loot_rules":[{"action":"salvage","material":59}],"navigation":true,"route":[{"cell":30998795,"x":45,"y":-34,"z":0}]})"));
    auto Tool=S->GetArrayField(TEXT("inventory"))[0]->AsObject();Tool->SetNumberField(TEXT("wcid"),900);Tool->SetStringField(TEXT("name"),TEXT("Ust"));Tool->SetNumberField(TEXT("count"),1);
    auto Step=[&]()
    {
        S->SetNumberField(TEXT("time"),S->GetNumberField(TEXT("time"))+.05);
        TSharedPtr<FJsonObject> I;TestTrue(*Error,VM.Step(S,P,I,Error));return I?I:MakeShared<FJsonObject>();
    };
    auto Action=[](auto I){FString A;I->TryGetStringField(TEXT("action"),A);return A;};
    auto Next=Step();
    for(int32 Corpse:{700,701})
    {
        TestEqual(TEXT("Next corpse starts on the next eligible decision"),Action(Next),FString(TEXT("open_corpse")));
        TestEqual(TEXT("Completed corpse is not reopened"),Next->GetNumberField(TEXT("item")),double(Corpse));
        TestTrue(TEXT("No repeated open while waiting for the server"),Action(Step()).IsEmpty());
        S->SetNumberField(TEXT("container"),Corpse);
        auto Item=Json(TEXT(R"({"id":20,"wcid":200,"name":"Copper Ring","material":59,"workmanship":7.5,"count":1,"identified":true,"resource_available":true})"));
        Item->SetNumberField(TEXT("id"),Corpse+100);Item->SetNumberField(TEXT("container"),Corpse);
        S->SetArrayField(TEXT("contents"),{MakeShared<FJsonValueObject>(Item)});
        TestEqual(TEXT("Loot rule takes the matched item"),Action(Step()),FString(TEXT("loot")));
        TestTrue(TEXT("No repeated pickup before ownership confirmation"),Action(Step()).IsEmpty());
        auto Inventory=S->GetArrayField(TEXT("inventory"));Item->SetNumberField(TEXT("container"),1);
        Inventory.Add(MakeShared<FJsonValueObject>(Item));S->SetArrayField(TEXT("inventory"),Inventory);S->SetArrayField(TEXT("contents"),{});
        auto Close=Step();TestEqual(TEXT("Confirmed pickup closes the completed corpse"),Action(Close),FString(TEXT("close_corpse")));
        TestTrue(TEXT("Close requests a prompt continuation"),Close->GetBoolField(TEXT("continue_work")));
        S->SetNumberField(TEXT("container"),0);
        auto Salvage=Step();TestEqual(TEXT("Queued loot is salvaged immediately after close"),Action(Salvage),FString(TEXT("salvage")));
        TestEqual(TEXT("Only the confirmed received item is salvaged"),Salvage->GetNumberField(TEXT("item")),double(Corpse+100));
        S->SetNumberField(TEXT("action_serial"),Corpse);
        TestTrue(TEXT("Unrelated successful use cannot confirm salvage"),Action(Step()).IsEmpty());
        Inventory.RemoveAt(1);S->SetArrayField(TEXT("inventory"),Inventory);
        Next=Step(); // authoritative removal; no three-second simulated time jump
    }
    TestEqual(TEXT("Final salvage hands back to navigation"),Action(Next),FString(TEXT("move")));
    TestTrue(TEXT("Policy handoffs require no elapsed multi-second delays"),S->GetNumberField(TEXT("time"))<102.);
    return !HasAnyErrors();
}
#endif
