#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "ACEWorldEntityActor.h"
#include "ACEWorldPresenterComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDeferredWorldAppearanceTest, "ACE.Rendering.DeferredWorldAppearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEDeferredWorldAppearanceTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI = NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(GI); Context.OwningGameInstance = GI; Context.SetCurrentWorld(World);
    GI->OnWorldChanged(nullptr, World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail models are available"), Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    Dat->SetLightweightStreaming(false);
    auto* Client = GI->GetSubsystem<UACEClientSubsystem>();
    auto Session = Client->GetSession();
    auto* Owner = World->SpawnActor<AActor>();
    auto* Presenter = NewObject<UACEWorldPresenterComponent>(Owner);
    Owner->AddInstanceComponent(Presenter); Presenter->RegisterComponent(); Presenter->Client = Client;

    // The server can place any retail setup under a new GUID/weenie class. It
    // need not appear in the landblock's DAT scenery or a client-side item list.
    FACEWorldObject Prop;
    Prop.Guid = int32(0x800F1234u); Prop.WeenieClassId = 900001; Prop.Name = TEXT("Custom scenery fixture");
    Prop.SetupId = 0x02000F1C; Prop.bHasPosition = true; Prop.Position.CellId = 0x7D640014;
    Prop.Position.Location = FVector(50, 50, 5);
    Prop.ObjectDescriptionFlags = ACEObjectDescFlag::Stuck;
    Prop.PhysicsState = ACEPhysicsState::Ethereal;
    Prop.RadarBehavior = ACERadarBehavior::ShowNever;
    Prop.UseRadius = 0.f; Prop.ItemUseable = 0;
    FACEWorldObject Immediate = Prop;
    Immediate.Guid += 2; Immediate.PhysicsState |= ACEPhysicsState::HasDefaultScript;
    auto Receive = [&](const FACEWorldObject& Object)
    {
        Session->WorldObjects.Add(Object.Guid, Object);
        Presenter->HandleObjectCreated(Object);
        Presenter->DrainPendingSpawns();
    };

    // Model the DAT index's pending interval without starting a background
    // worker. Completion restores the already loaded retail archive below.
    {
        TGuardValue<bool> Ready(Dat->bPortalLoaded, false);
        TGuardValue<bool> Loading(Dat->bBackgroundLoadInProgress, true);
        Receive(Prop);
        Session->WorldObjects.Add(Immediate.Guid, Immediate);
        Presenter->SpawnOrUpdateEntity(Immediate, true);
        for (int32 Frame = 0; Frame < 30; ++Frame) Presenter->DrainPendingDatAppearance();
        TestTrue(TEXT("Stationary server prop remains queued throughout DAT indexing"), Presenter->PendingDatAppearance.Contains(Prop.Guid));
        TestEqual(TEXT("Waiting for the archive spends no asset failures"), Presenter->AppearanceRetryCounts.FindRef(Prop.Guid), 0);
        TestTrue(TEXT("A cold immediate setup with scripts is not mistaken for completed particle-only geometry"),
            Presenter->PendingDatAppearance.Contains(Immediate.Guid));
    }
    // Each drain may stop after one expensive mesh to honor its frame budget.
    for (int32 Pass = 0; Pass < 2; ++Pass) Presenter->DrainPendingDatAppearance();
    auto* Actor = Presenter->FindEntityActor(Prop.Guid);
    if (!TestNotNull(TEXT("Custom server placement has a world actor"), Actor)) return false;
    if (!TestTrue(TEXT("Archive completion builds the model without another server packet"), Actor->bUsingDatMesh
        && Actor->Appearance && Actor->Appearance->HasAppearance())) return false;
    auto CheckVisibleGeometry = [&](AACEWorldEntityActor* Entity, const TCHAR* Phase, bool Visible)
    {
        if (!TestTrue(Phase, Entity && Entity->Appearance && Entity->Appearance->HasAppearance())) return;
        TestTrue(TEXT("The decoration has rendered parts"), Entity->Appearance->GetPartCount() > 0);
        for (int32 I = 0; I < Entity->Appearance->GetPartCount(); ++I)
        {
            const auto* Mesh = Entity->Appearance->GetPartMesh(I);
            TestTrue(Phase, Mesh && Mesh->IsVisible() == Visible);
        }
    };
    TestFalse(TEXT("Ethereal, stuck, nonusable no-radar decor remains visible"), Actor->IsHidden());
    CheckVisibleGeometry(Actor, TEXT("Decoration meshes are visible before relog"), true);
    auto* ImmediateActor = Presenter->FindEntityActor(Immediate.Guid);
    TestTrue(TEXT("The cold immediate model also recovers on archive completion"), ImmediateActor && ImmediateActor->bUsingDatMesh);

    // ACCObjectMaint::CreateObject applies the server ObjDesc to the setup.
    // Changing a part on that same setup must reach ApplyWorldObject even when
    // an old mesh and its default scripts are already ready.
    const uint64 OriginalRevision = Actor->Appearance->GetAppearanceRevision();
    auto& Part = Prop.Appearance.AnimPartChanges.AddDefaulted_GetRef();
    Part.PartIndex = 0; Part.PartId = 0x01000001;
    Receive(Prop); Presenter->DrainPendingDatAppearance();
    TestEqual(TEXT("Deferred descriptor installs the server's visual overrides"),
        Actor->Appearance->GetAppliedAppearanceHash(), Prop.Appearance.GetContentHash());
    TestTrue(TEXT("A changed part rebuilds an existing model"), Actor->Appearance->GetAppearanceRevision() > OriginalRevision);
    uint64 Revision = Actor->Appearance->GetAppearanceRevision();
    Receive(Prop); Presenter->DrainPendingDatAppearance();
    TestEqual(TEXT("A duplicate description preserves mesh and animation state"), Actor->Appearance->GetAppearanceRevision(), Revision);

    Prop.Translucency = .25f;
    Receive(Prop); Presenter->DrainPendingDatAppearance();
    TestTrue(TEXT("A physics descriptor's translucency change also reaches the appearance cache"),
        Actor->Appearance->GetAppearanceRevision() > Revision);

    // Even the immediate path may run before an object has a usable setup.
    FACEWorldObject Pending = Prop; Pending.Guid += 1; Pending.SetupId = 0; Pending.Appearance = FACEObjDesc();
    Session->WorldObjects.Add(Pending.Guid, Pending);
    Presenter->SpawnOrUpdateEntity(Pending, true);
    TestTrue(TEXT("An immediate model failure is retained for recovery"), Presenter->PendingDatAppearance.Contains(Pending.Guid));
    Presenter->DrainPendingDatAppearance();
    TestEqual(TEXT("A failed object is attempted once per drain, not once per budget slot"),
        Presenter->AppearanceRetryCounts.FindRef(Pending.Guid), 1);
    Presenter->AppearanceRetryCounts.Add(Pending.Guid, 21);
    Pending.SetupId = Prop.SetupId;
    Receive(Pending);
    TestFalse(TEXT("A fresh server description resets failures from the previous setup"), Presenter->AppearanceRetryCounts.Contains(Pending.Guid));
    Presenter->DrainPendingDatAppearance();
    auto* Repaired = Presenter->FindEntityActor(Pending.Guid);
    TestTrue(TEXT("A corrected server setup recovers the existing actor"), Repaired && Repaired->bUsingDatMesh);
    Presenter->AppearanceRetryCounts.Add(Pending.Guid, 20);
    Presenter->HandleObjectDeleted(Pending.Guid);
    TestFalse(TEXT("Deleting a GUID clears its previous appearance failures"), Presenter->AppearanceRetryCounts.Contains(Pending.Guid));
    Pending.SetupId = Prop.SetupId;
    Receive(Pending); Presenter->DrainPendingDatAppearance();
    auto* Replacement = Presenter->FindEntityActor(Pending.Guid);
    TestTrue(TEXT("Reusing a deleted GUID renders the new server object"), Replacement && Replacement->bUsingDatMesh);

    // Keep the session alive to exercise the persistent presenter's relog path.
    // A repeated CreateObject reconstructs the custom prop from its descriptor.
    Presenter->HandleLogout(EACESessionState::CharacterSelect);
    TestNull(TEXT("Logout removes the old custom actor"), Presenter->FindEntityActor(Prop.Guid));
    Receive(Prop); Presenter->DrainPendingDatAppearance();
    auto* Relogged = Presenter->FindEntityActor(Prop.Guid);
    TestTrue(TEXT("Relog reconstructs the server custom prop"), Relogged && Relogged != Actor && Relogged->bUsingDatMesh);
    if (Relogged && Relogged->Appearance)
    {
        TestEqual(TEXT("Relog preserves the server's custom model description"),
            Relogged->Appearance->GetAppliedAppearanceHash(), Prop.Appearance.GetContentHash());
        TestFalse(TEXT("Relog keeps noninteractive custom decor visible"), Relogged->IsHidden());
        CheckVisibleGeometry(Relogged, TEXT("Decoration meshes are visible after relog"), true);
    }

    // Retail uses explicit rendering flags (CPhysicsObj::set_nodraw and
    // ACCWeenieObject::SetHiddenAdmin), not guesses from collision/useability.
    FACEWorldObject Hidden = Prop; Hidden.Guid += 3;
    Hidden.ObjectDescriptionFlags |= ACEObjectDescFlag::HiddenAdmin;
    Receive(Hidden);
    TestNull(TEXT("Explicit HiddenAdmin still excludes the custom prop"), Presenter->FindEntityActor(Hidden.Guid));
    Hidden = Prop; Hidden.Guid += 4; Hidden.PhysicsState |= ACEPhysicsState::Cloaked;
    Receive(Hidden);
    TestNull(TEXT("Explicit Cloaked still excludes the custom prop for normal viewers"), Presenter->FindEntityActor(Hidden.Guid));
    Hidden = Prop; Hidden.Guid += 5; Hidden.PhysicsState |= ACEPhysicsState::NoDraw;
    Receive(Hidden); Presenter->DrainPendingDatAppearance();
    CheckVisibleGeometry(Presenter->FindEntityActor(Hidden.Guid), TEXT("Explicit NoDraw keeps the model hidden after deferred build"), false);
    Presenter->ClearSpawned();
    return true;
}
#endif
