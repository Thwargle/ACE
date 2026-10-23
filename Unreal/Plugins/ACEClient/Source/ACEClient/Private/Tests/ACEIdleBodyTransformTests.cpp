#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ProceduralMeshComponent.h"
#include "PrimitiveSceneProxy.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEIdleBodyTransformTest, "ACE.Rendering.IdleBodyTransforms",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEIdleBodyTransformTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    auto* Pawn=World->SpawnActor<APawn>();
    auto* Capsule=NewObject<UCapsuleComponent>(Pawn); Capsule->InitCapsuleSize(50,99);
    Pawn->SetRootComponent(Capsule); Capsule->RegisterComponent();
    auto* App=NewObject<UACECharacterAppearanceComponent>(Pawn); App->RegisterComponent();
    App->bAlignMeshToCapsuleBottom=true;
    FACEWorldObject Self; Self.Guid=12345; Self.SetupId=0x02000001; Self.MotionTableId=0x09000001;
    Self.bIsSelf=true; Self.bIsPlayer=true; Self.Appearance.PaletteBaseId=0x0400007E;
    // Pathwarden armor's retail ObjDesc part replacements.
    Self.Appearance.AnimPartChanges={{9,0x0100120D},{0,0x01001212},{10,0x01001230},
        {13,0x0100122F},{11,0x0100121E},{14,0x01001219}};
    if (!TestTrue(TEXT("Desktop self appearance builds"),App->ApplyWorldObject(Self,100,false))) return false;
    App->TickComponent(.5f,LEVELTICK_All,nullptr);
    auto* CaptureOwner=World->SpawnActor<AActor>();
    auto* Capture=NewObject<USceneCaptureComponent2D>(CaptureOwner);
    auto* Target=NewObject<UTextureRenderTarget2D>(Capture); Target->InitCustomFormat(320,320,PF_B8G8R8A8,false);
    Capture->TextureTarget=Target; Capture->CaptureSource=SCS_FinalColorLDR; Capture->FOVAngle=50;
    Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetAntiAliasing(false); Capture->ShowFlags.SetLighting(false);
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    for (int32 I=0; I<App->GetPartCount(); ++I)
    {
        auto* Part=Cast<UProceduralMeshComponent>(App->GetPartMesh(I));
        Part->bPreferCachedDraws=true; Part->MarkRenderStateDirty();
        Capture->ShowOnlyComponents.Add(Part);
    }
    Capture->RegisterComponent();
    auto* Reuse=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Render.ReusePrimitiveBuffers"));
    const int32 OldReuse=Reuse->GetInt(); ON_SCOPE_EXIT { Reuse->Set(OldReuse,ECVF_SetByCode); };
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    auto Render=[&]()
    {
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels); return Pixels;
    };
    // A stationary lower body must follow parent movement even when only the
    // torso changes its animation pose. Compare the live proxy against a newly
    // created proxy using fresh shader parameters, at ordinary and large coordinates.
    for (FVector Origin : {FVector::ZeroVector,FVector(1048550,3145700,-1000)})
    {
        for (int32 Stage=0; Stage<9; ++Stage)
        {
            TArray<FPrimitiveSceneProxy*> ResidentProxies;
            TArray<bool> PartVisibility, PartHidden;
            for (int32 I=0; I<App->GetPartCount(); ++I)
            {
                auto* Part=Cast<UPrimitiveComponent>(App->GetPartMesh(I));
                ResidentProxies.Add(Part->SceneProxy);
                PartVisibility.Add(Part->GetVisibleFlag()); PartHidden.Add(Part->bHiddenInGame);
            }
            Reuse->Set(1,ECVF_SetByCode);
            if (Stage==0) Pawn->SetActorLocationAndRotation(Origin+FVector(0,0,99),FRotator::ZeroRotator);
            if (Stage==1) Pawn->SetActorLocationAndRotation(Origin+FVector(80,35,99),FRotator(0,73,0));
            if (Stage>=2 && Stage<=5)
            {
                auto Visible=[&](bool Show)
                {
                    if (Stage==2) Pawn->SetActorHiddenInGame(!Show);
                    else for (int32 I=0; I<App->GetPartCount(); ++I)
                    {
                        auto* Part=Cast<UProceduralMeshComponent>(App->GetPartMesh(I));
                        if (Stage==3) Part->SetHiddenInGame(!Show || PartHidden[I]);
                        if (Stage==4) Part->SetVisibility(Show && PartVisibility[I]);
                        if (Stage==5) Part->SetMeshDrawVisible(Show && PartVisibility[I] && !PartHidden[I]);
                    }
                };
                Visible(false); Render();
                Pawn->SetActorLocation(Origin+FVector(120*Stage,150*Stage,99)); Render();
                Visible(true);
            }
            if (Stage==6) App->SetSelectionHighlight(1);
            if (Stage==7) { Self.Appearance.PaletteBaseId=0x0400007F; App->ApplyWorldObject(Self,100,false); }
            if (Stage==8) Pawn->SetActorScale3D(FVector(1.3));
            const FVector Center=Pawn->GetActorLocation();
            const FVector Eye=Center+FVector(300,-450,180);
            Capture->SetWorldLocationAndRotation(Eye,(Center-Eye).Rotation());
            // Upper-body idle motion changes while the legs retain their pose.
            for (int32 I=9; I<App->GetPartCount(); ++I) App->GetPartMesh(I)->AddLocalRotation(FRotator(0,.1,0));
            const auto Live=Render();
            for (int32 I=0; I<App->GetPartCount(); ++I)
            {
                auto* Part=Cast<UProceduralMeshComponent>(App->GetPartMesh(I));
                if (Stage>=2 && Stage<=5) TestTrue(FString::Printf(TEXT("Reveal preserves part %d geometry at stage %d"),I,Stage),
                    Part->SceneProxy==ResidentProxies[I]);
                TestTrue(TEXT("All body parts inherit the current common root"),Part->GetComponentTransform().Equals(
                    Part->GetRelativeTransform()*App->GetMeshRoot()->GetComponentTransform(),.00001));
                if (Part->SceneProxy) TestTrue(FString::Printf(TEXT("Renderer receives part %d transform at stage %d"),I,Stage),
                    Part->SceneProxy->GetLocalToWorld().Equals(Part->GetComponentTransform().ToMatrixWithScale(),.001));
                Part->MarkRenderStateDirty();
            }
            Reuse->Set(0,ECVF_SetByCode);
            const auto Fresh=Render();
            if (Stage==2 && Origin.IsZero())
            {
                TArray64<uint8> Png;
                FImageUtils::PNGCompressImageArray(320,320,Live,Png);
                FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("Automation/IdleBody-Live.png")));
                FImageUtils::PNGCompressImageArray(320,320,Fresh,Png);
                FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("Automation/IdleBody-Fresh.png")));
            }
            int64 Difference=0; int32 Visible=0;
            for (int32 I=0; I<Live.Num(); ++I)
            {
                Difference+=FMath::Abs(int32(Live[I].R)-Fresh[I].R)+FMath::Abs(int32(Live[I].G)-Fresh[I].G)
                    +FMath::Abs(int32(Live[I].B)-Fresh[I].B);
                Visible+=FMath::Max3(Fresh[I].R,Fresh[I].G,Fresh[I].B)>20;
            }
            TestTrue(TEXT("Fixture renders a visible body"),Visible>100);
            TestTrue(FString::Printf(TEXT("Idle body live/fresh pixels match: origin %s stage %d error %lld"),
                *Origin.ToString(),Stage,Difference),Difference<Live.Num());
        }
    }
    return true;
}
#endif
