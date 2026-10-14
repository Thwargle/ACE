#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEVTProfile.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVTWorkflowTest,"ACE.Plugins.VirindiWorkflows",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEVTWorkflowTest::RunTest(const FString&)
{
 auto Json=[](const TCHAR* Text){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;};
 FString Script,Error;TArray<FString> Issues;
 FFileHelper::LoadFileToString(Script,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
 auto Snapshot=[&](){return Json(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"action_serial":0,"action_error":0,"teleport_sequence":1,"world_name":"Test","vendor":0,"vendor_name":"Arcanist","vendor_wcid":200,"vendor_uses_pyreals":true,"pyreals":10000,"position":{"cell":2139029505,"x":84,"y":84,"z":12},"targets":[],"spells":[],"inventory":[{"id":51,"wcid":51,"name":"Potion","type":32,"identified":true,"usable":true,"count":5,"boost_vital":2,"boost":50}],"route_objects":[{"id":500,"name":"Arcanist","object_class":25,"distance":2}],"vendor_stock":[{"id":501,"wcid":300,"name":"Scarab","limit":100,"unit_value":10,"sell_rate":1}]})"));};
 auto Profile=[&](){return Json(TEXT(R"({"buffing":false,"recovery":true,"combat":"off","navigation":true,"vendor_restock":true,"vendor_rules":[{"server":"Test","vendor_name":"Arcanist","vendor_wcid":200,"item_name":"Scarab","item_wcid":300,"quantity":250}]})"));};
 auto Step=[&](FACEPluginVM& VM,const auto& S,const auto& P){TSharedPtr<FJsonObject> I;TestTrue(*Error,VM.Step(S,P,I,Error));return I?I:MakeShared<FJsonObject>();};
 auto Action=[](const auto& I){FString V;I->TryGetStringField(TEXT("action"),V);return V;};
 // NAV type 5 is Open/Close Vendor; type 7 remains an ordinary NPC Use.
 auto Nav=ACEVTProfile::Read(TEXT("uTank2 NAV 1.2\n4\n2\n5\n0\n0\n0\n0\n500\nArcanist\n5\n0\n0\n0\n0\n0\n\n"),TEXT("nav"),Error);
 if(!TestTrue(TEXT("Vendor route parses"),Nav.IsValid()))return false;
 auto Converted=ACEVTProfile::Convert(Nav,Issues);
 TestEqual(TEXT("Vendor route converts without gaps"),Issues.Num(),0);
 TestEqual(TEXT("Vendor is not generic Use"),Converted->GetArrayField(TEXT("route"))[0]->AsObject()->GetStringField(TEXT("kind")),FString(TEXT("vendor")));
 TestEqual(TEXT("Close Vendor preserves ID zero"),Converted->GetArrayField(TEXT("route"))[1]->AsObject()->GetNumberField(TEXT("object_id")),0.);
 for(bool Legacy:{false,true})
 {
  FACEPluginVM VM;TestTrue(TEXT("Workflow policy loads"),VM.Load(Script,Error));auto S=Snapshot(),P=Profile();
  if(Legacy)for(const auto& V:Converted->GetArrayField(TEXT("route")))V->AsObject()->SetStringField(TEXT("kind"),TEXT("use"));
  P->SetArrayField(TEXT("route"),Converted->GetArrayField(TEXT("route")));
  auto I=Step(VM,S,P);TestEqual(TEXT("Opens vendor first"),Action(I),FString(TEXT("use_world")));
  S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("action_error"),1);S->SetNumberField(TEXT("time"),101);
  TestFalse(TEXT("Unrelated UseDone cannot complete or fail OpenVendor"),Step(VM,S,P)->HasField(TEXT("action")));
  S->SetNumberField(TEXT("health"),10);
  TestEqual(TEXT("Waiting for VendorInfo permits recovery"),Step(VM,S,P)->GetNumberField(TEXT("item")),51.);
  S->SetNumberField(TEXT("health"),100);S->SetNumberField(TEXT("action_serial"),2);S->SetNumberField(TEXT("action_error"),0);
  S->SetNumberField(TEXT("vendor"),500);
  I=Step(VM,S,P);TestEqual(TEXT("Observed VendorInfo starts restocking"),Action(I),FString(TEXT("buy")));
  TestEqual(TEXT("Respects stock batch limit"),I->GetNumberField(TEXT("count")),100.);
  S->SetNumberField(TEXT("health"),10);S->SetNumberField(TEXT("time"),102);
  TestEqual(TEXT("Pending purchase cannot starve healing"),Step(VM,S,P)->GetNumberField(TEXT("item")),51.);
  S->SetNumberField(TEXT("health"),100);S->SetNumberField(TEXT("action_serial"),3);S->SetNumberField(TEXT("action_error"),1);
  TestFalse(TEXT("Healing error cannot fail or repeat a purchase"),Step(VM,S,P)->HasField(TEXT("action")));
  auto Items=S->GetArrayField(TEXT("inventory"));auto Scarabs=Json(TEXT(R"({"id":600,"wcid":300,"name":"Scarab","count":100})"));
  Items.Add(MakeShared<FJsonValueObject>(Scarabs));S->SetArrayField(TEXT("inventory"),Items);
  I=Step(VM,S,P);TestEqual(TEXT("Inventory confirmation permits next batch"),Action(I),FString(TEXT("buy")));
  TestEqual(TEXT("Second batch is bounded"),I->GetNumberField(TEXT("count")),100.);
  Scarabs->SetNumberField(TEXT("count"),200);I=Step(VM,S,P);TestEqual(TEXT("Final batch is only shortfall"),I->GetNumberField(TEXT("count")),50.);
  TestFalse(TEXT("Does not close vendor before final purchase replicates"),Step(VM,S,P)->HasField(TEXT("action")));
  Scarabs->SetNumberField(TEXT("count"),250);I=Step(VM,S,P);
  TestEqual(TEXT("Satisfied restock advances to Close Vendor"),Action(I),FString(TEXT("close_vendor")));
  TestEqual(TEXT("Closes exact current vendor"),I->GetNumberField(TEXT("vendor")),500.);
  TestFalse(TEXT("Closing waits for vendor state without resending"),Step(VM,S,P)->HasField(TEXT("action")));
  S->SetNumberField(TEXT("vendor"),0);I=Step(VM,S,P);
  TestEqual(TEXT("Open/buy/close route completes"),I->GetStringField(TEXT("status")),FString(TEXT("Route complete")));
 }
 {
  FACEPluginVM VM;VM.Load(Script,Error);auto S=Snapshot(),P=Profile();S->SetNumberField(TEXT("vendor"),500);P->SetBoolField(TEXT("navigation"),false);
  auto Stock=S->GetArrayField(TEXT("vendor_stock"));Stock.Add(MakeShared<FJsonValueObject>(Json(TEXT(R"({"id":502,"wcid":300,"name":"Scarab","limit":1,"unit_value":10})"))));S->SetArrayField(TEXT("vendor_stock"),Stock);
  auto I=Step(VM,S,P);TestEqual(TEXT("One resold item cannot mask the stocked stack"),I->GetNumberField(TEXT("item")),501.);
  P->SetBoolField(TEXT("vendor_restock"),false);Step(VM,S,P);S->SetNumberField(TEXT("time"),130);
  TestFalse(TEXT("Disabling restock cancels old purchase watchdog"),Step(VM,S,P)->HasField(TEXT("action")));
 }
 for(bool Split:{false,true})
 {
  FACEPluginVM VM;VM.Load(Script,Error);auto S=Snapshot(),P=Profile();S->SetNumberField(TEXT("vendor"),500);S->SetNumberField(TEXT("pyreals"),0);P->SetBoolField(TEXT("navigation"),false);
  P->GetArrayField(TEXT("vendor_rules"))[0]->AsObject()->SetNumberField(TEXT("quantity"),10);
  auto Note=Json(TEXT(R"({"id":700,"wcid":400,"name":"Trade Note","count":1,"unit_value":1000})"));if(Split)Note->SetNumberField(TEXT("count"),10);
  auto Items=S->GetArrayField(TEXT("inventory"));Items.Add(MakeShared<FJsonValueObject>(Note));S->SetArrayField(TEXT("inventory"),Items);S->SetArrayField(TEXT("vendor_trade_notes"),{MakeShared<FJsonValueObject>(Note)});
  TestEqual(TEXT("Starts appropriate note operation"),Action(Step(VM,S,P)),FString(Split?TEXT("split_note"):TEXT("sell_note")));
  S->SetNumberField(TEXT("health"),10);S->SetNumberField(TEXT("time"),101);
  TestEqual(TEXT("Note split/sale permits healing"),Step(VM,S,P)->GetNumberField(TEXT("item")),51.);
  S->SetNumberField(TEXT("health"),100);S->SetNumberField(TEXT("action_serial"),1);S->SetNumberField(TEXT("action_error"),1);
  TestFalse(TEXT("Unrelated recovery error cannot repeat or reject note operation"),Step(VM,S,P)->HasField(TEXT("action")));
  S->SetNumberField(TEXT("time"),116);TestEqual(TEXT("Unconfirmed note still has a bounded timeout"),Action(Step(VM,S,P)),FString(TEXT("activity_failed")));
 }
 // A navigation-only failure must leave the state machine/watchdog alive.
 {
  FACEPluginVM VM;VM.Load(Script,Error);auto S=Snapshot(),P=Profile();P->SetBoolField(TEXT("vendor_restock"),false);
  P->SetArrayField(TEXT("route"),Json(TEXT(R"({"v":[{"cell":2139029505,"x":94,"y":84,"z":12}]})"))->GetArrayField(TEXT("v")));
  P->SetArrayField(TEXT("vt_meta"),Json(TEXT(R"({"v":[{"state":"Default","condition":{"type":6,"value":5},"action":{"type":1,"value":"Recovery"}},{"state":"Recovery","condition":{"type":1},"action":{"type":2,"command":{"op":"echo","text":"Recovery meta still running"}}},{"state":"Recovery","condition":{"type":6,"value":2},"action":{"type":2,"command":{"op":"option","key":"navigation","value":true}}}]})"))->GetArrayField(TEXT("v")));
  Step(VM,S,P);
  P->SetObjectField(TEXT("ucm_activity_failure"),Json(TEXT(R"({"activity":"navigation","status":"Blocked"})")));
  TestEqual(TEXT("Route failure pauses only navigation"),Action(Step(VM,S,P)),FString(TEXT("pause_navigation")));
  P->RemoveField(TEXT("ucm_activity_failure"));P->SetBoolField(TEXT("navigation"),false);S->SetNumberField(TEXT("time"),106);
  Step(VM,S,P);auto I=Step(VM,S,P);
  TestEqual(TEXT("Paused route allows a meta recovery state"),I->GetStringField(TEXT("text")),FString(TEXT("Recovery meta still running")));
  TestFalse(TEXT("State transition alone cannot resume blocked movement"),Step(VM,S,P)->HasField(TEXT("action")));
  S->SetNumberField(TEXT("time"),109);
  TestEqual(TEXT("Explicit meta navigation enable retries route"),Action(Step(VM,S,P)),FString(TEXT("move")));
 }
 {
  auto P=Json(TEXT(R"({"buffing":false,"recovery":false,"combat":"off","navigation":false,"looting":false,"vt_meta":[
   {"state":"Default","condition":{"type":1},"action":{"type":3,"children":[
    {"type":2,"command":{"op":"load","profile":"quest.usd","kind":"usd"}},
    {"type":2,"command":{"op":"option","key":"looting","value":true}},
    {"type":3,"children":[{"type":2,"command":{"op":"load","profile":"quest.utl","kind":"utl"}},
     {"type":2,"command":{"op":"option","key":"navigation","value":true}}]},
    {"type":4,"route_profile":"Quest_Path","route":{"route":[{"cell":2139029505,"x":100,"y":84,"z":12}]}},
    {"type":11,"key":"looting","boolean":true,"scale":1,"properties":{"v":"loot_enabled"}},
    {"type":1,"value":"Running"}]}},
   {"state":"Running","condition":{"type":26,"expression":{"op":"call","name":"getvar","args":[{"op":"literal","value":"loot_enabled"}]}},"action":{"type":1,"value":"Verified"}}
  ]})"));
  auto S=Snapshot();FACEPluginVM VM;VM.Load(Script,Error);
  auto I=Step(VM,S,P);
  TestEqual(TEXT("Meta first loads character settings"),Action(I),FString(TEXT("profile_load")));
  TestEqual(TEXT("Later state change waits for load"),I->GetStringField(TEXT("meta_state")),FString(TEXT("Default")));
  TestFalse(TEXT("Later embedded route waits for load"),I->HasField(TEXT("runtime_route")));
  P->SetBoolField(TEXT("ucm_resume"),true);I=Step(VM,S,P);P->RemoveField(TEXT("ucm_resume"));
  TestEqual(TEXT("Resume retries an unacknowledged local profile load"),Action(I),FString(TEXT("profile_load")));
  P->SetNumberField(TEXT("vt_revision"),1);P->SetStringField(TEXT("vt_loaded_kind"),TEXT("usd"));
  S->SetNumberField(TEXT("time"),100.3);I=Step(VM,S,P);
  TestEqual(TEXT("Nested actions load loot second"),I->GetStringField(TEXT("profile")),FString(TEXT("quest.utl")));
  P->SetNumberField(TEXT("vt_revision"),2);P->SetStringField(TEXT("vt_loaded_kind"),TEXT("utl"));
  S->SetNumberField(TEXT("time"),100.6);I=Step(VM,S,P);
  TestEqual(TEXT("Options after USD are retained and visible to later rules"),I->GetStringField(TEXT("meta_state")),FString(TEXT("Verified")));
  TestTrue(TEXT("Embedded route installs after dependency loads"),I->HasField(TEXT("runtime_route")));
  S->SetNumberField(TEXT("time"),101);I=Step(VM,S,P);
  TestEqual(TEXT("Navigation enabled after loads starts walking"),Action(I),FString(TEXT("move")));
 }
 return true;
}
#endif
