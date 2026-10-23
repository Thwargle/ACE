#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "ImageUtils.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatTextureResolver.h"
#include "ACEWorldEntityActor.h"
#include "ACEScriptComponent.h"
#include "ACEParticleBatchComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEParticleBlendTest,"ACE.Rendering.ParticleBlendWeights",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEParticleBlendTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    FString Directory=TEXT("C:/Turbine/Asheron's Call"); FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT opens"),Dat && Dat->LoadDatDirectory(Directory))) return false;
    // Actual hardware sprites: Emissary, island portal, and Guardian's sword.
    for(uint32 Id:{0x080009FAu,0x0800096Bu,0x08000990u,0x08000992u})
    {
        FACEDatDecodedSurface Surface;
        if(!TestTrue(TEXT("Reported effect surface decodes"),Dat->GetTextureResolver()->ResolveSurface(Id,Surface))) return false;
        auto* Material=Dat->GetOrCreateParticleMaterial(Id);
        if(!TestNotNull(TEXT("Reported effect material exists"),Material)) return false;
        TestEqual(TEXT("Reported glows retain additive blending"),Material->GetBlendMode(),BLEND_Additive);
        UTexture* Texture=nullptr; Material->GetTextureParameterValue(TEXT("ACETexture"),Texture);
        auto* Image=Cast<UTexture2D>(Texture);
        if(!TestNotNull(TEXT("Reported glow has DAT pixels"),Image)) return false;
        TArray<void*> Uploads;Uploads.SetNumZeroed(Image->GetNumMips());
        TArray<int64> UploadSizes;UploadSizes.SetNumZeroed(Uploads.Num());
        ON_SCOPE_EXIT {for(void* Upload:Uploads)FMemory::Free(Upload);};
        if(!TestTrue(TEXT("Runtime sprite source can be uploaded again"),Image->GetInitialMipData(0,Uploads,UploadSizes)))return false;
        const auto* Pixels=static_cast<const FColor*>(Uploads[0]);
        int32 NonOpaque=0, AlphaMismatches=0;
        for(int32 I=0;I<Surface.Width*Surface.Height;++I)
        {
            NonOpaque+=Pixels[I].A!=255;
            AlphaMismatches+=Pixels[I].A!=Surface.Pixels[I].A;
        }
        if(Id==0x08000990 || Id==0x08000992)
            TestEqual(TEXT("Retail ONE/ONE sword sprite does not multiply unused file alpha"),NonOpaque,0);
        else TestEqual(TEXT("Retail SRCALPHA/ONE portal and Emissary preserve authored alpha"),AlphaMismatches,0);
    }
    if(!FApp::CanEverRender()) { AddInfo(TEXT("GPU blend checks require RenderOffscreen")); return true; }

    // The numerical oracle needs full HDR precision. Medium effects quality
    // uses a packed scene buffer whose additive rounding accumulates over
    // four layers, even when the capture target itself is FloatRGBA.
    auto* SceneFormat=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SceneColorFormat"));
    const int32 PreviousFormat=SceneFormat->GetInt();
    const auto PreviousFormatPriority=static_cast<EConsoleVariableFlags>(SceneFormat->GetFlags()&ECVF_SetByMask);
    SceneFormat->Set(4,ECVF_SetByCode);
    ON_SCOPE_EXIT {SceneFormat->ClearFlags(ECVF_SetByMask);SceneFormat->Set(PreviousFormat,PreviousFormatPriority);};

    auto* Owner=World->SpawnActor<AActor>();
    auto* Capture=NewObject<USceneCaptureComponent2D>(Owner); Capture->RegisterComponent();
    auto* Target=NewObject<UTextureRenderTarget2D>(Owner);
    Target->ClearColor=FLinearColor::Black; Target->InitCustomFormat(256,256,PF_FloatRGBA,true); Target->UpdateResourceImmediate(true);
    Capture->TextureTarget=Target; Capture->FOVAngle=60;
    Capture->CaptureSource=SCS_SceneColorHDRNoAlpha;
    Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowFlags.SetAtmosphere(false); Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetBloom(false); Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetAntiAliasing(false);
    auto Read=[&]() {
        if(GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FFloat16Color> Pixels; Target->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Pixels); return Pixels;
    };
    auto* Card=NewObject<UProceduralMeshComponent>(Owner); Card->RegisterComponent();
    Card->CreateMeshSection(0,{{100,-30,-30},{100,30,-30},{100,30,30},{100,-30,30}},
        {0,2,1,0,3,2,0,1,2,0,2,3},{},{{0,1},{1,1},{1,0},{0,0}}, {},{},false);
    auto* Batch=NewObject<UACEParticleBatchComponent>(Owner); Batch->RegisterComponent();
    auto SourceSection=*Card->GetProcMeshSection(0);
    for(auto& V:SourceSection.ProcVertexBuffer) V.Position.X=0;
    Batch->SetProcMeshSection(0,SourceSection); Batch->InitializeParticles(4);
    const bool MobileLDR=World->GetFeatureLevel()<=ERHIFeatureLevel::ES3_1 &&
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.MobileHDR"))->GetInt()==0;
    for(bool Batched:{false,true}) for(uint8 Alpha:{uint8(128),uint8(255)})
    {
        auto* Mid=UMaterialInstanceDynamic::Create(Batched ? Dat->EnsureAceBatchedParticleMaterialBase(true) : Dat->EnsureAceParticleAdditiveMaterialBase(),Owner);
        Mid->SetTextureParameterValue(TEXT("ACETexture"),FACEDatTextureResolver::CreateTransientRgbaUi(1,1,{FColor(255,255,255,Alpha)}));
        Mid->SetScalarParameterValue(TEXT("EmissiveStrength"),1.f); Mid->SetScalarParameterValue(TEXT("DiffuseStrength"),1.f);
        Card->SetMaterial(0,Mid); Batch->SetMaterial(0,Mid);
        Capture->ClearShowOnlyComponents(); Capture->ShowOnlyComponent(Batched ? Batch : Card);
        for(float Fade:{0.f,.25f,.5f,1.f}) for(int32 Layers:{1,2,4})
        {
            if(!Batched && Layers!=1) continue;
            Mid->SetScalarParameterValue(TEXT("OpacityMul"),Batched?1.f:Fade);
            Batch->ClearParticles();
            for(int32 I=0;I<Layers;++I) Batch->AddParticle(FTransform(FVector(100+I,0,0)),Fade);
            Batch->FlushParticles();
            const auto Pixels=Read();
            if(!TestEqual(TEXT("Blend capture is complete"),Pixels.Num(),256*256)) return false;
            const float Actual=Pixels[128*256+128].R.GetFloat();
            const float Expected=Layers*Fade*Alpha/255.f;
            // White removes color transfer from the oracle: two half-strength
            // layers must contribute one full-strength layer. The old gamma
            // decode of each fade yielded only 0.428 instead of 1 on desktop.
            TestTrue(FString::Printf(TEXT("%s alpha=%d fade=%.2f layers=%d: %.4f vs %.4f"),
                Batched?TEXT("batch"):TEXT("mesh"),Alpha,Fade,Layers,Actual,Expected),FMath::Abs(Actual-Expected)<.02f);
        }
    }
    // World-effect and glass paths must not apply object opacity twice either.
    Capture->ClearShowOnlyComponents(); Capture->ShowOnlyComponent(Card);
    for(bool Additive:{false,true})
    {
        auto* Mid=UMaterialInstanceDynamic::Create(Additive?Dat->EnsureAceUnlitAdditiveMaterialBase():Dat->EnsureAceUnlitTranslucentMaterialBase(),Owner);
        Mid->SetTextureParameterValue(TEXT("ACETexture"),FACEDatTextureResolver::CreateTransientRgbaUi(1,1,{FColor::White}));
        Mid->SetScalarParameterValue(TEXT("EmissiveStrength"),1.f); Mid->SetScalarParameterValue(TEXT("DiffuseStrength"),1.f);
        Mid->SetScalarParameterValue(TEXT("OpacityMul"),.5f); Mid->SetScalarParameterValue(TEXT("FogAmount"),0.f); Card->SetMaterial(0,Mid);
        const auto Pixels=Read();
        TestTrue(TEXT("World surface opacity attenuates white once"),FMath::Abs(Pixels[128*256+128].R.GetFloat()-.5f)<.02f);
    }

    // Exercise the displayed output as well as the pre-tonemap buffer. White
    // HDR-only probes cannot detect filmic hue shifts or compressed highlights.
    Capture->CaptureSource=SCS_FinalColorLDR;
    Capture->ShowFlags.SetSeparateTranslucency(true);
    Capture->PostProcessSettings.bOverride_AutoExposureBias=true;
    Capture->PostProcessSettings.AutoExposureBias=0.f;
    for(bool Filmic:{true,false})
    {
        Capture->PostProcessSettings.bOverride_ToneCurveAmount=true;
        Capture->PostProcessSettings.ToneCurveAmount=Filmic?1.f:0.f;
        Capture->PostProcessSettings.bOverride_BlueCorrection=true;
        Capture->PostProcessSettings.BlueCorrection=Filmic?1.f:0.f;
        Capture->PostProcessSettings.bOverride_ExpandGamut=true;
        Capture->PostProcessSettings.ExpandGamut=Filmic?1.f:0.f;
        for(FColor Color:{FColor::White,FColor(128,32,224),FColor(16,128,255)})
        {
            auto* Mid=UMaterialInstanceDynamic::Create(Dat->EnsureAceParticleAdditiveMaterialBase(),Owner);
            Mid->SetTextureParameterValue(TEXT("ACETexture"),FACEDatTextureResolver::CreateTransientRgbaUi(1,1,{Color}));
            Mid->SetScalarParameterValue(TEXT("EmissiveStrength"),1.f);
            Card->SetMaterial(0,Mid);
            const auto Pixels=Read();
            const FLinearColor Actual(Pixels[128*256+128]);
            AddInfo(FString::Printf(TEXT("Displayed particle filmic=%d source=%s result=%s"),Filmic,*Color.ToString(),*Actual.ToString()));
            const FLinearColor Expected(Color.R/255.f,Color.G/255.f,Color.B/255.f);
            TestTrue(TEXT("Final particle colors survive tone mapping unchanged"),
                FVector(Actual.R-Expected.R,Actual.G-Expected.G,Actual.B-Expected.B).GetAbsMax()<.02f);
        }
    }
    // Compare GPU composition against retail's independent byte-space blend,
    // over an actual opaque background, for both particle renderers.
    auto* Backdrop=NewObject<UProceduralMeshComponent>(Owner); Backdrop->RegisterComponent();
    Backdrop->SetProcMeshSection(0,*Card->GetProcMeshSection(0));
    Backdrop->SetWorldLocation(FVector(50,0,0));
    auto* BackMaterial=UMaterialInstanceDynamic::Create(Dat->EnsureAceUnlitTexturedMaterialBase(),Owner);
    BackMaterial->SetTextureParameterValue(TEXT("ACETexture"),FACEDatTextureResolver::CreateTransientRgbaUi(1,1,{FColor(48,80,112)}));
    BackMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"),1.f);
    BackMaterial->SetScalarParameterValue(TEXT("FogAmount"),0.f); Backdrop->SetMaterial(0,BackMaterial);
    Capture->ClearShowOnlyComponents(); Capture->ShowOnlyComponent(Backdrop);
    const FLinearColor Background(Read()[128*256+128]);
    for(bool Batched:{false,true}) for(FColor Color:{FColor::White,FColor(128,32,224),FColor(16,128,255)})
    {
        auto* Mid=UMaterialInstanceDynamic::Create(Batched?Dat->EnsureAceBatchedParticleMaterialBase(true):Dat->EnsureAceParticleAdditiveMaterialBase(),Owner);
        Mid->SetScalarParameterValue(TEXT("EmissiveStrength"),1.f);
        Card->SetMaterial(0,Mid); Batch->SetMaterial(0,Mid);
        Capture->ClearShowOnlyComponents(); Capture->ShowOnlyComponent(Backdrop); Capture->ShowOnlyComponent(Batched?Batch:Card);
        for(uint8 Alpha:{uint8(128),uint8(255)})
        {
            Color.A=Alpha;
            Mid->SetTextureParameterValue(TEXT("ACETexture"),FACEDatTextureResolver::CreateTransientRgbaUi(1,1,{Color}));
            for(float Fade:{0.f,.25f,.5f,1.f}) for(int32 Layers:{1,2,4})
            {
                if(!Batched && Layers!=1) continue;
                Mid->SetScalarParameterValue(TEXT("OpacityMul"),Batched?1.f:Fade);
                Batch->ClearParticles();
                for(int32 I=0;I<Layers;++I) Batch->AddParticle(FTransform(FVector(100+I,0,0)),Fade);
                Batch->FlushParticles();
                const FLinearColor Actual(Read()[128*256+128]);
                const float Weight=Fade*Alpha/255.f*Layers;
                const auto Expected=(Background+FLinearColor(Color.R/255.f,Color.G/255.f,Color.B/255.f)*Weight).GetClamped();
                TestTrue(FString::Printf(TEXT("Displayed %s %s fade=%.2f layers=%d: %s vs %s"),
                    Batched?TEXT("batch"):TEXT("mesh"),*Color.ToString(),Fade,Layers,*Actual.ToString(),*Expected.ToString()),
                    FVector(Actual.R-Expected.R,Actual.G-Expected.G,Actual.B-Expected.B).GetAbsMax()<.02f);
            }
        }
    }
    Backdrop->SetWorldLocation(FVector(-50,0,0));
    TestTrue(TEXT("Opaque foreground still occludes the display-space particles"),FLinearColor(Read()[128*256+128]).Equals(Background,.02f));
    Backdrop->DestroyComponent();

    // Exercise the gameplay AA paths as well as the non-temporal capture path.
    // TSR must consume the cleared buffer, not add the retained particles twice.
    auto* AA=IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
    const int32 PreviousAA=AA->GetInt();
    ON_SCOPE_EXIT { AA->Set(PreviousAA,ECVF_SetByCode); };
    Capture->bAlwaysPersistRenderingState=true;
    Capture->ShowFlags.SetAntiAliasing(true);
    Capture->ClearShowOnlyComponents(); Capture->ShowOnlyComponent(Card);
    auto* ColorMid=UMaterialInstanceDynamic::Create(Dat->EnsureAceParticleAdditiveMaterialBase(),Owner);
    ColorMid->SetTextureParameterValue(TEXT("ACETexture"),FACEDatTextureResolver::CreateTransientRgbaUi(1,1,{FColor(128,32,224)}));
    ColorMid->SetScalarParameterValue(TEXT("EmissiveStrength"),1.f); Card->SetMaterial(0,ColorMid);
    for(int32 Method:{1,2,4})
    {
        AA->Set(Method,ECVF_SetByCode);
        for(int32 Frame=0;Frame<3;++Frame) Read();
        const FLinearColor Actual(Read()[128*256+128]);
        TestTrue(FString::Printf(TEXT("AA method %d retains displayed particle color: %s"),Method,*Actual.ToString()),
            FVector(Actual.R-128.f/255.f,Actual.G-32.f/255.f,Actual.B-224.f/255.f).GetAbsMax()<.02f);
    }
    AA->Set(PreviousAA,ECVF_SetByCode); Capture->ShowFlags.SetAntiAliasing(false);

    // Render the actual reported assets, with a fixed camera and elapsed time.
    auto* View=World->SpawnActor<ACameraActor>();
    auto* PC=World->SpawnActor<APlayerController>(); World->AddController(PC);
    if(!PC->PlayerCameraManager) { PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>(); PC->PlayerCameraManager->InitializeFor(PC); }
    PC->PlayerCameraManager->bUseClientSideCameraUpdates=false; PC->SetViewTarget(View);
    const FString Out=FPaths::ProjectSavedDir()/TEXT("Automation/ParticleBlend"); IFileManager::Get().MakeDirectory(*Out,true);
    auto* Culling=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.DistanceCulling"));
    const int32 WasCulling=Culling->GetInt(); Culling->Set(0,ECVF_SetByCode);
    ON_SCOPE_EXIT { Culling->Set(WasCulling,ECVF_SetByCode); };
    for(uint32 Setup:{0x020009BBu,0x020006F4u,0x020016D4u})
    {
        FACEWorldObject Object; Object.Guid=Setup; Object.SetupId=Setup; Object.ItemType=ACEItemType::Misc;
        auto* Actor=World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Object,100,true);
        const FVector Look=Setup==0x020006F4 ? FVector(0,0,150) : FVector(0,0,80);
        View->SetActorLocation(Look+FVector(-600,-180,50)); View->SetActorRotation((Look-View->GetActorLocation()).Rotation());
        PC->PlayerCameraManager->UpdateCamera(0.f); Capture->SetWorldTransform(View->GetActorTransform());
        for(int32 Frame=0;Frame<180;++Frame) Actor->ScriptComponent->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
        Capture->ClearShowOnlyComponents(); Capture->ShowOnlyActorComponents(Actor);
        const auto Pixels=Read(); double Energy=0; for(auto P:Pixels) Energy+=P.R.GetFloat()+P.G.GetFloat()+P.B.GetFloat();
        TestTrue(FString::Printf(TEXT("Reported setup %08X renders visible particles"),Setup),Energy>10);
        TArray<FColor> Image; for(auto P:Pixels) { auto C=FLinearColor(P).ToFColor(false); C.A=255; Image.Add(C); }
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(256,256,Image,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Out/FString::Printf(TEXT("%08X.png"),Setup)));
        Actor->Destroy();
    }
    // Tower Guardian's server appearance (ACE-World creature 35273). Compare
    // its purple body with the optional gold selection highlight separately
    // from the sword particles so a selection tint cannot mask a texture issue.
    FACEWorldObject Guardian; Guardian.Guid=35273; Guardian.SetupId=0x020016D7;
    Guardian.MotionTableId=0x09000186; Guardian.ItemType=ACEItemType::Creature; Guardian.Scale=3.f;
    const int32 Parts[]={16788885,16788894,16788893,16788081,16788088,16788896,16788895,
        16788082,16788089,16788889,16788898,16788887,16788891,16788897,16788888,16788892,16789125};
    for(int32 I=0;I<UE_ARRAY_COUNT(Parts);++I)
    {
        FACEObjDescAnimPartChange Part; Part.PartIndex=I; Part.PartId=Parts[I]; Guardian.Appearance.AnimPartChanges.Add(Part);
    }
    const int32 Textures[][3]={{0,83894477,83892492},{0,83894478,83892492},{1,83894490,83892492},
        {2,83894483,83892492},{2,83894484,83892492},{3,83894184,83892492},{4,83894184,83892492},
        {5,83894490,83892492},{6,83894483,83892492},{6,83894484,83892492},{7,83894184,83892492},
        {8,83894184,83892492},{9,83894480,83894593},{9,83894481,83892492},{10,83894489,83892492},
        {11,83894479,83892492},{12,83894485,83892492},{13,83894489,83892492},{14,83894479,83892492},{15,83894485,83892492}};
    for(const auto& T:Textures)
    {
        FACEObjDescTextureChange Texture; Texture.PartIndex=T[0]; Texture.OldTexture=T[1]; Texture.NewTexture=T[2];
        Guardian.Appearance.TextureChanges.Add(Texture);
    }
    auto* Body=World->SpawnActor<AACEWorldEntityActor>(); Body->InitializeFromObject(Guardian,100,true);
    const FVector Look(0,0,300); View->SetActorLocation(Look+FVector(-950,-750,50));
    View->SetActorRotation((Look-View->GetActorLocation()).Rotation()); Capture->SetWorldTransform(View->GetActorTransform());
    Capture->ClearShowOnlyComponents(); Capture->ShowOnlyActorComponents(Body);
    for(int32 Selected:{0,1})
    {
        Body->SetSelectionHighlight(float(Selected)); const auto Pixels=Read();
        TArray<FColor> Image; for(auto P:Pixels) { auto C=FLinearColor(P).ToFColor(false); C.A=255; Image.Add(C); }
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(256,256,Image,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Out/FString::Printf(TEXT("Guardian-Selected%d.png"),Selected)));
    }
    Body->Destroy();
    return !HasAnyErrors();
}
#endif
