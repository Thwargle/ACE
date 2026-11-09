#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEEnvCellActor.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatFileTypes.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "../ACEBodySweep.h"
#include "../ACEVisibleObjectPick.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECageDoorCollisionTest, "ACE.Collision.CageDoors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECageDoorCollisionTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(GI); Context.OwningGameInstance = GI; GI->Init();
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    FString DatDirectory = TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), DatDirectory);
    if (!TestTrue(TEXT("Retail DAT loads"), Dat->LoadDatDirectory(DatDirectory))) return false;

    constexpr uint32 CageSetup = 0x02000281;
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Cage setup exists"), Dat->GetPortalDat()->ReadFile(CageSetup, Bytes))) return false;
    FACEDatCursor Cursor(Bytes); FACEDatSetupModel Setup;
    if (!TestTrue(TEXT("Cage setup unpacks"), ACEDatUnpack::UnpackSetupModel(Cursor, Setup))) return false;
    AddInfo(FString::Printf(TEXT("Cage flags=%X height=%f radius=%f spheres=%d cylinders=%d"),
        uint32(Setup.Flags), Setup.Height, Setup.Radius, Setup.Spheres.Num(), Setup.CylSpheres.Num()));
    TestTrue(TEXT("Cage authors physics BSP"), EnumHasAnyFlags(Setup.Flags, EACESetupFlags::HasPhysicsBSP));
    for (const auto& S : Setup.CylSpheres)
        AddInfo(FString::Printf(TEXT("Cylinder %s radius=%f height=%f"), *FVector(S.Origin).ToString(), S.Radius, S.Height));
    const auto Built = Dat->GetOrBuildSetupMeshShared(CageSetup, 100, 0);
    if (!TestTrue(TEXT("Cage mesh builds"), Built.IsValid())) return false;
    int32 PhysicsFaces = 0;
    for (const auto& Part : Built->Parts) for (const auto& Section : Part.Sections)
        if (Section.bCollisionOnly) PhysicsFaces += Section.Triangles.Num() / 3;
    TestTrue(TEXT("Cage has real BSP collision faces"), PhysicsFaces > 0);
    AddInfo(FString::Printf(TEXT("Cage physics triangles=%d"), PhysicsFaces));

    TArray<FACEDatCollisionShape> HumanSpheres;
    if (!TestTrue(TEXT("Retail player body loads"), Dat->GetSetupMovementSpheres(0x02000001, HumanSpheres)
        && HumanSpheres.Num() >= 2)) return false;
    // Sweep from feet, retaining retail's two spheres instead of a capsule.
    FACECollisionBody Body(FCollisionShape::MakeSphere(48));
    Body.Count = 2;
    for (int32 I = 0; I < 2; ++I)
    {
        Body.Centers[I] = FACEPosition::AceVectorToUnreal(FVector(HumanSpheres[I].Origin), 100);
        Body.Radii[I] = HumanSpheres[I].Radius * 100;
    }

    FACEWorldObject Object;
    Object.Guid = 0x75860005; Object.Name = TEXT("Shady's Cage Door"); Object.WeenieClassId = 72984;
    Object.SetupId = CageSetup; Object.MotionTableId = 0x09000016;
    Object.ItemType = ACEItemType::Creature; Object.ObjectDescriptionFlags = 0x4;
    Object.PhysicsState = 0x00010408; Object.bHasPosition = true;
    Object.Position.CellId = 0x586002E7; Object.Position.Location = FVector(69.9805, -175.285, 30.055);
    auto* Actor = World->SpawnActor<AACEWorldEntityActor>();
    Actor->InitializeFromObject(Object, 100, false);
    TestEqual(TEXT("Deferred appearance must not turn the pick sphere into a solid obstacle"),
        Actor->CollisionProxy->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
    TestTrue(TEXT("Deferred cage appearance loads"), Actor->ApplyDatAppearanceFromObject(Object));
    const FVector Origin = Actor->GetActorLocation();
    auto LocalPoint = [&](const FVector& P) { return Origin + FACEPosition::AceVectorToUnreal(P - Object.Position.Location, 100); };
    const FVector From = LocalPoint(FVector(70.862732, -173.685619, 30.005));
    const FVector NearDoor = LocalPoint(FVector(70.862732, -174.485, 30.005));
    const FVector ThroughDoor = LocalPoint(FVector(70.862732, -176.285, 30.005));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CageApproach), true);
    FHitResult Hit;
    const bool Blocked = ACEBodySweep::SweepBody(*World, Hit, From, NearDoor, Body, Query);
    AddInfo(FString::Printf(TEXT("Approach blocked=%d by %s at %s (distance %.3f AC)"), Blocked,
        *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), Hit.Distance / 100));
    TestFalse(TEXT("Player can approach the cage from reported position without jumping"), Blocked);
    TestTrue(TEXT("Authored cage leaves remain solid"), ACEBodySweep::SweepBody(*World, Hit, From, ThroughDoor, Body, Query));
    TestTrue(TEXT("Cage collision comes from its model"), Cast<UProceduralMeshComponent>(Hit.GetComponent()) != nullptr);
    TestEqual(TEXT("NPC selection volume never blocks the approach"), Actor->CollisionProxy->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
    TestTrue(TEXT("The cage can still be selected for giving the pet"),
        ACEVisibleObjectPick::Trace(*World, From + FVector(0, 0, 100), ThroughDoor + FVector(0, 0, 100), nullptr, nullptr, false) == Actor);

    for (int32 Pass = 0; Pass < 3; ++Pass)
    {
        Actor->ApplyPhysicsState(Object.PhysicsState);
        Actor->SetCellVisible(false); Actor->SetCellVisible(true);
        TestFalse(TEXT("Physics and visibility refresh preserve the clear approach"), ACEBodySweep::SweepBody(*World, Hit, From, NearDoor, Body, Query));
        TestTrue(TEXT("Physics and visibility refresh preserve the real door"), ACEBodySweep::SweepBody(*World, Hit, From, ThroughDoor, Body, Query));
    }
    Actor->ApplyPhysicsState(Object.PhysicsState | ACEPhysicsState::Ethereal);
    TestFalse(TEXT("Ethereal NPC cage is walk-through"), ACEBodySweep::SweepBody(*World, Hit, From, ThroughDoor, Body, Query));
    Actor->ApplyPhysicsState(Object.PhysicsState);
    TestTrue(TEXT("Clearing Ethereal restores the authored leaves"), ACEBodySweep::SweepBody(*World, Hit, From, ThroughDoor, Body, Query));

    // Verify the real neighboring cells too, not only an isolated model.
    const FVector LandblockOrigin = Origin - FACEPosition::AceVectorToUnreal(Object.Position.Location, 100);
    TArray<AACEEnvCellActor*> Rooms;
    for (uint32 Cell : {0x586002E6u, 0x586002E7u})
    {
        const auto* BuiltCell = Dat->GetOrBuildEnvCellMesh(Cell, 100);
        if (!TestNotNull(TEXT("Reported dungeon cell exists"), BuiltCell)) continue;
        for (const auto& Stab : BuiltCell->StaticObjects)
            Dat->GetOrBuildSetupMesh(Stab.Id, 100, UACEDatSubsystem::ACEPlacementResting);
        auto* Room = World->SpawnActor<AACEEnvCellActor>(); Rooms.Add(Room);
        TestTrue(TEXT("Dungeon collision loads"), Room->LoadEnvCell(Cell, LandblockOrigin, 100));
        Room->SetEnvCellCollisionActive(true); Room->EnsureStaticObjectsQueued();
        for (int32 I = 0; I < 200 && Room->HasPendingStaticObjects(); ++I)
        {
            ++GFrameCounter; Room->Tick(.016f);
            FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        }
        TestFalse(TEXT("All authored dungeon furniture is included"), Room->HasPendingStaticObjects());
    }
    bool RoomBlocked = ACEBodySweep::SweepBody(*World, Hit, From, NearDoor, Body, Query);
    AddInfo(FString::Printf(TEXT("Dungeon contact=%d by %s / %s normal=%s depth=%.4f distance=%.4f"), RoomBlocked,
        *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.Normal.ToString(), Hit.PenetrationDepth, Hit.Distance));
    if (RoomBlocked)
    {
        // The reported feet put the lower sphere exactly on the floor (DAT
        // center .475, radius .480). Walking treats that contact as support.
        // Clear it by half a centimetre without shrinking horizontal collision
        // or ignoring the room component, so walls still participate.
        TestTrue(TEXT("Initial dungeon contact is floor support, not an obstacle"),
            !ACEBodySweep::IsUpperBodyContact(Hit) && Hit.Normal.Z > .99f && Hit.PenetrationDepth < .5f);
        RoomBlocked = ACEBodySweep::SweepBody(*World, Hit, From, NearDoor, Body.WithClearance(.5f, 0), Query);
    }
    AddInfo(FString::Printf(TEXT("Dungeon approach blocked=%d by %s / %s"), RoomBlocked,
        *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent())));
    TestFalse(TEXT("Approach stays clear with reported room geometry and furniture"), RoomBlocked);
    for (auto* Room : Rooms) Room->Destroy();
    Actor->Destroy();

    // Apply the same data rule to other NPCs, placements, and object scales;
    // neither the name/WCID nor a server Door description flag is required.
    Object.Name = TEXT("Generic BSP NPC"); Object.WeenieClassId = 0;
    Object.PhysicsState &= ~ACEPhysicsState::HasPhysicsBSP;
    for (float Scale : {.5f, 1.f, 2.f}) for (float Heading : {0.f, 90.f, 215.f})
    {
        Object.Scale = Scale;
        auto* Variant = World->SpawnActor<AACEWorldEntityActor>(); Variant->InitializeFromObject(Object, 100, true);
        Variant->SetActorRotation(FRotator(0, Heading, 0));
        const auto Transform = Variant->GetActorTransform();
        const auto Point = [&](const FVector& WorldPoint) { return Transform.TransformPosition(WorldPoint - Origin); };
        auto ScaledBody = Body;
        for (int32 I = 0; I < 2; ++I) { ScaledBody.Centers[I] *= Scale; ScaledBody.Radii[I] *= Scale; }
        TestFalse(TEXT("Scaled/rotated BSP NPC retains the approach gap"), ACEBodySweep::SweepBody(*World, Hit, Point(From), Point(NearDoor), ScaledBody, Query));
        TestTrue(TEXT("Scaled/rotated BSP NPC retains the door leaves"), ACEBodySweep::SweepBody(*World, Hit, Point(From), Point(ThroughDoor), ScaledBody, Query));
        TestTrue(TEXT("DAT BSP works before the server physics bit arrives"), Cast<UProceduralMeshComponent>(Hit.GetComponent()) != nullptr);
        Variant->Destroy();
    }
    Object.Scale = 1;
    Object.ParentGuid = 12345; Object.ParentLocation = 1;
    auto* Attached = World->SpawnActor<AACEWorldEntityActor>(); Attached->InitializeFromObject(Object, 100, true);
    Attached->SetActorLocation(Origin);
    TestFalse(TEXT("Attached objects never acquire NPC mesh blocking"), ACEBodySweep::SweepBody(*World, Hit, From, ThroughDoor, Body, Query));
    Attached->Destroy();
    return true;
}
#endif
