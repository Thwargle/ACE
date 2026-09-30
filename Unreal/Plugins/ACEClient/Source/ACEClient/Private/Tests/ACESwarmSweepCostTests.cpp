#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../ACEBodySweep.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"

// Retain the pre-optimization query as an independent reference for both
// contact equivalence and timing. This code is never in a shipping build.
static bool ReferenceSwarmSweep(UWorld& World,FHitResult& Hit,const FVector& From,const FVector& To,
 const FCollisionShape& Body,const FCollisionQueryParams& Params)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(ACE_BodySweepReference);
 const float Radius=Body.GetCapsuleRadius();const double Offset=Body.GetCapsuleHalfHeight()-Radius;
 bool Found=false;Hit=FHitResult();TOptional<FCollisionQueryParams> Escape;
 for(int I=0;I<2;++I)
 {
  const FVector Shift(0,0,I==0?-Offset:Offset);FHitResult Part;bool Blocked=false;
  for(;;)
  {
   Blocked=World.SweepSingleByChannel(Part,From+Shift,To+Shift,FQuat::Identity,ECC_Pawn,
    FCollisionShape::MakeSphere(Radius),Escape.IsSet()?Escape.GetValue():Params);
   if(!Blocked||!ACEBodySweep::CanEscapeCreature(Part,From,To,Body))break;
   if(!Escape.IsSet())Escape.Emplace(Params);Escape->AddIgnoredComponent(Part.GetComponent());
  }
  if(!Blocked)continue;
  Part.Location-=Shift;Part.TraceStart=From;Part.TraceEnd=To;
  Part.MyBoneName=I?FName(TEXT("ACEUpperBodySphere")):NAME_None;
  if(!Found||(Part.bStartPenetrating&&!Hit.bStartPenetrating)
   ||(Part.bStartPenetrating==Hit.bStartPenetrating&&(Part.Time<Hit.Time
    ||(Part.Time==Hit.Time&&Part.PenetrationDepth>Hit.PenetrationDepth))))Hit=Part;
  Found=true;
 }
 return Found;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESwarmSweepCostTest,"ACE.Collision.SwarmSweepCost",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACESwarmSweepCostTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 ON_SCOPE_EXIT{World->DestroyWorld(false);};
 const FVector From(0,0,100);const auto Body=FCollisionShape::MakeCapsule(48,90.75);
 const FCollisionQueryParams Params(SCENE_QUERY_STAT(SwarmSweepCost),true);
 auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);
 Box->SetBoxExtent(FVector(10,1000,1000));Box->SetCollisionResponseToAllChannels(ECR_Block);
 Box->RegisterComponent();Wall->SetActorLocation(FVector(160,0,100));
 auto* Floor=World->SpawnActor<AActor>();auto* FloorBox=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(FloorBox);
 FloorBox->SetBoxExtent(FVector(1000,1000,10));FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
 FloorBox->RegisterComponent();Floor->SetActorLocation(FVector(0,0,-10));
 auto* Batch=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Collision.BatchCrowdQueries"));
 const int OriginalBatch=Batch->GetInt();ON_SCOPE_EXIT{Batch->Set(OriginalBatch,ECVF_SetByCode);};
 TArray<AActor*> Crowd;
 for(int Count:{0,1,2,3,4,5,6,8,32,64})
 {
  while(Crowd.Num()<Count)
  {
   const int I=Crowd.Num();auto* Mob=World->SpawnActor<AActor>();auto* Sphere=NewObject<USphereComponent>(Mob);
   Mob->SetRootComponent(Sphere);Sphere->InitSphereRadius(65);Sphere->ComponentTags.Add(TEXT("ACECreatureBody"));
   Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
   Sphere->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);Sphere->RegisterComponent();
   Mob->SetActorLocation(From+FVector(-30-(I%4)*3,(I%8-3.5)*7,(I/8-3.5)*12));Crowd.Add(Mob);
  }
  int Compared=0;
  for(float X:{-30.f,0.f,20.f,200.f})for(float Y:{-40.f,0.f,40.f})for(float Z:{-20.f,0.f,20.f})
  {
   const FVector To=From+FVector(X,Y,Z);FHitResult Old,New;
   const bool A=ReferenceSwarmSweep(*World,Old,From,To,Body,Params);
   const bool B=ACEBodySweep::SweepBody(*World,New,From,To,Body,Params);
   TestEqual(TEXT("Batched overlap filtering preserves blocked/clear results"),B,A);
   if(A&&B)
   {
    TestEqual(TEXT("Same first obstruction"),New.Component.Get(),Old.Component.Get());
    TestTrue(TEXT("Same travel fraction and contact point"),FMath::IsNearlyEqual(New.Time,Old.Time,.0001f)&&New.Location.Equals(Old.Location,.01));
   }
   ++Compared;
  }
  double OldUs=0,NewUs=0;
  constexpr int Repeats=1000;
  for(int Round=0;Round<4;++Round)for(bool Optimized:{false,true})
  {
   const double Start=FPlatformTime::Seconds();
   for(int I=0;I<Repeats;++I)
   {
    FHitResult Hit;const FVector To=From+FVector(200,0,0);
    if(Optimized)ACEBodySweep::SweepBody(*World,Hit,From,To,Body,Params);
    else ReferenceSwarmSweep(*World,Hit,From,To,Body,Params);
   }
   const double Us=(FPlatformTime::Seconds()-Start)*1.e6/Repeats;
   if(Round>0){if(Optimized)NewUs+=Us/3.;else OldUs+=Us/3.;}
  }
  AddInfo(FString::Printf(TEXT("Crowd %d: %d equivalent paths; escape query %.3f -> %.3f us (%.1f%% saved)"),
   Count,Compared,OldUs,NewUs,100*(1-NewUs/OldUs)));
  OldUs=NewUs=0;
  for(int Round=0;Round<4;++Round)for(int Mode:{0,1})
  {
   Batch->Set(Mode,ECVF_SetByCode);
   const FVector Top(-40,0,300),Bottom(-40,0,-50);
   TArray<FHitResult> Hits;
   TestTrue(TEXT("Floor remains visible behind the entire swarm"),ACEBodySweep::TraceGround(*World,Hits,Top,Bottom,Params));
   TestTrue(TEXT("The supporting object is the floor, never a creature"),Hits.Num()>0&&Hits.Last().GetComponent()==FloorBox);
   const double Start=FPlatformTime::Seconds();
   for(int I=0;I<Repeats;++I)ACEBodySweep::TraceGround(*World,Hits,Top,Bottom,Params);
   const double Us=(FPlatformTime::Seconds()-Start)*1.e6/Repeats;
   if(Round>0){if(Mode)NewUs+=Us/3.;else OldUs+=Us/3.;}
  }
  AddInfo(FString::Printf(TEXT("Crowd %d: floor query %.3f -> %.3f us (%.1f%% saved)"),Count,OldUs,NewUs,100*(1-NewUs/OldUs)));
 }
 return !HasAnyErrors();
}
#endif
