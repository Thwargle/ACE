#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "ACEParticleBatchComponent.h"
#include "ACEParticleUpdateSubsystem.h"
#include "ACEDatSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdParticleUpdateTest,"ACE.Rendering.CrowdParticleUpdates",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACECrowdParticleUpdateTest::RunTest(const FString&)
{
 auto* Parallel=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.ParallelUpdates"));
 auto* Threshold=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Particles.ParallelMinVertices"));
 const int32 Saved=Parallel->GetInt(),SavedThreshold=Threshold->GetInt();
 ON_SCOPE_EXIT { Parallel->Set(Saved,ECVF_SetByCode);Threshold->Set(SavedThreshold,ECVF_SetByCode); };
 Threshold->Set(131072,ECVF_SetByCode);
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;
 GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT { World->bInTick=false;GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!TestTrue(TEXT("Real DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
 const auto* Info=Dat->GetParticleEmitterInfo(0x320009D1); // Real Aetheria swarm geometry.
 if(!TestNotNull(TEXT("Authored particle geometry exists"),Info))return false;
 auto* Updates=World->GetSubsystem<UACEParticleUpdateSubsystem>();
 TArray<UACEParticleBatchComponent*> Batches;
 constexpr int32 Count=40*3,Capacity=512,Frames=40;
 for(int32 B=0;B<Count;++B)
 {
  auto* Owner=World->SpawnActor<AActor>();auto* Batch=NewObject<UACEParticleBatchComponent>(Owner);
  Owner->AddInstanceComponent(Batch);Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Batch->SetWorldLocation(FVector(-2800000+B*20,2400000,10000));
  if(!Dat->ApplyParticleGfxToProceduralMesh(Batch,Info->GfxObjId,100,false))return false;
  Batch->InitializeParticles(Capacity);Batch->RegisterComponent();Batches.Add(Batch);
 }
 for(const int32 Particles:{64,512})
 {
 TArray<TArray<FProcMeshSection>> Reference;
 double Milliseconds[2]={};
 for(int32 Mode:{0,1})
 {
  Parallel->Set(Mode,ECVF_SetByCode);
  for(auto* Batch:Batches)
  {
   Batch->ClearParticles();Batch->FlushParticles();
   for(int32 P=0;P<Particles;++P)Batch->AddParticle(FTransform(FVector::ZeroVector),1);
  }
  World->bInTick=true;
  for(int32 Frame=0;Frame<Frames;++Frame)
  {
   for(int32 B=0;B<Count;++B)for(int32 P=0;P<Particles;++P)
   {
    auto* Batch=Batches[B];
    const auto Pose=FTransform(FRotator(Frame+P,P*11+B,Frame*2).Quaternion(),
     Batch->GetComponentLocation()+FVector(P*7,Frame*5+B,P*3),FVector(1+.01*Frame));
    Batch->SetParticleVisual(P,Pose,float((P+Frame)%10)/10);
   }
   const double Start=FPlatformTime::Seconds();
   for(auto* Batch:Batches)Batch->FlushParticles();
   Updates->FlushPending();
   Milliseconds[Mode]+=(FPlatformTime::Seconds()-Start)*1000;
   if(Mode && Frame==0)
   {
    TestEqual(TEXT("Every crowded emitter reaches the world queue"),Updates->GetLastBatchCount(),Count);
    TestEqual(TEXT("Workers activate only above the measured crossover threshold"),Updates->WasLastFlushParallel(),
     Updates->GetLastVertexCount()>=Threshold->GetInt());
   }
   FlushRenderingCommands(); // Bound the fixture's queued rendering work between measured frames.
  }
  World->bInTick=false;
  for(int32 B=0;B<Count;++B)
  {
   auto* Batch=Batches[B];
   if(!Mode)
   {
    auto& Sections=Reference.AddDefaulted_GetRef();
    for(int32 S=0;S<Batch->GetNumSections();++S)Sections.Add(*Batch->GetProcMeshSection(S));
   }
   else for(int32 S=0;S<Batch->GetNumSections();++S)
   {
    const auto& Before=Reference[B][S];const auto& After=*Batch->GetProcMeshSection(S);
    bool Identical=Before.GetRenderIndexCount()==After.GetRenderIndexCount();
    for(int32 V=0;V<After.ProcVertexBuffer.Num();++V)
    {
     const auto& A=Before.ProcVertexBuffer[V];const auto& C=After.ProcVertexBuffer[V];
     Identical &= A.Position==C.Position && A.Normal==C.Normal && A.Color==C.Color && A.UV0==C.UV0;
    }
    TestTrue(TEXT("Parallel output exactly preserves all vertices, normals, colors, UVs and draw counts"),Identical);
   }
  }
 }
 AddInfo(FString::Printf(TEXT("40 casters / %d emitters / %d live particles / %lld vertices: serial %.3f ms, adaptive %.3f ms per CPU flush"),
  Count,Count*Particles,Updates->GetLastVertexCount(),Milliseconds[0]/Frames,Milliseconds[1]/Frames));
 }
 // Small scenes remain serial; repeated requests use the latest state once.
 World->bInTick=true;
 auto* Batch=Batches[0];Batch->ClearParticles();Batch->FlushParticles();
 Batch->AddParticle(FTransform(FVector(100,0,0)),.25f);Batch->FlushParticles();
 Updates->FlushPending();
 TestEqual(TEXT("One queue entry even after repeated changes"),Updates->GetLastBatchCount(),1);
 TestFalse(TEXT("Small scenes do not pay parallel dispatch overhead"),Updates->WasLastFlushParallel());
 TestEqual(TEXT("Latest lifecycle state wins"),Batch->GetParticleCount(),1);
 Batch->ClearParticles();Batch->FlushParticles();
 FWorldDelegates::OnWorldPostActorTick.Broadcast(World,LEVELTICK_All,1.f/90.f);
 TestEqual(TEXT("Cleared queued batch draws nothing"),Batch->GetProcMeshSection(0)->GetRenderIndexCount(),0);
 Batch=Batches[1];Batch->ClearParticles();Batch->FlushParticles();Batch->DestroyComponent();Updates->FlushPending();
 TestEqual(TEXT("Destroyed effects are not submitted"),Updates->GetLastBatchCount(),0);
 return !HasAnyErrors();
}
#endif
