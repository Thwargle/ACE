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
 for(const auto& OriginalPath:Paths)for(bool Reverse:{false,true})for(bool Tracked:{false,true})for(float Dt:{1.f/30,1.f/120})
 {
  const TPair<FVector,FVector> Path=Reverse?TPair<FVector,FVector>(OriginalPath.Value,OriginalPath.Key):OriginalPath;
  VR->bActive=Tracked;
  FACEPosition Pose;Pose.CellId=0xBED20001;Pose.SetLocationFromUnreal(FACEPosition::AceVectorToUnreal(Global+Path.Key,100),100);
  const FVector Direction=FACEPosition::AceVectorToUnreal((Path.Value-Path.Key).GetSafeNormal(),1);Pose.SetAceFacingFromUnrealDir2D(Direction);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bLocalPredicting=false;PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
  PC->JumpWorldAceVelocity=FVector::ZeroVector;PC->JumpAirborneSeconds=0;Session->SetLocalPosition(Pose);
  Pawn->SetActorLocationAndRotation(Pose.ToUnrealLocation(100)+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()),Pose.ToUnrealQuat());
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));PC->PlayerInput->ProcessInputStack({},Dt,false);
  const FVector Start=Pose.ToUnrealLocation(100);double Progress=0;int32 IndoorFrames=0,AirFrames=0;
  for(int Frame=0;Frame<FMath::CeilToInt(12.f/Dt)&&Progress<FVector::Distance(Path.Key,Path.Value)*100;++Frame)
  {
   ++GFrameCounter;Terrain->LastKnownCellId=PC->PredictedPose.CellId;
   Terrain->UpdateBuildingVisibility();if(!ACECellTransit::IsIndoorCell(Terrain->LastKnownCellId))Terrain->UpdateOutdoorEnvCollision();
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Capsule->GetScaledCapsuleHalfHeight()),Direction.Rotation());
   VR->MoveStick=FVector2D(0,1);PC->PlayerTick(Dt);
   IndoorFrames+=ACECellTransit::IsIndoorCell(PC->PredictedPose.CellId)?1:0;AirFrames+=PC->bJumpAirborne?1:0;
   Progress=FVector::DotProduct(PC->PredictedPose.ToUnrealLocation(100)-Start,Direction);
  }
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));PC->PlayerInput->ProcessInputStack({},Dt,false);
  VR->MoveStick=FVector2D::ZeroVector;
  const FVector End=PC->PredictedPose.ToUnrealLocation(100);
  const FString Case=FString::Printf(TEXT("Passage %s -> %s tracked=%d hz=%.0f"),*Path.Key.ToString(),*Path.Value.ToString(),Tracked,1.f/Dt);
  AddInfo(FString::Printf(TEXT("%s: travel=%.2fm indoor=%d air=%d"),*Case,Progress/100,IndoorFrames,AirFrames));
  TestTrue(*(Case+TEXT(" crosses completely")),Progress>=FVector::Distance(Path.Key,Path.Value)*100-20);
  TestTrue(*(Case+TEXT(" enters its interior cells")),IndoorFrames>0);
  TestEqual(*(Case+TEXT(" stays supported")),AirFrames,0);
  float TerrainZ=0;
  TestTrue(*(Case+TEXT(" has terrain support")),Dat->SampleOutdoorGroundZ(End.X,End.Y,100,TerrainZ));
  TestTrue(*(Case+TEXT(" stays on terrain beneath the bridge")),FMath::Abs(End.Z-TerrainZ)<2);
 }
 return true;
}
#endif
