#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMFinishingTest,"ACE.Plugins.StreakFinishing",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMFinishingTest::RunTest(const FString&)
{
    auto Json=[](const TCHAR* Text){TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J);return J;};
    FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto State=[&](){return Json(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"combat_mode":8,"components_required":false,"action_serial":0,"spells":[{"id":10,"name":"Fire Bolt","school":1,"category":121,"power":300,"skill":600},{"id":11,"name":"Fire Streak","school":1,"category":247,"power":350,"skill":600},{"id":12,"name":"Fire Ring","school":1,"category":226,"power":400,"skill":600}],"targets":[{"id":50,"name":"Tusker","identified":true,"distance":3,"health_fraction":1,"max_health":1000,"health_observed_at":100,"line_of_sight":true}],"inventory":[{"id":90,"name":"Fire wand","type":32768,"damage_type":16,"identified":true,"can_wield":true,"equipped":true}]})"));};
    auto Profile=[&](){return Json(TEXT(R"({"buffing":false,"recovery":false,"looting":false,"combat":"magic","damage_type":16,"monsters":[{"default":true,"bolt":true,"streak":true,"ring":false}]})"));};
    auto Run=[&](FACEPluginVM& VM,auto S,auto P){TSharedPtr<FJsonObject> Out;TestTrue(*Error,VM.Step(S,P,Out,Error));return Out?Out:MakeShared<FJsonObject>();};
    FACEPluginVM VM;TestTrue(TEXT("Shipped UCM loads"),VM.Load(Source,Error));auto S=State(),P=Profile();
    auto Target=S->GetArrayField(TEXT("targets"))[0]->AsObject();auto Rule=P->GetArrayField(TEXT("monsters"))[0]->AsObject();
    auto Spell=[&](int32 Expected,const TCHAR* Why){auto I=Run(VM,S,P);TestEqual(Why,I->GetNumberField(TEXT("spell")),double(Expected));};
    Spell(10,TEXT("Mixed mode starts with bolt even when streak difficulty is higher"));
    Target->SetNumberField(TEXT("health_fraction"),.05);Target->SetNumberField(TEXT("health_observed_at"),101);S->SetNumberField(TEXT("time"),101);
    Spell(10,TEXT("VT threshold is strict: 50 HP is not below floor(350/7)"));
    Target->SetNumberField(TEXT("health_fraction"),.049);Target->SetNumberField(TEXT("health_observed_at"),102);S->SetNumberField(TEXT("time"),102);
    Spell(11,TEXT("Low remaining HP selects streak finisher"));
    Target->SetNumberField(TEXT("health_fraction"),.11);Target->SetNumberField(TEXT("health_observed_at"),103);S->SetNumberField(TEXT("time"),104);
    S->SetArrayField(TEXT("combat_events"),Json(TEXT(R"({"items":[{"serial":1,"target":50,"hit":true,"spell":10,"damage":100,"time":104}]})"))->GetArrayField(TEXT("items")));
    Spell(11,TEXT("Confirmed damage estimates 10 HP and uses actual damage/7 threshold"));
    Spell(11,TEXT("Repeated receipt is not subtracted a second time"));
    Target->SetNumberField(TEXT("health_fraction"),.01);Target->SetNumberField(TEXT("health_observed_at"),105);S->SetNumberField(TEXT("time"),105);
    Spell(11,TEXT("New health sample supersedes older damage without double subtraction"));
    Target->SetNumberField(TEXT("health_fraction"),.02);Target->SetNumberField(TEXT("health_observed_at"),106);S->SetNumberField(TEXT("time"),106);
    Spell(10,TEXT("Actual hit damage overrides difficulty: 20 HP is above floor(100/7)"));
    Target->SetNumberField(TEXT("health_fraction"),.001);Target->SetNumberField(TEXT("health_observed_at"),105);
    Spell(10,TEXT("An older health sample cannot replace a newer estimate"));
    Target->SetNumberField(TEXT("id"),51);Target->SetNumberField(TEXT("health_fraction"),.04);Target->SetNumberField(TEXT("health_observed_at"),107);S->SetNumberField(TEXT("time"),107);
    Spell(11,TEXT("Other target does not inherit prior target damage"));
    Target->RemoveField(TEXT("health_fraction"));Target->RemoveField(TEXT("max_health"));Target->RemoveField(TEXT("health_observed_at"));Target->SetNumberField(TEXT("id"),52);
    Spell(10,TEXT("Unknown HP does not become a finishing target"));
    Target->SetNumberField(TEXT("max_health"),0);Target->SetNumberField(TEXT("health_fraction"),.02);Target->SetNumberField(TEXT("health_observed_at"),108);
    P->SetObjectField(TEXT("monster_catalog"),Json(TEXT(R"({"Tusker":{"max_health":1000}})")));
    Spell(11,TEXT("Catalog max HP resolves fractional health when appraisal max HP is unavailable"));
    Rule->SetBoolField(TEXT("bolt"),false);
    Spell(11,TEXT("Streak-only mode does not require low HP"));
    auto Book=S->GetArrayField(TEXT("spells"));Book.RemoveAt(1);S->SetArrayField(TEXT("spells"),Book);
    Spell(10,TEXT("Unavailable streak falls back to usable bolt"));
    Rule->SetBoolField(TEXT("streak"),false);Rule->SetBoolField(TEXT("ring"),true);
    Spell(12,TEXT("Ring-only mode works with one target"));
    Rule->SetBoolField(TEXT("bolt"),true);Rule->SetBoolField(TEXT("streak"),true);P->SetNumberField(TEXT("ring_min_targets"),1);
    Spell(12,TEXT("Eligible ring keeps priority over mixed bolt/streak"));
    return true;
}
#endif
