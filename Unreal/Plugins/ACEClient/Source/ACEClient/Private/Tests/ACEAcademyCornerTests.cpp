#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../ACEBodySweep.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACEHoverTooltipWidget.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "GameFramework/PlayerInput.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAcademyCornerTest,"ACE.RetailParity.AcademyCorners",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEAcademyCornerTest::RunTest(const FString&)
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
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(127*192,3*192,0),100);
 TSet<uint32> Cells;
 for(uint32 Id=0x7F030133;Id<=0x7F030140;++Id)Cells.Add(Id);
 for(uint32 Id=0x7F0302C2;Id<=0x7F0302C5;++Id)Cells.Add(Id);
 for(uint32 Id=0x7F03031E;Id<=0x7F030321;++Id)Cells.Add(Id);
 for(uint32 Id:Cells)
 {
  TestNotNull(TEXT("Academy mesh is decoded before collision registration"),Dat->GetOrBuildEnvCellMesh(Id,100));
  auto* Room=World->SpawnActor<AACEEnvCellActor>();
  TestTrue(TEXT("Academy collision loads from retail DAT"),Room->LoadEnvCell(Id,Origin,100));
  Room->SetEnvCellCollisionActive(true);
 }
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,90.75);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);
 PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
 PC->bRetailCursorInstalled=true;PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);Client->SetRunSkill(600);
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->ActivateRig();
 VR->bTracking=true;VR->Settings->MovementSmoothing=0;VR->Settings->bRun=true;
 VR->Settings->ForwardAssistDegrees=0;VR->Settings->MovementDirection=0;
 // Synthetic tracking supplies the stick directly; physical axis polling in
 // PlayerTick would otherwise replace it with zero on the test machine.
 PC->InputComponent->AxisBindings.Reset();
 const auto Body=FCollisionShape::MakeCapsule(48,90.75);
 FCollisionQueryParams Query(SCENE_QUERY_STAT(AcademyCorner),true,Pawn);
 int32 Cases=0,Failures=0;
 for(bool UseVR:{false,true})for(float X=126;X<=134;X+=2)for(float Y=-154;Y<=-132;Y+=2)
 {
  VR->bActive=UseVR;
  FVector Start=Origin+FVector(-X*100,Y*100,-600+90.75);
  float Support=0;
  if(!ACEBodySweep::FindFootSupport(*World,Start-FVector(0,0,90.75),48,5,Query,Support,10))continue;
  Start.Z=Support+90.75;
  FHitResult Initial;
  if(ACEBodySweep::Sweep(*World,Initial,Start,Start+FVector(0,0,.1),Body,Query)
   && Initial.bStartPenetrating && Initial.PenetrationDepth>.05f)continue;
  uint32 Cell=0;
  for(uint32 Id:Cells)if(ACECellTransit::SphereIntersectsEnvCell(*Dat,Id,Start,0,100)){Cell=Id;break;}
  if(!Cell)continue;
  for(float Yaw=0;Yaw<360;Yaw+=15)for(float Dt:{1.f/60,1.f/15})
  {
   FACEPosition Pose;Pose.CellId=Cell;Pose.SetLocationFromUnreal(Start-FVector(0,0,90.75),100);
   Pose.SetAceFacingFromUnrealDir2D(FRotator(0,Yaw,0).Vector());Session->SetLocalPosition(Pose);
   PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;PC->bJumpAirborne=false;
   PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;PC->bLocalPredicting=false;
   Pawn->SetActorLocationAndRotation(Start,Pose.ToUnrealQuat());
   auto Walk=[&](FKey Key,float Duration)
   {
    PC->PlayerInput->FlushPressedKeys();
    PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f));
    PC->PlayerInput->ProcessInputStack({},Dt,false);
    for(int32 I=0;I<FMath::CeilToInt(Duration/Dt);++I)
    {
     if(UseVR)
     {
      VR->Head->SetWorldLocationAndRotation(Pawn->GetActorLocation()+FVector(0,0,84.25),FRotator(0,Yaw,0));
      VR->MoveStick=FVector2D(0,Key==EKeys::Up?1:-1);
     }
     PC->PlayerTick(Dt);
    }
   };
   Walk(EKeys::Up,.7f);
   const FVector Pressed=Pawn->GetActorLocation();
   // Ignore paths leaving this bounded fixture, or continuing freely along a stair.
   if(!Cells.Contains(PC->PredictedPose.CellId))continue;
   Walk(EKeys::Up,.1f);
   if(FVector::Dist2D(Pressed,Pawn->GetActorLocation())>2.f)continue;
   Walk(EKeys::Down,.6f);++Cases;
   const FVector Retreated=Pawn->GetActorLocation();
   if(FVector::Dist2D(Pressed,Retreated)<25.f)
   {
    ++Failures;
    FHitResult Hit;ACEBodySweep::Sweep(*World,Hit,Pressed,Pressed+FVector(20,20,0),Body,Query);
    const FVector Out=ACEBodySweep::Recover(*World,Pressed,Hit.Normal*(Hit.PenetrationDepth+2),Hit,Body,Query);
    AddInfo(FString::Printf(TEXT("Corner contact pen=%d depth=%f normal=%s impact=%s component=%s recover=%s"),Hit.bStartPenetrating,
     Hit.PenetrationDepth,*Hit.Normal.ToString(),*Hit.ImpactNormal.ToString(),*GetNameSafe(Hit.GetComponent()),*(Out-Pressed).ToString()));
    const FVector Out2=ACEBodySweep::Recover(*World,Pressed,Hit.Normal.GetSafeNormal2D()*(Hit.PenetrationDepth+2),Hit,Body,Query);
    AddInfo(FString::Printf(TEXT("Recover2D=%s"),*(Out2-Pressed).ToString()));
    TArray<FHitResult> Hits;World->SweepMultiByChannel(Hits,Pressed,Pressed+FVector(0,0,.1),FQuat::Identity,ECC_Pawn,Body,Query);
    for(const auto& H:Hits)AddInfo(FString::Printf(TEXT("Corner raw pen=%d depth=%f normal=%s impact=%s component=%s"),H.bStartPenetrating,
     H.PenetrationDepth,*H.Normal.ToString(),*H.ImpactNormal.ToString(),*GetNameSafe(H.GetComponent())));
    AddError(FString::Printf(TEXT("Academy corner cannot retreat: vr=%d cell=%08X start=%s yaw=%.0f dt=%.3f pressed=%s retreated=%s air=%d"),
     UseVR,Cell,*(Start-Origin).ToString(),Yaw,Dt,*(Pressed-Origin).ToString(),*(Retreated-Origin).ToString(),PC->bJumpAirborne));
    if(Failures>=8)goto Done;
   }
  }
 }
Done:
 AddInfo(FString::Printf(TEXT("Academy blocked approach/retreat cases: %d, failures: %d"),Cases,Failures));
 TestTrue(TEXT("Academy fixture exercises blocked corners"),Cases>0);
 Session->State=EACESessionState::Disconnected;Session->PlayerGuid=0;
 GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
