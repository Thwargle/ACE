#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEScriptComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEInitialVisibilityTest, "ACE.RetailParity.InitialVisibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FACEInitialVisibilityTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
    GI->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(GI);
    ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;

    // Actual retail Bind Stone: Ready frame zero hides parts 3..6, not the stone.
    // A distant view also exercises pose throttling before the first game tick.
    auto* PC = World->SpawnActor<APlayerController>();
    PC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
    PC->PlayerCameraManager->InitializeFor(PC);
    PC->PlayerCameraManager->bUseClientSideCameraUpdates = false;
    auto* Camera = World->SpawnActor<ACameraActor>();
    Camera->SetActorLocation(FVector(200000, 200000, 200000));
    PC->SetViewTarget(Camera); PC->PlayerCameraManager->UpdateCamera(0.f);
    TestTrue(TEXT("Fixture has a distant camera"), PC->PlayerCameraManager->GetCameraLocation().Size() > 8000);

    FACEWorldObject Stone; Stone.Guid = 0x700010AC; Stone.Name = TEXT("Bind Stone");
    Stone.SetupId = 0x020010AC; Stone.MotionTableId = 0x09000160;
    Stone.ItemType = ACEItemType::LifeStone; Stone.PhysicsState = ACEPhysicsState::IgnoreCollisions | ACEPhysicsState::Gravity;
    Stone.bHasPosition = true; Stone.Position.CellId = 0x7D640001;
    auto CheckParts = [&](AACEWorldEntityActor* Actor, const TCHAR* Phase, bool bHidden)
    {
        TestEqual(TEXT("Retail bind stone has seven parts"), Actor->Appearance->GetPartCount(), 7);
        for (int32 I = 0; I < 7; ++I)
        {
            auto* Part = Cast<UMeshComponent>(Actor->Appearance->GetPartMesh(I));
            if (!TestNotNull(TEXT("Bind stone part exists"), Part)) continue;
            TestTrue(TEXT("Base stone is not hidden to suppress the spikes"), Part->IsVisible());
            for (int32 M = 0; M < Part->GetNumMaterials(); ++M)
            {
                auto* Material = Part->GetMaterial(M);
                if (!TestNotNull(TEXT("Bind stone part has a material"), Material)) continue;
                float Opacity = 1.f; Material->GetScalarParameterValue(TEXT("OpacityMul"), Opacity);
                const float Expected = I >= 3 && bHidden ? 0.f : 1.f;
                TestEqual(*FString::Printf(TEXT("%s part %d material %d opacity"), Phase, I, M), Opacity, Expected);
            }
        }
    };
    for (bool bDeferred : {false, true})
    {
        auto* Actor = World->SpawnActor<AACEWorldEntityActor>();
        Actor->InitializeFromObject(Stone, 100.f, !bDeferred);
        if (bDeferred) TestTrue(TEXT("Deferred appearance succeeds"), Actor->ApplyDatAppearanceFromObject(Stone));
        TestTrue(TEXT("A loaded setup without a default script still finishes initialization"), Actor->ScriptComponent->HasStartedDefaultScripts());
        CheckParts(Actor, bDeferred ? TEXT("Deferred first frame") : TEXT("Cold first frame"), true);
        // The first distant animation tick is smaller than the 15 Hz interval.
        Actor->Appearance->TickComponent(.001f, LEVELTICK_All, nullptr);
        CheckParts(Actor, TEXT("First throttled tick"), true);
        Actor->InitializeFromObject(Stone, 100.f, true);
        CheckParts(Actor, TEXT("Duplicate descriptor"), true);
        // Rebuilding materials must restore frame-zero state too.
        FACEWorldObject Rebuilt = Stone; Rebuilt.Translucency = .01f;
        Actor->InitializeFromObject(Rebuilt, 100.f, true);
        // Whole-object translucency affects the base, so check its original value separately below.
        Rebuilt.Translucency = 0.f;
        Actor->InitializeFromObject(Rebuilt, 100.f, true);
        CheckParts(Actor, TEXT("Rebuilt first frame"), true);
        // A bind action legitimately reveals the spikes. Do not hard-hide their meshes.
        FACEObjectMotionState Use; Use.ActionCommand = 0x10000051; Use.ActionSpeed = 1.f;
        Use.CurrentStyle = ACEMotion::StanceNonCombat; Use.ForwardCommand = ACEMotion::Ready;
        Actor->ApplyMotionState(Use);
        for (int32 Frame = 0; Frame < 20; ++Frame)
        {
            Actor->Appearance->TickComponent(.1f, LEVELTICK_All, nullptr);
            Actor->ScriptComponent->TickComponent(.1f, LEVELTICK_All, nullptr);
        }
        CheckParts(Actor, TEXT("Bind action reveals spikes"), false);
        Actor->Destroy();
    }
    return true;
}
#endif

