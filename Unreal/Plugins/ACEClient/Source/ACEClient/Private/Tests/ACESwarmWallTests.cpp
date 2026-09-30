#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../ACEBodySweep.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACETypes.h"
#include "ACEWorldEntityActor.h"
#include "ACECreatureFixtures.h"
#include "Components/SphereComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESwarmWallTest,"ACE.Collision.SwarmWall",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACESwarmWallTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 FACEPosition Seed;Seed.CellId=0x01430171;Seed.Location=FVector(49.011993,-74.999023,0);
 auto* Room=World->SpawnActor<AACEEnvCellActor>();
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(192,67*192,0),100);
 TestNotNull(TEXT("Reported swarm hallway exists in retail DAT"),Dat->GetOrBuildEnvCellMesh(Seed.CellId,100));
 TestTrue(TEXT("Reported swarm hallway loads from retail DAT"),Room->LoadEnvCell(Seed.CellId,Origin,100));
 Room->SetEnvCellCollisionActive(true);
 const auto Shape=FCollisionShape::MakeCapsule(48,90.75);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(SwarmWallRegression),true);
 const FVector Center=Seed.ToUnrealLocation(100)+FVector(0,0,90.75);
 int32 Cases=0,ForcedMoves=0,ExcessTravel=0,WallOverlaps=0;
 int32 AuthoredCases[4]={},AuthoredOverlaps[4]={};
 auto* Batch=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Collision.BatchCrowdQueries"));
 const int OriginalBatch=Batch->GetInt();ON_SCOPE_EXIT{Batch->Set(OriginalBatch,ECVF_SetByCode);};
 Batch->Set(1,ECVF_SetByCode);
 auto SpawnBody=[&](float Radius,const FVector& At)
 {
  auto* Mob=World->SpawnActor<AActor>();auto* Body=NewObject<USphereComponent>(Mob);Mob->SetRootComponent(Body);
  Body->InitSphereRadius(Radius);Body->ComponentTags.Add(TEXT("ACECreatureBody"));
  Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Body->SetCollisionResponseToAllChannels(ECR_Ignore);
  Body->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);Body->RegisterComponent();Mob->SetActorLocation(At);
  return Mob;
 };
 for(float X:{-100.f,0.f,100.f})for(float Y:{-100.f,0.f,100.f})for(int32 Angle=0;Angle<8;++Angle)
 {
  const FVector Start=Center+FVector(X,Y,0);
  const FVector Direction(FMath::Cos(Angle*PI/4),FMath::Sin(Angle*PI/4),0);
  FHitResult Wall;
  if(!ACEBodySweep::Sweep(*World,Wall,Start,Start+Direction*200,Shape,Query)
   || Wall.bStartPenetrating || FMath::Abs(Wall.Normal.Z)>.1f)continue;
  const FVector Normal=Wall.Normal.GetSafeNormal2D();
  const FVector Touch=Wall.Location+Normal*.25;
  const FVector Tangent(-Normal.Y,Normal.X,0);
  // A server-driven creature may move into a standing player. Retail clips
  // the player's requested travel; it does not force the body out of the mob.
  const FVector Standing=Touch+Normal*30;
  for(float Depth:{2.f,12.f,35.f,70.f})for(float Height:{-42.75f,0.f,42.75f})
  {
   auto* Mob=SpawnBody(40,Standing+Normal*(88-Depth)+FVector(0,0,Height));
   TArray<FHitResult> Contacts;
   ACEBodySweep::SweepBodyContacts(*World,Contacts,Standing,Standing+FVector(0,0,.001),Shape,Query);
   const FHitResult* Creature=Contacts.FindByPredicate([](const FHitResult& H){return H.bStartPenetrating && ACEBodySweep::IsCreatureBody(H);});
   if(Creature)
   {
    const FHitResult& Contact=*Creature;
    ++Cases;
    const FVector Recovered=ACEBodySweep::Recover(*World,Standing,Contact.Normal.GetSafeNormal2D()*(Contact.PenetrationDepth+.2),Contact,Shape,Query);
    ForcedMoves+=!Recovered.Equals(Standing,.01);
    FHitResult Block;
    ACEBodySweep::Sweep(*World,Block,Standing,Standing+Tangent*10,Shape,Query);
    TestFalse(TEXT("An existing creature overlap permits tangential escape"),ACEBodySweep::IsCreatureBody(Block));
    ACEBodySweep::Sweep(*World,Block,Standing,Standing-Normal*10,Shape,Query);
    TestFalse(TEXT("An existing creature overlap permits outward escape"),ACEBodySweep::IsCreatureBody(Block));
    TestTrue(TEXT("Travel deeper into the creature still blocks"),ACEBodySweep::Sweep(*World,Block,Standing,Standing+Normal*5,Shape,Query));
    TestTrue(TEXT("Escaping the creature still sweeps the hallway wall"),ACEBodySweep::Sweep(*World,Block,Standing,Standing-Normal*200,Shape,Query)
     && !ACEBodySweep::IsCreatureBody(Block));
   }
   Mob->Destroy();
  }
  for(int32 Profile=0;Profile<3;++Profile)
  {
   FACEWorldObject Object;Object.Guid=0x72020000+Profile;Object.bHasPosition=true;Object.Position=Seed;
   ACECreatureFixtures::Apply(Object,Profile);
   TArray<FACEDatCollisionShape> Spheres;bool BSP=false;Dat->GetSetupCollisionShapes(Object.SetupId,Spheres,BSP);
   double Envelope=0;
   for(const auto& Sphere:Spheres)for(double Z:{48.,133.5})
    Envelope=FMath::Max(Envelope,FMath::Sqrt(FMath::Max(0.,
     FMath::Square(48.+Sphere.Radius*100*Object.Scale)-FMath::Square(Z-Sphere.Origin.Z*100*Object.Scale))));
   auto* Creature=World->SpawnActor<AACEWorldEntityActor>();Creature->InitializeFromObject(Object,100,true);
   // Isolate the creature's permission to escape; the full-room swarm cases
   // below independently verify that those escapes still stop at architecture.
   FCollisionQueryParams CreatureOnly=Query;CreatureOnly.AddIgnoredActor(Room);
   for(double Depth:{2.,20.})
   {
    Creature->SetActorLocation(Standing-FVector(0,0,90.75)+Normal*(Envelope-Depth));
    FHitResult Contact;
    TestTrue(*FString::Printf(TEXT("%s fixture actually overlaps the standing player"),*Object.Name),
     ACEBodySweep::SweepBody(*World,Contact,Standing,Standing+Normal*.001,Shape,Query)
      && Contact.bStartPenetrating && ACEBodySweep::IsCreatureBody(Contact));
    const FVector Still=ACEBodySweep::Recover(*World,Standing,Normal*20,Contact,Shape,Query);
    TestTrue(TEXT("An authored creature cannot move the player without input"),Still.Equals(Standing,.01));
    FHitResult Hit;
    TestFalse(*FString::Printf(TEXT("%s allows a tangential escape from overlap"),*Object.Name),
     ACEBodySweep::SweepBody(*World,Hit,Standing,Standing+Tangent*10,Shape,CreatureOnly));
    TestFalse(*FString::Printf(TEXT("%s allows moving away from overlap"),*Object.Name),
     ACEBodySweep::SweepBody(*World,Hit,Standing,Standing-Normal*10,Shape,CreatureOnly));
    TestTrue(*FString::Printf(TEXT("%s blocks moving deeper into its body"),*Object.Name),
     ACEBodySweep::SweepBody(*World,Hit,Standing,Standing+Normal*5,Shape,CreatureOnly)&&ACEBodySweep::IsCreatureBody(Hit));
    ++Cases;
   }
   Creature->Destroy();
  }
  FRandomStream Random(Seed.CellId+Angle);
  // Fixed seed: exercise grounded slides and ascent through dense moving
  // sphere contacts at both straight walls and rounded doorway edges.
  for(int32 Trial=0;Trial<88;++Trial)
  {
   TArray<AActor*> Swarm;
   FCollisionQueryParams Environment=Query;
   const int32 Profile=Trial<80?-1:(Trial-80)/2; // each archetype, then mixed
   for(int32 I=0;I<12;++I)
   {
    const float Radius=Random.FRandRange(20,60);
    const FVector At=Touch+Normal*Random.FRandRange(20,90)+Tangent*Random.FRandRange(-90,90);
    AActor* Mob;
    if(Profile<0)Mob=SpawnBody(Radius,At+FVector(0,0,Random.FRandRange(-70,70)));
    else
    {
     FACEWorldObject Object;Object.Guid=0x72000000+I;Object.bHasPosition=true;Object.Position=Seed;
     ACECreatureFixtures::Apply(Object,Profile==3?I:Profile);
     auto* Creature=World->SpawnActor<AACEWorldEntityActor>();Creature->InitializeFromObject(Object,100,true);
     Creature->SetActorLocation(At-FVector(0,0,90.75));Mob=Creature;
    }
    Environment.AddIgnoredActor(Mob);Swarm.Add(Mob);
   }
   if(Profile>=0)
   {
    TArray<FHitResult> Contacts;ACEBodySweep::SweepBodyContacts(*World,Contacts,Touch,Touch+FVector(0,0,.001),Shape,Query);
    AuthoredOverlaps[Profile]+=Contacts.ContainsByPredicate([](const FHitResult& H){return H.bStartPenetrating&&ACEBodySweep::IsCreatureBody(H);});
   }
   for(int32 Mode=0;Mode<3;++Mode)
   {
    FHitResult Contact;
    FVector Recovered=Touch;
    FVector To=Touch-Normal*Random.FRandRange(0,30)+Tangent*Random.FRandRange(-30,30);
    if(Mode==2)To.Z+=25;
    if(Profile>=0)
    {
     FHitResult Reference,Optimized;Batch->Set(0,ECVF_SetByCode);
     const bool A=ACEBodySweep::SweepBody(*World,Reference,Touch,To,Shape,Query);
     Batch->Set(1,ECVF_SetByCode);
     const bool B=ACEBodySweep::SweepBody(*World,Optimized,Touch,To,Shape,Query);
     TestEqual(TEXT("Batched queries preserve real-creature obstruction"),B,A);
     if(A&&B)TestTrue(TEXT("Batched queries preserve real-creature contact position"),
      Reference.Location.Equals(Optimized.Location,.01)&&FMath::IsNearlyEqual(Reference.Time,Optimized.Time,.0001f));
    }
    if(Mode==0 && ACEBodySweep::Sweep(*World,Contact,Touch,Touch+FVector(0,0,.1),Shape,Query) && Contact.bStartPenetrating)
    {
     Recovered=ACEBodySweep::Recover(*World,Touch,Contact.Normal.GetSafeNormal2D()*(Contact.PenetrationDepth+.2),Contact,Shape,Query);
     if(ACEBodySweep::IsCreatureBody(Contact))ForcedMoves+=!Recovered.Equals(Touch,.01);
    }
    else if(Mode==1 && ACEBodySweep::Sweep(*World,Contact,Touch,To,Shape,Query))
     Recovered=ACEBodySweep::SlideGrounded(*World,Touch,To,Contact,Shape,Query);
    else if(Mode==2)
     Recovered=ACEBodySweep::MoveAirborne(*World,Touch,To,Shape,Query,false).Position;
    ++Cases;
    if(Profile>=0)++AuthoredCases[Profile];
    if(Mode>0 && FVector::Dist(Recovered,Touch)>FVector::Dist(To,Touch)+.5)
    {
     ++ExcessTravel;
     if(ExcessTravel<=6)AddInfo(FString::Printf(TEXT("Crowd displaced player beyond requested travel: mode=%d intended=%.2f actual=%.2f"),Mode,FVector::Dist(To,Touch),FVector::Dist(Recovered,Touch)));
    }
    TArray<FHitResult> EndContacts;ACEBodySweep::SweepBodyContacts(*World,EndContacts,Recovered,Recovered+FVector(0,0,.001),Shape,Environment);
    WallOverlaps+=EndContacts.ContainsByPredicate([](const FHitResult& H){return H.bStartPenetrating && H.PenetrationDepth>.5;});
   }
   for(auto* Mob:Swarm)Mob->Destroy();
  }
 }
 TestTrue(TEXT("Real hallway fixture exercises creature penetration against walls"),Cases>0);
 TestEqual(TEXT("Creature overlaps never force displacement without input"),ForcedMoves,0);
 TestEqual(TEXT("Crowded movement cannot add a body-sized shove to player travel"),ExcessTravel,0);
 TestEqual(TEXT("Crowded movement stays out of hallway walls and floors"),WallOverlaps,0);
 AddInfo(FString::Printf(TEXT("Checked %d crowd recovery cases at 0x01430171"),Cases));
 for(int32 Profile=0;Profile<4;++Profile)
 {
  const FString Name=Profile==3?TEXT("Mixed swarm"):ACECreatureFixtures::Models[Profile].Name;
  TestTrue(*FString::Printf(TEXT("%s exercises real authored contacts"),*Name),AuthoredOverlaps[Profile]>0);
  AddInfo(FString::Printf(TEXT("%s: %d wall/strafe/jump cases; %d initially overlapping swarms"),*Name,AuthoredCases[Profile],AuthoredOverlaps[Profile]));
 }
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
