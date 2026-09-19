#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERunSpeedParityTest,"ACE.RetailParity.RunSpeed",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)

bool FACERunSpeedParityTest::RunTest(const FString&)
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
