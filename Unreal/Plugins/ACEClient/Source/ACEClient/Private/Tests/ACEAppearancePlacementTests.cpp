#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterCreation.h"
#include "ACECharacterAppearanceComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAppearancePlacementTest,"ACE.RetailParity.AppearancePlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEAppearancePlacementTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); GI->Init();
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* Pawn=World->SpawnActor<APawn>();
    auto* Capsule=NewObject<UCapsuleComponent>(Pawn); Capsule->InitCapsuleSize(50,99);
    Pawn->SetRootComponent(Capsule); Pawn->AddInstanceComponent(Capsule); Capsule->RegisterComponent();
    Pawn->SetActorLocation(FVector(0,0,99));
    auto* App=NewObject<UACECharacterAppearanceComponent>(Pawn); Pawn->AddInstanceComponent(App); App->RegisterComponent();
    App->bAlignMeshToCapsuleBottom=true;
    FACECharacterCreation Creation; FString Error;
    if (!TestTrue(TEXT("Retail clothing table decoder loads"),Creation.Load(*Dat->GetPortalDat(),Error))) return false;
    FACEWorldObject Self; Self.SetupId=0x02000001; Self.bIsPlayer=true; Self.Name=TEXT("Mukkir Wings placement fixture");
    Self.Appearance.PaletteBaseId=0x0400007E;
    TestTrue(TEXT("Hoary Mattekar over-robe resolves"),Creation.ApplyClothing(0x100007E2,Self.SetupId,61,1,Self.Appearance));
    TestTrue(TEXT("Bunny slippers resolve"),Creation.ApplyClothing(0x1000069E,Self.SetupId,0,0,Self.Appearance));
    TestTrue(TEXT("Actual Mukkir clothing table resolves"),Creation.ApplyClothing(0x10000867,Self.SetupId,0,0,Self.Appearance));
    TestTrue(TEXT("Dressed human builds"),App->ApplyWorldObject(Self,100,false));
    App->TickComponent(.5f,LEVELTICK_All,nullptr);
    auto* Wings=Cast<UProceduralMeshComponent>(App->GetPartMesh(29));
    auto* WingMaterial=Wings?Wings->GetMaterial(0):nullptr;
    UTexture* WingTexture=nullptr;
    TestTrue(TEXT("Worn Mukkir Wings bind a texture"),WingMaterial && WingMaterial->GetTextureParameterValue(TEXT("ACETexture"),WingTexture));
    auto* Texture=Cast<UTexture2D>(WingTexture);
    if (TestNotNull(TEXT("Wings use a resolved 2D texture"),Texture))
    {
        TestEqual(TEXT("Mukkir's second U tile remains visible"),int32(Texture->AddressX),int32(TA_Wrap));
        TestEqual(TEXT("Untiled V edges stay clamped"),int32(Texture->AddressY),int32(TA_Clamp));
    }
    FBox Bounds(ForceInit); App->GetVisualWorldBounds(Bounds);
    AddInfo(FString::Printf(TEXT("Human ground meshRoot=%s bounds=%s"),*App->GetMeshRoot()->GetComponentLocation().ToString(),*Bounds.ToString()));
    for (int32 PartIndex : {3,4,7,8,29,30,31,32,33})
    {
        auto* Part=Cast<UProceduralMeshComponent>(App->GetPartMesh(PartIndex));
        if (!TestNotNull(TEXT("Human foot/wing part exists"),Part)) continue;
        AddInfo(FString::Printf(TEXT("Part %d location=%s minZ=%.4f sections=%d"),PartIndex,
            *Part->GetComponentLocation().ToString(),Part->Bounds.GetBox().Min.Z,Part->GetNumSections()));
    }
    auto* CaptureOwner=World->SpawnActor<AActor>();
    auto* Capture=NewObject<USceneCaptureComponent2D>(CaptureOwner);
    auto* Target=NewObject<UTextureRenderTarget2D>(Capture); Target->InitAutoFormat(800,600);
    Capture->TextureTarget=Target; Capture->CaptureSource=SCS_FinalColorLDR; Capture->FOVAngle=45;
    Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetAntiAliasing(false);
    Capture->RegisterComponent();
    const FVector Eye(160,-420,140);
    Capture->SetWorldLocationAndRotation(Eye,(FVector(0,0,100)-Eye).Rotation());
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
    TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
    TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(800,600,Pixels,PNG);
    FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/MukkirWings.png")));
    GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
