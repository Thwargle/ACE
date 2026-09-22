#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../ACEBodySweep.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "Dat/ACECellTransit.h"
#include "ACEHoverTooltipWidget.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRInteriorTest,"ACE.VR.InteriorMovement",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEVRInteriorTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
 auto Session=Client->GetSession();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(200*192,140*192,0),100);
 TArray<TPair<uint32,FVector>> Candidates;
 for(uint32 Id=0xC88C0143;Id<=0xC88C014A;++Id)
 {
  const auto* Built=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Built)continue;
  const FTransform Frame=Built->GetCellLocalToLandblock(100);
  for(const auto& Section:Built->CollisionSections)
   for(int32 I=0;I+2<Section.Triangles.Num();I+=3)
   {
    const FVector A=Frame.TransformPosition(Section.Vertices[Section.Triangles[I]]);
    const FVector B=Frame.TransformPosition(Section.Vertices[Section.Triangles[I+1]]);
    const FVector C=Frame.TransformPosition(Section.Vertices[Section.Triangles[I+2]]);
    if(FMath::Abs(FVector::CrossProduct(B-A,C-A).GetSafeNormal().Z)>.99f && FMath::Abs(A.Z-2200)<1)
    {
     const FVector Point=Origin+(A+B+C)/3;
     if(!Candidates.ContainsByPredicate([&](const auto& P){return P.Value.Equals(Point,1.f);}))Candidates.Emplace(Id,Point);
    }
   }
  for(const auto& Stab:Built->StaticObjects)Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
  auto* Room=World->SpawnActor<AACEEnvCellActor>();Room->LoadEnvCell(Id,Origin,100);
  Room->SetEnvCellCollisionActive(true);Room->EnsureStaticObjectsQueued();
  for(int32 I=0;I<200 && Room->HasPendingStaticObjects();++I)
  { ++GFrameCounter;Room->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); }
 }
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
	PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,88);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
 PC->bRetailCursorInstalled=true;PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);PC->ApplyPlayerCapsuleFromSetup(Self.SetupId);
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->bRun=true;
 Client->SetRunSkill(600);
 PC->InputComponent->AxisBindings.Reset(); // Inject tracked-stick samples below, without hardware zeroing them.
 const float Half=Capsule->GetScaledCapsuleHalfHeight(), Radius=Capsule->GetScaledCapsuleRadius();
 const auto Shape=FCollisionShape::MakeCapsule(Radius,Half);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(VRInteriorRegression),true,Pawn);
 // Wall slides can carry a path out through a real doorway. Include its
 // exterior support before testing; an isolated room otherwise falls into void.
 TArray<AACELandblockActor*> Lands;
 for(uint32 Block:{0xC78C0000u,0xC88C0000u,0x7D640000u})
 {
  Dat->GetOrBuildLandblockMesh(Block,100);
  auto* Land=World->SpawnActor<AACELandblockActor>();TestTrue(TEXT("Doorway exterior land loads"),Land->LoadLandblock(Block,100));
  Lands.Add(Land);
  for(int32 I=0;I<1500 && !Land->IsSceneryComplete();++I)
  { ++GFrameCounter;Land->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);FPlatformProcess::Sleep(.002f); }
  TestTrue(TEXT("Doorway exterior scenery is ready"),Land->IsSceneryComplete());
 }
 int32 Paths=0;
 for(const auto& Candidate:Candidates)
 {
  float Support; FHitResult Initial;
  const FVector Feet=Candidate.Value;
  if(!ACEBodySweep::FindFootSupport(*World,Feet,Radius,3,Query,Support,1))continue;
  if(ACEBodySweep::Sweep(*World,Initial,Feet+FVector(0,0,Half),Feet+FVector(.1,0,Half),Shape,Query) && Initial.bStartPenetrating)continue;
  for(float Dt:{1.f/90,1.f/20})for(int32 Heading=0;Heading<4;++Heading)
  {
   FACEPosition Pose;Pose.CellId=Candidate.Key;Pose.SetLocationFromUnreal(Feet,100);
   Pose.SetAceFacingFromUnrealDir2D(FRotator(0,Heading*90,0).Vector());Session->SetLocalPosition(Pose);
   PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;
   Pawn->SetActorLocationAndRotation(Feet+FVector(0,0,Half),Pose.ToUnrealQuat());
   const FVector Start=Pawn->GetActorLocation();FVector AtWall=Start;
   auto Tick=[&](float Axis)
   {
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half),FRotator(0,Heading*90,0));
    VR->MoveStick=FVector2D(0,Axis);PC->PlayerTick(Dt);
   };
   for(int32 I=0;I<FMath::CeilToInt(.35f/Dt);++I)Tick(1);
   AtWall=Pawn->GetActorLocation();
   for(int32 I=0;I<FMath::CeilToInt(.35f/Dt);++I)Tick(-1);
   const FVector End=Pawn->GetActorLocation();
   AddInfo(FString::Printf(TEXT("cell=%08X dt=%.3f heading=%d start=%s wall=%s end=%s airborne=%d"),Candidate.Key,Dt,Heading,*(Start-Origin).ToString(),*(AtWall-Origin).ToString(),*(End-Origin).ToString(),PC->bJumpAirborne));
   TestTrue(TEXT("Backing away from an interior wall does not stay locked"),FVector::Dist2D(AtWall,End)>10.f);
   // A path can end while falling off furniture. Let gravity settle before
   // checking support; requiring contact mid-flight rejects valid movement.
   for(int32 I=0;I<FMath::CeilToInt(1.f/Dt);++I)Tick(0);
   const FVector Settled=Pawn->GetActorLocation();
   float EndSupport=0;
   const bool bSupported=ACEBodySweep::FindFootSupport(*World,Settled-FVector(0,0,Half),Radius,5,Query,EndSupport,1)
      && FMath::Abs(Settled.Z-Half-EndSupport)<2.f;
   if(!bSupported)
   {
    FHitResult SupportHit;
    World->SweepSingleByChannel(SupportHit,Settled,Settled-FVector(0,0,10),FQuat::Identity,ECC_Pawn,Shape,Query);
    AddInfo(FString::Printf(TEXT("Unsupported settled=%s airborne=%d contact=%s normal=%s depth=%.3f"),
      *(Settled-Origin).ToString(),PC->bJumpAirborne,*GetNameSafe(SupportHit.GetComponent()),*SupportHit.Normal.ToString(),SupportHit.PenetrationDepth));
   }
   TestTrue(TEXT("Walking inside keeps the lower sphere supported, including doorway edges"),
    bSupported);
   ++Paths;
  }
  if(Paths>=48)break;
 }
 TestTrue(TEXT("Actual reported interior supplies collision test paths"),Paths>=8);
 const FVector YaraqOrigin=FACEPosition::AceVectorToUnreal(FVector(125*192,100*192,0),100);
 for(uint32 Id=0x7D640100;Id<=0x7D640114;++Id)
 {
  if(!Dat->GetOrBuildEnvCellMesh(Id,100))continue;
  auto* Room=World->SpawnActor<AACEEnvCellActor>();Room->LoadEnvCell(Id,YaraqOrigin,100);Room->SetEnvCellCollisionActive(true);
 }
 int32 DoorPaths=0;
 for(uint32 Id:{0xC88C0145u,0xC88C0146u,0x7D640101u,0x7D64010Cu})
 {
  const auto* Mesh=Dat->FindEnvCellMesh(Id,100);if(!Mesh)continue;
  const FVector DoorOrigin=(Id>>24)==0x7D ? YaraqOrigin : Origin;
  const FTransform Transform=Mesh->GetCellLocalToLandblock(100)*FTransform(DoorOrigin);
  for(int32 Portal=0;Portal<Mesh->CellPortals.Num();++Portal)
  {
   if(!Mesh->CellPortals[Portal].IsOutsidePortal())continue;
   const auto& Vertices=Mesh->PortalApertureLocalVerts[Portal];if(Vertices.Num()<3)continue;
   FVector Door=FVector::ZeroVector;float Bottom=MAX_flt,Top=-MAX_flt;
   for(const auto& V:Vertices){const FVector W=Transform.TransformPosition(V);Door+=W;Bottom=FMath::Min(Bottom,float(W.Z));Top=FMath::Max(Top,float(W.Z));}
   Door/=Vertices.Num();Door.Z=Bottom;
   if(Top-Bottom<Half*2)continue;
   FVector Normal=Transform.TransformVectorNoScale(Mesh->PortalApertureLocalNormals[Portal]).GetSafeNormal2D();
   if(Normal.IsNearlyZero())continue;
   AddInfo(FString::Printf(TEXT("Door geometry cell=%08X at=%s normal=%s height=%.2f"),Id,*(Door-DoorOrigin).ToString(),*Normal.ToString(),Top-Bottom));
   for(float Dt:{1.f/90,1.f/20})for(float Direction:{1.f,-1.f})
   {
    for(auto* Land:Lands)Land->SetOutdoorTerrainCollisionEnabled(true);
    FVector Feet=Door-Normal*150*Direction;float Z;
    if(!ACEBodySweep::FindFootSupport(*World,Feet,Radius,60,Query,Z,40))continue;Feet.Z=Z;
    uint32 Cell=Id;ACECellTransit::ResolveTransitCellId(*Dat,Id,Feet+FVector(0,0,Half),Radius,100,Cell);
    FACEPosition Pose;Pose.CellId=Cell;Pose.SetLocationFromUnreal(Feet,100);Pose.SetAceFacingFromUnrealDir2D(Normal*Direction);
    Session->SetLocalPosition(Pose);PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;
    Pawn->SetActorLocationAndRotation(Feet+FVector(0,0,Half),Pose.ToUnrealQuat());
    float MinRatio=1.f;int32 Samples=0;
    for(int32 I=0;I<80 && FVector::DotProduct(Pawn->GetActorLocation()-Door,Normal)*Direction<100;++I)
    {
     const FVector Before=Pawn->GetActorLocation();
     for(auto* Land:Lands)Land->SetOutdoorTerrainCollisionEnabled(!ACECellTransit::IsIndoorCell(PC->PredictedPose.CellId));
     VR->Head->SetWorldLocationAndRotation(Before+FVector(0,0,175-Half),(Normal*Direction).Rotation());VR->MoveStick=FVector2D(0,1);PC->PlayerTick(Dt);
     const FVector After=Pawn->GetActorLocation();
     const float Ratio=FVector::DotProduct(After-Before,Normal)*Direction/(Client->GetLocomotionSpeed(true)*100*Dt);
     if(FMath::Abs(FVector::DotProduct(Before-Door,Normal))<75)
     {
      ++Samples;
      if(Ratio<.75f && Ratio<MinRatio)
      {
       FHitResult H;ACEBodySweep::Sweep(*World,H,Before,Before+Normal*Direction*Client->GetLocomotionSpeed(true)*100*Dt,Shape,Query);
       AddInfo(FString::Printf(TEXT("Retail doorway bump cell=%08X portal=%d dt=%.3f dir=%.0f before=%s after=%s ratio=%.3f resident=%08X air=%d hit=%s/%s n=%s time=%.3f depth=%.3f"),Id,Portal,Dt,Direction,*(Before-Origin).ToString(),*(After-Origin).ToString(),Ratio,PC->PredictedPose.CellId,PC->bJumpAirborne,*GetNameSafe(H.GetActor()),*GetNameSafe(H.GetComponent()),*H.Normal.ToString(),H.Time,H.PenetrationDepth));
      }
      MinRatio=FMath::Min(MinRatio,Ratio);
     }
    }
    AddInfo(FString::Printf(TEXT("Door motion cell=%08X dt=%.3f direction=%.0f samples=%d minimumRatio=%.4f"),Id,Dt,Direction,Samples,MinRatio));
    TestTrue(FString::Printf(TEXT("Retail doorway %08X direction %.0f dt %.3f preserves movement"),Id,Direction,Dt),Samples>0 && MinRatio>.98f);++DoorPaths;
   }
  }
 }
 TestTrue(TEXT("Actual exterior doorways were traversed"),DoorPaths>0);
 // Reproduce the shallow wall overlap reported by Chaos at large world coordinates.
 auto Box=[&](FVector Center,FVector Extent)
 {
  auto* A=World->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);
  B->SetBoxExtent(Extent);B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);B->SetCollisionResponseToAllChannels(ECR_Block);
  B->RegisterComponent();A->SetActorLocation(Center);
 };
 const FVector Far=Origin+FVector(-50000,0,5000);
 Box(Far-FVector(0,0,10),FVector(300,300,10));Box(Far+FVector(100,0,150),FVector(1,300,150));
 const FVector Contact=Far+FVector(99-Radius+.01f,0,Half);
 FHitResult Hit;
 TestFalse(TEXT("Numerical wall contact allows moving away without dropping the floor"),ACEBodySweep::Sweep(*World,Hit,Contact,Contact-FVector(25,0,0),Shape,Query));
 float Floor=0;
 TestTrue(TEXT("A touching wall cannot hide foot support"),ACEBodySweep::FindFootSupport(*World,Contact-FVector(0,0,Half),Radius,3,Query,Floor,1));
 // Adjacent floor faces model a small doorway sill. Measure each requested
 // step, not only the eventual endpoint: an eventual crossing can conceal a
 // one-frame stop that is conspicuous in a headset.
 for(float Height:{0.f,2.f,6.f,12.f})
 {
  const FVector Door=Far+FVector(0,2000+Height*100,0);
  Box(Door+FVector(-200,0,-10),FVector(200,200,10));
  Box(Door+FVector(200,0,Height-10),FVector(200,200,10));
  Box(Door+FVector(0,Radius+30,100),FVector(8,15,100));
  Box(Door+FVector(0,-Radius-30,100),FVector(8,15,100));
  for(float Dt:{1.f/90,1.f/20})for(float Direction:{1.f,-1.f})
  {
   const FVector Feet=Door+FVector(-150*Direction,0,Direction>0?0:Height);
   FACEPosition Pose;Pose.CellId=0xC88C0001;Pose.SetLocationFromUnreal(Feet,100);Pose.NormalizeOutdoorLandblock();
   const FRotator Facing(0,Direction>0?0:180,0);Pose.SetAceFacingFromUnrealDir2D(Facing.Vector());Session->SetLocalPosition(Pose);
   PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;
   Pawn->SetActorLocationAndRotation(Feet+FVector(0,0,Half),Pose.ToUnrealQuat());
   float MinRatio=1.f;
   for(int32 I=0;I<150 && (Pawn->GetActorLocation().X-Door.X)*Direction<100;++I)
   {
    const FVector Before=Pawn->GetActorLocation();
    VR->Head->SetWorldLocationAndRotation(Before+FVector(0,0,175-Half),Facing);
    VR->MoveStick=FVector2D(0,1);PC->PlayerTick(Dt);
    const FVector After=Pawn->GetActorLocation();
    const float Expected=Client->GetLocomotionSpeed(true)*100*Dt;
    const float Ratio=(After.X-Before.X)*Direction/Expected;
    if(FMath::Abs(Before.X-Door.X)<75)
    {
     MinRatio=FMath::Min(MinRatio,Ratio);
     if(Ratio<.75f)AddInfo(FString::Printf(TEXT("Door bump sill=%.1f dt=%.3f dir=%.0f before=%s after=%s ratio=%.3f airborne=%d"),Height,Dt,Direction,*(Before-Door).ToString(),*(After-Door).ToString(),Ratio,PC->bJumpAirborne));
    }
   }
   TestTrue(FString::Printf(TEXT("Door sill %.0f cm direction %.0f dt %.3f preserves lateral input"),Height,Direction,Dt),MinRatio>.75f);
  }
 }
 Session->State=EACESessionState::Disconnected;Session->PlayerGuid=0;
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
