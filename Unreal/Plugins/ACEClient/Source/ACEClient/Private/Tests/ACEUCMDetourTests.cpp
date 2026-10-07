#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEVTProfile.h"
#include "Mods/ACEPluginCombat.h"
#include "ACECreatureFixtures.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMDetourTest,"ACE.Plugins.AutomaticCombatAndRouteRecovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMDetourTest::RunTest(const FString&)
{
    auto Json=[](const TCHAR* Text){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;};
    FString Script,Error;FFileHelper::LoadFileToString(Script,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto S=Json(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"action_serial":0,"action_error":0,"container":0,"teleport_sequence":1,"movement_blocked_serial":0,"position":{"cell":30998795,"x":30,"y":-34.515625,"z":0},"spells":[],"targets":[],"inventory":[{"id":99,"type":1,"equipped":true,"identified":true,"can_wield":true,"weapon_skill":47,"damage":10,"name":"Sword"}]})"));
    auto P=Json(TEXT(R"({"buffing":false,"recovery":false,"combat":"melee","navigation":true,"route":[{"cell":30998795,"x":25,"y":-34.515625,"z":0},{"cell":30998795,"x":45,"y":-34.515625,"z":0}]})"));
    FACEPluginVM VM;TestTrue(TEXT("Policy loads"),VM.Load(Script,Error));
    auto Step=[&](FACEPluginVM& V){TSharedPtr<FJsonObject> I;const bool OK=V.Step(S,P,I,Error);TestTrue(*Error,OK);return I?I:MakeShared<FJsonObject>();};
    // A closer waypoint through a wall is ineligible, regardless of camera.
    S->SetObjectField(TEXT("route_visible"),Json(TEXT(R"({"1":false,"2":true})")));
    TestEqual(TEXT("Join chooses visible waypoint over closer wall-separated point"),Step(VM)->GetNumberField(TEXT("x")),45.);
    S->RemoveField(TEXT("route_visible"));
    const auto Target=Json(TEXT(R"({"id":50,"name":"Red Rat","identified":true,"distance":1,"cell":30998795,"x":40,"y":-40,"z":0,"attack_height":1,"line_of_sight":true})"));
    S->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueObject>(Target)});
    for(FVector2D At:{FVector2D(34.804504,-34.515625),FVector2D(34.804504,-37),FVector2D(34.804504,-40),FVector2D(40,-40)})
    {
        auto Pos=S->GetObjectField(TEXT("position"));Pos->SetNumberField(TEXT("x"),At.X);Pos->SetNumberField(TEXT("y"),At.Y);S->SetNumberField(TEXT("time"),S->GetNumberField(TEXT("time"))+1);
        TestEqual(TEXT("Low creature uses low attack"),Step(VM)->GetNumberField(TEXT("height")),1.);
    }
    S->SetArrayField(TEXT("targets"),{});
    auto I=Step(VM);TestEqual(TEXT("Stops the completed attack before returning"),I->GetStringField(TEXT("action")),FString(TEXT("cancel_attack")));
    I=Step(VM);TestEqual(TEXT("After kill retraces approach instead of cutting wall to route"),I->GetStringField(TEXT("status")),FString(TEXT("Returning to route")));
    TestEqual(TEXT("Returns around the observed corner"),I->GetNumberField(TEXT("x")),34.804504);
    TestEqual(TEXT("Retains corner depth"),I->GetNumberField(TEXT("y")),-40.);
    for(FVector2D At:{FVector2D(34.804504,-40),FVector2D(34.804504,-37),FVector2D(34.804504,-34.515625)})
    {auto Pos=S->GetObjectField(TEXT("position"));Pos->SetNumberField(TEXT("x"),At.X);Pos->SetNumberField(TEXT("y"),At.Y);I=Step(VM);}
    TestEqual(TEXT("Returns to ordered destination after detour"),I->GetNumberField(TEXT("x")),45.);
    // Strongest usable trained skill wins; damage alone formerly selected a bow.
    P->SetBoolField(TEXT("navigation"),false);P->SetStringField(TEXT("combat"),TEXT("auto"));
    S->SetObjectField(TEXT("skills"),Json(TEXT(R"({"47":{"training":3,"current":146},"48":{"training":2,"current":63}})")));
    auto Items=S->GetArrayField(TEXT("inventory"));Items.Add(MakeShared<FJsonValueObject>(Json(TEXT(R"({"id":100,"type":256,"identified":true,"can_wield":true,"weapon_skill":48,"damage":1000,"name":"Thrown weapon"})"))));S->SetArrayField(TEXT("inventory"),Items);
    S->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueObject>(Target)});
    for(int Height:{1,2,3})
    {Target->SetNumberField(TEXT("attack_height"),Height);FACEPluginVM Combat;Combat.Load(Script,Error);I=Step(Combat);TestEqual(TEXT("Uses strongest trained combat skill"),I->GetNumberField(TEXT("mode")),2.);TestEqual(TEXT("Automatic target height reaches attack intent"),I->GetNumberField(TEXT("height")),double(Height));}
    Items[0]->AsObject()->SetBoolField(TEXT("can_wield"),false);Items[0]->AsObject()->SetBoolField(TEXT("equipped"),false);Items[1]->AsObject()->SetBoolField(TEXT("equipped"),true);
    FACEPluginVM Fallback;Fallback.Load(Script,Error);TestEqual(TEXT("Unavailable best weapon falls back to usable skill"),Step(Fallback)->GetNumberField(TEXT("mode")),4.);
    // A missing terminator may be restored, but actual missing data is rejected.
    const FString Tail=TEXT("UTL\r\n1\r\n0\r\nSalvageCombine\r\n15\r\n1\r\n1-10\r\n0\r\n0");
    auto D=ACEVTProfile::Read(Tail,TEXT("utl"),Error);TestTrue(TEXT("Missing final CRLF on salvage policy accepted"),D.IsValid());
    if(D){TArray<FString> Issues;ACEVTProfile::Convert(D,Issues);TestEqual(TEXT("Recovered salvage policy validates"),Issues.Num(),0);}
    TestFalse(TEXT("Truncated policy contents remain rejected"),ACEVTProfile::Read(Tail.LeftChop(3),TEXT("utl"),Error).IsValid());
    auto* GI=NewObject<UGameInstance>();GI->Init();auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(TestTrue(TEXT("Retail DAT loads for creature attack heights"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))
    {
        FACEPosition Player;Player.CellId=0x01D9010B;Player.Location=FVector(34.804504,-34.515625,0);
        int32 Index=0;for(const auto& Model:ACECreatureFixtures::Models)
        {
            FACEWorldObject Creature;ACECreatureFixtures::Apply(Creature,Index);Creature.Position=Player;
            const int32 Height=ACEPluginCombat::AttackHeight(*Dat,Creature,Player);
            AddInfo(FString::Printf(TEXT("%s auto height %d"),Model.Name,Height));
            TestEqual(*(FString(Model.Name)+TEXT(" selects correct attack quadrant")),Height,++Index);
            Creature.Position.Location.Z+=3;
            TestEqual(TEXT("Elevated targets use high attack regardless of species"),ACEPluginCombat::AttackHeight(*Dat,Creature,Player),3);
        }
    }
    GI->Shutdown();
    return true;
}
#endif
