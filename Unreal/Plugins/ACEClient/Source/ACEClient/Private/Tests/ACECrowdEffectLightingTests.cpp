#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEEffectLightSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/UObjectIterator.h"

namespace ACECrowdLightingTests
{
    struct FBudgetScope
    {
        IConsoleVariable* Desktop = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.FXLightBudget"));
        IConsoleVariable* Mobile = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.FXLightBudgetMobile"));
        IConsoleVariable* Distance = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.FXLightMaxDistance"));
        int32 PreviousDesktop = Desktop ? Desktop->GetInt() : 0;
        int32 PreviousMobile = Mobile ? Mobile->GetInt() : 0;
        float PreviousDistance = Distance ? Distance->GetFloat() : 0.f;

        FBudgetScope()
        {
            if (Distance) Distance->Set(3500.f, ECVF_SetByCode);
        }

        ~FBudgetScope()
        {
            if (Desktop) Desktop->Set(PreviousDesktop, ECVF_SetByCode);
            if (Mobile) Mobile->Set(PreviousMobile, ECVF_SetByCode);
            if (Distance) Distance->Set(PreviousDistance, ECVF_SetByCode);
        }
        bool IsValid() const { return Desktop && Mobile && Distance; }
        void Set(int32 Value) const
        {
            Desktop->Set(Value, ECVF_SetByCode);
            Mobile->Set(Value, ECVF_SetByCode);
        }
    };

    struct FSource
    {
        AActor* Owner = nullptr;
        UActorComponent* Component = nullptr;
        FACEEffectLightRequest Request;
    };

    struct FWorld
    {
        UWorld* World = nullptr;
        UACEEffectLightSubsystem* Lights = nullptr;
        ACameraActor* View = nullptr;
        APlayerController* PC = nullptr;

