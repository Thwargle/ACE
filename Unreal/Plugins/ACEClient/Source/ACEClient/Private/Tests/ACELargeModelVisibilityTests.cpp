#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralMeshViewFacing.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "ImageUtils.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELargeModelVisibilityTest, "ACE.Rendering.LargeModelVisibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACELargeModelVisibilityTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance = GI;
    GI->OnWorldChanged(nullptr, World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT opens"), Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;

    // A server may legitimately use a sprite GfxObj as a creature's setup.
    // Verify this takes the regular world-object path, not particle-only code.
    FACEWorldObject Object; Object.Guid = 0x7100511; Object.SetupId = 0x01001111;
    Object.ItemType = ACEItemType::Creature; Object.Scale = 25.f;
    auto* Actor = World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Object, 100, true);
    auto* Part = Cast<UProceduralMeshComponent>(Actor->Appearance->GetPartMesh(0));
    if (!TestNotNull(TEXT("Server-defined sprite creature has a mesh"), Part)) return false;
    TestEqual(TEXT("World creatures honor DAT viewer-facing mode"), Part->GetViewFacingMode(), 2u);
    TestTrue(TEXT("Server scale reaches mesh transform"), Part->GetComponentScale().Equals(FVector(25)));

    // Deterministic card: old code draws it edge-on from the side. The close
    // views sit INSIDE its conservative bounds, reproducing the large-card case.
    Part->ClearAllMeshSections();
    const TArray<FVector> Vertices = {{-100,0,-100},{100,0,-100},{100,0,100},{-100,0,100}};
    Part->CreateMeshSection(0, Vertices, {0,1,2,0,2,3,2,1,0,3,2,0}, {},
        {{0,0},{1,0},{1,1},{0,1}}, {FColor::White,FColor::White,FColor::White,FColor::White}, {}, false);
    Part->SetMaterial(0, Dat->GetVertexColorMaterial());
    Part->SetViewFacing(2, FVector::ZeroVector);
    auto* Capture = NewObject<USceneCaptureComponent2D>(Actor); Capture->RegisterComponent();
    auto* Target = NewObject<UTextureRenderTarget2D>(Actor);
    Target->ClearColor = FLinearColor::Black; Target->InitCustomFormat(128, 128, PF_FloatRGBA, true);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target; Capture->FOVAngle = 90;
    Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false; Capture->CaptureSource = SCS_SceneColorHDR;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyComponent(Part);
    Capture->ShowFlags.SetAtmosphere(false); Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetAntiAliasing(false);
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/LargeModelVisibility");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FVector Origin(-2800000,2400000,10000);
    int32 Captures = 0;
    auto Render = [&](const FVector& Eye, const TCHAR* Name)
    {
        Capture->SetWorldLocationAndRotation(Eye, (Origin - Eye).Rotation());
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        int32 Visible = 0;
        for (auto& Pixel : Pixels) { Visible += int32(Pixel.R)+Pixel.G+Pixel.B > 12; Pixel.A = 255; }
        if (Captures++ < 4)
        {
            TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(128,128,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG, *(Directory / FString::Printf(TEXT("%02d-%s.png"), Captures,Name)));
        }
        return Visible;
    };
    for (bool Cached : {false,true}) for (float Scale : {1.f,25.f,100.f})
    {
        Part->bPreferCachedDraws = Cached; Part->MarkRenderStateDirty();
        Actor->SetActorScale3D(FVector(Scale)); Actor->SetActorLocation(Origin);
        Part->SetWorldLocationAndRotation(Origin, FQuat::Identity);
        const FTransform Simulation = Part->GetComponentTransform();
        for (const FVector Direction : {FVector(1,0,0),FVector(0,1,0),FVector(0,0,1),FVector(-1,1,.1).GetSafeNormal()})
        for (float Distance : {50.f, 500.f})
        {
            const FVector Eye = Origin + Direction * Distance * Scale;
            TestTrue(TEXT("Scaled creature remains rendered from close and distant view directions"), Render(Eye,TEXT("facing")) > 100);
            TestTrue(TEXT("Rendering never changes simulation/attachment transform"), Part->GetComponentTransform().Equals(Simulation));
            const FTransform Draw = ProceduralMeshViewFacing::DrawTransform(Simulation, FVector::ZeroVector, 2, Eye);
            for (const FVector Vertex : Vertices)
                TestTrue(TEXT("Scaled rotated geometry stays inside culling bounds"),
                    Part->Bounds.GetBox().ExpandBy(.01).IsInsideOrOn(Draw.TransformPosition(Vertex)));
        }
    }
    // Counterfactual confirms the test catches the original edge-on invisibility.
    Part->SetViewFacing(1,FVector::ZeroVector);
    TestEqual(TEXT("Rigid edge-on card has no screen coverage"), Render(Origin+FVector(5000,0,0),TEXT("rigid-control")), 0);
    for (uint32 Mode : {1u,2u,3u,4u,5u})
    {
        const FTransform Simulation(FRotator(13,21,9),Origin,FVector(25));
        const FTransform Draw = ProceduralMeshViewFacing::DrawTransform(Simulation,FVector(15,0,30),Mode,Origin+FVector(500,30,100));
        TestTrue(TEXT("Draw modes preserve authored position and scale"), Draw.GetLocation()==Simulation.GetLocation() && Draw.GetScale3D()==Simulation.GetScale3D());
        if (Mode==1) TestTrue(TEXT("Solid models retain authored rotation"), Draw.Equals(Simulation));
        if (Mode>=3) TestTrue(TEXT("Constrained sprite retains authored axis"),
            Draw.GetUnitAxis(EAxis::Type(Mode-2)).Equals(Simulation.GetUnitAxis(EAxis::Type(Mode-2)),1.e-6));
    }
    return true;
}
#endif
