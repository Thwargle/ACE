#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginSight.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMRouteRejoinTest,"ACE.Plugins.LongCombatRouteRejoin",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMRouteRejoinTest::RunTest(const FString&)
{
    auto Json=[](const TCHAR* Text){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;};
    FString Script,Error;FFileHelper::LoadFileToString(Script,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto Snapshot=[&](){return Json(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"action_serial":0,"action_error":0,"container":0,"teleport_sequence":1,"movement_blocked_serial":0,"position":{"cell":30998795,"x":30,"y":0,"z":0},"spells":[],"targets":[],"inventory":[{"id":99,"type":256,"equipped":true,"identified":true,"can_wield":true,"weapon_skill":48,"damage":10,"name":"Thrown weapon"}]})"));};
    auto Profile=[&](){return Json(TEXT(R"({"buffing":false,"recovery":false,"combat":"missile","approach_range":60,"radius":6,"navigation":true,"route":[{"cell":30998795,"x":25,"y":0,"z":0},{"cell":30998795,"x":45,"y":0,"z":0}]})"));};
    auto Step=[&](FACEPluginVM& VM,const auto& S,const auto& P){TSharedPtr<FJsonObject> I;TestTrue(*Error,VM.Step(S,P,I,Error));return I?I:MakeShared<FJsonObject>();};
    auto At=[](const auto& S,double X,double Y){auto Pos=S->GetObjectField(TEXT("position"));Pos->SetNumberField(TEXT("x"),X);Pos->SetNumberField(TEXT("y"),Y);S->SetNumberField(TEXT("time"),S->GetNumberField(TEXT("time"))+.5);};
    for(bool Joined:{false,true})for(bool Legacy:{false,true})
    {
        FACEPluginVM VM;TestTrue(TEXT("UCM policy loads"),VM.Load(Script,Error));auto S=Snapshot(),P=Profile();
        for(const auto& V:P->GetArrayField(TEXT("route"))){V->AsObject()->SetBoolField(TEXT("legacy"),Legacy);V->AsObject()->SetBoolField(TEXT("walk_first"),true);}
        S->SetObjectField(TEXT("route_visible"),Json(TEXT(R"({"1":false,"2":true})")));
        if(Joined)TestEqual(TEXT("Already following the ordered route"),Step(VM,S,P)->GetNumberField(TEXT("x")),45.);
        auto Target=Json(TEXT(R"({"id":50,"name":"Tusker Guard","identified":true,"distance":60,"cell":30998795,"x":30,"y":60,"z":0,"attack_height":2,"line_of_sight":true})"));
        S->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueObject>(Target)});
        auto I=Step(VM,S,P);TestEqual(TEXT("60m acquisition approaches before joining if combat is active"),I->GetStringField(TEXT("action")),FString(TEXT("move")));
        TestTrue(TEXT("Host records movement during combat waits"),I->GetBoolField(TEXT("route_track_movement")));
        for(double Y:{10.,20.,30.,40.,50.,55.})
        {At(S,30,Y);Target->SetNumberField(TEXT("distance"),60-Y);I=Step(VM,S,P);}
        TestEqual(TEXT("Fires after reaching the requested 6m range"),I->GetStringField(TEXT("action")),FString(TEXT("attack")));
        S->SetArrayField(TEXT("targets"),{});
        // Nothing has direct visibility to the route from the end of the chase.
        S->SetObjectField(TEXT("route_visible"),Json(TEXT(R"({"1":false,"2":false})")));
        I=Step(VM,S,P);TestEqual(TEXT("Cancels the defeated target"),I->GetStringField(TEXT("action")),FString(TEXT("cancel_attack")));
        I=Step(VM,S,P);
        for(double Y:{50.,40.,30.,20.,10.,0.})
        {
            TestEqual(TEXT("Long chase returns over its observed path instead of losing navigation"),I->GetStringField(TEXT("status")),FString(TEXT("Returning to route")));
            TestEqual(TEXT("Return follows the next visited point"),I->GetNumberField(TEXT("y")),Y);
            At(S,30,Y);
            if(Y==0)S->SetObjectField(TEXT("route_visible"),Json(TEXT(R"({"1":false,"2":true})")));
            I=Step(VM,S,P);
        }
        TestEqual(TEXT("Resumes the route after a 55m combat excursion"),I->GetNumberField(TEXT("x")),45.);
        TestFalse(TEXT("Return does not disable navigation"),I->GetBoolField(TEXT("route_join_pending")));
    }

    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);auto* GI=NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(GI);Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->OnWorldChanged(nullptr,World);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    // Record a bent 60m approach while the policy is waiting for an action;
    // the old 15m gap heuristic discarded the entire return path here.
    {
        FACEPluginVM VM;VM.Load(Script,Error);auto S=Snapshot(),P=Profile();
        auto Target=Json(TEXT(R"({"id":50,"name":"Tusker Guard","identified":true,"distance":60,"cell":30998795,"x":60,"y":30,"z":0,"line_of_sight":true})"));
        S->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueObject>(Target)});Step(VM,S,P);
        Host->bTrackRouteMovement=true;
        for(const FVector& V:{FVector(30,0,0),FVector(30,10,0),FVector(30,20,0),FVector(30,30,0),FVector(40,30,0),FVector(50,30,0),FVector(60,30,0)})
        {FACEPosition Pos;Pos.CellId=30998795;Pos.Location=V;Host->RecordRouteMovement(Pos,1);}
        TArray<TSharedPtr<FJsonValue>> Samples;
        for(const auto& Pos:Host->RouteMovementSamples){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("cell"),uint32(Pos.CellId));J->SetNumberField(TEXT("x"),Pos.Location.X);J->SetNumberField(TEXT("y"),Pos.Location.Y);J->SetNumberField(TEXT("z"),Pos.Location.Z);Samples.Add(MakeShared<FJsonValueObject>(J));}
        S->SetArrayField(TEXT("route_motion"),Samples);At(S,60,30);Target->SetNumberField(TEXT("distance"),0);Step(VM,S,P);
        S->RemoveField(TEXT("route_motion"));S->SetArrayField(TEXT("targets"),{});Step(VM,S,P);
        auto I=Step(VM,S,P);
        for(const FVector2D& V:{FVector2D(50,30),FVector2D(40,30),FVector2D(30,30),FVector2D(30,20),FVector2D(30,10),FVector2D(30,0)})
        {
            TestEqual(TEXT("Return retains corners traversed between decisions"),I->GetNumberField(TEXT("x")),V.X);
            TestEqual(TEXT("Return retains corner depth"),I->GetNumberField(TEXT("y")),V.Y);
            At(S,V.X,V.Y);I=Step(VM,S,P);
        }
        FACEPosition Teleported;Teleported.CellId=0x0143014F;Teleported.Location=FVector(2,3,0);
        Host->RecordRouteMovement(Teleported,2);
        TestEqual(TEXT("Teleport discards previous-area movement samples"),Host->RouteMovementSamples.Num(),1);
        Host->bTrackRouteMovement=false;
    }
    // Test the real floor/obstruction query, not a stubbed route_visible table.
    FACEPosition Pos;Pos.CellId=30998795;Pos.Location=FVector::ZeroVector;
    const FVector Origin=Pos.ToUnrealLocation();
    auto Box=[&](const FVector& Center,const FVector& Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* C=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(C);Actor->AddInstanceComponent(C);C->SetBoxExtent(Extent);
        C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);C->SetCollisionObjectType(ECC_WorldStatic);
        C->SetCollisionResponseToAllChannels(ECR_Block);C->RegisterComponent();Actor->SetActorLocation(Origin+Center);return Actor;
    };
    Box(FVector(0,0,-50),FVector(60000,60000,50));
    Box(FVector(-29000,0,100),FVector(100,500,100));
    FACEPluginSightQuery Sight(*World,nullptr);
    auto S=Snapshot(),P=Profile();P->SetStringField(TEXT("combat"),TEXT("off"));At(S,0,0);
    TArray<TSharedPtr<FJsonValue>> Points;
    for(int32 Index=0;Index<17;++Index)
    {
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("cell"),uint32(Pos.CellId));
        J->SetNumberField(TEXT("x"),Index<16?300+Index:0);J->SetNumberField(TEXT("y"),Index<16?0:330);J->SetNumberField(TEXT("z"),0);
        Points.Add(MakeShared<FJsonValueObject>(J));
    }
    P->SetArrayField(TEXT("route"),Points);FACEPluginVM VM;VM.Load(Script,Error);
    bool Rejoined=false;int32 PendingBatches=0;
    for(int32 Batch=0;Batch<20;++Batch)
    {
        Host->UpdateRouteVisibility(Pos,Points,Sight,S);
        const bool Pending=S->GetBoolField(TEXT("route_visible_pending"));auto I=Step(VM,S,P);
        if(Pending){++PendingBatches;TestEqual(TEXT("Budget exhaustion is still scanning, not unreachable"),I->GetStringField(TEXT("status")),FString(TEXT("Finding nearest route waypoint")));}
        else {Rejoined=I->HasField(TEXT("y"))&&I->GetNumberField(TEXT("y"))==330;break;}
    }
    TestTrue(TEXT("Long candidate paths require several bounded batches"),PendingBatches>=4);
    TestTrue(TEXT("Can join the 17th entry at 330m after rejecting nearer wall-separated points"),Rejoined);
    // A genuinely blocked different floor is still rejected; never bypass collision.
    Points.SetNum(1);Points[0]->AsObject()->SetNumberField(TEXT("x"),0);Points[0]->AsObject()->SetNumberField(TEXT("z"),6);
    Host->UpdateRouteVisibility(Pos,Points,Sight,S);
    TestFalse(TEXT("A different floor is not a reachable entry"),S->GetObjectField(TEXT("route_visible"))->GetBoolField(TEXT("1")));
    TestFalse(TEXT("Completed rejection is distinct from untested entries"),S->GetBoolField(TEXT("route_visible_pending")));
    return !HasAnyErrors();
}
#endif
