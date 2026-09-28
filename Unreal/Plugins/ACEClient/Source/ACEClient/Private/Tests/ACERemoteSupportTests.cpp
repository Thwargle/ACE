#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEWorldEntityActor.h"
#include "ACEDatSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERemoteSupportTest,"ACE.RetailParity.RemoteSupport",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACERemoteSupportTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.SetCurrentWorld(World);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 TestTrue(TEXT("Retail player body DAT loads"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
 FACEPosition P;P.CellId=0x016C0101;P.Location=FVector(50,50,100);P.bIsGrounded=true;
 const FVector Origin=P.ToUnrealLocation(100);
 TArray<AActor*> Geometry;
 auto Box=[&](FVector Center,FVector Extent)
 {
  auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UBoxComponent>(A);A->SetRootComponent(C);
  C->SetBoxExtent(Extent);C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  C->SetCollisionObjectType(ECC_WorldStatic);C->SetCollisionResponseToAllChannels(ECR_Block);
  C->RegisterComponent();A->SetActorLocation(Origin+Center);Geometry.Add(A);return A;
 };
 Box(FVector(0,0,-10),FVector(400,1200,10));
 // Four 20 cm risers, within the actual player's DAT step height. A full
 // width wall/ceiling fixture below ensures step-up never bypasses solids.
 for(int I=0;I<4;++I)Box(FVector(0,200+I*180,10+I*10),FVector(400,90,10+I*10));
 for(int FPS:{30,90,144})for(int Direction:{1,-1})
 {
  FACEWorldObject O;O.SetupId=0x02000001;O.bIsPlayer=true;O.ItemType=ACEItemType::Creature;
  O.bHasPosition=true;O.Position=P;O.Position.Location.Y+=(Direction>0?-1.f:8.f);
  O.Position.Location.Z+=Direction>0?0:.8f;
  auto* Walker=World->SpawnActor<AACEWorldEntityActor>();Walker->InitializeFromObject(O,100,false);
  Walker->RemoteMotion.bMoving=true;Walker->RemoteMotion.Forward=Direction;
  Walker->RemoteMotion.ForwardUnitsPerSecond=4*Direction;
  const FVector Start=Walker->GetActorLocation();double MaxPenetration=0;
  for(int I=0;I<FPS*2;++I)
  {
   Walker->Tick(1.f/FPS);
   // Actual lower sphere may extend over a tread while its center is still
   // outside it. Compare against every riser, not merely a center floor ray.
   const FVector Feet=Walker->GetActorLocation()-Origin;
   const double Radius=Walker->MovementSweepRadius;
   const FVector Center=Feet+FVector(0,0,Walker->MovementBodyOffsetZ-Walker->MovementHalfHeight+Radius);
   for(int S=0;S<4;++S)
   {
    const FBox Tread(FVector(-400,110+S*180,0),FVector(400,290+S*180,20+S*20));
    MaxPenetration=FMath::Max(MaxPenetration,Radius-FMath::Sqrt(Tread.ComputeSquaredDistanceToPoint(Center)));
   }
  }
  const double Travel=(Walker->GetActorLocation().Y-Start.Y)*Direction;
  AddInfo(FString::Printf(TEXT("Remote stairs %d FPS direction %d: %.2f cm travel, %.3f cm penetration"),FPS,Direction,Travel,MaxPenetration));
  TestTrue(TEXT("Remote walks the stairs without waiting for a corrective packet"),Travel>700);
  TestTrue(TEXT("Displayed lower sphere stays out of stair risers"),MaxPenetration<1.0);
  Walker->Destroy();
 }
 for(auto* A:Geometry)A->Destroy();Geometry.Reset();
 Box(FVector(0,0,-10),FVector(400,2000,10));
 FACEWorldObject O;O.SetupId=0x02000001;O.bIsPlayer=true;O.ItemType=ACEItemType::Creature;O.bHasPosition=true;O.Position=P;
 auto* Walker=World->SpawnActor<AACEWorldEntityActor>();Walker->InitializeFromObject(O,100,false);
 // Warm geometry and compare the same collision path before/after. Isolated
 // CPU timings are informational; correctness has no machine-speed threshold.
 for(int Phase=0;Phase<4;++Phase)
 {
  const double Begin=FPlatformTime::Seconds();FVector Result;
  for(int I=0;I<3000;++I)Result=Walker->ResolvePredictedMovement(Origin+FVector(0,0,1),Origin+FVector(0,4,1));
  AddInfo(FString::Printf(TEXT("Remote supported step phase %d: %.3f us/call, result %s"),Phase,
   (FPlatformTime::Seconds()-Begin)*1.e6/3000,*(Result-Origin).ToString()));
  TestTrue(TEXT("Flat supported step preserves velocity"),Result.Equals(Origin+FVector(0,4,1),.1));
 }
 Walker->Destroy();
 for(bool LowCeiling:{false,true})
 {
  auto* Obstacle=Box(FVector(0,200,LowCeiling?10:150),FVector(400,90,LowCeiling?10:150));
  AActor* Ceiling=LowCeiling?Box(FVector(0,250,195),FVector(400,300,10)):nullptr;
  for(int FPS:{30,90,144})
  {
   auto* Blocked=World->SpawnActor<AACEWorldEntityActor>();Blocked->InitializeFromObject(O,100,false);
   Blocked->RemoteMotion.bMoving=true;Blocked->RemoteMotion.Forward=1;Blocked->RemoteMotion.ForwardUnitsPerSecond=4;
   for(int I=0;I<FPS;++I)Blocked->Tick(1.f/FPS);
   const FVector Final=Blocked->GetActorLocation()-Origin;
   TestTrue(LowCeiling?TEXT("A valid riser cannot bypass low headroom"):TEXT("Step-up cannot cross a full-height wall"),Final.Y<100 && Final.Z<2);
   Blocked->Destroy();
  }
  Obstacle->Destroy();if(Ceiling)Ceiling->Destroy();
 }
 for(auto* A:Geometry)if(IsValid(A))A->Destroy();
 GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
