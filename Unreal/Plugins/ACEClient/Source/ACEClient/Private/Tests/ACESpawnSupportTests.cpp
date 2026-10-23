#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEWorldPresenterComponent.h"
#include "ACETerrainPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACELandblockActor.h"
#include "ACEEnvCellActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESpawnSupportTest, "ACE.RetailParity.SpawnSupport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACESpawnSupportTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    auto* Owner=World->SpawnActor<AActor>();
    auto* Terrain=NewObject<UACETerrainPresenterComponent>(Owner);
    Owner->AddInstanceComponent(Terrain); Terrain->RegisterComponent();
    Terrain->bHasKnownCell=true; Terrain->LastKnownCellId=0x7D640014;
    auto* Presenter=NewObject<UACEWorldPresenterComponent>(Owner);
    Owner->AddInstanceComponent(Presenter); Presenter->RegisterComponent();
    Presenter->Client=GI->GetSubsystem<UACEClientSubsystem>();
    FACEWorldObject Statue; Statue.Guid=0x77D64033; Statue.SetupId=0x020007DD;
    Statue.ItemType=0x2000; Statue.PhysicsState=0x418;
    Statue.ObjectDescriptionFlags=0x14; Statue.bHasPosition=true;
    Statue.Position.CellId=0x7D640014; Statue.Position.Location=FVector(60,84,50);
    Presenter->HandleObjectCreated(Statue); Presenter->DrainPendingSpawns();
    TestNull(TEXT("Cold spawn waits for DAT physics metadata"),Presenter->FindEntityActor(Statue.Guid));
    TestEqual(TEXT("Cold spawn is retained"),Presenter->PendingSpawns.Num(),1);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    Dat->SetLightweightStreaming(false);
    Dat->GetOrBuildLandblockMesh(0x7D640000,100);
    float GroundZ=0;
    const FVector XY=Statue.Position.ToUnrealLocation(100);
    if(!TestTrue(TEXT("Yaraq terrain samples before scenery is resident"),Dat->SampleOutdoorGroundZ(XY.X,XY.Y,100,GroundZ))) return false;
    Statue.Position.Location.Z=(GroundZ+500)/100;
    Presenter->HandleObjectCreated(Statue); Presenter->DrainPendingSpawns();
    auto* Entity=Presenter->FindEntityActor(Statue.Guid);
    if(!TestNotNull(TEXT("Archive completion creates queued statue"),Entity)) return false;
    TestTrue(TEXT("Statue has authored movement spheres"),!Entity->MovementSpheresAc.IsEmpty());
    TestTrue(TEXT("Statue starts gravity even with zero initial velocity"),Entity->bHavePhysicsVelocity);
    TestTrue(TEXT("Presenter supplies collision residency source"),Entity->PlacementTerrain.Get()==Terrain);
    const FVector Start=Entity->GetActorLocation();
    for(int I=0;I<120;++I) Entity->Tick(.1f);
    TestTrue(TEXT("Delayed outdoor scenery never lets statue sink to terrain"),Entity->GetActorLocation().Equals(Start,.01));
    TestTrue(TEXT("Waiting does not accumulate fall velocity"),Entity->AcePhysicsVelocity.IsNearlyZero());

    Dat->GetOrBuildLandblockMesh(0x7D640000,100);
    auto* Land=World->SpawnActor<AACELandblockActor>();
    if(!TestTrue(TEXT("Yaraq loads"),Land->LoadLandblock(0x7D640000,100))) return false;
    Terrain->Spawned.Add(int32(0x7D640000),Land);
    const double Deadline=FPlatformTime::Seconds()+30;
    while(!Land->IsPlacementCollisionReady() && FPlatformTime::Seconds()<Deadline)
    { ++GFrameCounter; Land->Tick(.016f); FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); FPlatformProcess::Sleep(.001f); }
    if(!TestTrue(TEXT("Authored scenery and collision finish loading"),Terrain->IsObjectCellCollisionReady(Statue.Position.CellId))) return false;

    // Hold a late authored table/pedestal separately from the already resident
    // heightfield. RegionDesc flora must not hold up object physics.
    AACELandblockActor::FPendingScenery Pending; Pending.ModelId=0x02000162;
    Land->PendingScenery.Add(Pending);
    TestFalse(TEXT("Resident terrain alone is insufficient for prop placement"),Terrain->IsObjectCellCollisionReady(Statue.Position.CellId));
    for(int I=0;I<60;++I) Entity->Tick(.1f);
    TestTrue(TEXT("Late authored support keeps original server position"),Entity->GetActorLocation().Equals(Start,.01));
    Land->PendingScenery.Last().bRegionDesc=true;
    TestTrue(TEXT("Decorative flora does not block settling"),Terrain->IsObjectCellCollisionReady(Statue.Position.CellId));
    Land->PendingScenery.Pop();
    auto* Support=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Support);
    Support->SetRootComponent(Box); Box->SetBoxExtent(FVector(200,200,10));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
    const float SurfaceZ=GroundZ+300;
    Support->SetActorLocation(FVector(Start.X,Start.Y,SurfaceZ-10));
    float Bottom=MAX_flt; for(const auto& S:Entity->MovementSpheresAc) Bottom=FMath::Min(Bottom,S.Z-S.W);
    const float ExpectedZ=SurfaceZ-Bottom*100+1;
    for(int FPS:{5,30,144})
    {
        Entity->InitializeFromObject(Statue,100,false);
        for(int I=0;I<FPS*3;++I) Entity->Tick(1.f/FPS);
        TestTrue(TEXT("Statue lands on support at all frame rates"),FMath::IsNearlyEqual(Entity->GetActorLocation().Z,double(ExpectedZ),.1));
        TestFalse(TEXT("Contact retires gravity rather than falling through support"),Entity->bHavePhysicsVelocity);
    }
    Entity->Destroy();

    FACEWorldObject Loose=Statue; Loose.Guid+=1; Loose.SetupId=0x0200017C;
    Loose.ItemType=ACEItemType::Caster; Loose.ObjectDescriptionFlags=0; Loose.PhysicsState=ACEPhysicsState::Gravity;
    auto* Dropped=World->SpawnActor<AACEWorldEntityActor>(); Dropped->PlacementTerrain=Terrain;
    Dropped->InitializeFromObject(Loose,100,false);
    const double AirZ=Dropped->GetActorLocation().Z;
    TestTrue(TEXT("A loose item receives its physics metadata on the first spawn"),!Dropped->MovementSpheresAc.IsEmpty());
    for(int I=0;I<180;++I) Dropped->Tick(1.f/60);
    TestTrue(TEXT("Zero-velocity loose item falls instead of remaining suspended"),Dropped->GetActorLocation().Z<AirZ-50);
    TestTrue(TEXT("Loose item rests above its support"),Dropped->GetActorLocation().Z>=SurfaceZ-1);
    Dropped->Destroy();

    // Exercise actual Academy room/furniture residency as well as outdoors.
    constexpr uint32 Cell=0x8C04023C;
    TestFalse(TEXT("An absent indoor cell cannot support props"),Terrain->IsObjectCellCollisionReady(Cell));
    FACEPosition Indoor; Indoor.CellId=Cell;
    Dat->GetOrBuildEnvCellMesh(Cell,100);
    auto* Room=World->SpawnActor<AACEEnvCellActor>();
    if(!TestTrue(TEXT("Academy room loads"),Room->LoadEnvCell(Cell,Indoor.ToUnrealLocation(100),100))) return false;
    Terrain->SpawnedEnvCells.Add(int32(Cell),Room); Room->EnsureStaticObjectsQueued(); Room->SetEnvCellCollisionActive(true);
    TestTrue(TEXT("Academy has pending furniture"),Room->HasPendingStaticObjects());
    TestFalse(TEXT("Room floor is not enough while its table is queued"),Terrain->IsObjectCellCollisionReady(Cell));
    const double RoomDeadline=FPlatformTime::Seconds()+20;
    while(!Terrain->IsObjectCellCollisionReady(Cell) && FPlatformTime::Seconds()<RoomDeadline)
    { ++GFrameCounter; Room->Tick(.016f); FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); FPlatformProcess::Sleep(.001f); }
    TestTrue(TEXT("Room props can settle once furniture collision is ready"),Terrain->IsObjectCellCollisionReady(Cell));
    return true;
}
#endif
