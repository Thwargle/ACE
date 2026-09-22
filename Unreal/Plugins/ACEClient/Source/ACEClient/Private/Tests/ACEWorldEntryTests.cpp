#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "ACETerrainChunkActor.h"
#include "ACETerrainPresenterComponent.h"
#include "ACELoadingScreenActor.h"
#include "ACERetailPortalAnimation.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACECharacterCreation.h"
#include "ACEHoverTooltipWidget.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/SpringArmComponent.h"
#include "ACEInputBindings.h"
#include "Misc/ConfigCacheIni.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Async/TaskGraphInterfaces.h"
#include "Dat/ACEDiskTileCache.h"
#include "HAL/FileManager.h"

namespace
{
    struct FEntryWorld
    {
        UWorld* World;
        UGameInstance* GI;
        FEntryWorld()
        {
            const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
                .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI);
            Context.OwningGameInstance = GI; GI->Init();
        }
        ~FEntryWorld()
        {
            GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELoadingTransitionTest, "ACE.RetailParity.LoadingTransition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACELoadingTransitionTest::RunTest(const FString&)
{
    FEntryWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    Dat->EnsureLoaded();
    auto* Client=Fixture.GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
    auto* PC=Fixture.World->SpawnActor<AACEPlayerController>();PC->Client=Client;
    PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));Fixture.World->AddController(PC);
    auto* Pawn=Fixture.World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
    Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->InitCapsuleSize(22,88);Capsule->RegisterComponent();
    PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
    const double Deadline=FPlatformTime::Seconds()+15;
    do
    {
        PC->PreparePortalScreen();FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        if(PC->LoadingScreenActor && PC->LoadingScreenActor->bMeshReady) break;
        FPlatformProcess::Sleep(.002f);
    } while(FPlatformTime::Seconds()<Deadline);
    auto* Prepared=PC->LoadingScreenActor.Get();
    if(!TestTrue(TEXT("Lobby prepares the actual portal DAT mesh"),Prepared && Prepared->bMeshReady)) return false;
    TestTrue(TEXT("Prepared portal stays invisible in the lobby"),Prepared->IsHidden());
    TestFalse(TEXT("Prepared portal does not animate in the lobby"),Prepared->IsActorTickEnabled());
    Session->State=EACESessionState::EnteringWorld;PC->HandleSessionStateChanged(Session->State);
    TestTrue(TEXT("Play queues its transition outside the Slate callback"),PC->bPendingEnterWorldTransition);
    PC->PlayerTick(.016f);
    TestTrue(TEXT("Portal starts before server world entry"),PC->bEnterWorldLoading);
    TestTrue(TEXT("Play reuses the prepared tunnel"),PC->LoadingScreenActor==Prepared && Prepared->bMeshReady);
    TestFalse(TEXT("Destination work cannot start before its server pose arrives"),PC->bDestinationStreamingStarted);
    TestFalse(TEXT("Waiting for entry holds terrain installs"),Dat->IsWorldStreamingAllowed());
    // Sanctuary recall, captured from the reported indoor/outdoor bridge.
    FACEPosition Pose;Pose.CellId=0xF4180104;Pose.Location=FVector(36.90,48.70,169.805);
    Session->SetLocalPosition(Pose);Session->State=EACESessionState::InWorld;
    ++GFrameCounter;PC->TickWorldTransition();
    TestTrue(TEXT("Destination work starts on a later portal frame"),PC->bDestinationStreamingStarted);
    TestTrue(TEXT("Destination streaming is enabled"),Dat->IsWorldStreamingAllowed());
    const double HudDeadline=FPlatformTime::Seconds()+15;
    while(!PC->bGameplayUiAssetsReady && FPlatformTime::Seconds()<HudDeadline)
    {
        const int32 Before=PC->GameplayUiPrefetchIndex;
        PC->TryPrefetchGameplayHudAssets();
        TestTrue(TEXT("A HUD batch decodes at most eight textures"),PC->GameplayUiPrefetchIndex-Before<=8);
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    }
    TestTrue(TEXT("Bounded HUD warming eventually finishes"),PC->bGameplayUiAssetsReady);
    auto* Terrain=NewObject<UACETerrainPresenterComponent>(PC);PC->AddInstanceComponent(Terrain);Terrain->RegisterComponent();
    Terrain->Client=Client;Terrain->TerrainChunkSize=1;Terrain->LoadRadius=0;Terrain->UnloadRadius=0;
    Terrain->LandblockClass=AACELandblockActor::StaticClass();
    TestTrue(TEXT("Sanctuary needs its exterior before crossing the bridge exit"),Terrain->NeedsExteriorTerrain(Pose.CellId));
    TestTrue(TEXT("Ordinary town interiors also retain exterior streaming"),Terrain->NeedsExteriorTerrain(0x7D630112));
    TestFalse(TEXT("A dungeon does not stream an unrelated outdoor landscape"),Terrain->NeedsExteriorTerrain(0x0179010D));
    Dat->GetOrBuildLandblockMesh(Pose.CellId & 0xFFFF0000u,100);
    Terrain->KickLandblockLoginBurst();
    TestTrue(TEXT("Login burst schedules work without spawning terrain inline"),Terrain->Spawned.IsEmpty());
    const double TerrainDeadline=FPlatformTime::Seconds()+15;
    while(Terrain->Spawned.IsEmpty() && FPlatformTime::Seconds()<TerrainDeadline)
    {
        ++GFrameCounter;Terrain->SyncAroundCell(Pose.CellId);
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::Sleep(.002f);
    }
    TestTrue(TEXT("Sanctuary terrain loads while the player remains on the indoor bridge"),Terrain->Spawned.Contains(0xF4180000));
    TestEqual(TEXT("Loading the exterior does not move the player outdoors"),Client->GetPlayerPosition().CellId,Pose.CellId);
    Session->State=EACESessionState::CharacterSelect;PC->HandleSessionStateChanged(Session->State);
    TestFalse(TEXT("Rejected/cancelled world entry releases loading state"),PC->bEnterWorldLoading || PC->bPendingEnterWorldTransition);
    TestFalse(TEXT("Returning to character selection releases movement input"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Returning to character selection leaves portal space"),Dat->IsInPortalSpace());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACETerrainPortalRevealTest, "ACE.RetailParity.TerrainPortalReveal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACETerrainPortalRevealTest::RunTest(const FString&)
{
    FEntryWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    Dat->EnsureLoaded();
    auto* Owner=Fixture.World->SpawnActor<AActor>();
    auto* Terrain=NewObject<UACETerrainPresenterComponent>(Owner);
    auto* Land=Fixture.World->SpawnActor<AACELandblockActor>();
    auto* Chunk=Fixture.World->SpawnActor<AACETerrainChunkActor>();
    // Both reported characters arrived outdoors on this Caulcano tower. All
    // landscape actors finish loading inside the tunnel, before the reveal.
    Dat->GetOrBuildLandblockMesh(0x09090000,100);
    TestTrue(TEXT("Caulcano destination terrain loads"),Land->LoadLandblock(0x09090000,100,false,1,0));
    TestTrue(TEXT("Caulcano terrain has drawable sections"),Land->IsOutdoorTerrainMeshReady());
    Terrain->bHasKnownCell=true;
    Terrain->LastKnownCellId=0x0909000C;
    Terrain->Spawned.Add(0x09090000,Land);
    Terrain->SpawnedChunks.Add(0x08090000,Chunk);
    for(int32 Visit=0;Visit<2;++Visit)
    {
        Dat->SetInPortalSpace(true);
        Terrain->UpdateBuildingVisibility();
        TestTrue(TEXT("Portal hides complete landblock actors, including scenery"),Land->IsHidden());
        TestTrue(TEXT("Portal hides chunk actors"),Chunk->IsHidden());
        Terrain->UpdateCameraVisibility();
        TestTrue(TEXT("Camera updates cannot reveal terrain during portal space"),Land->IsHidden());
        Dat->SetInPortalSpace(false);
        Terrain->KickLandblockLoginBurst();
        // No new cell, terrain actor, movement event or room entry occurs.
        // Exercise the normal per-frame path used by a stationary login.
        Terrain->UpdateCameraVisibility();
        TestFalse(TEXT("Stationary portal exit restores landblock and scenery visibility"),Land->IsHidden());
        TestFalse(TEXT("Stationary portal exit restores chunk visibility"),Chunk->IsHidden());
        TestFalse(TEXT("Outdoor arrival reveals terrain components too"),Land->TerrainMesh->bHiddenInGame);
        Terrain->UpdateCameraVisibility();
        TestFalse(TEXT("The next camera update retains the revealed world"),Land->IsHidden());
    }
    // Revealing the actor must not bypass the separate baked-terrain policy.
    Terrain->bWcBakedTerrainMode=true;
    Dat->SetInPortalSpace(true);
    Terrain->UpdateBuildingVisibility();
    Dat->SetInPortalSpace(false);
    Terrain->UpdateCameraVisibility();
    TestFalse(TEXT("Baked worlds still reveal their building actors"),Land->IsHidden());
    TestTrue(TEXT("Baked worlds keep duplicate procedural terrain hidden"),Land->TerrainMesh->bHiddenInGame);
    return !HasAnyErrors();
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FACETerrainArrivalTest, "ACE.RetailParity.TerrainArrival",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FACETerrainArrivalTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    Names.Add(TEXT("Desktop")); Commands.Add(TEXT("Desktop"));
    Names.Add(TEXT("ShoushiQuestBudget")); Commands.Add(TEXT("Shoushi"));
}
bool FACETerrainArrivalTest::RunTest(const FString& Parameters)
{
    FEntryWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    const double Start=FPlatformTime::Seconds();
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    Dat->EnsureLoaded();
    // A unique test fingerprint exercises cold disk misses without removing the
    // user's cache. No maintenance is run with this synthetic fingerprint.
    const uint64 Fingerprint=(uint64(GetTypeHash(FGuid::NewGuid()))<<32)|0x54455354;
    Dat->CachedDatFingerprint=Fingerprint;
    const bool Shoushi=Parameters==TEXT("Shoushi");
    const int32 Radius=Shoushi?3:5, CenterX=Shoushi?0xDA:0x7E, CenterY=Shoushi?0x55:0x65;
    TArray<uint32> Blocks;
    for(int32 X=-Radius;X<=Radius;++X) for(int32 Y=-Radius;Y<=Radius;++Y)
        Blocks.Add((uint32(CenterX + X)<<24)|(uint32(CenterY + Y)<<16));
    Blocks.Sort([CenterX,CenterY](uint32 A,uint32 B)
    {
        auto Distance=[CenterX,CenterY](uint32 V){return FMath::Square(int32(V>>24)-CenterX)+FMath::Square(int32((V>>16)&255)-CenterY);};
        return Distance(A)<Distance(B);
    });
    TArray<AACELandblockActor*> Lands;
    TSet<uint32> Pending;
    for(uint32 Id:Blocks) { Dat->RequestLandblockMesh(Id,100); Pending.Add(Id); }
    const double Deadline=Start+90;
    while(!Pending.IsEmpty() && FPlatformTime::Seconds()<Deadline)
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        for(uint32 Id:Blocks)
        {
            if(!Pending.Contains(Id) || Dat->RequestLandblockMesh(Id,100)!=UACEDatSubsystem::EACELandMeshStatus::Ready) continue;
            auto* Land=Fixture.World->SpawnActor<AACELandblockActor>();
            TestTrue(TEXT("Cold arrival terrain applies to runtime mesh"),Land->LoadLandblock(Id,100,false,1,0));
            Lands.Add(Land); Pending.Remove(Id);
        }
        FPlatformProcess::Sleep(.002f);
    }
    FlushRenderingCommands();
    TestEqual(TEXT("All visible blocks arrive without holes"),Lands.Num(),Blocks.Num());
    for(auto* Land:Lands) TestTrue(TEXT("Every arrival block has renderable terrain"),Land->IsOutdoorTerrainMeshReady());
    TSet<const FACETerrainBlend*> Unique;
    uint64 Bytes=0; int32 Sections=0;
    for(const auto& Pair:Dat->LandblockCache) for(const auto& Sec:Pair.Value->Sections)
    {
        ++Sections;
        if(Sec.SharedBake && !Unique.Contains(Sec.SharedBake.Get()))
        { Unique.Add(Sec.SharedBake.Get()); Bytes+=Sec.SharedBake->GetAllocatedSize(); }
    }
    AddInfo(FString::Printf(TEXT("Cold terrain arrival including DAT indexing, disk writes, runtime mesh application and render flush: %.3fs, %d blocks, %d sections, %d shared blends, %.1f MiB blend storage"),
        FPlatformTime::Seconds()-Start,Lands.Num(),Sections,Unique.Num(),Bytes/(1024.0*1024.0)));
    TestTrue(TEXT("Neighbor sections share their blend storage"),Unique.Num()<Sections/2);
    uint64 CpuMipBytes=0;
    for(const auto& Pair:Dat->LandTextureCache) if(auto* Texture=Pair.Value.Get())
        for(const auto& Mip:Texture->GetPlatformData()->Mips) CpuMipBytes+=Mip.BulkData.GetBulkDataSize();
    TestEqual(TEXT("Loaded terrain retains no duplicate CPU mip chains"),CpuMipBytes,uint64(0));
    Dat->ClearLoadedState(); // joins any outstanding work before removing test-owned files
    for(const FString& Dir:{ACEDiskTileCache::GetLandblocksDir(),ACEDiskTileCache::GetPCodesDir()})
    {
        TArray<FString> Files;
        const FString Suffix=FString::Printf(TEXT("_%016llX"),Fingerprint);
        IFileManager::Get().FindFiles(Files,*(Dir/TEXT("*")),true,false);
        for(const FString& Name:Files)
            if(FPaths::GetBaseFilename(Name).EndsWith(Suffix)) IFileManager::Get().Delete(*(Dir/Name),false,true);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailWorldEntryTest, "ACE.RetailParity.WorldEntry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailWorldEntryTest::RunTest(const FString& Parameters)
{
    FEntryWorld Fixture;
    auto* Dat = Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* Client = Fixture.GI->GetSubsystem<UACEClientSubsystem>();
    auto Session = Client->GetSession();
    auto* Controller = Fixture.World->SpawnActor<AACEPlayerController>();
    Controller->Client = Client;
    auto* Pawn = Fixture.World->SpawnActor<APawn>();
    auto* Capsule = NewObject<UCapsuleComponent>(Pawn);
    Capsule->InitCapsuleSize(22, 88); Pawn->SetRootComponent(Capsule);
    Pawn->AddInstanceComponent(Capsule); Capsule->RegisterComponent(); Controller->Possess(Pawn);
    Pawn->SetActorEnableCollision(false);
    auto* Presenter = NewObject<UACETerrainPresenterComponent>(Controller);
    Controller->AddInstanceComponent(Presenter); Presenter->RegisterComponent();
    Presenter->Client = Client;

    // Exact failing destination from the preserved user log. No manual collision
    // activation: portal-space visibility must prepare the still-hidden destination.
    FACEPosition Pose; Pose.CellId = 0x7D630112; Pose.Location = FVector(36.71, 89.43, 9.20);
    Session->SetLocalPosition(Pose);
	Presenter->LastKnownCellId = Pose.CellId;
	Presenter->bHasKnownCell = true;
    auto* Room = Fixture.World->SpawnActor<AACEEnvCellActor>();
    const FVector Origin = FACEPosition::AceVectorToUnreal(FVector(125 * 192, 99 * 192, 0), 100);
    Dat->GetOrCreateTexturedMaterial(0x08000708);
    TestNotNull(TEXT("Destination mesh is decoded before actor creation"), Dat->GetOrBuildEnvCellMesh(Pose.CellId, 100));
    TestTrue(TEXT("Reported stuck-login room loads from DAT"), Room->LoadEnvCell(Pose.CellId, Origin, 100));
    Presenter->SpawnedEnvCells.Add(Pose.CellId, Room);
    TestFalse(TEXT("Loaded room starts without cooked collision"), Presenter->IsPlayerCellCollisionReady());
    Dat->SetInPortalSpace(true);
    Presenter->UpdateBuildingVisibility();
    TestTrue(TEXT("Hidden destination can finish the collision readiness gate"), Presenter->IsPlayerCellCollisionReady());
    TestTrue(TEXT("Preparing physics does not reveal the destination"), Room->IsHidden());
    FVector Placement;
    TestTrue(TEXT("Actual logged-in position has a supported, unobstructed placement"), Controller->FindWorldEntryPlacement(Placement));
    AddInfo(FString::Printf(TEXT("User destination placement: %s"), *Placement.ToString()));
    for (int32 I = 0; I < 20; ++I) Presenter->UpdateBuildingVisibility();
    TestTrue(TEXT("Repeated hidden updates retain cooked room collision"), Presenter->IsPlayerCellCollisionReady());
    Presenter->SpawnedEnvCells.Reset(); Room->Destroy();

    // The failed dungeon visit entered a cell with no room triangles. Its DAT
    // stabs and adjacent cells must be usable before the portal tunnel releases.
    Pose.CellId = 0x0179010D; Pose.Location = FVector(100, -100, 0);
    Session->SetLocalPosition(Pose); Presenter->LastKnownCellId = Pose.CellId;
    Fixture.World->AddController(Controller);
    Dat->SetWorldStreamingAllowed(true);
    const FVector DungeonOrigin = FACEPosition::AceVectorToUnreal(FVector(192, 121 * 192, 0), 100);
    for (uint32 Id : {0x0179010Du, 0x01790100u, 0x01790114u, 0x0179010Cu,
                     0x01790128u, 0x0179010Eu, 0x01790106u})
    {
        TestNotNull(TEXT("Dungeon cell DAT decodes"), Dat->GetOrBuildEnvCellMesh(Id, 100));
        auto* DungeonRoom = Fixture.World->SpawnActor<AACEEnvCellActor>();
        TestTrue(TEXT("Dungeon cell actor loads"), DungeonRoom->LoadEnvCell(Id, DungeonOrigin, 100));
        Presenter->SpawnedEnvCells.Add(Id, DungeonRoom);
    }
    TestTrue(TEXT("Deferred dungeon floor counts as unfinished before queueing"),
        Presenter->SpawnedEnvCells.FindRef(Pose.CellId)->HasPendingStaticObjects());
    Presenter->UpdateBuildingVisibility();
    const double DungeonDeadline = FPlatformTime::Seconds() + 10;
    do
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        for (const auto& Pair : Presenter->SpawnedEnvCells) Pair.Value->Tick(.016f);
        if (Controller->FindWorldEntryPlacement(Placement)
            && !Presenter->SpawnedEnvCells.FindRef(Pose.CellId)->HasPendingStaticObjects()) break;
        FPlatformProcess::Sleep(.002f);
    } while (FPlatformTime::Seconds() < DungeonDeadline);
    TestTrue(TEXT("Empty dungeon transit cell gets support while portal space stays hidden"),
        Controller->FindWorldEntryPlacement(Placement));
    TestFalse(TEXT("Destination static geometry finishes before reveal"),
        Presenter->SpawnedEnvCells.FindRef(Pose.CellId)->HasPendingStaticObjects());
    TestTrue(TEXT("DAT floor supports arrival at the cell origin"),
        Placement.Equals(Pose.ToUnrealLocation(100) + FVector(0,0,88.5), .1));
    AddInfo(FString::Printf(TEXT("Dungeon placement: %s"), *Placement.ToString()));
    for (const auto& Pair : Presenter->SpawnedEnvCells)
    {
        TestTrue(TEXT("Dungeon geometry stays hidden during placement"), Pair.Value->IsHidden());
        TestTrue(TEXT("Direct transit neighbor collision is active before portal exit"), Pair.Value->GetActorEnableCollision());
        TestTrue(TEXT("Direct transit neighbor collision is cooked before portal exit"), Pair.Value->IsCollisionCooked());
        Pair.Value->Destroy();
    }
    Presenter->SpawnedEnvCells.Reset();

    // Exact rejected Town Network -> Khayyaban arrival from the user log.
    Pose.CellId = 0x9F44001A; Pose.Location = FVector(90, 24.553, 31.890);
    Session->SetLocalPosition(Pose);
    Dat->GetOrBuildLandblockMesh(0x9F440000,100);
    auto* Khayyaban = Fixture.World->SpawnActor<AACELandblockActor>();
    TestTrue(TEXT("Khayyaban terrain loads from DAT"), Khayyaban->LoadLandblock(0x9F440000,100,true,1,0));
    const double TerrainDeadline = FPlatformTime::Seconds() + 5;
    FHitResult TerrainHit;
    const FVector Arrival = Pose.ToUnrealLocation(100);
    bool bTerrainHit=false;
    do
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        bTerrainHit=Fixture.World->LineTraceSingleByChannel(TerrainHit,
            Arrival + FVector(0,0,2000), Arrival - FVector(0,0,2000), ECC_Pawn);
        if (bTerrainHit) break;
        FPlatformProcess::Sleep(.002f);
    } while (FPlatformTime::Seconds() < TerrainDeadline);
    TestTrue(TEXT("Khayyaban has cooked terrain support"),bTerrainHit);
    AddInfo(FString::Printf(TEXT("Khayyaban floor=%d arrival=%s hit=%s normal=%s"), bTerrainHit,
        *Arrival.ToString(), *TerrainHit.Location.ToString(), *TerrainHit.ImpactNormal.ToString()));
    TestTrue(TEXT("Logged Khayyaban portal position has a supported placement"), Controller->FindWorldEntryPlacement(Placement));
    TestTrue(TEXT("Khayyaban correction preserves the server XY"),FVector::Dist2D(Placement,Arrival)<1.f);
    TestTrue(TEXT("Khayyaban correction places the player above the surface"),Placement.Z>=TerrainHit.ImpactPoint.Z);
    AddInfo(FString::Printf(TEXT("Khayyaban placement: %s"), *Placement.ToString()));
    Presenter->LastKnownCellId=Pose.CellId; Presenter->TerrainChunkSize=1;
    Presenter->Spawned.Add(0x9F440000,Khayyaban);
    TestTrue(TEXT("Arrival terrain alone satisfies center readiness"),Presenter->IsLoadRadiusTerrainReadyForRadius(0));
    TestFalse(TEXT("A safe center cannot conceal missing visible neighbors"),Presenter->IsLoadRadiusTerrainReadyForRadius(1));
    for (int32 DX=-1;DX<=1;++DX) for(int32 DY=-1;DY<=1;++DY)
    {
        if(!DX && !DY) continue;
        const int32 Id=((0x9F+DX)<<24)|((0x44+DY)<<16);
        Dat->GetOrBuildLandblockMesh(Id,100);
        auto* Land=Fixture.World->SpawnActor<AACELandblockActor>();
        TestTrue(TEXT("Visible neighboring landblock decodes"),Land->LoadLandblock(Id,100,false,1,0));
        Presenter->Spawned.Add(Id,Land);
    }
    const double CoverageDeadline=FPlatformTime::Seconds()+5;
    while(!Presenter->IsLoadRadiusTerrainReadyForRadius(1) && FPlatformTime::Seconds()<CoverageDeadline)
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        for(const auto& Pair:Presenter->Spawned) Pair.Value->Tick(.016f);
        FPlatformProcess::Sleep(.002f);
    }
    TestTrue(TEXT("Complete visible terrain can release the loading screen"),Presenter->IsLoadRadiusTerrainReadyForRadius(1));
    for(const auto& Pair:Presenter->Spawned) if(Pair.Value!=Khayyaban) Pair.Value->Destroy();
    Presenter->Spawned.Reset();
    Khayyaban->Destroy();

    Pose.CellId = 0x01010001; Pose.Location = FVector(40, 40, 0); Session->SetLocalPosition(Pose);
    const FVector Feet = Pose.ToUnrealLocation(100);
    auto MakeBox = [&](FVector Center, FVector Extent)
    {
        auto* Owner = Fixture.World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Owner); Owner->SetRootComponent(Box);
        Owner->AddInstanceComponent(Box); Box->SetBoxExtent(Extent);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
        Owner->SetActorLocation(Center); return Owner;
    };
    TestFalse(TEXT("Empty space is not a safe spawn"), Controller->FindWorldEntryPlacement(Placement));
    auto* Floor = MakeBox(Feet - FVector(0,0,10), FVector(1000,1000,10));
    TestTrue(TEXT("Supported clear spawn succeeds"), Controller->FindWorldEntryPlacement(Placement));
    // Full foot sphere support must succeed on slopes, not only on flat ground.
    for (float Pitch : {15.f,30.f,45.f})
    {
        Floor->SetActorRotation(FRotator(Pitch,0,0));
        TestTrue(TEXT("Admin arrival on a walkable slope has a clear capsule"),Controller->FindWorldEntryPlacement(Placement));
        TestTrue(TEXT("Slope placement stays at the server XY"),FVector::Dist2D(Placement,Feet)<1.f);
    }
    Floor->SetActorRotation(FRotator::ZeroRotator);
    auto* App=NewObject<UACECharacterAppearanceComponent>(Pawn);
    Pawn->AddInstanceComponent(App); App->RegisterComponent(); App->bAlignMeshToCapsuleBottom=true;
    Pawn->SetActorScale3D(FVector(.7)); App->EnsureMeshRoot();
    TestTrue(TEXT("Scaled player visual foot does not apply actor scale twice"),
        App->MeshRoot->GetComponentLocation().Equals(Capsule->GetComponentLocation()-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()),.01));
    Pawn->SetActorScale3D(FVector(1));
    // Exercise the actual appearance/capsule update order. Re-equipping changes
    // Object.Scale and must neither compound capsule size nor move visual feet.
    const int32 PreviousGuid = Session->PlayerGuid;
    FACEWorldObject SelfObject; SelfObject.Guid = 12345; SelfObject.SetupId = 0x02000001;
    SelfObject.ItemType = 0x10; SelfObject.Name = TEXT("Scaled local player");
    Session->PlayerGuid = SelfObject.Guid;
    Pawn->SetActorLocation(Feet + FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()));
    for (float ObjectScale : {.7f, 1.1f, .9f, .9f, .7f})
    {
        SelfObject.Scale = ObjectScale; Session->WorldObjects.Add(SelfObject.Guid, SelfObject);
        Controller->ApplyPlayerCapsuleFromSetup(SelfObject.SetupId);
        TestTrue(TEXT("Local scaled appearance builds"), App->ApplyWorldObject(SelfObject,100,false));
        Controller->ApplyPlayerCapsuleFromSetup(SelfObject.SetupId);
        TestTrue(TEXT("Appearance updates keep local visual feet on the same surface"),
            App->MeshRoot->GetComponentLocation().Equals(Feet,.01));
        const float ExpectedHalf = FMath::Clamp(Controller->PlayerSetupHeightAc * ObjectScale,1.5f,2.2f)*50.f-1.f;
        TestTrue(TEXT("World capsule height applies object scale exactly once"),
            FMath::IsNearlyEqual(Capsule->GetScaledCapsuleHalfHeight(),ExpectedHalf,.01f));
    }
    // Exercise the actual standing movement/ground-snap path with the reported
    // outfit, rather than checking the mesh root alone.
    FACECharacterCreation Clothing; FString ClothingError;
    TestTrue(TEXT("Grounding clothing data loads"),Clothing.Load(*Dat->GetPortalDat(),ClothingError));
    SelfObject.Scale=1; SelfObject.bIsPlayer=true;
    Clothing.ApplyClothing(0x1000069E,SelfObject.SetupId,0,0,SelfObject.Appearance);
    Clothing.ApplyClothing(0x100007E2,SelfObject.SetupId,0,0,SelfObject.Appearance);
    Clothing.ApplyClothing(0x10000867,SelfObject.SetupId,0,0,SelfObject.Appearance);
    Session->WorldObjects.Add(SelfObject.Guid,SelfObject);
    App->ApplyWorldObject(SelfObject,100,false); Controller->ApplyPlayerCapsuleFromSetup(SelfObject.SetupId);
    Session->State=EACESessionState::InWorld;
    Controller->PlayerInput=NewObject<UPlayerInput>(Controller);
    Controller->bRetailCursorInstalled=true; // Headless movement fixture has no Slate viewport.
    Controller->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(Fixture.GI,UACEHoverTooltipWidget::StaticClass());
    Controller->bEnterWorldLoading=false; Controller->bWorldRevealActive=false;
    Controller->bHavePredictedPose=true; Controller->PredictedPose=Pose;
    Controller->PredictedPose.Location.Z+=.2f;
    Pawn->SetActorLocation(Feet+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()+20));
    for (int32 Frame=0;Frame<30;++Frame)
    {
        Controller->PlayerTick(.016f);
        App->TickComponent(.016f,LEVELTICK_All,nullptr);
    }
    FBox GroundedBounds(ForceInit); App->GetVisualWorldBounds(GroundedBounds);
    AddInfo(FString::Printf(TEXT("Grounded outfit floor=%.3f capsuleFeet=%.3f visualMin=%.3f"),Feet.Z,
        Pawn->GetActorLocation().Z-Capsule->GetScaledCapsuleHalfHeight(),GroundedBounds.Min.Z));
    TestTrue(TEXT("Stationary player settles from a small floor gap"),
        FMath::Abs(App->GetMeshRoot()->GetComponentLocation().Z-Feet.Z)<.5f);
    // Exercise actual key polling and predicted yaw, including forward movement
    {
        const FACEPosition SavedPose=Controller->PredictedPose;
        const FTransform SavedTransform=Pawn->GetActorTransform();
        FACEPosition FallPose; FallPose.CellId=0x7D630001; FallPose.Location=FVector(100,100,230);
        const FVector FallFeet=FallPose.ToUnrealLocation(100);
        auto* Landing=Fixture.World->SpawnActor<AActor>();
        auto* LandingFloor=NewObject<UBoxComponent>(Landing); Landing->SetRootComponent(LandingFloor);
        Landing->AddInstanceComponent(LandingFloor); LandingFloor->SetBoxExtent(FVector(1000,1000,10));
        LandingFloor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        LandingFloor->SetCollisionResponseToAllChannels(ECR_Block); LandingFloor->RegisterComponent();
        Landing->SetActorLocation(FallFeet-FVector(0,0,3010));
        Controller->LastServerPose=FallPose; Controller->bHaveLastServerPose=true; Controller->bHavePredictedPose=false;
        Controller->StepHoldSeconds=0; Session->SetLocalPosition(FallPose);
        Pawn->SetActorLocation(FallFeet+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()));
        Controller->PlayerTick(.016f);
        TestTrue(TEXT("Unsupported idle player enters falling without a Jump action"),Controller->bJumpAirborne);
        for (int32 Frame=0;Frame<40;++Frame) Controller->PlayerTick(.016f);
        TestTrue(TEXT("Fall continues without keyboard input"),Pawn->GetActorLocation().Z<FallFeet.Z-100);
        for (int32 Frame=0;Frame<160;++Frame) Controller->PlayerTick(.016f);
        TestFalse(TEXT("Stationary fall lands on the lower floor"),Controller->bJumpAirborne);
        TestFalse(TEXT("Landing expires the stale server pose above the cliff"),Controller->bHaveLastServerPose);
        TestTrue(TEXT("Client reaches the bottom of a thirty metre drop"),FMath::Abs(Pawn->GetActorLocation().Z-Capsule->GetScaledCapsuleHalfHeight()-(FallFeet.Z-3000))<3);
        FACEPosition Bottom=Controller->PredictedPose;
        Controller->bLocalPredicting=false; Controller->bHavePredictedPose=true;
        Controller->PredictedPose.Location.Z+=30;
        Controller->SoftReconcilePredictedTowardServer(Bottom,.016f,false);
        TestEqual(TEXT("Idle reconciliation accepts a vertical-only server correction"),Controller->PredictedPose.Location.Z,Bottom.Location.Z);
        Controller->bJumpAirborne=true; Controller->bLocalPredicting=true;
        Controller->bStandingJumpLocked=true; Controller->bHaveJumpLaunchFacing=true;
        ++Session->ForcePositionSeq;
        Controller->HandlePositionUpdate(Client->GetPlayerGuid(),Bottom);
        TestTrue(TEXT("Forced server position is applied while locally predicting"),Pawn->GetActorLocation().Equals(Bottom.ToUnrealLocation(100)+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()),.01));
        TestFalse(TEXT("Forced correction releases the old ballistic arc"),Controller->bJumpAirborne);
        TestFalse(TEXT("Forced landing also releases standing-jump movement lock"),Controller->bStandingJumpLocked);
        TestFalse(TEXT("Forced landing drops stale launch facing"),Controller->bHaveJumpLaunchFacing);
        Landing->Destroy();
        Controller->PredictedPose=SavedPose; Controller->bHavePredictedPose=true; Controller->bHaveLastServerPose=false;
        Controller->bLocalPredicting=false; Controller->StepHoldSeconds=0;
        Session->SetLocalPosition(SavedPose); Pawn->SetActorTransform(SavedTransform);
    }

    // Exercise actual key polling and predicted yaw, including forward movement
    // with an orbit offset (camera follow must not override an explicit turn).
    {
        TGuardValue<FString> InputPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/TurnKeysFixture.ini"));
        FConfigFile InputConfig; InputConfig.NoSave=true;
        GConfig->SetFile(GGameUserSettingsIni,&InputConfig); ACEInputBindings::Reload();
        const bool MouseTurningBefore=Client->IsCharacterOptionSet(0x31);
        const FACEPosition StartPose=Controller->PredictedPose;
        const FTransform StartTransform=Pawn->GetActorTransform();
        auto* TestBoom=NewObject<USpringArmComponent>(Pawn);
        TestBoom->SetupAttachment(Pawn->GetRootComponent());TestBoom->RegisterComponent();
        auto PressKey=[&](FKey Key)
        {
            Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f));
        };
        for (bool MouseTurning : {false,true}) for (bool Forward : {false,true})
        {
            Client->SendSetSingleCharacterOption(0x31,MouseTurning);
            for (FKey Key : {EKeys::A,EKeys::D,EKeys::Left,EKeys::Right})
            {
                Controller->PlayerInput->FlushPressedKeys();PressKey(Key);
                if (Forward) PressKey(EKeys::W);
                Controller->PlayerInput->ProcessInputStack({},.016f,false);
                Pawn->SetActorTransform(StartTransform);Controller->PredictedPose=StartPose;
                TestBoom->SetRelativeRotation(FRotator(-15,150,0));
                Controller->PlayerTick(.016f);
                const float ExpectedTurn=(Key==EKeys::A || Key==EKeys::Left)?-1.f:1.f;
                TestEqual(TEXT("A/D and arrows remain turn inputs in both mouse modes"),Controller->TurnAxis,ExpectedTurn);
                TestEqual(TEXT("Turn keys never become sidestep inputs"),Controller->RightAxis,0.f);
                const FQuat Expected=(FQuat(FVector::UpVector,FMath::DegreesToRadians(-180.f*ExpectedTurn*.016f))*StartPose.GetAcQuat()).GetNormalized();
                TestTrue(TEXT("Keyboard turning changes predicted facing even with camera-follow movement"),Controller->PredictedPose.GetAcQuat().Equals(Expected,.001));
            }
        }
        for (FKey Key : {EKeys::Q,EKeys::E})
        {
            Controller->PlayerInput->FlushPressedKeys();PressKey(Key);
            Controller->PlayerInput->ProcessInputStack({},.016f,false);
            Pawn->SetActorTransform(StartTransform);Controller->PredictedPose=StartPose;
            TestBoom->SetRelativeRotation(FRotator(-15,90,0));
            Controller->PlayerTick(.016f);
            TestEqual(TEXT("Q/E retain sidestep bindings"),Controller->RightAxis,Key==EKeys::Q?-1.f:1.f);
            TestEqual(TEXT("Sidestep does not supply keyboard turn input"),Controller->TurnAxis,0.f);
        }
        Controller->PlayerInput->FlushPressedKeys();
        Controller->PlayerInput->ProcessInputStack({},.016f,false);
        // The double-click interaction path must arrive and send Use with any orbit offset.
        // Use the actual controller, collision, network writer and isolated loopback socket.
        auto* ApproachSockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        auto* ApproachReceiver=ApproachSockets->CreateSocket(NAME_DGram,TEXT("Approach fixture receiver"),false);
        auto ApproachAddress=ApproachSockets->CreateInternetAddr(); bool AddressValid=false;
        ApproachAddress->SetIp(TEXT("127.0.0.1"),AddressValid);ApproachAddress->SetPort(0);
        if (!ApproachReceiver || !ApproachReceiver->Bind(*ApproachAddress)) return false;
        ApproachReceiver->GetAddress(*ApproachAddress);
        Session->SocketC2S=ApproachSockets->CreateSocket(NAME_DGram,TEXT("Approach fixture sender"),false);
        Session->ServerC2SAddr=ApproachAddress;Session->IssacClient=MakeUnique<FACEIsaac>(123u);
        auto CountAction=[&](uint32 Action)
        {
            int32 Count=0;
            for (const auto& Pair:Session->CachedC2SPackets)
            {
                FACEBinaryReader R(Pair.Value.Payload);R.Skip(16);
                if (R.ReadUInt32()!=ACEOpcode::GameAction) continue;
                R.ReadUInt32();if (R.ReadUInt32()==Action) ++Count;
            }
            return Count;
        };
        FACEWorldObject UseTarget;UseTarget.Guid=54321;UseTarget.bHasPosition=true;
        UseTarget.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;UseTarget.ItemUseable=32;
        UseTarget.Name=TEXT("Approach fixture lever");UseTarget.UseRadius=.6f;
        UseTarget.Position=StartPose;
        const FVector StartFeet=StartPose.ToUnrealLocation(100);
        auto ResetApproach=[&](FVector TargetOffset)
        {
            Controller->ClearServerMoveTo();Session->bUseBusy=false;
            Controller->PredictedPose=StartPose;Controller->bHavePredictedPose=true;
            Controller->bHaveLastServerPose=false;Controller->StepHoldSeconds=0;
            Session->SetLocalPosition(StartPose);Pawn->SetActorTransform(StartTransform);
            UseTarget.Position.SetLocationFromUnreal(StartFeet+TargetOffset,100);
            Session->WorldObjects.Add(UseTarget.Guid,UseTarget);Session->CachedC2SPackets.Reset();
        };
        for (bool MouseTurning : {false,true}) for (float Orbit : {-90.f,90.f,180.f})
        {
            Client->SendSetSingleCharacterOption(0x31,MouseTurning);
            ResetApproach(FVector(500,0,0));TestBoom->SetRelativeRotation(FRotator(-15,Orbit,0));
            Controller->InteractWithObject(UseTarget.Guid);
            TestTrue(TEXT("World item interaction starts an approach"),Controller->bServerMoveToActive);
            for (int32 Frame=0;Frame<240 && CountAction(ACEGameAction::Use)==0;++Frame)
                Controller->PlayerTick(.016f);
            TestEqual(TEXT("Turned-camera approach reaches item and sends Use once"),CountAction(ACEGameAction::Use),1);
            const FVector Arrived=Controller->PredictedPose.ToUnrealLocation(100);
            TestTrue(TEXT("Orbit offset cannot steer automatic approach away from target"),FMath::Abs(Arrived.Y-StartFeet.Y)<1.f);
            TestTrue(TEXT("Use is sent only after reaching the target cylinder"),
                Controller->GetUseCylinderDistanceCm(UseTarget,Arrived,UseTarget.Position.ToUnrealLocation(100))<=62.f);
        }
        // Stuck game boards are usable world objects; corpses can retain the Dead pose.
        for (bool bCorpse : {false, true})
        {
            UseTarget.ObjectDescriptionFlags = ACEObjectDescFlag::Stuck
                | (bCorpse ? ACEObjectDescFlag::Corpse : 0u);
            UseTarget.ItemType = bCorpse ? ACEItemType::Container : static_cast<int32>(0x80000000u);
            UseTarget.ItemUseable = 1; // the public special-object descriptor enables Use
            UseTarget.InitialMotionCommand = bCorpse ? ACEMotion::Dead : ACEMotion::Ready;
            ResetApproach(FVector(300,0,0));
            Client->SelectObject(UseTarget.Guid);
            Controller->InteractWithSelectedObject();
            TestTrue(TEXT("Chess boards and corpses start a use approach"), Controller->bServerMoveToActive);
            for (int32 Frame=0; Frame<240 && CountAction(ACEGameAction::Use)==0; ++Frame)
                Controller->PlayerTick(.016f);
            TestEqual(TEXT("Chess/corpse arrival sends exactly one Use"), CountAction(ACEGameAction::Use), 1);
        }
        UseTarget.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
        UseTarget.ItemType=0; UseTarget.ItemUseable=32; UseTarget.InitialMotionCommand=ACEMotion::Ready;
        // Decode actual MoveTo packets, retaining the start across echoes and all finite limits.
        Session->OnMotionUpdate.AddUObject(Controller,&AACEPlayerController::HandleMotionUpdate);
        Session->OnMoveToFailed.AddUObject(Controller,&AACEPlayerController::HandleMoveToFailed);
        uint16 MotionSequence=1;
        auto ReceiveApproach=[&](float Limit)
        {
            FACEBinaryWriter W;W.WriteUInt32(SelfObject.Guid);W.WriteUInt16(0);
            W.WriteUInt16(MotionSequence++);W.WriteUInt16(0);W.WriteUInt8(0);W.Align();
            W.WriteUInt8(6);W.WriteUInt8(0);W.WriteUInt16(0);W.WriteUInt32(UseTarget.Guid);
            W.WriteUInt32(UseTarget.Position.CellId);W.WriteFloat(UseTarget.Position.Location.X);
            W.WriteFloat(UseTarget.Position.Location.Y);W.WriteFloat(UseTarget.Position.Location.Z);
            W.WriteUInt32(0);W.WriteFloat(.6f);W.WriteFloat(0);W.WriteFloat(Limit);
            W.WriteFloat(1);W.WriteFloat(15);W.WriteFloat(0);W.WriteFloat(1);
            FACEBinaryReader R(W.GetData());Session->HandleUpdateMotion(R);
        };
        int32 ChargeErrors=0;
        const auto ChatHandle=Session->OnChatMessage.AddLambda([&](const FString& Message,const FString&,int32 Type)
        {
            if (Message==TEXT("You charged too far!"))
            {
                ++ChargeErrors;TestEqual(TEXT("Charge failure uses retail transient notification"),Type,ACEChatMessageType::TransientInfo);
            }
        });
        for (float Displacement : {1499.f,1500.f,1501.f})
        {
            ResetApproach(FVector(10000,0,0));ChargeErrors=0;
            Controller->BeginUseApproach(UseTarget.Guid,.6f,false);ReceiveApproach(15);
            const FACEPosition OriginalStart=Controller->ApproachStartPose;
            Controller->PredictedPose.SetLocationFromUnreal(StartFeet+FVector(0,0,Displacement),100);
            ReceiveApproach(15);
            TestTrue(TEXT("Server echo does not reset the charge starting position"),
                Controller->ApproachStartPose.ToUnrealLocation(100).Equals(OriginalStart.ToUnrealLocation(100),.001));
            Session->CachedC2SPackets.Reset();Controller->PlayerTick(.016f);
            TestEqual(TEXT("Charge distance includes vertical travel and only fails beyond limit"),Controller->bServerMoveToActive,Displacement<=1500);
            TestEqual(TEXT("Exceeding charge limit reports failure once"),ChargeErrors,Displacement>1500?1:0);
            TestEqual(TEXT("Failed or distant approach never uses the object"),CountAction(ACEGameAction::Use),0);
            if (Displacement>1500)
            {
                TestTrue(TEXT("Failed approach sends stop to the server"),CountAction(ACEGameAction::MoveToState)>0);
                TestFalse(TEXT("Failure releases forced position reporting"),Session->bForcePositionReporting);
                Controller->PlayerTick(.016f);TestEqual(TEXT("Later ticks cannot repeat the failure"),ChargeErrors,1);
            }
        }
        ResetApproach(FVector(2000,0,0));ReceiveApproach(15);ChargeErrors=0;
        Controller->PredictedPose=UseTarget.Position;Controller->PlayerTick(.016f);
        TestEqual(TEXT("Arrival wins over charge distance at target"),ChargeErrors,0);
        ResetApproach(FVector(10000,0,0));ReceiveApproach(250);
        TestEqual(TEXT("Finite server limits above 100 are preserved"),Controller->ServerMoveToFailDistance,250.f);
        ReceiveApproach(MAX_flt);
        TestEqual(TEXT("Unlimited server echo clears previous finite limit"),Controller->ServerMoveToFailDistance,0.f);
        ChargeErrors=0;FACEBinaryWriter Error;Error.WriteUInt32(0x003D);
        FACEBinaryReader ErrorReader(Error.GetData());Session->HandleWeenieError(ErrorReader);
        TestFalse(TEXT("Server charge error cancels an active local approach"),Controller->bServerMoveToActive);
        TestEqual(TEXT("Server charge error retains its retail message"),ChargeErrors,1);
        Session->OnChatMessage.Remove(ChatHandle);Session->OnMotionUpdate.RemoveAll(Controller);
        Session->OnMoveToFailed.RemoveAll(Controller);Session->WorldObjects.Remove(UseTarget.Guid);
        Session->bUseBusy=false;Controller->ClearServerMoveTo();
        Session->SocketC2S->Close();ApproachSockets->DestroySocket(Session->SocketC2S);Session->SocketC2S=nullptr;
        Session->ServerC2SAddr.Reset();Session->CachedC2SPackets.Reset();
        ApproachReceiver->Close();ApproachSockets->DestroySocket(ApproachReceiver);
        TestBoom->DestroyComponent();
        Client->SendSetSingleCharacterOption(0x31,MouseTurningBefore);
        Pawn->SetActorTransform(StartTransform);Controller->PredictedPose=StartPose;
        Controller->PlayerTick(.016f);
    }
    ACEInputBindings::Reload();
    for (uint32 Stance : {ACEMotion::StanceNonCombat,ACEMotion::StanceMagic})
    {
        App->SetPreferredStyle(Stance);
        for (int32 Frame=0;Frame<30;++Frame) App->TickComponent(.016f,LEVELTICK_All,nullptr);
        float LowestVertex=MAX_flt;
        for (int32 Leg : {0,1,2,5,6}) if (auto* Part=Cast<UProceduralMeshComponent>(App->GetPartMesh(Leg)))
            for (int32 Section=0;Section<Part->GetNumSections();++Section)
                for (const auto& Vertex:Part->GetProcMeshSection(Section)->ProcVertexBuffer)
                    LowestVertex=FMath::Min(LowestVertex,float(Part->GetComponentTransform().TransformPosition(Vertex.Position).Z));
        AddInfo(FString::Printf(TEXT("Grounded robe stance=%08X lowestVertex=%.3f floor=%.3f"),Stance,LowestVertex,Feet.Z));
    }
    // Walk both across and along a narrow seat using the actual controller tick.
    // A center ray can already see the floor while the foot still overlaps the
    // seat. The body must descend once after clearing it, not alternate floors.
    auto* BenchSeat=Fixture.World->SpawnActor<AActor>();
    auto* SeatMesh=NewObject<UInstancedStaticMeshComponent>(BenchSeat);
    BenchSeat->SetRootComponent(SeatMesh);BenchSeat->AddInstanceComponent(SeatMesh);
    Dat->GetOrBuildSetupMesh(0x02000120,100,UACEDatSubsystem::ACEPlacementResting);
    auto* SeatAsset=Dat->GetOrCreateSetupStaticMesh(0x02000120,100,true,UACEDatSubsystem::ACEPlacementResting);
    if (!TestNotNull(TEXT("Actual town hall bench physics loads"),SeatAsset)) return false;
    SeatMesh->SetStaticMesh(SeatAsset);SeatMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    SeatMesh->SetCollisionResponseToAllChannels(ECR_Block);SeatMesh->RegisterComponent();
    const FVector SeatCenter=SeatAsset->GetBoundingBox().GetCenter();
    SeatMesh->AddInstance(FTransform(Feet-FVector(SeatCenter.X,SeatCenter.Y,0)),true);
    for (bool Indoor : {false,true})
    {
        // Isolate the same cooked bench in each movement mode; the room/stair
        // transit fixtures separately cover the surrounding town hall geometry.
        FACEPosition BenchPose=Pose;
        if (Indoor) BenchPose.CellId=0x0134010E;
        const FVector BenchFeet=BenchPose.ToUnrealLocation(100);
        Floor->SetActorLocation(BenchFeet-FVector(0,0,10));
        SeatMesh->UpdateInstanceTransform(0,FTransform(BenchFeet-FVector(SeatCenter.X,SeatCenter.Y,0)),true,true,true);
        for (bool Along : {false,true}) for (int32 Direction : {-1,1})
        {
            const FVector StartOffset=Along?FVector(0,-240*Direction,0):FVector(-110*Direction,0,0);
            FACEPosition WalkPose=BenchPose; WalkPose.SetLocationFromUnreal(BenchFeet+StartOffset,100);
            WalkPose.SetAceFacingFromUnrealDir2D(Along?FVector(0,Direction,0):FVector(Direction,0,0));
            Session->SetLocalPosition(WalkPose); Controller->PredictedPose=WalkPose;
            Controller->bHavePredictedPose=true; Controller->bLocalPredicting=true;
            Controller->StepHoldSeconds=0; Controller->bHaveLastServerPose=false;
            Pawn->SetActorLocation(BenchFeet+StartOffset+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()));
            Controller->bAutoRun=true;
            float PreviousZ=BenchFeet.Z, MaxZ=BenchFeet.Z;
            int32 Reversals=0, LastVertical=0, SeatPenetrations=0;
            for (int32 Frame=0;Frame<45;++Frame)
            {
                Controller->PlayerTick(.016f);
                const float Z=Pawn->GetActorLocation().Z-Capsule->GetScaledCapsuleHalfHeight();
                const int32 Vertical=Z>PreviousZ+2?1:(Z<PreviousZ-2?-1:0);
                if (Vertical && LastVertical && Vertical!=LastVertical) ++Reversals;
                if (Vertical) LastVertical=Vertical;
                MaxZ=FMath::Max(MaxZ,Z); PreviousZ=Z;
                FHitResult Contact; FCollisionQueryParams Q(NAME_None,true,Pawn); Q.AddIgnoredActor(Floor);
                const FVector Probe=Pawn->GetActorLocation()+FVector(0,0,.5f);
                Fixture.World->SweepSingleByChannel(Contact,Probe,Probe+FVector(0,0,.1f),FQuat::Identity,ECC_Pawn,
                    FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Q);
                if (Contact.Component==SeatMesh && Contact.bStartPenetrating && Contact.PenetrationDepth>.1f) ++SeatPenetrations;
                TestEqual(TEXT("Bench tick retains the tested indoor/outdoor movement mode"),
                    (static_cast<uint32>(Controller->PredictedPose.CellId)&0xFFFFu)>=0x100u,Indoor);
            }
            const FVector Travel=Pawn->GetActorLocation()-(BenchFeet+StartOffset);
            AddInfo(FString::Printf(TEXT("Bench walk indoor=%d along=%d direction=%d travel=%s maxRise=%.2f reversals=%d penetrations=%d finalFeet=%.2f"),
                Indoor,Along,Direction,*Travel.ToString(),MaxZ-BenchFeet.Z,Reversals,SeatPenetrations,PreviousZ-BenchFeet.Z));
            TestTrue(TEXT("Walking crosses the bench without getting stuck"),(Along?Travel.Y:Travel.X)*Direction>(Along?480.f:220.f));
            TestTrue(TEXT("Walking climbs the seat"),MaxZ>BenchFeet.Z+40);
            TestTrue(TEXT("Bench traversal rises then descends without oscillation"),Reversals<=1);
            TestEqual(TEXT("Walking never penetrates the bench seat"),SeatPenetrations,0);
            TestTrue(TEXT("Walking returns to the floor after clearing the bench"),FMath::Abs(PreviousZ-BenchFeet.Z)<.5f);
            Controller->bAutoRun=false; Controller->ForwardAxis=0;
        }
    }
    Floor->SetActorLocation(Feet-FVector(0,0,10));
    BenchSeat->Destroy();
    // Use the reported Yaraq login coordinates against actual cooked terrain.
    FACEPosition YaraqPose; YaraqPose.CellId=0x7D64000D; YaraqPose.Location=FVector(35.68,102.89,12.01);
    const FVector YaraqFeet=YaraqPose.ToUnrealLocation(100);
    Dat->GetOrBuildLandblockMesh(0x7D640000,100);
    auto* Yaraq=Fixture.World->SpawnActor<AACELandblockActor>();
    TestTrue(TEXT("Reported Yaraq terrain loads"),Yaraq->LoadLandblock(0x7D640000,100,true,1,0));
    Session->SetLocalPosition(YaraqPose); Controller->PredictedPose=YaraqPose;
    Pawn->SetActorLocation(YaraqFeet+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()));
    for (int32 Frame=0;Frame<30;++Frame)
    {
        Controller->PlayerTick(.016f);
        App->TickComponent(.016f,LEVELTICK_All,nullptr);
    }
    float YaraqGround=0;
    TestTrue(TEXT("Reported Yaraq floor can be sampled"),Dat->SampleOutdoorGroundZ(YaraqFeet.X,YaraqFeet.Y,100,YaraqGround));
    AddInfo(FString::Printf(TEXT("Yaraq grounding DAT=%.3f capsuleFeet=%.3f meshOrigin=%.3f"),YaraqGround,
        Pawn->GetActorLocation().Z-Capsule->GetScaledCapsuleHalfHeight(),App->GetMeshRoot()->GetComponentLocation().Z));
    TestTrue(TEXT("Player origin meets the actual Yaraq terrain"),FMath::Abs(App->GetMeshRoot()->GetComponentLocation().Z-YaraqGround)<.5f);
    // Collision coordinates alone cannot expose floating caused by pixel depth
    // offset: town terrain used to write a surface 48 cm behind the real floor.
    Yaraq->SetLandEnvCellFloorPriority(true,Dat,true);
    auto* DepthOwner=Fixture.World->SpawnActor<AActor>();
    auto* DepthCapture=NewObject<USceneCaptureComponent2D>(DepthOwner);
    DepthOwner->SetRootComponent(DepthCapture); DepthCapture->RegisterComponent();
    auto* DepthTarget=NewObject<UTextureRenderTarget2D>(DepthCapture);
    DepthTarget->RenderTargetFormat=RTF_R32f; DepthTarget->InitAutoFormat(32,32);
    DepthCapture->TextureTarget=DepthTarget; DepthCapture->CaptureSource=SCS_SceneDepth;
    DepthCapture->bCaptureEveryFrame=false; DepthCapture->bCaptureOnMovement=false; DepthCapture->FOVAngle=10;
    DepthCapture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    DepthCapture->ShowOnlyComponent(Yaraq->TerrainMesh);
    DepthCapture->SetWorldLocationAndRotation(FVector(YaraqFeet.X,YaraqFeet.Y,YaraqGround+500),FRotator(-90,0,0));
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    Fixture.World->SendAllEndOfFrameUpdates(); DepthCapture->CaptureScene(); FlushRenderingCommands();
    TArray<FLinearColor> DepthPixels;
    DepthTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(DepthPixels);
    if (TestTrue(TEXT("Terrain depth capture returns pixels"),DepthPixels.Num()==32*32))
    {
        const float Depth=DepthPixels[16*32+16].R;
        AddInfo(FString::Printf(TEXT("Yaraq visible terrain depth %.3f cm; actual floor is 500 cm from camera"),Depth));
        TestTrue(TEXT("Town rendered depth remains within 5 mm of collision ground"),FMath::Abs(Depth-500.f)<.5f);
    }
    DepthOwner->Destroy();
    Yaraq->Destroy(); Session->SetLocalPosition(Pose); Controller->PredictedPose=Pose;
    Session->State=EACESessionState::Disconnected;
    Session->WorldObjects.Remove(SelfObject.Guid); Session->PlayerGuid = PreviousGuid;
    Pawn->SetActorScale3D(FVector(1)); Capsule->SetCapsuleSize(22,88);
    auto* SmallObstacle = MakeBox(Feet + FVector(0,0,100), FVector(30,30,100));
    TestTrue(TEXT("Small obstacle gets a nearby supported placement"), Controller->FindWorldEntryPlacement(Placement));
    TestTrue(TEXT("Placement actually leaves the obstructed point"), FVector::Dist2D(Placement, Feet) > 30);
    SmallObstacle->Destroy();
    auto* Solid = MakeBox(Feet + FVector(0,0,200), FVector(600,600,200));
    TestFalse(TEXT("No valid placement within retail radius stays hidden"), Controller->FindWorldEntryPlacement(Placement));

    // Only loopback traffic. Assert the recovery uses the real retail GameAction,
    // sends once, and cannot reuse the original destination's LoginComplete.
    auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* Receiver = Sockets->CreateSocket(NAME_DGram, TEXT("World entry recovery fixture"), false);
    if (!TestNotNull(TEXT("Loopback receiver"), Receiver)) return false;
    auto Address = Sockets->CreateInternetAddr(); bool Valid = false;
    Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(0);
    if (!Receiver->Bind(*Address)) { Sockets->DestroySocket(Receiver); return false; }
    Receiver->GetAddress(*Address); Receiver->SetNonBlocking(true);
    Session->SocketC2S = Sockets->CreateSocket(NAME_DGram, TEXT("World entry sender"), false);
    Session->ServerC2SAddr = Address; Session->State = EACESessionState::InWorld;
    Session->IssacClient = MakeUnique<FACEIsaac>(123u);
    Controller->EnterWorldLoadElapsed = 44;
    TestFalse(TEXT("Slow loading within grace does not recall"), Controller->TickWorldEntryRecovery(.016f, TEXT("cell-collision")));
    Controller->EnterWorldLoadElapsed = 46;
    TestFalse(TEXT("Visual scenery wait does not trigger recall"), Controller->TickWorldEntryRecovery(.016f, TEXT("near-scenery")));
    TestTrue(TEXT("Unsafe destination requests recovery"), Controller->TickWorldEntryRecovery(.016f, TEXT("spawn-placement")));
    for (int32 I = 0; I < 120; ++I) Controller->TickWorldEntryRecovery(1.f / 60.f, TEXT("cell-collision"));
    int32 Recalls = 0;
    const double Until = FPlatformTime::Seconds() + .1;
    while (FPlatformTime::Seconds() < Until)
    {
        uint8 Bytes[2048]; int32 Count = 0;
        if (!Receiver->Recv(Bytes, sizeof(Bytes), Count)) { FPlatformProcess::Sleep(.001f); continue; }
        FACEBinaryReader R(Bytes, Count); R.Skip(20 + 14);
        TestEqual(TEXT("Recall retains weenie queue"), R.ReadUInt16(), ACEQueue::WeenieQueue);
        TestEqual(TEXT("Recall is a GameAction"), R.ReadUInt32(), 0xF7B1u);
        R.ReadUInt32();
        TestEqual(TEXT("Server selects the attuned lifestone (TeleToLifestone)"), R.ReadUInt32(), 0x63u);
        ++Recalls;
    }
    TestEqual(TEXT("Recovery sends exactly one recall across repeated frames"), Recalls, 1);
    FString Gate;
    TestFalse(TEXT("Original destination cannot reveal while recall is pending"), Controller->IsWorldTransitionReady(&Gate, true));
    TestEqual(TEXT("Recall has its own readiness gate"), Gate, FString(TEXT("lifestone-recall")));
    Controller->bPortalExitNotified = true;
    Controller->ResetWorldEntryDestination();
    TestFalse(TEXT("New server destination needs a fresh LoginComplete"), Controller->bPortalExitNotified);
    TestFalse(TEXT("Destination receipt finishes recall wait"), Controller->bAwaitingRecoveryDestination);
    TestEqual(TEXT("New destination has a fresh loading grace"), Controller->EnterWorldLoadElapsed, 0.f);
    TestTrue(TEXT("Destination reset preserves one-attempt limit"), Controller->bWorldEntryRecoveryAttempted);
    TestFalse(TEXT("Unrevealed destination does not start autonomous position reporting"), Session->bForcePositionReporting);
    Controller->EnterWorldLoadElapsed = 46;
    Controller->TickWorldEntryRecovery(.016f, TEXT("cell-collision"));
    TestEqual(TEXT("Unusable lifestone disconnects instead of repeating recall"), Session->State, EACESessionState::Disconnected);
    TestFalse(TEXT("Failed recovery leaves the endless loading state"), Controller->bEnterWorldLoading);
    TestFalse(TEXT("Failure has a user-readable explanation"), Controller->WorldEntryFailureReason.IsEmpty());
    Session->State = EACESessionState::InWorld;
    Controller->bAwaitingRecoveryDestination = true;
    Controller->WorldEntryRecoveryElapsed = 44.99f;
    Controller->TickWorldEntryRecovery(.02f, TEXT("lifestone-recall"));
    TestEqual(TEXT("Rejected or unanswered recall cannot wait forever"), Session->State, EACESessionState::Disconnected);
    TestTrue(TEXT("Unanswered recall explains attunement and server eligibility"), Controller->WorldEntryFailureReason.Contains(TEXT("attuned")));
    Receiver->Close(); Sockets->DestroySocket(Receiver);
    Floor->Destroy(); Solid->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailPortalSpaceTest, "ACE.RetailParity.PortalSpace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailPortalSpaceTest::RunTest(const FString& Parameters)
{
    using ACERetailPortalAnimation::GetAnimLevel;
    for (auto Pair : {TPair<float,int32>(-.1f,0), {.1f,22}, {.25f,146}, {.5f,512}, {.75f,877}, {.9f,1001}, {1.1f,1024}})
        TestEqual(TEXT("Retail UIGlobals fixed-point curve sample"), GetAnimLevel(Pair.Key), Pair.Value);
    FEntryWorld Fixture;
    auto* Dat = Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* Tunnel = Fixture.World->SpawnActor<AACELoadingScreenActor>();
    Tunnel->BeginTunnel();
    const double Deadline = FPlatformTime::Seconds() + 20;
    while (!Tunnel->TryBuildMesh() && FPlatformTime::Seconds() < Deadline)
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::Sleep(.002f);
    }
    if (!TestTrue(TEXT("Retail portal setup builds"), Tunnel->bMeshReady)) return false;
    TestEqual(TEXT("Portal uses the DAT animation DID"), Tunnel->AnimationId, 0x030005AC);
    TestEqual(TEXT("Portal setup contains two animated parts"), Tunnel->Appearance->PartMeshes.Num(), 2);
    TestTrue(TEXT("Camera matches retail AC eye"), Tunnel->CameraBoom->GetRelativeLocation().Equals(FVector(-24,-270,88), .001));
    Tunnel->TeleportYaw = 90; Tunnel->ApplyPortalCamera();
    TestTrue(TEXT("Retail positive roll sends up toward converted AC +X"), Tunnel->Camera->GetUpVector().Equals(FVector(-1,0,0), .001));
    const FVector TestPoint(80, 120, 150);
    const FVector RetailViewPoint = Tunnel->Camera->GetComponentTransform().InverseTransformPosition(
        Tunnel->GetActorTransform().TransformPosition(TestPoint));
    Tunnel->SetTrackedView(true);
    const FTransform UnrolledView(FRotationMatrix::MakeFromXZ(FVector(0, 1, 0), FVector::UpVector).ToQuat(),
        Tunnel->Camera->GetComponentLocation());
    const FVector TrackedViewPoint = UnrolledView.InverseTransformPosition(
        Tunnel->Appearance->GetMeshRoot()->GetComponentTransform().TransformPosition(TestPoint));
    TestTrue(TEXT("Tracked portal rotates geometry to reproduce the retail view without rolling the HMD"),
        TrackedViewPoint.Equals(RetailViewPoint, .01));
    Tunnel->SetTrackedView(false);
    TestTrue(TEXT("Desktop portal restores the authored mesh frame"),
        Tunnel->Appearance->GetMeshRoot()->GetRelativeTransform().Equals(FTransform::Identity, .001));
    Tunnel->TeleportYawStart = 0; Tunnel->TeleportYawEnd = 300;
    Tunnel->TeleportYawElapsed = 0; Tunnel->TeleportYawDuration = 1;
    Tunnel->Tick(.25f);
    TestEqual(TEXT("Camera uses eased numeric interpolation, not shortest-angle interpolation"), Tunnel->TeleportYaw, 300.f * 146.f / 1024.f);
    for (int32 Visit = 0; Visit < 2; ++Visit)
    {
        Tunnel->BeginTunnel(); TestTrue(TEXT("Tunnel can be reused"), Tunnel->TryBuildMesh());
        TestFalse(TEXT("Tunnel actor is the sole animation clock"), Tunnel->Appearance->IsComponentTickEnabled());
        int32 Changed = 0;
        FTransform Previous = Tunnel->Appearance->PartMeshes[0]->GetRelativeTransform();
        FTransform PreviousSecond = Tunnel->Appearance->PartMeshes[1]->GetRelativeTransform();
        for (int32 Frame = 0; Frame < 120; ++Frame)
        {
            Tunnel->Tick(1.f / 120.f);
            const auto Current = Tunnel->Appearance->PartMeshes[0]->GetRelativeTransform();
            const auto Second = Tunnel->Appearance->PartMeshes[1]->GetRelativeTransform();
            if (!Current.Equals(Previous, .0001) || !Second.Equals(PreviousSecond, .0001)) ++Changed;
            Previous = Current;
            PreviousSecond = Second;
        }
        AddInfo(FString::Printf(TEXT("Tunnel visit %d: %d distinct displayed poses over 120 frames"), Visit, Changed));
        TestTrue(TEXT("Forty-fps source keys interpolate on display frames"), Changed > 100);
        TestTrue(TEXT("One second of display time advances animation once"), FMath::IsNearlyEqual(Tunnel->Appearance->AnimTime, 1.f, .001f));
        TestFalse(TEXT("Portal exit stretches the tunnel before showing the world"), Tunnel->TickReveal(.016f, FVector::ZeroVector));
        const float InitialFov = Tunnel->Camera->FieldOfView;
        TestFalse(TEXT("Halfway through tunnel stretch remains in portal view"), Tunnel->TickReveal(.484f, FVector::ZeroVector));
        TestEqual(TEXT("Exit changes the world projection, never the portal camera"), Tunnel->Camera->FieldOfView, InitialFov);
        TestTrue(TEXT("One-second tunnel stretch completes"), Tunnel->TickReveal(.5f, FVector::ZeroVector));
        TestTrue(TEXT("World stretch starts wide"), ACERetailPortalAnimation::StretchFov(90.f, 4.f/3.f, 0.f, true) > 170.f);
        TestTrue(TEXT("World stretch restores normal projection"), FMath::IsNearlyEqual(ACERetailPortalAnimation::StretchFov(90.f, 4.f/3.f, 1.f, true), 90.f, .001f));
    }
    Tunnel->BeginTunnel(); Tunnel->TryBuildMesh();
    Tunnel->TeleportYawStart = 0; Tunnel->TeleportYawEnd = 180;
    Tunnel->TeleportYawElapsed = 0; Tunnel->TeleportYawDuration = 1.2f;
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    auto* Target = NewObject<UTextureRenderTarget2D>(Tunnel);
    Target->ClearColor = FLinearColor::Black; Target->InitCustomFormat(640,480,PF_B8G8R8A8,false);
    auto* Capture = NewObject<USceneCaptureComponent2D>(Tunnel);
    Capture->TextureTarget = Target; Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->FOVAngle = Tunnel->Camera->FieldOfView;
    Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false;
    Capture->PostProcessSettings = Tunnel->Camera->PostProcessSettings;
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetAtmosphere(false);
    Capture->RegisterComponent();
    for (int32 Frame = 0; Frame < 3; ++Frame)
    {
        Tunnel->Tick(Frame == 0 ? 0.f : .3f);
        Capture->SetWorldTransform(Tunnel->Camera->GetComponentTransform());
        Fixture.World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        int32 Visible = 0;
        for (const auto& Pixel : Pixels) if (FMath::Max3(Pixel.R,Pixel.G,Pixel.B) > 20) ++Visible;
        TestTrue(TEXT("Retail camera sees textured tunnel geometry"), Visible > Pixels.Num() / 10);
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(640,480,Pixels,PNG);
        const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/RetailParity") / FString::Printf(TEXT("PortalSpace%d.png"),Frame);
        TestTrue(TEXT("Portal frame capture saved"), FFileHelper::SaveArrayToFile(PNG, *Path));
    }
    return true;
}
#endif
