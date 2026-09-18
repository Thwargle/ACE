#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEParticleBatchComponent.h"
#include "ACEDatSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEParticleBatchTest,"ACE.Rendering.ParticleBatchLifetime",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEParticleBatchTest::RunTest(const FString&)
{
 auto* Prefix=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.ActivePrefix"));
 const int32 Previous=Prefix->GetInt(); Prefix->Set(1,ECVF_SetByCode);
 ON_SCOPE_EXIT { Prefix->Set(Previous,ECVF_SetByCode); };
 auto* Batch=NewObject<UACEParticleBatchComponent>();
 const TArray<FVector> Vertices={{1,0,0},{0,1,0},{0,0,1}};
 Batch->CreateMeshSection(0,Vertices,{0,1,2},{},{},
  {FColor(10,20,30,128),FColor(10,20,30,128),FColor(10,20,30,128)},{},false);
 Batch->InitializeParticles(32);
 Batch->AddParticle(FTransform(FVector(100,0,0)),1);
 Batch->AddParticle(FTransform(FVector(200,0,0)),.5f);Batch->FlushParticles();
 auto* Section=Batch->GetProcMeshSection(0);
 TestEqual(TEXT("Particle keeps authored RGB independently of unused DAT alpha"),Section->ProcVertexBuffer[0].Color,FColor(10,20,30,255));
 TestEqual(TEXT("Fade carries only per-particle opacity"),Section->ProcVertexBuffer[3].Color.A,uint8(128));
 Batch->RemoveParticle(0);Batch->FlushParticles();
 TestTrue(TEXT("Swap removal retains the surviving transform"),Section->ProcVertexBuffer[0].Position.Equals(FVector(201,0,0)));
 TestEqual(TEXT("Only the surviving triangle is submitted"),Section->GetRenderIndexCount(),3);
 Batch->ClearParticles();Batch->FlushParticles();
 TestEqual(TEXT("Clearing submits no geometry"),Section->GetRenderIndexCount(),0);
 Batch->AddParticle(FTransform(FVector(300,0,0)),1);Batch->FlushParticles();
 TestTrue(TEXT("Refill works after clearing"),Section->ProcVertexBuffer[0].Position.Equals(FVector(301,0,0)));
 TestEqual(TEXT("Refill restores one triangle"),Section->GetRenderIndexCount(),3);
 Prefix->Set(0,ECVF_SetByCode);Batch->SetParticleVisual(0,FTransform(FVector(300,0,0)),1);Batch->FlushParticles();
 TestEqual(TEXT("Fallback restores full index range"),Section->GetRenderIndexCount(),96);
 TestTrue(TEXT("Fallback clears all inactive slots"),Section->ProcVertexBuffer[3].Position.IsNearlyZero() && Section->ProcVertexBuffer[3].Color.A==0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEParticlePrefixRenderTest,"ACE.Rendering.ParticlePrefixRender",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEParticlePrefixRenderTest::RunTest(const FString&)
{
 if (!FApp::CanEverRender()) { AddError(TEXT("This test needs an active renderer")); return false; }
 auto* Prefix=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.ActivePrefix"));
 const int32 Previous=Prefix->GetInt();
 ON_SCOPE_EXIT { Prefix->Set(Previous,ECVF_SetByCode); };
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);GI->Init();
 ON_SCOPE_EXIT { GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
 auto* Owner=World->SpawnActor<AActor>();
 auto* Batch=NewObject<UACEParticleBatchComponent>(Owner);Batch->RegisterComponent();
 // Asymmetric colors/UVs catch stale attributes as the active buffer shrinks and grows.
 Batch->CreateMeshSection(0,{{0,-18,-18},{0,18,-18},{0,18,18},{0,-18,18}},
  {0,2,1,0,3,2,0,1,2,0,2,3},{},{{0,1},{1,1},{1,0},{0,0}},
  {FColor::White,FColor::White,FColor::White,FColor::White},{},false);
 Batch->InitializeParticles(64);
 auto* Texture=UTexture2D::CreateTransient(2,2,PF_B8G8R8A8);
 auto* Data=static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
 Data[0]=FColor::Red;Data[1]=FColor::Green;Data[2]=FColor::Blue;Data[3]=FColor::White;
 Texture->GetPlatformData()->Mips[0].BulkData.Unlock();Texture->Filter=TF_Nearest;Texture->UpdateResource();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 auto* Target=NewObject<UTextureRenderTarget2D>(Owner);Target->InitCustomFormat(128,128,PF_FloatRGBA,false);
 auto* Capture=NewObject<USceneCaptureComponent2D>(Owner);Capture->TextureTarget=Target;Capture->FOVAngle=90;
 Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->CaptureSource=SCS_SceneColorHDRNoAlpha;
 Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetBloom(false);Capture->ShowFlags.SetAtmosphere(false);
 Capture->ShowFlags.SetAntiAliasing(false);Capture->ShowFlags.SetLighting(false);
 Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
 Capture->ShowOnlyComponents.Add(Batch);Capture->RegisterComponent();
 auto Read=[&]() {
  if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
  World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();
  TArray<FLinearColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);return Pixels;
 };
 for(bool Additive:{false,true})
 {
  auto* Material=UMaterialInstanceDynamic::Create(Dat->EnsureAceBatchedParticleMaterialBase(Additive),Owner);
  Material->SetTextureParameterValue(TEXT("ACETexture"),Texture);
  Material->SetScalarParameterValue(TEXT("OpacityMul"),1.f);Material->SetScalarParameterValue(TEXT("EmissiveStrength"),1.f);
  Batch->SetMaterial(0,Material);
  TArray<TArray<FLinearColor>> Reference;
  for(int32 Mode:{0,1})
  {
   Prefix->Set(Mode,ECVF_SetByCode);Batch->ClearParticles();Batch->FlushParticles();
   int32 Frame=0;
   auto Compare=[&](const TCHAR* Stage) {
    const auto Pixels=Read();TestEqual(TEXT("Capture size"),Pixels.Num(),128*128);
    int32 Visible=0;for(const auto& P:Pixels)Visible+=P.GetMax()>.03f && (P.R+P.G+P.B)>.03f;
    if(Frame==2)TestEqual(TEXT("Empty batch is invisible"),Visible,0);
    else TestTrue(TEXT("Live batch renders visible colored geometry"),Visible>30);
    if(Mode==0)Reference.Add(Pixels);
    else {
     float Error=0;for(int32 I=0;I<Pixels.Num();++I) {
      const auto D=Pixels[I]-Reference[Frame][I];Error=FMath::Max(Error,FMath::Max3(FMath::Abs(D.R),FMath::Abs(D.G),FMath::Abs(D.B)));
     }
     TestTrue(FString::Printf(TEXT("%s %s matches full-buffer rendering (error %.6f)"),Additive?TEXT("additive"):TEXT("translucent"),Stage,Error),Error<.002f);
    }
    ++Frame;
   };
   Batch->AddParticle(FTransform(FVector(100,-30,0)),1.f);
   Batch->AddParticle(FTransform(FVector(100,30,0)),.35f);Batch->FlushParticles();Compare(TEXT("two particles"));
   Batch->RemoveParticle(0);Batch->FlushParticles();Compare(TEXT("swap shrink"));
   Batch->ClearParticles();Batch->FlushParticles();Compare(TEXT("clear"));
   // Grow to full capacity after zero vertices; only some cards are in the capture.
   for(int32 I=0;I<64;++I)Batch->AddParticle(FTransform(FVector(100,(I-2)*36,0)),.7f);
   Batch->FlushParticles();Compare(TEXT("full refill"));
   Batch->RemoveParticle(2);Batch->FlushParticles();Batch->MarkRenderStateDirty();Compare(TEXT("proxy recreation"));
  }
 }
 return true;
}
#endif
