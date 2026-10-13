#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "ACEInputBindings.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "ACEHoverTooltipWidget.h"
#include "ACELandblockActor.h"
#include "ACEEnvCellActor.h"
#include "ACETerrainPresenterComponent.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "Components/CapsuleComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPositionPrecisionTest,"ACE.RetailParity.PositionPrecision",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEPositionPrecisionTest::RunTest(const FString&)
{
 // Retail Position::localtolocal retains a cell-local frame and applies only
 // the relative block offset. A float world-space intermediate loses small
 // slides and can move a point to the other side of a cell portal.
 for(uint32 Cell:{0x01010001u,0xBDD2010Eu,0xFFFF0100u})for(float Scale:{1.f,100.f,125.f})
 {
  FACEPosition Pose;Pose.CellId=Cell;Pose.Location=FVector(183.0005,84.0005,186.005);
  const FVector Offset(double((Cell>>24)&255)*192.,double((Cell>>16)&255)*192.,0);
  const FVector ExactWorld=FACEPosition::AceVectorToUnreal(Offset+Pose.Location,Scale);
  const FString Case=FString::Printf(TEXT("Cell %08X scale %.0f"),Cell,Scale);
  TestTrue(*(Case+TEXT(" keeps local precision in Unreal coordinates")),Pose.ToUnrealLocation(Scale).Equals(ExactWorld,1.e-7));
  FACEPosition RoundTrip=Pose;RoundTrip.SetLocationFromUnreal(ExactWorld,Scale);
  TestTrue(*(Case+TEXT(" restores the cell-local point")),RoundTrip.Location.Equals(Pose.Location,1.e-9));
  TestTrue(*(Case+TEXT(" transit conversion uses the same point")),ACECellTransit::UnrealToAceGlobal(ExactWorld,Scale).Equals(Offset+Pose.Location,1.e-9));
  const FVector Start=Pose.Location;
  for(int32 I=0;I<1000;++I)Pose.SetLocationFromUnreal(Pose.ToUnrealLocation(Scale)+FVector(-.0005*Scale,.0003*Scale,0),Scale);
  TestTrue(*(Case+TEXT(" does not discard repeated small collision slides")),Pose.Location.Equals(Start+FVector(.5,.3,0),1.e-6));
 }
 // A sphere just short of touching a landcell/block edge must not add that
 // neighbor early, or keep it after the body has cleared the boundary.
 for(int32 Axis:{0,1})for(double Edge:{24.,192.})for(bool Touching:{false,true})
 {
  FVector Center(189.*192.+12.,210.*192.+12.,0);
  Center[Axis]=(Axis==0?189.:210.)*192.+Edge+.5+(Touching?-.0005:.0005);
  TArray<uint32> Cells;ACECellTransit::AddOutdoorLandNeighbors(0xBDD20001,Center,.5f,Cells);
  const uint32 LeftCell=0xBDD20000u|(Edge==24.?1u:(Axis==0?57u:8u));
  TestEqual(TEXT("Outdoor transit retains the precise side of a sphere boundary"),Cells.Contains(LeftCell),Touching);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEBuildingPassageTest,"ACE.RetailParity.BuildingPassages",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEBuildingPassageTest::RunTest(const FString&)
{
 // Keep the regression independent of the developer's saved key bindings.
 ON_SCOPE_EXIT { ACEInputBindings::Reload(); };
 TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/BuildingPassageFixture.ini"));
 FConfigFile Config;Config.NoSave=false;Config.bCanSaveAllSections=true;
 GConfig->SetFile(GGameUserSettingsIni,&Config);ACEInputBindings::Reload();
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
 auto* Host=World->SpawnActor<AActor>();auto* Terrain=NewObject<UACETerrainPresenterComponent>(Host);Host->AddInstanceComponent(Terrain);
 Terrain->Client=Client;Terrain->bHasKnownCell=true;
 for(uint32 Block:{0xBDD20000u,0xBED20000u})
 {
  Dat->GetOrBuildLandblockMesh(Block,100);
  auto* Land=World->SpawnActor<AACELandblockActor>();Land->SetSkipNonBuildingScenery(false);
  TestTrue(TEXT("Castle land loads"),Land->LoadLandblock(Block,100));Terrain->Spawned.Add(Block,Land);
  for(int I=0;I<1500&&!Land->IsSceneryComplete();++I){++GFrameCounter;Land->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);FPlatformProcess::Sleep(.001f);}
  TestTrue(TEXT("Exterior scenery is ready"),Land->IsSceneryComplete());
  FACEDatLandblockInfo Info;if(!Dat->LoadLandblockInfo(Block,Info))continue;
  const FVector Origin=FACEPosition::AceVectorToUnreal(FVector((Block>>24)*192,((Block>>16)&255)*192,0),100);
  for(uint32 I=0;I<Info.NumCells;++I)
  {
   const uint32 Id=Block|(0x100+I);const auto* Cell=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Cell)continue;
   for(const auto& Stab:Cell->StaticObjects)Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
   auto* Room=World->SpawnActor<AACEEnvCellActor>();Room->LoadEnvCell(Id,Origin,100);Room->SetEnvCellCollisionActive(true);Room->EnsureStaticObjectsQueued();
   for(int J=0;J<200&&Room->HasPendingStaticObjects();++J){++GFrameCounter;Room->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);}
   Terrain->SpawnedEnvCells.Add(Id,Room);
  }
 }
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);Capsule->InitCapsuleSize(48,88);
 Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);
 PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();PC->bRetailCursorInstalled=true;
 PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;Session->WorldObjects.Add(Self.Guid,Self);
 PC->ApplyPlayerCapsuleFromSetup(Self.SetupId);Session->PlayerVitals.bValid=true;Session->PlayerVitals.RunSkillCurrent=100;
 Session->PlayerVitals.Health=Session->PlayerVitals.Stamina=Session->PlayerVitals.MaxHealth=Session->PlayerVitals.MaxStamina=500;Session->OnVitalsUpdated.Broadcast(Session->PlayerVitals);
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 if(!TestNotNull(TEXT("Tracked fixture has a headset camera"),VR->Head.Get()))return false;
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->bRun=true;VR->Settings->MovementDirection=0;
 PC->InputComponent->AxisBindings.Reset();
 const FVector Global=FVector(190*192,210*192,0);
 const TArray<TPair<FVector,FVector>> Paths={
  {FVector(12,84,186),FVector(-20,84,186)},
  {FVector(12,83,186),FVector(-20,83,186)},
  {FVector(12,85,186),FVector(-20,85,186)},
  {FVector(12,82,186),FVector(-20,82,186)},
  {FVector(12,86,186),FVector(-20,86,186)},
  {FVector(12,81.5,186),FVector(-20,81.5,186)},
  {FVector(12,86.5,186),FVector(-20,86.5,186)},
  {FVector(12,80.25,186),FVector(-20,80.25,186)},
  {FVector(12,87.75,186),FVector(-20,87.75,186)},
  {FVector(12,81,186),FVector(-20,87,186)},
  {FVector(12,87,186),FVector(-20,81,186)},
  {FVector(-29,102,186),FVector(-44,102,186)},
  {FVector(-29,66,186),FVector(-44,66,186)},
  {FVector(-54,77,186),FVector(-54,91,186)}};
 // These cells have no authored floor polygons: their horizontal outside
 // portals deliberately retain the terrain. A sphere clear of every outside
 // plane must not admit land merely because the room can see outside.
 for(const auto& Entry:TArray<TPair<uint32,FVector>>{
  {0xBDD2010Eu,FVector(-6,84,186)}, {0xBDD20106u,FVector(-36,102,186)},
  {0xBDD2010Du,FVector(-36,66,186)}, {0xBDD20123u,FVector(-54,84,186)}})
 {
  for(bool TouchFloor:{true,false})
  {
   TArray<uint32> Cells;
   const FVector Center=FACEPosition::AceVectorToUnreal(Global+Entry.Value+FVector(0,0,TouchFloor?.2:1.2),100);
   ACECellTransit::FindCellList(*Dat,Entry.Key,Center,21,100,Cells);
   const bool HasLand=Cells.ContainsByPredicate([](uint32 Id){return !ACECellTransit::IsIndoorCell(Id);});
   TestEqual(*FString::Printf(TEXT("Outside plane transit %08X touching=%d"),Entry.Key,TouchFloor),HasLand,TouchFloor);
  }
 }
 // The gate's two vertical portals are at local X=183/189. Positions on
 // opposite sides of either portal must remain distinct after presentation
 // conversion; the old float world coordinate rounded both onto the plane.
 for(double Edge:{183.,189.})for(bool Inside:{false,true})
 {
  FACEPosition At;At.CellId=0xBDD2010E;
  At.Location=FVector(Edge+(Edge==183.?1.:-1.)*(Inside?.0005:-.0005),84,186.9);
  TestEqual(TEXT("Gate containment retains the side of a near-portal point"),Dat->IsPointInsideEnvCell(At.CellId,At.ToUnrealLocation(100),100),Inside);
 }
 Client->SetRunSkill(400);
 for(const auto& OriginalPath:Paths)for(bool Reverse:{false,true})for(bool Tracked:{false,true})for(bool Running:{false,true})for(float Dt:{1.f/30,1.f/60,1.f/144,1.f/240})
 {
  const TPair<FVector,FVector> Path=Reverse?TPair<FVector,FVector>(OriginalPath.Value,OriginalPath.Key):OriginalPath;
  VR->bActive=Tracked;VR->Settings->bRun=Running;
  FACEPosition Pose;Pose.CellId=0xBED20001;Pose.SetLocationFromUnreal(FACEPosition::AceVectorToUnreal(Global+Path.Key,100),100);
  const FVector Direction=FACEPosition::AceVectorToUnreal((Path.Value-Path.Key).GetSafeNormal(),1);Pose.SetAceFacingFromUnrealDir2D(Direction);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bLocalPredicting=false;PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
  PC->JumpWorldAceVelocity=FVector::ZeroVector;PC->JumpAirborneSeconds=0;Session->SetLocalPosition(Pose);
  Pawn->SetActorLocationAndRotation(Pose.ToUnrealLocation(100)+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()),Pose.ToUnrealQuat());
  Terrain->CommitOccupancyCellId(Pose.CellId);Terrain->ViewerCellId=Pose.CellId;Terrain->UpdateBuildingVisibility();Terrain->UpdateOutdoorEnvCollision();
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));PC->PlayerInput->ProcessInputStack({},Dt,false);
  if(!Running){PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift,IE_Pressed,1.f));PC->PlayerInput->ProcessInputStack({},Dt,false);}
  const FVector Start=Pose.ToUnrealLocation(100);double Progress=0;int32 IndoorFrames=0,AirFrames=0,SlowFrames=0;
  const double ExpectedStep=Client->GetLocomotionSpeed(Running)*100*Dt;
  double MaxGroundError=0;float StreamTime=0;
  for(int Frame=0;Frame<FMath::CeilToInt(12.f/Dt)&&Progress<FVector::Distance(Path.Key,Path.Value)*100;++Frame)
  {
   ++GFrameCounter;
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Capsule->GetScaledCapsuleHalfHeight()),Direction.Rotation());
   const FVector Before=PC->PredictedPose.ToUnrealLocation(100);const uint32 BeforeCell=PC->PredictedPose.CellId;
   VR->MoveStick=FVector2D(0,1);PC->PlayerTick(Dt);
   StreamTime+=Dt;
   if(StreamTime>=.1f){StreamTime=0;if(!ACECellTransit::IsIndoorCell(Terrain->LastKnownCellId))Terrain->UpdateOutdoorEnvCollision();}
   Terrain->UpdateCameraVisibility();
   IndoorFrames+=ACECellTransit::IsIndoorCell(PC->PredictedPose.CellId)?1:0;AirFrames+=PC->bJumpAirborne?1:0;
   Progress=FVector::DotProduct(PC->PredictedPose.ToUnrealLocation(100)-Start,Direction);
   const double Step=FVector::DotProduct(PC->PredictedPose.ToUnrealLocation(100)-Before,Direction);
   float FloorZ=0;const FVector NowFeet=PC->PredictedPose.ToUnrealLocation(100);
   if(Frame>4&&Dat->SampleOutdoorGroundZ(NowFeet.X,NowFeet.Y,100,FloorZ))MaxGroundError=FMath::Max(MaxGroundError,FMath::Abs(NowFeet.Z-FloorZ));
   // Ground contact keeps a 0.1 cm skin along the surface normal. On a
   // slope it has a small XY component independent of frame duration.
   if(Frame>4&&Step+.1<ExpectedStep*.98)
   {
    if(SlowFrames<4)AddInfo(FString::Printf(TEXT("Passage slowdown cell=%08X -> %08X pos=%s step=%.3f expected=%.3f"),BeforeCell,uint32(PC->PredictedPose.CellId),*PC->PredictedPose.Location.ToString(),Step,ExpectedStep));
    ++SlowFrames;
   }
  }
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));PC->PlayerInput->ProcessInputStack({},Dt,false);
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift,IE_Released,0.f));PC->PlayerInput->ProcessInputStack({},Dt,false);
  VR->MoveStick=FVector2D::ZeroVector;
  const FVector End=PC->PredictedPose.ToUnrealLocation(100);
  const FString Case=FString::Printf(TEXT("Passage %s -> %s tracked=%d run=%d hz=%.0f"),*Path.Key.ToString(),*Path.Value.ToString(),Tracked,Running,1.f/Dt);
  AddInfo(FString::Printf(TEXT("%s: travel=%.2fm indoor=%d air=%d groundError=%.3f"),*Case,Progress/100,IndoorFrames,AirFrames,MaxGroundError));
  TestTrue(*(Case+TEXT(" crosses completely")),Progress>=FVector::Distance(Path.Key,Path.Value)*100-20);
  TestTrue(*(Case+TEXT(" enters its interior cells")),IndoorFrames>0);
  TestEqual(*(Case+TEXT(" stays supported")),AirFrames,0);
  TestEqual(*(Case+TEXT(" crosses seams without catching or slowing")),SlowFrames,0);
  TestTrue(*(Case+TEXT(" has no vertical seam jitter")),MaxGroundError<2);
  float TerrainZ=0;
  TestTrue(*(Case+TEXT(" has terrain support")),Dat->SampleOutdoorGroundZ(End.X,End.Y,100,TerrainZ));
  TestTrue(*(Case+TEXT(" stays on terrain beneath the bridge")),FMath::Abs(End.Z-TerrainZ)<2);
 }
 return true;
}
#endif
