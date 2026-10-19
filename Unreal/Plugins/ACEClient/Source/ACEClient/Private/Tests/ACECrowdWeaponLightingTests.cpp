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
#include "ACECharacterAppearanceComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDualWieldLightingTest, "ACE.Rendering.DualWieldLighting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEDualWieldLightingTest::RunTest(const FString&)
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
    FACEWorldObject Player; Player.Guid = 0x50000001; Player.SetupId = 0x02000001; Player.ItemType = ACEItemType::Creature;
    auto* Body = World->SpawnActor<AACEWorldEntityActor>(); Body->InitializeFromObject(Player, 100.f, true);
    auto* View = World->SpawnActor<ACameraActor>();
    auto* PC = World->SpawnActor<APlayerController>(); World->AddController(PC);
    if (!PC->PlayerCameraManager)
    {
        PC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
        PC->PlayerCameraManager->InitializeFor(PC);
    }
    PC->SetViewTarget(View);
    TArray<AACEWorldEntityActor*> Weapons;
    for (int32 Hand = 0; Hand < 2; ++Hand)
    {
        FACEWorldObject Item; Item.Guid = 0x70010001 + Hand; Item.ItemType = ACEItemType::MeleeWeapon;
        Item.SetupId = Hand == 0 ? 0x02000615 : 0x02000616; // Flaming and Frost Nekode.
        Item.ParentGuid = Player.Guid; Item.ParentLocation = Hand == 0 ? 1 : 8;
        auto* Weapon = World->SpawnActor<AACEWorldEntityActor>(); Weapon->InitializeFromObject(Item, 100.f, true);
        Weapon->AttachToParentActor(Body, Item.ParentLocation);
        Weapon->ScriptComponent->NotifyAppearanceReady(false); Weapons.Add(Weapon);
        TestTrue(TEXT("Weapon uses its actual animated hand"), Weapon->IsAttachedToParent() && !Weapon->IsHidden());
    }
    for (uint32 OffhandSetup : {0x02000616u, 0x02000617u, 0x02001028u}) // Frost, lightning, Quintessence Sickle.
    {
        FACEWorldObject Offhand; Offhand.Guid = 0x70010002; Offhand.ItemType = ACEItemType::MeleeWeapon;
        Offhand.SetupId = OffhandSetup; Offhand.ParentGuid = Player.Guid; Offhand.ParentLocation = 8;
        Weapons[1]->InitializeFromObject(Offhand, 100.f, true);
        Weapons[1]->AttachToParentActor(Body, 8); Weapons[0]->AttachToParentActor(Body, 1);
        Weapons[1]->ScriptComponent->NotifyAppearanceReady(false);
        for (int32 Swap = 0; Swap < 2; ++Swap)
        {
            int32 LitFrames[2] = {};
            for (int32 Frame = 0; Frame < 300; ++Frame)
            {
                Body->SetActorLocationAndRotation(FVector(Frame * 2, 0, 0), FRotator(0, Frame, 0));
                for (auto* Weapon : Weapons) Weapon->ScriptComponent->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
                // Look from the Sickle's black mesh emitter towards its purple sparks.
                // A black translucent effect must not win illumination by proximity.
                auto* OffhandFX = Weapons[1]->ScriptComponent.Get();
                if (!OffhandFX->ActiveEmitters.IsEmpty())
                    View->SetActorLocation(OffhandFX->GetEmitterTransform(OffhandFX->ActiveEmitters.Last()).GetLocation() + FVector(0, 0, 1));
                PC->PlayerCameraManager->UpdateCamera(0.f);
                Lights->RefreshLights(1.f / 60.f);
                if (Frame < 60) continue;
                for (int32 Hand = 0; Hand < 2; ++Hand)
                    for (const auto& E : Weapons[Hand]->ScriptComponent->ActiveEmitters)
                        if (auto* Light = Lights->FindParticleLight(Weapons[Hand]->ScriptComponent, E.InstanceId))
                            LitFrames[Hand] += Light->Intensity > 0 && Light->GetLightColor().GetLuminance() > .001f;
            }
            for (int32 Hand = 0; Hand < 2; ++Hand)
            {
                TestEqual(FString::Printf(TEXT("Setup %08X weapon %d lights every moving frame, hand swap %d"),
                    OffhandSetup, Hand, Swap), LitFrames[Hand], 240);
                for (const auto& E : Weapons[Hand]->ScriptComponent->ActiveEmitters)
                    if (auto* Light = Lights->FindParticleLight(Weapons[Hand]->ScriptComponent, E.InstanceId))
                    {
                        const auto Color = Light->GetLightColor();
                        TestTrue(TEXT("Carried glow is not diluted by the texture's dark padding"),
                            FMath::Max3(Color.R, Color.G, Color.B) > .99f);
                        TestTrue(TEXT("Glow follows its current hand while running and turning"),
                            Light->GetComponentLocation().Equals(Weapons[Hand]->ScriptComponent->GetEmitterTransform(E).GetLocation(), .01));
                    }
            }
            Weapons[0]->AttachToParentActor(Body, 8); Weapons[1]->AttachToParentActor(Body, 1);
        }
    }
    // Verify actual rendered illumination, not just point-light allocation.
    // Isolate neutral floor/body receivers from the self-lit particle sprites.
    if (FApp::CanEverRender())
    {
        auto* ReceiverActor = World->SpawnActor<AActor>();
        auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        auto MakeReceiver = [&](const TCHAR* Asset, FVector Offset, FVector Scale)
        {
            auto* Receiver = NewObject<UStaticMeshComponent>(ReceiverActor);
            ReceiverActor->AddInstanceComponent(Receiver);
            Receiver->SetMobility(EComponentMobility::Movable);
            Receiver->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Asset)); Receiver->SetMaterial(0, Material);
            Receiver->SetWorldLocation(Body->GetActorLocation() + Offset); Receiver->SetWorldScale3D(Scale);
            Receiver->SetCastShadow(false); Receiver->RegisterComponent();
        };
        MakeReceiver(TEXT("/Engine/BasicShapes/Plane.Plane"), FVector::ZeroVector, FVector(6.f));
        MakeReceiver(TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0, 0, 80), FVector(.5, .5, 1.6));
        constexpr int32 Size = 128;
        auto* Target = NewObject<UTextureRenderTarget2D>(ReceiverActor);
        Target->InitCustomFormat(Size, Size, PF_FloatRGBA, false);
        auto* Capture = NewObject<USceneCaptureComponent2D>(ReceiverActor);
        Capture->TextureTarget = Target; Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false;
        Capture->CaptureSource = SCS_SceneColorHDRNoAlpha;
        Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        Capture->ShowOnlyActorComponents(ReceiverActor);
        Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetBloom(false);
        Capture->ShowFlags.SetAtmosphere(false); Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetAntiAliasing(false);
        const FVector CameraOffset(-260, -320, 300);
        Capture->SetWorldLocationAndRotation(Body->GetActorLocation() + CameraOffset,
            (FVector(0, 0, 50) - CameraOffset).Rotation());
        Capture->RegisterComponent();
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/DualWieldLighting");
        IFileManager::Get().MakeDirectory(*Directory, true);
        auto Read = [&](const TCHAR* Name)
        {
            if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
            World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
            TArray<FLinearColor> Pixels;
            Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
            TArray<FColor> Display;
            for (const auto& P : Pixels) Display.Add(P.ToFColor(true));
            if (Display.Num() == Size * Size)
            {
                TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size, Size, Display, PNG);
                FFileHelper::SaveArrayToFile(PNG, *(Directory / Name));
            }
            return Pixels;
        };
        auto* FireFX = Weapons[0]->ScriptComponent.Get();
        auto* SickleFX = Weapons[1]->ScriptComponent.Get();
        // End the swap test with the Sickle in the offhand.
        Weapons[0]->AttachToParentActor(Body, 1); Weapons[1]->AttachToParentActor(Body, 8);
        Lights->RemoveSource(FireFX); Lights->RemoveSource(SickleFX); Lights->RefreshLights(.05f);
        const auto Dark = Read(TEXT("Dark.png"));
        FireFX->TickParticleLights(); Lights->RefreshLights(.05f);
        const auto MainOnly = Read(TEXT("MainHand.png"));
        SickleFX->TickParticleLights(); FireFX->TickParticleLights(); Lights->RefreshLights(.05f);
        const auto Both = Read(TEXT("BothHands.png"));
        Lights->RemoveSource(FireFX); SickleFX->TickParticleLights(); Lights->RefreshLights(.05f);
        const auto OffOnly = Read(TEXT("OffHandSickle.png"));
        if (!TestTrue(TEXT("Dual wield captures contain a full frame"), Dark.Num() == Size * Size
            && MainOnly.Num() == Dark.Num() && Both.Num() == Dark.Num() && OffOnly.Num() == Dark.Num())) return false;
        double MainEnergy = 0, OffEnergy = 0, BlueContribution = 0, RedContribution = 0;
        for (int32 I = 0; I < Dark.Num(); ++I)
        {
            MainEnergy += FMath::Max(0.f, MainOnly[I].GetLuminance() - Dark[I].GetLuminance());
            OffEnergy += FMath::Max(0.f, OffOnly[I].GetLuminance() - Dark[I].GetLuminance());
            BlueContribution += FMath::Max(0.f, Both[I].B - MainOnly[I].B);
            RedContribution += FMath::Max(0.f, Both[I].R - OffOnly[I].R);
        }
        TestTrue(TEXT("Main-hand weapon illuminates neutral receivers"), MainEnergy > 1.);
        TestTrue(TEXT("Offhand Sickle illuminates neutral receivers"), OffEnergy > 1.);
        TestTrue(TEXT("Both hands contribute visible color simultaneously"), BlueContribution > 1. && RedContribution > 1.);
        AddInfo(FString::Printf(TEXT("Rendered energy: main %.3f, offhand %.3f; combined red %.3f, blue %.3f. Captures: %s"),
            MainEnergy, OffEnergy, RedContribution, BlueContribution, *Directory));
    }
    return !HasAnyErrors();
}
#endif
