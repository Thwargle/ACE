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
  int32 AirFrames=0;
  for(int32 I=0;I<FMath::CeilToInt(1.8f/Dt);++I){Tick(Dt);AirFrames+=PC->bJumpAirborne?1:0;}
  const FVector End=Pawn->GetActorLocation()-Origin-FVector(0,0,Half);
  TestEqual(*FString::Printf(TEXT("Walking/running stays grounded at ledge: pitch=%.0f vr=%d run=%d dt=%.3f end=%s"),Pitch,Tracked,Run,Dt,*End.ToString()),AirFrames,0);
  AddInfo(FString::Printf(TEXT("Ledge matrix pitch=%.0f vr=%d run=%d dt=%.3f end=%s"),Pitch,Tracked,Run,Dt,*End.ToString()));
  TestTrue(*FString::Printf(TEXT("Ledge prevention permits approach pitch=%.0f vr=%d run=%d dt=%.3f"),Pitch,Tracked,Run,Dt),Pawn->GetActorLocation().X>Start.X+50);
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
 return !HasAnyErrors();
}
#endif
