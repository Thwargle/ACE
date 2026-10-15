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
    // Compare the production player pawn with a remote entity. Cast cleanup and
    // appearance refreshes must preserve independent, server-triggered effects.
    FX->StopAllEffects();Human.MotionTableId=0x09000001;Human.bIsSelf=true;
    auto* PawnClass=LoadClass<APawn>(nullptr,TEXT("/Script/ACUnreal.ACUnrealPawn"));
    if(!TestNotNull(TEXT("Production player pawn class loads"),PawnClass))return false;
    auto* LocalPawn=World->SpawnActor<APawn>(PawnClass);
    auto* LocalAppearance=LocalPawn->FindComponentByClass<UACECharacterAppearanceComponent>();
    auto* LocalFX=LocalPawn->FindComponentByClass<UACEScriptComponent>();
    if(!TestTrue(TEXT("Player pawn has appearance and effect components"),LocalAppearance && LocalFX))return false;
    LocalAppearance->ApplyWorldObject(Human,100,false);
    LocalFX->InitializeFromObject(Human,100);LocalFX->NotifyAppearanceReady();
    Human.bIsSelf=false;
    auto* Remote=World->SpawnActor<AACEWorldEntityActor>();Remote->InitializeFromObject(Human,100,true);
    auto* RemoteFX=Remote->ScriptComponent.Get();
    Culling->Set(1, ECVF_SetByCode);
    LocalPawn->SetActorLocation(Origin);Remote->SetActorLocation(Origin);
    View->SetActorLocation(Origin+FVector(-350,0,180));
    PC->PlayerCameraManager->UpdateCamera(0.f);
    for(uint32 Surge=0xA2;Surge<=0xA6;++Surge)
    {
        const FString Case=FString::Printf(TEXT("Surge 0x%X: "),Surge);
        LocalAppearance->ClearActionMotion();
        LocalFX->StopAllEffects();RemoteFX->StopAllEffects();
        LocalFX->Random.Initialize(42);RemoteFX->Random.Initialize(42);
        LocalFX->PlayEffect(Surge,1.f);RemoteFX->PlayEffect(Surge,1.f);
        int32 LastSurgeFrame=0;
        for (int32 Frame=0;Frame<2100;++Frame)
        {
            if(Frame==60) LocalAppearance->PlayActionMotion(0x400000d3,1.f,0x80000049);
            if(Frame==120) LocalAppearance->ClearActionMotion();
            if(Frame==240) LocalAppearance->PlayActionMotion(0x40000032,1.f,0x80000049);
            LocalAppearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            if(Frame==180) { LocalFX->InitializeFromObject(Human,100);LocalFX->NotifyAppearanceReady(); }
            if(Frame%60==0) {LocalAppearance->SetAppearanceVisible(true);LocalFX->RestoreMeshVisuals();}
            LocalFX->TickComponent(1.f/60,LEVELTICK_All,nullptr);RemoteFX->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            const auto* OwnSurge=LocalFX->ActiveEmitters.FindByPredicate([Surge](const auto& E){return E.SourcePlayScript==Surge;});
            const auto* OtherSurge=RemoteFX->ActiveEmitters.FindByPredicate([Surge](const auto& E){return E.SourcePlayScript==Surge;});
            if(OwnSurge)LastSurgeFrame=Frame;
            if(Frame%60==59)
            {
                TestEqual(Case+TEXT("local cast cleanup and appearance refresh preserve surge lifetime"),OwnSurge!=nullptr,OtherSurge!=nullptr);
                if(OwnSurge && OtherSurge)TestEqual(Case+TEXT("local surge retains the same visible particle count"),OwnSurge->Particles.Num(),OtherSurge->Particles.Num());
                if(OwnSurge && OwnSurge->Batch) TestTrue(Case+TEXT("local surge batch stays visible"),OwnSurge->Batch->IsVisible() && !OwnSurge->Batch->bHiddenInGame);
            }
        }
        TestTrue(Case+TEXT("DAT surge survives the reported first few seconds"),LastSurgeFrame>600);
        TestTrue(Case+TEXT("finite surge finishes rather than becoming permanent"),LastSurgeFrame<2099);
        AddInfo(Case+FString::Printf(TEXT("complete DAT lifetime including particle tail: %.2f seconds"),float(LastSurgeFrame)/60));

        // Full level-8 windups reuse every surge slot (5..9). The earlier test
        // incorrectly accepted losing the surge here. Compare both casting
        // bodies against an idle DAT-driven reference through natural expiry.
        LocalAppearance->ClearActionMotion();Remote->Appearance->ClearActionMotion();
        LocalFX->StopAllEffects();RemoteFX->StopAllEffects();FX->StopAllEffects();
        LocalFX->PlayEffect(Surge,1.f);RemoteFX->PlayEffect(Surge,1.f);FX->PlayEffect(Surge,1.f);
        const auto* InitialSurge=LocalFX->ActiveEmitters.FindByPredicate([Surge](const auto& E){return E.SourcePlayScript==Surge;});
        if(!TestNotNull(Case+TEXT("surge creates its DAT slot"),InitialSurge))return false;
        const uint32 Slot=InitialSurge->SlotId;
        bool SameLifetime=true,SamePopulation=true,Visible=true,SameAge=true,CastOverlap=false;
        int32 LastCastingFrame=0,OrbitSamples=0;
        double MaxCastingOrbitError=0;
        for(int32 Frame=0;Frame<2100;++Frame)
        {
            if(Frame==60 || Frame==240 || Frame==420 || Frame==600)
            {
                LocalAppearance->PlayActionMotion(0x10000076,2.f,0x80000049);
                Remote->Appearance->PlayActionMotion(0x10000076,2.f,0x80000049);
                LocalAppearance->QueueActionMotion(0x40000032,2.f,0x80000049);
                Remote->Appearance->QueueActionMotion(0x40000032,2.f,0x80000049);
            }
            // Cancel one cast and refresh the local body while the surge runs.
            if(Frame==500) LocalAppearance->ClearActionMotion();
            if(Frame==510) { LocalFX->InitializeFromObject(Human,100);LocalFX->NotifyAppearanceReady(); }
            LocalAppearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            Remote->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            FX->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            LocalFX->TickComponent(1.f/60,LEVELTICK_All,nullptr);RemoteFX->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            const auto* ReferenceSurge=FX->ActiveEmitters.FindByPredicate([Surge](const auto& E){return E.SourcePlayScript==Surge;});
            const auto* OwnSurge=LocalFX->ActiveEmitters.FindByPredicate([Surge](const auto& E){return E.SourcePlayScript==Surge;});
            if(OwnSurge) LastCastingFrame=Frame;
            for(auto* CastingFX:{LocalFX,RemoteFX})
            {
                const auto* CastingSurge=CastingFX->ActiveEmitters.FindByPredicate([Surge](const auto& E){return E.SourcePlayScript==Surge;});
                SameLifetime &= (CastingSurge!=nullptr)==(ReferenceSurge!=nullptr);
                if(!CastingSurge || !ReferenceSurge) continue;
                SamePopulation &= CastingSurge->Particles.Num()==ReferenceSurge->Particles.Num();
                SameAge &= FMath::IsNearlyEqual(CastingSurge->Age,ReferenceSurge->Age,.001f);
                if(CastingSurge->Batch) Visible &= CastingSurge->Batch->IsVisible() && !CastingSurge->Batch->bHiddenInGame;
                for(const auto& P:CastingSurge->Particles) if(P.Mesh.IsValid()) Visible &= P.Mesh->IsVisible() && !P.Mesh->bHiddenInGame;
                CastOverlap |= CastingFX->ActiveEmitters.ContainsByPredicate([Slot](const auto& E){return E.SlotId==Slot && E.SourcePlayScript==0;});
                if(Frame%30==0) for(const auto& P:CastingSurge->Particles)
                {
                    const float T=P.Age+float(CastingFX->ParticleTimeSinceUpdate);
                    const FVector Anchor=P.bParentLocal ? CastingFX->GetParticleStartFrame(*CastingSurge).GetLocation() : P.StartOrigin;
                    const FVector Orbit=FACEPosition::AceVectorToUnreal(FVector(P.C.X*FMath::Cos(P.B.X*T),
                        P.C.Y*FMath::Sin(P.B.Y*T),P.C.Z*FMath::Cos(P.B.Z*T)),100);
                    MaxCastingOrbitError=FMath::Max(MaxCastingOrbitError,FVector::Distance(P.Position,Anchor+P.Offset+T*P.A+Orbit));
                    ++OrbitSamples;
                }
            }
            if(Surge==0xA2 && Frame==480 && OwnSurge)
            {
                View->SetActorRotation((LocalAppearance->GetPartMesh(9)->GetComponentLocation()-View->GetActorLocation()).Rotation());
                SaveCapture(*OwnSurge,480);
            }
        }
        TestTrue(Case+TEXT("full windups, releases and cancellation preserve the complete surge lifetime"),SameLifetime);
        TestTrue(Case+TEXT("casting preserves every authored surge particle"),SamePopulation);
        TestTrue(Case+TEXT("casting does not restart or extend the surge clock"),SameAge);
        TestTrue(Case+TEXT("surge particles remain visible while casting"),Visible);
        TestTrue(Case+TEXT("surge continues its DAT orbit throughout casting"),OrbitSamples>20 && MaxCastingOrbitError<.05);
        TestTrue(Case+TEXT("cast visuals and surge coexist even when their authored slots collide"),CastOverlap);
        TestEqual(Case+TEXT("casting surge expires at the same frame as the idle baseline"),LastCastingFrame,LastSurgeFrame);
        TestTrue(Case+TEXT("surge and cast particles all finish naturally"),LocalFX->ActiveEmitters.IsEmpty() && RemoteFX->ActiveEmitters.IsEmpty());

        // Reprocs refresh only their own visual, including direct-DID playback.
        LocalFX->PlayEffect(Surge,1.f);
        LocalFX->TickComponent(.5f,LEVELTICK_All,nullptr);
        FACEDatAnimationHook CastHook; CastHook.Type=EACEAnimationHookType::CreateParticle;
        CastHook.Id=0x32000122; CastHook.SecondaryId=Slot; CastHook.PartIndex=0xFFFFFFFFu;
        LocalFX->DispatchAnimationHook(CastHook);
        const uint32 ScriptId=0x3300125Bu+(Surge-0xA2u);
        LocalFX->PlayScriptId(ScriptId,1.f);
        const uint32 EmitterId=0x320009D1u+(Surge-0xA2u);
        int32 SurgeCount=0;
        for(const auto& E:LocalFX->ActiveEmitters) if(E.AuthoredEmitterId==EmitterId) ++SurgeCount;
        TestEqual(Case+TEXT("a reproc replaces its previous surge without stacking"),SurgeCount,1);
        TestTrue(Case+TEXT("a reproc does not remove the casting emitter"),LocalFX->ActiveEmitters.ContainsByPredicate(
            [Slot](const auto& E){return E.InstanceId==Slot && E.SourcePlayScript==0;}));
        FACEDatAnimationHook StopHook; StopHook.Type=EACEAnimationHookType::StopParticle; StopHook.Id=Slot;
        LocalFX->DispatchAnimationHook(StopHook);
        StopHook.Type=EACEAnimationHookType::DestroyParticle; LocalFX->DispatchAnimationHook(StopHook);
        TestTrue(Case+TEXT("cast stop/destroy hooks leave the surge running"),LocalFX->ActiveEmitters.ContainsByPredicate(
            [EmitterId](const auto& E){return E.AuthoredEmitterId==EmitterId && !E.bStopped;}));
        LocalFX->ExecuteHook(StopHook,1.f,0,ScriptId);
        TestFalse(Case+TEXT("a surge's own destroy hook still works"),LocalFX->ActiveEmitters.ContainsByPredicate(
            [EmitterId](const auto& E){return E.AuthoredEmitterId==EmitterId;}));

        // Direct/default-script entry points receive the same protection.
        LocalAppearance->ClearActionMotion();
        LocalFX->StopAllEffects();RemoteFX->StopAllEffects();
        LocalFX->PlayScriptInternal(ScriptId,1.f,0,0.f);RemoteFX->PlayScriptInternal(ScriptId,1.f,0,0.f);
        LocalAppearance->PlayActionMotion(0x40000032,2.f,0x80000049);
        for(int32 Frame=0;Frame<360;++Frame)
        {
            LocalAppearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            LocalFX->TickComponent(1.f/60,LEVELTICK_All,nullptr);RemoteFX->TickComponent(1.f/60,LEVELTICK_All,nullptr);
        }
        const auto* OwnById=LocalFX->ActiveEmitters.FindByPredicate([EmitterId](const auto& E){return E.AuthoredEmitterId==EmitterId;});
        const auto* OtherById=RemoteFX->ActiveEmitters.FindByPredicate([EmitterId](const auto& E){return E.AuthoredEmitterId==EmitterId;});
        TestTrue(Case+TEXT("cast completion preserves independent default-script effects"),OwnById && OtherById);
        if(OwnById && OtherById)TestEqual(Case+TEXT("default-script effect retains its authored particle population"),OwnById->Particles.Num(),OtherById->Particles.Num());
    }
    LocalFX->StopAllEffects();LocalPawn->Destroy();RemoteFX->StopAllEffects();Remote->Destroy();
    return !HasAnyErrors();
}
#endif
