#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMSchedulingTest,"ACE.Plugins.SchedulingParity",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMSchedulingTest::RunTest(const FString&)
{
 auto Json=[](const TCHAR* Text){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;};
 FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
 auto Snapshot=[&](){return Json(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"action_serial":0,"action_error":0,"container":0,"teleport_sequence":1,"position":{"cell":30998795,"x":30,"y":-34,"z":0},"spells":[],"targets":[],"inventory":[{"id":99,"wcid":99,"count":1,"type":1,"equipped":true,"identified":true,"can_wield":true,"weapon_skill":47,"damage":10,"name":"Sword"},{"id":51,"wcid":51,"name":"Potion","type":32,"identified":true,"usable":true,"count":5,"boost_vital":2,"boost":50}]})"));};
 auto Step=[&](FACEPluginVM& VM,auto S,auto P){TSharedPtr<FJsonObject> I;TestTrue(*Error,VM.Step(S,P,I,Error));return I?I:MakeShared<FJsonObject>();};
 for(bool Boost:{false,true}) for(bool Legacy:{false,true})
 {
  FACEPluginVM VM;TestTrue(TEXT("UCM loads"),VM.Load(Source,Error));auto S=Snapshot();
  auto P=Json(TEXT(R"({"buffing":false,"recovery":true,"combat":"melee","navigation":true,"route":[{"kind":"pause","seconds":20,"cell":30998795,"x":30,"y":-34,"z":0},{"cell":30998795,"x":45,"y":-34,"z":0}]})"));
  P->SetBoolField(TEXT("nav_priority"),Boost);
  P->GetArrayField(TEXT("route"))[0]->AsObject()->SetBoolField(TEXT("legacy"),Legacy);
  TestEqual(TEXT("Pause starts at native or imported waypoint"),Step(VM,S,P)->GetStringField(TEXT("status")),FString(TEXT("Pausing on route")));
  S->SetNumberField(TEXT("time"),101);S->SetNumberField(TEXT("health"),10);
  TestEqual(TEXT("Route pause never prevents emergency healing"),Step(VM,S,P)->GetNumberField(TEXT("item")),51.);
  S->SetNumberField(TEXT("health"),100);S->SetNumberField(TEXT("time"),102);
  S->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueObject>(Json(TEXT(R"({"id":500,"name":"Rat","distance":1,"identified":true,"line_of_sight":true})")))});
  TestEqual(TEXT("Combat proceeds during pause even with navigation priority"),Step(VM,S,P)->GetStringField(TEXT("action")),FString(TEXT("attack")));
  S->GetObjectField(TEXT("position"))->SetNumberField(TEXT("x"),34);Step(VM,S,P);
  S->GetObjectField(TEXT("position"))->SetNumberField(TEXT("y"),-38);Step(VM,S,P);
  S->SetArrayField(TEXT("targets"),{});Step(VM,S,P); // Cancel completed attack.
  TestFalse(TEXT("Navigation still respects remaining pause duration"),Step(VM,S,P)->HasField(TEXT("action")));
  S->SetNumberField(TEXT("time"),121);
  auto I=Step(VM,S,P);
  TestEqual(TEXT("Pause completion remains an ordered route boundary"),I->GetStringField(TEXT("status")),FString(TEXT("Route pause complete")));
  I=Step(VM,S,P);
  TestEqual(TEXT("Resume retraces combat detour rather than cutting through walls"),I->GetStringField(TEXT("status")),FString(TEXT("Returning to route")));
  TestEqual(TEXT("Detour returns to observed corner"),I->GetNumberField(TEXT("x")),34.);
  TestEqual(TEXT("Detour preserves corner depth"),I->GetNumberField(TEXT("y")),-34.);
  S->GetObjectField(TEXT("position"))->SetNumberField(TEXT("y"),-34);Step(VM,S,P);
  S->GetObjectField(TEXT("position"))->SetNumberField(TEXT("x"),30);I=Step(VM,S,P);
  TestEqual(TEXT("Timer advances exactly one waypoint after combat"),I->GetNumberField(TEXT("x")),45.);
 }
 {
  FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot();
  auto P=Json(TEXT(R"({"buffing":false,"recovery":true,"combat":"melee","looting":true,"loot_rules":[{"material":59,"min_workmanship":7,"action":"keep"},{"action":"skip"}]})"));
  S->SetNumberField(TEXT("container"),700);S->SetBoolField(TEXT("container_is_corpse"),true);
  TArray<TSharedPtr<FJsonValue>> Items;
  for(int Id=701;Id<=706;++Id)
  {
   auto Item=Json(TEXT(R"({"name":"Ring","count":1,"type":8,"material":59,"identified":false})"));Item->SetNumberField(TEXT("id"),Id);Item->SetNumberField(TEXT("wcid"),Id);Items.Add(MakeShared<FJsonValueObject>(Item));
  }
  S->SetArrayField(TEXT("contents"),Items);
  for(int Id=701;Id<=704;++Id)
  {
   auto I=Step(VM,S,P);TestEqual(TEXT("Appraisals advance without waiting for first reply"),I->GetNumberField(TEXT("item")),double(Id));
   TestEqual(TEXT("Unresolved first rule requests appraisal, never falls through to Skip"),I->GetStringField(TEXT("action")),FString(TEXT("identify")));
  }
  auto I=Step(VM,S,P);TestFalse(TEXT("At most four unresolved appraisals; no premature corpse close"),I->HasField(TEXT("action")));
  TestEqual(TEXT("Outstanding appraisals are visible to user"),I->GetStringField(TEXT("status")),FString(TEXT("Waiting for loot appraisal")));
  // Replies can arrive out of order. A completed item should be collected now.
  Items[2]->AsObject()->SetBoolField(TEXT("identified"),true);Items[2]->AsObject()->SetNumberField(TEXT("workmanship"),8);
  I=Step(VM,S,P);TestEqual(TEXT("Out-of-order matching appraisal loots immediately"),I->GetNumberField(TEXT("item")),703.);
  TestEqual(TEXT("Matching item is transferred"),I->GetStringField(TEXT("action")),FString(TEXT("loot")));
  auto Inventory=S->GetArrayField(TEXT("inventory"));Inventory.Add(Items[2]);S->SetArrayField(TEXT("inventory"),Inventory);Items.RemoveAt(2);S->SetArrayField(TEXT("contents"),Items);
  S->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueObject>(Json(TEXT(R"({"id":500,"name":"Rat","distance":1,"identified":true,"line_of_sight":true})")))});
  TestEqual(TEXT("Pending appraisal/receipt does not outrank combat"),Step(VM,S,P)->GetStringField(TEXT("action")),FString(TEXT("attack")));
  S->SetNumberField(TEXT("health"),10);
  TestEqual(TEXT("Appraisal queue never prevents healing"),Step(VM,S,P)->GetNumberField(TEXT("item")),51.);
  S->SetNumberField(TEXT("health"),100);S->SetArrayField(TEXT("targets"),{});Step(VM,S,P);
  for(auto& Item:Items){Item->AsObject()->SetBoolField(TEXT("identified"),true);Item->AsObject()->SetNumberField(TEXT("material"),1);}
  TestEqual(TEXT("Corpse closes only after all rules are resolved"),Step(VM,S,P)->GetStringField(TEXT("action")),FString(TEXT("close_corpse")));
 }
 for(bool Reply:{false,true})
 {
  FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot();
  auto P=Json(TEXT(R"({"buffing":false,"recovery":false,"combat":"off","looting":true,"loot_rules":[{"material":59,"min_workmanship":7,"action":"keep"}]})"));
  auto Item=Json(TEXT(R"({"id":701,"wcid":701,"count":1,"name":"Ring","type":8,"material":59,"identified":false})"));
  S->SetNumberField(TEXT("container"),700);S->SetArrayField(TEXT("contents"),{MakeShared<FJsonValueObject>(Item)});
  for(int Attempt=0;Attempt<3;++Attempt)
  {
   S->SetNumberField(TEXT("time"),100+Attempt*10);
   TestEqual(TEXT("Missing appraisal is retried within bounded policy"),Step(VM,S,P)->GetStringField(TEXT("action")),FString(TEXT("identify")));
   S->SetNumberField(TEXT("time"),101+Attempt*10);
   TestFalse(TEXT("Every attempt, including the last, gets time to receive its reply"),Step(VM,S,P)->HasField(TEXT("action")));
  }
  S->SetNumberField(TEXT("time"),Reply?129:130);
  if(Reply){Item->SetBoolField(TEXT("identified"),true);Item->SetNumberField(TEXT("workmanship"),8);}
  TestEqual(TEXT("Final reply is usable; exhausted requests pause only looting"),Step(VM,S,P)->GetStringField(TEXT("action")),FString(Reply?TEXT("loot"):TEXT("activity_failed")));
 }
 return !HasAnyErrors();
}
#endif