        FWorld()
        {
            const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
                .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Lights = World->GetSubsystem<UACEEffectLightSubsystem>();
            View = World->SpawnActor<ACameraActor>();
            PC = World->SpawnActor<APlayerController>();
            World->AddController(PC);
            if (!PC->PlayerCameraManager)
            {
                PC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
                PC->PlayerCameraManager->InitializeFor(PC);
            }
            PC->PlayerCameraManager->bUseClientSideCameraUpdates = false;
            PC->SetViewTarget(View);
            SetView(FVector::ZeroVector);
        }
        ~FWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        void SetView(const FVector& Position)
        {
            View->SetActorLocation(Position);
            PC->PlayerCameraManager->UpdateCamera(0.f);
        }
        FSource Source(const FVector& Position, float Intensity = 10.f, float Radius = 320.f)
        {
            FSource Result;
            Result.Owner = World->SpawnActor<AActor>();
            auto* Root = NewObject<USceneComponent>(Result.Owner);
            Result.Owner->AddInstanceComponent(Root);
            Result.Owner->SetRootComponent(Root);
            Root->SetMobility(EComponentMobility::Movable);
            Root->RegisterComponent();
            Result.Owner->SetActorLocation(Position);
            Result.Component = Root;
            Result.Request.EmitterId = 1;
            Result.Request.Position = Position;
            Result.Request.Color = FLinearColor(1.f, .6f, .2f);
            Result.Request.Intensity = Intensity;
            Result.Request.Radius = Radius;
            return Result;
        }
        void Submit(const FSource& Source)
        {
            Lights->SubmitParticles(Source.Component, MakeArrayView(&Source.Request, 1));
        }
        void SubmitAll(const TArray<FSource>& Sources)
        {
            for (const auto& Source : Sources) Submit(Source);
        }
        TArray<UPointLightComponent*> ActualLights(bool VisibleOnly = true) const
        {
            TArray<UPointLightComponent*> Result;
            for (TObjectIterator<UPointLightComponent> It; It; ++It)
            {
                auto* Light = *It;
                if (IsValid(Light) && Light->GetWorld() == World && Light->IsRegistered()
                    && (!VisibleOnly || (Light->IsVisible() && !Light->bHiddenInGame && Light->Intensity > 0.f)))
                    Result.Add(Light);
            }
            return Result;
        }
        float TotalIntensity() const
        {
            float Result = 0.f;
            for (const auto* Light : ActualLights()) Result += Light->Intensity;
            return Result;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdLightBudgetTest, "ACE.Rendering.CrowdEffectLighting.Budget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECrowdLightBudgetTest::RunTest(const FString&)
{
    using namespace ACECrowdLightingTests;
    FBudgetScope Budget;
    if (!TestTrue(TEXT("Both platform light budgets exist"), Budget.IsValid())) return false;
    Budget.Set(8);
    FWorld F;
    if (!TestNotNull(TEXT("Each world owns a light subsystem"), F.Lights)) return false;
    TArray<FSource> Sources;
    for (int32 I = 0; I < 128; ++I) Sources.Add(F.Source(FVector(I % 8, (I / 8) % 8, 100 + I / 64)));
    F.SubmitAll(Sources); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("128 sources select only eight lights"), F.Lights->GetVisibleLightCount(), 8);
    TestEqual(TEXT("Renderer has only eight visible actual point lights"), F.ActualLights().Num(), 8);
    TestEqual(TEXT("Unselected sources do not allocate hidden private light pools"), F.ActualLights(false).Num(), 8);
    TestTrue(TEXT("A packed crowd's total light energy stays bounded"), F.TotalIntensity() > 0.f && F.TotalIntensity() <= 12.001f);
    const int32 InitialPoolSize = F.Lights->GetAllocatedLightCount();

    for (int32 I = 0; I < Sources.Num(); ++I)
    {
        Sources[I].Request.Position = FVector((I % 8) * 150.f, ((I / 8) % 4) * 150.f, 100.f + (I / 32) * 150.f);
        Sources[I].Request.Radius = 30.f;
    }
    F.SubmitAll(Sources); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Dispersed sources remain globally bounded"), F.ActualLights().Num(), 8);
    TestTrue(TEXT("Separated effects retain their individual light strength"), F.TotalIntensity() > 70.f);
    TestEqual(TEXT("Moving to a new crowd reuses the same pool"), F.Lights->GetAllocatedLightCount(), InitialPoolSize);

    Budget.Set(2); F.SubmitAll(Sources); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Reducing the budget hides excess lights"), F.ActualLights().Num(), 2);
    Budget.Set(0); F.SubmitAll(Sources); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Zero budget disables all effect illumination"), F.ActualLights().Num(), 0);
    for (const auto& Source : Sources)
        F.Lights->UpdateParticlePosition(Source.Component, 1, FVector::ZeroVector);
    TestEqual(TEXT("Presentation updates cannot resurrect rejected lights"), F.ActualLights().Num(), 0);
    Budget.Set(8); F.SubmitAll(Sources); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Increasing the budget restores selected effects"), F.ActualLights().Num(), 8);
    TestEqual(TEXT("Budget changes do not grow a second light pool"), F.Lights->GetAllocatedLightCount(), InitialPoolSize);

    for (const auto& Source : Sources) F.Lights->RemoveSource(Source.Component);
    F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Stopping all sources clears actual illumination"), F.ActualLights().Num(), 0);
    auto Single = F.Source(FVector(100, 0, 0));
    TArray<FACEEffectLightRequest> Emitters;
    for (uint32 Id = 1; Id <= 3; ++Id)
    {
        auto Request = Single.Request; Request.EmitterId = Id; Emitters.Add(Request);
    }
    F.Lights->SubmitParticles(Single.Component, Emitters); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Three emitters on one carried item receive one light"), F.Lights->GetVisibleLightCount(Single.Component), 1);
    int32 Selected = 0;
    for (uint32 Id = 1; Id <= 3; ++Id) Selected += F.Lights->IsParticleSelected(Single.Component, Id) ? 1 : 0;
    TestEqual(TEXT("Selection and actual source light counts agree"), Selected, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdLightSelectionTest, "ACE.Rendering.CrowdEffectLighting.SelectionAndLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECrowdLightSelectionTest::RunTest(const FString&)
{
    using namespace ACECrowdLightingTests;
    FBudgetScope Budget;
    if (!TestTrue(TEXT("Light budgets exist"), Budget.IsValid())) return false;
    Budget.Set(1);
    FWorld F;
    if (!TestNotNull(TEXT("Light manager initializes"), F.Lights)) return false;
    auto Far = F.Source(FVector(1500, 0, 0));
    F.Submit(Far); F.Lights->RefreshLights(.05f);
    TestTrue(TEXT("First available source is selected"), F.Lights->IsParticleSelected(Far.Component, 1));
    auto Near = F.Source(FVector(100, 0, 0));
    F.Submit(Far); F.Submit(Near); F.Lights->RefreshLights(.05f);
    TestTrue(TEXT("A newly arriving nearby effect replaces the far incumbent"), F.Lights->IsParticleSelected(Near.Component, 1));
    TestFalse(TEXT("A source cannot monopolize the budget by registering first"), F.Lights->IsParticleSelected(Far.Component, 1));
    F.Lights->UpdateParticlePosition(Far.Component, 1, FVector(1, 0, 0));
    TestFalse(TEXT("Updating a rejected particle cannot select it outside the budget pass"), F.Lights->IsParticleSelected(Far.Component, 1));

    // Nearly equidistant sources must not exchange ownership on tiny head motion.
    Far.Request.Position = FVector(-101, 0, 0);
    F.Submit(Far); F.Submit(Near); F.Lights->RefreshLights(.05f);
    auto* StableLight = F.Lights->FindParticleLight(Near.Component, 1);
    F.SetView(FVector(-1, 0, 0));
    F.Submit(Far); F.Submit(Near); F.Lights->RefreshLights(.05f);
    TestTrue(TEXT("Small camera motion retains the close incumbent"), F.Lights->IsParticleSelected(Near.Component, 1));
    TestTrue(TEXT("Stable selection reuses its point light"), F.Lights->FindParticleLight(Near.Component, 1) == StableLight);
    Far.Request.Position = FVector(1500, 0, 0); F.SetView(FVector(1500, 0, 0));
    F.Submit(Far); F.Submit(Near); F.Lights->RefreshLights(.05f);
    TestTrue(TEXT("Meaningful camera movement updates proximity selection"), F.Lights->IsParticleSelected(Far.Component, 1));

    F.Lights->RemoveSource(Near.Component);
    F.Lights->RemoveEmitter(Far.Component, 1); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Removing the selected emitter retires its illumination"), F.ActualLights().Num(), 0);
    F.Submit(Far); F.Lights->RefreshLights(.05f);
    F.Lights->SubmitParticles(Far.Component, TConstArrayView<FACEEffectLightRequest>()); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("An empty submission clears a previous emitter set"), F.ActualLights().Num(), 0);
    F.Submit(Far); F.Lights->RefreshLights(.05f);
    F.Lights->RefreshLights(.30f);
    TestEqual(TEXT("An idle particle source expires without a final component tick"), F.ActualLights().Num(), 0);
    F.Submit(Far); F.Lights->RefreshLights(.40f);
    TestEqual(TEXT("A fresh submission survives the frame that follows a long hitch"), F.ActualLights().Num(), 1);
    F.Lights->RefreshLights(.30f);
    TestEqual(TEXT("An untouched source still expires after the hitch frame"), F.ActualLights().Num(), 0);
    F.Submit(Far); F.Lights->RefreshLights(.05f);
    Far.Owner->SetActorHiddenInGame(true); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Hidden owners stop illuminating the scene"), F.ActualLights().Num(), 0);
    Far.Owner->SetActorHiddenInGame(false); F.Submit(Far); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("A visible owner can submit again"), F.ActualLights().Num(), 1);
    Far.Owner->Destroy(); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Destroyed owners cannot leave pooled lights visible"), F.ActualLights().Num(), 0);

    auto Persistent = F.Source(FVector(200, 0, 0), 6.4f);
    F.SetView(FVector::ZeroVector);
    F.Lights->SetScriptLight(Persistent.Component, true, Persistent.Request); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Authored SetLight uses the shared pool"), F.Lights->GetVisibleLightCount(Persistent.Component), 1);
    Persistent.Owner->SetActorLocation(FVector(250, 30, 20));
    F.Lights->RefreshLights(.40f);
    auto* ScriptLight = F.Lights->FindScriptLight(Persistent.Component);
    if (!TestNotNull(TEXT("Authored light persists while its source is idle"), ScriptLight)) return false;
    TestTrue(TEXT("Idle authored light follows the moving owner"), ScriptLight->GetComponentLocation().Equals(Persistent.Owner->GetActorLocation(), .01f));
    F.Lights->RemoveParticles(Persistent.Component); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Removing particles preserves the independent authored light"), F.ActualLights().Num(), 1);
    F.Lights->SetScriptLight(Persistent.Component, false, Persistent.Request); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("SetLight off releases authored illumination"), F.ActualLights().Num(), 0);
    F.Lights->SetScriptLight(Persistent.Component, true, Persistent.Request); F.Lights->RefreshLights(.05f);
    F.Lights->RemoveSource(Persistent.Component); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Source end removes both particle and authored illumination"), F.ActualLights().Num(), 0);

