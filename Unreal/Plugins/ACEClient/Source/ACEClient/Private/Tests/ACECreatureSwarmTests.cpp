#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEBodySweep.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECreatureSwarmTest,"ACE.RetailParity.CreatureSwarm",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACECreatureSwarmTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 const FVector Base(-3870000,2700000,10000);
 auto* FloorActor=World->SpawnActor<AActor>();auto* Floor=NewObject<UBoxComponent>(FloorActor);
 FloorActor->SetRootComponent(Floor);Floor->SetBoxExtent(FVector(5000,5000,10));
 Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Floor->SetCollisionResponseToAllChannels(ECR_Block);
 Floor->RegisterComponent();FloorActor->SetActorLocation(Base-FVector(0,0,10));
 const auto Shape=FCollisionShape::MakeCapsule(48,90.75);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(CreatureSwarmRegression),true);
 int32 Cases=0;
 for(uint32 Setup:{0x02000A95u,0x02000037u,0x02001121u})
 {
  TArray<AACEWorldEntityActor*> Mobs;
  for(int32 I=0;I<24;++I)
  {
   FACEWorldObject Mob;Mob.Guid=0x78002000+I;Mob.SetupId=Setup;Mob.Scale=1.2f;
   Mob.ItemType=ACEItemType::Creature;Mob.PhysicsState=ACEPhysicsState::Gravity;
   auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Mob,100,true);Mobs.Add(Actor);
  }
  for(int32 FPS:{30,90,144})for(int32 Mode=0;Mode<3;++Mode)for(int32 Heading=0;Heading<8;++Heading)
  {
   const FVector Direction=FVector(FMath::Cos(Heading*PI/4),FMath::Sin(Heading*PI/4),0);
   FVector Position=Base-Direction*400+FVector(0,0,90.75),Velocity=Direction*600+FVector(0,0,600);
   bool Landed=false;int32 Contacts=0;const double Dt=1./FPS;
   for(int32 Frame=0;Frame<FPS*8 && !Landed;++Frame)
   {
    for(int32 I=0;I<Mobs.Num();++I)
    {
     const double Angle=I*2*PI/Mobs.Num()+(Mode?Frame*Dt*.4:0);
     const FVector Center=Mode==2 && Frame*Dt>.35 ? FVector(Position.X,Position.Y,Base.Z) : Base;
     Mobs[I]->SetActorLocation(Center+FVector(80*FMath::Cos(Angle),80*FMath::Sin(Angle),Mode==1?150:0));
    }
    const FVector Goal=Position+Velocity*Dt-FVector(0,0,490*Dt*Dt);Velocity.Z-=980*Dt;
    const auto Move=ACEBodySweep::MoveAirborne(*World,Position,Goal,Shape,Query,Velocity.Z<=0,.6641741f);
    Position=Move.Position;Landed=Move.bLanded;
    if(!Move.ContactNormal.IsNearlyZero())++Contacts;
    if(!Landed)
    {
     if(Position.Z-Goal.Z < -2 && Velocity.Z>0)Velocity.Z=0;
     const double Into=FVector::DotProduct(Velocity,Move.ContactNormal);
     if(Into<0)Velocity-=Move.ContactNormal*(Into*1.05);
    }
   }
   TestTrue(FString::Printf(TEXT("Jump into swarm lands: setup=%08X fps=%d mode=%d heading=%d contacts=%d height=%.3f velocity=%s"),
    Setup,FPS,Mode,Heading,Contacts,Position.Z-Base.Z,*Velocity.ToString()),Landed);
   TestTrue(TEXT("Swarm descent does not tunnel through the floor"),Position.Z>=Base.Z+90.7);
   ++Cases;
  }
  // A crowd can obstruct the separating push from a small wall overlap.
  // Gravity must still progress along the wall, without crossing it.
  auto* WallActor=World->SpawnActor<AActor>();auto* Wall=NewObject<UBoxComponent>(WallActor);
  WallActor->SetRootComponent(Wall);Wall->SetBoxExtent(FVector(10,1000,1000));
  Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Wall->SetCollisionResponseToAllChannels(ECR_Block);
  Wall->RegisterComponent();WallActor->SetActorLocation(Base+FVector(-10,0,0));
  TArray<FACEDatCollisionShape> Spheres;bool BSP=false;Dat->GetSetupCollisionShapes(Setup,Spheres,BSP);
  double Width=0;
  for(const auto& S:Spheres)for(double Z:{48.,133.5})
   Width=FMath::Max(Width,FMath::Sqrt(FMath::Max(0.,FMath::Square(48.+S.Radius*120)-FMath::Square(Z-S.Origin.Z*120))));
  for(double Depth:{.02,.2,2.})
  {
   for(auto* Mob:Mobs)Mob->SetActorLocation(Base+FVector(48-Depth+Width+.01,0,250-90.75));
   FVector Position=Base+FVector(48-Depth,0,250);bool Landed=false;
   for(int32 Frame=0;Frame<300 && !Landed;++Frame)
   {
    const auto Move=ACEBodySweep::MoveAirborne(*World,Position,Position-FVector(0,0,5),Shape,Query,true,.6641741f);
    Position=Move.Position;Landed=Move.bLanded;
   }
   TestTrue(FString::Printf(TEXT("Crowd beside wall permits falling: setup=%08X depth=%.2f height=%.3f"),Setup,Depth,Position.Z-Base.Z),Landed);
   TestTrue(TEXT("Crowd cannot push the player farther through the wall"),Position.X>=Base.X+48-Depth-.05);
  }
  WallActor->Destroy();for(auto* Mob:Mobs)Mob->Destroy();
 }
 AddInfo(FString::Printf(TEXT("Completed %d airborne swarm cases with real low/mid/high creature setups"),Cases));
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
