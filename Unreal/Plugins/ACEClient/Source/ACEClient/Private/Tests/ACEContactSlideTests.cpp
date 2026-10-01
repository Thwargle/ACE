#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../ACEBodySweep.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECreatureFixtures.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Components/BoxComponent.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEContactSlideTest,"ACE.Collision.CreatureSliding",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEContactSlideTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 FACEPosition Seed;Seed.CellId=0x7D63000D;Seed.Location=FVector(25.6875,97.013672,12);
 const FVector Base=Seed.ToUnrealLocation(100);
 const auto Shape=FCollisionShape::MakeCapsule(48,90.75);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(CreatureSlidingRegression),true);
 int Cases=0,Stopped=0,TooDeep=0;
 for(int Profile=0;Profile<4;++Profile)
 {
  FACEWorldObject Object;Object.Guid=0x72030000+Profile;Object.bHasPosition=true;Object.Position=Seed;
  ACECreatureFixtures::Apply(Object,Profile);
  // ACE-World-16PY-Patches Creature/Skeleton/04266 Old Bones.sql.
  if(Profile==3){Object.Name=TEXT("Old Bones");Object.SetupId=0x02000059;Object.MotionTableId=0x09000025;Object.Scale=1;}
  auto* Mob=World->SpawnActor<AACEWorldEntityActor>();Mob->InitializeFromObject(Object,100,true);
  Mob->SetActorLocation(Base);
  for(int Angle=0;Angle<24;++Angle)
  {
   const double A=Angle*PI/12;
   const FVector N(FMath::Cos(A),FMath::Sin(A),0),T(-N.Y,N.X,0);
   const FVector Center=Base+FVector(0,0,90.75);
   FHitResult Touch;
   if(!ACEBodySweep::Sweep(*World,Touch,Center+N*500,Center,Shape,Query)||Touch.bStartPenetrating)continue;
   for(float Step:{.5f,2.f,10.f})for(double Tangent:{-.8,-.2,.2,.8})
   {
    FVector Position=Touch.Location+N*.02;
    const FVector Desired=(T*Tangent-N*.6).GetSafeNormal()*Step;
    for(int Frame=0;Frame<20;++Frame)
    {
     const FVector Before=Position;FHitResult Hit;
     if(ACEBodySweep::Sweep(*World,Hit,Before,Before+Desired,Shape,Query))
     {
      const FVector Normal=Hit.Normal.GetSafeNormal2D();
      const FVector Available=Desired-Normal*FMath::Min(0.,FVector::DotProduct(Desired,Normal));
      Position=ACEBodySweep::SlideGrounded(*World,Before,Before+Desired,Hit,Shape,Query);
      if(Available.Size()>.15 && FVector::Dist2D(Position,Before)<Available.Size()*.4)
      {
       if(++Stopped<=12)AddInfo(FString::Printf(TEXT("Stopped %s angle=%d step=%.2f tangent=%.2f frame=%d pen=%d depth=%.6f normal=%s available=%.3f actual=%.3f"),
        *Object.Name,Angle,Step,Tangent,Frame,Hit.bStartPenetrating,Hit.PenetrationDepth,*Hit.Normal.ToString(),Available.Size(),FVector::Dist2D(Position,Before)));
      }
     }
     else Position=Before+Desired;
     TArray<FHitResult> End;ACEBodySweep::SweepBodyContacts(*World,End,Position,Position+FVector(0,0,.001),Shape,Query);
     TooDeep+=End.ContainsByPredicate([](const FHitResult& H){return H.bStartPenetrating&&H.PenetrationDepth>.2;});
     ++Cases;
    }
   }
  }
  Mob->Destroy();
 }
 AddInfo(FString::Printf(TEXT("Authored creature sliding: %d frames, %d stopped tangents, %d penetrations"),Cases,Stopped,TooDeep));
 TestTrue(TEXT("All four creature fixtures exercise contacts"),Cases>20000);
 TestEqual(TEXT("Authored creature contacts retain available tangential movement"),Stopped,0);
 TestEqual(TEXT("Continuous creature sliding never tunnels into a body"),TooDeep,0);
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERefinedContactOrderingTest,"ACE.Collision.RefinedContactOrdering",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACERefinedContactOrderingTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 ON_SCOPE_EXIT{World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 auto* Creature=World->SpawnActor<AActor>();auto* Sphere=NewObject<USphereComponent>(Creature);
 Creature->SetRootComponent(Sphere);Sphere->InitSphereRadius(40);
 Sphere->ComponentTags.Add(TEXT("ACECreatureBody"));Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);Sphere->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
 Sphere->RegisterComponent();
 auto* WallActor=World->SpawnActor<AActor>();auto* Wall=NewObject<UBoxComponent>(WallActor);
 WallActor->SetRootComponent(Wall);Wall->InitBoxExtent(FVector(1,100,100));
 Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
 Wall->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);Wall->RegisterComponent();
 const FVector From(0,0,0),To(200,0,0);const auto Body=FCollisionShape::MakeCapsule(10,10);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(RefinedContactOrdering),false);
 auto PlaceCreature=[&](const FVector& Center)
 {
  Creature->SetActorLocation(Center);
  // Model a conservative/imprecise backend sphere without changing the
  // authored component radius used by the exact solver. This creates a stable
  // 2 mm disagreement instead of relying on platform-dependent Chaos rounding.
  TestTrue(TEXT("Fixture applies a conservative physics sphere"),Sphere->BodyInstance.UpdateBodyScale(FVector(1.005),true));
  TestEqual(TEXT("Authored sphere radius remains unchanged"),Sphere->GetScaledSphereRadius(),40.f);
 };
 auto RawContact=[&]()
 {
  FHitResult Hit;
  TestTrue(TEXT("Backend sweep finds the early creature candidate"),World->SweepSingleByChannel(Hit,From,To,FQuat::Identity,
   ECC_Pawn,FCollisionShape::MakeSphere(10),Query) && Hit.GetComponent()==Sphere);
  return Hit;
 };
 auto CheckBoth=[&](UPrimitiveComponent* Expected,const TCHAR* Label,bool Penetrating=false)
 {
  FHitResult Hit;
  TestTrue(FString::Printf(TEXT("%s: body sweep retains the nearest exact blocker"),Label),
   ACEBodySweep::SweepBody(*World,Hit,From,To,Body,Query) && Hit.GetComponent()==Expected && Hit.bStartPenetrating==Penetrating);
  TestTrue(FString::Printf(TEXT("%s: body TOI remains relative to the complete segment"),Label),
   Hit.Location.Equals(From+(To-From)*Hit.Time,.01));
  TArray<FHitResult> Contacts;ACEBodySweep::SweepBodyContacts(*World,Contacts,From,To,Body,Query);
  TestTrue(FString::Printf(TEXT("%s: contact sweep retains the nearest exact blocker"),Label),
   Contacts.ContainsByPredicate([&](const FHitResult& H){return H.GetComponent()==Expected && H.bBlockingHit && H.bStartPenetrating==Penetrating;}));
  TestFalse(FString::Printf(TEXT("%s: contact TOIs remain relative to the complete segment"),Label),
   Contacts.ContainsByPredicate([&](const FHitResult& H){return !H.Location.Equals(From+(To-From)*H.Time,.01);}));
  if(!Penetrating)TestFalse(FString::Printf(TEXT("%s: contact sweep omits rejected or later blockers"),Label),
   Contacts.ContainsByPredicate([&](const FHitResult& H){return H.bBlockingHit && H.GetComponent()!=Expected;}));
 };

 // The backend sphere grazes the path; the authored sphere misses it.
 // Both query variants must retry to expose the wall hidden behind that hit.
 PlaceCreature(FVector(40,50.1,0));WallActor->SetActorLocation(FVector(91,0,0));
 FHitResult Rejected=RawContact();
 TestFalse(TEXT("Authored sphere rejects the backend tangent"),ACEBodySweep::RefineCreatureContact(Rejected,From,To,10));
 CheckBoth(Wall,TEXT("Rejected creature before wall"));

 // The exact creature TOI is x=50; Chaos's inflated sphere hits at x=49.8.
 // A wall at x=49.9 must become the winner after refining that first hit.
 PlaceCreature(FVector(100,0,0));WallActor->SetActorLocation(FVector(60.9,0,0));
 FHitResult Delayed=RawContact();const float BackendTime=Delayed.Time;
 TestTrue(TEXT("Authored contact occurs later than the backend candidate"),
  ACEBodySweep::RefineCreatureContact(Delayed,From,To,10) && Delayed.Time>BackendTime);
 CheckBoth(Wall,TEXT("Wall before delayed creature"));

 // Retrying must not lose the saved exact creature if the exposed wall is
 // farther away, or if the next query has no hit at all.
 WallActor->SetActorLocation(FVector(91,0,0));
 CheckBoth(Sphere,TEXT("Delayed creature before wall"));
 Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 CheckBoth(Sphere,TEXT("Delayed creature with no later blocker"));

 // An exact overlap moving inward is a valid time-zero blocker. The
 // reordering fix must never turn it into an ignored escape contact.
 Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);PlaceCreature(FVector(45,0,0));
 CheckBoth(Sphere,TEXT("Exact penetrating creature"),true);
 return true;
}
#endif
