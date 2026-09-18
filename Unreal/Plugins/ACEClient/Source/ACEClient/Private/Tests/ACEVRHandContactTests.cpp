#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VR/ACEVRHandCollision.h"
#include "VR/ACEVRMath.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRHandContactTest,"ACE.VR.HandContact",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEVRHandContactTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* Wall=World->SpawnActor<AActor>();
 auto* Box=NewObject<UBoxComponent>(Wall); Wall->AddInstanceComponent(Box); Wall->SetRootComponent(Box);
 Box->SetBoxExtent(FVector(2,300,300)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
 Box->RegisterComponent(); Box->SetWorldLocation(FVector(50,0,100));
 FCollisionQueryParams Query(SCENE_QUERY_STAT(VRHandContactTest),false);
 FACEVRContactShape Palm; Palm.Extent=FVector(6);
 TArray<FACEVRContactShape> Shapes{Palm}; FACEVRContactState Hand;
 const FVector Seed(0,0,100);
 auto Move=[&](FVector P,FRotator R=FRotator::ZeroRotator)
 { ACEVRHandCollision::Move(World,Hand,FTransform(R,P),Seed,Shapes,Query); };
 Move(FVector(20,0,100)); TestTrue(TEXT("Free hand follows tracking exactly"),Hand.Pose.GetLocation().Equals(FVector(20,0,100),.01));
 Move(FVector(90,0,100));
 TestTrue(TEXT("Fast hand stops at the near side of a thin wall"),Hand.bContact && Hand.Pose.GetLocation().X<42.1 && Hand.Pose.GetLocation().X>40);
 for(int I=1;I<=20;++I) Move(FVector(90,I*4,100));
 TestTrue(TEXT("Pressure preserves hand sliding along the wall"),FMath::Abs(Hand.Pose.GetLocation().Y-80)<.1 && Hand.Pose.GetLocation().X<42.1);
 TestFalse(TEXT("Contact pose stays out of world geometry"),ACEVRHandCollision::Overlaps(World,Hand.Pose,Shapes,Query));
 Move(FVector(20,80,100)); TestFalse(TEXT("Withdrawing releases resistance"),Hand.bContact);
 TestTrue(TEXT("Withdrawing recovers one-to-one tracking"),Hand.Pose.GetLocation().Equals(FVector(20,80,100),.1));
 // A sword's tip rotates into the wall although the grip remains stationary.
 FACEVRContactShape Blade; Blade.Local=FTransform(FVector(40,0,0)); Blade.Extent=FVector(40,2,2);
 Shapes.Add(Blade); Hand.Reset(); Move(Seed,FRotator(0,90,0)); Move(Seed);
 TestTrue(TEXT("Rotating equipment stops before crossing the wall"),Hand.bContact && FMath::Abs(Hand.Pose.Rotator().Yaw)>20);
 TestFalse(TEXT("Stopped blade does not penetrate"),ACEVRHandCollision::Overlaps(World,Hand.Pose,Shapes,Query));
 Move(Seed,FRotator(0,90,0)); TestFalse(TEXT("Rotating a blade away releases contact"),Hand.bContact);
 Move(Seed);
 // The desired orientation still hits the wall at the old grip position, but
 // is clear once the grip retreats. Coupling both motions used to trap it.
 Move(Seed-FVector(40,0,0));
 TestTrue(TEXT("Retreating while turning a caught blade recovers the full grip pose"),
  Hand.Pose.Equals(FTransform(Seed-FVector(40,0,0)),.02));
 TestFalse(TEXT("Released blade has no lingering contact"),Hand.bContact);
 Hand.Reset(); Move(Seed,FRotator(0,90,0)); Move(Seed);
 Move(Seed+FVector(0,60,0));
 TestTrue(TEXT("Blocked blade rotation cannot freeze sliding along a wall"),FMath::Abs(Hand.Pose.GetLocation().Y-60)<.1);
 TestFalse(TEXT("Sliding with angular contact cannot penetrate the wall"),ACEVRHandCollision::Overlaps(World,Hand.Pose,Shapes,Query));

 // Small controller rotations continue while dragging real equipment. Exercise
 // slow motion too: a skin proportional to travel collapses at low frame deltas.
 for (float FrameTravel : {.15f,1.f,4.f})
 {
  Hand.Reset(); Move(Seed,FRotator(0,90,0)); Move(Seed);
  for (int32 I=1;I<=60;++I)
  {
   const float Y=I*FrameTravel;
   Move(Seed+FVector(0,Y,0),FRotator(FMath::Sin(I*.17f)*1.5f,FMath::Sin(I*.11f)*2.f,0));
   TestTrue(TEXT("Equipment tangential travel survives small live tracking rotations"),FMath::Abs(Hand.Pose.GetLocation().Y-Y)<1.f);
   TestFalse(TEXT("Sliding equipment remains outside the wall"),ACEVRHandCollision::Overlaps(World,Hand.Pose,Shapes,Query));
  }
 }
 Move(Seed+FVector(0,60,0),FRotator(0,90,0));
 TestFalse(TEXT("A constrained blade can turn away after sliding"),Hand.bContact);
 // Round a finite building corner while keeping pressure on it, then recover
 // tracking on the far side. Exercise both Chaos boxes and mesh triangles.
 Shapes.SetNum(1); Box->SetBoxExtent(FVector(30,30,300)); Box->SetWorldLocation(FVector(80,0,100));
 Hand.Reset(); Move(FVector(20,0,100)); Move(FVector(80,0,100));
 for(int I=1;I<=12;++I)
 {
  Move(FVector(80,I*4,100),FRotator(0,I*3,0));
  TestFalse(TEXT("Rotating palm stays outside a convex corner"),ACEVRHandCollision::Overlaps(World,Hand.Pose,Shapes,Query));
 }
 Move(FVector(80,55,100));
 TestTrue(TEXT("Palm slides around the end of a building wall"),Hand.Pose.GetLocation().Equals(FVector(80,55,100),.1));
 Box->SetBoxExtent(FVector(2,300,300)); Box->SetWorldLocation(FVector(50,0,100));
 // Reacquiring tracking on the far side cannot teleport the hand through it.
 Shapes.SetNum(1); Hand.Reset(); Move(FVector(100,0,100));
 TestTrue(TEXT("Tracking reacquisition sweeps from the player's side"),Hand.bContact && Hand.Pose.GetLocation().X<43);
 Query.AddIgnoredActor(Wall); Move(FVector(100,0,100));
 TestFalse(TEXT("Ignored owner/equipment cannot resist the hand"),Hand.bContact);
 TestTrue(TEXT("Ignored geometry permits full hand travel"),Hand.Pose.GetLocation().Equals(FVector(100,0,100),.01));
 // The locomotion blocker is at x=50; the actual visible surface is x=85.
 TArray<FACEVRContactTriangle> Surface;
 for(const auto& Points : {TArray<FVector>{FVector(85,-300,0),FVector(85,300,0),FVector(85,300,300)},
  TArray<FVector>{FVector(85,-300,0),FVector(85,300,300),FVector(85,-300,300)}})
 {
  FACEVRContactTriangle T; T.A=Points[0]; T.B=Points[1]; T.C=Points[2]; T.Bounds=FBox(Points); Surface.Add(T);
 }
 Hand.Reset();
 ACEVRHandCollision::Move(World,Hand,FTransform(FVector(65,0,100)),Seed,Shapes,Query,&Surface);
 TestTrue(TEXT("Hand enters the broad body volume until it reaches visible geometry"),Hand.Pose.GetLocation().Equals(FVector(65,0,100),.1));
 ACEVRHandCollision::Move(World,Hand,FTransform(FVector(120,0,100)),Seed,Shapes,Query,&Surface);
 TestTrue(TEXT("Hand stops at the triangle surface rather than the locomotion cylinder"),Hand.bContact && Hand.Pose.GetLocation().X>78 && Hand.Pose.GetLocation().X<79.1);
 ACEVRHandCollision::Move(World,Hand,FTransform(FVector(120,100,100)),Seed,Shapes,Query,&Surface);
 TestTrue(TEXT("Triangle contact preserves tangential sliding"),FMath::Abs(Hand.Pose.GetLocation().Y-100)<.1);
 ACEVRHandCollision::Move(World,Hand,FTransform(FVector(65,100,100)),Seed,Shapes,Query,&Surface);
 TestFalse(TEXT("Withdrawing from a visible triangle releases contact"),Hand.bContact);
 // A broad shield moving across two coplanar triangles, including their seam.
 FACEVRContactShape Shield;Shield.Local=FTransform(FVector(15,0,0));Shield.Extent=FVector(3,24,32);
 Shapes.Add(Shield);Hand.Reset();
 ACEVRHandCollision::Move(World,Hand,FTransform(FVector(45,-80,100)),Seed,Shapes,Query,&Surface);
 for(int32 I=0;I<=80;++I)
 {
  const float Y=-80+I*2.f;
  ACEVRHandCollision::Move(World,Hand,FTransform(FRotator(FMath::Sin(I*.2f),FMath::Cos(I*.1f),0),FVector(110,Y,100)),Seed,Shapes,Query,&Surface);
  TestTrue(TEXT("Shield slides across a visible mesh seam with continuous pressure"),FMath::Abs(Hand.Pose.GetLocation().Y-Y)<1.f);
  TestFalse(TEXT("Sliding shield never enters the mesh"),ACEVRHandCollision::Overlaps(World,Hand.Pose,Shapes,Query,&Surface));
 }
 Shapes.SetNum(1);
 // Two perpendicular triangle walls meet at a sharp inside corner.
 for(const auto& Points : {TArray<FVector>{FVector(-300,85,0),FVector(300,85,0),FVector(300,85,300)},
  TArray<FVector>{FVector(-300,85,0),FVector(300,85,300),FVector(-300,85,300)}})
 {
  FACEVRContactTriangle T; T.A=Points[0]; T.B=Points[1]; T.C=Points[2]; T.Bounds=FBox(Points); Surface.Add(T);
 }
 Hand.Reset();
 ACEVRHandCollision::Move(World,Hand,FTransform(FVector(120,120,100)),Seed,Shapes,Query,&Surface);
 TestTrue(TEXT("Both planes of an inside corner resist penetration"),Hand.bContact && Hand.Pose.GetLocation().X<79.1 && Hand.Pose.GetLocation().Y<79.1);
 ACEVRHandCollision::Move(World,Hand,FTransform(FRotator(0,40,0),FVector(45,120,130)),Seed,Shapes,Query,&Surface);
 TestTrue(TEXT("Withdrawing from one corner plane retains outward and vertical motion"),
  FMath::Abs(Hand.Pose.GetLocation().X-45)<.1 && FMath::Abs(Hand.Pose.GetLocation().Z-130)<.1);
 TestFalse(TEXT("Corner recovery cannot cut through either mesh plane"),ACEVRHandCollision::Overlaps(World,Hand.Pose,Shapes,Query,&Surface));
 ACEVRHandCollision::Move(World,Hand,FTransform(FVector(20,20,100)),Seed,Shapes,Query,&Surface);
 TestTrue(TEXT("Pulling out of a sharp mesh corner fully releases the hand"),!Hand.bContact && Hand.Pose.Equals(FTransform(FVector(20,20,100)),.02));
 // A held-contact gesture cannot repeatedly fire, or rearm from one quiet frame.
 Shapes.Add(Blade); Query.ClearIgnoredActors();
 Box->SetBoxExtent(FVector(30,30,300)); Box->SetWorldLocation(FVector(80,0,100));
 Hand.Reset(); Move(FVector(0,-45,100),FRotator(0,30,0));
 for(int I=0;I<12;++I) Move(FVector(0,-45,100),FRotator(0,0,0));
 Move(FVector(65,-80,100),FRotator(0,0,0));
 TestTrue(TEXT("Long equipment recovers around a clear outside corner without moving the body"),Hand.Pose.Equals(FTransform(FVector(65,-80,100)),.1));
 Shapes.SetNum(1); Hand.bValid=true;Hand.Pose=FTransform(FVector(44.5,0,100));
 Move(FVector(20,10,100));
 TestTrue(TEXT("A fractional overlap recovers locally and follows withdrawal"),!Hand.bContact && Hand.Pose.GetLocation().Equals(FVector(20,10,100),.1));
 ACEVRMath::FSwing Punch; float Speed=0; bool Ready=false;
 Punch.Sample(FVector::ZeroVector,.02f,250,true,Speed);
 for(int I=1;I<=3;++I) Ready=Punch.Sample(FVector(I*6,0,0),.02f,250,true,Speed);
 TestTrue(TEXT("An eighteen-centimeter deliberate punch is recognized"),Ready);
 Punch.Commit();
 for(int I=0;I<6;++I) TestFalse(TEXT("Maintaining contact cannot repeat damage"),Punch.Sample(FVector(18,0,0),.02f,250,true,Speed));
 TestFalse(TEXT("Brief pause does not rearm punch"),Punch.bArmed);
 for(int I=0;I<4;++I) Punch.Sample(FVector(18,0,0),.02f,250,true,Speed);
 TestTrue(TEXT("A deliberate recovery permits the next punch"),Punch.bArmed);
 Punch.Reset();Punch.Sample(FVector::ZeroVector,.02f,250,true,Speed);
 for(int I=1;I<=4;++I)Ready=Punch.Sample(FVector(I*6,0,0),.02f,250,true,Speed);
 Punch.Commit();
 for(int I=5;I<=8;++I)TestFalse(TEXT("Follow-through cannot repeatedly hit"),Punch.Sample(FVector(I*6,0,0),.02f,250,true,Speed));
 int Backhands=0;
 for(int I=7;I>=0;--I) if(Punch.Sample(FVector(I*6,0,0),.02f,250,true,Speed)) { ++Backhands;Punch.Commit(); }
 TestEqual(TEXT("Deliberate reversal permits one backhand without stopping"),Backhands,1);
 Punch.Reset(); TestFalse(TEXT("Tracking reset never itself attacks"),Punch.Sample(FVector(80,0,0),.02f,250,true,Speed));
 World->DestroyWorld(false); return true;
}
#endif
