#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../ACEBodySweep.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEHoverTooltipWidget.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRHandCollision.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"
#include "ACEEnvCellActor.h"
#include "ACETypes.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRWallContactTest,"ACE.VR.WallContact",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEVRWallContactTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(201*192,140*192,0),100);
 TestNotNull(TEXT("Reported room exists in retail data"),Dat->GetOrBuildEnvCellMesh(0xC98C0129,100));
 auto* Room=World->SpawnActor<AACEEnvCellActor>();TestTrue(TEXT("Reported room collision loads"),Room->LoadEnvCell(0xC98C0129,Origin,100));
 Room->SetEnvCellCollisionActive(true);
 // Captured from the Quest while wall contact held the player still at 16 FPS.
 const FVector Contact(-3872582.239993,2706250.000408,2290.75);
 // Hands and held equipment use the same retail room, at its actual large
 // world coordinates. Origin-centered boxes do not exercise Chaos precision.
 {
  FCollisionQueryParams HandQuery(SCENE_QUERY_STAT(VRRoomEquipmentSlide),false);
  HandQuery.bFindInitialOverlaps=true;
  FACEVRContactShape Palm;Palm.Extent=FVector(6);
  FACEVRContactShape Weapon;Weapon.Local=FTransform(FVector(0,-30,0));Weapon.Extent=FVector(3,30,3);
  TArray<FACEVRContactShape> Parts{Palm,Weapon};
  const FVector Seed=Contact+FVector(0,70,30);
  for(float Step:{.2f,1.5f})
  {
   FACEVRContactState Hand;
   ACEVRHandCollision::Move(World,Hand,FTransform(Seed),Seed,Parts,HandQuery);
   for(int32 I=0;I<40;++I)
   {
    const FVector Desired=Contact+FVector(I*Step,-70,30);
    ACEVRHandCollision::Move(World,Hand,FTransform(FRotator(0,FMath::Sin(I*.13f),0),Desired),Seed,Parts,HandQuery);
    TestTrue(TEXT("Held equipment slides continuously on actual interior wall geometry"),FMath::Abs(Hand.Pose.GetLocation().X-Desired.X)<1.f);
    TestFalse(TEXT("Equipment remains outside retail room walls"),ACEVRHandCollision::Overlaps(World,Hand.Pose,Parts,HandQuery));
   }
  }
 }
 const auto Shape=FCollisionShape::MakeCapsule(48,90.75);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(VRWallContactRegression),true);
 // Two initially overlapping walls must be recovered together. Exercise the
 // large coordinates used in the world and verify the opposite wall still blocks.
 {
  const FVector Base=Contact+FVector(5000,5000,1000);
  TArray<AActor*> Walls;
  auto Box=[&](FVector P,FVector E) {
   auto* A=World->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);
   A->SetRootComponent(B);B->SetBoxExtent(E);B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
   B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();A->SetActorLocation(Base+P);Walls.Add(A);
  };
  Box(FVector(-10,250,0),FVector(10,350,200));Box(FVector(250,-10,0),FVector(350,10,200));
  for(float Depth:{.2f,2.f,12.f})
  {
   const FVector Start=Base+FVector(48-Depth,48-Depth,0);FHitResult Hit;
   TestTrue(TEXT("Corner starts overlapping"),ACEBodySweep::Sweep(*World,Hit,Start,Start+FVector(5,5,0),Shape,Query));
   const FVector Out=ACEBodySweep::Recover(*World,Start,Hit.Normal*(Hit.PenetrationDepth+2),Hit,Shape,Query);
   TestTrue(TEXT("Inside corner escapes both overlapping walls"),Out.X>=Base.X+48 && Out.Y>=Base.Y+48);
   TestFalse(TEXT("Recovered corner permits retreat"),ACEBodySweep::Sweep(*World,Hit,Out,Out+FVector(30,30,0),Shape,Query));
  }
  Box(FVector(105,250,0),FVector(10,350,200));
  const FVector Trapped=Base+FVector(40,40,0);FHitResult Hit;
  ACEBodySweep::Sweep(*World,Hit,Trapped,Trapped+FVector(5,5,0),Shape,Query);
  const FVector Bounded=ACEBodySweep::Recover(*World,Trapped,Hit.Normal*(Hit.PenetrationDepth+2),Hit,Shape,Query);
  TestTrue(TEXT("Corner recovery cannot cross a different opposing wall"),Bounded.X<Base.X+95);
  for(auto* A:Walls) A->Destroy();
 }
 // Exercise both contact orders and sub-millimeter numerical overlap. Keep
 // physical blocking for movement into the wall and for a deeper overlap.
 for(float Offset:{-.03f,0.f,.03f})
 {
  const FVector P=Contact+FVector(0,Offset,0);
  for(const FVector Delta:{FVector(0,25,0),FVector(25,0,0),FVector(-25,0,0)})
  {
   FHitResult Hit;
   TestFalse(TEXT("Wall-floor contact permits retreat and sliding"),ACEBodySweep::Sweep(*World,Hit,P,P+Delta,Shape,Query));
  }
  FHitResult Hit;
  TestTrue(TEXT("Moving into the same wall still blocks"),ACEBodySweep::Sweep(*World,Hit,P,P-FVector(0,25,0),Shape,Query));
  float Support=0;
  TestTrue(TEXT("A touching wall preserves floor support"),ACEBodySweep::FindFootSupport(*World,P-FVector(0,0,90.75),48,3,Query,Support,1));
 }
 FHitResult Deep;
 const FVector Overlap=Contact-FVector(0,2,0);
 TestTrue(TEXT("Deep penetration is not hidden by numerical clearance"),ACEBodySweep::Sweep(*World,Deep,Overlap,Overlap+FVector(0,25,0),Shape,Query));
 const FVector Recovered=ACEBodySweep::Recover(*World,Overlap,Deep.Normal*(Deep.PenetrationDepth+2),Deep,Shape,Query);
 TestTrue(TEXT("Existing overlap can recover toward the room"),Recovered.Y>Contact.Y+1);
 FHitResult Later;
 TestTrue(TEXT("Retreat still sweeps the next wall in the same mesh"),ACEBodySweep::Sweep(*World,Later,Contact,Contact+FVector(0,2000,0),Shape,Query));

 // Entering a wall from a shallow existing contact must retain this frame's
 // sideways travel, rather than spending the whole frame on depenetration.
 for(float Depth:{.03f,.2f,2.f})
 {
  const FVector Start=Contact-FVector(0,Depth,0);
  FHitResult Hit;
  TestTrue(TEXT("Fixture begins with an inward wall hit"),ACEBodySweep::Sweep(*World,Hit,Start,Start+FVector(12,-8,0),Shape,Query));
  const FVector End=ACEBodySweep::SlideFromPenetration(*World,Start,Start+FVector(12,-8,0),Hit,Shape,Query);
  TestTrue(TEXT("Recovery preserves this frame's tangential movement"),End.X>=Start.X+11.5);
  TestTrue(TEXT("Recovery remains on the room side of the wall"),End.Y>=Contact.Y);
  TestTrue(TEXT("Recovery preserves floor height"),FMath::Abs(End.Z-Start.Z)<.1);
 }

 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,90.75);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
 PC->bRetailCursorInstalled=true;PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->bRun=true;
 // This fixture measures collision, without the later-added forward-stick assist
 // deliberately reducing the input's sideways component.
 VR->Settings->ForwardAssistDegrees=0;
 Client->SetRunSkill(600);PC->InputComponent->AxisBindings.Reset();
 const double Begin=FPlatformTime::Seconds();int32 Ticks=0;
 for(float Dt:{1.f/90,1.f/15})
 {
  FACEPosition Pose;Pose.CellId=0xC98C0129;Pose.SetLocationFromUnreal(Contact-FVector(0,0,90.75),100);
  Pose.SetAceFacingFromUnrealDir2D(FVector(0,-1,0));Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;
  Pawn->SetActorLocationAndRotation(Contact,Pose.ToUnrealQuat());
  auto Tick=[&](FVector2D Stick)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-90.75),FRotator(0,-90,0));
   VR->MoveStick=Stick;PC->PlayerTick(Dt);++Ticks;
  };
  for(int32 I=0;I<FMath::CeilToInt(.3f/Dt);++I)Tick(FVector2D(0,1));
  const FVector Pressed=Pawn->GetActorLocation();
  TestTrue(TEXT("Tracked movement cannot push through the captured wall"),Pressed.Y>=Contact.Y-.1);
  for(int32 I=0;I<FMath::CeilToInt(.2f/Dt);++I)Tick(FVector2D(1,0));
  const FVector Slid=Pawn->GetActorLocation();
  TestTrue(TEXT("Tracked strafe slides along the captured wall"),FMath::Abs(Slid.X-Pressed.X)>10);
  for(int32 I=0;I<FMath::CeilToInt(.2f/Dt);++I)Tick(FVector2D(0,-1));
  const FVector Retreated=Pawn->GetActorLocation();
  TestTrue(TEXT("Tracked retreat releases from the captured wall"),Retreated.Y>Slid.Y+10);
  TestTrue(TEXT("Wall contact does not change the player's floor"),FMath::Abs(Retreated.Z-Contact.Z)<2);
  AddInfo(FString::Printf(TEXT("dt=%.4f pressed=%s slid=%s retreated=%s"),Dt,*(Pressed-Contact).ToString(),*(Slid-Contact).ToString(),*(Retreated-Contact).ToString()));
 }
 // Continuous diagonal contact must retain the tangential component every
 // frame, not merely make some progress after backing away.
 for(float Dt:{1.f/90,1.f/15})for(const FVector2D Stick:{FVector2D(.35f,.937f),FVector2D(.707f,.707f),FVector2D(.937f,.35f)})
 {
  FACEPosition Pose;Pose.CellId=0xC98C0129;Pose.SetLocationFromUnreal(Contact-FVector(0,0,90.75),100);
  Pose.SetAceFacingFromUnrealDir2D(FVector(0,-1,0));Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;
  Pawn->SetActorLocationAndRotation(Contact,Pose.ToUnrealQuat());
  double MinSlide=1.e9,MaxSlide=0,MaxWobble=0;
  const double StartTime=FPlatformTime::Seconds();
  for(int32 I=0;I<FMath::CeilToInt(.3f/Dt);++I)
  {
   const FVector Before=Pawn->GetActorLocation();
   VR->Head->SetWorldLocationAndRotation(Before+FVector(0,0,175-90.75),FRotator(0,-90,0));
   VR->MoveStick=Stick;PC->PlayerTick(Dt);
   const FVector After=Pawn->GetActorLocation();
   if(I>0){MinSlide=FMath::Min(MinSlide,After.X-Before.X);MaxSlide=FMath::Max(MaxSlide,After.X-Before.X);MaxWobble=FMath::Max(MaxWobble,FMath::Abs(After.Y-Before.Y));}
   TestTrue(TEXT("Diagonal contact preserves floor height"),FMath::Abs(After.Z-Contact.Z)<2);
  }
  AddInfo(FString::Printf(TEXT("diagonal dt=%.4f stick=%s min=%.3f max=%.3f wobble=%.3f cpu=%.3fms"),Dt,*Stick.ToString(),MinSlide,MaxSlide,MaxWobble,(FPlatformTime::Seconds()-StartTime)*1000));
  TestTrue(TEXT("Diagonal wall sliding has no stopped frames"),MinSlide>Client->GetSidestepSpeed(true)*100*Dt*Stick.X*.7);
  TestTrue(TEXT("Contact does not repeatedly push the camera away from the wall"),MaxWobble<.5);
 }
 AddInfo(FString::Printf(TEXT("Wall integration: %d ticks in %.3f ms"),Ticks,(FPlatformTime::Seconds()-Begin)*1000));
 // Falling beside a wall must make downward progress even when a moving
 // creature has pushed the capsule slightly into that wall.
 for(float Depth:{.1f,.3f,1.f})
 {
  FVector P=Contact+FVector(0,-Depth,20);bool Landed=false;
  for(int I=0;I<90 && !Landed;++I)
  {
   const auto Move=ACEBodySweep::MoveAirborne(*World,P,P+FVector(0,0,-2),Shape,Query,true,.6641741f);
   P=Move.Position;Landed=Move.bLanded;
  }
  TestTrue(*FString::Printf(TEXT("Wall-side fall lands after overlap %.2f (z %.2f)"),Depth,P.Z-Contact.Z),Landed);
 }
 {
  TArray<AActor*> Creatures;
  for(float X:{-72.f,72.f})
  {
   auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UCapsuleComponent>(A);A->SetRootComponent(C);
   C->InitCapsuleSize(40,90);C->ComponentTags.Add(TEXT("ACECreatureBody"));C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
   C->SetCollisionResponseToAllChannels(ECR_Ignore);C->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
   C->RegisterComponent();A->SetActorLocation(Contact+FVector(X,1,0));Creatures.Add(A);
  }
  float Support=0;
  TestTrue(TEXT("Crowded bodies cannot hide the real floor from the support sphere"),
   ACEBodySweep::FindFootSupport(*World,Contact-FVector(0,0,90.75),48,30,Query,Support));
  FVector P=Contact+FVector(0,0,25);bool Landed=false;
  for(int I=0;I<90 && !Landed;++I)
  {
   const auto Move=ACEBodySweep::MoveAirborne(*World,P,P+FVector(3,-1,-2),Shape,Query,true,.6641741f);
   P=Move.Position;Landed=Move.bLanded;
  }
  TestTrue(TEXT("Crowded wall-side fall reaches actual ground instead of holding jump"),Landed && FMath::Abs(P.Z-Contact.Z)<1);
  TestTrue(TEXT("Crowded fall cannot bypass the static wall"),P.Y>=Contact.Y-.5);
  TestTrue(TEXT("Crowded fall does not travel horizontally through monsters"),FMath::Abs(P.X-Contact.X)<48);
  for(auto* A:Creatures)A->Destroy();
 }
 // A stationary tracked player must settle once on a tilted support rather
 // than oscillating between the center ray and lower-sphere height.
 {
  auto* A=World->SpawnActor<AActor>();auto* Ramp=NewObject<UBoxComponent>(A);A->SetRootComponent(Ramp);
  Ramp->SetBoxExtent(FVector(500,500,10));Ramp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Ramp->SetCollisionResponseToAllChannels(ECR_Ignore);Ramp->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
  Ramp->RegisterComponent();A->SetActorLocationAndRotation(Contact+FVector(2000,0,400),FRotator(25,0,0));
  FHitResult Ground;const FVector At=A->GetActorLocation();
  World->LineTraceSingleByChannel(Ground,At+FVector(0,0,500),At-FVector(0,0,500),ECC_Pawn,Query);
  FACEPosition Pose;Pose.CellId=0xC98C0129;Pose.SetLocationFromUnreal(Ground.ImpactPoint+FVector(0,0,10),100);
  Session->SetLocalPosition(Pose);PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
  Pawn->SetActorLocation(Pose.ToUnrealLocation(100)+FVector(0,0,90.75));VR->MoveStick=FVector2D::ZeroVector;
  double Low=1.e20,High=-1.e20;
  for(int I=0;I<180;++I)
  {
   VR->Head->SetWorldLocation(Pawn->GetActorLocation()+FVector(0,0,175-90.75));
   PC->PlayerTick(1.f/90);
   if(I>30){Low=FMath::Min(Low,Pawn->GetActorLocation().Z);High=FMath::Max(High,Pawn->GetActorLocation().Z);}
  }
  TestFalse(TEXT("Stationary slope does not latch jumping"),PC->bJumpAirborne);
  TestTrue(*FString::Printf(TEXT("Stationary slope stays stable (%.4f cm)"),High-Low),High-Low<.1);
  TestTrue(TEXT("Stationary support remains on the ramp"),FMath::Abs(Pawn->GetActorLocation().Z-90.75-Ground.ImpactPoint.Z)<15);
  A->Destroy();
 }
 Session->State=EACESessionState::Disconnected;Session->PlayerGuid=0;
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