    auto Mixed = F.Source(FVector(100, 0, 0));
    F.Submit(Mixed); F.Lights->RefreshLights(.05f);
    TestTrue(TEXT("Mixed source first establishes an inferred incumbent"), F.Lights->IsParticleSelected(Mixed.Component, 1));
    auto Authored = Mixed.Request;
    Authored.Color = FLinearColor(.2f, .8f, 1.f); Authored.Intensity = 6.4f;
    F.Lights->SetScriptLight(Mixed.Component, true, Authored);
    F.Submit(Mixed); F.Lights->RefreshLights(.05f);
    auto* AuthoredLight = F.Lights->FindScriptLight(Mixed.Component);
    if (!TestNotNull(TEXT("Explicit SetLight replaces the same source's inferred incumbent"), AuthoredLight)) return false;
    TestFalse(TEXT("Inferred light cannot mask a later authored hook"), F.Lights->IsParticleSelected(Mixed.Component, 1));
    TestTrue(TEXT("The selected authored hook retains its intensity"), FMath::IsNearlyEqual(AuthoredLight->Intensity, 6.4f, .001f));
    TestTrue(TEXT("The selected authored hook retains its color"), AuthoredLight->GetLightColor().Equals(Authored.Color, .01f));
    TestEqual(TEXT("Authored and inferred requests still share one source slot"), F.Lights->GetVisibleLightCount(Mixed.Component), 1);
    F.Lights->SetScriptLight(Mixed.Component, false, Authored);
    F.Submit(Mixed); F.Lights->RefreshLights(.05f);
    TestTrue(TEXT("SetLight off restores the source's still-live inferred effect"), F.Lights->IsParticleSelected(Mixed.Component, 1));
    TestNull(TEXT("Disabled authored light no longer owns a pool slot"), F.Lights->FindScriptLight(Mixed.Component));
    F.Lights->RemoveSource(Mixed.Component); F.Lights->RefreshLights(.05f);

