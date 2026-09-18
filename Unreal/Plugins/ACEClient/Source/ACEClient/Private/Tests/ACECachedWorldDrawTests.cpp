#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACESkyDomeActor.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECachedWorldDrawTest, "ACE.Rendering.CachedWorldDraws",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECachedWorldDrawTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .RequiresHitProxies(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    auto* Owner = World->SpawnActor<AActor>();
    auto* Mesh = NewObject<UProceduralMeshComponent>(Owner); Owner->SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCastShadow(false); Mesh->RegisterComponent();
    auto* Material = UMaterialInstanceDynamic::Create(Dat->EnsureAceUnlitTexturedMaterialBase(), Owner);
    Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 0.65f);
    Mesh->SetMaterial(0, Material);
    auto* Target = NewObject<UTextureRenderTarget2D>(Owner); Target->InitCustomFormat(160, 160, PF_B8G8R8A8, false);
    auto* Capture = NewObject<USceneCaptureComponent2D>(Owner); Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false; Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetBloom(false); Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetAntiAliasing(false);
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyComponents.Add(Mesh); Capture->RegisterComponent();
    auto Render = [&]()
    {
        if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels); return Pixels;
    };
    auto* Cached = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Render.CachedWorldDraws"));
    const int32 Previous = Cached->GetInt(); Cached->Set(1, ECVF_SetByCode);
    ON_SCOPE_EXIT { Cached->Set(Previous, ECVF_SetByCode); };
    TArray<TArray<FColor>> Reference;
    const TArray<FVector> Vertices = {{100,-32,-30},{100,32,-30},{100,32,30},{100,-32,30}};
    for (bool UseCache : {false, true})
    {
        Mesh->bPreferCachedDraws = UseCache; Mesh->MarkRenderStateDirty();
        Mesh->SetWorldTransform(FTransform::Identity);
        Material->SetScalarParameterValue(TEXT("EmissiveStrength"), .65f);
        Mesh->CreateMeshSection(0, Vertices, {0,2,1,0,3,2,0,1,2,0,2,3}, {}, {}, {}, {}, false);
        int32 Stage = 0;
        auto Compare = [&](const TCHAR* Name, bool Visible)
        {
            const auto Pixels = Render();
            TestEqual(TEXT("Capture has expected dimensions"), Pixels.Num(), 160*160);
            int32 Lit = 0; for (const auto& Pixel : Pixels) Lit += FMath::Max3(Pixel.R,Pixel.G,Pixel.B)>20;
            if (Visible) TestTrue(FString::Printf(TEXT("%s draws geometry"), Name), Lit>100);
            else TestEqual(FString::Printf(TEXT("%s hides geometry"), Name), Lit, 0);
            if (!UseCache) Reference.Add(Pixels);
            else
            {
                int64 Error = 0;
                for (int32 I=0; I<Pixels.Num(); ++I)
                    Error += FMath::Abs(int32(Pixels[I].R)-Reference[Stage][I].R)
                        + FMath::Abs(int32(Pixels[I].G)-Reference[Stage][I].G)
                        + FMath::Abs(int32(Pixels[I].B)-Reference[Stage][I].B);
                TestTrue(FString::Printf(TEXT("%s cached/reference pixels agree"), Name), Error<160*160);
            }
            ++Stage;
        };
        Compare(TEXT("Initial"), true);
        Mesh->SetWorldLocation(FVector(0,12,7)); Compare(TEXT("Moved"), true);
        Material->SetScalarParameterValue(TEXT("EmissiveStrength"), .22f); Compare(TEXT("Material parameter"), true);
        Mesh->SetMeshSectionVisible(0, false); Compare(TEXT("Hidden section"), false);
        Mesh->SetMeshSectionVisible(0, true); Compare(TEXT("Restored section"), true);
        TArray<FVector> Smaller = Vertices; for (auto& V : Smaller) { V.Y*=.5f; V.Z*=.5f; }
        Mesh->UpdateMeshSection(0, Smaller, {}, {}, {}, {}); Compare(TEXT("Vertex edit"), true);
        Mesh->ClearMeshSection(0); Compare(TEXT("Cleared"), false);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEMobileShadowTest, "ACE.Rendering.MobileShadowReceiver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEMobileShadowTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .RequiresHitProxies(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    FString Directory=TEXT("C:/Turbine/Asheron's Call"); FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    if (!TestTrue(TEXT("Open DAT for real human shadow"),Dat->LoadDatDirectory(Directory))) return false;
    // AC world coordinates exceed Unreal's default primitive octree extent.
    const FVector Center(2500000, 2500000, 0);
    auto* ReceiverActor = World->SpawnActor<AActor>();
    auto* Receiver = NewObject<UProceduralMeshComponent>(ReceiverActor); ReceiverActor->SetRootComponent(Receiver);
    Receiver->bPreferCachedDraws=true; Receiver->SetCastShadow(false);
    Receiver->CreateMeshSection(0,{{-600,-600,0},{600,-600,0},{600,600,0},{-600,600,0}},
        {0,2,1,0,3,2},{FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector},{},{},{},false);
    Receiver->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
    Receiver->RegisterComponent(); ReceiverActor->SetActorLocation(Center);
    FACEWorldObject Human; Human.Guid=0x50000034; Human.SetupId=0x02000001; Human.MotionTableId=0x09000001;
    auto* Player=World->SpawnActor<AACEWorldEntityActor>(); Player->InitializeFromObject(Human,100,true);
    Player->SetActorLocation(Center); Player->Appearance->SetPartsCastShadow(true,false);
    auto* Sky=World->SpawnActor<AACESkyDomeActor>();
    FACEDatSkyTimeOfDay Key; Key.DirBright=.8f; Key.DirPitch=55; Key.DirHeading=90;
    Key.DirColor=0xFFFFFFFF; Key.AmbColor=0xFFFFFFFF; Key.AmbBright=.35f;
    Sky->UpdateWorldLighting(Key,Key,0,.5f);
    auto* Light=Sky->SunLightActor->FindComponentByClass<UDirectionalLightComponent>();
    const bool Mobile=World->GetFeatureLevel()==ERHIFeatureLevel::ES3_1;
    TestTrue(TEXT("Startup permits both mobile shadow cascades"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.CSM.MaxCascades"))->GetInt()>=2);
    TestEqual(TEXT("Startup enables receivers beyond the engine octree"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.Shadow.CSMShaderCullingMethod"))->GetInt(),0);
    TestEqual(TEXT("Live mobile lighting reserves a near cascade and a distant cascade"),Light->DynamicShadowCascades,Mobile?2:4);
    TestEqual(TEXT("Live mobile shadows cover sixty metres"),Light->DynamicShadowDistanceMovableLight,Mobile?6000.f:60000.f);
    if(Mobile)
    {
        const float FirstSplit=Light->DynamicShadowDistanceMovableLight/(1.f+Light->CascadeDistributionExponent);
        TestTrue(TEXT("Detailed mobile shadows extend beyond the immediate interaction area"),FirstSplit>=1400.f);
    }
    auto* Target=NewObject<UTextureRenderTarget2D>(ReceiverActor); Target->InitCustomFormat(320,240,PF_B8G8R8A8,false);
    auto* Capture=NewObject<USceneCaptureComponent2D>(ReceiverActor); Capture->TextureTarget=Target;
    Capture->CaptureSource=SCS_FinalColorLDR; Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetBloom(false); Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetAntiAliasing(false); Capture->ShowFlags.SetDynamicShadows(true);
    const FVector Eye=Center+FVector(140,-400,320); Capture->SetWorldLocationAndRotation(Eye,(Center+FVector(80,0,25)-Eye).Rotation());
    Capture->RegisterComponent();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    auto Render=[&](const TCHAR* Name)
    {
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray<uint8> Png; FImageUtils::CompressImageArray(320,240,Pixels,Png);
        const FString Path=FPaths::ProjectSavedDir()/TEXT("PerformanceShadowTests")/Name;
        FFileHelper::SaveArrayToFile(Png,*Path); return Pixels;
    };
    const auto With=Render(TEXT("Shadow-On.png"));
    Player->Appearance->SetPartsCastShadow(false,false); const auto Without=Render(TEXT("Shadow-Off.png"));
    int32 Darkened=0;
    for (int32 I=0;I<With.Num();++I) Darkened+=int32(Without[I].R)-With[I].R>12;
    AddInfo(FString::Printf(TEXT("Feature level %d: human shadow darkens %d receiver pixels"),int32(World->GetFeatureLevel()),Darkened));
    TestTrue(TEXT("Runtime cascades cast a human shadow at AC world coordinates"),Darkened>80);
    // The first-person player hides body parts from the owner, but their actual
    // geometry must still cast shadows. HiddenInGame exercises the same hidden
    // shadow path without making this off-screen capture a player view.
    Player->Appearance->SetPartsCastShadow(true,false);
    for(int32 I=0;I<Player->Appearance->GetPartCount();++I)
        if(auto* Part=Cast<UPrimitiveComponent>(Player->Appearance->GetPartMesh(I)))
        { Part->SetCastHiddenShadow(true); Part->SetHiddenInGame(true); }
    const auto HiddenWith=Render(TEXT("Hidden-Body-Shadow-On.png"));
    Player->Appearance->SetPartsCastShadow(false,false);
    const auto HiddenWithout=Render(TEXT("Hidden-Body-Shadow-Off.png"));
    int32 GroundShadow=0, PreservedShadow=0;
    for(int32 I=0;I<HiddenWith.Num();++I)
    {
        // Exclude the visible character itself: its self-shadow is necessarily
        // absent once hidden. Compare the same ground pixels in both captures.
        const bool Ground=FMath::Abs(int32(Without[I].R)-HiddenWithout[I].R)<5
            && FMath::Abs(int32(Without[I].G)-HiddenWithout[I].G)<5
            && FMath::Abs(int32(Without[I].B)-HiddenWithout[I].B)<5;
        if(Ground && int32(Without[I].R)-With[I].R>12)
        { ++GroundShadow; PreservedShadow+=int32(HiddenWithout[I].R)-HiddenWith[I].R>12; }
    }
    AddInfo(FString::Printf(TEXT("Hidden body preserves %d of %d shadowed ground pixels"),PreservedShadow,GroundShadow));
    TestTrue(TEXT("Hiding the body preserves its full shadow silhouette"),GroundShadow>80 && PreservedShadow>=GroundShadow*.95f);
    return true;
}
#endif
