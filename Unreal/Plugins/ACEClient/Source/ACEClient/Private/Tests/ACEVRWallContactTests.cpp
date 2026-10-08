#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../ACEBodySweep.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEWorldEntityActor.h"
#include "ACEOrbitCameraBoom.h"
#include "ACESession.h"
#include "ACEHoverTooltipWidget.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRHandCollision.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/InputComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"
#include "ACEEnvCellActor.h"
#include "ACETypes.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACECellTransit.h"
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
 // A ledge intersecting only the upper sphere is an obstruction even when
 // its separating normal points upward. It must never receive foot clearance.
 {
  const FVector Base=Contact+FVector(-5000,5000,1000);
  auto* A=World->SpawnActor<AActor>();auto* Ledge=NewObject<UBoxComponent>(A);A->SetRootComponent(Ledge);
  Ledge->SetBoxExtent(FVector(200,200,2));Ledge->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Ledge->SetCollisionResponseToAllChannels(ECR_Block);Ledge->RegisterComponent();A->SetActorLocation(Base+FVector(0,0,150));
  FHitResult Upper;const FVector Center=Base+FVector(0,0,130);
  TestTrue(TEXT("Upper-sphere ledge survives floor-clearance retries"),ACEBodySweep::Sweep(*World,Upper,Center,Center+FVector(0,20,0),Shape,Query));
  TestTrue(TEXT("Upward upper contact is distinguishable from foot support"),ACEBodySweep::IsUpperBodyContact(Upper) && Upper.Normal.Z>.66);
  A->Destroy();
 }
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
 // A buried room's camera sphere must ignore terrain only while wholly
 // indoors; room walls still obstruct, and outdoor terrain still blocks.
 {
  FACEPosition Pose;Pose.CellId=0xC98C0129;Pose.SetLocationFromUnreal(Contact+FVector(0,300,-90.75),100);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;Session->SetLocalPosition(Pose);
  Pawn->SetActorLocation(Contact+FVector(0,300,0));
  auto* Boom=NewObject<UACEOrbitCameraBoom>(Pawn);Pawn->AddInstanceComponent(Boom);
  Boom->SetupAttachment(Capsule);Boom->RegisterComponent();Boom->ProbeSize=5;
  const FVector Start=Boom->GetComponentLocation(),End=Start+FVector(0,100,0);
  auto* A=World->SpawnActor<AActor>();auto* Ground=NewObject<UBoxComponent>(A);A->SetRootComponent(Ground);
  Ground->SetBoxExtent(FVector(500,500,20));Ground->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Ground->SetCollisionResponseToAllChannels(ECR_Block);Ground->ComponentTags.Add(TEXT("ACEOutdoorTerrain"));
  Ground->RegisterComponent();A->SetActorLocation(Start);
  TestTrue(TEXT("Indoor camera is not collapsed by a crossing outdoor heightfield"),Boom->BlendLocations(End,Start,true,0).Equals(End,.1));
  Ground->ComponentTags.Reset();
  TestTrue(TEXT("Same camera continues to collide with room architecture"),!Boom->BlendLocations(End,Start,true,0).Equals(End,.1));
  Ground->ComponentTags.Add(TEXT("ACEOutdoorTerrain"));PC->PredictedPose.CellId=0xC98C0020;
  TestTrue(TEXT("Outdoor camera retains terrain blocking"),Boom->BlendLocations(End,Start,true,0).Equals(Start,.1));
  A->Destroy();Boom->DestroyComponent();
 }
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 // Actual Shoushi building cells at 33.4S,72.5E. Unlike the enclosed room
 // above, these add outdoor cells through their doorway/SeenOutside portals.
 {
  const uint32 LB=0xDA550000;
  const auto Info=Dat->GetLandblockInfo(LB);
  Dat->GetOrBuildLandblockMesh(LB,100);
  int32 Checked=0;
  if(Info) for(uint32 I=0;I<Info->NumCells;++I)Dat->GetOrBuildEnvCellMesh(LB|uint32(0x100+I),100);
  if(Info) for(uint32 I=0;I<Info->NumCells;++I)
  {
   const uint32 Cell=LB|uint32(0x100+I);const auto* Mesh=Dat->FindEnvCellMesh(Cell,100);
   if(!Mesh || !Mesh->bHasLocalBounds)continue;
   const FTransform Frame=Mesh->GetCellLocalToLandblock(100)*FTransform(FVector(-218*19200,85*19200,0));
   const FVector P=Frame.TransformPosition(FVector((Mesh->LocalBoundsMin.X+Mesh->LocalBoundsMax.X)*.5,
    (Mesh->LocalBoundsMin.Y+Mesh->LocalBoundsMax.Y)*.5,Mesh->LocalBoundsMin.Z+90));
   const FVector End=P+FVector(0,30,0);
   if(FVector::Dist2D(P,FVector(-4186800,1645200,0))>4000 || !Dat->IsPointInsideEnvCell(Cell,P,100,0)
     || !Dat->IsPointInsideEnvCell(Cell,End,100,0))continue;
   TArray<uint32> Cells;ACECellTransit::FindCellList(*Dat,Cell,P,40,100,Cells);
   if(!Cells.ContainsByPredicate([](uint32 C){return !ACECellTransit::IsIndoorCell(C);}))continue;
   FACEPosition Pose;Pose.CellId=Cell;Pose.SetLocationFromUnreal(P-FVector(0,0,90.75),100);
   PC->PredictedPose=Pose;PC->bHavePredictedPose=true;Session->SetLocalPosition(Pose);Pawn->SetActorLocation(P);
   auto* Boom=NewObject<UACEOrbitCameraBoom>(Pawn);Pawn->AddInstanceComponent(Boom);
   Boom->SetupAttachment(Capsule);Boom->RegisterComponent();Boom->ProbeSize=5;
   auto* A=World->SpawnActor<AActor>();auto* Ground=NewObject<UBoxComponent>(A);A->SetRootComponent(Ground);
   Ground->SetBoxExtent(FVector(10,10,10));Ground->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
   Ground->SetCollisionResponseToAllChannels(ECR_Block);Ground->ComponentTags.Add(TEXT("ACEOutdoorTerrain"));
   Ground->RegisterComponent();A->SetActorLocation(P);
   TestTrue(TEXT("Open Shoushi room ignores overlapping outdoor terrain at an indoor camera contact"),Boom->BlendLocations(End,P,true,0).Equals(End,.1));
   Ground->ComponentTags.Reset();
   TestFalse(TEXT("Open Shoushi room still blocks the camera on architecture"),Boom->BlendLocations(End,P,true,0).Equals(End,.1));
   AddInfo(FString::Printf(TEXT("Shoushi camera cell=%08X point=%s"),Cell,*P.ToString()));
   ++Checked;A->Destroy();Boom->DestroyComponent();
  }
  TestTrue(TEXT("Camera regression exercises actual Shoushi rooms with outdoor transit candidates"),Checked>0);
 }
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->bRun=true;VR->Settings->MovementDirection=0;
 // This fixture measures collision, without the later-added forward-stick assist
 // deliberately reducing the input's sideways component.
 VR->Settings->ForwardAssistDegrees=0;
 Client->SetRunSkill(600);PC->InputComponent->AxisBindings.Reset();
 // Real authored bodies, including low, waist-height and flying creatures.
 // Press into each body first, then strafe without releasing forward.
 for(uint32 Setup:{0x02000A95u,0x02000037u,0x02001121u,0x02000964u,0x02000041u})
 {
  FACEWorldObject Mob;Mob.Guid=45678;Mob.SetupId=Setup;Mob.ItemType=ACEItemType::Creature;
  Mob.PhysicsState=ACEPhysicsState::Gravity;Mob.bHasPosition=true;Mob.Position.CellId=0xC98C0129;
  Mob.Position.SetLocationFromUnreal(Contact+FVector(0,350,-90.75),100);
  auto* Monster=World->SpawnActor<AACEWorldEntityActor>();Monster->InitializeFromObject(Mob,100,true);
  FACEPosition Pose=Mob.Position;Pose.SetLocationFromUnreal(Contact+FVector(0,650,-90.75),100);
  Pose.SetAceFacingFromUnrealDir2D(FVector(0,-1,0));Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
  Pawn->SetActorLocationAndRotation(Pose.ToUnrealLocation(100)+FVector(0,0,90.75),Pose.ToUnrealQuat());
  auto Move=[&](FVector2D Stick){VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,84.25),FRotator(0,-90,0));VR->MoveStick=Stick;PC->PlayerTick(1.f/90);};
  for(int I=0;I<45;++I)Move(FVector2D(0,1));
  const FVector Pressed=Pawn->GetActorLocation();
  for(int I=0;I<30;++I)Move(FVector2D(1,1));
  const FVector Slid=Pawn->GetActorLocation();
  TestTrue(FString::Printf(TEXT("Actual creature %08X permits diagonal strafe after forward contact: %s"),Setup,*(Slid-Pressed).ToString()),FMath::Abs(Slid.X-Pressed.X)>15);
  TestFalse(TEXT("Sliding around actual creature stays grounded"),PC->bJumpAirborne);
  Monster->Destroy();
 }
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
 // Normal locomotion into a crowd must never become a jump. Moving remote
 // bodies can overlap the local body between network updates, even at rest.
 {
  TArray<AActor*> Crowd;
  for (int I=0; I<20; ++I)
  {
   auto* A=World->SpawnActor<AActor>(); auto* Body=NewObject<UCapsuleComponent>(A);
   A->SetRootComponent(Body);Body->InitCapsuleSize(40,90);
   Body->ComponentTags.Add(TEXT("ACECreatureBody"));Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
   Body->SetCollisionResponseToAllChannels(ECR_Ignore);Body->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
   Body->RegisterComponent(); Crowd.Add(A);
  }
  for (float Dt : {1.f/90,1.f/15}) for (bool Run : {false,true})
  {
   FACEPosition Pose;Pose.CellId=0xC98C0129;Pose.SetLocationFromUnreal(Contact-FVector(0,0,90.75),100);
   Pose.SetAceFacingFromUnrealDir2D(FVector(0,-1,0));Session->SetLocalPosition(Pose);
   PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
   PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
   Pawn->SetActorLocationAndRotation(Contact,Pose.ToUnrealQuat());VR->Settings->bRun=Run;
   int32 AirborneFrames=0; double MaxHeight=0;
   for (int Frame=0;Frame<60;++Frame)
   {
    for (int I=0;I<Crowd.Num();++I)
    {
     const float Angle=I*PI*2/Crowd.Num();
     Crowd[I]->SetActorLocation(Contact+FVector(65*FMath::Cos(Angle),55+25*FMath::Sin(Angle),-15+I));
    }
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-90.75),FRotator(0,-90,0));
    VR->MoveStick=FVector2D(Frame<30?.7f:-.7f,.7f);PC->PlayerTick(Dt);
    AirborneFrames+=PC->bJumpAirborne?1:0;
    MaxHeight=FMath::Max(MaxHeight,FMath::Abs(Pawn->GetActorLocation().Z-Contact.Z));
   }
   TestEqual(TEXT("Walking/running beside a swarming wall does not synthesize jumping"),AirborneFrames,0);
   TestTrue(TEXT("Crowd separation does not lift the player's feet"),MaxHeight<2.);
   AddInfo(FString::Printf(TEXT("Crowd dt=%.4f run=%d airborne=%d height=%.3f"),Dt,Run,AirborneFrames,MaxHeight));
  }
  VR->Settings->bRun=true;
  for(auto* A:Crowd) A->Destroy();
 }
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
 // More than sixteen overlapping remote bodies still cannot occlude terrain
 // queries. This catches both the old bounded sphere retry and post-filtered
 // line trace, without requiring a hardware headset or network timing.
 {
  TArray<AActor*> Bodies;
  const FVector Feet=Contact+FVector(0,120,-90.75);
  for (int32 I=0;I<24;++I)
  {
   auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UCapsuleComponent>(A);A->SetRootComponent(C);
   C->InitCapsuleSize(40,90);C->ComponentTags.Add(TEXT("ACECreatureBody"));C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
   C->SetCollisionResponseToAllChannels(ECR_Ignore);C->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
   C->RegisterComponent();A->SetActorLocation(Feet+FVector(0,0,-45+I*.25));Bodies.Add(A);
  }
  float Floor=0;
  TestTrue(TEXT("Floor support survives more than sixteen overlapping creatures"),ACEBodySweep::FindFootSupport(*World,Feet,48,30,Query,Floor));
  TArray<FHitResult> Ground;
  TestTrue(TEXT("Ground ray sees the floor behind blocking creatures"),ACEBodySweep::TraceGround(*World,Ground,Feet+FVector(0,0,100),Feet-FVector(0,0,40),Query));
  TestTrue(TEXT("Ground results contain no creature floor"),Ground.ContainsByPredicate([](const FHitResult& H){return H.bBlockingHit && !ACEBodySweep::IsCreatureBody(H) && H.ImpactNormal.Z>.66;}));
  const auto Fall=ACEBodySweep::MoveAirborne(*World,Feet+FVector(0,0,160),Feet+FVector(0,0,50),FCollisionShape::MakeCapsule(30,90),Query,true);
  TestTrue(TEXT("Falling through a dense overlapping crowd still reaches architecture"),Fall.bLanded && Fall.Position.Z<Feet.Z+100);
  for(auto* A:Bodies)A->Destroy();
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
 // Independent room faces meeting at ramp/flat seams, at large world coordinates.
 // Include tiny authored cracks/overlaps and both input paths/directions.
 for(float Rise:{300.f,600.f})for(float Gap:{0.f,.2f,-.2f})for(float Lip:{0.f,2.f,-2.f})
 {
  const FVector Base=Contact+FVector(8000,8000,2000);
  TArray<AActor*> Surfaces;
  auto Face=[&](double X0,double Z0,double X1,double Z1)
  {
   auto* A=World->SpawnActor<AActor>();auto* Mesh=NewObject<UProceduralMeshComponent>(A);A->SetRootComponent(Mesh);
   Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Mesh->SetCollisionResponseToAllChannels(ECR_Block);
   Mesh->RegisterComponent();A->SetActorLocation(Base);
   Mesh->CreateMeshSection(0,{FVector(X0,-300,Z0),FVector(X1,-300,Z1),FVector(X1,300,Z1),FVector(X0,300,Z0)},
    {0,1,2,0,2,3},{},{},{},{},true);Surfaces.Add(A);
  };
  Face(-600,0,0,0);Face(Gap,Lip,600,Rise+Lip);Face(600+Gap,Rise,1200,Rise);
  for(bool Tracked:{false,true})for(bool Reverse:{false,true})for(float Rate:{30.f,144.f})
  {
   const FVector Direction(Reverse?-1:1,0,0);
   FACEPosition Seed;Seed.CellId=0xC98C0129;
   Seed.SetLocationFromUnreal(Base+FVector(Reverse?850:-250,0,Reverse?Rise:0),100);
   Seed.SetAceFacingFromUnrealDir2D(Direction);
   VR->bActive=Tracked;VR->Settings->bRun=true;Session->SetLocalPosition(Seed);PC->PredictedPose=Seed;
   PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
   Pawn->SetActorLocationAndRotation(Seed.ToUnrealLocation()+FVector(0,0,90.75),Seed.ToUnrealQuat());
   if(!Tracked)PC->PlayerInput->InputKey(FInputKeyParams(EKeys::W,IE_Pressed,1.,false));
   bool Reached=false;int32 Stalls=0,MaxStalls=0;
   for(int32 Frame=0;Frame<int32(Rate*8);++Frame)
   {
    const FVector Before=Pawn->GetActorLocation();
    VR->Head->SetWorldLocationAndRotation(Before+FVector(0,0,175-90.75),Direction.Rotation());
    VR->MoveStick=FVector2D(0,1);++GFrameCounter;PC->PlayerTick(1.f/Rate);
    const FVector After=Pawn->GetActorLocation();
    Stalls=FVector::Dist2D(Before,After)<.01?Stalls+1:0;MaxStalls=FMath::Max(MaxStalls,Stalls);
    if(Reverse?After.X<Base.X-150:After.X>Base.X+750){Reached=true;break;}
   }
   if(!Tracked)PC->PlayerInput->InputKey(FInputKeyParams(EKeys::W,IE_Released,0.,false));
   AddInfo(FString::Printf(TEXT("Ramp seam rise=%.0f gap=%.2f lip=%.2f vr=%d reverse=%d fps=%.0f end=%s stalls=%d"),Rise,Gap,Lip,Tracked,Reverse,Rate,*(Pawn->GetActorLocation()-Base).ToString(),MaxStalls));
   TestTrue(TEXT("Ramp and flat seams can be crossed without jumping"),Reached);
   TestTrue(TEXT("Seam does not hold moving player in place"),MaxStalls<Rate*.25);
  }
  for(auto* A:Surfaces)A->Destroy();
 }
 // The reported crowded dungeon floor, using the retail cell at its actual
 // coordinates. Exercise normal movement and a real charged jump while crowded.
 for(int32 SwarmCase=0;SwarmCase<4;++SwarmCase)
 {
  const uint32 ReportedCell=SwarmCase==0?0x0143015Fu:SwarmCase==1?0x0143014Fu:0x01430171u;
  const bool ActualWasps=SwarmCase==3;
  FACEPosition Seed;Seed.CellId=ReportedCell;Seed.Location=ReportedCell==0x0143015F
   ? FVector(43.304642,-68.413086,-.002981) : ReportedCell==0x0143014F
   ? FVector(36.702820,-30.505859,.031020) : FVector(49.011993,-74.999023,0);
  Seed.RotationW=.898748f;Seed.RotationXYZ=FVector(0,0,-.438466);
  if(ReportedCell==0x01430171){Seed.RotationW=.932723f;Seed.RotationXYZ.Z=-.360594f;}
  auto* ReportedRoom=World->SpawnActor<AACEEnvCellActor>();
  const FVector RoomOrigin=FACEPosition::AceVectorToUnreal(FVector(192,67*192,0),100);
  TestNotNull(TEXT("Reported swarm dungeon geometry exists in retail DAT"),Dat->GetOrBuildEnvCellMesh(ReportedCell,100));
  TestTrue(TEXT("Reported swarm dungeon cell loads from retail DAT"),ReportedRoom->LoadEnvCell(ReportedCell,RoomOrigin,100));
  ReportedRoom->SetEnvCellCollisionActive(true);
  const FVector Center=Seed.ToUnrealLocation(100)+FVector(0,0,90.75);
  TArray<AActor*> Bodies;
  FCollisionQueryParams Environment=Query;
  for(int I=0;I<(ActualWasps?32:8);++I)
  {
   if(ActualWasps)
   {
    FACEWorldObject Wasp;Wasp.Guid=0x78001000+I;Wasp.SetupId=0x02001121;
    Wasp.MotionTableId=0x09000167;Wasp.Scale=1.2f;Wasp.ItemType=ACEItemType::Creature;
    Wasp.PhysicsState=ACEPhysicsState::Gravity;Wasp.bHasPosition=true;Wasp.Position=Seed;
    const float Angle=I*PI/16;
    Wasp.Position.SetLocationFromUnreal(Center+FVector(55*FMath::Cos(Angle),55*FMath::Sin(Angle),-90.75),100);
    auto* Monster=World->SpawnActor<AACEWorldEntityActor>();Monster->InitializeFromObject(Wasp,100,true);
    Bodies.Add(Monster);Environment.AddIgnoredActor(Monster);continue;
   }
   auto* A=World->SpawnActor<AActor>();UPrimitiveComponent* Body;
   if(ReportedCell==0x01430171)
   {
    auto* Sphere=NewObject<USphereComponent>(A);Sphere->InitSphereRadius(40);Body=Sphere;
   }
   else
   {
    auto* CapsuleBody=NewObject<UCapsuleComponent>(A);CapsuleBody->InitCapsuleSize(35,85);Body=CapsuleBody;
   }
   A->SetRootComponent(Body);
   Body->ComponentTags.Add(TEXT("ACECreatureBody"));Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
   Body->SetCollisionResponseToAllChannels(ECR_Ignore);Body->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);Body->RegisterComponent();
   const float Angle=I*PI/4;A->SetActorLocation(Center+FVector(55*FMath::Cos(Angle),55*FMath::Sin(Angle),-10+I));Bodies.Add(A);
   Environment.AddIgnoredActor(A);
  }
  for(bool Tracked:{false,true})for(bool Run:{false,true})for(bool Jump:{false,true})
  {
   VR->bActive=Tracked;VR->Settings->bRun=Run;Session->SetLocalPosition(Seed);PC->PredictedPose=Seed;
   PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
   Pawn->SetActorLocationAndRotation(Center,Seed.ToUnrealQuat());int32 Airborne=0,WallOverlaps=0;
   if(Jump)
   {
    PC->bRunning=Run;PC->bJumpCharging=true;PC->JumpChargeExtent=1;
    PC->ReleaseJump(1,.4f);
    TestTrue(TEXT("Crowd test submits a real charged jump"),PC->bJumpAirborne);
   }
   if(!Tracked)
   {
    PC->PlayerInput->InputKey(FInputKeyParams(EKeys::W,IE_Pressed,1.,false));
    if(!Run)PC->PlayerInput->InputKey(FInputKeyParams(EKeys::LeftShift,IE_Pressed,1.,false));
   }
   for(int Frame=0;Frame<(Jump?150:45);++Frame)
   {
    if(ActualWasps)for(int32 I=0;I<Bodies.Num();++I)
    {
     // Networked creatures may move into an airborne player between sweeps.
     // Include changing altitude instead of testing only stationary proxies.
     const float Angle=I*PI/16+Frame*.015f,Radius=45+15*FMath::Sin(Frame*.13f);
     Bodies[I]->SetActorLocation(Center+FVector(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),-90.75+35*FMath::Sin(Frame*.1f)));
    }
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-90.75),Seed.ToUnrealQuat());
    VR->MoveStick=FVector2D(.4,1);PC->PlayerTick(1.f/30);Airborne+=PC->bJumpAirborne?1:0;
    if(ReportedCell==0x01430171)
    {
     const FVector At=Pawn->GetActorLocation();TArray<FHitResult> Contacts;
     ACEBodySweep::SweepBodyContacts(*World,Contacts,At,At+FVector(0,0,.001),Shape,Environment);
     WallOverlaps+=Contacts.ContainsByPredicate([](const FHitResult& H){return H.bStartPenetrating && H.PenetrationDepth>.5;});
    }
   }
   if(!Tracked)
   {
    PC->PlayerInput->InputKey(FInputKeyParams(EKeys::W,IE_Released,0.,false));
    PC->PlayerInput->InputKey(FInputKeyParams(EKeys::LeftShift,IE_Released,0.,false));
   }
   if(!Jump)TestEqual(*FString::Printf(TEXT("Reported swarm floor stays grounded: cell=%08X tracked=%d run=%d"),ReportedCell,Tracked,Run),Airborne,0);
   else
   {
    // At the new location an overlapping sphere can block ascent on the very
    // first frame. That must settle at the floor, not force a sideways shove
    // just to manufacture airtime. Preserve the older fixtures' ascent checks.
    if(ReportedCell!=0x01430171)TestTrue(TEXT("Crowd test actually launches a jump"),Airborne>0);
    TestFalse(*FString::Printf(TEXT("Swarm jump settles and releases input: cell=%08X tracked=%d run=%d feet=%s"),ReportedCell,Tracked,Run,*PC->PredictedPose.Location.ToString()),PC->bJumpAirborne || PC->bStandingJumpLocked);
   }
   if(ReportedCell==0x01430171)TestEqual(*FString::Printf(TEXT("Hallway swarm never embeds the player: tracked=%d run=%d jump=%d"),Tracked,Run,Jump),WallOverlaps,0);
  }
  for(auto* A:Bodies)A->Destroy();
  ReportedRoom->Destroy();
 }
 // Enter a swarm from outside, with the full controller integrating gravity,
 // bouncing velocity and updating the position reported to the server.
 for(bool Reported:{false,true})
 {
  FACEPosition Report;Report.CellId=0x0143014F;Report.Location=FVector(36.698425,-29.878906,0);
  const FVector Base=Reported?Report.ToUnrealLocation(100):Contact+FVector(10000,10000,2000);
  TArray<AACEEnvCellActor*> Cells;
  if(Reported)
  {
   TArray<uint32> Ids{0x0143014F};
   for(int32 Index=0;Index<Ids.Num() && Index<24;++Index)
   {
    auto* Cell=World->SpawnActor<AACEEnvCellActor>();
    Cell->LoadEnvCell(Ids[Index],FVector(-19200,67*19200,0),100);Cell->SetEnvCellCollisionActive(true);Cells.Add(Cell);
    const auto* Mesh=Dat->FindEnvCellMesh(Ids[Index],100);
    if(Mesh && Index<4)for(const auto& Portal:Mesh->CellPortals)
     if(!Portal.IsOutsidePortal())Ids.AddUnique(0x01430000u|Portal.OtherCellId);
   }
  }
  auto* FloorActor=World->SpawnActor<AActor>();auto* Floor=NewObject<UBoxComponent>(FloorActor);
  FloorActor->SetRootComponent(Floor);Floor->SetBoxExtent(FVector(5000,5000,10));
  Floor->SetCollisionEnabled(Reported?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryOnly);Floor->SetCollisionResponseToAllChannels(ECR_Block);
  Floor->RegisterComponent();FloorActor->SetActorLocation(Base-FVector(0,0,10));
  TArray<AACEWorldEntityActor*> Mobs;
  for(int32 I=0;I<32;++I)
  {
   FACEWorldObject Mob;Mob.Guid=0x78002000+I;Mob.SetupId=0x02001121;Mob.Scale=1.2f;
   Mob.ItemType=ACEItemType::Creature;Mob.PhysicsState=ACEPhysicsState::Gravity;
   auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Mob,100,true);Mobs.Add(Actor);
  }
  int32 JumpCases=0;
  for(bool Tracked:{false,true})for(int32 FPS:{30,144})for(int32 Mode=0;Mode<3;++Mode)for(int32 Heading=0;Heading<8;++Heading)
  {
   const FVector Direction(FMath::Cos(Heading*PI/4),FMath::Sin(Heading*PI/4),0);
   FACEPosition Pose;Pose.CellId=Reported?0x0143014F:0xC98C0129;Pose.SetLocationFromUnreal(Base-Direction*(Reported?0:400),100);
   const FVector Start=Pose.ToUnrealLocation(100)+FVector(0,0,90.75);
   FCollisionQueryParams Env=Query;for(auto* Mob:Mobs)Env.AddIgnoredActor(Mob);
   TArray<FHitResult> Initial;ACEBodySweep::SweepBodyContacts(*World,Initial,Start,Start+FVector(0,0,.001),Shape,Env);
   if(Initial.ContainsByPredicate([](const FHitResult& H){return H.bStartPenetrating && H.PenetrationDepth>.05;}))continue;
   Pose.SetAceFacingFromUnrealDir2D(Direction);Session->SetLocalPosition(Pose);
   VR->bActive=Tracked;VR->MoveStick=FVector2D::ZeroVector;
   PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
   PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
   Pawn->SetActorLocationAndRotation(Pose.ToUnrealLocation(100)+FVector(0,0,90.75),Pose.ToUnrealQuat());
   PC->bRunning=true;PC->bJumpCharging=true;PC->ReleaseJump(1,.4f);
   PC->JumpWorldAceVelocity=FVector(-Direction.X*6,Direction.Y*6,6);
   int32 Frames=0;
   for(;Frames<FPS*8 && PC->bJumpAirborne;++Frames)
   {
    for(int32 I=0;I<Mobs.Num();++I)
    {
     const double Angle=I*2*PI/Mobs.Num()+(Mode?Frames*.4/FPS:0);
     const FVector At=Pawn->GetActorLocation();
     const FVector Center=Mode==2 && Frames>FPS*.35 ? FVector(At.X,At.Y,Base.Z) : Base+Direction*(Reported?150:0);
     Mobs[I]->SetActorLocation(Center+FVector(80*FMath::Cos(Angle),80*FMath::Sin(Angle),Mode==1?150:0));
    }
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,84.25),Direction.Rotation());
    PC->PlayerTick(1.f/FPS);
   }
   TestFalse(FString::Printf(TEXT("Controller swarm jump lands: reported=%d tracked=%d fps=%d mode=%d heading=%d frames=%d feet=%.3f velocity=%s"),
    Reported,Tracked,FPS,Mode,Heading,Frames,Pawn->GetActorLocation().Z-Base.Z-90.75,*PC->JumpWorldAceVelocity.ToString()),PC->bJumpAirborne);
   TestTrue(TEXT("Landing in a swarm restores the reported server contact state"),Session->bAutoPosContact);
   ++JumpCases;
  }
  TestTrue(TEXT("Swarm integration exercises valid starting positions"),JumpCases>0);
  AddInfo(FString::Printf(TEXT("Completed %d controller swarm jumps, reported dungeon=%d"),JumpCases,Reported));
  if(!Reported)
  {
   // A small wall overlap plus a nearby (not yet overlapping) wasp blocks
   // depenetration. Exercise the actual controller and network contact flag.
   auto* WallActor=World->SpawnActor<AActor>();auto* Wall=NewObject<UBoxComponent>(WallActor);
   WallActor->SetRootComponent(Wall);Wall->SetBoxExtent(FVector(10,1000,1000));
   Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Wall->SetCollisionResponseToAllChannels(ECR_Block);
   Wall->RegisterComponent();WallActor->SetActorLocation(Base+FVector(-10,0,0));
   TArray<FACEDatCollisionShape> Spheres;bool BSP=false;Dat->GetSetupCollisionShapes(0x02001121,Spheres,BSP);
   double Width=0;
   for(const auto& S:Spheres)for(double Z:{48.,133.5})
    Width=FMath::Max(Width,FMath::Sqrt(FMath::Max(0.,FMath::Square(48.+S.Radius*120)-FMath::Square(Z-S.Origin.Z*120))));
   for(bool Tracked:{false,true})for(int32 FPS:{30,144})for(double Depth:{.2,2.})
   {
    for(auto* Mob:Mobs)Mob->SetActorLocation(Base+FVector(48-Depth+Width+.01,0,250-90.75));
    FACEPosition Pose;Pose.CellId=0xC98C0129;Pose.SetLocationFromUnreal(Base+FVector(48-Depth,0,250-90.75),100);
    Pose.SetAceFacingFromUnrealDir2D(FVector(0,1,0));Session->SetLocalPosition(Pose);
    VR->bActive=Tracked;VR->MoveStick=FVector2D::ZeroVector;
    PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
    PC->bJumpAirborne=true;PC->bStandingJumpLocked=true;PC->StepHoldSeconds=0;
    PC->JumpWorldAceVelocity=FVector(0,0,-1);Session->SetReportedContact(false);
    Pawn->SetActorLocationAndRotation(Pose.ToUnrealLocation(100)+FVector(0,0,90.75),Pose.ToUnrealQuat());
    for(int32 Frame=0;Frame<FPS*3 && PC->bJumpAirborne;++Frame)
    {
     VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,84.25),FRotator(0,90,0));
     PC->PlayerTick(1.f/FPS);
    }
    TestFalse(FString::Printf(TEXT("Crowded wall releases airborne/input lock: vr=%d fps=%d depth=%.2f"),Tracked,FPS,Depth),
     PC->bJumpAirborne || PC->bStandingJumpLocked);
    TestTrue(TEXT("Recovered landing reports ground contact to the server"),Session->bAutoPosContact);
    TestTrue(TEXT("Recovered landing reports the same feet position as the visual body"),
     Session->GetPlayerPosition().ToUnrealLocation(100).Equals(Pawn->GetActorLocation()-FVector(0,0,90.75),.1));
   }
   WallActor->Destroy();
  }
  for(auto* Mob:Mobs)Mob->Destroy();FloorActor->Destroy();for(auto* Cell:Cells)Cell->Destroy();
 }
 Session->State=EACESessionState::Disconnected;Session->PlayerGuid=0;
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
