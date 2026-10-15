#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/PointLightComponent.h"
#include "ACEDatSubsystem.h"
#include "ACEEffectLightSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACEParticleBatchComponent.h"
#include "ACEScriptComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdWeaponLightingTest, "ACE.Rendering.CrowdWeaponLighting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECrowdWeaponLightingTest::RunTest(const FString&)
{
    auto* Budget = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.FXLightBudget"));
    auto* Mobile = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.FXLightBudgetMobile"));
    const int32 SavedBudget = Budget->GetInt(), SavedMobile = Mobile->GetInt();
    Budget->Set(8, ECVF_SetByCode); Mobile->Set(8, ECVF_SetByCode);
    ON_SCOPE_EXIT { Budget->Set(SavedBudget, ECVF_SetByCode); Mobile->Set(SavedMobile, ECVF_SetByCode); };
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance = GI;
    GI->OnWorldChanged(nullptr, World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    auto* Lights = World->GetSubsystem<UACEEffectLightSubsystem>();
    if (!TestNotNull(TEXT("World light pool exists"), Lights)
        || !TestTrue(TEXT("Weapon DAT opens"), Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    TArray<AACEWorldEntityActor*> Weapons;
    for (int32 Index = 0; Index < 64; ++Index)
    {
        FACEWorldObject Object; Object.Guid = 0x70010000 + Index; Object.SetupId = 0x020003CE; // Acid Staff.
        auto* Weapon = World->SpawnActor<AACEWorldEntityActor>(); Weapon->InitializeFromObject(Object, 100.f, true);
        Weapon->bAttachedToParent = true;
        Weapon->SetActorLocation(FVector((Index % 8) * 20, (Index / 8) * 20, 100));
        Weapons.Add(Weapon);
        Weapon->ScriptComponent->NotifyAppearanceReady(false);
    }
    for (int32 Frame = 0; Frame < 60; ++Frame)
    {
        for (auto* Weapon : Weapons) Weapon->ScriptComponent->TickComponent(1.f / 30.f, LEVELTICK_All, nullptr);
        Lights->RefreshLights(1.f / 30.f);
    }
    int32 Particles = 0, EmittingWeapons = 0, LuminousWeapons = 0;
    float TotalIntensity = 0.f;
    for (auto* Weapon : Weapons)
    {
        auto* FX = Weapon->ScriptComponent.Get();
        bool bParticlesVisible = false;
        for (const auto& Emitter : FX->ActiveEmitters)
        {
            Particles += Emitter.Particles.Num();
            if (!Emitter.Particles.IsEmpty()) bParticlesVisible = true;
            if (auto* Light = Lights->FindParticleLight(FX, Emitter.InstanceId))
            {
                TotalIntensity += Light->Intensity;
                TestEqual(TEXT("Held weapon light has a compact radius"), Light->AttenuationRadius, 320.f);
            }
        }
        EmittingWeapons += bParticlesVisible;
        LuminousWeapons += Lights->GetVisibleLightCount(FX) > 0;
    }
    TestEqual(TEXT("Every real weapon keeps its authored particles"), EmittingWeapons, Weapons.Num());
    TestTrue(TEXT("Many weapon particles remain visible despite bounded scene lights"), Particles >= Weapons.Num());
    TestTrue(TEXT("Real weapon crowd gets some illumination within eight light cap"), LuminousWeapons > 0 && LuminousWeapons <= 8);
    TestTrue(TEXT("Dense weapon illumination cannot multiply beyond overlap budget"), TotalIntensity <= 12.001f);
    TestTrue(TEXT("Pool allocation remains bounded"), Lights->GetAllocatedLightCount() <= 8);
    const double Start = FPlatformTime::Seconds();
    for (int32 Frame = 0; Frame < 128; ++Frame)
    {
        for (auto* Weapon : Weapons)
        {
            Weapon->ScriptComponent->TickParticleLights();
            Weapon->ScriptComponent->TickParticleLights(true);
        }
        Lights->RefreshLights(1.f / 90.f);
    }
    AddInfo(FString::Printf(TEXT("64 real Acid Staff effects: %d particles; %d lit weapons; %d allocated lights; %.4f ms for submissions, presentation lighting and shared pool per frame (128 samples, CPU only)"),
        Particles, LuminousWeapons, Lights->GetAllocatedLightCount(), (FPlatformTime::Seconds() - Start) * 1000 / 128));
    // Held illumination follows the hook even when old world-space sparks are
    // behind the wielder. Exercise both 30 Hz lifecycle updates and render-only
    // frames, at real Dereth coordinate magnitudes and while turning.
    for(int32 I=1;I<Weapons.Num();++I)Weapons[I]->ScriptComponent->StopAllEffects();
    auto* MovingFX=Weapons[0]->ScriptComponent.Get();
    for(int32 Fps:{30,60,144})
    {
        double MaxError=0;int32 Samples=0;
        for(int32 Frame=0;Frame<Fps;++Frame)
        {
            const double T=double(Frame)/Fps;
            Weapons[0]->SetActorLocationAndRotation(FVector(-2800000+T*700,2400000+T*180,100+T*60),FRotator(0,T*140,0));
            MovingFX->TickParticleSimulation(1.f/Fps);Lights->RefreshLights(1.f/Fps);
            for(const auto& Emitter:MovingFX->ActiveEmitters)
                if(auto* Light=Lights->FindParticleLight(MovingFX,Emitter.InstanceId))
                {++Samples;MaxError=FMath::Max(MaxError,FVector::Distance(Light->GetComponentLocation(),MovingFX->GetEmitterTransform(Emitter).GetLocation()));}
        }
        TestTrue(FString::Printf(TEXT("Held glow is sampled while running at %d Hz"),Fps),Samples>=Fps);
        TestTrue(FString::Printf(TEXT("Held glow follows the current hook without trailing at %d Hz (error %.6f cm)"),Fps,MaxError),MaxError<.01);
    }
    // Finite bursts keep illuminating the held weapon while their last sparks
    // fade, even after emission stops. Stopping is not detaching the weapon.
    for(const auto& Emitter:MovingFX->ActiveEmitters)MovingFX->StopEmitter(Emitter.InstanceId,false);
    Weapons[0]->AddActorWorldOffset(FVector(120,40,0));
    MovingFX->TickParticleSimulation(1.f/144);Lights->RefreshLights(1.f/144);
    int32 FadingLights=0;
    for(const auto& Emitter:MovingFX->ActiveEmitters)if(auto* Light=Lights->FindParticleLight(MovingFX,Emitter.InstanceId))
    {
        ++FadingLights;
        TestTrue(TEXT("Fading held-weapon illumination stays attached"),Light->GetComponentLocation().Equals(MovingFX->GetEmitterTransform(Emitter).GetLocation(),.01));
    }
    TestTrue(TEXT("Finite held-weapon burst retains a fading light"),FadingLights>0);
    // The persistent SetLight hook must not escape the cap when particle scripts stop ticking.
    auto* FX = Weapons[0]->ScriptComponent.Get();
    for (auto* Weapon : Weapons) Weapon->ScriptComponent->StopAllEffects();
    FACEDatAnimationHook Hook; Hook.Type = EACEAnimationHookType::SetLight; Hook.State = 1;
    FX->ExecuteHook(Hook, 1.f, 0);
    Lights->RefreshLights(.05f);
    TestEqual(TEXT("Authored hook uses shared pool"), Lights->GetVisibleLightCount(FX), 1);
    Weapons[0]->SetActorLocation(FVector(40, 60, 200));
    Lights->RefreshLights(.4f);
    auto* Script = Lights->FindScriptLight(FX);
    TestTrue(TEXT("Idle authored hook still follows its source"), Script && Script->GetComponentLocation().Equals(Weapons[0]->GetActorLocation(), .01));
    Hook.State = 0; FX->ExecuteHook(Hook, 1.f, 0);
    TestEqual(TEXT("Hook off retires light without waiting for another tick"), Lights->GetVisibleLightCount(), 0);
    for (auto* Weapon : Weapons) Weapon->Destroy();
    return !HasAnyErrors();
}
#endif
