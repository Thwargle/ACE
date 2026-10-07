#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "ImageUtils.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEScriptComponent.h"
#include "ACEParticleBatchComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEParticlePresentationTest, "ACE.Rendering.ParticlePresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEParticlePresentationTest::RunTest(const FString&)
{
    auto* BatchMode = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.Batched"));
    auto* ReuseMode = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.ReuseEmitterFrames"));
    auto* Culling = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.DistanceCulling"));
    const int32 SavedBatch = BatchMode->GetInt(), SavedReuse = ReuseMode->GetInt(), SavedCulling = Culling->GetInt();
    ON_SCOPE_EXIT { BatchMode->Set(SavedBatch, ECVF_SetByCode); ReuseMode->Set(SavedReuse, ECVF_SetByCode); Culling->Set(SavedCulling, ECVF_SetByCode); };
    Culling->Set(0, ECVF_SetByCode);
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance = GI;
    GI->OnWorldChanged(nullptr, World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT opens"), Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    auto* View = World->SpawnActor<ACameraActor>();
    auto* PC = World->SpawnActor<APlayerController>(); World->AddController(PC);
    if (!PC->PlayerCameraManager)
    {
        PC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
        PC->PlayerCameraManager->InitializeFor(PC);
    }
    PC->PlayerCameraManager->bUseClientSideCameraUpdates = false; PC->SetViewTarget(View);
    FACEWorldObject Human; Human.Guid = 0x71000A2; Human.SetupId = 0x02000001;
    Human.ItemType = ACEItemType::Creature; Human.bIsPlayer = true;
    auto* Actor = World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Human, 100, true);
    auto* FX = Actor->ScriptComponent.Get();
    auto* Torso = Actor->Appearance ? Actor->Appearance->GetPartMesh(9) : nullptr;
    if (!TestTrue(TEXT("Aetheria host has script component and real torso part"), FX && Torso)) return false;
    const FVector Origin(-2800000, 2400000, 10000); // Real Dereth coordinate magnitude catches batch precision loss.
    const FTransform TorsoBind = Torso->GetRelativeTransform();
    auto SetScene = [&](float Time)
    {
        // Uphill movement plus independent animated torso and camera elevation.
        Actor->SetActorLocationAndRotation(Origin + FVector(Time * 120, Time * 35, Time * 85),
            FRotator(Time * 16, 35 + Time * 43, Time * 9));
        Torso->SetRelativeTransform(FTransform(FRotator(Time * 11, Time * 27, Time * 7).Quaternion() * TorsoBind.GetRotation(),
            TorsoBind.GetLocation() + FVector(Time * 8, Time * 5, 120 + Time * 21), FVector::OneVector));
        View->SetActorLocation(Origin + FVector(-260 + Time * 95, -170 + Time * 120, 300 - Time * 185));
        View->SetActorRotation((Torso->GetComponentLocation() - View->GetActorLocation()).Rotation());
        PC->PlayerCameraManager->UpdateCamera(0.f);
    };
    SetScene(0);
    auto* Reference = NewObject<UProceduralMeshComponent>(Actor);
    auto* Capture = NewObject<USceneCaptureComponent2D>(View); Capture->RegisterComponent();
    auto* Target = NewObject<UTextureRenderTarget2D>(View);
    Target->ClearColor = FLinearColor::Black; Target->InitCustomFormat(384, 384, PF_FloatRGBA, true); Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target; Capture->FOVAngle = 50;
    Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false; Capture->CaptureSource = SCS_SceneColorHDR;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowFlags.SetAtmosphere(false); Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetAntiAliasing(false);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/ParticlePresentation");
    IFileManager::Get().MakeDirectory(*Directory, true);
    auto SaveCapture = [&](const UACEScriptComponent::FActiveEmitter& Emitter, int32 Frame)
    {
        Capture->SetWorldTransform(View->GetActorTransform()); Capture->ClearShowOnlyComponents();
        if (Emitter.Batch) Capture->ShowOnlyComponent(Emitter.Batch);
        for (const auto& P : Emitter.Particles) if (P.Mesh.IsValid()) Capture->ShowOnlyComponent(P.Mesh.Get());
        if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        for (auto& Pixel : Pixels) Pixel.A = 255;
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(384, 384, Pixels, PNG);
        FFileHelper::SaveArrayToFile(PNG, *(Directory / FString::Printf(TEXT("AetheriaSlope-%02d.png"), Frame)));
    };

    for (int32 Batch : {0, 1}) for (int32 Reuse : {0, 1}) for (int32 Fps : {60, 72, 90, 120, 144})
    for (bool ParentLocal : {true, false})
    {
        BatchMode->Set(Batch, ECVF_SetByCode); ReuseMode->Set(Reuse, ECVF_SetByCode);
        FX->StopAllEffects(); SetScene(0); FX->Random.Initialize(0xA2);
        FX->PlayScriptId(0x3300125B, 1.f); // PlayScript AetheriaSurgeDestruction (0xA2).
        int32 EmitterIndex = FX->ActiveEmitters.IndexOfByPredicate([](const auto& E) { return E.AuthoredEmitterId == 0x320009D1; });
        if (!TestTrue(TEXT("Real Aetheria script creates its authored glyph emitter"), EmitterIndex != INDEX_NONE)) return false;
        TArray<uint32> OtherEmitters;
        for (const auto& E : FX->ActiveEmitters) if (E.AuthoredEmitterId != 0x320009D1) OtherEmitters.Add(E.InstanceId);
        for (uint32 Id : OtherEmitters) FX->StopEmitter(Id, true);
        EmitterIndex = FX->ActiveEmitters.IndexOfByPredicate([](const auto& E) { return E.AuthoredEmitterId == 0x320009D1; });
        auto& Emitter = FX->ActiveEmitters[EmitterIndex];
        TestEqual(TEXT("Aetheria glyph is a swarm"), Emitter.Info.ParticleType, 5);
        TestEqual(TEXT("Aetheria glyph follows torso part 9"), Emitter.PartIndex, 9);
        TestEqual(TEXT("Aetheria glyph uses viewer-facing draw mode"), Emitter.DrawMode, 2u);
        TestTrue(TEXT("Authored glyph starts parent-local"), Emitter.Info.IsParentLocal != 0);
        if (Emitter.Particles.IsEmpty()) FX->SpawnParticle(Emitter);
        if (!TestTrue(TEXT("Aetheria glyph has a real particle visual"), !Emitter.Particles.IsEmpty())) return false;
        // Keep this birth alive across several quanta; no new hooks affect the isolated motion oracle.
        FX->ActiveScripts.Reset();
        if (!TestTrue(TEXT("Actual DAT glyph lifetime spans the motion probe"), Emitter.Particles[0].Life > .2f)) return false;
        for (auto& P : Emitter.Particles) P.bParentLocal = ParentLocal;
        if (!TestEqual(TEXT("Requested real particle rendering path is active"), bool(Emitter.Batch), Batch != 0)) return false;
        // Local first-person visibility/appearance hooks act on the body only.
        // Batched effects must be isolated just like individual particle meshes.
        auto* ParticleVisual=Batch?static_cast<UProceduralMeshComponent*>(Emitter.Batch.Get()):Emitter.Particles[0].Mesh.Get();
        float BeforeOpacity=0,AfterOpacity=0;
        if(ParticleVisual && ParticleVisual->GetMaterial(0))
        {
            ParticleVisual->GetMaterial(0)->GetScalarParameterValue(TEXT("OpacityMul"),BeforeOpacity);
            FX->ApplyMaterialScalar(INDEX_NONE,TEXT("OpacityMul"),0.f);
            ParticleVisual->GetMaterial(0)->GetScalarParameterValue(TEXT("OpacityMul"),AfterOpacity);
            TestEqual(TEXT("Body transparency hooks never hide Aetheria particles"),AfterOpacity,BeforeOpacity);
            FX->ApplyMaterialScalar(INDEX_NONE,TEXT("OpacityMul"),1.f);
        }
        Reference->ClearAllMeshSections();
        if (!TestTrue(TEXT("Untransformed DAT glyph geometry loads for independent vertex oracle"),
            Dat->ApplyParticleGfxToProceduralMesh(Reference, Emitter.Info.GfxObjId, 100, false))) return false;
        const auto Birth = Emitter.Particles[0];
        const float Dt = 1.f / Fps;
        float Elapsed = 0;
        int32 SubquantumFrames = 0, BoundaryFrames = 0;
        double MaxVertexError = 0, MaxPositionError = 0;
        bool LifecycleUnchanged = true, FacingCorrect = true, FadeCorrect = true, ContinuousClock = true;
        bool OrbitAdvanced = true;
        float PreviousPhase = 0;
        for (int32 Frame = 1; Frame <= FMath::CeilToInt(.15f * Fps); ++Frame)
        {
            const float BeforeAge = Emitter.Particles[0].Age, BeforeEmitterAge = Emitter.Age, BeforeEmitTime = Emitter.TimeSinceEmit;
            const int32 BeforeBorn = Emitter.TotalBorn, BeforeSeed = FX->Random.GetCurrentSeed();
            const FVector BeforeOrigin = Emitter.LastOrigin;
            const bool Subquantum = FX->ParticleTimeSinceUpdate + Dt < 1.0 / 30.0;
            Elapsed += Dt; SetScene(Elapsed);
            FX->TickParticleSimulation(Dt);
            if (Emitter.Particles.IsEmpty()) { AddError(TEXT("Aetheria birth expired before bounded motion probe")); return false; }
            const auto& P = Emitter.Particles[0];
            if (Subquantum)
            {
                ++SubquantumFrames;
                LifecycleUnchanged &= P.Age == BeforeAge && Emitter.Age == BeforeEmitterAge
                    && Emitter.TimeSinceEmit == BeforeEmitTime && Emitter.TotalBorn == BeforeBorn
                    && Emitter.LastOrigin == BeforeOrigin && FX->Random.GetCurrentSeed() == BeforeSeed;
            }
            else ++BoundaryFrames;
            const float T = Birth.Age + Elapsed;
            ContinuousClock &= FMath::IsNearlyEqual(P.Age + float(FX->ParticleTimeSinceUpdate), T, .00001f);
            const FVector Anchor = ParentLocal ? Torso->GetComponentLocation() : Birth.StartOrigin;
            const FVector Orbit = FACEPosition::AceVectorToUnreal(FVector(
                Birth.C.X * FMath::Cos(Birth.B.X * T), Birth.C.Y * FMath::Sin(Birth.B.Y * T), Birth.C.Z * FMath::Cos(Birth.B.Z * T)), 100);
            const FVector ExpectedPosition = Anchor + Birth.Offset + T * Birth.A + Orbit;
            const FQuat ExpectedFacing = FRotationMatrix::MakeFromYZ(
                (ExpectedPosition - View->GetActorLocation()).GetSafeNormal(), FVector::UpVector).ToQuat();
            const float Alpha = FMath::Clamp(P.Age / P.Life, 0.f, 1.f);
            const float Scale = FMath::Lerp(P.StartScale, P.FinalScale, Alpha);
            const float Opacity = 1.f - FMath::Lerp(P.StartTrans, P.FinalTrans, Alpha);
            const FTransform Expected(ExpectedFacing, ExpectedPosition, FVector(Scale));
            FVector ActualPosition = ExpectedPosition;
            if (Batch)
            {
                for (int32 Section = 0; Section < Reference->GetNumSections(); ++Section)
                {
                    const auto* Source = Reference->GetProcMeshSection(Section);
                    const auto* Rendered = Emitter.Batch->GetProcMeshSection(Section);
                    for (int32 V = 0; V < Source->ProcVertexBuffer.Num(); ++V)
                    {
                        const auto& Out = Rendered->ProcVertexBuffer[P.InstanceIndex * Source->ProcVertexBuffer.Num() + V];
                        const FVector Actual = Emitter.Batch->GetComponentTransform().TransformPosition(Out.Position);
                        MaxVertexError = FMath::Max(MaxVertexError, FVector::Distance(Actual, Expected.TransformPosition(Source->ProcVertexBuffer[V].Position)));
                        FadeCorrect &= FMath::Abs(int32(Out.Color.A) - FMath::RoundToInt(Opacity * 255.f)) <= 1;
                    }
                }
            }
            else if (auto* Mesh = P.Mesh.Get())
            {
                ActualPosition = Mesh->GetComponentLocation();
                MaxPositionError = FMath::Max(MaxPositionError, FVector::Distance(ActualPosition, ExpectedPosition));
                FacingCorrect &= Mesh->GetComponentQuat().Equals(ExpectedFacing, .0001);
                FadeCorrect &= Mesh->GetComponentScale().Equals(FVector(Scale), .0001);
                auto* Mid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
                FadeCorrect &= Mid && FMath::IsNearlyEqual(Mid->K2_GetScalarParameterValue(TEXT("OpacityMul")), Opacity, .0001f);
            }
            else { AddError(TEXT("Unbatched glyph lost its mesh")); return false; }
            if (!Batch)
            {
                const FVector ActualOrbit = ActualPosition - Anchor - Birth.Offset - T * Birth.A;
                const float Phase = FMath::Atan2(ActualOrbit.Y, -ActualOrbit.X);
                if (Frame > 1) OrbitAdvanced &= Phase > PreviousPhase;
                PreviousPhase = Phase;
            }
            if (Batch == 1 && Reuse == 1 && Fps == 60 && ParentLocal && (Frame == 2 || Frame == 3 || Frame == 8))
                SaveCapture(Emitter, Frame);
        }
        const FString Case = FString::Printf(TEXT("%dHz batch%d cache%d parent%d"), Fps, Batch, Reuse, ParentLocal);
        TestTrue(Case + TEXT(" actually spans intermediate and lifecycle frames"), SubquantumFrames > 0 && BoundaryFrames > 0);
        TestTrue(Case + TEXT(" presentation never advances lifecycle or RNG"), LifecycleUnchanged);
        TestTrue(Case + TEXT(" render time stays continuous across lifecycle boundaries"), ContinuousClock);
        TestTrue(Case + FString::Printf(TEXT(" analytic position error %.6f"), MaxPositionError), MaxPositionError < .01);
        TestTrue(Case + FString::Printf(TEXT(" complete batch pose error %.6f"), MaxVertexError), MaxVertexError < .01);
        TestTrue(Case + TEXT(" current hillside camera controls billboard rotation"), FacingCorrect);
        TestTrue(Case + TEXT(" scale and fade remain at lifecycle age"), FadeCorrect);
        TestTrue(Case + TEXT(" glyph orbit never freezes or steps backward at a quantum"), OrbitAdvanced);
        if (Fps == 90 && ParentLocal)
        {
            // Log a bounded CPU sample for the real surge, not a timing threshold
            // that varies with machine load. Vary render time to exercise uploads.
            const int32 Seed = FX->Random.GetCurrentSeed(), Born = Emitter.TotalBorn;
            const float Age = Emitter.Age, ParticleAge = Emitter.Particles[0].Age;
            FlushRenderingCommands();
            const double Start = FPlatformTime::Seconds();
            for (int32 Sample = 0; Sample < 1000; ++Sample)
            {
                FX->TickParticlePresentation(float(Sample % 4) / 240.f);
                FX->TickParticleLights(true);
            }
            AddInfo(FString::Printf(TEXT("Aetheria presentation batch%d cache%d: %.5f ms/call, %d actual DAT glyph particles, 1000 calls (CPU only)"),
                Batch, Reuse, (FPlatformTime::Seconds() - Start) * 1000 / 1000, Emitter.Particles.Num()));
            TestTrue(Case + TEXT(" repeated presentation keeps simulation and RNG stable"),
                Emitter.Age == Age && Emitter.Particles[0].Age == ParticleAge && Emitter.TotalBorn == Born && FX->Random.GetCurrentSeed() == Seed);
        }
    }
    // Compare the local plain pawn with a remote entity, using complete DAT effects
    // and real casting motions. Cleanup must not erase the independent surge slot.
    FX->StopAllEffects();Human.MotionTableId=0x09000001;Human.bIsSelf=true;
    auto* LocalPawn=World->SpawnActor<APawn>();
    auto* LocalRoot=NewObject<USceneComponent>(LocalPawn);LocalPawn->AddInstanceComponent(LocalRoot);
    LocalPawn->SetRootComponent(LocalRoot);LocalRoot->RegisterComponent();
    auto* LocalAppearance=NewObject<UACECharacterAppearanceComponent>(LocalPawn);
    LocalPawn->AddInstanceComponent(LocalAppearance);LocalAppearance->RegisterComponent();
    LocalAppearance->ApplyWorldObject(Human,100,false);
    auto* LocalFX=NewObject<UACEScriptComponent>(LocalPawn);LocalPawn->AddInstanceComponent(LocalFX);LocalFX->RegisterComponent();
    LocalFX->InitializeFromObject(Human,100);LocalFX->NotifyAppearanceReady();
    Human.bIsSelf=false;
    auto* Remote=World->SpawnActor<AACEWorldEntityActor>();Remote->InitializeFromObject(Human,100,true);
    auto* RemoteFX=Remote->ScriptComponent.Get();
    LocalFX->StopAllEffects();RemoteFX->StopAllEffects();
    LocalFX->Random.Initialize(42);RemoteFX->Random.Initialize(42);
    LocalFX->PlayEffect(0xA2,1.f);RemoteFX->PlayEffect(0xA2,1.f);
    int32 LastSurgeFrame=0;
    for (int32 Frame=0;Frame<1500;++Frame)
    {
        if(Frame==60) LocalAppearance->PlayActionMotion(0x400000d3,1.f,0x80000049);
        if(Frame==120) { LocalAppearance->ClearActionMotion();LocalFX->StopCastGestureEffects(); }
        if(Frame==240) LocalAppearance->PlayActionMotion(0x40000032,1.f,0x80000049);
        LocalAppearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
        if(Frame==180) { LocalFX->InitializeFromObject(Human,100);LocalFX->NotifyAppearanceReady(); }
        LocalFX->TickScripts(1.f/60);RemoteFX->TickScripts(1.f/60);
        LocalFX->TickParticleSimulation(1.f/60);RemoteFX->TickParticleSimulation(1.f/60);
        const auto* OwnSurge=LocalFX->ActiveEmitters.FindByPredicate([](const auto& E){return E.SourcePlayScript==0xA2;});
        const auto* OtherSurge=RemoteFX->ActiveEmitters.FindByPredicate([](const auto& E){return E.SourcePlayScript==0xA2;});
        if(OwnSurge)LastSurgeFrame=Frame;
        if(Frame%60==59)
        {
            TestEqual(TEXT("Local cast cleanup and appearance refresh preserve surge lifetime"),OwnSurge!=nullptr,OtherSurge!=nullptr);
            if(OwnSurge && OtherSurge)TestEqual(TEXT("Local surge retains the same visible particle count"),OwnSurge->Particles.Num(),OtherSurge->Particles.Num());
        }
    }
    TestTrue(TEXT("DAT surge survives the reported first few seconds"),LastSurgeFrame>600);
    TestTrue(TEXT("Finite surge finishes rather than becoming permanent"),LastSurgeFrame<1499);
    AddInfo(FString::Printf(TEXT("Complete DAT surge lifetime including particle tail: %.2f seconds"),float(LastSurgeFrame)/60));
    LocalFX->StopAllEffects();LocalPawn->Destroy();RemoteFX->StopAllEffects();Remote->Destroy();
    return !HasAnyErrors();
}
#endif
