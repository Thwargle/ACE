#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "ACEParticleBatchComponent.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACEScriptComponent.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACERegionSceneryActor.h"
#include "ACELandblockActor.h"
#include "ACETerrainChunkActor.h"
#include "ACETerrainPresenterComponent.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEAmbientReach.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACEDatCursor.h"
#include "Dat/ACEDatTextureResolver.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"

namespace
{
    struct FInteriorTestWorld
    {
        UWorld* World;
        UGameInstance* GI;
        FInteriorTestWorld()
        {
            const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
                .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            GI = NewObject<UGameInstance>(GEngine);
            World->SetGameInstance(GI); GI->Init();
        }
        ~FInteriorTestWorld()
        {
            GI->Shutdown();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAmbientEntranceTest, "ACE.Audio.OutdoorEntrances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEAmbientEntranceTest::RunTest(const FString& Parameters)
{
    // A bent corridor. Euclidean proximity through a wall must not bypass the
    // path through connected rooms, and a nearby but disconnected exit is silent.
    TMap<uint32,FACEBuiltEnvCellMesh> Rooms;
    auto AddPortal=[&](uint32 Cell,uint16 Other,uint16 OtherPortal,FVector Center)
    {
        auto& Room=Rooms.FindOrAdd(Cell);
        FACEDatCellPortal Portal;Portal.OtherCellId=Other;Portal.OtherPortalId=OtherPortal;
        Room.CellPortals.Add(Portal);
        Room.PortalApertureLocalVerts.Add({Center+FVector(-100,0,-100),Center+FVector(100,0,-100),
            Center+FVector(100,0,100),Center+FVector(-100,0,100)});
    };
    AddPortal(0x100,0x101,0,FVector(0,2000,0));
    AddPortal(0x101,0x100,0,FVector(0,2000,0));
    AddPortal(0x101,0xFFFF,0,FVector(3000,2000,0));
    AddPortal(0x102,0xFFFF,0,FVector(0,0,0));
    Rooms.Add(0x103,{});
    auto Find=[&](uint32 Cell) { return Rooms.Find(Cell); };
    const auto AroundCorner=ACEAmbientReach::FindOutdoor(0x100,FVector::ZeroVector,100,Find);
    TestTrue(TEXT("Sound reaches an entrance through neighboring cells"),AroundCorner.OutdoorCell!=0);
    TestTrue(TEXT("Attenuation follows the connected corridor, not the nearest wall"),AroundCorner.DistanceAc>48.f);
    TestTrue(TEXT("Outdoor sound fades deeper inside"),AroundCorner.Gain>0 && AroundCorner.Gain<.18f);
    const auto Near=ACEAmbientReach::FindOutdoor(0x101,FVector(3000,1950,0),100,Find);
    TestEqual(TEXT("No hard cutoff just inside the doorway"),Near.Gain,1.f);
    TestEqual(TEXT("Outdoor terrain has full ambience"),ACEAmbientReach::FindOutdoor(1,FVector::ZeroVector,100,Find).Gain,1.f);
    TestEqual(TEXT("Sealed rooms do not hear a disconnected entrance"),ACEAmbientReach::FindOutdoor(0x103,FVector::ZeroVector,100,Find).Gain,0.f);
    TestEqual(TEXT("Unknown cells do not create ambient sound"),ACEAmbientReach::FindOutdoor(0,FVector::ZeroVector,100,Find).Gain,0.f);
    TestEqual(TEXT("Missing streamed cells fail without loading geometry"),ACEAmbientReach::FindOutdoor(0x104,FVector::ZeroVector,100,Find).Gain,0.f);
    TestEqual(TEXT("Deep interiors stop scheduling outdoor sound"),ACEAmbientReach::FindOutdoor(0x100,FVector(0,-15000,0),100,Find).Gain,0.f);
    TestTrue(TEXT("Retail inverse-square weighting at 40m"),FMath::IsNearlyEqual(ACEAmbientReach::DistanceGain(40),.25f));

    FInteriorTestWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT loads"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    int32 Exits=0;
    for(uint32 Key:{0x7D640000u,0xC88C0000u,0x09050000u})
    {
        FACEDatLandblockInfo Info;
        if (!Dat->LoadLandblockInfo(Key,Info)) continue;
        for(uint32 I=0;I<Info.NumCells;++I) Dat->GetOrBuildEnvCellMesh(Key|(0x100+I),100);
        for(uint32 I=0;I<Info.NumCells;++I)
        {
            const uint32 Id=Key|(0x100+I);const auto* Mesh=Dat->FindEnvCellMesh(Id,100);
            if(!Mesh)continue;
            const FVector Origin=FACEPosition::AceVectorToUnreal(FVector((Id>>24)*192.f,((Id>>16)&255)*192.f,0),100);
            const auto Transform=Mesh->GetCellLocalToLandblock(100)*FTransform(Origin);
            for(int32 P=0;P<Mesh->CellPortals.Num();++P)
            {
                if(!Mesh->CellPortals[P].IsOutsidePortal() || !Mesh->PortalApertureLocalVerts.IsValidIndex(P)
                    || Mesh->PortalApertureLocalVerts[P].Num()<3)continue;
                FVector Center=FVector::ZeroVector;
                for(auto V:Mesh->PortalApertureLocalVerts[P])Center+=V;
                Center/=Mesh->PortalApertureLocalVerts[P].Num();
                const auto Result=ACEAmbientReach::FindOutdoor(Id,Transform.TransformPosition(Center),100,
                    [&](uint32 Cell){return Dat->FindEnvCellMesh(Cell,100);});
                TestTrue(TEXT("Actual retail exterior portal remains audible at its threshold"),Result.OutdoorCell!=0 && Result.Gain==1);
                TestTrue(TEXT("Entrance resolves a valid one-based landcell"),(Result.OutdoorCell&0xFFFF)>=1 && (Result.OutdoorCell&0xFFFF)<=64);
                ++Exits;
            }
        }
    }
    TestTrue(TEXT("Fixture tests real building entrances"),Exits>0);
    AddInfo(FString::Printf(TEXT("Validated %d real exterior portals"),Exits));
    const FACEDatAmbientSTBDesc* Stb=nullptr;
    TestFalse(TEXT("Ambient rejects landcell zero"),Dat->TryResolveAmbientSTBForCell(0x7D640000,Stb));
    TestFalse(TEXT("Ambient rejects landcells above 64"),Dat->TryResolveAmbientSTBForCell(0x7D640041,Stb));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailInteriorStreamingTest, "ACE.RetailParity.InteriorStreaming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailInteriorStreamingTest::RunTest(const FString& Parameters)
{
    FInteriorTestWorld Fixture;
    auto* Dat = Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    Dat->GetOrCreateTexturedMaterial(0x08000708);
    // World Composition supplies a baked landscape. Repeated outdoor visibility
    // refreshes must keep the procedural support actor's terrain suppressed.
    {
        auto* Owner = Fixture.World->SpawnActor<AActor>();
        auto* Presenter = NewObject<UACETerrainPresenterComponent>(Owner);
        auto* Land = Fixture.World->SpawnActor<AACELandblockActor>();
        Presenter->bWcBakedTerrainMode = true;
        Presenter->bHasKnownCell = true; Presenter->LastKnownCellId = 0x7D640001;
        Presenter->Spawned.Add(0x7D640000, Land);
        Land->SetOutdoorTerrainHiddenInGame(true);
        for (int32 I = 0; I < 3; ++I) Presenter->UpdateCameraVisibility();
        TestTrue(TEXT("WC visibility refresh never draws the procedural terrain over the baked landscape"), Land->TerrainMesh->bHiddenInGame);
        Presenter->bWcBakedTerrainMode = false;
        Presenter->UpdateCameraVisibility();
        TestFalse(TEXT("Normal procedural terrain remains visible outdoors"), Land->TerrainMesh->bHiddenInGame);
        Presenter->Spawned.Reset(); Land->Destroy(); Owner->Destroy();
    }
    int32 SolidPools = 0, DecorationPools = 0, Instances = 0;
    // Actual rooms traversed immediately before UECC-D57CB129 on build .3.
    for (uint32 CellId : {0x7D63010Du, 0x7D63010Eu, 0x7D630112u})
    {
        const auto* Cell = Dat->GetOrBuildEnvCellMesh(CellId, 100);
        if (!TestNotNull(TEXT("Crash room DAT"), Cell)) continue;
        const TArray<FACEDatStab> Stabs = Cell->StaticObjects;
        auto* Room = Fixture.World->SpawnActor<AACEEnvCellActor>();
        TestTrue(TEXT("Crash room actor loads"), Room->LoadEnvCell(CellId, FVector::ZeroVector, 100));
        for (const auto& Stab : Stabs)
            Dat->GetOrBuildSetupMesh(Stab.Id, 100, UACEDatSubsystem::ACEPlacementResting);

        // Load with collision already on, then leave/re-enter with existing instances,
        // and keep loading more furniture. This used to index past an empty body array.
        for (int32 Pass = 0; Pass < 4; ++Pass)
        {
            Room->SetEnvCellCollisionActive(false);
            Room->SetEnvCellCollisionActive(true);
            for (const auto& Stab : Stabs)
                TestTrue(TEXT("Scenery can finish after entry"), Room->TrySpawnOneStaticObject(Dat, Stab));
            for (const auto& Pair : Room->StaticObjectHisms)
            {
                auto* Mesh = Pair.Value.Get();
                if (Mesh->GetBodySetup())
                {
                    TestEqual(TEXT("Solid furniture retains collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
                    TestEqual(TEXT("Each solid instance owns a physics body slot"), Mesh->GetInstanceBodies().Num(), Mesh->GetInstanceCount());
                    if (Pass == 3) ++SolidPools;
                }
                else
                {
                    TestEqual(TEXT("DAT decoration without PhysicsBSP has no invented collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
                    TestFalse(TEXT("Empty collision never creates a partial physics state"), Mesh->IsPhysicsStateCreated());
                    if (Pass == 3) ++DecorationPools;
                }
                if (Pass == 3) Instances += Mesh->GetInstanceCount();
            }
        }
        Room->Destroy();
    }
    AddInfo(FString::Printf(TEXT("Crash fixtures: %d solid pools, %d non-colliding decoration pools, %d instances"), SolidPools, DecorationPools, Instances));
    TestTrue(TEXT("Fixture exercises collisionless decorations responsible for crash"), DecorationPools > 0);
    TestTrue(TEXT("Fixture preserves real furniture collision"), SolidPools > 0);
    {
        // Town hall lower flight reported at 0134010E. Its stair object is
        // owned by 0134010F, and must survive a neighboring-cell floor query.
        const uint32 StairCell=0x0134010F;
        const auto* Built=Dat->GetOrBuildEnvCellMesh(StairCell,100);
        if (TestNotNull(TEXT("Reported town hall stair room loads"),Built))
        {
            const auto Stabs=Built->StaticObjects;
            auto* Room=Fixture.World->SpawnActor<AACEEnvCellActor>();
            Room->LoadEnvCell(StairCell,FVector::ZeroVector,100);Room->SetEnvCellCollisionActive(true);
            for (const auto& Stab:Stabs)
            {
                Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
                Room->TrySpawnOneStaticObject(Dat,Stab);
            }
            auto* Stairs=Room->StaticObjectHisms.FindRef(0x02000623).Get();
            if (TestNotNull(TEXT("Town hall stair uses its authored physics mesh"),Stairs))
            {
                Fixture.World->Tick(LEVELTICK_All,.016f);
                FCollisionQueryParams Params(SCENE_QUERY_STAT(ACETownHallSteps),true);
                ACECellTransit::RestrictRoomCollision(*Fixture.World,{0x0134010Eu},Params);
                FTransform Placement;Stairs->GetInstanceTransform(0,Placement,true);
                const FBox Bounds=Stairs->GetStaticMesh()->GetBoundingBox().TransformBy(Placement);int32 Supported=0,Preserved=0;
                for (double X=Bounds.Min.X+25;X<Bounds.Max.X;X+=50)
                    for (double Y=Bounds.Min.Y+25;Y<Bounds.Max.Y;Y+=50)
                    {
                        const FVector Top(X,Y,Bounds.Max.Z+5),Bottom(X,Y,Bounds.Min.Z-5);
                        FHitResult Reference,Filtered;
                        if (!Fixture.World->LineTraceSingleByChannel(Reference,Top,Bottom,ECC_Pawn,FCollisionQueryParams(NAME_None,true))
                            || Reference.Component!=Stairs || Reference.ImpactNormal.Z<.66) continue;
                        ++Supported;
                        if (Fixture.World->LineTraceSingleByChannel(Filtered,Top,Bottom,ECC_Pawn,Params)
                            && Filtered.Component==Stairs) ++Preserved;
                    }
                TestTrue(TEXT("Town hall fixture has walkable stair treads"),Supported>10);
                TestEqual(TEXT("Neighbor-cell movement query preserves every town hall tread"),Preserved,Supported);
                AddInfo(FString::Printf(TEXT("Town hall authored stair support: %d/%d samples bounds=%s collision=%d bodies=%d"),Preserved,Supported,*Bounds.ToString(),int32(Stairs->GetCollisionEnabled()),Stairs->GetInstanceBodies().Num()));
            }
            Room->Destroy();
        }
    }
    // A connectivity portal may still be a drawable ceiling. The Town Network
    // flat cap cells are the exact regression: each contains a single polygon.
    for (uint32 Id : {0x000701A6u, 0x000701A7u})
    {
        const auto* Built = Dat->GetOrBuildEnvCellMesh(Id,100);
        if (!TestNotNull(TEXT("Town Network ceiling cap exists"),Built)) continue;
        TestTrue(TEXT("Cap retains ceiling geometry despite its cell portal"),!Built->Sections.IsEmpty());
        auto* Cap = Fixture.World->SpawnActor<AACEEnvCellActor>();
        TestTrue(TEXT("Ceiling cap uploads to the renderer"),Cap->LoadEnvCell(Id,FVector(0,134400,0),100));
        Cap->SetEnvCellHiddenInGame(false); Cap->SetCeilingDrawSuppressed(false);
        TestTrue(TEXT("Runtime ceiling has visible draw sections"),Cap->CellMesh->GetNumSections()>0
            && Cap->CellMesh->GetProcMeshSection(0)->bSectionVisible);
        Cap->Destroy();
    }
    // Destroying attachment parents alone does not destroy separately spawned
    // animated scenery. Repeat the unload/revisit sequence with both actor owners.
    for (int32 Visit=0; Visit<3; ++Visit)
    {
        auto* Chunk=Fixture.World->SpawnActor<AACETerrainChunkActor>();
        auto* Land=Fixture.World->SpawnActor<AACELandblockActor>();
        auto* Bird=Fixture.World->SpawnActor<AACERegionSceneryActor>();
        Bird->AttachToActor(Land,FAttachmentTransformRules::KeepWorldTransform);
        Land->AnimatedScenery.Add(Bird); Chunk->ChildLandblocks.Add(0x7D640000,Land);
        Chunk->Destroy();
        TestTrue(TEXT("Chunk eviction destroys landblock children"),Land->IsActorBeingDestroyed());
        TestTrue(TEXT("Landblock eviction destroys animated birds"),Bird->IsActorBeingDestroyed());
        auto* Room=Fixture.World->SpawnActor<AACEEnvCellActor>();
        auto* Lamp=Fixture.World->SpawnActor<AACERegionSceneryActor>();
        Lamp->AttachToActor(Room,FAttachmentTransformRules::KeepWorldTransform);
        Room->AnimatedStabs.Add(Lamp); Room->Destroy();
        TestTrue(TEXT("Room eviction destroys scripted scenery"),Lamp->IsActorBeingDestroyed());
    }
    auto* Host=Fixture.World->SpawnActor<AActor>();
    auto* Terrain=NewObject<UACETerrainPresenterComponent>(Host);
    Host->AddInstanceComponent(Terrain); Terrain->RegisterComponent();
    auto* Entities=NewObject<UACEWorldPresenterComponent>(Host);
    Host->AddInstanceComponent(Entities); Entities->RegisterComponent();
    auto* Client=Fixture.GI->GetSubsystem<UACEClientSubsystem>();
    Terrain->Client=Client; Entities->Client=Client;
    FACEPosition Destination; Destination.CellId=0x00070143; Destination.Location=FVector(70,-60,0);
    Client->GetSession()->SetLocalPosition(Destination);
    Terrain->LastKnownCellId=Destination.CellId; Terrain->bHasKnownCell=true;
    Terrain->LastOutdoorCellId=0x7D640001; Terrain->KeptLandblocks.Add(0x7D640000);
    Terrain->SyncAroundCell(Destination.CellId);
    TestEqual(TEXT("Dungeon residency replaces the old outdoor ring"),Terrain->KeptLandblocks.Num(),1);
    TestTrue(TEXT("Destination dungeon stays resident"),Terrain->IsLandblockKept(0x00070000));
    TestFalse(TEXT("Previous town no longer stays resident in dungeon"),Terrain->IsLandblockKept(0x7D640000));
    FACEWorldObject Remote; Remote.Guid=0x50000005; Remote.bIsPlayer=true;
    Remote.bHasPosition=true; Remote.Position=Destination;
    TestTrue(TEXT("Networked player in dungeon is admitted"),Entities->ShouldStreamObject(Remote));
    Remote.bIsPlayer=false; Remote.ItemType=ACEItemType::Container;
    TestTrue(TEXT("Networked dungeon loot is admitted"),Entities->ShouldStreamObject(Remote));
    Remote.Position.CellId=0x7D640001; Remote.bHasVelocity=true;
    TestFalse(TEXT("Velocity field cannot bypass residency for ordinary objects"),Entities->ShouldStreamObject(Remote));
	Remote.bIsPlayer=true;Remote.ItemType=ACEItemType::Creature;Remote.Position=Destination;
	Remote.SetupId=0x02000001;Remote.MotionTableId=0x09000001;
	FACEWorldObject Weapon;Weapon.Guid=Remote.Guid+1;Weapon.ParentGuid=Weapon.WielderId=Remote.Guid;
	Weapon.ParentLocation=1;Weapon.ItemType=ACEItemType::Caster;Weapon.SetupId=0x02000F1C;Weapon.Name=TEXT("Weeping Wand");
	for (bool WeaponFirst : {false,true})
	{
		Client->GetSession()->UpsertWorldObject(Remote);Client->GetSession()->UpsertWorldObject(Weapon);
		TestTrue(TEXT("Held item admitted while its parent actor is still queued"),Entities->ShouldStreamObject(Weapon));
		Entities->HandleObjectCreated(WeaponFirst ? Weapon : Remote);
		if (WeaponFirst) Entities->DrainPendingSpawns();
		Entities->HandleObjectCreated(WeaponFirst ? Remote : Weapon);
		for (int32 Pass=0;Pass<6;++Pass)
		{
			Entities->DrainPendingSpawns();Entities->DrainPendingAttachments();Entities->DrainPendingDatAppearance();
		}
		auto* WeaponActor=Entities->Spawned.FindRef(Weapon.Guid).Get();
		TestNotNull(TEXT("Network weapon survives parent/child arrival order"),WeaponActor);
		if (WeaponActor)
		{
			TestTrue(TEXT("Network weapon attaches to its player"),WeaponActor->IsAttachedToParent());
			TestTrue(TEXT("Network weapon builds its DAT model"),WeaponActor->Appearance && WeaponActor->Appearance->HasAppearance());
		}
		Entities->DestroySpawnedGuid(Remote.Guid);
	}
	Entities->LastEntityKeepLbs.Reset();
	Entities->SyncEntitiesToKeepRing({0x00070000});
	for (int32 Pass=0;Pass<6;++Pass)
	{
		Entities->DrainPendingSpawns();Entities->DrainPendingAttachments();Entities->DrainPendingDatAppearance();
	}
	TestTrue(TEXT("Held weapon respawns with its player when residency returns"),Entities->Spawned.Contains(Weapon.Guid));
    // Surface-level dungeons can share coordinates with the landscape. Their
    // occupants must use the admitted room set, not the landblock keep ring.
    auto* HiddenRoom=Fixture.World->SpawnActor<AACEEnvCellActor>();
    Terrain->SpawnedEnvCells.Add(Destination.CellId,HiddenRoom);
    auto* RemoteActor=Entities->Spawned.FindRef(Remote.Guid).Get();
    auto* HeldActor=Entities->Spawned.FindRef(Weapon.Guid).Get();
    if (TestNotNull(TEXT("Dungeon occupant fixture"),RemoteActor)
        && TestNotNull(TEXT("Dungeon occupant weapon fixture"),HeldActor))
    {
        TGuardValue<EACESessionState> SessionState(Client->GetSession()->State,EACESessionState::InWorld);
        HiddenRoom->SetEnvCellHiddenInGame(false);
        Entities->RefreshCellVisibility();
        Client->SelectObject(Remote.Guid);
        TestEqual(TEXT("Visible dungeon occupant can be selected"),Client->GetSelectedObject().Guid,Remote.Guid);
        TestTrue(TEXT("Occupant of admitted room stays visible"),!RemoteActor->IsHidden());
        HiddenRoom->SetEnvCellHiddenInGame(true);
        Entities->RefreshCellVisibility();
        TestFalse(TEXT("Radar visibility predicate excludes hidden dungeon cell"),Terrain->IsWorldCellVisible(Destination.CellId));
        TestTrue(TEXT("Hidden dungeon hides its occupant"),RemoteActor->IsHidden());
        TestTrue(TEXT("Hidden dungeon hides held weapons too"),HeldActor->IsHidden());
        TestFalse(TEXT("Hidden occupant cannot be mouse-picked or block movement"),RemoteActor->GetActorEnableCollision());
        TestEqual(TEXT("Leaving admitted rooms clears the target"),Client->GetSelectedObject().Guid,0);
        RemoteActor->InitializeFromObject(Remote,100,true);
        HeldActor->AttachToParentActor(RemoteActor,Weapon.ParentLocation);
        TestTrue(TEXT("Appearance refresh cannot expose hidden occupant"),RemoteActor->IsHidden());
        TestFalse(TEXT("Physics refresh cannot restore hidden occupant collision"),RemoteActor->GetActorEnableCollision());
        TestTrue(TEXT("Attachment refresh cannot expose hidden weapon"),HeldActor->IsHidden());
        HiddenRoom->SetEnvCellHiddenInGame(false);
        Entities->RefreshCellVisibility();
        TestTrue(TEXT("Entering dungeon restores occupant and weapon"),!RemoteActor->IsHidden() && !HeldActor->IsHidden());
        TestTrue(TEXT("Entering dungeon restores picking"),RemoteActor->GetActorEnableCollision());
        Terrain->bShowOutdoorEntities=false;
        TestFalse(TEXT("Closed dungeon does not show outdoor occupants"),Terrain->IsWorldCellVisible(0x7D640001));
        Terrain->bShowOutdoorEntities=true;
        TestTrue(TEXT("Outdoor portal view admits outdoor occupants"),Terrain->IsWorldCellVisible(0x7D640001));
    }
    HiddenRoom->Destroy(); Terrain->SpawnedEnvCells.Remove(Destination.CellId);
    Host->Destroy();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEParticleFrameReuseTest, "ACE.Rendering.ParticleFrameReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEParticleFrameReuseTest::RunTest(const FString& Parameters)
{
    auto* Reuse = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.ReuseEmitterFrames"));
    const int32 Previous = Reuse->GetInt();
    ON_SCOPE_EXIT { Reuse->Set(Previous, ECVF_SetByCode); };
    FInteriorTestWorld Fixture;
    auto* View = Fixture.World->SpawnActor<ACameraActor>();
    auto* PC = Fixture.World->SpawnActor<APlayerController>();
    Fixture.World->AddController(PC);
    if (!PC->PlayerCameraManager)
    {
        PC->PlayerCameraManager = Fixture.World->SpawnActor<APlayerCameraManager>();
        PC->PlayerCameraManager->InitializeFor(PC);
    }
    PC->PlayerCameraManager->bUseClientSideCameraUpdates = false;
    PC->SetViewTarget(View);
    auto* Owner = Fixture.World->SpawnActor<AActor>();
    auto* Root = NewObject<USceneComponent>(Owner);
    Owner->SetRootComponent(Root); Root->RegisterComponent();
    USceneComponent* Parents[2];
    for (auto& Parent : Parents)
    {
        Parent = NewObject<USceneComponent>(Owner);
        Parent->SetupAttachment(Root); Parent->RegisterComponent();
    }
    UACEScriptComponent* FX[2];
    for (auto& Component : FX)
    {
        Component = NewObject<UACEScriptComponent>(Owner);
        Owner->AddInstanceComponent(Component); Component->RegisterComponent();
        for (int32 E = 0; E < 2; ++E)
        {
            UACEScriptComponent::FActiveEmitter Emitter;
            Emitter.Parent = Parents[E]; Emitter.bVRHandFeedback = true; Emitter.bStopped = true;
            Emitter.Batch = NewObject<UACEParticleBatchComponent>(Owner);
            Owner->AddInstanceComponent(Emitter.Batch); Emitter.Batch->RegisterComponent();
            Emitter.Batch->CreateMeshSection(0, {{0,0,0},{0,2,0},{0,0,3}}, {0,1,2}, {}, {}, {}, {}, false);
            Emitter.Batch->InitializeParticles(128);
            for (int32 I = 0; I < 128; ++I)
            {
                UACEScriptComponent::FActiveParticle Particle;
                Particle.InstanceIndex = Emitter.Batch->AddParticle(FTransform::Identity, 1.f);
                Particle.bParentLocal = I % 2 == 0;
                Particle.DrawMode = I % 6;
                Particle.Type = 1 + I % 12;
                Particle.StartOrigin = FVector(400, E * 150, 200);
                Particle.Position = Particle.StartOrigin;
                Particle.Offset = FVector(I, I % 7, I % 11);
                Particle.A = FVector(5,3,1); Particle.B = FVector(.3,.7,.4); Particle.C = FVector(.4,.2,.8);
                Particle.Life = 10000.f; Particle.FinalTrans = .8f; Particle.FinalScale = .5f;
                Emitter.Particles.Add(Particle);
            }
            Component->ActiveEmitters.Add(MoveTemp(Emitter));
        }
    }
    // Move both attachments independently and change the camera without advancing
    // GFrameCounter. Reuse must be scoped to this call, never a stale frame cache.
    for (int32 Step = 0; Step < 4; ++Step)
    {
        View->SetActorLocation(FVector(-100 + Step * 31, 80 - Step * 43, 140));
        PC->PlayerCameraManager->UpdateCamera(0.f);
        for (int32 E = 0; E < 2; ++E)
            Parents[E]->SetWorldTransform(FTransform(FRotator(Step * 7, E * 60 + Step * 21, 3), FVector(200 + Step * 12, E * 90, 170 + Step * 8)));
        Reuse->Set(0, ECVF_SetByCode); FX[0]->TickEmitters(1.f / 30.f);
        Reuse->Set(1, ECVF_SetByCode); FX[1]->TickEmitters(1.f / 30.f);
        double MaxError = 0;
        bool SameColors = true;
        for (int32 E = 0; E < 2; ++E)
        {
            const auto& A = FX[0]->ActiveEmitters[E].Batch->GetProcMeshSection(0)->ProcVertexBuffer;
            const auto& B = FX[1]->ActiveEmitters[E].Batch->GetProcMeshSection(0)->ProcVertexBuffer;
            for (int32 I = 0; I < A.Num(); ++I)
            {
                MaxError = FMath::Max(MaxError, FVector::Distance(A[I].Position, B[I].Position));
                MaxError = FMath::Max(MaxError, FVector::Distance(A[I].Normal, B[I].Normal));
                SameColors &= A[I].Color == B[I].Color;
            }
        }
        TestTrue(TEXT("All draw/motion modes retain the reference particle geometry"), MaxError < .0001);
        TestTrue(TEXT("Reused frames retain reference fades"), SameColors);
    }
    for (int32 Mode : {0, 1})
    {
        Reuse->Set(Mode, ECVF_SetByCode);
        double Seconds = 0;
        for (int32 Sample = 0; Sample < 4; ++Sample)
        {
            FlushRenderingCommands();
            const double Start = FPlatformTime::Seconds();
            for (int32 Tick = 0; Tick < 100; ++Tick) FX[Mode]->TickEmitters(.001f);
            Seconds += FPlatformTime::Seconds() - Start;
        }
        AddInfo(FString::Printf(TEXT("Particle frame reuse %d: %.4f ms/update, 256 particles, 400 updates (CPU microbenchmark)"), Mode, Seconds * 1000 / 400));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEParticleDistanceTest, "ACE.RetailParity.ParticleDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEParticleDistanceTest::RunTest(const FString& Parameters)
{
    auto* Culling=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.DistanceCulling"));
    auto* IdleInterval=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.IdleTickInterval"));
    const float PreviousIdle=IdleInterval->GetFloat(); IdleInterval->Set(.1f,ECVF_SetByCode);
    const int32 Previous=Culling->GetInt(); Culling->Set(1,ECVF_SetByCode);
    ON_SCOPE_EXIT { Culling->Set(Previous,ECVF_SetByCode); IdleInterval->Set(PreviousIdle,ECVF_SetByCode); };
    FInteriorTestWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* View=Fixture.World->SpawnActor<ACameraActor>();
    auto* PC=Fixture.World->SpawnActor<APlayerController>();
    Fixture.World->AddController(PC);
    if (!PC->PlayerCameraManager)
    {
        PC->PlayerCameraManager=Fixture.World->SpawnActor<APlayerCameraManager>();
        PC->PlayerCameraManager->InitializeFor(PC);
    }
    PC->PlayerCameraManager->bUseClientSideCameraUpdates=false;
    PC->SetViewTarget(View); PC->PlayerCameraManager->UpdateCamera(0.f);
    auto* Owner=Fixture.World->SpawnActor<AActor>();
    auto* Root=NewObject<USceneComponent>(Owner); Owner->SetRootComponent(Root); Root->RegisterComponent();
    auto* FX=NewObject<UACEScriptComponent>(Owner); Owner->AddInstanceComponent(FX); FX->RegisterComponent();
    FACEWorldObject Pillar; Pillar.SetupId=0x02000F8B; FX->InitializeFromObject(Pillar,100);
    FVector Camera;
    if (!TestTrue(TEXT("Test uses the actual particle camera lookup"),FX->GetParticleViewLocation(Camera))) return false;
    FACEDatAnimationHook Glow; Glow.Type=EACEAnimationHookType::CreateParticle; Glow.Id=0x3200075A; Glow.SecondaryId=1;
    Owner->SetActorLocation(Camera+FVector(1000000,0,0));
    FX->CreateEmitter(Glow,false,1.f);
    if (!TestEqual(TEXT("Distant ambient script slot retained"),FX->ActiveEmitters.Num(),1)) return false;
    auto& Far=FX->ActiveEmitters[0];
    AddInfo(FString::Printf(TEXT("Authored pillar particle degrade distance %.2f meters"),Far.MaxDegradeDistance));
    TestTrue(TEXT("Distant initial population deferred without allocating a batch"),Far.bDegraded && !Far.Batch && Far.Particles.IsEmpty());
    FX->UpdateEffectTickInterval();
    TestEqual(TEXT("Only suspended infinite ambience uses a slower range check"),FX->GetComponentTickInterval(),.1f);
    FX->SetUvVelocity(0,.1f,0.f);
    TestEqual(TEXT("Incoming UV animation clears the ambient cooldown immediately"),FX->GetComponentTickInterval(),0.f);
    const double BeforeWakeClock=FX->ScriptClock;
    FX->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("New effects do not inherit time spent asleep"),
        FX->ScriptClock-BeforeWakeClock <= Fixture.World->GetDeltaSeconds()+.00001);
    FX->UvScrolls.Reset();
    FX->TickEmitters(60.f);
    TestTrue(TEXT("Infinite ambient survives time spent distant"),!FX->ActiveEmitters.IsEmpty());
    Owner->SetActorLocation(Camera+FVector(100,0,0)); FX->TickEmitters(1.f/30.f); FX->TickParticleLights();
    auto& Near=FX->ActiveEmitters[0];
    FX->UpdateEffectTickInterval();
    TestEqual(TEXT("Visible ambient effects resume normal tick cadence"),FX->GetComponentTickInterval(),0.f);
    TestTrue(TEXT("Approaching a deferred emitter restores its authored initial population"),!Near.bDegraded && !Near.Particles.IsEmpty());
    TestTrue(TEXT("Near luminous effect has a light"),!FX->ParticleLights.IsEmpty() && FX->ParticleLights[0]->IsVisible());
    Owner->SetActorLocation(Camera+FVector(1000000,0,0)); FX->TickEmitters(1.f/30.f); FX->TickParticleLights();
    TestTrue(TEXT("Leaving draw range hides batch and lights"),Near.bDegraded && (!Near.Batch || !Near.Batch->IsVisible()) && !FX->ParticleLights[0]->IsVisible());
    const int32 Births=Near.TotalBorn; FX->TickEmitters(10.f);
    TestEqual(TEXT("Infinite ambient does not accumulate births while degraded"),Near.TotalBorn,Births);
    Culling->Set(0,ECVF_SetByCode); FX->TickEmitters(1.f/30.f);
    TestFalse(TEXT("Comparison switch resumes a distant emitter live"),Near.bDegraded);
    Culling->Set(1,ECVF_SetByCode); FX->TickEmitters(1.f/30.f);
    FX->StopEmitter(1,false); FX->TickEmitters(1.f/30.f);
    TestTrue(TEXT("Stopped degraded ambient emitter cannot leak"),FX->ActiveEmitters.IsEmpty());
    // Both finite budgets expire offscreen; returning cannot replay old combat FX.
    for (const bool Timed : {true,false})
    {
        FX->CreateEmitter(Glow,false,1.f);
        auto& Finite=FX->ActiveEmitters[0]; Finite.Info.TotalSeconds=Timed ? .1 : 0;
        Finite.Info.TotalParticles=Timed ? 0 : 2; Finite.Info.EmitterType=1;
        Finite.Info.Birthrate=.01; Finite.TotalBorn=0;
        FX->UpdateEffectTickInterval();
        TestEqual(TEXT("Finite effect expiry never inherits distant ambient cadence"),FX->GetComponentTickInterval(),0.f);
        for(int32 I=0;I<5 && !FX->ActiveEmitters.IsEmpty();++I) FX->TickEmitters(.04f);
        TestTrue(TEXT("Degraded finite effect retires at its time/count budget"),FX->ActiveEmitters.IsEmpty());
    }
    FX->CreateEmitter(Glow,false,1.f);
    auto& Special=FX->ActiveEmitters[0]; Special.bVRHandFeedback=true;
    FX->UpdateEffectTickInterval();
    TestEqual(TEXT("Tracked feedback cannot use slow ambient cadence"),FX->GetComponentTickInterval(),0.f);
    TestFalse(TEXT("Tracked-hand feedback remains visible"),FX->ShouldDegradeEmitter(Special,Camera));
    Special.bVRHandFeedback=false; FX->bEnvironmentWeather=true;
    TestFalse(TEXT("Astronomical sky/weather frames are exempt"),FX->ShouldDegradeEmitter(Special,Camera));
    FX->bEnvironmentWeather=false; FX->StopAllEffects(); FX->WakeEffectTick();
    FX->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestFalse(TEXT("Finished distant effects disable ticking instead of returning forever"),FX->IsComponentTickEnabled());
    Owner->Destroy(); PC->Destroy(); View->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailParticleLightingTest, "ACE.RetailParity.ParticleLighting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailParticleLightingTest::RunTest(const FString& Parameters)
{
    // These fixtures inspect the reference renderer's individual meshes.
    // RenderingAndReplication compares its pixels against the instanced path.
    auto* Batches=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.Batched"));
    const int32 PreviousBatchMode=Batches->GetInt(); Batches->Set(0,ECVF_SetByCode);
    ON_SCOPE_EXIT { Batches->Set(PreviousBatchMode,ECVF_SetByCode); };

    FInteriorTestWorld Fixture;
    for (int32 Kind = 0; Kind < 3; ++Kind)
    {
        AActor* Owner = Kind == 0 ? Fixture.World->SpawnActor<APawn>()
            : Kind == 1 ? static_cast<AActor*>(Fixture.World->SpawnActor<AACEWorldEntityActor>())
            : Fixture.World->SpawnActor<AActor>();
        if (auto* Entity = Cast<AACEWorldEntityActor>(Owner)) Entity->bIsPlayer = true;
        auto* FX = NewObject<UACEScriptComponent>(Owner);
        Owner->AddInstanceComponent(FX); FX->RegisterComponent();
        auto CountLights = [&]()
        {
            int32 Count = 0;
            for (UPointLightComponent* Light : FX->ParticleLights) if (Light && Light->IsVisible() && Light->Intensity > 0) ++Count;
            return Count;
        };
        auto& Emitter = FX->ActiveEmitters.AddDefaulted_GetRef();
        Emitter.LightLum = .8f;
        Emitter.Particles.AddDefaulted();
        // Color cannot distinguish blood from red enchantments; test non-red blood too.
        for (FLinearColor Color : {FLinearColor::Red, FLinearColor::Green, FLinearColor::White})
        {
            Emitter.LightColor = Color;
            for (uint32 Script = 0x5B; Script <= 0x66; ++Script)
            {
                Emitter.SourcePlayScript = Script; FX->TickParticleLights();
                TestEqual(TEXT("Directional blood splatter never lights the scene"), CountLights(), 0);
            }
        }
        Emitter.LightColor = FLinearColor::Red;
        // Direct animation/weapon emitter, projectile collision, neighboring sparks,
        // and enchantment source effects must stay luminous even on player actors.
        for (uint32 Script : {0u, 0x5Au, 0x67u, 0x8Du})
        {
            Emitter.SourcePlayScript = Script; FX->TickParticleLights();
            TestEqual(TEXT("Red non-blood effects retain their light on every owner type"), CountLights(), 1);
        }
        if (auto* Entity=Cast<AACEWorldEntityActor>(Owner))
        {
            Entity->PhysicsState=ACEPhysicsState::Missile;
            Entity->SetActorLocation(FVector(900,400,200));
            Emitter.Particles[0].Position=FVector(-900,-400,0);
            Emitter.SourcePlayScript=4; FX->TickParticleLights();
            TestTrue(TEXT("Projectile light travels with its head, not the fading trail centroid"),
                FX->ParticleLights[0]->GetComponentLocation().Equals(Entity->GetActorLocation(),.01));
            TestTrue(TEXT("Projectile illumination is bright enough to reach nearby surfaces"),FX->ParticleLights[0]->Intensity>=10.f);
        }
        Emitter.SourcePlayScript = 0x60; FX->TickParticleLights();
        TestEqual(TEXT("Reused light is hidden for blood"), CountLights(), 0);
        FX->bEnvironmentWeather = true; Emitter.SourcePlayScript = 0; FX->TickParticleLights();
        TestEqual(TEXT("Weather remains non-luminous"), CountLights(), 0);
        FX->StopAllEffects();
        Owner->Destroy();
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailRuntimeCostTest, "ACE.RetailParity.RuntimeCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailRuntimeCostTest::RunTest(const FString& Parameters)
{
    FInteriorTestWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    TArray<AACEWorldEntityActor*> Actors;
    for (int32 I=0;I<64;++I)
    {
        FACEWorldObject O;O.Guid=1000+I;O.SetupId=0x02000001;O.MotionTableId=0x09000001;
        O.ItemType=ACEItemType::Creature;O.bIsPlayer=true;O.bHasPosition=true;
        O.Position.CellId=0x016C01C3;O.Position.Location=FVector((I%8)*3,(I/8)*3,0);
        auto* A=Fixture.World->SpawnActor<AACEWorldEntityActor>();A->InitializeFromObject(O,100,true);
        Actors.Add(A);
    }
    double ActorTime=0,AnimationTime=0;
    for(int32 Frame=0;Frame<360;++Frame)
    {
        double Start=FPlatformTime::Seconds();
        for(auto* A:Actors) if(A->IsActorTickEnabled()) A->Tick(1.f/60.f);
        ActorTime+=FPlatformTime::Seconds()-Start;
        Start=FPlatformTime::Seconds();
        for(auto* A:Actors) A->Appearance->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
        AnimationTime+=FPlatformTime::Seconds()-Start;
    }
    int32 Ticking=0;for(auto* A:Actors) Ticking+=A->IsActorTickEnabled();
    AddInfo(FString::Printf(TEXT("Runtime CPU fixture (64 idle players): actor/ground %.3f ms; animation %.3f ms; ticking actors %d"),
        ActorTime*1000/360,AnimationTime*1000/360,Ticking));
    TestEqual(TEXT("Settled remotes stop ground polling even without a streamed floor"),Ticking,0);
    auto* Remote=Actors[0];
    TestTrue(TEXT("Idle appearance still animates independently"),Remote->Appearance->IsComponentTickEnabled());
    FACEPosition Correction;Correction.CellId=0x016C01C3;Correction.Location=FVector(0.4,0,0);
    const FVector Before=Remote->GetActorLocation();Remote->ApplyACEPosition(Correction);
    TestTrue(TEXT("Server correction wakes idle actor"),Remote->IsActorTickEnabled());
    Remote->Tick(1.f/60.f);
    TestTrue(TEXT("Woken actor interpolates the new position"),!Remote->GetActorLocation().Equals(Before,.01f));
    FACEObjectMotionState Motion;Motion.bMoving=true;Motion.StrafeUnitsPerSecond=2;
    Remote->ApplyMotionState(Motion);
    TestTrue(TEXT("Strafing wakes actor without forward speed"),Remote->IsActorTickEnabled());
    for(auto* A:Actors) A->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailParticleTimingTest, "ACE.RetailParity.ParticleTiming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailParticleTimingTest::RunTest(const FString& Parameters)
{
    // These fixtures inspect the reference renderer's individual meshes.
    // RenderingAndReplication compares its pixels against the instanced path.
    auto* Batches=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.Batched"));
    const int32 PreviousBatchMode=Batches->GetInt(); Batches->Set(0,ECVF_SetByCode);
    ON_SCOPE_EXIT { Batches->Set(PreviousBatchMode,ECVF_SetByCode); };

    FInteriorTestWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    const auto* Info=Dat->GetParticleEmitterInfo(0x320005A8);
    if (!TestNotNull(TEXT("Authored wand emitter exists"),Info)) return false;
    auto* Owner=Fixture.World->SpawnActor<AActor>();
    auto* Root=NewObject<USceneComponent>(Owner); Owner->SetRootComponent(Root); Root->RegisterComponent();
    auto* FX=NewObject<UACEScriptComponent>(Owner); Owner->AddInstanceComponent(FX); FX->RegisterComponent();
    auto& E=FX->ActiveEmitters.AddDefaulted_GetRef(); E.Info=*Info;
    E.Info.EmitterType=1; E.Info.InitialParticles=0; E.Info.Birthrate=.02;
    E.Info.TotalSeconds=5; E.Info.TotalParticles=10; E.Info.MaxParticles=10;
    FX->TickEmitters(.2f);
    TestEqual(TEXT("Hitch emits once, matching ParticleEmitter::UpdateParticles"),E.TotalBorn,1);
    if (TestEqual(TEXT("Real DAT particle visual created"),E.Particles.Num(),1))
        TestEqual(TEXT("Newborn retains its birth frame"),E.Particles[0].Age,0.f);
    FX->TickEmitters(.01f);
    TestEqual(TEXT("Last emission time resets to now; no backlog"),E.TotalBorn,1);
    FX->TickEmitters(.015f);
    TestEqual(TEXT("Next authored interval emits one particle"),E.TotalBorn,2);
    if (E.Particles.Num()>1) TestEqual(TEXT("Subsequent birth also starts at age zero"),E.Particles.Last().Age,0.f);
    FX->StopAllEffects();
    // Reverse release order reproduces interleaved glow/spark reuse. Neither
    // particle should lose its geometry or private material to the other.
    auto* PooledGlow=FX->AcquireParticleMesh(0x01001067,false);
    auto* PooledSpark=FX->AcquireParticleMesh(0x01001097,false);
    auto* PooledGlowMaterial=PooledGlow ? PooledGlow->GetMaterial(0) : nullptr;
    auto* PooledSparkMaterial=PooledSpark ? PooledSpark->GetMaterial(0) : nullptr;
    FX->ReleaseParticleMesh(PooledGlow); FX->ReleaseParticleMesh(PooledSpark);
    auto* ReusedPooledGlow=FX->AcquireParticleMesh(0x01001067,false);
    auto* ReusedPooledSpark=FX->AcquireParticleMesh(0x01001097,false);
    TestTrue(TEXT("Interleaved emitter reuses matching geometry and private material"),
        PooledGlow && PooledSpark && ReusedPooledGlow==PooledGlow && ReusedPooledSpark==PooledSpark
        && ReusedPooledGlow->GetMaterial(0)==PooledGlowMaterial && ReusedPooledSpark->GetMaterial(0)==PooledSparkMaterial);
    FX->ReleaseParticleMesh(ReusedPooledGlow); FX->ReleaseParticleMesh(ReusedPooledSpark);
    FACEWorldObject Object; Object.SetupId=0x02000001;
    FX->InitializeFromObject(Object,100);
    FACEDatAnimationHook Hook; Hook.Type=EACEAnimationHookType::CreateParticle; Hook.Id=0x320005A8; Hook.SecondaryId=1;
    FX->CreateEmitter(Hook, false, 1.f);
    if (TestEqual(TEXT("Wand emitter created through hook path"),FX->ActiveEmitters.Num(),1))
    {
        auto& Wand=FX->ActiveEmitters[0];
        TestEqual(TEXT("Wand retains DAT emission interval"),Wand.Info.Birthrate,Info->Birthrate);
        TestEqual(TEXT("Wand retains DAT parent space"),Wand.Info.IsParentLocal,Info->IsParentLocal);
        FX->SpawnParticle(Wand);
        if (TestTrue(TEXT("Wand birth produces a particle"),!Wand.Particles.IsEmpty()))
        {
            TestTrue(TEXT("Wand uses DAT starting-size variation"),FMath::IsWithinInclusive(Wand.Particles.Last().StartScale,.9f,1.1f));
            TestTrue(TEXT("Wand uses DAT ending-size variation and retail minimum"),FMath::IsWithinInclusive(Wand.Particles.Last().FinalScale,.1f,.2f));
        }
    }
    FX->StopAllEffects();
    // Four single banners and one double banner in landblock 2B12 use this
    // .03-second glow (scripts 33000DBA/DBB). Check every rendered frame, not
    // just emitter births, so a high-refresh shrink/flash cannot slip through.
    const auto* PillarInfo=Dat->GetParticleEmitterInfo(0x3200075A);
    if (!TestNotNull(TEXT("Reported pillar glow is present in DAT"),PillarInfo)) return false;
    TestEqual(TEXT("Pillar lifetime is shorter than retail's physics quantum"),PillarInfo->Lifespan,.03);
    for (const int32 Fps : {24,30,60,120,144,240})
    {
        FX->StopAllEffects();
        FACEWorldObject Pillar; Pillar.SetupId=0x02000F8B;
        FX->InitializeFromObject(Pillar,100);
        FACEDatAnimationHook Glow; Glow.Type=EACEAnimationHookType::CreateParticle;
        Glow.Id=0x3200075A; Glow.SecondaryId=0; FX->CreateEmitter(Glow,false,1.f);
        if (!TestEqual(TEXT("Single pillar emitter is created"),FX->ActiveEmitters.Num(),1)) continue;
        bool Visible=true,StableSize=true,StableOpacity=true;
        for (int32 Frame=0; Frame<Fps*2; ++Frame)
        {
            FX->TickComponent(1.f/Fps,LEVELTICK_All,nullptr);
            if (Frame < Fps/5) continue;
            const auto& PillarEmitter=FX->ActiveEmitters[0];
            if (PillarEmitter.Particles.Num()!=1) { Visible=false; continue; }
            const auto* Mesh=PillarEmitter.Particles[0].Mesh.Get();
            if (!Mesh || !Mesh->IsVisible()) { Visible=false; continue; }
            StableSize &= Mesh->GetComponentScale().Equals(FVector(3.3),.001);
            auto* Mid=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            StableOpacity &= Mid && FMath::IsNearlyEqual(Mid->K2_GetScalarParameterValue(TEXT("OpacityMul")),.4f,.001f);
        }
        TestTrue(FString::Printf(TEXT("Pillar remains visible on every frame at %d Hz"),Fps),Visible);
        TestTrue(FString::Printf(TEXT("Pillar glow never shrinks between physics updates at %d Hz"),Fps),StableSize);
        TestTrue(FString::Printf(TEXT("Pillar opacity stays authored at %d Hz"),Fps),StableOpacity);
        if (Fps==60 && FX->ActiveEmitters[0].Particles.Num()==1)
        {
            Fixture.World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
            auto* GlowMesh=FX->ActiveEmitters[0].Particles[0].Mesh.Get();
            FX->TickEmitters(1.f/30.f);
            TestTrue(TEXT("Continuous glow reuses its component across a birth"),
                FX->ActiveEmitters[0].Particles.Num()==1 && FX->ActiveEmitters[0].Particles[0].Mesh.Get()==GlowMesh);
            TestFalse(TEXT("Continuous glow birth does not invalidate the render proxy"),GlowMesh->IsRenderStateDirty());
        }
        FX->StopEmitter(FX->ActiveEmitters[0].InstanceId,false);
        for (int32 Frame=0;Frame<Fps/2;++Frame) FX->TickComponent(1.f/Fps,LEVELTICK_All,nullptr);
        TestTrue(TEXT("Stopped glow expires normally instead of being frozen forever"),FX->ActiveEmitters.IsEmpty());
    }
    FX->StopAllEffects();
    // Actual reported fixtures, including virtual part 6 in the destroyed portal.
    {
        // Fianhe's authored paired eye emitters. Model scaling belongs to the
        // part origin, never to ParticleEmitter::SetParenting's copied offset.
        FACEWorldObject Empyrean; Empyrean.SetupId=0x02001A16; Empyrean.ItemType=ACEItemType::Creature;
        Empyrean.Scale=1.25f;
        auto* Actor=Fixture.World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Empyrean,100,true);
        auto* Script=Actor->ScriptComponent.Get(); Script->StopAllEffects(); Script->PlayScriptId(0x33001285,1.f);
        TestEqual(TEXT("Fianhe has two authored eye emitters"),Script->ActiveEmitters.Num(),2);
        for(const auto& Eye:Script->ActiveEmitters)
        {
            TestEqual(TEXT("Eye glow anchors to head part"),Eye.PartIndex,16);
            TestTrue(TEXT("Eye hook retains retail offset at non-unit model scale"),FMath::IsNearlyEqual(Eye.HookLocalUe.Z,13.8,.01));
            TestTrue(TEXT("Eye lateral offset retains DAT spacing"),FMath::IsNearlyEqual(FMath::Abs(Eye.HookLocalUe.X),4.1,.01));
        }
        Actor->Destroy();
    }
    {
        // SpellProjectile::ProjectileImpact broadcasts SetState before Explode.
        // A point-blank impact must retain its script host through that sequence.
        auto* Client=Fixture.GI->GetSubsystem<UACEClientSubsystem>();
        auto* Presenter=NewObject<UACEWorldPresenterComponent>(Owner); Presenter->Client=Client;
        FACEWorldObject Bolt; Bolt.Guid=0x7000040D; Bolt.SetupId=0x0200040D;
        Bolt.PhysicsState=ACEPhysicsState::Missile; Bolt.PhysicsEffectTableId=0x34000004;
        auto* Projectile=Fixture.World->SpawnActor<AACEWorldEntityActor>();
        Projectile->InitializeFromObject(Bolt,100,true); Projectile->ScriptComponent->StopAllEffects();
        Client->GetSession()->WorldObjects.Add(Bolt.Guid,Bolt); Presenter->Spawned.Add(Bolt.Guid,Projectile);
        Presenter->HandlePhysicsStateUpdate(Bolt.Guid,Bolt.PhysicsState|ACEPhysicsState::Cloaked|ACEPhysicsState::NoDraw|ACEPhysicsState::Ethereal);
        TestTrue(TEXT("Impact cloak preserves the projectile effect host"),Presenter->Spawned.Contains(Bolt.Guid)&&IsValid(Projectile));
        Presenter->HandlePlayScriptId(Bolt.Guid,0x330000D5,1.f);
        TestTrue(TEXT("Impact script can still create particles after NoDraw"),!Projectile->ScriptComponent->ActiveEmitters.IsEmpty());
        Presenter->HandleObjectDeleted(Bolt.Guid);
        TestFalse(TEXT("Server deletion releases the projectile"),Presenter->Spawned.Contains(Bolt.Guid));
        Client->GetSession()->WorldObjects.Remove(Bolt.Guid);
    }
    for (uint32 Setup : {0x02000F1Cu,0x020019E4u,0x02000EC3u,0x020012BCu,0x020012BFu,0x020012BBu,0x0200184Au,0x020012C0u,0x0200184Du,0x02001491u})
    {
        FACEWorldObject Reported; Reported.Guid=Setup; Reported.SetupId=Setup; Reported.ItemType=ACEItemType::Misc;
        auto* Actor=Fixture.World->SpawnActor<AACEWorldEntityActor>();
        Actor->InitializeFromObject(Reported,100,true);
        Actor->SetActorLocationAndRotation(FVector(2300,4100,1700),FRotator(0,67,0));
        auto* Script=Actor->ScriptComponent.Get();
        for(int32 I=0;I<20;++I) Script->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
        int32 Drips=0; bool Disc=false; int32 Born=0;
        for(auto& Emitter:Script->ActiveEmitters)
        {
            AddInfo(FString::Printf(TEXT("Weapon FX setup=%08X emitter=%08X gfx=%08X type=%d parent=%d life=%.3f interval=%.3f scale=%.3f..%.3f particles=%d"),
                Setup,Emitter.AuthoredEmitterId,Emitter.Info.HwGfxObjId,Emitter.Info.ParticleType,Emitter.Info.IsParentLocal,
                Emitter.Info.Lifespan,Emitter.Info.Birthrate,Emitter.Info.StartScale,Emitter.Info.FinalScale,Emitter.Particles.Num()));
            if (const auto* Mesh=Dat->GetOrBuildSetupMesh(Emitter.Info.HwGfxObjId,100))
                for (const auto& PartMesh:Mesh->Parts) for (const auto& Section:PartMesh.Sections)
                {
                    FACEDatDecodedSurface Surface;
                    if (Dat->GetTextureResolver()->ResolveSurface(Section.SurfaceId,Surface))
                        AddInfo(FString::Printf(TEXT("Weapon surface %08X additive=%d clip=%d translucent=%d alpha=%d trans=%.2f lum=%.2f"),
                            Section.SurfaceId,Surface.bAdditive,Surface.bClipMap,Surface.bSurfaceTranslucent,Surface.bUsesAlpha,Surface.Translucency,Surface.Luminosity));
                }
            FTransform Part;
            if (Emitter.PartIndex>=0 && Actor->Appearance->GetPartCurrentTransform(Emitter.PartIndex,Part))
            {
                const FTransform Expected=Part*Actor->Appearance->GetMeshRoot()->GetComponentTransform();
                TestTrue(TEXT("Particle birth frame follows the rotated DAT part"),Script->GetParticleStartFrame(Emitter).GetLocation().Equals(Expected.GetLocation(),.1));
            }
            Script->SpawnParticle(Emitter);
            Born+=Emitter.Particles.Num();
            for(const auto& Particle:Emitter.Particles)
            {
                TestEqual(TEXT("Reported particle keeps DAT parent-local flag"),Particle.bParentLocal,Emitter.Info.IsParentLocal!=0);
                if (Emitter.AuthoredEmitterId==0x32000743)
                {
                    ++Drips;
                    TestTrue(TEXT("Weeping Wand drips accelerate down in world space"),Particle.B.Z<0 && FMath::Abs(Particle.B.X)<.001 && FMath::Abs(Particle.B.Y)<.001);
                    TestTrue(TEXT("Actual Weeping Wand size is its DAT scale, without an item shrink factor"),Particle.StartScale>=.1f && Particle.StartScale<=.3f);
                }
                if(Emitter.Info.GfxObjId==0x01003DE3) Disc=true;
            }
        }
        TestTrue(TEXT("Reported default script actually creates particle geometry"),Born>0);
        if (Setup==0x02001491 && FApp::CanEverRender())
        {
            // Exact setup and hooks from the reported Slashing Baton, rendered
            // from a real camera so billboard orientation/materials are exercised.
            Actor->SetActorLocationAndRotation(FVector::ZeroVector,FRotator(90,0,0));
            Script->StopAllEffects(); Script->StartDefaultScripts(false);
            // StopAllEffects retains the default-script latch; restart via DID.
            if (Script->ActiveEmitters.IsEmpty()) Script->PlayScriptId(0x33000F6B,1.f);
            auto* View=Fixture.World->SpawnActor<ACameraActor>();
            View->SetActorLocationAndRotation(FVector(-150,35,20),(FVector(0,35,0)-FVector(-150,35,20)).Rotation());
            View->GetCameraComponent()->SetFieldOfView(45);
            auto* PC=Fixture.World->SpawnActor<APlayerController>();
            Fixture.World->AddController(PC);
            if(!PC->PlayerCameraManager)
            {
                PC->PlayerCameraManager=Fixture.World->SpawnActor<APlayerCameraManager>();
                PC->PlayerCameraManager->InitializeFor(PC);
            }
            PC->PlayerCameraManager->bUseClientSideCameraUpdates=false;
            PC->SetViewTarget(View); PC->PlayerCameraManager->UpdateCamera(0.f);
            TestTrue(TEXT("Particle fixture has an active matching view"),Fixture.World->GetFirstPlayerController()==PC &&
                PC->PlayerCameraManager->GetCameraLocation().Equals(View->GetActorLocation(),.01));
            auto* Capture=NewObject<USceneCaptureComponent2D>(View);
            Capture->RegisterComponent(); Capture->SetWorldTransform(View->GetActorTransform());
            auto* RT=NewObject<UTextureRenderTarget2D>(View);
            RT->ClearColor=FLinearColor::Black; RT->InitCustomFormat(512,512,PF_FloatRGBA,true); RT->UpdateResourceImmediate(true);
            Capture->TextureTarget=RT; Capture->FOVAngle=45;
            Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
            Capture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR;
            Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            Capture->ShowFlags.SetAtmosphere(false); Capture->ShowFlags.SetFog(false);
            Capture->ShowFlags.SetBloom(false); Capture->ShowFlags.SetEyeAdaptation(false);
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Automation/BatonFx");
            IFileManager::Get().MakeDirectory(*Dir,true);
            for(int32 Frame=0;Frame<180;++Frame)
            {
                Script->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
                if(Frame<60 || Frame%15) continue;
                int32 Stars=0,Glows=0;
                Capture->ClearShowOnlyComponents();
                for(const auto& Emitter:Script->ActiveEmitters)
                {
                    if(Emitter.AuthoredEmitterId==0x32000802) Stars+=Emitter.Particles.Num();
                    if(Emitter.AuthoredEmitterId==0x32000803) Glows+=Emitter.Particles.Num();
                    for(const auto& Particle:Emitter.Particles) if(Particle.Mesh.IsValid()) Capture->ShowOnlyComponent(Particle.Mesh.Get());
                    if(Frame==60 && !Emitter.Particles.IsEmpty())
                    {
                        float OutputGain=0.f;
                        Emitter.Particles[0].Mesh->GetMaterial(0)->GetScalarParameterValue(TEXT("ParticleBrightness"),OutputGain);
                        TestEqual(TEXT("Sparkles brighten independently of the stationary halo"),OutputGain,
                            Emitter.AuthoredEmitterId==0x32000802 ? 1.8f : 1.f);
                        UTexture* Texture=nullptr;
                        Emitter.Particles[0].Mesh->GetMaterial(0)->GetTextureParameterValue(TEXT("ACETexture"),Texture);
                        auto* Sprite=Cast<UTexture2D>(Texture);
                        if(TestNotNull(TEXT("Baton uses its DAT sprite texture"),Sprite))
                        {
                            TestEqual(TEXT("Retail sprite has four mip levels"),Sprite->GetNumMips(),4);
                            TestEqual(TEXT("Retail sprite selects mips without trilinear blending"),Sprite->Filter,TF_Bilinear);
                        }
                    }
                }
                TestEqual(TEXT("Slashing Baton has one persistent glow"),Glows,1);
                TestTrue(TEXT("Slashing Baton maintains the authored star drip"),Stars>=4 && Stars<=5);
                if(GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                Capture->CaptureScene(); FlushRenderingCommands();
                TArray<FColor> Pixels;
                RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
                for(auto& Pixel:Pixels) Pixel.A=255;
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(512,512,Pixels,PNG);
                FFileHelper::SaveArrayToFile(PNG,*(Dir/FString::Printf(TEXT("SlashingBaton-%03d.png"),Frame)));
            }
            // Independent pixel oracle for fixed-function source modulation.
            // White texels remove texture filtering from the calculation; test
            // both texture alpha and emitter translucency, exactly once each.
            UProceduralMeshComponent* Card=nullptr;
            for(const auto& Emitter:Script->ActiveEmitters)
                if(Emitter.AuthoredEmitterId==0x32000803 && !Emitter.Particles.IsEmpty()) Card=Emitter.Particles[0].Mesh.Get();
            if(TestNotNull(TEXT("Glow card available for blend regression"),Card))
            {
                Capture->ClearShowOnlyComponents(); Capture->ShowOnlyComponent(Card);
                auto* Mid=Cast<UMaterialInstanceDynamic>(Card->GetMaterial(0));
                float Brightness=0.f;
                TestTrue(TEXT("Stationary halo retains original brightness"),
                    Mid->GetScalarParameterValue(TEXT("ParticleBrightness"),Brightness) && Brightness==1.f);
                Brightness=1.8f;
                Mid->SetScalarParameterValue(TEXT("ParticleBrightness"),Brightness);
                const bool MobileLDR=Fixture.World->GetFeatureLevel()<=ERHIFeatureLevel::ES3_1
                    && IConsoleManager::Get().FindConsoleVariable(TEXT("r.MobileHDR"))->GetInt()==0;
                // MobileBasePassPixelShader writes sqrt(linear) into the LDR
                // scene color target, including SceneColorHDR captures. Values
                // above white clamp there; SM5 preserves linear HDR values.
                auto CaptureValue=[&](float LinearValue) { return MobileLDR
                    ? FMath::Sqrt(FMath::Clamp(LinearValue,0.f,1.f)) : LinearValue; };
                for(uint8 Alpha : {uint8(255),uint8(128)})
                {
                    auto* White=FACEDatTextureResolver::CreateTransientRgbaUi(1,1,{FColor(255,255,255,Alpha)});
                    Mid->SetTextureParameterValue(TEXT("ACETexture"),White);
                    for(float Fade : {0.f,.3f,1.f})
                    {
                        Mid->SetScalarParameterValue(TEXT("OpacityMul"),Fade);
                        Capture->CaptureScene(); FlushRenderingCommands();
                        TArray<FFloat16Color> Linear;
                        RT->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Linear);
                        float Peak=0.f; for(const auto& Pixel:Linear) Peak=FMath::Max(Peak,Pixel.R.GetFloat());
                        const float Display=Fade*float(Alpha)/255.f;
                        const float Expected=Display<=.04045f ? Display/12.92f : FMath::Pow((Display+.055f)/1.055f,2.4f);
                        TestTrue(FString::Printf(TEXT("Particle gain preserves alpha=%u fade=%.1f: captured peak %.4f expected %.4f"),Alpha,Fade,Peak,CaptureValue(Expected*Brightness)),FMath::Abs(Peak-CaptureValue(Expected*Brightness))<.012f);
                        Mid->SetScalarParameterValue(TEXT("ParticleBrightness"),1.f);
                        Capture->CaptureScene(); FlushRenderingCommands();
                        RT->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Linear);
                        Peak=0; for(const auto& Pixel:Linear) Peak=FMath::Max(Peak,Pixel.R.GetFloat());
                        TestTrue(TEXT("Unity gain retains retail source modulation"),FMath::Abs(Peak-CaptureValue(Expected))<.012f);
                        Mid->SetScalarParameterValue(TEXT("ParticleBrightness"),Brightness);
                        const FQuat Facing=Card->GetComponentQuat();
                        Card->SetWorldRotation(Facing*FQuat(FVector::UpVector,PI));
                        Capture->CaptureScene(); FlushRenderingCommands();
                        RT->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Linear);
                        Peak=0; for(const auto& Pixel:Linear) Peak=FMath::Max(Peak,Pixel.R.GetFloat());
                        TestTrue(TEXT("Authored back face remains visible with the same brightness"),FMath::Abs(Peak-CaptureValue(Expected*Brightness))<.012f);
                        Card->SetWorldRotation(Facing);
                    }
                }
                // Ordinary alpha particles share the authored front/back mesh.
                // One 50% white layer on black must yield 0.5, not the 0.75
                // produced by rendering the reverse face as a second layer.
                auto* AlphaMid=UMaterialInstanceDynamic::Create(Dat->EnsureAceParticleTranslucentMaterialBase(),Card);
                AlphaMid->SetTextureParameterValue(TEXT("ACETexture"),FACEDatTextureResolver::CreateTransientRgbaUi(1,1,{FColor::White}));
                AlphaMid->SetScalarParameterValue(TEXT("DiffuseStrength"),1.f);
                AlphaMid->SetScalarParameterValue(TEXT("EmissiveStrength"),1.f);
                AlphaMid->SetScalarParameterValue(TEXT("OpacityMul"),.5f);
                AlphaMid->SetScalarParameterValue(TEXT("FogAmount"),0.f);
                Card->SetMaterial(0,AlphaMid);
                if(GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                Capture->CaptureScene(); FlushRenderingCommands();
                TArray<FFloat16Color> Linear;
                RT->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Linear);
                float Peak=0; for(const auto& Pixel:Linear) Peak=FMath::Max(Peak,Pixel.R.GetFloat());
                TestTrue(TEXT("Alpha sprite blends its face only once"),FMath::Abs(Peak-.5f)<.012f);
            }
            Capture->DestroyComponent(); PC->Destroy(); View->Destroy();
        }
        if(Setup==0x02000F1C) TestTrue(TEXT("All three authored weeping drip hooks run"),Drips>=3);
        if(Setup==0x020019E4) TestTrue(TEXT("Destroyed portal's authored disc is rendered"),Disc);
        if(Setup==0x02000EC3)
        {
            // Addressing a body buff to the item must not use a human PeTable.
            const int32 Before=Script->ActiveEmitters.Num(); Script->PlayEffect(0x41,1.f);
            TestEqual(TEXT("Assault Orb does not invent human buff effects"),Script->ActiveEmitters.Num(),Before);
            auto* Client=Fixture.GI->GetSubsystem<UACEClientSubsystem>();
            auto* Presenter=NewObject<UACEWorldPresenterComponent>(Actor); Presenter->Client=Client;
            FACEWorldObject Wielder; Wielder.Guid=1729; Wielder.SetupId=0x02000001;
            Wielder.bIsPlayer=true; Wielder.PhysicsEffectTableId=0x34000004; Wielder.ItemType=ACEItemType::Creature;
            auto* Body=Fixture.World->SpawnActor<AACEWorldEntityActor>(); Body->InitializeFromObject(Wielder,100,true);
            Body->ScriptComponent->NotifyAppearanceReady(); Body->ScriptComponent->StopAllEffects();
            Reported.WielderId=Wielder.Guid;
            Client->GetSession()->WorldObjects.Add(Reported.Guid,Reported);
            Client->GetSession()->WorldObjects.Add(Wielder.Guid,Wielder);
            Presenter->Spawned.Add(Reported.Guid,Actor); Presenter->Spawned.Add(Wielder.Guid,Body);
            TestTrue(TEXT("Network item effect resolves its own ready script component"),Presenter->TryPlayEffectOnActor(Reported.Guid,0x41,1.f));
            TestEqual(TEXT("Item effect cannot flash a second buff on its wielder"),Body->ScriptComponent->ActiveEmitters.Num(),0);
            TestTrue(TEXT("An explicitly addressed body effect still plays"),Presenter->TryPlayEffectOnActor(Wielder.Guid,0x41,1.f));
            TestTrue(TEXT("Body routing control creates the requested buff particles"),Body->ScriptComponent->ActiveEmitters.Num()>0);
            Body->ScriptComponent->StopAllEffects();
            Presenter->Spawned.Remove(Wielder.Guid);
            Presenter->HandlePlayEffect(Wielder.Guid,0x41,1.f);
            Presenter->HandlePlayScriptId(Wielder.Guid,0x330000D5,1.f);
            Presenter->HandlePlayEffect(Wielder.Guid,0x41,1.f);
            TestEqual(TEXT("Repeated queued effects retain both messages"),Presenter->PendingEffects.Num(),2);
            Presenter->Spawned.Add(Wielder.Guid,Body);
            Presenter->FlushPendingEffectsFor(Wielder.Guid);
            TestEqual(TEXT("Queued script opcodes flush without losing anonymous hooks"),Body->ScriptComponent->ActiveEmitters.Num(),4);
            for(const auto& Emitter:Body->ScriptComponent->ActiveEmitters)
                if(Emitter.InstanceId==1) TestEqual(TEXT("Last server request owns the buff slot"),Emitter.AuthoredEmitterId,0x320000DCu);
            Presenter->Spawned.Reset(); Client->GetSession()->WorldObjects.Reset();
            Body->Destroy();
        }
        Script->StopAllEffects(); Actor->Destroy();
    }
    {
        // Real wielded baton lifecycle, including the state/attachment refresh
        // that the isolated material fixture above deliberately doesn't exercise.
        auto* Presenter=NewObject<UACEWorldPresenterComponent>(Owner);
        Presenter->Client=Fixture.GI->GetSubsystem<UACEClientSubsystem>();
        FACEWorldObject Wielder; Wielder.Guid=0x123401; Wielder.SetupId=0x02000001;
        Wielder.bIsPlayer=true; Wielder.ItemType=ACEItemType::Creature;
        auto* Body=Fixture.World->SpawnActor<AACEWorldEntityActor>(); Body->InitializeFromObject(Wielder,100,true);
        FACEWorldObject Baton; Baton.Guid=0x123402; Baton.SetupId=0x02001491; Baton.ItemType=ACEItemType::Misc;
        Baton.ParentGuid=Wielder.Guid; Baton.WielderId=Wielder.Guid; Baton.ParentLocation=1;
        auto* Weapon=Fixture.World->SpawnActor<AACEWorldEntityActor>(); Weapon->InitializeFromObject(Baton,100,true);
        Presenter->Spawned.Add(Wielder.Guid,Body); Presenter->Spawned.Add(Baton.Guid,Weapon);
        auto* Script=Weapon->ScriptComponent.Get();
        TestTrue(TEXT("Baton attaches through the live presenter"),Presenter->TryAttachToParent(Weapon,Wielder.Guid,1));
        bool KeptOffsets=true,KeptBirthFrames=true,KeptAges=true,KeptTrajectory=true,LeavesTrail=false,KeptTravel=true,RadialBirths=true;
        TArray<FVector> BirthOffsets;
        for(int32 Frame=0;Frame<150;++Frame)
        {
            TArray<UACEScriptComponent::FActiveParticle> Before;
            int32 BirthCount=0;
            FVector PreviousOrigin=FVector::ZeroVector;
            for(const auto& Emitter:Script->ActiveEmitters) if(Emitter.AuthoredEmitterId==0x32000802)
            { Before=Emitter.Particles; BirthCount=Emitter.TotalBorn; PreviousOrigin=Emitter.LastOrigin; }
            // Run at 4 m/s. Refreshing the held item's network properties and
            // attachment must not teleport particles that already left its tip.
            Body->SetActorLocation(FVector(Frame*400.f/30.f,0,0));
            Script->InitializeFromObject(Baton,100);
            Presenter->TryAttachToParent(Weapon,Wielder.Guid,1);
            for(const auto& Emitter:Script->ActiveEmitters) if(Emitter.AuthoredEmitterId==0x32000802)
            {
                KeptTravel &= Emitter.LastOrigin.Equals(PreviousOrigin,.001);
                for(int32 I=0;I<Before.Num() && I<Emitter.Particles.Num();++I)
                {
                    const auto& P=Emitter.Particles[I]; const auto& Old=Before[I];
                    KeptOffsets &= P.Offset.Equals(Old.Offset,.001);
                    KeptBirthFrames &= P.StartOrigin.Equals(Old.StartOrigin,.001) && P.StartRotation.Equals(Old.StartRotation,.001);
                    KeptAges &= P.Age==Old.Age && P.Life==Old.Life;
                }
            }
            Script->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
            for(const auto& Emitter:Script->ActiveEmitters) if(Emitter.AuthoredEmitterId==0x32000802)
            {
                for(const auto& P:Emitter.Particles)
                {
                    // Retail Particle::Update Explode, with the DAT's A=C=0.
                    const FVector Expected=P.StartOrigin+P.Offset+P.B*P.Age*P.Age;
                    KeptTrajectory &= P.Position.Equals(Expected,.01);
                    LeavesTrail |= Frame>30 && P.Age>.5f &&
                        Script->GetParticleStartFrame(Emitter).GetLocation().X-P.Position.X>150.f;
                }
                if(Emitter.TotalBorn>BirthCount && !Emitter.Particles.IsEmpty())
                {
                    const auto& P=Emitter.Particles.Last();
                    const FVector Offset=P.StartRotation.UnrotateVector(P.Offset)-Script->ResolveHookOffsetLocal(Emitter);
                    RadialBirths &= Offset.Size()>=9.99 && Offset.Size()<=20.01;
                    if(!BirthOffsets.ContainsByPredicate([&](const FVector& Other){return Other.Equals(Offset,.01);})) BirthOffsets.Add(Offset);
                }
            }
        }
        TestTrue(TEXT("Attachment refresh retains each star's random radial offset"),KeptOffsets);
        TestTrue(TEXT("Attachment refresh retains world-space particle birth frames"),KeptBirthFrames);
        TestTrue(TEXT("Attachment refresh preserves the authored remaining lifetime"),KeptAges);
        TestTrue(TEXT("Attachment refresh retains the emitter's distance accumulation origin"),KeptTravel);
        TestTrue(TEXT("Stars are born in the authored 10–20 cm annulus around the tip"),RadialBirths);
        TestTrue(TEXT("Moving baton stars keep retail world-space gravity trajectories"),KeptTrajectory);
        TestTrue(TEXT("Running leaves surviving stars behind the current wand tip"),LeavesTrail);
        TestTrue(TEXT("Network refreshes do not repeat the same star emission offset"),BirthOffsets.Num()>8);
        AddInfo(FString::Printf(TEXT("Moving baton: %d distinct birth offsets over five seconds"),BirthOffsets.Num()));
        TestEqual(TEXT("State refresh does not duplicate the baton emitters"),Script->ActiveEmitters.Num(),2);
        for(auto& Emitter:Script->ActiveEmitters) Emitter.bStopped=true;
        for(int32 Frame=0;Frame<6;++Frame) Script->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
        int32 RemainingStars=0;
        for(const auto& Emitter:Script->ActiveEmitters) if(Emitter.AuthoredEmitterId==0x32000802) RemainingStars+=Emitter.Particles.Num();
        TestTrue(TEXT("Stopping emission lets existing stars finish dripping"),RemainingStars>0);
        for(int32 Frame=0;Frame<30;++Frame) Script->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
        TestEqual(TEXT("Stars expire at their DAT lifetime rather than persisting indefinitely"),Script->ActiveEmitters.Num(),0);
        Script->StopAllEffects(); Body->ScriptComponent->StopAllEffects();
        Presenter->Spawned.Reset(); Weapon->Destroy(); Body->Destroy();
    }
    // Real enchantment scripts share numbered emitter slot 1. Each new buff
    // must replace its predecessor even when it names a different emitter DID.
    FX->StopAllEffects();
    FX->PlayScriptId(0x330000D8,1.f);
    uint32 OldEmitter=0;
    TWeakObjectPtr<UProceduralMeshComponent> OldMesh;
    for(auto& Emitter:FX->ActiveEmitters) if(Emitter.InstanceId==1)
    {
        OldEmitter=Emitter.AuthoredEmitterId; FX->SpawnParticle(Emitter);
        if(!Emitter.Particles.IsEmpty()) OldMesh=Emitter.Particles.Last().Mesh;
    }
    TestTrue(TEXT("First buff has its authored numbered emitter"),OldEmitter!=0);
    FX->PlayScriptId(0x330000D5,1.f);
    int32 SlotCount=0;
    for(const auto& Emitter:FX->ActiveEmitters) if(Emitter.InstanceId==1)
    {
        ++SlotCount;
        TestTrue(TEXT("New buff replaces the old emitter asset"),Emitter.AuthoredEmitterId!=OldEmitter);
        TestFalse(TEXT("Replacement emits normally instead of leaving stopped copies"),Emitter.bStopped);
    }
    TestEqual(TEXT("Only one numbered buff emitter survives"),SlotCount,1);
    bool Reused=false;
    for(const auto& Emitter:FX->ActiveEmitters) for(const auto& Particle:Emitter.Particles)
        Reused |= Particle.Mesh==OldMesh;
    TestTrue(TEXT("Replaced geometry is hidden or recycled into a current particle"),!OldMesh.IsValid() || !OldMesh->IsVisible() || Reused);
    // Zero-ID hooks are independent, including repeated identical ambience.
    Hook.Id=0x320005A8; Hook.SecondaryId=0;
    const int32 BeforeAnonymous=FX->ActiveEmitters.Num();
    FX->CreateEmitter(Hook,false,1.f); FX->CreateEmitter(Hook,false,1.f);
    TestEqual(TEXT("Anonymous creates allocate distinct emitters"),FX->ActiveEmitters.Num(),BeforeAnonymous+2);
    FX->StopEmitter(0,true);
    TestEqual(TEXT("Stop slot zero does not destroy anonymous emitters"),FX->ActiveEmitters.Num(),BeforeAnonymous+2);
    Hook.SecondaryId=1;
    const int32 BeforeBlocking=FX->ActiveEmitters.Num();
    FX->CreateEmitter(Hook,true,1.f);
    TestEqual(TEXT("Blocking create does not replace an occupied slot"),FX->ActiveEmitters.Num(),BeforeBlocking);
    // ScriptManager runs complete scripts in arrival order. A hitch crossing
    // the first script's end must execute its last hook before the next one's first.
    FX->StopAllEffects();
    auto Entry=[](double Time, float Rate)
    {
        FACEDatPhysicsScriptEntry E; E.StartTime=Time;
        E.Hook.Type=EACEAnimationHookType::SetOmega;
        E.Hook.Direction=EACEAnimationHookDirection::Forward;
        E.Hook.Vector=FVector3f(0,0,Rate); return E;
    };
    UACEScriptComponent::FActiveScript First;
    First.Entries={Entry(1,1),Entry(2,2)};
    UACEScriptComponent::FActiveScript Second;
    Second.StartTime=2; Second.Entries={Entry(0,3),Entry(1,4)};
    FX->ActiveScripts={First,Second};
    FX->TickScripts(1.f);
    TestEqual(TEXT("First script advances without overlapping next buff"),FX->Omega.Z,30.0);
    FX->TickScripts(1.1f);
    TestEqual(TEXT("Tail first hook follows head last hook across a hitch"),FX->Omega.Z,90.0);
    FX->TickScripts(1.f);
    TestEqual(TEXT("Second script reaches its final hook"),FX->Omega.Z,120.0);
    TestEqual(TEXT("Finished FIFO drains"),FX->ActiveScripts.Num(),0);
    FX->StopAllEffects();
    Object.bIsPlayer=true; Object.PhysicsEffectTableId=0x34000004;
    FX->InitializeFromObject(Object,100); FX->NotifyAppearanceReady();
    FX->SetPhysicsHidden(true);
    TestEqual(TEXT("Retail Hidden starts fourteen body bubble emitters"),FX->ActiveEmitters.Num(),14);
    TestEqual(TEXT("Hidden sets full body translucency"),FX->ObjectTranslucency,1.f);
    for (int32 Frame=0; Frame<12; ++Frame) FX->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
    int32 BubbleCount=0; bool VisibleBubble=false;
    for (const auto& Bubble : FX->ActiveEmitters)
    {
        BubbleCount+=Bubble.Particles.Num();
        if (Bubble.Batch)
        {
            Bubble.Batch->FlushParticles();
            for (int32 S=0; S<Bubble.Batch->GetNumSections(); ++S)
                for (const auto& V: Bubble.Batch->GetProcMeshSection(S)->ProcVertexBuffer) VisibleBubble |= V.Color.A > 2;
        }
        for (const auto& Particle : Bubble.Particles)
            if (const auto* Mesh=Particle.Mesh.Get())
                if (auto* MID=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
                    VisibleBubble|=Mesh->IsVisible() && MID->K2_GetScalarParameterValue(TEXT("OpacityMul"))>.01f;
    }
    TestTrue(TEXT("Hidden state produces live bubbles before the camera cut"),BubbleCount>0);
    TestTrue(TEXT("Hidden body opacity does not hide its particles"),VisibleBubble);
    FX->SetPhysicsHidden(false);
    TestTrue(TEXT("UnHide preserves existing bubble lifetimes"),!FX->ActiveEmitters.IsEmpty());
    for (const auto& Bubble : FX->ActiveEmitters)
        TestTrue(TEXT("UnHide stops each authored emitter slot"),Bubble.bStopped);
    for (int32 Frame=0; Frame<60; ++Frame) FX->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("UnHide completes the authored body fade"),FX->ObjectTranslucency,0.f);
    for (int32 Frame=0; Frame<600; ++Frame) FX->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Portal bubbles drain and disappear after arrival"),FX->ActiveEmitters.Num(),0);
    FX->StopAllEffects(); Owner->Destroy();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEInteractionEffectsTest, "ACE.Interaction.SpellAndBloodEffects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEInteractionEffectsTest::RunTest(const FString&)
{
    FInteriorTestWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* View=Fixture.World->SpawnActor<ACameraActor>();View->SetActorLocation(FVector(-300,0,100));
    View->GetCameraComponent()->SetFieldOfView(70);
    auto* PC=Fixture.World->SpawnActor<APlayerController>();Fixture.World->AddController(PC);
    if(!PC->PlayerCameraManager){PC->PlayerCameraManager=Fixture.World->SpawnActor<APlayerCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);}
    PC->PlayerCameraManager->bUseClientSideCameraUpdates=false;PC->SetViewTarget(View);PC->PlayerCameraManager->UpdateCamera(0);
    auto* RT=NewObject<UTextureRenderTarget2D>(View);RT->InitCustomFormat(256,256,PF_FloatRGBA,false);
    auto* Capture=NewObject<USceneCaptureComponent2D>(View);Capture->TextureTarget=RT;Capture->FOVAngle=70;
    Capture->bCaptureEveryFrame=Capture->bCaptureOnMovement=false;Capture->CaptureSource=SCS_SceneColorHDRNoAlpha;
    Capture->ShowFlags.SetLighting(false);Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetBloom(false);Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->RegisterComponent();Capture->SetWorldLocation(FVector(-300,0,100));
    auto VisiblePixels=[&](AActor* Subject,const FString& Name,UPrimitiveComponent* Extra=nullptr) {
        Capture->ClearShowOnlyComponents();Capture->ShowOnlyActorComponents(Subject,true);
        // The comfort overlay must cover the whole scene, including actors
        // outside a filtered particle test's show-only list.
        if(Extra) {Capture->ClearShowOnlyComponents();Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;}
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        Fixture.World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
        Capture->CaptureScene();FlushRenderingCommands();
        TArray<FLinearColor> Pixels;RT->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
        int32 Count=0;float Max=0;for(const auto& Pixel:Pixels){const float RGB=FMath::Max3(Pixel.R,Pixel.G,Pixel.B);Max=FMath::Max(Max,RGB);if(RGB>.015f)++Count;}
        AddInfo(FString::Printf(TEXT("%s rendered %d lit pixels, maximum=%.5f"),*Name,Count,Max));return Count;
    };
    // These real assets exercise setup meshes and default emitters; particle
    // counts alone cannot prove the mobile renderer drew the spell.
    for(uint32 Setup:{0x02000885u,0x02000986u})
    {
        FACEWorldObject Spell;Spell.Guid=int32(Setup);Spell.SetupId=Setup;
        Spell.PhysicsState=ACEPhysicsState::Missile|ACEPhysicsState::AlignPath|ACEPhysicsState::HasDefaultScript;
        Spell.PhysicsEffectTableId=Setup==0x02000986u?0x34000008:0;
        auto* Projectile=Fixture.World->SpawnActor<AACEWorldEntityActor>();Projectile->InitializeFromObject(Spell,100,true);
        Projectile->SetActorLocation(FVector(0,0,100));
        auto* Script=Projectile->ScriptComponent.Get();Script->NotifyAppearanceReady();
        int32 PeakPixels=0;
        for(int I=0;I<80;++I)
        {
            Projectile->AddActorWorldOffset(FVector(0,1,0));
            Script->TickScripts(.025f);Script->TickEmitters(.025f);
            if(I==2 || I==10 || I==30 || I==60) PeakPixels=FMath::Max(PeakPixels,VisiblePixels(Projectile,FString::Printf(TEXT("Ring %08X"),Setup)));
        }
        AddInfo(FString::Printf(TEXT("Ring setup=%08X default=%08X table=%08X appearance=%d"),Setup,Script->SetupDefaultScript,Script->SetupScriptTableId,Projectile->Appearance->HasAppearance()));
        for(const auto& E:Script->ActiveEmitters)AddInfo(FString::Printf(TEXT("Ring emitter=%08X kind=%d gfx=%08X particles=%d degraded=%d"),E.AuthoredEmitterId,E.Info.EmitterType,E.Info.GfxObjId,E.Particles.Num(),E.bDegraded));
        TestTrue(TEXT("Actual Tectonic Rifts and Rabbit spell assets render"),PeakPixels>10);
        Projectile->Destroy();
    }
    FACEWorldObject Object; Object.Guid=100;Object.SetupId=0x02000001;Object.PhysicsEffectTableId=0x34000004;
    Object.bIsPlayer=true; Object.ItemType=ACEItemType::Creature;
    auto* Owner=Fixture.World->SpawnActor<AACEWorldEntityActor>();Owner->InitializeFromObject(Object,100,true);
    auto* FX=Owner->ScriptComponent.Get(); FX->NotifyAppearanceReady(); FX->StopAllEffects();
    {
        auto* Audio=NewObject<UAudioComponent>(Fixture.World);Audio->RegisterComponentWithWorld(Fixture.World);
        auto& Pending=FX->ActiveSounds.AddDefaulted_GetRef();Pending.Component=Audio;
        Pending.StartSeconds=FPlatformTime::Seconds()-1.;Pending.EndSeconds=FPlatformTime::Seconds()+4.;
        FX->CleanupFinishedAudio();
        TestEqual(TEXT("Procedural audio startup silence cannot truncate a known-duration sound"),FX->ActiveSounds.Num(),1);
        FX->ActiveSounds[0].EndSeconds=FPlatformTime::Seconds()-1.;FX->CleanupFinishedAudio();
        TestTrue(TEXT("Known-duration sound releases its component at completion"),FX->ActiveSounds.IsEmpty() && !Audio->IsRegistered());
    }
    auto* VR=NewObject<UACEVRComponent>(Owner);Owner->AddInstanceComponent(VR);VR->RegisterComponent();
    VR->Settings=NewObject<UACEVRSettings>();
    for (bool InVR : {false,true})
    {
        VR->bActive=InVR;
        for (uint32 Type : {0x1Fu,0x4Cu,0x4Bu,0x51u,0x5Bu})
        {
            FX->StopAllEffects(); FX->PlayEffect(Type,1.f);
            int32 Peak=0;
            for (int32 Frame=0;Frame<80;++Frame)
            {
                FX->TickScripts(.025f);FX->TickEmitters(.025f);
                int32 Count=0;for(const auto& E:FX->ActiveEmitters) Count+=E.Particles.Num(); Peak=FMath::Max(Peak,Count);
                if (InVR && Type!=0x5B)
                    for (const auto& E:FX->ActiveEmitters)
                        TestTrue(TEXT("Self-cast feedback follows the dominant hand"),E.bVRHandFeedback && E.Parent.Get()==VR->GetHeldAnchor(1));
            }
            TestTrue(FString::Printf(TEXT("%s effect %02X produces visible particles (peak %d)"),InVR?TEXT("VR"):TEXT("Desktop"),Type,Peak),Peak>0);
        }
    }
    FX->StopAllEffects();VR->bActive=false;
    // A queued, long running victim effect must not delay a lethal-hit burst.
    FX->PlayEffect(0x4C,1.f);
    auto* Impact=FX->PlayImpactEffect(0x5B,1.f);
    if(TestNotNull(TEXT("Blood owns an independent effect component"),Impact))
    {
        TestNotEqual(TEXT("Blood is not owned by the defeated actor"),Impact->GetOwner(),static_cast<AActor*>(Owner));
        const auto Frames=Impact->ImpactFrames;
        Owner->AddActorWorldOffset(FVector(2000,0,0));Owner->Destroy();
        int32 Peak=0, VisibleBlood=0;
        for(int32 Frame=0;Frame<100;++Frame)
        {
            Impact->TickScripts(.025f);Impact->TickEmitters(.025f);
            int32 Count=0;for(const auto& E:Impact->ActiveEmitters) Count+=E.Particles.Num();Peak=FMath::Max(Peak,Count);
            if(Frame==5 || Frame==10 || Frame==20) VisibleBlood=FMath::Max(VisibleBlood,VisiblePixels(Impact->GetOwner(),TEXT("Lethal blood")));
            if(Frame==5)for(const auto& E:Impact->ActiveEmitters)if(!E.Particles.IsEmpty())
                AddInfo(FString::Printf(TEXT("Blood gfx=%08X batch=%d count=%d p=%s scale=%.3f alpha=%.3f start=%s bounds=%s hidden=%d"),E.Info.GfxObjId,E.bInstanced,E.Particles.Num(),*E.Particles[0].Position.ToString(),E.Particles[0].StartScale,E.Particles[0].StartTrans,*E.Particles[0].StartOrigin.ToString(),E.Batch?*E.Batch->Bounds.ToString():TEXT("none"),Impact->GetOwner()->IsHidden()));
        }
        TestTrue(TEXT("Lethal blood remains visible after victim removal"),Peak>0);
        TestTrue(TEXT("Lethal blood is drawn by the renderer after victim removal"),VisibleBlood>10);
        TestTrue(TEXT("Blood retains the original hit position"),Impact->ImpactFrames[-1].Equals(Frames[-1]));
        TestFalse(TEXT("Blood host cannot collide with a VR player"),Impact->GetOwner()->GetActorEnableCollision());
        Impact->GetOwner()->Destroy();
    }
    else Owner->Destroy();
    // A death one-shot outlives the creature, but never the world or its PCM
    // deadline. WorldSettings owns it so even no-audio test worlds clean up.
    auto* Dying=Fixture.World->SpawnActor<AACEWorldEntityActor>();Dying->InitializeFromObject(Object,100,false);
    auto* DeathFX=Dying->ScriptComponent.Get();DeathFX->StopAllEffects();DeathFX->StopAllSounds();
    auto* AudioOwner=Fixture.World->GetWorldSettings();
    auto* Tail=NewObject<UAudioComponent>(AudioOwner);AudioOwner->AddInstanceComponent(Tail);
    Tail->RegisterComponentWithWorld(Fixture.World);
    auto& DeathSound=DeathFX->ActiveSounds.AddDefaulted_GetRef();DeathSound.Component=Tail;
    DeathSound.EndSeconds=FPlatformTime::Seconds()+1.;
    Dying->bReceivedDeathMotion=true;
    DeathFX->BeginPlay();DeathFX->EndPlay(EEndPlayReason::Destroyed);Dying->Destroy();
    TestTrue(TEXT("Death sound survives its creature's removal"),Tail->IsRegistered() && DeathFX->ActiveSounds.IsEmpty());
    Fixture.World->GetTimerManager().Tick(0.f);
    ++GFrameCounter;Fixture.World->GetTimerManager().Tick(2.f);
    TestFalse(TEXT("Detached death sound releases at its bounded deadline"),Tail->IsRegistered());
    // Render the production comfort geometry in this independent scene. The
    // UI rig fixture repeatedly changes view/portal ownership during its test.
    {
    auto* VRPawn=Fixture.World->SpawnActor<APawn>();
    auto* CoverRig=NewObject<UACEVRComponent>(VRPawn);VRPawn->AddInstanceComponent(CoverRig);CoverRig->RegisterComponent();
    CoverRig->PC=Fixture.World->SpawnActor<AACEPlayerController>();
    CoverRig->Head=NewObject<UCameraComponent>(VRPawn);VRPawn->AddInstanceComponent(CoverRig->Head);VRPawn->SetRootComponent(CoverRig->Head);CoverRig->Head->RegisterComponent();
    CoverRig->Head->SetWorldTransform(View->GetActorTransform());CoverRig->InitializeComfortCurtain();
    if(TestNotNull(TEXT("Comfort overlay exists"),CoverRig->ComfortCurtain.Get()))
    {
        FACEWorldObject Reference;Reference.Guid=992201;Reference.SetupId=0x02000986;
        auto* Behind=Fixture.World->SpawnActor<AACEWorldEntityActor>();Behind->InitializeFromObject(Reference,100,true);
        Behind->SetActorLocation(FVector(0,0,100));Behind->AttachToActor(VRPawn,FAttachmentTransformRules::KeepWorldTransform);
        Capture->ShowFlags.SetLighting(true);
        // Activation primes the resources before gameplay and fades in once
        // tracking is valid. Exercise that startup before testing occlusion.
        VisiblePixels(Behind,TEXT("Comfort startup"),CoverRig->ComfortCurtain);
        TestEqual(TEXT("Startup cover is opaque"),VisiblePixels(Behind,TEXT("Comfort startup ready"),CoverRig->ComfortCurtain),0);
        CoverRig->bTracking=true;CoverRig->UpdateComfort(1.f);
        TestTrue(TEXT("Uncovered scene is visible"),VisiblePixels(Behind,TEXT("Comfort uncovered"),CoverRig->ComfortCurtain)>10);
        CoverRig->bTracking=false;CoverRig->UpdateComfort(1.f);
        TestEqual(TEXT("Mobile LDR comfort cover hides penetrated geometry"),VisiblePixels(Behind,TEXT("Comfort covered"),CoverRig->ComfortCurtain),0);
        CoverRig->bTracking=true;CoverRig->UpdateComfort(1.f);
        TestTrue(TEXT("World returns after occlusion clears"),VisiblePixels(Behind,TEXT("Comfort restored"),CoverRig->ComfortCurtain)>10);
        CoverRig->bTracking=false;CoverRig->UpdateComfort(1.f);
        TestEqual(TEXT("Reused comfort cover still obscures the scene"),VisiblePixels(Behind,TEXT("Comfort covered again"),CoverRig->ComfortCurtain),0);
    }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDoorwayGeometryCacheTest, "ACE.Rendering.DoorwayGeometryCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEDoorwayGeometryCacheTest::RunTest(const FString& Parameters)
{
    FInteriorTestWorld Fixture;
    auto* Dat = Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail data loads"), Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    auto* Cache = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Render.CacheDoorwayGeometry"));
    const int32 Previous = Cache->GetInt();
    ON_SCOPE_EXIT { Cache->Set(Previous, ECVF_SetByCode); };
    using FAperture = ACEOutdoorPortalPlan::FAdmittedAperture;
    auto Same = [&](const TArray<FAperture>& A, const TArray<FAperture>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 I=0; I<A.Num(); ++I)
            if (A[I].DestEnvCellId != B[I].DestEnvCellId || A[I].OtherPortalId != B[I].OtherPortalId
                || A[I].WorldNormal != B[I].WorldNormal || A[I].WorldVerts != B[I].WorldVerts) return false;
        return true;
    };
    Cache->Set(1, ECVF_SetByCode);
    TArray<FAperture> Partial;
    Dat->LoadBuildingDoorwayApertures(0x7D640000, 100, Partial);
    TestEqual(TEXT("Unstreamed building geometry is never cached as a sealed doorway"), Dat->DoorwayGeometryCache.Num(), 0);
    int32 DoorCount = 0;
    for (uint32 Key : {0x7D640000u, 0x7D630000u, 0xC88C0000u, 0x09050000u})
    {
        FACEDatLandblockInfo Info;
        if (!Dat->LoadLandblockInfo(Key, Info)) continue;
        for (float Scale : {100.f, 50.f})
        {
            for (const auto& Building : Info.Buildings) Dat->GetOrBuildSetupMesh(Building.ModelId, Scale);
            TArray<FAperture> Reference, First, Hit;
            Cache->Set(0, ECVF_SetByCode); Dat->LoadBuildingDoorwayApertures(Key, Scale, Reference);
            Cache->Set(1, ECVF_SetByCode); Dat->LoadBuildingDoorwayApertures(Key, Scale, First);
            Dat->LoadBuildingDoorwayApertures(Key, Scale, Hit);
            TestTrue(TEXT("Cold/hit geometry preserves every portal vertex, side and destination"), Same(Reference, First) && Same(Reference, Hit));
            DoorCount += Hit.Num();
            if (!Hit.IsEmpty()) Hit[0].WorldVerts.Reset(); // Caller clips may not modify cached originals.
            Dat->LoadBuildingDoorwayApertures(Key, Scale, Hit);
            TestTrue(TEXT("Clipping a returned aperture cannot mutate the cached geometry"), Same(Reference, Hit));
            const auto Shared=Dat->GetBuildingDoorwayApertures(Key,Scale);
            TestTrue(TEXT("Traversal obtains immutable geometry identical to the copy API"),Shared && Same(Reference,*Shared));
            TestTrue(TEXT("Cache hits reuse the aperture allocation"),Shared==Dat->GetBuildingDoorwayApertures(Key,Scale));
        }
    }
    TestTrue(TEXT("Regression exercises actual doorways"), DoorCount>0);
    const uint32 Key=0x7D640000;
    TArray<FAperture> Doors;
    Dat->LoadBuildingDoorwayApertures(Key,100,Doors);
    if (!Doors.IsEmpty())
    {
        Dat->GetOrBuildEnvCellMesh(Doors[0].DestEnvCellId,100);
        FVector Center=FVector::ZeroVector;
        for (auto V : Doors[0].WorldVerts) Center+=V;
        Center/=Doors[0].WorldVerts.Num();
        for (float Side : {-1.f,1.f})
        {
            TArray<uint32> Keys{Key}; TSet<int32> Reference, Hit;
            const FVector Eye=Center+Doors[0].WorldNormal*Side*150;
            Cache->Set(0,ECVF_SetByCode);
            ACEOutdoorPortalPlan::CollectOutdoorAdmittedEnvCells(*Dat,Eye,{},100,Keys,Reference);
            Cache->Set(1,ECVF_SetByCode);
            ACEOutdoorPortalPlan::CollectOutdoorAdmittedEnvCells(*Dat,Eye,{},100,Keys,Hit);
            TestTrue(TEXT("Moving across a doorway still recomputes PView admission"),Reference.Includes(Hit) && Hit.Includes(Reference));
        }
    }
    // Paired microbenchmark excludes DAT loading; no hardware-dependent speed assertion.
    for (int32 Enabled : {0,1,0,1})
    {
        Cache->Set(Enabled,ECVF_SetByCode);
        const double Start=FPlatformTime::Seconds();
        for (int32 I=0; I<4000; ++I) Dat->LoadBuildingDoorwayApertures(Key,100,Doors);
        AddInfo(FString::Printf(TEXT("Doorway geometry cached=%d: %.3f us/call (%d doors)"),
            Enabled,(FPlatformTime::Seconds()-Start)*1000000/4000,Doors.Num()));
    }
    // Warm all cells before measuring the complete per-frame traversal, not
    // asynchronous data preparation. Keep camera admission active each call.
    Cache->Set(1,ECVF_SetByCode);
    FACEDatLandblockInfo BenchInfo; Dat->LoadLandblockInfo(Key,BenchInfo);
    for (uint32 I=0; I<BenchInfo.NumCells; ++I) Dat->GetOrBuildEnvCellMesh(Key|(0x100+I),100);
    Dat->LoadBuildingDoorwayApertures(Key,100,Doors);
    if (!Doors.IsEmpty())
    {
        const FVector Eye=Doors[0].WorldVerts[0]+Doors[0].WorldNormal*200;
        TArray<uint32> Keys{Key}; TSet<int32> Visible;
        TArray<FAperture> Entrances, Exits;
        for (int32 Phase=0; Phase<4; ++Phase)
        {
            const double Start=FPlatformTime::Seconds();
            for (int32 I=0; I<1000; ++I)
                ACEOutdoorPortalPlan::CollectOutdoorAdmittedEnvCells(*Dat,Eye,{},100,Keys,Visible,&Entrances,&Exits);
            AddInfo(FString::Printf(TEXT("PView warm traversal: %.3f us/call (%d cells, %d entries, %d exits)"),
                (FPlatformTime::Seconds()-Start)*1000,Visible.Num(),Entrances.Num(),Exits.Num()));
        }
    }
    auto HeldGeometry=Dat->GetBuildingDoorwayApertures(Key,100);
    TWeakPtr<const TArray<FAperture>> WeakGeometry=HeldGeometry;
    Dat->ClearLoadedState();
    TestEqual(TEXT("DAT reload releases the complete geometry cache"),Dat->DoorwayGeometryCache.Num(),0);
    TestTrue(TEXT("An active traversal remains valid across cache eviction/reload"),HeldGeometry && Same(*HeldGeometry,Doors));
    HeldGeometry.Reset();
    TestFalse(TEXT("Evicted geometry releases after the active traversal ends"),WeakGeometry.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPortalClipScratchTest,"ACE.RetailParity.PortalClipScratch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEPortalClipScratchTest::RunTest(const FString&)
{
    // Preserve the old clipper as an independent buffer-lifetime reference:
    // plane math/order and partial clipping must not change with scratch reuse.
    auto Reference=[](const TArray<FVector>& Verts,const FConvexVolume& Frustum,TArray<FVector>& Out)
    {
        TArray<FVector> Current=Verts,Next;
        for(const FPlane& P:Frustum.Planes)
        {
            if(!ACEOutdoorPortalPlan::ClipPolygonAgainstPlane(Current,FVector(-P.X,-P.Y,-P.Z),P.W,Next))
            {Out.Reset();return false;}
            Current=MoveTemp(Next);Next.Reset();
        }
        Out=MoveTemp(Current);return Out.Num()>=3;
    };
    const TArray<FVector> Polygon={{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0}};
    FConvexVolume Frustum;
    for(const FVector& Normal:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),
        FVector(1,1,0).GetSafeNormal(),FVector(-1,-1,0).GetSafeNormal()})
        Frustum.Planes.Add(FPlane(Normal*75,Normal));
    Frustum.Init();
    for(int32 Offset=0;Offset<300;Offset+=17)
    {
        TArray<FVector> Input=Polygon,A,B;
        for(auto& V:Input)V+=FVector(Offset,Offset*.3,0);
        const bool Expected=Reference(Input,Frustum,A);
        TestEqual(TEXT("Scratch reuse preserves visible/rejected apertures"),ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(Input,Frustum,B),Expected);
        TestTrue(TEXT("Scratch reuse preserves every clipped vertex and order"),A==B);
        TestEqual(TEXT("In-place caller remains supported"),ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(Input,Frustum,Input),Expected);
        TestTrue(TEXT("In-place output matches reference"),Input==A);
    }
    TArray<FVector> Output;
    for(int32 Phase=0;Phase<4;++Phase)
    {
        const double Start=FPlatformTime::Seconds();
        for(int32 I=0;I<20000;++I)
            if(Phase%2) ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(Polygon,Frustum,Output);
            else Reference(Polygon,Frustum,Output);
        AddInfo(FString::Printf(TEXT("Portal clip scratch reuse=%d: %.3f us/call"),Phase%2,
            (FPlatformTime::Seconds()-Start)*1000000/20000));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESetupMetadataCacheTest, "ACE.Rendering.SetupMetadataCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACESetupMetadataCacheTest::RunTest(const FString& Parameters)
{
    FInteriorTestWorld Fixture;
    auto* Dat=Fixture.GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail data loads"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    auto* Cache=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Dat.CacheSetupMetadata"));
    const int32 Previous=Cache->GetInt(); ON_SCOPE_EXIT { Cache->Set(Previous,ECVF_SetByCode); };
    FACEDatDatabase Portal;
    if (!Portal.Open(TEXT("C:/Turbine/Asheron's Call/client_portal.dat"))) return false;
    for (uint32 Id : {0x02000001u,0x0200004Eu,0x02000306u})
    {
        TArray<uint8> Blob; FACEDatSetupModel Setup;
        if (!TestTrue(TEXT("Reference Setup file exists"),Portal.ReadFile(Id,Blob))) continue;
        FACEDatCursor Cursor(Blob);
        if (!TestTrue(TEXT("Reference Setup unpacks"),ACEDatUnpack::UnpackSetupModel(Cursor,Setup))) continue;
        for (int32 Enabled : {0,1,1})
        {
            Cache->Set(Enabled,ECVF_SetByCode);
            float Up,Down,H,R,SR; uint32 Animation,Script,Table,Sound; FVector3f Origin;
            TestTrue(TEXT("Physics metadata resolves"),Dat->TryGetSetupPhysics(Id,Up,H,R,Animation,&Down,&Origin,&SR));
            TestEqual(TEXT("Authored step up"),Up,Setup.StepUpHeight>0?Setup.StepUpHeight:.5f);
            TestEqual(TEXT("Authored step down"),Down,Setup.StepDownHeight>0?Setup.StepDownHeight:.5f);
            TestEqual(TEXT("Authored height"),H,Setup.Height); TestEqual(TEXT("Authored radius"),R,Setup.Radius);
            TestEqual(TEXT("Authored default animation"),Animation,Setup.DefaultAnimation);
            TestTrue(TEXT("Selection sphere remains model-specific"),Origin==Setup.SelectionSphereOrigin && SR==Setup.SelectionSphereRadius);
            TestTrue(TEXT("Effect metadata resolves"),Dat->TryGetSetupRuntimeMetadata(Id,Script,Table,Sound));
            TestTrue(TEXT("Scripts, tables and sounds retain authored IDs"),Script==Setup.DefaultScript && Table==Setup.DefaultScriptTable && Sound==Setup.DefaultSoundTable);
            TArray<FACEDatCollisionShape> Shapes; bool BSP;
            TestTrue(TEXT("Collision metadata resolves"),Dat->GetSetupCollisionShapes(Id,Shapes,BSP));
            const auto& Expected=Setup.CylSpheres.IsEmpty()?Setup.Spheres:Setup.CylSpheres;
            TestEqual(TEXT("Collision shape count"),Shapes.Num(),Expected.Num());
            TestEqual(TEXT("Physics BSP flag"),BSP,EnumHasAnyFlags(Setup.Flags,EACESetupFlags::HasPhysicsBSP));
            for(int32 I=0;I<FMath::Min(Shapes.Num(),Expected.Num());++I)
                TestTrue(TEXT("Collision origins and dimensions match DAT"),Shapes[I].Origin==Expected[I].Origin && Shapes[I].Radius==Expected[I].Radius && Shapes[I].Height==Expected[I].Height);
        }
    }
    for (int32 Enabled : {0,1,0,1})
    {
        Cache->Set(Enabled,ECVF_SetByCode); float Up,H,R; uint32 Animation;
        const double Start=FPlatformTime::Seconds();
        for(int32 I=0;I<10000;++I) Dat->TryGetSetupPhysics(0x02000001,Up,H,R,Animation);
        AddInfo(FString::Printf(TEXT("Setup metadata cached=%d: %.3f us/query"),Enabled,(FPlatformTime::Seconds()-Start)*1000000/10000));
    }
    Cache->Set(1,ECVF_SetByCode);
    for (uint32 Id=0x02FF0000;Id<0x02FF0300;++Id)
    {
        float Up=999,H=999,R=999;uint32 Animation=999;
        TestFalse(TEXT("Unknown setup fails safely"),Dat->TryGetSetupPhysics(Id,Up,H,R,Animation));
        TestTrue(TEXT("Unknown setup restores defaults"),Up==.5f && H==2.f && R==.5f && Animation==0);
    }
    TestEqual(TEXT("Travel and unknown models cannot grow metadata cache without bound"),Dat->SetupRuntimeMetadataCache.Num(),512);
    Dat->ClearLoadedState();
    TestEqual(TEXT("DAT reload invalidates Setup metadata"),Dat->SetupRuntimeMetadataCache.Num(),0);
    return true;
}
#endif
