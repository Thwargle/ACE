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
#include "Dat/ACEDatCursor.h"

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

    // Pathwarden Scale Hauberk: retail clothing references intermediate models,
    // and CPhysicsPart resolves the first degrade entry for close-up rendering.
    Self.Appearance=FACEObjDesc(); Self.Appearance.PaletteBaseId=0x0400007E;
    TestTrue(TEXT("Actual Pathwarden Scale Hauberk resolves"),Creation.ApplyClothing(0x100000A6,Self.SetupId,0,0,Self.Appearance));
    FACESetupMeshBuilder Builder(Dat->GetPortalDat(),nullptr,nullptr);
    struct FArmorPart { int32 Part; uint32 Root; uint32 Close; };
    const FArmorPart ArmorParts[]={{9,0x0100120D,0x01001868},{0,0x01001212,0x01001841},
        {10,0x01001230,0x01001870},{13,0x0100122F,0x0100186F},
        {11,0x0100121E,0x0100186C},{14,0x01001219,0x0100186D}};
    for (const auto& Fixture:ArmorParts)
    {
        FACEDatGfxObj Actual;
        TestTrue(TEXT("Root armor model resolves"),Builder.LoadGfxObj(Fixture.Root,Actual));
        TestEqual(TEXT("Root resolves retail close-up armor model"),Actual.Id,Fixture.Close);
        TArray<uint8> Bytes; Dat->GetPortalDat()->ReadFile(Fixture.Close,Bytes);
        FACEDatCursor Cursor(Bytes); FACEDatGfxObj Expected;
        TestTrue(TEXT("Read independent close-up geometry"),ACEDatUnpack::UnpackGfxObj(Cursor,Expected));
        TestEqual(TEXT("Close-up vertices preserved"),Actual.Vertices.Num(),Expected.Vertices.Num());
        TestEqual(TEXT("Close-up polygons preserved"),Actual.Polygons.Num(),Expected.Polygons.Num());
        for (const auto& V:Expected.Vertices)
            TestTrue(TEXT("Armor shape matches retail vertex coordinates"),Actual.Vertices.Contains(V.Key)
                && Actual.Vertices[V.Key].Origin.Equals(V.Value.Origin));
        FACEDatGfxObj Cached; Builder.LoadGfxObj(Fixture.Root,Cached);
        TestEqual(TEXT("Cached armor retains resolved model"),Cached.Id,Fixture.Close);
        Builder.LoadGfxObj(Fixture.Close,Cached);
        TestEqual(TEXT("Direct close-up lookup does not recurse through same degrade table"),Cached.Id,Fixture.Close);
    }
    TestTrue(TEXT("Dressed hauberk applies through the shared appearance component"),App->ApplyWorldObject(Self,100,false));
    App->TickComponent(.5f,LEVELTICK_All,nullptr);
    FACEDatGfxObj Chest; Builder.LoadGfxObj(0x0100120D,Chest);
    TestEqual(TEXT("Hauberk chest uses 64 close-up faces rather than 43 coarse faces"),Chest.Polygons.Num(),64);
    // Save front and side views for comparison with the supplied retail images.
    Capture->FOVAngle=32;
    for (int32 View=0; View<2; ++View)
    {
        const FVector ArmorEye=View==0?FVector(220,360,135):FVector(370,100,135);
        Capture->SetWorldLocationAndRotation(ArmorEye,(FVector(0,0,110)-ArmorEye).Rotation());
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        FImageUtils::PNGCompressImageArray(800,600,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/PathwardenArmor%d.png"),View)));
    }
    GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
