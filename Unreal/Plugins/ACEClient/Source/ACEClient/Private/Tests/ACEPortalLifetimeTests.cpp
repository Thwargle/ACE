#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "ACEScriptComponent.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Components/PrimitiveComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPortalLifetimeTest, "ACE.Rendering.PortalLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEPortalLifetimeTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI = NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(GI); Context.OwningGameInstance = GI; Context.SetCurrentWorld(World);
    GI->OnWorldChanged(nullptr, World); GI->Init(); World->InitializeActorsForPlay(FURL());
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    const bool Loaded = Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"));
    TestTrue(TEXT("Retail DAT loads"), Loaded);
    if (Loaded)
    {
        auto* Client = GI->GetSubsystem<UACEClientSubsystem>();
        auto Session = Client->GetSession();
        Session->PlayerGuid = 0x50000001;
        Session->State = EACESessionState::InWorld;
        auto* Owner = World->SpawnActor<AActor>();
        auto* Presenter = NewObject<UACEWorldPresenterComponent>(Owner);
        Owner->AddInstanceComponent(Presenter); Presenter->RegisterComponent(); Owner->DispatchBeginPlay();

        FACEWorldObject Portal;
        Portal.Guid = int32(0x80000123u); Portal.Name = TEXT("Portal Gateway"); Portal.WeenieClassId = 1955;
        Portal.ItemType = ACEItemType::Portal; Portal.SetupId = 0x020001B3;
        Portal.bHasPosition = true; Portal.Position.CellId = 0x7D640014;
        Portal.PhysicsState = ACEPhysicsState::HasDefaultScript;
        Session->WorldObjects.Add(Portal.Guid, Portal);
        Presenter->SpawnOrUpdateEntity(Portal, true);
        auto* Actor = Presenter->FindEntityActor(Portal.Guid);
        if (TestNotNull(TEXT("Summoned portal spawns"), Actor))
        {
            Actor->DispatchBeginPlay();
            auto* Script = Actor->ScriptComponent.Get();
            for (int32 Frame = 0; Frame < 60; ++Frame)
            {
                Script->TickScripts(1.f / 60.f); Script->TickEmitters(1.f / 60.f);
            }
            TestTrue(TEXT("Retail portal creates emitters"), !Script->ActiveEmitters.IsEmpty());
            TArray<UPrimitiveComponent*> Primitives;
            Actor->GetComponents(Primitives);
            AddInfo(FString::Printf(TEXT("Portal emitters=%d primitives=%d"), Script->ActiveEmitters.Num(), Primitives.Num()));

            FACEBinaryWriter Delete;
            Delete.WriteUInt32(uint32(Portal.Guid)); Delete.WriteUInt16(1); Delete.WriteUInt16(0);
            FACEBinaryReader Reader(Delete.GetData()); Session->HandleObjectDelete(Reader);
            TestFalse(TEXT("Server expiration removes the session object"), Session->WorldObjects.Contains(Portal.Guid));
            TestNull(TEXT("Server expiration removes the presented actor"), Presenter->FindEntityActor(Portal.Guid));
            TestTrue(TEXT("Server expiration destroys the actor"), Actor->IsActorBeingDestroyed());
            TestTrue(TEXT("Server expiration stops all portal emitters"), Script->ActiveEmitters.IsEmpty());
            for (const auto* Primitive : Primitives)
                TestFalse(TEXT("Expired portal leaves no registered visual"), Primitive->IsRegistered());
        }

        // The server forgets observers after 25s outside PVS and sends no delete
        // for subsequent portal decay. Simulate leaving, decay, and a late return.
        auto InPVS = [](int32) { return true; };
        auto OutsidePVS = [](int32) { return false; };
        Session->UpsertWorldObject(Portal);
        Presenter->SpawnOrUpdateEntity(Portal, true);
        auto* LostPortal = Presenter->FindEntityActor(Portal.Guid);
        if (LostPortal) LostPortal->DispatchBeginPlay();
        Session->MaintainWorldObjectVisibility(100., InPVS);
        Session->MaintainWorldObjectVisibility(101., OutsidePVS);
        Session->MaintainWorldObjectVisibility(125.99, OutsidePVS);
        TestTrue(TEXT("Lost portal remains known during retail grace period"), Session->WorldObjects.Contains(Portal.Guid));
        Session->MaintainWorldObjectVisibility(127., InPVS);
        TestFalse(TEXT("Returning after grace cannot resurrect an expired portal"), Session->WorldObjects.Contains(Portal.Guid));
        TestNull(TEXT("Lost portal visual and pending spawn are retired"), Presenter->FindEntityActor(Portal.Guid));
        TestTrue(TEXT("Lost portal actor is destroyed"), LostPortal && LostPortal->IsActorBeingDestroyed());
        TestFalse(TEXT("Lost portal cannot rebuild deferred appearance"), Presenter->PendingDatAppearance.Contains(Portal.Guid));
        TestFalse(TEXT("Lost portal cannot replay queued creation"), Presenter->PendingSpawns.ContainsByPredicate(
            [&](const FACEWorldObject& O) { return O.Guid == Portal.Guid; }));
        Presenter->SyncEntitiesToKeepRing({int32(uint32(Portal.Position.CellId) & 0xFFFF0000u)});
        TestFalse(TEXT("Reloading the old terrain cannot recreate the expired portal"), Presenter->PendingSpawns.ContainsByPredicate(
            [&](const FACEWorldObject& O) { return O.Guid == Portal.Guid; }));

        Session->UpsertWorldObject(Portal);
        Session->MaintainWorldObjectVisibility(200., OutsidePVS);
        Session->MaintainWorldObjectVisibility(224., InPVS);
        Session->MaintainWorldObjectVisibility(300., InPVS);
        TestTrue(TEXT("Returning before grace keeps the live portal"), Session->WorldObjects.Contains(Portal.Guid));
        Session->MaintainWorldObjectVisibility(301., OutsidePVS);
        Session->UpsertWorldObject(Portal);
        Session->MaintainWorldObjectVisibility(400., InPVS);
        TestTrue(TEXT("A fresh server description renews object interest"), Session->WorldObjects.Contains(Portal.Guid));
        Session->MaintainWorldObjectVisibility(10000., InPVS);
        TestTrue(TEXT("Stationary live portals have no guessed lifetime"), Session->WorldObjects.Contains(Portal.Guid));

        FACEWorldObject Pack = Portal; Pack.Guid = 10; Pack.ContainerId = Session->PlayerGuid;
        FACEWorldObject Equipped = Portal; Equipped.Guid = 11; Equipped.WielderId = Session->PlayerGuid;
        FACEWorldObject Remote = Portal; Remote.Guid = 12; Remote.ItemType = ACEItemType::Creature;
        FACEWorldObject Weapon = Portal; Weapon.Guid = 13; Weapon.ParentGuid = Remote.Guid; Weapon.bHasPosition = false;
        Session->WorldObjects.Add(Pack.Guid, Pack); Session->WorldObjects.Add(Equipped.Guid, Equipped);
        Session->WorldObjects.Add(Remote.Guid, Remote); Session->WorldObjects.Add(Weapon.Guid, Weapon);
        Session->MaintainWorldObjectVisibility(11000., OutsidePVS);
        Session->MaintainWorldObjectVisibility(11026., OutsidePVS);
        TestTrue(TEXT("Inventory survives travel"), Session->WorldObjects.Contains(Pack.Guid));
        TestTrue(TEXT("Local equipment survives travel"), Session->WorldObjects.Contains(Equipped.Guid));
        TestFalse(TEXT("Remote owner retires after leaving PVS"), Session->WorldObjects.Contains(Remote.Guid));
        TestFalse(TEXT("Remote held visual retires with its owner"), Session->WorldObjects.Contains(Weapon.Guid));

        // Spawned creatures use the same lost-object lifetime as portals. Cover
        // both a retained actor and terrain that has already unloaded its actor:
        // neither may be rebuilt from the stale descriptor after a long absence.
        for (bool UnloadTerrain : {false, true})
        {
            const double Start = UnloadTerrain ? 12200. : 12000.;
            FACEWorldObject Mob;
            Mob.Guid = int32(0x80000200u); Mob.Name = TEXT("Spawned creature");
            Mob.ItemType = ACEItemType::Creature; Mob.ObjectDescriptionFlags = ACEObjectDescFlag::Attackable;
            Mob.SetupId = 0x02000001; Mob.bHasPosition = true; Mob.Position.CellId = 0x7D640014;
            Mob.bHasPhysicsTimestamps = true; Mob.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance] = 1;
            Session->UpsertWorldObject(Mob); Presenter->SpawnOrUpdateEntity(Mob, true);
            auto* MobActor = Presenter->FindEntityActor(Mob.Guid);
            if (!TestNotNull(TEXT("Spawned creature has a world actor"), MobActor)) continue;
            MobActor->DispatchBeginPlay();

            FACEWorldObject Held;
            Held.Guid = int32(0x80000201u); Held.ParentGuid = Held.WielderId = Mob.Guid; Held.ParentLocation = 1;
            Session->UpsertWorldObject(Held); Presenter->SpawnOrUpdateEntity(Held, false);
            auto* HeldActor = Presenter->FindEntityActor(Held.Guid);
            TestNotNull(TEXT("Creature equipment has an owned actor"), HeldActor);
            if (HeldActor) HeldActor->DispatchBeginPlay();
            TArray<UPrimitiveComponent*> MobPrimitives;
            MobActor->GetComponents(MobPrimitives);
            TestTrue(TEXT("Creature fixture has registered geometry"), MobPrimitives.ContainsByPredicate(
                [](const UPrimitiveComponent* P) { return P->IsRegistered(); }));
            Presenter->LatestMotion.Add(Mob.Guid, FACEObjectMotionState());
            Session->SelectObject(Mob.Guid);
            TestEqual(TEXT("Spawned creature can be selected before leaving"), Session->SelectedObject.Guid, Mob.Guid);
            Session->MaintainWorldObjectVisibility(Start, OutsidePVS);
            if (UnloadTerrain) Presenter->SyncEntitiesToKeepRing({int32(0x80640000u)});
            Session->MaintainWorldObjectVisibility(Start + 26., InPVS);
            TestFalse(TEXT("Absent creature is removed without a server delete packet"), Session->WorldObjects.Contains(Mob.Guid));
            TestFalse(TEXT("Absent creature equipment is removed"), Session->WorldObjects.Contains(Held.Guid));
            TestNull(TEXT("Absent creature leaves no world actor"), Presenter->FindEntityActor(Mob.Guid));
            TestNull(TEXT("Absent creature leaves no equipment actor"), Presenter->FindEntityActor(Held.Guid));
            TestTrue(TEXT("Absent creature actor is destroyed"), MobActor->IsActorBeingDestroyed());
            TestTrue(TEXT("Absent creature equipment actor is destroyed"), HeldActor && HeldActor->IsActorBeingDestroyed());
            TestEqual(TEXT("Absent creature loses selection and health targeting"), Session->SelectedObject.Guid, 0);
            TestFalse(TEXT("Absent creature cannot replay its old motion"), Presenter->LatestMotion.Contains(Mob.Guid));
            for (const auto* Primitive : MobPrimitives)
                TestFalse(TEXT("Absent creature leaves no registered geometry or collision"), Primitive->IsRegistered());
            Presenter->SyncEntitiesToKeepRing({int32(0x7D640000u)});
            TestFalse(TEXT("Reloaded terrain cannot queue the stale creature or its equipment"), Presenter->PendingSpawns.ContainsByPredicate(
                [&](const FACEWorldObject& O) { return O.Guid == Mob.Guid || O.Guid == Held.Guid; }));

            ++Mob.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance];
            Session->UpsertWorldObject(Mob); Presenter->SpawnOrUpdateEntity(Mob, true);
            auto* Respawn = Presenter->FindEntityActor(Mob.Guid);
            TestTrue(TEXT("A fresh server spawn creates a new creature actor"), Respawn && Respawn != MobActor);
            TestTrue(TEXT("A fresh server spawn can be attacked normally"), Session->WorldObjects[Mob.Guid].IsAttackable());
            Session->DeleteWorldObject(Mob.Guid);
        }

        // Use real DAT cells to exercise runtime classification, without a camera.
        Session->UpsertWorldObject(Portal);
        Session->PlayerPosition = Portal.Position;
        Client->TickWorldObjectVisibility(1.f);
        TestFalse(TEXT("Outdoor portal in our landblock stays in interest"), Session->ObjectVisibilityDeadlines.Contains(Portal.Guid));
        Session->PlayerPosition.CellId = 0x80640014;
        Client->TickWorldObjectVisibility(1.f);
        TestTrue(TEXT("Distant outdoor landblock starts retail grace"), Session->ObjectVisibilityDeadlines.Contains(Portal.Guid));
        Session->PlayerPosition = Portal.Position;
        Client->TickWorldObjectVisibility(1.f);
        TestFalse(TEXT("Walking back promptly cancels grace"), Session->ObjectVisibilityDeadlines.Contains(Portal.Guid));

        const uint32 DungeonCell = 0x0143014F;
        FACEDatEnvCell Cell;
        if (TestTrue(TEXT("Dungeon visibility data loads"), Dat->LoadEnvCell(DungeonCell, Cell)))
        {
            Session->PlayerPosition.CellId = DungeonCell;
            Portal.Position.CellId = DungeonCell; Session->UpsertWorldObject(Portal);
            Client->TickWorldObjectVisibility(1.f);
            TestFalse(TEXT("Portal in occupied dungeon cell remains known"), Session->ObjectVisibilityDeadlines.Contains(Portal.Guid));
            if (TestTrue(TEXT("Dungeon has authored PVS neighbors"), !Cell.VisibleCells.IsEmpty()))
            {
                Portal.Position.CellId = int32((DungeonCell & 0xFFFF0000u) | Cell.VisibleCells[0]);
                Session->UpsertWorldObject(Portal); Client->TickWorldObjectVisibility(1.f);
                TestFalse(TEXT("Portal in dungeon PVS survives regardless of camera direction"), Session->ObjectVisibilityDeadlines.Contains(Portal.Guid));
            }
            Portal.Position.CellId = 0x7D640014; Session->UpsertWorldObject(Portal);
            Client->TickWorldObjectVisibility(1.f);
            TestTrue(TEXT("Outdoor portal leaves interest during dungeon travel"), Session->ObjectVisibilityDeadlines.Contains(Portal.Guid));
        }
        Session->ClearWorldState();
        TestTrue(TEXT("Logout clears pending expiration"), Session->ObjectVisibilityDeadlines.IsEmpty());
    }
    GI->Shutdown();
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
