#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEPlayerController.h"
#include "ACEBodySweep.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "ACETerrainPresenterComponent.h"
#include "ACEHoverTooltipWidget.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEFortTethStairsTest,"ACE.RetailParity.FortTethStairs",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEFortTethStairsTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 ON_SCOPE_EXIT { GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
 auto Session=Client->GetSession();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 auto* Host=World->SpawnActor<AActor>();auto* Terrain=NewObject<UACETerrainPresenterComponent>(Host);
 Terrain->Client=Client;Terrain->bHasKnownCell=true;
 TArray<AACEEnvCellActor*> Rooms;
 for(uint32 Block:{0x26810000u,0x25810000u,0xE4540000u,0xC6A90000u})
 {
  const FVector Origin=FACEPosition::AceVectorToUnreal(FVector((Block>>24)*192,((Block>>16)&255)*192,0),100);
  FACEDatLandblockInfo Info;if(!Dat->LoadLandblockInfo(Block,Info))return false;
  for(const auto& Building:Info.Buildings)Dat->GetOrBuildSetupMesh(Building.ModelId,100);
  auto* Land=World->SpawnActor<AACELandblockActor>();Land->SetSkipNonBuildingScenery(true);
  TestTrue(TEXT("Teth building shells load"),Land->LoadLandblockSceneryOnly(Block,100));Terrain->Spawned.Add(Block,Land);
  for(int32 I=0;I<100;++I){++GFrameCounter;Land->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);}
  TestTrue(TEXT("Fixture includes solid exterior shells"),Land->GetDoorwayClipMeshCount()>0);
  for(uint32 Index=0;Index<Info.NumCells;++Index)
  {
   const uint32 Id=Block| (0x100+Index);
   const auto* Built=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Built)return false;
   const auto Stabs=Built->StaticObjects;
   for(const auto& Stab:Stabs)Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
   auto* Room=World->SpawnActor<AACEEnvCellActor>();Room->LoadEnvCell(Id,Origin,100);
   Room->SetEnvCellCollisionActive(true);Room->EnsureStaticObjectsQueued();
   for(int32 I=0;I<200 && Room->HasPendingStaticObjects();++I)
   { ++GFrameCounter;Room->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); }
   TestFalse(TEXT("Fort Teth stair geometry is loaded"),Room->HasPendingStaticObjects());
   Terrain->SpawnedEnvCells.Add(Id,Room);Rooms.Add(Room);
  }
 }
 auto* Controller=World->SpawnActor<AACEPlayerController>();Controller->Client=Client;
 Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(Controller);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,88);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);Controller->Possess(Pawn);
 Controller->PlayerInput=NewObject<UPlayerInput>(Controller);Controller->SetupInputComponent();
 Controller->bRetailCursorInstalled=true;Controller->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 Controller->bEnterWorldLoading=false;Controller->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);Controller->ApplyPlayerCapsuleFromSetup(Self.SetupId);
 const float Half=Capsule->GetScaledCapsuleHalfHeight();
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=Controller;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 if(!TestNotNull(TEXT("Tracked stair fixture has a headset camera"),VR->Head.Get()))return false;
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->bRun=true;VR->Settings->MovementDirection=0;
 // Hardware axis polling must not overwrite this fixture's simulated stick.
 Controller->InputComponent->AxisBindings.Reset();
 struct FEntrance { uint32 Cell;FVector Location;FVector Down; };
 const FEntrance Entrances[]={
  {0x2681000B,FVector(39.339844,61.687500,235.084335),FVector(0,-1,0)},
  {0x2581003A,FVector(182.962891,37.648438,235.045807),FVector(0,-1,0)},
  {0x25810019,FVector(85.721680,9.376953,235.110977),FVector(1,0,0)},
  {0x25810012,FVector(61.653809,33.345703,235.052048),FVector(1,0,0)},
  {0x25810015,FVector(56.484375,106.345703,235.053284),FVector(0,1,0)},
  {0x2581003D,FVector(178.353516,110.949219,235.044220),FVector(-1,0,0)}
 };
 auto Place=[&](FACEPosition Pose)
 {
  Session->SetLocalPosition(Pose);Controller->PredictedPose=Pose;Controller->bHavePredictedPose=true;
  Controller->bHaveLastServerPose=false;Controller->bLocalPredicting=false;
  Controller->bJumpAirborne=false;Controller->bStandingJumpLocked=false;Controller->StepHoldSeconds=0;
  Controller->JumpWorldAceVelocity=FVector::ZeroVector;Controller->JumpAirborneSeconds=0;
  Pawn->SetActorLocationAndRotation(Pose.ToUnrealLocation(100)+FVector(0,0,Half),Pose.ToUnrealQuat());
 };
 auto Refresh=[&]()
 {
  Terrain->LastKnownCellId=Controller->PredictedPose.CellId;
  if(ACECellTransit::IsIndoorCell(Terrain->LastKnownCellId))Terrain->UpdateBuildingVisibility();
  else Terrain->UpdateOutdoorEnvCollision();
 };
 // Compare the complete authored collision with runtime residency around the
 // reported walls. A drawable wall must not become passable at a cell boundary.
 for(const auto& Entry:TArray<TPair<uint32,FVector>>{
  {0x25810019,FVector(85.270020,23.521484,220)},
  {0xE454000E,FVector(40.824219,131.324219,6)},
  {0xE454001E,FVector(83.945312,125.347656,6)},
  {0xE454000E,FVector(41.695312,130.730469,6)},
  {0xE454001E,FVector(79.648438,134.835938,20.869003)},
  {0xE454001E,FVector(79.019531,132.884766,14.8)}})
 {
  FACEPosition Pose;Pose.CellId=Entry.Key;Pose.Location=Entry.Value;Place(Pose);
  const FVector Center=Pose.ToUnrealLocation(100)+FVector(0,0,Half);
  FCollisionQueryParams Query(NAME_None,true,Pawn);
  for(auto* Room:Rooms)Room->SetEnvCellCollisionActive(true);
  for(const auto& Pair:Terrain->Spawned)if(Pair.Value)Pair.Value->SetBuildingShellsBlockPawn(true);
  TArray<FHitResult> Reference;Reference.SetNum(24);
  for(int I=0;I<24;++I)
  {
   const FVector D=FRotator(0,I*15,0).Vector()*400;
   World->LineTraceSingleByChannel(Reference[I],Center,Center+D,ECC_Pawn,Query);
  }
  Refresh();int32 WallCount=0;
  for(int I=0;I<24;++I)
  {
   const auto& Wall=Reference[I];if(!Wall.bBlockingHit || FMath::Abs(Wall.ImpactNormal.Z)>.3f)continue;
   ++WallCount;FHitResult Actual;const FVector End=Center+FRotator(0,I*15,0).Vector()*400;
   World->LineTraceSingleByChannel(Actual,Center,End,ECC_Pawn,Query);
   TestTrue(*FString::Printf(TEXT("Wall remains resident %08X yaw=%d component=%s dist=%.1f actual=%.1f"),Entry.Key,I*15,*GetNameSafe(Wall.GetComponent()),Wall.Distance,Actual.Distance),Actual.bBlockingHit && Actual.Distance<=Wall.Distance+1);
  }
  AddInfo(FString::Printf(TEXT("Reported wall %08X %s exercised %d directions"),Entry.Key,*Entry.Value.ToString(),WallCount));
  const FHitResult* Nearest=nullptr;
  for(const auto& Wall:Reference)
   if(Wall.bBlockingHit && FMath::Abs(Wall.ImpactNormal.Z)<.3f && (!Nearest || Wall.Distance<Nearest->Distance))Nearest=&Wall;
  if(Nearest)for(bool Tracked:{false,true})
  {
   const FVector Direction=(Nearest->TraceEnd-Nearest->TraceStart).GetSafeNormal2D();
   Pose.SetAceFacingFromUnrealDir2D(Direction);Place(Pose);VR->bActive=Tracked;
   Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
   Controller->PlayerInput->ProcessInputStack({},1.f/30,false);
   for(int Frame=0;Frame<120;++Frame)
   {
    // Alternate along and into the wall; straight-on contact missed the
    // reported repeated corner escape through a neighboring triangle.
    const FVector Across=FVector::CrossProduct(Direction,FVector::UpVector);
    const FVector Travel=(Direction+Across*((Frame/15)%2 ? -.8f : .8f)).GetSafeNormal();
    Controller->PredictedPose.SetAceFacingFromUnrealDir2D(Travel);
    Pawn->SetActorRotation(Controller->PredictedPose.ToUnrealQuat());
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),Travel.Rotation());
    VR->MoveStick=FVector2D(0,1);Refresh();Controller->PlayerTick(1.f/30);
   }
   const double WallSide=FVector::DotProduct(Pawn->GetActorLocation()-Nearest->ImpactPoint,Nearest->ImpactNormal);
   TestTrue(*FString::Printf(TEXT("Actual movement cannot cross reported wall %08X tracked=%d side=%.1f"),Entry.Key,Tracked,WallSide),WallSide>=-.5);
   Controller->PlayerInput->FlushPressedKeys();Controller->PlayerInput->ProcessInputStack({},1.f/30,false);VR->MoveStick=FVector2D::ZeroVector;
  }
 }
 // Reported center-post/stairwell wedge: ordinary directional movement must
 // offer a way back out, without jumping or crossing the post/stair geometry.
 for(bool Tracked:{false,true})for(float Dt:{1.f/90,1.f/30})
 {
  VR->bActive=Tracked;int32 Escapes=0;
  for(int32 Yaw=0;Yaw<360;Yaw+=30)
  {
   const FVector Travel=FRotator(0,Yaw,0).Vector();
   FACEPosition Pose;Pose.CellId=0xC6A901AE;Pose.Location=FVector(19.925781,21.117188,42.084137);
   Pose.SetAceFacingFromUnrealDir2D(Travel);Place(Pose);Refresh();
   const FVector Start=Pawn->GetActorLocation();
   Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
   Controller->PlayerInput->ProcessInputStack({},Dt,false);
   int32 AirFrames=0;
   for(int32 Frame=0;Frame<FMath::CeilToInt(1.5f/Dt);++Frame)
   {
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),Travel.Rotation());
    VR->MoveStick=FVector2D(0,1);Refresh();Controller->PlayerTick(Dt);
    AirFrames+=Controller->bJumpAirborne?1:0;
    if(FVector::Dist2D(Start,Pawn->GetActorLocation())>150)break;
   }
   const double Distance=FVector::Dist2D(Start,Pawn->GetActorLocation());
   if(Distance>100 && AirFrames==0)++Escapes;
   AddInfo(FString::Printf(TEXT("Center post C6A901AE tracked=%d hz=%.0f yaw=%d travel=%.1f air=%d"),Tracked,1.f/Dt,Yaw,Distance,AirFrames));
   Controller->PlayerInput->FlushPressedKeys();Controller->PlayerInput->ProcessInputStack({},Dt,false);VR->MoveStick=FVector2D::ZeroVector;
  }
  TestTrue(*FString::Printf(TEXT("Center post allows ordinary escape without a jump tracked=%d hz=%.0f exits=%d"),Tracked,1.f/Dt,Escapes),Escapes>=4);
 }
 for(const auto& Entry:Entrances)for(bool Tracked:{false,true})for(float Dt:{1.f/90,1.f/30})for(int32 Lane:{-1,0,1})
 {
  VR->bActive=Tracked;
  FACEPosition Pose;Pose.CellId=Entry.Cell;Pose.Location=Entry.Location;
  Pose.SetAceFacingFromUnrealDir2D(Entry.Down);Place(Pose);
  FVector Start=Pose.ToUnrealLocation(100);
  Refresh();
  FHitResult Flight;
  const FVector FlightXY=Start+Entry.Down*125;
  World->LineTraceSingleByChannel(Flight,FlightXY+FVector(0,0,100),FlightXY-FVector(0,0,400),ECC_Pawn,FCollisionQueryParams(NAME_None,true,Pawn));
  const auto* FlightOwner=Cast<AACEEnvCellActor>(Flight.GetActor());
  if(!TestNotNull(TEXT("Top flight has its authored stair collision"),FlightOwner))return false;
  const int32 StairCell=FlightOwner->EnvCellId;
  const FVector Side=FVector::CrossProduct(Entry.Down,FVector::UpVector)*Lane;
  FCollisionQueryParams StairQuery(NAME_None,true,Pawn);
  const float Radius=Capsule->GetScaledCapsuleRadius();
  const auto Shape=FCollisionShape::MakeCapsule(Radius,Half);
  float LaneOffset=0;
  if(Lane!=0)
  {
   float MidFeetZ=Flight.ImpactPoint.Z;
   ACEBodySweep::FindFootSupport(*World,Flight.ImpactPoint,Radius,100,StairQuery,MidFeetZ,60);
   const FVector Mid(FlightXY.X,FlightXY.Y,MidFeetZ+Half);
   FHitResult SideHit;
   if(ACEBodySweep::Sweep(*World,SideHit,Mid,Mid+Side*160,Shape,StairQuery))
    LaneOffset=FMath::Max(0.,FVector::DotProduct(SideHit.Location-Mid,Side)-1.);
   else
   {
    // Open-sided flight: find the authored tread edge rather than inventing
    // a fixture-specific width. Keep the feet just inside that edge.
    for(float Offset=5;Offset<=160;Offset+=5)
    {
     FHitResult Edge;const FVector At=Flight.ImpactPoint+Side*Offset;
     if(!World->LineTraceSingleByChannel(Edge,At+FVector(0,0,40),At-FVector(0,0,40),ECC_Pawn,StairQuery)
       || Edge.ImpactNormal.Z<.6641741f || FMath::Abs(Edge.ImpactPoint.Z-Flight.ImpactPoint.Z)>10)break;
     LaneOffset=Offset-1;
    }
   }
   Start+=Side*LaneOffset;Pose.SetLocationFromUnreal(Start,100);Place(Pose);Refresh();
   AddInfo(FString::Printf(TEXT("Stair edge %08X lane=%d offset=%.1f"),Entry.Cell,Lane,LaneOffset));
  }
  Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
  Controller->PlayerInput->ProcessInputStack({},Dt,false);
  int32 AirFrames=0;
  for(int32 Frame=0;Frame<FMath::CeilToInt(2.f/Dt);++Frame)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),Entry.Down.Rotation());
   VR->MoveStick=FVector2D(0,1);
   Refresh();Controller->PlayerTick(Dt);AirFrames+=Controller->bJumpAirborne?1:0;
   if(FVector::DotProduct(Pawn->GetActorLocation()-Start,Entry.Down)>325)break;
  }
  const FVector End=Pawn->GetActorLocation()-FVector(0,0,Half);
  const float Travel=FVector::DotProduct(End-Start,Entry.Down);
  TestTrue(*FString::Printf(TEXT("Top lip %08X tracked=%d dt=%.3f lane=%d descends (travel %.1f, drop %.1f)"),Entry.Cell,Tracked,Dt,Lane,Travel,Start.Z-End.Z),Travel>300 && End.Z<Start.Z-175 && End.Z>Start.Z-400);
  TestEqual(TEXT("Stair descent remains grounded"),AirFrames,0);
  AddInfo(FString::Printf(TEXT("Teth %08X tracked=%d dt=%.3f travel=%.1f drop=%.1f endcell=%08X"),Entry.Cell,Tracked,Dt,Travel,Start.Z-End.Z,Controller->PredictedPose.CellId));
  // Reverse immediately: the same passage must continue to work uphill.
  Pose=Controller->PredictedPose;Pose.SetAceFacingFromUnrealDir2D(-Entry.Down);Place(Pose);
  for(int32 Frame=0;Frame<FMath::CeilToInt(2.f/Dt);++Frame)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),(-Entry.Down).Rotation());
   Refresh();Controller->PlayerTick(Dt);
   if(FVector::DotProduct(Pawn->GetActorLocation()-End,-Entry.Down)>Travel-10)break;
  }
  TestTrue(*FString::Printf(TEXT("Top lip uphill %08X tracked=%d dt=%.3f lane=%d"),Entry.Cell,Tracked,Dt,Lane),Pawn->GetActorLocation().Z-Half>Start.Z-40);
  Controller->PlayerInput->FlushPressedKeys();Controller->PlayerInput->ProcessInputStack({},Dt,false);VR->MoveStick=FVector2D::ZeroVector;
  // Fall from the jump apex over the top flight while still outdoor-resident.
  Pose.CellId=Entry.Cell;Pose.SetLocationFromUnreal(Start+Entry.Down*125+FVector(0,0,400),100);Place(Pose);
  Controller->bJumpAirborne=true;
  for(int32 Frame=0;Frame<FMath::CeilToInt(3.f/Dt);++Frame)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),Entry.Down.Rotation());
   Refresh();Controller->PlayerTick(Dt);
   if(!Controller->bJumpAirborne)break;
  }
  TestFalse(TEXT("A jump through the upper entrance lands on the stairs"),Controller->bJumpAirborne);
  TestTrue(TEXT("Landing stays on the top flight instead of falling through the tower"),Pawn->GetActorLocation().Z-Half>Start.Z-200);
  if(Lane!=0)
  {
   // Fall beside the flight with the lower sphere straddling its edge. This
   // exercises side/riser/soffit contact, not just a clean vertical landing.
   const FVector Drop=Flight.ImpactPoint+Side*(LaneOffset+Radius*.5f)+FVector(0,0,5);
   Pose.CellId=Entry.Cell;Pose.SetLocationFromUnreal(Drop,100);Place(Pose);
   Controller->bJumpAirborne=true;
   int32 FallingFrames=0;
   for(;FallingFrames<FMath::CeilToInt(3.f/Dt);++FallingFrames)
   {
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),Entry.Down.Rotation());
    Refresh();Controller->PlayerTick(Dt);
    if(!Controller->bJumpAirborne)break;
   }
   AddInfo(FString::Printf(TEXT("Stair side fall %08X tracked=%d hz=%.0f lane=%d seconds=%.3f drop=%.1f"),Entry.Cell,Tracked,1.f/Dt,Lane,FallingFrames*Dt,Drop.Z-(Pawn->GetActorLocation().Z-Half)));
   TestFalse(TEXT("Stair side fall settles without remaining airborne"),Controller->bJumpAirborne);
   TestTrue(TEXT("Stair side fall does not tunnel below the tower floor"),Pawn->GetActorLocation().Z-Half>Start.Z-1600);
  }
  const uint32 Block=Entry.Cell&0xFFFF0000u;
  TSet<int32> Visible;FConvexVolume DownView;
  const FVector Eye=FlightXY+FVector(0,0,575);
  for(const FVector Normal:{FVector(1,0,1),FVector(-1,0,1),FVector(0,1,1),FVector(0,-1,1),FVector(0,0,1)})
   DownView.Planes.Add(FPlane(Eye,Normal.GetSafeNormal()));
  DownView.Init();
  ACEOutdoorPortalPlan::CollectOutdoorAdmittedEnvCells(*Dat,Eye,DownView,100,MakeArrayView(&Block,1),Visible);
  TestTrue(TEXT("The actual stairwell remains drawable looking down through its roof opening"),Visible.Contains(StairCell));
 }
 // First approach from above: collision, not a previous camera peek, must
 // request the deferred stair object. Do not eagerly build every unseen room.
 VR->bActive=false;
 auto* FreshRoom=Terrain->SpawnedEnvCells.FindRef(0x26810100).Get();
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(0x26*192,0x81*192,0),100);
 FreshRoom->LoadEnvCell(0x26810100,Origin,100);
 FACEPosition Arrival;Arrival.CellId=Entrances[0].Cell;Arrival.Location=Entrances[0].Location;Place(Arrival);
 Refresh();
 TestTrue(TEXT("Upper entrance collision queues its deferred stairs without a prior view"),FreshRoom->IsActorTickEnabled());
 for(int32 I=0;I<100 && FreshRoom->HasPendingStaticObjects();++I)
 { ++GFrameCounter;FreshRoom->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); }
 TestFalse(TEXT("First-approach stair collision finishes loading"),FreshRoom->HasPendingStaticObjects());
 return !HasAnyErrors();
}
#endif
