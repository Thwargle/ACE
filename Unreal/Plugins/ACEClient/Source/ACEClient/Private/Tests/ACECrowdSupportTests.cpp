#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "ACEBodySweep.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECrowdSupportTest,"ACE.Collision.CrowdSupport",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACECrowdSupportTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Batch=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Collision.BatchCrowdQueries"));
 const int Previous=Batch->GetInt();ON_SCOPE_EXIT{Batch->Set(Previous,ECVF_SetByCode);};
 const FVector Origin(2000000,4000000,10000);
 auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);
 Box->SetBoxExtent(FVector(500,500,10));Box->SetCollisionResponseToAllChannels(ECR_Block);
 Box->RegisterComponent();Floor->SetActorLocation(Origin-FVector(0,0,10));
 auto Sphere=[&](FVector Location,float Radius)
 {
  auto* A=World->SpawnActor<AActor>();auto* S=NewObject<USphereComponent>(A);A->SetRootComponent(S);
  S->InitSphereRadius(Radius);S->ComponentTags.Add(TEXT("ACECreatureBody"));
  S->SetCollisionEnabled(ECollisionEnabled::QueryOnly);S->SetCollisionResponseToAllChannels(ECR_Ignore);
  S->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);S->RegisterComponent();A->SetActorLocation(Origin+Location);
  return A;
 };
 TArray<AActor*> Crowd;
 const FCollisionQueryParams Query(SCENE_QUERY_STAT(CrowdSupportTest),true);
 for(int Count:{0,1,4,5,6,32,120})
 {
  while(Crowd.Num()<Count)
  {
   const int I=Crowd.Num();
   Crowd.Add(Sphere(FVector((I%8-3.5)*7,(I/8-7)*5,90),65));
  }
  for(bool StandOnCreature:{false,true})
  {
   AActor* Support=StandOnCreature?Sphere(FVector(0,0,15),50):nullptr;
   for(float X:{-40.f,0.f,40.f})for(float Up:{0.f,10.f})
   {
    const FVector Feet=Origin+FVector(X,0,StandOnCreature?65:0);
    float Heights[2]={};FVector Normals[2];bool Found[2]={};
    for(int Mode:{0,1})
    {
     Batch->Set(Mode,ECVF_SetByCode);
     Found[Mode]=ACEBodySweep::FindFootSupport(*World,Feet,48,20,Query,Heights[Mode],Up,false,true,&Normals[Mode]);
    }
    TestEqual(TEXT("Crowd batching preserves the reference support decision"),Found[1],Found[0]);
    if(Found[0] && Found[1])
    {
     TestTrue(TEXT("Crowd batching preserves support height"),FMath::IsNearlyEqual(Heights[0],Heights[1],.01f));
     TestTrue(TEXT("Crowd batching preserves the contact tangent"),Normals[0].Equals(Normals[1],.0001));
    }
    if(X==0 && Up==0)TestTrue(StandOnCreature?TEXT("A valid creature top remains support"):TEXT("Side overlaps cannot hide the floor"),Found[1]);
   }
   if(Support)Support->Destroy();
  }
  double Times[2]={};
  for(int Mode:{0,1})
  {
   Batch->Set(Mode,ECVF_SetByCode);float Z=0;
   const double Begin=FPlatformTime::Seconds();
   for(int I=0;I<500;++I)ACEBodySweep::FindFootSupport(*World,Origin,48,2,Query,Z,.5f,false,true,nullptr,false);
   Times[Mode]=(FPlatformTime::Seconds()-Begin)*1.e6/500;
  }
  AddInfo(FString::Printf(TEXT("Crowd %d: sphere support %.3f -> %.3f us/query"),Count,Times[0],Times[1]));
 }
 return !HasAnyErrors();
}
#endif
