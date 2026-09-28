#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ACEInputBindings.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "ACETerrainChunkActor.h"
#include "ACETerrainPresenterComponent.h"
#include "ACEHoverTooltipWidget.h"
#include "ACEBodySweep.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEntranceTransitionTest,"ACE.RetailParity.EntranceTransitions",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEEntranceTransitionTest::RunTest(const FString&)
{
 ON_SCOPE_EXIT { ACEInputBindings::Reload(); };
 TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/EntranceFixture.ini"));
 FConfigFile Config;Config.NoSave=false;Config.bCanSaveAllSections=true;
 GConfig->SetFile(GGameUserSettingsIni,&Config);ACEInputBindings::Reload();
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 // A nonzero clock lets GetPlayerViewPoint use the camera cache in this fixture.
 World->Tick(LEVELTICK_TimeOnly,.01f);
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 auto Session=Client->GetSession();
 auto* Host=World->SpawnActor<AActor>();auto* Terrain=NewObject<UACETerrainPresenterComponent>(Host);
 Host->AddInstanceComponent(Terrain);Terrain->Client=Client;Terrain->bHasKnownCell=true;
 const uint32 Block=0x7D640000;
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(125*192,100*192,0),100);
 FACEPosition Report;Report.CellId=0x7D640014;Report.Location=FVector(59.847656,78.339844,12);
 FACEDatLandblockInfo Info;if(!Dat->LoadLandblockInfo(Block,Info))return false;
 for(const auto& Building:Info.Buildings)Dat->GetOrBuildSetupMesh(Building.ModelId,100);
 Dat->GetOrBuildLandblockMesh(Block,100);
 auto* Land=World->SpawnActor<AACELandblockActor>();Land->SetSkipNonBuildingScenery(true);
 TestTrue(TEXT("Reported landblock loads"),Land->LoadLandblock(Block,100));Terrain->Spawned.Add(Block,Land);
 for(int I=0;I<1500 && !Land->IsSceneryComplete();++I)
 {++GFrameCounter;Land->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);FPlatformProcess::Sleep(.002f);}
 TestTrue(TEXT("Exterior shells ready before crossing"),Land->IsSceneryComplete());
 FVector Door=FVector::ZeroVector,Normal=FVector::ZeroVector;uint32 DoorCell=0;double Nearest=DBL_MAX;
 for(uint32 Index=0;Index<Info.NumCells;++Index)
 {
  const uint32 Id=Block|(0x100+Index);const auto* Mesh=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Mesh)return false;
  const FTransform Transform=Mesh->GetCellLocalToLandblock(100)*FTransform(Origin);
  for(int P=0;P<Mesh->CellPortals.Num();++P)
  {
   if(!Mesh->CellPortals[P].IsOutsidePortal() || !Mesh->PortalApertureLocalVerts.IsValidIndex(P))continue;
   const auto& Verts=Mesh->PortalApertureLocalVerts[P];if(Verts.Num()<3)continue;
   FVector Center=FVector::ZeroVector;double Bottom=DBL_MAX,Top=-DBL_MAX;
   for(const auto& V:Verts){const FVector W=Transform.TransformPosition(V);Center+=W;Bottom=FMath::Min(Bottom,W.Z);Top=FMath::Max(Top,W.Z);}
   Center/=Verts.Num();Center.Z=Bottom;
   const double Distance=FVector::Dist(Center,Report.ToUnrealLocation(100));
   if(Distance<800)AddInfo(FString::Printf(TEXT("Nearby aperture cell=%08X distance=%.1f bottom=%s height=%.1f"),Id,Distance,*(Center-Origin).ToString(),Top-Bottom));
   if(Top-Bottom>150 && Distance<Nearest)
   {Nearest=Distance;Door=Center;DoorCell=Id;Normal=Transform.TransformVectorNoScale(Mesh->PortalApertureLocalNormals[P]).GetSafeNormal2D();}
  }
  const auto Stabs=Mesh->StaticObjects;
  for(const auto& Stab:Stabs)Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
  auto* Room=World->SpawnActor<AACEEnvCellActor>();Room->LoadEnvCell(Id,Origin,100);
  Room->SetEnvCellCollisionActive(true);Room->EnsureStaticObjectsQueued();
  for(int I=0;I<200 && Room->HasPendingStaticObjects();++I)
  {++GFrameCounter;Room->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);}
  Terrain->SpawnedEnvCells.Add(Id,Room);
 }
 AddInfo(FString::Printf(TEXT("Reported entrance cell=%08X distance=%.1fcm center=%s normal=%s rooms=%d shells=%d"),
  DoorCell,Nearest,*(Door-Origin).ToString(),*Normal.ToString(),Terrain->SpawnedEnvCells.Num(),Land->BuildingShells.Num()));
 if(!TestTrue(TEXT("Exterior aperture near user report"),DoorCell!=0 && Nearest<1000))return false;

 // Camera PView can hide outside pixels while the feet still need land support.
 Land->SetOutdoorTerrainCollisionEnabled(true);Land->SetOutdoorTerrainHiddenInGame(true);
 TestTrue(TEXT("Hiding exterior pixels does not remove outdoor support"),Land->TerrainMesh->IsCollisionEnabled());
 Land->SetOutdoorTerrainHiddenInGame(false);
 TestTrue(TEXT("Showing exterior pixels preserves support"),Land->TerrainMesh->IsCollisionEnabled());
 Land->SetOutdoorTerrainCollisionEnabled(false);Land->SetOutdoorTerrainHiddenInGame(false);
 TestFalse(TEXT("Looking outside from indoors does not enable outdoor support"),Land->TerrainMesh->IsCollisionEnabled());
 auto* Chunk=World->SpawnActor<AACETerrainChunkActor>();
 Chunk->SetOutdoorTerrainCollisionEnabled(true);Chunk->SetOutdoorTerrainHiddenInGame(true);
 TestTrue(TEXT("Chunk PView cannot disable outdoor collision"),Chunk->TerrainMesh->IsCollisionEnabled());
 Chunk->SetOutdoorTerrainCollisionEnabled(false);Chunk->SetOutdoorTerrainHiddenInGame(false);
 TestFalse(TEXT("Chunk look-out cannot enable indoor terrain collision"),Chunk->TerrainMesh->IsCollisionEnabled());
 Chunk->Destroy();

 // Filters must update without destroying unrelated scenery's physics bodies.
 TArray<UPrimitiveComponent*> Shells;
 for(UProceduralMeshComponent* Mesh:Land->SceneryMeshes)if(Mesh && Mesh->BodyInstance.IsValidBodyInstance())Shells.Add(Mesh);
 for(const auto& Shell:Land->BuildingShells)
 {if(Shell.Mesh && Shell.Mesh->BodyInstance.IsValidBodyInstance())Shells.Add(Shell.Mesh);if(Shell.CollisionMesh && Shell.CollisionMesh->BodyInstance.IsValidBodyInstance())Shells.Add(Shell.CollisionMesh);}
 int32 Recreated=0;double MaxFilterMs=0;
 for(int Pass=0;Pass<6;++Pass)
 {
  TArray<FPhysicsActorHandle> Bodies;for(auto* Mesh:Shells)Bodies.Add(Mesh->BodyInstance.GetPhysicsActor());
  const double Start=FPlatformTime::Seconds();Land->SetBuildingShellsBlockPawn(Pass%2==0);
  MaxFilterMs=FMath::Max(MaxFilterMs,(FPlatformTime::Seconds()-Start)*1000);
  for(int I=0;I<Shells.Num();++I)Recreated+=Bodies[I]!=Shells[I]->BodyInstance.GetPhysicsActor();
 }
 AddInfo(FString::Printf(TEXT("Entrance filters: bodies=%d recreated=%d max=%.3fms"),Shells.Num(),Recreated,MaxFilterMs));
 TestTrue(TEXT("Test includes shell physics"),Shells.Num()>0);
 TestEqual(TEXT("Occupancy filter changes retain physics bodies"),Recreated,0);
 Land->SetBuildingShellsBlockPawn(true);
 FCollisionQueryParams ShellQuery(NAME_None,true);
 for(const auto& Pair:Terrain->SpawnedEnvCells)ShellQuery.AddIgnoredActor(Pair.Value);
 ShellQuery.AddIgnoredComponent(Land->TerrainMesh.Get());
 bool TestedFilter=false;
 for(auto* Component:Shells)
 {
  for(int Axis=0;Axis<2 && !TestedFilter;++Axis)
  {
   for(float Offset:{-.75f,-.25f,.25f,.75f})
   {
    FVector Center=Component->Bounds.Origin,N=FVector::ZeroVector;
    Center[1-Axis]+=Component->Bounds.BoxExtent[1-Axis]*Offset;
    N[Axis]=Component->Bounds.BoxExtent[Axis]+20;
    FHitResult Hit;
    if(!World->LineTraceSingleByChannel(Hit,Center+N,Center-N,ECC_Pawn,ShellQuery) || !Shells.Contains(Hit.GetComponent()))continue;
    Land->SetBuildingShellsBlockPawn(false);
    TestFalse(TEXT("Ignored shell is absent from Pawn scene queries"),World->LineTraceSingleByChannel(Hit,Center+N,Center-N,ECC_Pawn,ShellQuery));
    Land->SetBuildingShellsBlockPawn(true);
    TestTrue(TEXT("Restored shell immediately blocks Pawn scene queries"),World->LineTraceSingleByChannel(Hit,Center+N,Center-N,ECC_Pawn,ShellQuery));
    TestedFilter=true;break;
   }
  }
  if(TestedFilter)break;
 }
 TestTrue(TEXT("Actual building wall filter was queried"),TestedFilter);

 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,90.75);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
 PC->SetViewTarget(Pawn);
 PC->bRetailCursorInstalled=true;PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);PC->ApplyPlayerCapsuleFromSetup(Self.SetupId);
 const float Half=Capsule->GetScaledCapsuleHalfHeight(),Radius=Capsule->GetScaledCapsuleRadius();
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->bRun=true;VR->Settings->MovementDirection=0;
 PC->InputComponent->AxisBindings.Reset();Client->SetRunSkill(300);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(EntranceTransition),true,Pawn);
 TArray<FString> MotionRows;MotionRows.Add(TEXT("vr,run,dt,direction,frame,distance,z,ratio,predicted,occupied,refresh_ms"));
 for(bool Tracked:{false,true})for(bool Running:{false,true})for(float Dt:{1.f/90,1.f/20})for(float Direction:{1.f,-1.f})
 {
  Land->SetOutdoorTerrainCollisionEnabled(true);Land->SetBuildingShellsBlockPawn(true);
  for(const auto& Pair:Terrain->SpawnedEnvCells)Pair.Value->SetEnvCellCollisionActive(true);
  FVector Feet=Door-Normal*150*Direction;float Z;
  if(!TestTrue(TEXT("Starting entrance support exists"),ACEBodySweep::FindFootSupport(*World,Feet,Radius,400,Query,Z,40)))continue;
  AddInfo(FString::Printf(TEXT("Entrance start dir=%.0f near=%s ground=%.1f"),Direction,*(Feet-Origin).ToString(),Z));
  Feet.Z=Z;uint32 Cell=DoorCell;ACECellTransit::ResolveTransitCellId(*Dat,DoorCell,Feet+FVector(0,0,Half),Radius,100,Cell);
  FACEPosition Pose;Pose.CellId=Cell;Pose.SetLocationFromUnreal(Feet,100);Pose.SetAceFacingFromUnrealDir2D(Normal*Direction);
  Session->SetLocalPosition(Pose);PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bLocalPredicting=false;
  PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;PC->JumpWorldAceVelocity=FVector::ZeroVector;
  Pawn->SetActorLocationAndRotation(Feet+FVector(0,0,Half),Pose.ToUnrealQuat());VR->bActive=Tracked;
  VR->Settings->bRun=Running;
  Terrain->CommitOccupancyCellId(Cell);Terrain->ViewerCellId=Cell;Terrain->UpdateBuildingVisibility();
  if(!ACECellTransit::IsIndoorCell(Cell))Terrain->UpdateOutdoorEnvCollision();
  // The shop's ramp lies below the plaza heightfield. Place the initial feet
  // using the now-active cell collision, rather than the all-rooms setup query.
  if(TestTrue(TEXT("Occupied cell supplies initial foot support"),ACEBodySweep::FindFootSupport(*World,Feet,Radius,150,Query,Z,1)))
  {
   Feet.Z=Z;Pose.SetLocationFromUnreal(Feet,100);Session->SetLocalPosition(Pose);PC->PredictedPose=Pose;
   Pawn->SetActorLocation(Feet+FVector(0,0,Half));
  }
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));PC->PlayerInput->ProcessInputStack({},Dt,false);
  if(!Running){PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift,IE_Pressed,1.f));PC->PlayerInput->ProcessInputStack({},Dt,false);}
  float MinimumRatio=1,MaxZStep=0,StreamTime=0;int Samples=0;double MaxRefreshMs=0;
  for(int Frame=0;Frame<FMath::CeilToInt(4/Dt) && FVector::DotProduct(Pawn->GetActorLocation()-Door,Normal)*Direction<300;++Frame)
  {
   const FVector Before=Pawn->GetActorLocation();
   VR->Head->SetWorldLocationAndRotation(Before+FVector(0,0,175-Half),(Normal*Direction).Rotation());VR->MoveStick=FVector2D(0,1);
   PC->PlayerTick(Dt);
   if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(Dt);
   const FVector After=Pawn->GetActorLocation();
   const float Ratio=FVector::DotProduct(After-Before,Normal)*Direction/(Client->GetLocomotionSpeed(Running)*100*Dt);
   if(FMath::Abs(FVector::DotProduct(Before-Door,Normal))<150){++Samples;MinimumRatio=FMath::Min(MinimumRatio,Ratio);MaxZStep=FMath::Max(MaxZStep,float(FMath::Abs(After.Z-Before.Z)));}
   const double Start=FPlatformTime::Seconds();
   StreamTime+=Dt;
   if(StreamTime>=.1f)
   {
    StreamTime=0;
    if(!ACECellTransit::IsIndoorCell(Terrain->LastKnownCellId))Terrain->UpdateOutdoorEnvCollision();
   }
   // Exercise the production post-movement callback: cell handoff cannot wait
   // for either the streaming timer or a network position update.
   Terrain->UpdateCameraVisibility();
   TestEqual(TEXT("Collision residency follows the accepted movement cell immediately"),Terrain->LastKnownCellId,uint32(PC->PredictedPose.CellId));
   const double RefreshMs=(FPlatformTime::Seconds()-Start)*1000;MaxRefreshMs=FMath::Max(MaxRefreshMs,RefreshMs);
   MotionRows.Add(FString::Printf(TEXT("%d,%d,%.4f,%.0f,%d,%.3f,%.3f,%.4f,%08X,%08X,%.3f"),Tracked,Running,Dt,Direction,Frame,
    FVector::DotProduct(After-Door,Normal)*Direction,After.Z-Half,Ratio,PC->PredictedPose.CellId,Terrain->LastKnownCellId,RefreshMs));
  }
  const bool Crossed=FVector::DotProduct(Pawn->GetActorLocation()-Door,Normal)*Direction>=140;
  AddInfo(FString::Printf(TEXT("Entrance VR=%d run=%d dt=%.4f dir=%.0f samples=%d minRatio=%.4f zStep=%.3f crossed=%d refreshMax=%.3fms cell=%08X held=%08X"),
   Tracked,Running,Dt,Direction,Samples,MinimumRatio,MaxZStep,Crossed,MaxRefreshMs,PC->PredictedPose.CellId,Terrain->LastKnownCellId));
  TestTrue(TEXT("Doorway crossing keeps requested motion"),Crossed && Samples>0 && MinimumRatio>.98f);
  TestTrue(TEXT("Doorway ramp has no residency-induced vertical snap"),MaxZStep<=FMath::Max(4.f,Client->GetLocomotionSpeed(Running)*100*Dt));
  TestFalse(TEXT("Doorway never starts a fall"),PC->bJumpAirborne);
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));PC->PlayerInput->ProcessInputStack({},Dt,false);
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift,IE_Released,0.f));PC->PlayerInput->ProcessInputStack({},Dt,false);
 }
 FFileHelper::SaveStringArrayToFile(MotionRows,*(FPaths::ProjectSavedDir()/TEXT("EntranceMotion.csv")));
 // Reported window: 7D640149 is a thin entry cell leading to 7D64014C.
 // Start an airborne body just outside its authored aperture. Losing the entry
 // cell until landing strands the actor outdoors beyond its transit sphere.
 if(!PC->PlayerCameraManager)
 {
  PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
  PC->PlayerCameraManager->InitializeFor(PC);
  PC->SetViewTarget(Pawn);
 }
 TestNotNull(TEXT("Window visibility fixture has a desktop camera"),PC->PlayerCameraManager.Get());
 for(bool Tracked:{false,true})for(float Dt:{1.f/90,1.f/20})for(bool ColdCollision:{false,true})
 {
  if(ColdCollision)for(uint32 Id:{0x7D640149u,0x7D64014Cu})
  {
   auto* Room=Terrain->SpawnedEnvCells.FindChecked(Id).Get();
   TestTrue(TEXT("Window room reloads with deferred collision"),Room->LoadEnvCell(Id,Origin,100));
   TestFalse(TEXT("Cold window room begins without cooked collision"),Room->IsCollisionCooked());
  }
  const FVector Feet=Origin+FVector(-8962.6953,4110.5469,1335);
  FACEPosition Pose;Pose.CellId=0x7D64001A;Pose.SetLocationFromUnreal(Feet,100);
  Pose.SetAceFacingFromUnrealDir2D(FVector(0,-1,0));Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bLocalPredicting=false;
  PC->bJumpAirborne=true;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
  PC->JumpWorldAceVelocity=FVector(0,-10,0);PC->JumpAirborneSeconds=.3f;
  Pawn->SetActorLocationAndRotation(Feet+FVector(0,0,Half),Pose.ToUnrealQuat());VR->bActive=Tracked;
  VR->MoveStick=FVector2D::ZeroVector;
  Terrain->CommitOccupancyCellId(Pose.CellId);Terrain->UpdateBuildingVisibility();Terrain->UpdateOutdoorEnvCollision();
  bool EnteredInAir=false;float MinimumFeetZ=Feet.Z;int32 AirFrames=0;
  for(int Frame=0;Frame<FMath::CeilToInt(1.5f/Dt);++Frame)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),FRotator(0,-90,0));
   PC->PlayerTick(Dt);
   // This headless fixture has no viewport to update the desktop camera.
   if(PC->PlayerCameraManager)
   {
    PC->PlayerCameraManager->UpdateCamera(Dt);
    FMinimalViewInfo View;View.Location=Pawn->GetActorLocation()+FVector(0,0,175-Half);
    View.Rotation=FRotator(0,-90,0);View.FOV=90;
    PC->PlayerCameraManager->SetCameraCachePOV(View);
   }
   Terrain->UpdateCameraVisibility();
   if(!ACECellTransit::IsIndoorCell(Terrain->LastKnownCellId))Terrain->UpdateOutdoorEnvCollision();
   EnteredInAir|=PC->bJumpAirborne && ACECellTransit::IsIndoorCell(PC->PredictedPose.CellId);
   AirFrames+=PC->bJumpAirborne;
   MinimumFeetZ=FMath::Min(MinimumFeetZ,float(Pawn->GetActorLocation().Z-Half));
  }
  const FVector Final=Pawn->GetActorLocation();
  AddInfo(FString::Printf(TEXT("Window VR=%d dt=%.4f cold=%d airFrames=%d enteredInAir=%d cell=%08X viewer=%08X feet=%s minZ=%.3f"),
   Tracked,Dt,ColdCollision,AirFrames,EnteredInAir,PC->PredictedPose.CellId,Terrain->ViewerCellId,*(Final-Origin-FVector(0,0,Half)).ToString(),MinimumFeetZ));
  TestTrue(TEXT("Window transit is accepted while airborne"),EnteredInAir);
  TestTrue(TEXT("Jump passes through the window into the main room"),Final.Y-Origin.Y<3950);
  TestEqual(TEXT("Window landing retains its indoor cell"),uint32(PC->PredictedPose.CellId),0x7D64014Cu);
  TestFalse(TEXT("Window jump lands normally"),PC->bJumpAirborne);
  TestTrue(TEXT("Window landing retains floor support"),MinimumFeetZ>1100);
  TestTrue(TEXT("Landing room collision is cooked"),Terrain->SpawnedEnvCells.FindChecked(0x7D64014C)->IsCollisionCooked());
  TestEqual(TEXT("Interior camera resolves from the accepted player cell"),Terrain->ViewerCellId,0x7D64014Cu);
  TestFalse(TEXT("Window entry room remains visible after landing"),Terrain->SpawnedEnvCells.FindChecked(0x7D64014C)->IsHidden());
 }
 Session->State=EACESessionState::Disconnected;Session->PlayerGuid=0;
 return true;
}
#endif
