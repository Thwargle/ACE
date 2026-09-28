#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEWorldEntityActor.h"
#include "ACEDatSubsystem.h"
#include "VR/ACEVRPose.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERemoteMovementReplayTest,"ACE.RetailParity.RemoteMovementReplay",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACERemoteMovementReplayTest::RunTest(const FString&)
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
 P.bHasContactState=true;P.bHasTeleportSequence=true;
 const FVector Origin=P.ToUnrealLocation(100);
 TArray<AActor*> Geometry;
 auto Box=[&](FVector Center,FVector Extent)
 {
  auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UBoxComponent>(A);A->SetRootComponent(C);
  C->SetBoxExtent(Extent);C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  C->SetCollisionObjectType(ECC_WorldStatic);C->SetCollisionResponseToAllChannels(ECR_Block);
  C->RegisterComponent();A->SetActorLocation(Origin+Center);Geometry.Add(A);return A;
 };
 Box(FVector(0,0,-10),FVector(2000,2000,10));
 auto Spawn=[&]()
 {
  FACEWorldObject O;O.SetupId=0x02000001;O.bIsPlayer=true;O.ItemType=ACEItemType::Creature;
  O.PhysicsState=ACEPhysicsState::Gravity;O.bHasPosition=true;O.Position=P;
  auto* A=World->SpawnActor<AACEWorldEntityActor>();A->InitializeFromObject(O,100,false);return A;
 };
 for(int FPS:{30,90,144})
 {
  const float Dt=1.f/FPS;
  // F748 has its own sequence, not a newer VectorUpdate. A late airborne
  // position must not restart the already-running jump or replay its velocity.
  auto* A=Spawn();A->ApplyPhysicsVelocity(FVector(0,4,5));
  const FVector Start=A->GetActorLocation();double MaxArcError=0;
  for(int I=1;I<=FPS*.8;++I)
  {
   if(I%FMath::Max(1,FPS/5)==0)
   {
    const double Sent=FMath::Max(0.,(I-1.)/FPS-.15);
    FACEPosition Late=P;Late.bIsGrounded=false;Late.bHasVelocity=true;
    Late.SetLocationFromUnreal(Start+FVector(0,400*Sent,500*Sent-490*Sent*Sent),100);
    Late.Velocity=FVector(0,4,5-9.8*Sent);A->ApplyACEPosition(Late);
   }
   A->Tick(Dt);const double T=double(I)/FPS;
   MaxArcError=FMath::Max(MaxArcError,FVector::Distance(A->GetActorLocation(),Start+FVector(0,400*T,500*T-490*T*T)));
  }
  AddInfo(FString::Printf(TEXT("Delayed jump %d FPS: %.3f cm arc error"),FPS,MaxArcError));
  TestTrue(TEXT("Late airborne position packets do not rewind the jump"),MaxArcError<1);
  A->Destroy();

  A=Spawn();A->ApplyPhysicsVelocity(FVector(0,4,5));
  for(int I=0;I<FPS/2;++I)A->Tick(Dt);
  FACEPosition Land=P;Land.Location.Y+=4.3;A->ApplyACEPosition(Land);
  TestTrue(TEXT("Early landing confirmation waits for local physical contact"),A->bHavePhysicsVelocity && A->PendingRemoteLanding.IsSet());
  for(int I=0;I<FPS*2;++I)if(A->IsActorTickEnabled())A->Tick(Dt);
  TestTrue(TEXT("Queued landing correction keeps ticking until consumed"),!A->PendingRemoteLanding.IsSet());
  TestTrue(TEXT("Landing converges to the confirmed position without replaying the jump"),
   FVector::Dist2D(A->GetActorLocation(),Land.ToUnrealLocation(100))<1 && FMath::Abs(A->GetActorLocation().Z-Origin.Z-1)<.1);
  A->Destroy();

  // Same network latency for spawn and motion, then +/-50ms arrival jitter
  // around 1Hz positions. This previously suppressed forward transport when
  // the server sample was just behind the displayed feet.
  A=Spawn();A->RemoteMotion.bMoving=true;A->RemoteMotion.Forward=1;A->RemoteMotion.ForwardUnitsPerSecond=4;
  FVector Previous=A->GetActorLocation();double MinAdvance=DBL_MAX,MaxSide=0;int Packet=1;
  for(int I=1;I<=FPS*4;++I)
  {
   const double Now=double(I-1)/FPS;
   if(Packet<=3 && Now>=Packet+(Packet%2?-.05:.05))
   {
    FACEPosition Update=P;Update.Location.Y+=Packet*4;Update.Location.X+=(Packet%2?.02:-.02);
    Update.Location.Z+=.15;A->ApplyACEPosition(Update);++Packet;
   }
   A->Tick(Dt);const FVector Current=A->GetActorLocation();
   MinAdvance=FMath::Min(MinAdvance,Current.Y-Previous.Y);
   MaxSide=FMath::Max(MaxSide,FMath::Abs(Current.X-Origin.X));Previous=Current;
  }
  AddInfo(FString::Printf(TEXT("Jittered walking %d FPS: minimum step %.3f cm, lateral %.3f cm"),FPS,MinAdvance,MaxSide));
  // Frame quantization of packet delivery allows sub-millimetre residuals.
  TestTrue(TEXT("Packet arrival jitter does not visibly reverse a straight-running avatar"),MinAdvance>=-.1);
  // Position timing legitimately requires an XY correction. Bound lateral
  // noise by the supplied 2cm rather than letting it accumulate between packets.
  TestTrue(TEXT("Jittered corrections preserve the server's lateral corridor"),MaxSide<=2.01);
  A->Destroy();

  // A ground-only height discrepancy cannot convert otherwise sub-tolerance
  // sideways noise into a correction while standing beside another player.
  A=Spawn();double SideNoise=0;
  for(int I=0;I<FPS*2;++I)
  {
   if(I%(FPS/2)==0){FACEPosition Update=P;Update.Location.X+=(I%FPS?.02:-.02);Update.Location.Z+=.15;A->ApplyACEPosition(Update);}
   A->Tick(Dt);SideNoise=FMath::Max(SideNoise,FMath::Abs(A->GetActorLocation().X-Origin.X));
  }
  TestTrue(TEXT("Sphere seating differences do not defeat the 5cm position tolerance"),SideNoise<.01);
  A->Destroy();

  // A real teleport is an epoch change, even when the destination is nearby.
  A=Spawn();FACEPosition Portal=P;Portal.Location.Y+=3;Portal.TeleportSequence=1;
  A->ApplyACEPosition(Portal);
  TestTrue(TEXT("Nearby server teleport snaps once instead of walking through the intervening wall"),
   FVector::Dist2D(A->GetActorLocation(),Portal.ToUnrealLocation(100))<.1);
  A->Destroy();
  A=Spawn();FACEVRPose JumpPose;JumpPose.Version=2;JumpPose.Root=(Origin+FVector(0,0,100))/100;
  // Simulate a newer F748 landing/zero velocity while an older jump is buffered.
  A->ApplyPhysicsVelocity(FVector::ZeroVector);
  for(int I=0;I<FPS;++I)A->ApplyRemoteVRRoot(JumpPose,Dt);
  TestTrue(TEXT("Newer ground state cannot pull a buffered VR jump onto the floor"),A->GetActorLocation().Z-Origin.Z>99);
  A->Destroy();

  // Ordinary correction is still physics movement in retail. Test the visible
  // root and its selection capsule, not just a collision-constrained predictor.
  auto* Wall=Box(FVector(0,200,200),FVector(400,10,200));
  A=Spawn();FACEPosition Correction=P;Correction.Location.Y+=4;A->ApplyACEPosition(Correction);
  double MaxY=0,MaxPickError=0;
  for(int I=0;I<FPS;++I)
  {
   A->Tick(Dt);MaxY=FMath::Max(MaxY,A->GetActorLocation().Y-Origin.Y);
   const FVector Pick=A->CollisionProxy->GetComponentLocation();
   const FVector Expected=A->GetActorTransform().TransformPosition(A->CollisionProxy->GetRelativeLocation());
   MaxPickError=FMath::Max(MaxPickError,FVector::Distance(Pick,Expected));
  }
  TestTrue(TEXT("A normal server correction cannot carry the displayed player through a wall"),MaxY<190);
  TestTrue(TEXT("Selection capsule follows the displayed actor throughout reconciliation"),MaxPickError<.01);
  FHitResult PickHit;const FVector PickCenter=A->CollisionProxy->GetComponentLocation();
  World->LineTraceSingleByChannel(PickHit,PickCenter-FVector(200,0,0),PickCenter+FVector(200,0,0),ECC_Visibility);
  TestTrue(TEXT("A selection ray hits the corrected visual body rather than the cached server position"),PickHit.GetActor()==A);
  A->Destroy();Wall->Destroy();

  // Use the real 20 Hz pose buffer at rendering cadence. Applying another
  // locomotion filter must not add a speed-dependent trailing body offset.
  A=Spawn();FACERemoteVRPose Buffer;uint32 Sequence=0;double MaxLag=0;
  for(int I=0;I<FPS*3;++I)
  {
   const double Now=10.+double(I)/FPS;
   while(Sequence*.05<=double(I)/FPS+1.e-7)
   {
    FACEVRPose Pose;Pose.Version=2;Pose.Sequence=Sequence+1;Pose.ReceivedAt=10.+Sequence*.05;
    Pose.Root=(Origin+FVector(0,400*Sequence*.05,1))/100;Buffer.Add(Pose);++Sequence;
   }
   const auto Sample=Buffer.Sample(Now);A->ApplyRemoteVRRoot(Sample,Dt);
   if(I>FPS)MaxLag=FMath::Max(MaxLag,FMath::Abs(A->GetActorLocation().Y-Sample.Root.Y*100));
  }
  AddInfo(FString::Printf(TEXT("Buffered VR %d FPS: %.3f cm extra root lag"),FPS,MaxLag));
  TestTrue(TEXT("VR root and tracked limbs use one buffered movement timeline"),MaxLag<1);
  A->EndRemoteVRRoot();const FVector Before=A->GetActorLocation();
  FACEVRPose Reacquired=Buffer.Current;Reacquired.Root.Y+=2;
  A->ApplyRemoteVRRoot(Reacquired,Dt);
  TestTrue(TEXT("Tracking reacquisition blends into an existing avatar without teleporting"),
   FVector::Distance(A->GetActorLocation(),Before)<800.*Dt+.1);
  A->Destroy();
 }
 // Packet corrections while ascending/descending real risers. Sample the
 // reference from the same DAT body, then replay jittered 1Hz wire positions
 // against an independently simulated/rendered observer.
 for(int S=0;S<4;++S)Box(FVector(0,200+S*180,10+S*10),FVector(400,90,10+S*10));
 for(int FPS:{30,90,144})for(int Direction:{1,-1})
 {
  const float Dt=1.f/FPS;FACEPosition Start=P;
  Start.Location.Y+=Direction>0?-1:8;Start.Location.Z+=Direction>0?0:.8;
  auto MakeWalker=[&]()
  {
   auto* A=Spawn();A->bHaveRemotePredict=false;A->ApplyACEPosition(Start);
   A->RemoteMotion.bMoving=true;A->RemoteMotion.Forward=Direction;A->RemoteMotion.ForwardUnitsPerSecond=4*Direction;
   return A;
  };
  auto* Reference=MakeWalker();TArray<FVector> Path;Path.Add(Reference->GetActorLocation());
  for(int I=0;I<FPS*2;++I){Reference->Tick(Dt);Path.Add(Reference->GetActorLocation());}
  Reference->Destroy();auto* Observer=MakeWalker();double MaxPenetration=0;
  for(int I=1;I<=FPS*2;++I)
  {
   if(I==FPS+FMath::CeilToInt(.05*FPS))
   {
    FACEPosition Packet=Start;Packet.SetLocationFromUnreal(Path[FPS],100);Observer->ApplyACEPosition(Packet);
   }
   Observer->Tick(Dt);const FVector Feet=Observer->GetActorLocation()-Origin;
   const double Radius=Observer->MovementSweepRadius;
   const FVector Center=Feet+FVector(0,0,Observer->MovementBodyOffsetZ-Observer->MovementHalfHeight+Radius);
   for(int S=0;S<4;++S)
   {
    const FBox Tread(FVector(-400,110+S*180,0),FVector(400,290+S*180,20+S*20));
    MaxPenetration=FMath::Max(MaxPenetration,Radius-FMath::Sqrt(Tread.ComputeSquaredDistanceToPoint(Center)));
   }
  }
  const double Travel=(Observer->GetActorLocation().Y-Path[0].Y)*Direction;
  AddInfo(FString::Printf(TEXT("Packet stairs %d FPS direction %d: %.3f cm travel, %.3f cm penetration"),FPS,Direction,Travel,MaxPenetration));
  TestTrue(TEXT("Packet-driven stairs retain forward progress"),Travel>700);
  TestTrue(TEXT("Packet correction cannot embed the visible body in stair risers"),MaxPenetration<1);
  Observer->Destroy();
 }
 for(auto* A:Geometry)if(IsValid(A))A->Destroy();
 GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