    auto DestroyedComponent = F.Source(FVector(50, 0, 0));
    F.Submit(DestroyedComponent); F.Lights->RefreshLights(.05f);
    DestroyedComponent.Component->DestroyComponent(); F.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Destroyed source components also retire their light"), F.ActualLights().Num(), 0);
    TestTrue(TEXT("Repeated selection, destruction and restart do not grow the pool"), F.Lights->GetAllocatedLightCount() <= 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdLightWorldIsolationTest, "ACE.Rendering.CrowdEffectLighting.WorldIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECrowdLightWorldIsolationTest::RunTest(const FString&)
{
    using namespace ACECrowdLightingTests;
    FBudgetScope Budget;
    if (!TestTrue(TEXT("Light budgets exist"), Budget.IsValid())) return false;
    Budget.Set(8);
    FWorld A, B;
    if (!TestTrue(TEXT("Concurrent worlds have distinct managers"), A.Lights && B.Lights && A.Lights != B.Lights)) return false;
    TArray<FSource> First, Second;
    for (int32 I = 0; I < 16; ++I)
    {
        First.Add(A.Source(FVector(I * 2, 0, 100)));
        Second.Add(B.Source(FVector(I * 2, 0, 100)));
    }
    A.SubmitAll(First); B.SubmitAll(Second);
    A.Lights->RefreshLights(.05f); B.Lights->RefreshLights(.05f);
    TestEqual(TEXT("First world has its own full budget"), A.ActualLights().Num(), 8);
    TestEqual(TEXT("Second world has its own full budget"), B.ActualLights().Num(), 8);
    for (const auto& Source : First) A.Lights->RemoveSource(Source.Component);
    A.Lights->RefreshLights(.05f);
    TestEqual(TEXT("Stopping the first world hides only its pool"), A.ActualLights().Num(), 0);
    TestEqual(TEXT("The second world's selections remain visible"), B.ActualLights().Num(), 8);
    B.Lights->RefreshLights(.30f);
    TestEqual(TEXT("Expiry advances independently in the second world"), B.ActualLights().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdLightWorldTickTest, "ACE.Rendering.CrowdEffectLighting.PostActorTick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECrowdLightWorldTickTest::RunTest(const FString&)
{
    using namespace ACECrowdLightingTests;
    FBudgetScope Budget;
    if (!TestTrue(TEXT("Light budgets exist"), Budget.IsValid())) return false;
    Budget.Set(2);
    FWorld A, B;
    if (!TestTrue(TEXT("Both post-actor callbacks initialize"), A.Lights && B.Lights)) return false;
    auto Left = A.Source(FVector(-400, 0, 100), 10.f, 160.f);
    auto Right = A.Source(FVector(400, 0, 100), 10.f, 160.f);
    A.Submit(Left); A.Submit(Right);
    // Exercise the real world dispatch rather than directly refreshing the manager.
    A.World->Tick(LEVELTICK_All, .05f);
    TestEqual(TEXT("A real world tick dispatches effect-light selection"), A.Lights->GetVisibleLightCount(), 2);
    auto* LeftLight = A.Lights->FindParticleLight(Left.Component, 1);
    auto* RightLight = A.Lights->FindParticleLight(Right.Component, 1);
    if (!TestTrue(TEXT("Real dispatch creates both selected lights"), LeftLight && RightLight)) return false;
    TestTrue(TEXT("Separated effects begin at their full strengths"), A.TotalIntensity() > 19.f);

    // These submissions model the latest poses produced in TG_PostUpdateWork.
    // Broadcast the actual engine delegate to verify its world filtering and its
    // use of those current-frame poses before rendering.
    const FVector Together(100, 20, 100);
    Left.Request.Position = Together; Right.Request.Position = Together;
    A.Submit(Left); A.Submit(Right);
    FWorldDelegates::OnWorldPostActorTick.Broadcast(B.World, LEVELTICK_All, .40f);
    TestTrue(TEXT("Another world's post-actor event cannot apply pending transforms"),
        LeftLight->GetComponentLocation().Equals(FVector(-400, 0, 100), .001f));
    TestTrue(TEXT("Another world's clock cannot expire or renormalize these lights"),
        A.Lights->GetVisibleLightCount() == 2 && A.TotalIntensity() > 19.f);
    FWorldDelegates::OnWorldPostActorTick.Broadcast(A.World, LEVELTICK_All, .05f);
    TestTrue(TEXT("Post-actor selection consumes this frame's latest left pose"), LeftLight->GetComponentLocation().Equals(Together, .001f));
    TestTrue(TEXT("Post-actor selection consumes this frame's latest right pose"), RightLight->GetComponentLocation().Equals(Together, .001f));
    TestTrue(TEXT("Current-frame overlap is normalized before rendering"), A.TotalIntensity() > 0.f && A.TotalIntensity() <= 12.001f);
    TestEqual(TEXT("Foreign world remains without submitted effects"), B.Lights->GetVisibleLightCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdLightCostTest, "ACE.Rendering.CrowdEffectLighting.SelectionCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECrowdLightCostTest::RunTest(const FString&)
{
    using namespace ACECrowdLightingTests;
    FBudgetScope Budget;
    if (!TestTrue(TEXT("Light budgets exist"), Budget.IsValid())) return false;
    Budget.Set(8);
    FWorld F;
    if (!TestNotNull(TEXT("Light manager initializes"), F.Lights)) return false;
    TArray<FSource> Sources;
    for (int32 I = 0; I < 128; ++I) Sources.Add(F.Source(FVector((I % 16) * 40, (I / 16) * 40, 100)));
    F.SubmitAll(Sources); F.Lights->RefreshLights(.05f);
    const int32 AllocatedBefore = F.Lights->GetAllocatedLightCount();
    constexpr int32 Samples = 96;
    const double Start = FPlatformTime::Seconds();
    for (int32 Frame = 0; Frame < Samples; ++Frame)
    {
        F.SetView(FVector((Frame % 16) * 40, 0, 0));
        for (auto& Source : Sources)
        {
            Source.Request.Position.Z = 100.f + (Frame % 3);
            F.Submit(Source);
        }
        F.Lights->RefreshLights(1.f / 15.f);
    }
    const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0 / Samples;
    AddInfo(FString::Printf(TEXT("Crowd light CPU workload: %d sources, %d actual visible lights, %.4f ms/sample over %d submission+selection samples. Excludes GPU rendering and particle simulation."),
        Sources.Num(), F.ActualLights().Num(), Milliseconds, Samples));
    TestEqual(TEXT("Measured moving crowd remains capped"), F.ActualLights().Num(), 8);
    TestEqual(TEXT("Measured steady workload allocates no additional light components"), F.Lights->GetAllocatedLightCount(), AllocatedBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdLightSurfaceTest, "ACE.Rendering.CrowdEffectLighting.SurfaceRender",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECrowdLightSurfaceTest::RunTest(const FString&)
{
    using namespace ACECrowdLightingTests;
    if (!FApp::CanEverRender()) { AddError(TEXT("SurfaceRender requires an active renderer")); return false; }
    FBudgetScope Budget;
    if (!TestTrue(TEXT("Light budgets exist"), Budget.IsValid())) return false;
    Budget.Set(8);
    FWorld F;
    if (!TestNotNull(TEXT("Light manager initializes"), F.Lights)) return false;
    auto* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!TestTrue(TEXT("Neutral receiver assets load"), Plane && Material)) return false;
    auto* ReceiverActor = F.World->SpawnActor<AActor>();
    auto* Receiver = NewObject<UStaticMeshComponent>(ReceiverActor);
    ReceiverActor->AddInstanceComponent(Receiver); ReceiverActor->SetRootComponent(Receiver);
    Receiver->SetMobility(EComponentMobility::Movable); Receiver->SetStaticMesh(Plane); Receiver->SetMaterial(0, Material);
    Receiver->SetCollisionEnabled(ECollisionEnabled::NoCollision); Receiver->SetCastShadow(false);
    Receiver->SetWorldScale3D(FVector(8.f)); Receiver->RegisterComponent();
    constexpr int32 Size = 128;
    auto* Target = NewObject<UTextureRenderTarget2D>(ReceiverActor);
    Target->InitCustomFormat(Size, Size, PF_FloatRGBA, false);
    auto* Capture = NewObject<USceneCaptureComponent2D>(ReceiverActor);
    Capture->TextureTarget = Target; Capture->FOVAngle = 60.f;
    Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false;
    Capture->CaptureSource = SCS_SceneColorHDRNoAlpha;
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetAtmosphere(false); Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetAntiAliasing(false);
    Capture->SetWorldLocationAndRotation(FVector(0, 0, 900), FRotator(-90, 0, 0));
    Capture->RegisterComponent();
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/CrowdEffectLighting");
    IFileManager::Get().MakeDirectory(*Directory, true);
    auto Read = [&](const TCHAR* Name)
    {
        if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
        F.World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FLinearColor> Pixels;
        Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
        TArray<FColor> Display;
        Display.Reserve(Pixels.Num());
        for (const auto& Pixel : Pixels) Display.Add(Pixel.ToFColor(true));
        if (Display.Num() == Size * Size)
        {
            TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size, Size, Display, PNG);
            FFileHelper::SaveArrayToFile(PNG, *(Directory / Name));
        }
        return Pixels;
    };
    const auto Dark = Read(TEXT("NoLights.png"));
    TArray<FSource> Sources;
    Sources.Add(F.Source(FVector(0, 0, 150), 12.f, 600.f));
    Sources[0].Request.Color = FLinearColor::White;
    F.SubmitAll(Sources); F.Lights->RefreshLights(.05f);
    const auto Isolated = Read(TEXT("Isolated.png"));
    for (int32 I = 1; I < 128; ++I)
    {
        auto Source = F.Source(FVector(0, 0, 150), 12.f, 600.f);
        Source.Request.Color = FLinearColor::White; Sources.Add(Source);
    }
    F.SubmitAll(Sources); F.Lights->RefreshLights(.05f);
    const auto Crowd = Read(TEXT("Crowd128.png"));
    if (!TestTrue(TEXT("All receiver captures contain a full frame"), Dark.Num() == Size * Size
        && Isolated.Num() == Dark.Num() && Crowd.Num() == Dark.Num())) return false;
    double SingleEnergy = 0., CrowdEnergy = 0.;
    float MaximumDifference = 0.f;
    for (int32 I = 0; I < Dark.Num(); ++I)
    {
        SingleEnergy += FMath::Max(0.f, Isolated[I].GetLuminance() - Dark[I].GetLuminance());
        CrowdEnergy += FMath::Max(0.f, Crowd[I].GetLuminance() - Dark[I].GetLuminance());
        MaximumDifference = FMath::Max(MaximumDifference, FMath::Abs(Crowd[I].GetLuminance() - Isolated[I].GetLuminance()));
    }
    TestTrue(TEXT("The isolated effect measurably illuminates the receiver"), SingleEnergy > 1.0);
    TestTrue(TEXT("128 overlapping sources do not wash the surface brighter than an isolated light"), CrowdEnergy <= SingleEnergy * 1.10 + .01);
    TestTrue(TEXT("Crowd normalization preserves a visible local light"), CrowdEnergy >= SingleEnergy * .75);
    TestEqual(TEXT("Rendered crowd still submits only eight point lights"), F.ActualLights().Num(), 8);
    AddInfo(FString::Printf(TEXT("Surface comparison: isolated energy %.4f, 128-source energy %.4f, max per-pixel difference %.6f. PNGs: %s"),
        SingleEnergy, CrowdEnergy, MaximumDifference, *Directory));
    return true;
}
#endif
