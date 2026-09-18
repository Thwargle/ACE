#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "ImageUtils.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailPortalMaterialTest, "ACE.RetailParity.PortalMaskMaterial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailPortalMaterialTest::RunTest(const FString& Parameters)
{
    auto* Dat = NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Apertures;
    for (double Y : {-6.0, 0.0, 6.0})
    {
        auto& A = Apertures.AddDefaulted_GetRef();
        A.WorldNormal = FVector(1,0,0);
        A.WorldVerts = { FVector(5,Y-1,-1), FVector(5,Y+1,-1), FVector(5,Y,1) };
    }
    Dat->SetLandLookOutClip(true, FVector::ZeroVector, Apertures);
    auto* Material = Cast<UMaterialInstanceDynamic>(Dat->GetOrCreateLandMaterial(0, {FColor::White}, 1, 1));
    if (!TestNotNull(TEXT("Create land portal material"), Material)) return false;
    TestEqual(TEXT("Portal mask participates in depth rendering"), Material->GetBlendMode(), BLEND_Masked);
    TestEqual(TEXT("Material receives every portal view"), Material->K2_GetScalarParameterValue(TEXT("PortalViewCount")), 3.f);
    auto* Texture = Cast<UTexture2D>(Material->K2_GetTextureParameterValue(TEXT("PortalViews")));
    if (!TestNotNull(TEXT("Material receives packed plane texture"), Texture)) return false;
    TestEqual(TEXT("Plane texture retains signed floating-point equations"), Texture->GetPixelFormat(), PF_A32B32G32R32F);
    TestFalse(TEXT("Plane equations are not gamma corrected"), Texture->SRGB);
    const FACEPortalViewMask Expected = FACEPortalViewMask::Build(FVector::ZeroVector, Apertures);
    auto& Bulk = Texture->GetPlatformData()->Mips[0].BulkData;
    const void* Packed = Bulk.LockReadOnly();
    TestTrue(TEXT("Shader plane upload preserves polygon data"), FMemory::Memcmp(Packed,
        Expected.Records.GetData(), Expected.Records.Num()*sizeof(FVector4f)) == 0);
    Bulk.Unlock();
    if (!FApp::CanEverRender())
    {
        AddInfo(TEXT("NullRHI: GPU pixel check requires rerun with -RenderOffscreen and without -NullRHI"));
        return true;
    }
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FMaterialResource* Resource = Material->GetMaterial()->GetMaterialResource(GMaxRHIShaderPlatform);
    if (!TestNotNull(TEXT("Land shader resource"), Resource)) return false;
#if WITH_EDITOR
    for (const FString& Error : Resource->GetCompileErrors()) AddError(Error);
    TestTrue(TEXT("Portal shader has no compiler errors"), Resource->GetCompileErrors().IsEmpty());
#endif
    if (!TestNotNull(TEXT("Portal shader compiled"), Resource->GetGameThreadShaderMap())) return false;

    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .RequiresHitProxies(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    AActor* Owner = World->SpawnActor<AActor>();
    auto* Card = NewObject<UProceduralMeshComponent>(Owner);
    Owner->SetRootComponent(Card); Card->RegisterComponent();
    Card->CreateMeshSection_LinearColor(0,
        {FVector(10,-20,-20), FVector(10,-20,20), FVector(10,20,20), FVector(10,20,-20)},
        {0,1,2,0,2,3,0,2,1,0,3,2}, {FVector(-1,0,0),FVector(-1,0,0),FVector(-1,0,0),FVector(-1,0,0)},
        {FVector2D(0,0),FVector2D(0,1),FVector2D(1,1),FVector2D(1,0)},
        {FLinearColor::White,FLinearColor::White,FLinearColor::White,FLinearColor::White}, {}, false);
    Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 1.f);
    Card->SetMaterial(0, Material);
    // A red surface behind the masked card proves discarded terrain does not
    // leave invisible depth that hides interior surfaces in later passes.
    auto* Background = NewObject<UProceduralMeshComponent>(Owner);
    Background->RegisterComponent();
    FProcMeshSection BackgroundSection = *Card->GetProcMeshSection(0);
    for (auto& Vertex : BackgroundSection.ProcVertexBuffer) Vertex.Position.X = 12.f;
    Background->SetProcMeshSection(0, BackgroundSection);
    auto* BackgroundMaterial = Cast<UMaterialInstanceDynamic>(Dat->GetOrCreateLandMaterial(1, {FColor::Red}, 1, 1));
    BackgroundMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"), 1.f);
    BackgroundMaterial->SetScalarParameterValue(TEXT("LookOutEnable"), 0.f);
    Background->SetMaterial(0, BackgroundMaterial);
    auto* Target = NewObject<UTextureRenderTarget2D>(Owner);
    Target->ClearColor = FLinearColor::Black;
    Target->InitCustomFormat(256,256,PF_B8G8R8A8,false);
    auto* Capture = NewObject<USceneCaptureComponent2D>(Owner);
    Capture->TextureTarget = Target;
    Capture->FOVAngle = 120.f;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetAntiAliasing(false);
    Capture->RegisterComponent();
    World->SendAllEndOfFrameUpdates();
    Capture->CaptureScene();
    FlushRenderingCommands();
    TArray<FColor> Pixels;
    TestTrue(TEXT("Read portal-mask GPU render"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
    if (Pixels.Num() == 256*256)
    {
        // Lighting/tone mapping can add a low-intensity glow to the red surface.
        // Compare chroma, not an exact black/zero-channel threshold.
        auto Lit = [&](int32 X, int32 Y) { const FColor C = Pixels[Y*256+X]; return C.R > 32 && C.G > C.R*0.8f && C.B > C.R*0.8f; };
        auto Red = [&](int32 X, int32 Y) { const FColor C = Pixels[Y*256+X]; return C.R > 32 && C.G < C.R*0.5f && C.B < C.R*0.5f; };
        TestTrue(TEXT("GPU draws center portal"), Lit(128,128));
        TestTrue(TEXT("GPU draws third portal"), Lit(217,128));
        TestTrue(TEXT("GPU draws first portal"), Lit(39,128));
        TestFalse(TEXT("GPU rejects triangular corner admitted by old rectangle"), Lit(141,115));
        TestFalse(TEXT("GPU rejects gap between disjoint doors"), Lit(172,128));
        TestTrue(TEXT("Clipped terrain depth does not hide background at polygon corner"), Red(141,115));
        TestTrue(TEXT("Clipped terrain depth does not hide background between doors"), Red(172,128));
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(256,256,Pixels,Png);
        const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/RetailParity/PortalMask.png");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
        FFileHelper::SaveArrayToFile(Png,*Path);
        Apertures.Pop();
        Dat->SetLandLookOutClip(true, FVector::ZeroVector, Apertures);
        BackgroundMaterial->SetScalarParameterValue(TEXT("LookOutEnable"), 0.f);
        World->SendAllEndOfFrameUpdates();
        Capture->CaptureScene(); FlushRenderingCommands();
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TestTrue(TEXT("GPU updates moving portal views without stale texture data"), Red(217,128));
        TestTrue(TEXT("Other portal views survive dynamic texture update"), Lit(128,128));
        TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Exits;
        Dat->SetLandLookOutClip(true,FVector::ZeroVector,Apertures,&Exits);
        BackgroundMaterial->SetScalarParameterValue(TEXT("LookOutEnable"),0.f);
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TestTrue(TEXT("Looking into a basement removes foreground grass through the door"),Red(128,128));
        TestTrue(TEXT("Looking into a basement preserves land between doors"),Lit(172,128));
        Exits.Add(Apertures.Last());
        for (auto& V : Exits[0].WorldVerts) V *= 1.5;
        Dat->SetLandLookOutClip(true,FVector::ZeroVector,Apertures,&Exits);
        BackgroundMaterial->SetScalarParameterValue(TEXT("LookOutEnable"),0.f);
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TestTrue(TEXT("Courtyard exit restores land beyond the second door"),Lit(128,128));
        TestTrue(TEXT("A separate basement entrance remains clear"),Red(39,128));
    }
    World->DestroyWorld(false);
    return true;
}

#endif
