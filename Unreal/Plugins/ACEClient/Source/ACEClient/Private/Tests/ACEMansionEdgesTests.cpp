#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ACEInputBindings.h"
#include "ACEPlayerController.h"
#include "ACEBodySweep.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "ACETerrainPresenterComponent.h"
#include "Dat/ACECellTransit.h"
#include "ACEHoverTooltipWidget.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEMansionEdgesTest,"ACE.RetailParity.MansionEdges",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEMansionEdgesTest::RunTest(const FString&)
{
 ON_SCOPE_EXIT {ACEInputBindings::Reload();};
 TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/MansionFixture.ini"));
 FConfigFile FixtureConfig;FixtureConfig.NoSave=false;FixtureConfig.bCanSaveAllSections=true;
 GConfig->SetFile(GGameUserSettingsIni,&FixtureConfig);ACEInputBindings::Reload();
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 constexpr uint32 Block=0xA9A10000;
 auto* Host=World->SpawnActor<AActor>();auto* Terrain=NewObject<UACETerrainPresenterComponent>(Host);
 Terrain->Client=Client;Terrain->bHasKnownCell=true;
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(169*192,161*192,0),100);
 FACEDatLandblockInfo Info;if(!Dat->LoadLandblockInfo(Block,Info))return false;
 for(const auto& Building:Info.Buildings)Dat->GetOrBuildSetupMesh(Building.ModelId,100);
 auto* Land=World->SpawnActor<AACELandblockActor>();Land->SetSkipNonBuildingScenery(true);
 TestTrue(TEXT("Reported mansion exterior loads"),Land->LoadLandblockSceneryOnly(Block,100));
 Terrain->Spawned.Add(Block,Land);
 for(int32 I=0;I<100;++I){++GFrameCounter;Land->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);}
 TestTrue(TEXT("Mansion fixture includes solid exterior shells"),Land->GetDoorwayClipMeshCount()>0);
 for(uint32 Index=0;Index<Info.NumCells;++Index)
 {
  const uint32 Id=Block|(0x100+Index);
  const auto* Built=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Built)return false;
  const auto Stabs=Built->StaticObjects;
  for(const auto& Stab:Stabs)Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
  auto* Room=World->SpawnActor<AACEEnvCellActor>();Room->LoadEnvCell(Id,Origin,100);
  Room->SetEnvCellCollisionActive(true);Room->EnsureStaticObjectsQueued();
  for(int32 I=0;I<200 && Room->HasPendingStaticObjects();++I)
  {++GFrameCounter;Room->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);}
  TestFalse(TEXT("Mansion static collision finished loading"),Room->HasPendingStaticObjects());
  Terrain->SpawnedEnvCells.Add(Id,Room);
 }
 auto Session=Client->GetSession();
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,90.75);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
 PC->bRetailCursorInstalled=true;PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);PC->ApplyPlayerCapsuleFromSetup(Self.SetupId);
 const float Half=Capsule->GetScaledCapsuleHalfHeight();
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->MovementDirection=0;
 PC->InputComponent->AxisBindings.Reset();Client->SetRunSkill(600);
 // Use the reported pose and a second start on the outer roof rim, so a
 // blocked approach to the narrow roof gap cannot make the edge test vacuous.
 FACEPosition Reported;Reported.CellId=0xA9A10016;
 Reported.Location=FVector(68.678311,127.423019,147.944502);
 TestEqual(TEXT("Human step-down comes from retail Setup, without a one-meter clamp"),PC->GetStepDownHeightCm(),150.f);
 FVector Direction=FVector::YAxisVector;
 auto Refresh=[&]()
 {
  Terrain->LastKnownCellId=PC->PredictedPose.CellId;
  if(ACECellTransit::IsIndoorCell(Terrain->LastKnownCellId))Terrain->UpdateBuildingVisibility();
  else Terrain->UpdateOutdoorEnvCollision();
 };
 auto Place=[&](FVector Location,bool Tracked,float Yaw,bool Run)
 {
  PC->PlayerInput->FlushPressedKeys();PC->ClearServerMoveTo();PC->bAutoRun=false;
  PC->PlayerInput->ProcessInputStack({},.016f,false);
  Direction=FRotator(0,Yaw,0).Vector();
  FACEPosition Pose=Reported;Pose.Location=Location;Pose.SetAceFacingFromUnrealDir2D(Direction);
  Session->SetLocalPosition(Pose);PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bJumpAirborne=false;PC->bJumpCharging=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;PC->bLocalPredicting=false;
  PC->JumpWorldAceVelocity=PC->LandingWorldAceVelocity=FVector::ZeroVector;PC->LandingRootTrack.Reset();
  VR->bActive=Tracked;VR->MoveStick=VR->SmoothedMoveStick=FVector2D::ZeroVector;VR->Settings->bRun=Run;
  Pawn->SetActorLocationAndRotation(Pose.ToUnrealLocation(100)+FVector(0,0,Half),Pose.ToUnrealQuat());
  Refresh();
 };
 auto Tick=[&](float Dt)
 {
  Refresh();
  VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),Direction.Rotation());
  PC->PlayerTick(Dt);
 };
 // The authored rim ends at AC Y=128.25, above the balcony at Z=146.8.
 // Retail's half-radius footprint ends at 128.49 for the human 48 cm sphere.
 // Include the reported heading (qw=.991545, qz=-.129763), direct approach,
 // diagonals, desktop/VR, walk/run, and 10--144 Hz.
 for(const FVector Seed:{Reported.Location,FVector(68.678311,128.1,148.005)})
 for(bool Tracked:{false,true})for(bool Run:{false,true})
 for(float Dt:{1.f/144,1.f/90,1.f/30,.1f})for(float Yaw:{75.f,90.f,104.9119f})
 {
  Place(Seed,Tracked,Yaw,Run);
  if(Tracked)VR->MoveStick=FVector2D(0,1);
  else
  {
   PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
   if(!Run)PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift,IE_Pressed,1.f));
  }
  PC->PlayerInput->ProcessInputStack({},Dt,false);
  int32 LostContact=0;double Lowest=Seed.Z,MaxY=Seed.Y,PositionError=0;
  for(int32 Frame=0;Frame<FMath::CeilToInt(2.f/Dt);++Frame)
  {
   Tick(Dt);
   LostContact+=PC->bJumpAirborne || !Session->bAutoPosContact ? 1 : 0;
   Lowest=FMath::Min(Lowest,PC->PredictedPose.Location.Z);
   MaxY=FMath::Max(MaxY,PC->PredictedPose.Location.Y);
   PositionError=FMath::Max(PositionError,FVector::Dist(Session->GetPlayerPosition().ToUnrealLocation(100),
    Pawn->GetActorLocation()-FVector(0,0,Half)));
   // End diagonal edge slides before they reach the adjoining sloped roof.
   // That surface can legitimately lead downward; this fixture's boundary
   // and height assertions describe the flat roof above the front door.
   if(PC->PredictedPose.Location.X<67 || PC->PredictedPose.Location.X>70)break;
  }
  const FString Case=FString::Printf(TEXT("seedY=%.3f yaw=%.4f vr=%d run=%d hz=%.0f end=%s"),Seed.Y,Yaw,Tracked,Run,1.f/Dt,*PC->PredictedPose.Location.ToString());
  // Reference CTransition on this DAT stops a perpendicular approach. From
  // the outer rim, a 40.83 cm diagonal insertion can reach the balcony within
  // the human Setup's 1.5 m step-down. Do not invent a roof-specific barrier
  // against that legitimate grounded descent.
  const bool bRetailBalconyStep=Seed.Y>128 && !Tracked && Run && Dt>=1.f/30 && Yaw!=90;
  if(bRetailBalconyStep)
  {
   TestTrue(*(TEXT("Retail diagonal step reaches balcony ")+Case),MaxY>129 && Lowest<147 && Lowest>146.79);
  }
  else
  {
   TestTrue(*(TEXT("No unsupported roof-to-balcony walk-off ")+Case),Lowest>147.7);
   TestTrue(*(TEXT("Retail roof footprint ")+Case),MaxY<=128.50);
  }
  TestEqual(*(TEXT("No invisible fall/contact loss ")+Case),LostContact,0);
  TestTrue(*(TEXT("Visible and reported feet agree ")+Case),PositionError<1.);
  if(Seed.Y>128 && Yaw==90 && (Tracked || !Run || Dt<1.f/30))
   TestTrue(*(TEXT("Player approaches the rim before stopping ")+Case),MaxY>128.20);
  if(Yaw==90)
  {
   const double EdgeY=PC->PredictedPose.Location.Y;
   if(Tracked)VR->MoveStick=FVector2D(0,-1);
   else
   {
    PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));
    PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::X,IE_Pressed,1.f));
    PC->PlayerInput->ProcessInputStack({},Dt,false);
   }
   for(int32 Frame=0;Frame<FMath::CeilToInt(.3f/Dt);++Frame)Tick(Dt);
   TestTrue(*(TEXT("Can back away from the roof edge ")+Case),PC->PredictedPose.Location.Y<EdgeY-.1);
  }
 }
 // A deliberate jump is allowed to cross the same edge. The movement and
 // contact guard must not turn the roof into an invisible wall in the air.
 for(bool Tracked:{false,true})for(float Dt:{1.f/90,1.f/30})
 {
  Place(FVector(68.678311,128.1,148.005),Tracked,90,true);
  PC->bRunning=true;PC->bJumpCharging=true;PC->JumpChargeExtent=1;
  PC->ReleaseJump(1,0);
  TestTrue(TEXT("Intentional mansion jump launches"),PC->bJumpAirborne);
  bool Crossed=false;
  for(int32 Frame=0;Frame<FMath::CeilToInt(.2f/Dt);++Frame)
  {
   Tick(Dt);
   Crossed|=PC->PredictedPose.Location.Y>128.6 && PC->bJumpAirborne && !Session->bAutoPosContact;
  }
  TestTrue(TEXT("Intentional jump crosses the roof edge and reports airborne"),Crossed);
 }
 return !HasAnyErrors();
}
#endif
