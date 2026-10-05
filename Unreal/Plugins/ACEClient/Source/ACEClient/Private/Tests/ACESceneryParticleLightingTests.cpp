#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ACEDatSubsystem.h"
#include "ACEEffectLightSubsystem.h"
#include "ACEParticleBatchComponent.h"
#include "ACERegionSceneryActor.h"
#include "ACEScriptComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESceneryParticleLightingTest, "ACE.RetailParity.SceneryParticleLighting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACESceneryParticleLightingTest::RunTest(const FString&)
{
    auto* Batches = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.Batched"));
    auto* Culling = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.DistanceCulling"));
    auto* Budget = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.FXLightBudget"));
    auto* MobileBudget = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.FXLightBudgetMobile"));
    auto* Distance = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.FXLightMaxDistance"));
    const int32 SavedBatch = Batches->GetInt(), SavedCulling = Culling->GetInt();
    const int32 SavedBudget = Budget->GetInt(), SavedMobileBudget = MobileBudget->GetInt();
    const float SavedDistance = Distance->GetFloat();
    ON_SCOPE_EXIT
    {
        Batches->Set(SavedBatch, ECVF_SetByCode); Culling->Set(SavedCulling, ECVF_SetByCode);
        Budget->Set(SavedBudget, ECVF_SetByCode); MobileBudget->Set(SavedMobileBudget, ECVF_SetByCode);
        Distance->Set(SavedDistance, ECVF_SetByCode);
    };
    Culling->Set(1, ECVF_SetByCode);
    Budget->Set(8, ECVF_SetByCode); MobileBudget->Set(8, ECVF_SetByCode);
    Distance->Set(3500.f, ECVF_SetByCode);
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance = GI;
    GI->OnWorldChanged(nullptr, World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail snow DAT opens"), Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    auto* Lights = World->GetSubsystem<UACEEffectLightSubsystem>();
    if (!TestNotNull(TEXT("Scenery has a shared world light budget"), Lights)) return false;
    auto* View = World->SpawnActor<ACameraActor>();
    auto* PC = World->SpawnActor<APlayerController>(); World->AddController(PC);
    if (!PC->PlayerCameraManager)
    {
        PC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
        PC->PlayerCameraManager->InitializeFor(PC);
    }
    PC->PlayerCameraManager->bUseClientSideCameraUpdates = false;
    PC->SetViewTarget(View); PC->PlayerCameraManager->UpdateCamera(0.f);
    auto CountLights = [Lights](AActor* Owner)
    {
        auto* Source = Owner->FindComponentByClass<UACEScriptComponent>();
        return Source ? Lights->GetVisibleLightCount(Source) : 0;
    };
    struct FSnowCase { uint32 Setup, Script, Emitter; };
    const FSnowCase Cases[] = {{0x02000406, 0x33000114, 0x32000143}, {0x02000407, 0x33000113, 0x32000142}};
    FLinearColor SnowColor; float SnowLuminosity = 0;
    const uint64 SurfaceResolvesBefore = Dat->GetTextureResolver()->GetSurfaceResolveCount();
    const bool bSnowEstimate = Dat->TryEstimateGfxLight(0x01001166, SnowColor, SnowLuminosity);
    TestTrue(TEXT("Snow's bright authored texture is a valid light candidate"), bSnowEstimate);
    TestEqual(TEXT("Snow surface luminosity remains authored"), SnowLuminosity, 1.f);
    TestEqual(TEXT("First light estimate resolves its authored surface once"),
        Dat->GetTextureResolver()->GetSurfaceResolveCount(), SurfaceResolvesBefore + 1);
    for (int32 Repeat = 0; Repeat < 128; ++Repeat)
    {
        FLinearColor CachedColor = FLinearColor::Black; float CachedLuminosity = -1.f;
        TestEqual(TEXT("Cached light estimate preserves success"),
            Dat->TryEstimateGfxLight(0x01001166, CachedColor, CachedLuminosity), bSnowEstimate);
        TestTrue(TEXT("Cached light estimate preserves exact color and luminosity"),
            CachedColor == SnowColor && CachedLuminosity == SnowLuminosity);
    }
    TestEqual(TEXT("Repeated light estimates skip surface lookup and pixel averaging"),
        Dat->GetTextureResolver()->GetSurfaceResolveCount(), SurfaceResolvesBefore + 1);

    for (int32 Batch : {0, 1}) for (const auto& Snow : Cases)
    {
        Batches->Set(Batch, ECVF_SetByCode);
        auto* Owner = World->SpawnActor<AACERegionSceneryActor>();
        if (!TestTrue(TEXT("Actual region scenery snow initializes"), Owner->InitializeFromSetup(Snow.Setup, 1.f, 100.f, false, true))) return false;
        auto* FX = Owner->ScriptComponent.Get();
        if(Owner->Appearance && !Owner->Appearance->GetPartMesh(0))
        {
            Owner->Appearance->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
            TestFalse(TEXT("Particle-only scenery stops scheduling empty pose ticks"),Owner->Appearance->IsComponentTickEnabled());
        }
        const FString Case = FString::Printf(TEXT("Snow %08X batch%d"), Snow.Setup, Batch);
        TestFalse(Case + TEXT(" disables generated illumination"), FX->bAllowInferredParticleLights);
        TestFalse(Case + TEXT(" retains scenery coordinates rather than camera weather coordinates"), FX->bEnvironmentWeather);
        TestEqual(Case + TEXT(" loads its authored default script"), FX->SetupDefaultScript, Snow.Script);
        if (!TestEqual(Case + TEXT(" creates the authored emitter"), FX->ActiveEmitters.Num(), 1)) return false;
        auto& Emitter = FX->ActiveEmitters[0];
        TestEqual(Case + TEXT(" retains the emitter DID"), Emitter.AuthoredEmitterId, Snow.Emitter);
        TestEqual(Case + TEXT(" retains the snow graphic"), Emitter.Info.GfxObjId, uint32(0x01001166));
        TestEqual(Case + TEXT(" skips light estimation"), Emitter.LightLum, 0.f);
        for (int32 Frame = 0; Frame < 120; ++Frame) FX->TickParticleSimulation(1.f / 60.f);
        if (!TestTrue(Case + TEXT(" emits visible snow"), !Emitter.Particles.IsEmpty())) return false;
        TestEqual(Case + TEXT(" preserves the selected renderer"), bool(Emitter.Batch), Batch != 0);
        TestEqual(Case + TEXT(" preserves authored parent space"), Emitter.Particles[0].bParentLocal, Emitter.Info.IsParentLocal != 0);
        auto* Visual = Batch ? static_cast<UProceduralMeshComponent*>(Emitter.Batch.Get()) : Emitter.Particles[0].Mesh.Get();
        if (!TestTrue(Case + TEXT(" retains a visible particle component"), Visual && Visual->IsVisible() && !Visual->bHiddenInGame)) return false;
        float MaterialLuminosity = 0.f;
        TestTrue(Case + TEXT(" preserves authored material self-brightness"), Visual->GetMaterial(0)
            && Visual->GetMaterial(0)->GetScalarParameterValue(TEXT("EmissiveStrength"), MaterialLuminosity)
            && MaterialLuminosity == SnowLuminosity);
        const FVector Before = Emitter.Particles[0].Position;
        const float Age = Emitter.Particles[0].Age;
        const int32 Born = Emitter.TotalBorn, Seed = FX->Random.GetCurrentSeed();
        FX->TickParticlePresentation(.01f); FX->TickParticleLights(true); Lights->RefreshLights(.05f);
        TestFalse(Case + TEXT(" still moves between lifecycle updates"), Emitter.Particles[0].Position.Equals(Before, .001));
        TestTrue(Case + TEXT(" presentation preserves lifetime, births and RNG"),
            Emitter.Particles[0].Age == Age && Emitter.TotalBorn == Born && FX->Random.GetCurrentSeed() == Seed);
        if (!Batch)
            TestTrue(Case + TEXT(" updates the actual reference mesh"), Emitter.Particles[0].Mesh.IsValid()
                && Emitter.Particles[0].Mesh->GetComponentLocation().Equals(Emitter.Particles[0].Position, .001));
        else
            TestEqual(Case + TEXT(" keeps the batch population"), Emitter.Batch->GetParticleCount(), Emitter.Particles.Num());
        TestEqual(Case + TEXT(" receives no lights across lifecycle and presentation ticks"), CountLights(Owner), 0);
        TestEqual(Case + TEXT(" leaves the world light pool unallocated"), Lights->GetAllocatedLightCount(), 0);

        // Scenery must retain its finite draw distance; treating it as sky weather would bypass this.
        Owner->SetActorLocation(FVector(10000000, 0, 0)); FX->TickEmitters(1.f / 30.f); FX->TickParticleLights();
        TestTrue(Case + TEXT(" still degrades outside authored range"), Emitter.bDegraded);
        Owner->SetActorLocation(FVector::ZeroVector); FX->TickEmitters(1.f / 30.f); FX->TickParticleLights();
        TestTrue(Case + TEXT(" resumes particles on reentry"), !Emitter.bDegraded && !Emitter.Particles.IsEmpty());
        FX->StopAllEffects(); FX->TickParticleSimulation(1.f / 60.f);
        TestTrue(Case + TEXT(" stops all emitters"), FX->ActiveEmitters.IsEmpty());
        FX->PlayScriptId(Snow.Script, 1.f);
        for (int32 Frame = 0; Frame < 60; ++Frame) FX->TickParticleSimulation(1.f / 60.f);
        TestTrue(Case + TEXT(" restarts its authored snow script"), !FX->ActiveEmitters.IsEmpty() && !FX->ActiveEmitters[0].Particles.IsEmpty());
        Lights->RefreshLights(.05f);
        TestEqual(Case + TEXT(" remains light-free after reentry and restart"), CountLights(Owner), 0);
        TestEqual(Case + TEXT(" never allocates a world light on reentry and restart"), Lights->GetAllocatedLightCount(), 0);
        FX->StopAllEffects(); Owner->Destroy();
    }

    // The same actor class also hosts placed torches. Only RegionDesc terrain gets the exclusion.
    auto* Torch = World->SpawnActor<AACERegionSceneryActor>();
    if (!TestTrue(TEXT("Non-region authored torch initializes"), Torch->InitializeFromSetup(0x02000081))) return false;
    TestTrue(TEXT("Non-region scenery retains light eligibility"), Torch->ScriptComponent->bAllowInferredParticleLights);
    for (int32 Frame = 0; Frame < 60; ++Frame) Torch->ScriptComponent->TickParticleSimulation(1.f / 60.f);
    Torch->ScriptComponent->TickParticleLights(); Lights->RefreshLights(.05f);
    TestTrue(TEXT("Non-region authored torch retains visible effect lights"), CountLights(Torch) > 0);
    Torch->ScriptComponent->StopAllEffects(); Torch->Destroy();

    // The owner policy must not remove the normal spell/portal light path.
    for (uint32 Setup : {0x02000001u, 0x020001B3u})
    {
        FACEWorldObject Object; Object.SetupId = Setup;
        auto* Owner = World->SpawnActor<AACEWorldEntityActor>(); Owner->InitializeFromObject(Object, 100, true);
        auto* FX = Owner->ScriptComponent.Get(); FX->StopAllEffects();
        TestTrue(TEXT("Ordinary entity and portal owners allow effect illumination"), FX->bAllowInferredParticleLights);
        FACEDatAnimationHook Hook; Hook.Type = EACEAnimationHookType::CreateParticle;
        Hook.Id = 0x3200075A; Hook.SecondaryId = 1; // Authored luminous pillar glow.
        FX->CreateEmitter(Hook, false, 1.f, 0x8Du);
        if (!TestEqual(TEXT("Luminous control creates an emitter"), FX->ActiveEmitters.Num(), 1)) return false;
        if (FX->ActiveEmitters[0].Particles.IsEmpty()) FX->SpawnParticle(FX->ActiveEmitters[0]);
        FX->TickParticleLights(); Lights->RefreshLights(.05f); FX->TickParticleLights(true);
        TestTrue(TEXT("Spell and portal control illumination survives"), CountLights(Owner) > 0);
        // Switching an existing component to an excluded environment must also hide pooled lights.
        FX->InitializeForEnvironment(100); FX->TickParticleLights(); FX->TickParticleLights(true);
        TestFalse(TEXT("Environment initialization disables effect illumination"), FX->bAllowInferredParticleLights);
        TestEqual(TEXT("Excluded environment retires existing light visibility"), CountLights(Owner), 0);
        FX->StopAllEffects(); Owner->Destroy();
    }

    Batches->Set(1, ECVF_SetByCode);
    TArray<AACERegionSceneryActor*> Dense;
    for (int32 Index = 0; Index < 64; ++Index)
    {
        auto* Owner = World->SpawnActor<AACERegionSceneryActor>();
        Owner->SetActorLocation(FVector((Index % 8) * 50, (Index / 8) * 50, 0));
        if (!TestTrue(TEXT("Dense snow fixture initializes actual scenery"), Owner->InitializeFromSetup(Cases[Index % 2].Setup, 1.f, 100.f, false, true))) return false;
        Dense.Add(Owner);
        for (int32 Frame = 0; Frame < 60; ++Frame) Owner->ScriptComponent->TickParticleSimulation(1.f / 60.f);
    }
    int32 Particles = 0, InitialLights = 0;
    for (auto* Owner : Dense)
    {
        InitialLights += CountLights(Owner);
        for (const auto& Emitter : Owner->ScriptComponent->ActiveEmitters) Particles += Emitter.Particles.Num();
    }
    TestTrue(TEXT("Dense measurement contains actual snow particles"), Particles > 0);
    TestEqual(TEXT("64 excluded snow owners receive no point lights"), InitialLights, 0);
    // Compare owner eligibility over identical particles. Even eligible snow must
    // obey the shared budget; RegionDesc snow remains excluded altogether.
    for (bool Eligible : {true, false})
    {
        for (auto* Owner : Dense)
        {
            auto* FX = Owner->ScriptComponent.Get(); FX->bAllowInferredParticleLights = Eligible;
            for (auto& Emitter : FX->ActiveEmitters)
                Dat->TryEstimateGfxLight(Emitter.Info.GfxObjId, Emitter.LightColor, Emitter.LightLum);
            FX->TickParticleLights();
        }
        Lights->RefreshLights(.05f);
        int32 VisibleLights = 0;
        for (auto* Owner : Dense) VisibleLights += CountLights(Owner);
        if (Eligible) TestTrue(TEXT("Eligible snow control shares at most eight world lights"), VisibleLights > 0 && VisibleLights <= 8);
        else TestEqual(TEXT("Scenery exclusion retires all budgeted snow lights"), VisibleLights, 0);
        TestTrue(TEXT("Dense scenery cannot grow the shared light pool beyond its budget"), Lights->GetAllocatedLightCount() <= 8);
    }
    for (auto* Owner : Dense) { Owner->ScriptComponent->StopAllEffects(); Owner->Destroy(); }
    return !HasAnyErrors();
}
#endif
