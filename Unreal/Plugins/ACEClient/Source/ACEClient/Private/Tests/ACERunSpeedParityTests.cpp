#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEInputBindings.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEHoverTooltipWidget.h"
#include "Dat/ACEDatCursor.h"
#include "Dat/ACEDatFileTypes.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERunSpeedParityTest,"ACE.RetailParity.RunSpeed",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)

bool FACERunSpeedParityTest::RunTest(const FString&)
{
 const FString OriginalSettings=GGameUserSettingsIni;
 GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/RunSpeedFixture.ini");
 FConfigFile Config;Config.NoSave=false;Config.bCanSaveAllSections=true;
 GConfig->SetFile(GGameUserSettingsIni,&Config);ACEInputBindings::Reload();
 ON_SCOPE_EXIT{GGameUserSettingsIni=OriginalSettings;ACEInputBindings::Reload();};
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
 auto Session=Client->GetSession();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;

 // Independent evidence: measure the position frames from the retail human
 // RunForward animation, rather than deriving expected travel from our helper.
 TArray<uint8> Blob;Dat->GetPortalDat()->ReadFile(0x03000002,Blob);
 FACEDatCursor Cursor(Blob);FACEDatAnimation Animation;
 if(!ACEDatUnpack::UnpackAnimation(Cursor,Animation))return false;
 FVector Offset=FVector::ZeroVector;Cursor.Seek(16);
 for(uint32 I=0;I<Animation.NumFrames;++I)
 {
  FVector3f Delta; if(!Cursor.Read(Delta)||!Cursor.Skip(16))return false;
  Offset+=FVector(Delta);
 }
 const double DatRunSpeed=Offset.Size2D()*30.0/Animation.NumFrames;
 TestTrue(TEXT("Retail human RunForward root motion averages 4 metres/sec"),FMath::IsNearlyEqual(DatRunSpeed,4.,.00002));

 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,88);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
 PC->bRetailCursorInstalled=true;PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);
 auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);
 Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(8000,8000,50));
 Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Floor->SetActorLocation(FVector(-25000,25000,9950));
 const FVector StartFeet(-24000,25000,10000);
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->bRun=true;VR->Settings->MovementScale=1;
 VR->Settings->ForwardAssistDegrees=10;VR->Settings->MovementDirection=0;
 PC->InputComponent->AxisBindings.Reset();
 // Retail CommandList::AddCommand/NukeCommand stack opposing directions,
 // instead of summing them to zero. Use the actual controller event path.
 ACEInputBindings::BeginEdit();ACEInputBindings::Defaults();ACEInputBindings::Commit();
 auto Key=[&](FKey K,bool Down)
 {PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Down?IE_Pressed:IE_Released,Down?1.f:0.f));
  // This isolated world has no viewport input routing; feed its PlayerInput
  // as well, while retaining the controller's physical event-order capture.
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(K,Down?IE_Pressed:IE_Released,Down?1.f:0.f));
  PC->PlayerInput->ProcessInputStack({},1.f/90,false);};
 struct FAxisCase {FKey Positive,Negative,PositiveKey,NegativeKey;};
 for(const auto& Pair:{FAxisCase{EKeys::W,EKeys::S,EKeys::W,EKeys::X},
  {EKeys::E,EKeys::Q,EKeys::C,EKeys::Z},{EKeys::D,EKeys::A,EKeys::D,EKeys::A}})
 {
  Key(Pair.PositiveKey,true);Key(Pair.NegativeKey,true);
  TestEqual(TEXT("Newest opposing movement command takes priority"),ACEInputBindings::MovementAxis(PC,Pair.Positive,Pair.Negative,PC->MovementKeyPressOrder),-1.f);
  Key(Pair.NegativeKey,false);
  TestEqual(TEXT("Releasing it restores the previously held command"),ACEInputBindings::MovementAxis(PC,Pair.Positive,Pair.Negative,PC->MovementKeyPressOrder),1.f);
  Key(Pair.PositiveKey,false);
 }
 Key(EKeys::C,true);Key(EKeys::A,true);
 TestEqual(TEXT("Mouse-facing turn and strafe keys share newest-command priority"),
  ACEInputBindings::MovementAxis(PC,EKeys::E,EKeys::Q,PC->MovementKeyPressOrder,EKeys::D,EKeys::A),-1.f);
 Key(EKeys::A,false);
 TestEqual(TEXT("Mouse-facing release restores the held strafe"),
  ACEInputBindings::MovementAxis(PC,EKeys::E,EKeys::Q,PC->MovementKeyPressOrder,EKeys::D,EKeys::A),1.f);
 Key(EKeys::C,false);
 ACEInputBindings::BeginEdit();ACEInputBindings::Set(EKeys::S,0,FInputChord(EKeys::F2));ACEInputBindings::Commit();
 Key(EKeys::W,true);Key(EKeys::F2,true);
 TestEqual(TEXT("Command order follows a rebound physical backward key"),
  ACEInputBindings::MovementAxis(PC,EKeys::W,EKeys::S,PC->MovementKeyPressOrder),-1.f);
 Key(EKeys::F2,false);Key(EKeys::W,false);
 ACEInputBindings::BeginEdit();ACEInputBindings::Defaults();ACEInputBindings::Commit();
 auto* App=NewObject<UACECharacterAppearanceComponent>(Pawn);Pawn->AddInstanceComponent(App);App->RegisterComponent();
 App->PlayActionMotion(0x40000034,2.f,ACEMotion::StanceMagic);
 App->SetLocomotionInput(0,1,true,1);
 TestEqual(TEXT("Strafe retains the active casting gesture"),App->ActionCommand,0x40000034u);
 // SetLocomotionInput is deliberately shared with remote presentation;
 // only the local command edge may interrupt a server casting substate.
 App->InterruptCastWithMovement();
 TestEqual(TEXT("New local forward command replaces casting substate"),App->ActionCommand,0u);
 App->PlayActionMotion(0x1000006F,2.f,ACEMotion::StanceMagic);
 App->InterruptCastWithMovement();
 TestEqual(TEXT("Movement does not truncate a queued scarab windup action"),App->ActionCommand,0x1000006Fu);
 App->ClearActionMotion();
 struct FCase { int32 Skill; float Scale,Burden; int32 Stamina; double Speed; };
 const FCase Cases[]={
  {593,1,0,500,12.2257250946},{593,1.1f,0,500,13.4482976041},
  {799,1,0,500,12.7977977978},{800,1,0,500,18.0},
  {801,1,0,500,12.8021978022},{1000,1,0,500,13.1666666667},
  {593,1,1.5f,500,8.1128625473},{800,1,0,0,4.0},{0,1,0,500,4.0}};
 for(bool bVR:{false,true})for(int32 Rate:{30,72,90,144})for(const auto& C:Cases)
 {
  VR->bActive=bVR;VR->bInventoryOpen=VR->bSettingsOpen=VR->bKeyboardOpen=false;
  VR->MoveStick=FVector2D(.04f,.93f);VR->SmoothedMoveStick=FVector2D::ZeroVector;
  Session->WorldObjects[Self.Guid].Scale=C.Scale;
  Session->PlayerVitals.bValid=true;Session->PlayerVitals.RunSkillCurrent=C.Skill;
  Session->PlayerVitals.Health=Session->PlayerVitals.MaxHealth=500;
  Session->PlayerVitals.Stamina=C.Stamina;Session->PlayerVitals.MaxStamina=500;
  Session->OnVitalsUpdated.Broadcast(Session->PlayerVitals);Client->SetBurden(C.Burden);
  FACEPosition Pose;Pose.CellId=0x01010001;Pose.SetLocationFromUnreal(StartFeet,100);
  Pose.SetAceFacingFromUnrealDir2D(FVector::XAxisVector);Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bLocalPredicting=false;PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
  Pawn->SetActorLocationAndRotation(StartFeet+FVector(0,0,88),Pose.ToUnrealQuat());
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
  PC->PlayerInput->ProcessInputStack({},1.f/Rate,false);
  for(int32 Frame=0;Frame<Rate;++Frame)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,77),FRotator::ZeroRotator);
   PC->PlayerTick(1.f/Rate);
  }
  const double Actual=FVector::Dist2D(StartFeet,Pawn->GetActorLocation())/100.;
  const FString Label=FString::Printf(TEXT("%s %dHz run=%d size=%.2f burden=%.1f stamina=%d: %.5fm expected %.5fm"),
   bVR?TEXT("VR full stick"):TEXT("Desktop W"),Rate,C.Skill,C.Scale,C.Burden,C.Stamina,Actual,C.Speed);
  AddInfo(Label);TestTrue(*Label,FMath::Abs(Actual-C.Speed)<.006);
  TestFalse(TEXT("Flat-ground run remains grounded"),PC->bJumpAirborne);
  PC->PlayerInput->FlushPressedKeys();PC->PlayerInput->ProcessInputStack({},1.f/Rate,false);
 }
 for(bool Run:{false,true})
 {
  VR->bActive=false;Session->WorldObjects[Self.Guid].Scale=1;
  Session->PlayerVitals.RunSkillCurrent=593;Session->PlayerVitals.Stamina=500;
  Session->OnVitalsUpdated.Broadcast(Session->PlayerVitals);Client->SetBurden(0);
  FACEPosition Pose;Pose.CellId=0x01010001;Pose.SetLocationFromUnreal(StartFeet,100);
  Pose.SetAceFacingFromUnrealDir2D(FVector::XAxisVector);Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
  Pawn->SetActorLocationAndRotation(StartFeet+FVector(0,0,88),Pose.ToUnrealQuat());
  Key(EKeys::X,true);if(!Run)Key(EKeys::LeftShift,true);
  for(int Frame=0;Frame<90;++Frame)PC->PlayerTick(1.f/90);
  const double Expected=3.12*.65*(Run?12.2257250946/4.:1.);
  AddInfo(FString::Printf(TEXT("Backpedal run=%d actual=%.6f expected=%.6f"),Run,FVector::Dist2D(StartFeet,Pawn->GetActorLocation())/100.,Expected));
  TestTrue(TEXT("Backward movement uses negative WalkForward, including Run hold rate"),
   FMath::Abs(FVector::Dist2D(StartFeet,Pawn->GetActorLocation())/100.-Expected)<.006);
  Key(EKeys::X,false);
  PC->PredictedPose=Pose;Session->SetLocalPosition(Pose);PC->bHaveLastServerPose=false;
  Key(EKeys::D,true);
  for(int Frame=0;Frame<90;++Frame)PC->PlayerTick(1.f/90);
  const float Yaw=FMath::RadiansToDegrees(Pose.GetAcQuat().AngularDistance(PC->PredictedPose.GetAcQuat()));
  TestTrue(TEXT("Local turning agrees with retail turn modifier and Run multiplier"),
   FMath::Abs(Yaw-FMath::RadiansToDegrees(1.5f)*(Run?1.5f:1.f))<.02f);
  Key(EKeys::D,false);if(!Run)Key(EKeys::LeftShift,false);
 }
 // Retail CMotionInterp::get_state_velocity / CACQualities::InqJumpVelocity:
 // the airborne velocity is not scaled by creature size like grounded root motion.
 for(bool Tracked:{false,true})for(int Rate:{30,90,144})for(float Size:{1.f,1.1f})for(float Extent:{.25f,1.f})
 {
  VR->bActive=Tracked;VR->MoveStick=FVector2D(0,1);VR->Settings->bRun=true;
  Session->WorldObjects[Self.Guid].Scale=Size;Session->bHasPlayerEncumbrance=false;Client->SetBurden(0);
  Session->PlayerVitals.RunSkillCurrent=593;Session->PlayerVitals.JumpSkillCurrent=400;
  Session->PlayerVitals.Stamina=500;Session->OnVitalsUpdated.Broadcast(Session->PlayerVitals);
  FACEPosition Pose;Pose.CellId=0x01010001;Pose.SetLocationFromUnreal(StartFeet,100);
  Pose.SetAceFacingFromUnrealDir2D(FVector::XAxisVector);Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;PC->bRunning=true;
  Pawn->SetActorLocationAndRotation(StartFeet+FVector(0,0,88),Pose.ToUnrealQuat());
  PC->bJumpCharging=true;PC->JumpChargeExtent=Extent;PC->ReleaseJump(1,0);
  const double Height=(400./1700.*22.2+.05)*Extent;
  const double Flight=2*FMath::Sqrt(Height*19.6)/9.8;
  double Peak=0;int Frames=0;
  for(;Frames<Rate*5 && PC->bJumpAirborne;++Frames)
  {
   VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,77),FRotator::ZeroRotator);
   PC->PlayerTick(1.f/Rate);
   Peak=FMath::Max(Peak,(Pawn->GetActorLocation().Z-88-StartFeet.Z)/100.);
  }
  const double Distance=FVector::Dist2D(StartFeet,Pawn->GetActorLocation())/100.;
  TestTrue(TEXT("Jump apex agrees with retail skill/charge formula"),FMath::Abs(Peak-Height)<.02);
  TestTrue(TEXT("Jump duration is frame-rate independent"),FMath::Abs(double(Frames)/Rate-Flight)<2./Rate);
  TestTrue(TEXT("Jump range agrees with retail leave-ground run speed"),FMath::Abs(Distance-12.2257250946*Flight)<12.2257250946*2./Rate+.02);
  TestFalse(TEXT("Completed jump releases airborne animation and input"),PC->bJumpAirborne);
  const FVector Touchdown=Pawn->GetActorLocation();
  // A 30 Hz VR frame contains three movement substeps; its final substep
  // may already have coasted after contact. Start this comparison at the
  // sampled pose/velocity, rather than counting that time a second time.
  const double TouchdownSpeed=PC->LandingWorldAceVelocity.Size2D();
  VR->MoveStick=FVector2D::ZeroVector;
  TestTrue(TEXT("Landing preserves horizontal physics velocity"),PC->LandingWorldAceVelocity.Size2D()>10.);
  for(int I=0;I<Rate/2;++I)
  {
   VR->Head->SetWorldLocation(Pawn->GetActorLocation()+FVector(0,0,77));PC->PlayerTick(1.f/Rate);
  }
  const double Glide=FVector::Dist2D(Touchdown,Pawn->GetActorLocation())/100.;
  TestTrue(TEXT("Retail friction permits a brief glide instead of gluing feet at contact"),Glide>2.5 && Glide<3.5);
  // Independent CPhysicsObj::UpdatePhysicsInternal reference: friction is
  // applied before displacement, and the pre-friction speed owns the cutoff.
  double ReferenceSpeed=TouchdownSpeed,ReferenceDistance=0;
  for(int I=0;I<Rate/2;++I)
  {
   ReferenceSpeed=ReferenceSpeed*ReferenceSpeed < .0625+.0002 ? 0 : ReferenceSpeed*FMath::Pow(.05,1./Rate);
   ReferenceDistance+=ReferenceSpeed/Rate;
  }
  TestTrue(TEXT("Half-second glide matches retail friction, not an arbitrary distance multiplier"),FMath::Abs(Glide-ReferenceDistance)<.025);
  for(int I=Rate/2;I<Rate*4;++I)
  {
   ReferenceSpeed=ReferenceSpeed*ReferenceSpeed < .0625+.0002 ? 0 : ReferenceSpeed*FMath::Pow(.05,1./Rate);
   ReferenceDistance+=ReferenceSpeed/Rate;
   VR->Head->SetWorldLocation(Pawn->GetActorLocation()+FVector(0,0,77));PC->PlayerTick(1.f/Rate);
  }
  const double FullGlide=FVector::Dist2D(Touchdown,Pawn->GetActorLocation())/100.;
  TestTrue(TEXT("Full coast reaches the retail friction distance without early cancellation"),FMath::Abs(FullGlide-ReferenceDistance)<.025);
  TestTrue(TEXT("Coast ends at retail's small-velocity cutoff"),PC->LandingWorldAceVelocity.IsNearlyZero());
  TestFalse(TEXT("Landing glide does not re-enter airborne animation"),PC->bJumpAirborne);
  AddInfo(FString::Printf(TEXT("Jump tracked=%d rate=%d scale=%.1f charge=%.2f apex=%.3fm range=%.3fm time=%.3fs"),Tracked,Rate,Size,Extent,Peak,Distance,double(Frames)/Rate));
 }
 // Retail permits charging during flight but checks contact when executing.
 for(bool Tracked:{false,true})
 {
  VR->bActive=Tracked;PC->bJumpAirborne=true;PC->bJumpCharging=false;
  PC->bStandingJumpLocked=false;PC->JumpWorldAceVelocity=FVector(0,4,-2);
  PC->BeginJumpCharge();
  TestTrue(TEXT("Airborne player can charge the next jump"),PC->bJumpCharging);
  PC->JumpChargeExtent=.6f;PC->ReleaseJump(1,0);
  TestTrue(TEXT("Early release does not double-jump or replace flight velocity"),
   PC->bJumpAirborne && PC->JumpWorldAceVelocity.Equals(FVector(0,4,-2)) && !PC->bJumpCharging);
  FACEPosition Landing;Landing.CellId=0x01010001;Landing.SetLocationFromUnreal(StartFeet+FVector(0,0,20),100);
  Session->SetLocalPosition(Landing);PC->PredictedPose=Landing;PC->bHavePredictedPose=true;
  Pawn->SetActorLocation(StartFeet+FVector(0,0,108));VR->MoveStick=FVector2D::ZeroVector;
  PC->BeginJumpCharge();PC->JumpChargeExtent=.6f;
  for(int I=0;I<90 && PC->bJumpAirborne;++I)
  {
   VR->Head->SetWorldLocation(Pawn->GetActorLocation()+FVector(0,0,77));PC->PlayerTick(1.f/90);
  }
  TestTrue(TEXT("Holding through touchdown retains the accumulated charge"),!PC->bJumpAirborne && PC->bJumpCharging && PC->JumpChargeExtent>=.6f);
  PC->ReleaseJump(1,0);
  TestTrue(TEXT("Release after contact launches the charged follow-up jump"),PC->bJumpAirborne && PC->JumpWorldAceVelocity.Z>1.f);
  PC->bJumpAirborne=false;PC->bStandingJumpLocked=false;
 }
 Session->PlayerVitals.Strength=100;Session->PlayerVitals.CarryingCapacityAugs=0;
 Session->PlayerVitals.Stamina=500;Client->SetRunSkill(593);
 Session->bHasPlayerEncumbrance=true;Session->PlayerEncumbranceVal=22500;
 Client->SetBurden(0); // Deliberately stale UI value.
 TestTrue(TEXT("Weight packets affect speed without an inventory-panel refresh"),FMath::IsNearlyEqual(Client->GetLocomotionSpeed(true),8.1128625f,.00001f));
 Session->PlayerVitals.CarryingCapacityAugs=5;
 TestTrue(TEXT("Carrying augmentations use the server/retail capacity"),FMath::IsNearlyEqual(Client->GetLocomotionSpeed(true),12.225725f,.00001f));
 VR->bActive=false;
 GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
