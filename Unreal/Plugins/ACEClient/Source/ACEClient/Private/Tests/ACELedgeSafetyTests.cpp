#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ACEInputBindings.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEHoverTooltipWidget.h"
#include "ACEBodySweep.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELedgeSafetyTest,"ACE.RetailParity.LedgeSafety",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELedgeSafetyTest::RunTest(const FString&)
{
 // Exercise retail defaults, independently of the developer's custom keymap.
 ON_SCOPE_EXIT { ACEInputBindings::Reload(); };
 TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/LedgeFixture.ini"));
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
 const float Half=Capsule->GetScaledCapsuleHalfHeight(),Radius=Capsule->GetScaledCapsuleRadius();
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->MovementDirection=0;
 PC->InputComponent->AxisBindings.Reset();Client->SetRunSkill(600);
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(125*192,100*192,0),100)+FVector(0,8000,3000);
 auto Box=[&](FVector Center,FVector Size)
 {
  auto* A=World->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);
  B->SetBoxExtent(Size);B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();A->SetActorLocation(Origin+Center);
  return A;
 };
 auto* Platform=Box(FVector::ZeroVector,FVector(200,200,50));
 const FCollisionQueryParams Query(SCENE_QUERY_STAT(LedgeSafety),true,Pawn);
 FRotator ViewDirection=FRotator::ZeroRotator;
 auto Place=[&](FVector Feet,bool Tracked,FVector Direction=FVector::XAxisVector)
 {
  PC->PlayerInput->FlushPressedKeys();PC->ClearServerMoveTo();PC->bAutoRun=false;
  PC->PlayerInput->ProcessInputStack({},.016f,false);
  ViewDirection=Direction.Rotation();
  VR->bActive=Tracked;VR->MoveStick=VR->SmoothedMoveStick=FVector2D::ZeroVector;
  FACEPosition Pose;Pose.CellId=0x7D640004;Pose.SetLocationFromUnreal(Feet,100);Pose.SetAceFacingFromUnrealDir2D(Direction);
  Session->SetLocalPosition(Pose);PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bJumpAirborne=false;PC->bJumpCharging=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;PC->bLocalPredicting=false;
  Pawn->SetActorLocationAndRotation(Feet+FVector(0,0,Half),Pose.ToUnrealQuat());
 };
 auto Tick=[&](float Dt,FVector HeadDelta=FVector::ZeroVector)
 {
  VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,175-Half)+HeadDelta,ViewDirection);
  PC->PlayerTick(Dt);
 };
 // Real controller / sweep ceiling contacts use the same restitution as walls.
 // CPhysicsObj::handle_all_collisions also clears tangential velocity for Inelastic.
 auto* Ceiling=Box(FVector(0,0,Half*2+100),FVector(200,200,10));
 for(bool Tracked:{false,true})for(bool Inelastic:{false,true})for(float Elasticity:{0.f,.1f})
 {
  Place(Origin+FVector(0,0,60),Tracked);
  auto& PhysicsSelf=Session->WorldObjects[Self.Guid];PhysicsSelf.Elasticity=Elasticity;
  PhysicsSelf.PhysicsState=ACEPhysicsState::Gravity|(Inelastic?ACEPhysicsState::Inelastic:0);
  PC->bJumpAirborne=true;PC->JumpWorldAceVelocity=FVector(1,0,5);PC->JumpAirborneSeconds=0;
  for(int32 I=0;I<12 && PC->JumpWorldAceVelocity.Z>0;++I)Tick(1.f/60);
  TestTrue(TEXT("Ceiling contact occurs before the natural jump apex"),PC->JumpAirborneSeconds<.2f);
  TestTrue(TEXT("Ceiling contact stays airborne"),PC->bJumpAirborne);
  if(Inelastic)TestTrue(TEXT("Inelastic ceiling clears every velocity component"),PC->JumpWorldAceVelocity.IsNearlyZero(.001));
  else
  {
   TestTrue(TEXT("Ceiling preserves tangential velocity"),FMath::IsNearlyEqual(PC->JumpWorldAceVelocity.X,1.,.001));
   TestTrue(TEXT("Ceiling uses server elasticity instead of deleting upward velocity"),
    Elasticity>0 ? PC->JumpWorldAceVelocity.Z<-.1 : FMath::IsNearlyZero(PC->JumpWorldAceVelocity.Z,.001));
  }
 }
 Ceiling->Destroy();Session->WorldObjects[Self.Guid].PhysicsState=0;Session->WorldObjects[Self.Guid].Elasticity=.05f;
 // Flat and sloping platforms end in a true drop, with no lower tread.
 for(float Pitch:{0.f,25.f,-25.f})for(bool Tracked:{false,true})for(bool Run:{false,true})for(float Dt:{1.f/90,1.f/15})
 {
  Platform->SetActorRotation(FRotator(Pitch,0,0));
  const FVector Start=Platform->GetActorTransform().TransformPosition(FVector(-100,0,50));
  float Support=Start.Z;
  TestTrue(TEXT("Ledge scenario starts on a real walkable surface"),ACEBodySweep::FindFootSupport(*World,Start,Radius,70,Query,Support,30));
  Place(FVector(Start.X,Start.Y,Support),Tracked);VR->Settings->bRun=Run;
  if(Tracked)VR->MoveStick=FVector2D(0,1);
  else
  {
   PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
   if(!Run)PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift,IE_Pressed,1.f));
  }
  PC->PlayerInput->ProcessInputStack({},Dt,false);
  int32 AirFrames=0,ReportedAirFrames=0;
  for(int32 I=0;I<FMath::CeilToInt(1.8f/Dt);++I)
  {
   Tick(Dt);AirFrames+=PC->bJumpAirborne?1:0;ReportedAirFrames+=Session->bAutoPosContact?0:1;
  }
  const FVector End=Pawn->GetActorLocation()-Origin-FVector(0,0,Half);
  TestEqual(*FString::Printf(TEXT("Walking/running stays grounded at ledge: pitch=%.0f vr=%d run=%d dt=%.3f end=%s"),Pitch,Tracked,Run,Dt,*End.ToString()),AirFrames,0);
  TestEqual(TEXT("Blocked ledge never reports an invisible fall to the server"),ReportedAirFrames,0);
  TestTrue(TEXT("Reported position agrees with the visible pawn's feet at the ledge"),
   Session->GetPlayerPosition().ToUnrealLocation(100).Equals(Pawn->GetActorLocation()-FVector(0,0,Half),1.));
  AddInfo(FString::Printf(TEXT("Ledge matrix pitch=%.0f vr=%d run=%d dt=%.3f end=%s"),Pitch,Tracked,Run,Dt,*End.ToString()));
  TestTrue(*FString::Printf(TEXT("Ledge prevention permits approach pitch=%.0f vr=%d run=%d dt=%.3f"),Pitch,Tracked,Run,Dt),Pawn->GetActorLocation().X>Start.X+50);
  if (Pitch==0.f)
   TestTrue(TEXT("Grounded ledge position stays within retail's half-radius footprint"),End.X<=200+Radius*.5f+.5f);
  const double EdgeX=Pawn->GetActorLocation().X;
  if(Tracked) VR->MoveStick=FVector2D(0,-1);
  else
  {
   PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));
   PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::X,IE_Pressed,1.f));
   PC->PlayerInput->ProcessInputStack({},Dt,false);
  }
  for(int32 I=0;I<FMath::CeilToInt(.2f/Dt);++I)Tick(Dt);
  TestTrue(TEXT("Player can immediately back away from a blocked edge"),Pawn->GetActorLocation().X<EdgeX-10);
 }
 Platform->SetActorRotation(FRotator::ZeroRotator);
 // Holding diagonally outward must still slide along the safe ledge tangent.
 Platform->FindComponentByClass<UBoxComponent>()->SetBoxExtent(FVector(200,1500,50));
 for(bool Tracked:{false,true})
 {
  Place(Origin+FVector(180,-400,50),Tracked,FVector(1,1,0).GetSafeNormal());
  VR->Settings->bRun=true;
  if(Tracked)VR->MoveStick=FVector2D(0,1);
  else PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
  PC->PlayerInput->ProcessInputStack({},.016f,false);
  for(int32 I=0;I<90;++I)Tick(1.f/90);
  TestFalse(TEXT("Diagonal edge slide stays grounded"),PC->bJumpAirborne);
  TestTrue(TEXT("Diagonal edge slide preserves tangential movement"),Pawn->GetActorLocation().Y>Origin.Y-150);
 }
 Platform->FindComponentByClass<UBoxComponent>()->SetBoxExtent(FVector(200,200,50));
 // Room-scale adjustments below the old 0.5 cm threshold must not creep off.
 Place(Origin+FVector(190,0,50),true);
 int32 TinyAir=0;
 for(int32 I=0;I<240;++I){Tick(1.f/90,FVector(.35,0,0));TinyAir+=PC->bJumpAirborne?1:0;}
 TestEqual(TEXT("Tiny room-scale motion cannot bypass the precipice check"),TinyAir,0);
 TestTrue(TEXT("Room-scale motion reaches the edge before stopping"),Pawn->GetActorLocation().X>Origin.X+205);
 TestTrue(TEXT("Room-scale edge keeps supporting platform height"),Pawn->GetActorLocation().Z-Half>Origin.Z+25);
 // The move-to action used by F/use is subject to the same outdoor edge rule.
 Place(Origin+FVector(0,0,50),false);
 FACEWorldObject Target;Target.Guid=555;Target.bHasPosition=true;Target.Position=PC->PredictedPose;
 Target.Position.SetLocationFromUnreal(Origin+FVector(600,0,50),100);Session->WorldObjects.Add(Target.Guid,Target);
 PC->BeginUseApproach(Target.Guid,.1f,false,false);
 int32 ApproachAir=0;
 for(int32 I=0;I<120;++I){Tick(1.f/60);ApproachAir+=PC->bJumpAirborne?1:0;}
 TestEqual(TEXT("Outdoor approach cannot walk off a platform"),ApproachAir,0);
 TestTrue(TEXT("Outdoor approach moves toward the target before reaching the edge"),Pawn->GetActorLocation().X>Origin.X+100);
 PC->ClearServerMoveTo();
 // Diagonal contact with a wall redirects the move after the initial edge check.
 auto* Wall=Box(FVector(180,0,200),FVector(2,200,200));
 Place(Origin+FVector(125,170,50),false,FVector(1,1,0).GetSafeNormal());
 PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
 PC->PlayerInput->ProcessInputStack({},.016f,false);
 int32 SlideAir=0;
 for(int32 I=0;I<120;++I){Tick(1.f/30);SlideAir+=PC->bJumpAirborne?1:0;}
 TestEqual(TEXT("Wall slide cannot redirect a grounded move past its floor"),SlideAir,0);
 Wall->Destroy();
 // A low-FPS frame can have supported endpoints on opposite sides of a gap.
 // Checking only the destination must not turn walking into a jump.
 auto* FarPlatform=Box(FVector(480,0,0),FVector(200,200,50));
 for(bool Tracked:{false,true})for(float Dt:{1.f/90,.1f})
 {
  Place(Origin+FVector(210,0,50),Tracked);VR->Settings->bRun=true;
  if(Tracked)VR->MoveStick=FVector2D(0,1);
  else PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
  PC->PlayerInput->ProcessInputStack({},Dt,false);
  for(int32 I=0;I<20;++I)Tick(Dt);
  TestTrue(*FString::Printf(TEXT("Walking cannot bridge an open gap vr=%d dt=%.4f"),Tracked,Dt),
   Pawn->GetActorLocation().X<=Origin.X+200+Radius*.5f+.5f);
  TestFalse(TEXT("Blocked gap crossing retains ground contact"),PC->bJumpAirborne);
 }
 FarPlatform->Destroy();
 // A real Jump explicitly clears grounded contact and must still cross the edge.
 for(bool Tracked:{false,true})
 {
  Place(Origin+FVector(130,0,50),Tracked);PC->bRunning=true;PC->bJumpCharging=true;PC->JumpChargeExtent=1;
  PC->ReleaseJump(1,0);TestTrue(TEXT("Intentional jump launches at a ledge"),PC->bJumpAirborne);
  for(int32 I=0;I<35;++I)Tick(1.f/90);
  TestTrue(TEXT("Intentional jump travels beyond the ledge"),Pawn->GetActorLocation().X>Origin.X+260);
  Place(Origin+FVector(500,0,400),Tracked);Tick(1.f/90);
  TestTrue(TEXT("Initially unsupported portal/spawn position can fall"),PC->bJumpAirborne);
 }
 // A real collision mesh must use retail's half-radius polygon footprint,
 // rather than accepting the full sphere's larger rounded contact region.
 Platform->Destroy();
 auto* MeshOwner=World->SpawnActor<AActor>();
 auto* Mesh=NewObject<UProceduralMeshComponent>(MeshOwner);MeshOwner->SetRootComponent(Mesh);
 Mesh->bUseAsyncCooking=false;Mesh->SetCollisionResponseToAllChannels(ECR_Block);Mesh->RegisterComponent();
 Mesh->CreateMeshSection(0,{FVector(-200,-200,50),FVector(200,-200,50),FVector(200,200,50),FVector(-200,200,50)},
  {0,2,1,0,3,2},{},{},{},{},true);
 MeshOwner->SetActorLocation(Origin);
 float SupportZ=0;
 TestTrue(TEXT("Retail footprint accepts a center 23 cm past a flat edge"),
  ACEBodySweep::FindFootSupport(*World,Origin+FVector(223,0,50),48,50,Query,SupportZ,1,false,true));
 TestTrue(TEXT("Supported flat polygon retains its plane instead of rolling down the rounded rim"),
  FMath::IsNearlyEqual(SupportZ,float(Origin.Z+50),.1f));
 TestFalse(TEXT("Retail footprint rejects a center 25 cm past a flat edge"),
  ACEBodySweep::FindFootSupport(*World,Origin+FVector(225,0,50),48,50,Query,SupportZ,1,false,true));
 auto* LowerFloor=Box(FVector(400,0,-80),FVector(300,200,10));
 TestFalse(TEXT("Balcony below a rejected roof rim cannot override its half-step walkability check"),
  ACEBodySweep::FindFootSupport(*World,Origin+FVector(225,0,50),48,150,Query,SupportZ,1,false,true));
 TestTrue(TEXT("Two-sphere body can descend to a clear floor within authored full step-down"),
  ACEBodySweep::FindFootSupport(*World,Origin+FVector(300,0,50),48,150,Query,SupportZ,1,false,true));
 TestTrue(TEXT("Clear descent reaches the actual lower floor"),FMath::IsNearlyEqual(SupportZ,float(Origin.Z-70),.1f));
 TestFalse(TEXT("Single-sphere body uses half-radius probe when authored step-down exceeds its diameter"),
  ACEBodySweep::FindFootSupport(*World,Origin+FVector(300,0,50),48,150,Query,SupportZ,1,false,true,nullptr,true,1));
 LowerFloor->Destroy();
 Mesh->ComponentTags.Add(TEXT("ACEOutdoorTerrain"));
 TestTrue(TEXT("Terrain supports the vertical low point inside the plateau"),
  ACEBodySweep::FindFootSupport(*World,Origin+FVector(199,0,50),48,50,Query,SupportZ,1,false,true));
 TestFalse(TEXT("Terrain rim behind the player is not support beyond the cliff"),
  ACEBodySweep::FindFootSupport(*World,Origin+FVector(201,0,50),48,50,Query,SupportZ,1,false,true));
 for(bool Tracked:{false,true})for(float Dt:{1.f/90,.1f})
 {
  Place(Origin+FVector(180,0,50),Tracked);VR->Settings->bRun=true;
  if(Tracked)VR->MoveStick=FVector2D(0,1);
  else PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
  PC->PlayerInput->ProcessInputStack({},Dt,false);
  int32 LostContact=0;
  for(int32 I=0;I<30;++I){Tick(Dt);LostContact+=(!Session->bAutoPosContact || PC->bJumpAirborne)?1:0;}
  TestTrue(TEXT("Terrain cliff stops the player while the vertical low point is still supported"),
   Pawn->GetActorLocation().X<=Origin.X+200.1);
  TestEqual(TEXT("Terrain edge keeps visual and reported ground contact"),LostContact,0);
 }
 return !HasAnyErrors();
}
#endif
