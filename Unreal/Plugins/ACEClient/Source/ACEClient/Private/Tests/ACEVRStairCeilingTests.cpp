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
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRStairCeilingTest,"ACE.VR.StairCeiling",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEVRStairCeilingTest::RunTest(const FString&)
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
 TArray<AACEEnvCellActor*> Rooms;
 for(uint32 Id=0xC98C0106;Id<=0xC98C0115;++Id)
 {
  const auto* Built=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Built)continue;
  const auto Stabs=Built->StaticObjects;
  for(const auto& Stab:Stabs)Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
  auto* Room=World->SpawnActor<AACEEnvCellActor>();Room->LoadEnvCell(Id,Origin,100);
  Room->SetEnvCellCollisionActive(true);Room->EnsureStaticObjectsQueued();
  for(int32 I=0;I<200 && Room->HasPendingStaticObjects();++I)
  {++GFrameCounter;Room->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);}
  TestFalse(TEXT("Stair room furniture collision finished loading"),Room->HasPendingStaticObjects());
  Rooms.Add(Room);
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
 Client->SetRunSkill(600);PC->InputComponent->AxisBindings.Reset();
 const FVector Feet=Origin+FVector(-11490.6,17450.6,2298.46);
 for(float Yaw:{0.f,45.f,90.f,135.f,180.f,225.f,270.f,315.f})
 {
  FACEPosition Pose;Pose.CellId=0xC98C0109;Pose.SetLocationFromUnreal(Feet,100);
  Pose.SetAceFacingFromUnrealDir2D(FRotator(0,Yaw,0).Vector());Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;PC->bLocalPredicting=false;
  Pawn->SetActorLocationAndRotation(Feet+FVector(0,0,90.75),Pose.ToUnrealQuat());
  int32 AirStallFrames=0,LongestAirStall=0;
  for(int32 I=0;I<30;++I)
  {
   const FVector Before=Pawn->GetActorLocation();
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-90.75),FRotator(0,Yaw,0));
   VR->MoveStick=FVector2D(0,1);PC->PlayerTick(1.f/30);
   AirStallFrames=PC->bJumpAirborne && Pawn->GetActorLocation().Equals(Before,.1f) ? AirStallFrames+1 : 0;
   LongestAirStall=FMath::Max(LongestAirStall,AirStallFrames);
   if(I%10==0)
   {
    FCollisionQueryParams Query(SCENE_QUERY_STAT(StairCeilingContact),true,Pawn);
    FHitResult Hit;const auto Shape=FCollisionShape::MakeCapsule(48,90.75);
    const FVector Center=Pawn->GetActorLocation();
    World->SweepSingleByChannel(Hit,Center,Center-FVector(0,0,10),FQuat::Identity,ECC_Pawn,Shape,Query);
    AddInfo(FString::Printf(TEXT("Stair yaw=%.0f frame=%d delta=%s air=%d hit=%d pen=%.4f normal=%s impact=%s comp=%s"),Yaw,I,*(Center-Feet-FVector(0,0,90.75)).ToString(),PC->bJumpAirborne,Hit.bStartPenetrating,Hit.PenetrationDepth,*Hit.Normal.ToString(),*Hit.ImpactNormal.ToString(),*GetNameSafe(Hit.GetComponent())));
   }
  }
  // Walking off the side can briefly fall. It must continue integrating and
  // land, rather than freezing all inputs at the same unsupported position.
  TestTrue(TEXT("Ceiling contact cannot trap an immobile airborne body"),LongestAirStall<6);
  if(Yaw==270.f)
  {
   const FVector Pressed=Pawn->GetActorLocation();
   FHitResult FootHit;
   FCollisionQueryParams FootQuery(SCENE_QUERY_STAT(StairFootOverlap),true,Pawn);
   const FVector FootCenter=Pressed-FVector(0,0,90.75-48);
   World->SweepSingleByChannel(FootHit,FootCenter,FootCenter-FVector(0,0,.1f),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(48),FootQuery);
   TestTrue(TEXT("Blocked crown does not leave the foot embedded in the ramp"),!FootHit.bStartPenetrating || FootHit.PenetrationDepth<.5f);
   for(int32 I=0;I<20;++I)
   {
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-90.75),FRotator(0,Yaw,0));
    VR->MoveStick=FVector2D(0,-1);PC->PlayerTick(1.f/30);
   }
   TestTrue(TEXT("Backing down releases the capsule from a low stairwell ceiling"),Pawn->GetActorLocation().Y>Pressed.Y+50);
   TestFalse(TEXT("Backing down the stairs remains responsive"),PC->bJumpAirborne);
  }
 }
 // The captured position is beside the opening's edge. Exercise the actual
 // route through its center as well as escape from contact with that edge.
 for(float Dt:{1.f/90,1.f/15})
 {
  const FVector StairFeet=Origin+FVector(-11550,17600,2200);
  FACEPosition Pose;Pose.CellId=0xC98C0109;Pose.SetLocationFromUnreal(StairFeet,100);
  Pose.SetAceFacingFromUnrealDir2D(FVector(0,-1,0));Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;PC->bLocalPredicting=false;
  Pawn->SetActorLocationAndRotation(StairFeet+FVector(0,0,90.75),Pose.ToUnrealQuat());
  for(int32 I=0;I<FMath::CeilToInt(1.3f/Dt);++I)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-90.75),FRotator(0,-90,0));
   VR->MoveStick=FVector2D(0,1);PC->PlayerTick(Dt);
   if(I%10==0)AddInfo(FString::Printf(TEXT("Climb dt=%.4f frame=%d feet=%s air=%d"),Dt,I,*(Pawn->GetActorLocation()-Origin-FVector(0,0,90.75)).ToString(),PC->bJumpAirborne));
   if(Pawn->GetActorLocation().Z>2500+90 && Pawn->GetActorLocation().Y<Origin.Y+17190)break;
  }
  TestTrue(TEXT("Center of reported stairwell reaches the upper floor"),Pawn->GetActorLocation().Z>2500+90);
  for(int32 I=0;I<FMath::CeilToInt(1.3f/Dt);++I)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-90.75),FRotator(0,-90,0));
   VR->MoveStick=FVector2D(0,-1);PC->PlayerTick(Dt);
   if(I%10==0)AddInfo(FString::Printf(TEXT("Descend dt=%.4f frame=%d feet=%s air=%d"),Dt,I,*(Pawn->GetActorLocation()-Origin-FVector(0,0,90.75)).ToString(),PC->bJumpAirborne));
   if(Pawn->GetActorLocation().Z<2202+90.75 && Pawn->GetActorLocation().Y>Origin.Y+17580)break;
  }
  TestTrue(TEXT("Reported stairwell can also be descended"),Pawn->GetActorLocation().Z<2202+90.75);
  TestFalse(TEXT("Descent returns to grounded movement"),PC->bJumpAirborne);
 }
 // Actual DAT slopes around Samsur 4.4 S, 18.2 E. Exercise the complete
 // controller, not only a shape helper, because final ground snapping can
 // undo a correct swept move on the same frame.
 {
  const FVector Samsur=FACEPosition::AceVectorToUnreal(FVector(28836,23412,0),100);
  TArray<FVector> Slopes;
  TMap<FVector,FVector> SlopeNormals;
  for(int32 X=149;X<=151;++X) for(int32 Y=120;Y<=122;++Y)
  {
   const uint32 Id=(uint32(X)<<24)|(uint32(Y)<<16);
   const auto* Built=Dat->GetOrBuildLandblockMesh(Id,100); if(!Built) continue;
   auto* Terrain=World->SpawnActor<AActor>();auto* Mesh=NewObject<UProceduralMeshComponent>(Terrain);
   Terrain->AddInstanceComponent(Mesh);Terrain->SetRootComponent(Mesh);
   Mesh->bUseAsyncCooking=false;Mesh->bUseComplexAsSimpleCollision=true;
   Mesh->ComponentTags.Add(TEXT("ACEOutdoorTerrain")); Mesh->RegisterComponent();
   Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Mesh->SetCollisionResponseToAllChannels(ECR_Block);
   const FVector Base=FACEPosition::AceVectorToUnreal(FVector(X*192,Y*192,0),100);Terrain->SetActorLocation(Base);
   int32 SectionIndex=0;
   for(const auto& Section:Built->Sections)
   {
    Mesh->CreateMeshSection(SectionIndex++,Section.Vertices,Section.Triangles,Section.Normals,Section.UVs,{},{},true);
    for(int32 I=0;I+2<Section.Triangles.Num();I+=3)
    {
     const FVector A=Section.Vertices[Section.Triangles[I]],B=Section.Vertices[Section.Triangles[I+1]],C=Section.Vertices[Section.Triangles[I+2]];
     const FVector Center=(A+B+C)/3+Base;
     FVector Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();if(Normal.Z<0)Normal=-Normal;
     const double NZ=Normal.Z;
     if(NZ>.03 && NZ<.6641741 && FVector::DistSquared2D(Center,Samsur)<FMath::Square(8000.)) {Slopes.Add(Center);SlopeNormals.Add(Center,Normal);}
    }
   }
  }
  Slopes.Sort([&](const FVector& A,const FVector& B){return SlopeNormals[A].Z<SlopeNormals[B].Z;});
  TestTrue(TEXT("Reported Samsur area contains steep DAT faces"),!Slopes.IsEmpty());
  double WorstClearance=10000, WorstOverlap=0; int32 Samples=0;
  for(int32 S=0;S<FMath::Min(12,Slopes.Num());++S) for(float Dt:{1.f/90,1.f/20}) for(float Turn:{0.f,90.f,180.f})
  {
   const float Yaw=SlopeNormals[Slopes[S]].Rotation().Yaw+Turn;
   FACEPosition Pose;Pose.CellId=0x96790001;Pose.SetLocationFromUnreal(Slopes[S],100);Pose.NormalizeOutdoorLandblock();
   Pose.SetAceFacingFromUnrealDir2D(FRotator(0,Yaw,0).Vector());Session->SetLocalPosition(Pose);
   PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;PC->bLocalPredicting=false;
   Pawn->SetActorLocationAndRotation(Slopes[S]+FVector(0,0,90.75),Pose.ToUnrealQuat());
   for(int32 I=0;I<FMath::CeilToInt(2.f/Dt);++I)
   {
    VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-90.75),FRotator(0,Yaw,0));
    VR->MoveStick=FVector2D(0,1);PC->PlayerTick(Dt);
    float Z;const FVector At=Pawn->GetActorLocation();
    if(Dat->SampleOutdoorGroundZ(At.X,At.Y,100,Z)) { WorstClearance=FMath::Min(WorstClearance,At.Z-90.75-Z);++Samples; }
    if(I>2)
    {
     FHitResult Pen;FCollisionQueryParams Q(SCENE_QUERY_STAT(SamsurBody),true,Pawn);
     if(World->SweepSingleByChannel(Pen,At,At+FVector(0,0,.01),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(48,90.75),Q) && Pen.bStartPenetrating)
      WorstOverlap=FMath::Max(WorstOverlap,double(Pen.PenetrationDepth));
    }
   }
  }
  AddInfo(FString::Printf(TEXT("Samsur steep slopes: %d frames, minimum feet clearance %.3f cm"),Samples,WorstClearance));
  TestTrue(TEXT("Moving on Samsur inclines never puts the feet below terrain"),Samples>100 && WorstClearance>=-2.);
  AddInfo(FString::Printf(TEXT("Samsur maximum settled body penetration %.3f cm"),WorstOverlap));
  TestTrue(TEXT("Steep terrain cannot occupy the player's body or head"),WorstOverlap<1.);
  if(!Slopes.IsEmpty())
  {
   // A delayed terrain collision cook/server correction can put both the
   // capsule and eyes behind a one-sided surface. A normal sweep cannot be
   // relied on to detect that starting position.
   const FVector Surface=Slopes[0];
   for(float Dt:{1.f/90,1.f/15})
   {
    FACEPosition Pose;Pose.CellId=0x96790001;
    Pose.SetLocationFromUnreal(Surface-FVector(0,0,200),100);Pose.NormalizeOutdoorLandblock();
    Session->SetLocalPosition(Pose);PC->PredictedPose=Pose;
    PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bLocalPredicting=true;
    PC->bJumpAirborne=true;PC->JumpWorldAceVelocity=FVector(0,0,-12);PC->StepHoldSeconds=0;
    PC->bJumpAirborneSent=true;PC->JumpAirborneSeconds=5.f;
    Pawn->SetActorLocation(Pose.ToUnrealLocation(100)+FVector(0,0,90.75));
    VR->Head->SetWorldLocation(Pawn->GetActorLocation()+FVector(0,0,175-90.75));
    VR->Fade=0;VR->UpdateComfort(Dt);
    TestEqual(TEXT("Eyes below a one-sided slope never expose the void for a fade-in interval"),VR->Fade,1.f);
    VR->MoveStick=FVector2D(0,1);PC->PlayerTick(Dt);
    const FVector At=Pawn->GetActorLocation();float Ground;
    TestTrue(TEXT("Falling recovers above the DAT floor even after a missed terrain surface"),
     Dat->SampleOutdoorGroundZ(At.X,At.Y,100,Ground) && At.Z-90.75>=Ground-.1);
    TestFalse(TEXT("Terrain fallback completes the airborne network state"),PC->bJumpAirborne || PC->bJumpAirborneSent);
    TestEqual(TEXT("Terrain fallback resets the jump timer"),PC->JumpAirborneSeconds,0.f);
    VR->Head->SetWorldLocation(At+FVector(0,0,175-90.75));VR->UpdateComfort(1.f);
    TestEqual(TEXT("Recovered eyes restore the world view"),VR->Fade,0.f);
   }
  }
 }
 // Real river cells near Rithwic: Chaos must not snap feet back to the
 // rendered water sheet after DAT has supplied the submerged walking height.
 {
  FVector Water=FVector::ZeroVector;float Depth=0;uint32 WetBlock=0;
  const FACEBuiltLandblockMesh* WetMesh=nullptr;
  for(int32 X=198;X<=203 && !WetBlock;++X)for(int32 Y=137;Y<=143 && !WetBlock;++Y)
  {
   const uint32 Id=(X<<24)|(Y<<16);const auto* Mesh=Dat->GetOrBuildLandblockMesh(Id,100);
   if(!Mesh)continue;
   for(int32 I=1;I<8 && !WetBlock;++I)for(int32 J=1;J<8 && !WetBlock;++J)
   {
    const FVector At=FACEPosition::AceVectorToUnreal(FVector(X*192+I*24+12,Y*192+J*24+12,0),100);
    const float D=Dat->GetOutdoorWaterDepthCm(At.X,At.Y,100);float Ground=0;
    if(D>20 && Dat->SampleOutdoorGroundZ(At.X,At.Y,100,Ground)){Water=FVector(At.X,At.Y,Ground);Depth=D;WetBlock=Id;WetMesh=Mesh;}
   }
  }
  TestTrue(TEXT("Retail river fixture has submerged walking support"),WetBlock!=0);
  if(WetMesh)
  {
   auto* Land=World->SpawnActor<AACELandblockActor>();auto* Mesh=Land->TerrainMesh.Get();
   Mesh->bUseAsyncCooking=false;Mesh->bUseComplexAsSimpleCollision=true;
   Land->SetActorLocation(FACEPosition::AceVectorToUnreal(FVector((WetBlock>>24)*192,((WetBlock>>16)&255)*192,0),100));
   Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Mesh->SetCollisionResponseToAllChannels(ECR_Block);
   int32 SectionIndex=0;for(const auto& S:WetMesh->Sections)Mesh->CreateMeshSection(SectionIndex++,S.Vertices,S.Triangles,S.Normals,S.UVs,{},{},true);
   for(float Dt:{1.f/90,1.f/20})
   {
    FACEPosition Pose;Pose.CellId=WetBlock|1;Pose.SetLocationFromUnreal(Water+FVector(0,0,Depth),100);Pose.NormalizeOutdoorLandblock();
    Session->SetLocalPosition(Pose);PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;PC->StepHoldSeconds=0;PC->bLocalPredicting=true;
    Pawn->SetActorLocation(Pose.ToUnrealLocation(100)+FVector(0,0,90.75));VR->MoveStick=FVector2D::ZeroVector;
    for(int32 I=0;I<FMath::CeilToInt(.5f/Dt);++I){VR->Head->SetWorldLocation(Pawn->GetActorLocation()+FVector(0,0,84.25));PC->PlayerTick(Dt);}
    const float FeetZ=Pawn->GetActorLocation().Z-90.75;
    AddInfo(FString::Printf(TEXT("Water %08X depth %.1f feet %.1f expected %.1f"),WetBlock,Depth,FeetZ,Water.Z));
    TestTrue(TEXT("VR steps down through rendered water to retail wading height"),FMath::Abs(FeetZ-Water.Z)<2.f);
   }
   Land->Destroy();
  }
 }
 // Downhill recovery must still stop at a different obstruction.
 const FVector Center=Feet+FVector(0,0,90.75);
 const auto Shape=FCollisionShape::MakeCapsule(48,90.75);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(StairEscapeSafety),true,Pawn);
 FHitResult Contact;World->SweepSingleByChannel(Contact,Center,Center-FVector(0,0,1),FQuat::Identity,ECC_Pawn,Shape,Query);
 TestTrue(TEXT("Captured stair position intersects its ramp"),Contact.bStartPenetrating && Contact.Normal.Z>.66f);
 const FVector Push=Contact.Normal*(Contact.PenetrationDepth+.5f);
 const FVector Escaped=ACEBodySweep::Recover(*World,Center,Push,Contact,Shape,Query);
 TestTrue(TEXT("Ramp recovery releases sideways when the crown cannot rise"),Escaped.Y>Center.Y+8 && Escaped.Z<=Center.Z+.1);
 auto* Blocker=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Blocker);
 Blocker->SetRootComponent(Box);Blocker->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(100,2,100));
 Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
 Blocker->SetActorLocation(Center+FVector(0,51,0));
 TestTrue(TEXT("Downhill recovery cannot cross a second wall"),ACEBodySweep::Recover(*World,Center,Push,Contact,Shape,Query).Equals(Center,.01));
 Session->State=EACESessionState::Disconnected;Session->PlayerGuid=0;
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
