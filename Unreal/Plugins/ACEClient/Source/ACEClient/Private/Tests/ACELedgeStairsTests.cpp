#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACEHoverTooltipWidget.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELedgeStairsTest,"ACE.RetailParity.LedgeStairs",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELedgeStairsTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 TestTrue(TEXT("Runtime collision provider resolves the fixture game world"),GI->GetWorld()==World);
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
 auto Session=Client->GetSession();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(125*192,100*192,0),100);
 TArray<AACEEnvCellActor*> Rooms;
 for(uint32 Id=0x7D640149;Id<0x7D640150;++Id)
 {
  const auto* Built=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Built)return false;
  const auto Stabs=Built->StaticObjects;
  for(const auto& Stab:Stabs)Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
  auto* Room=World->SpawnActor<AACEEnvCellActor>();Room->LoadEnvCell(Id,Origin,100);
  Room->SetEnvCellCollisionActive(true);Room->EnsureStaticObjectsQueued();
  for(int32 I=0;I<200 && Room->HasPendingStaticObjects();++I)
  {
   ++GFrameCounter;Room->Tick(.016f);
   FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
  }
  TestFalse(TEXT("Room stair/furniture collision is completely loaded"),Room->HasPendingStaticObjects());
  Rooms.Add(Room);
 }
 auto* Controller=World->SpawnActor<AACEPlayerController>();Controller->Client=Client;
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(22,88);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);Controller->Possess(Pawn);
 Controller->PlayerInput=NewObject<UPlayerInput>(Controller);
 // Old saved development settings must not steal retail gameplay keys.
 Controller->PlayerInput->DebugExecBindings.AddDefaulted_GetRef().Key=EKeys::Semicolon;
 GetMutableDefault<UInputSettings>()->ConsoleKeys.AddUnique(EKeys::Tilde);
 Controller->SetupInputComponent();
 TestTrue(TEXT("Controller removes inherited Unreal debug key bindings"),Controller->PlayerInput->DebugExecBindings.IsEmpty());
 TestTrue(TEXT("Backtick cannot activate the Unreal console"),GetDefault<UInputSettings>()->ConsoleKeys.IsEmpty());
 FString DebuggerKey;
 GConfig->GetString(TEXT("/Script/GameplayDebugger.GameplayDebuggerConfig"),TEXT("ActivationKey"),DebuggerKey,GEngineIni);
 TestEqual(TEXT("Gameplay debugger does not capture apostrophe"),DebuggerKey,FString(TEXT("None")));
 Controller->bRetailCursorInstalled=true;Controller->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 Controller->bEnterWorldLoading=false;Controller->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);
 Controller->ApplyPlayerCapsuleFromSetup(Self.SetupId);
 const float Half=Capsule->GetScaledCapsuleHalfHeight();
 AddInfo(FString::Printf(TEXT("Retail player capsule radius=%.2f half=%.2f"),Capsule->GetScaledCapsuleRadius(),Half));
 TestEqual(TEXT("Human walking radius comes from the authored sphere, not the enclosing setup bound"),Capsule->GetScaledCapsuleRadius(),48.f);
 auto Place=[&](FVector Local,uint32 Cell)
 {
  FACEPosition Pose;Pose.CellId=Cell;Pose.SetLocationFromUnreal(Origin+Local,100);
  Pose.SetAceFacingFromUnrealDir2D(FVector::XAxisVector);
  Session->SetLocalPosition(Pose);Controller->PredictedPose=Pose;Controller->bHavePredictedPose=true;
  Controller->bHaveLastServerPose=false;Controller->bLocalPredicting=false;
  Controller->bJumpAirborne=false;Controller->bStandingJumpLocked=false;Controller->StepHoldSeconds=0;
  Pawn->SetActorLocationAndRotation(Origin+Local+FVector(0,0,Half),Pose.ToUnrealQuat());
 };
 Place(FVector(-9165,2924,805),0x7D64014E);
 Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
 Controller->PlayerInput->ProcessInputStack({},.016f,false);
 for(int32 Frame=0;Frame<240;++Frame)
 {
  Controller->PlayerTick(.016f);
  const FVector Local=Pawn->GetActorLocation()-Origin-FVector(0,0,Half);
  if(Frame%30==0)AddInfo(FString::Printf(TEXT("Arcanum climb frame=%d feet=%s cell=%08X airborne=%d"),Frame,*Local.ToString(),Controller->PredictedPose.CellId,Controller->bJumpAirborne));
  if(Local.X>-8580)break;
 }
 const FVector Final=Pawn->GetActorLocation()-Origin-FVector(0,0,Half);
 TestTrue(TEXT("Walk all the way from Arcanum basement up onto the main floor"),Final.X>-8620 && Final.Z>1195);
 Controller->PlayerInput->FlushPressedKeys();Controller->PlayerInput->ProcessInputStack({},.016f,false);
 for(float FrameTime : {.016f,.033f,.05f})
 for(float Offset : {-30.f,0.f,30.f})
 {
  Place(FVector(-8540,2924+Offset,1200),0x7D64014B);
  auto Face=[&](float Direction)
  {
   auto Pose=Controller->PredictedPose;Pose.SetAceFacingFromUnrealDir2D(FVector(Direction,0,0));
   Controller->PredictedPose=Pose;Session->SetLocalPosition(Pose);Pawn->SetActorRotation(Pose.ToUnrealQuat());
  };
  for(float Direction : {-1.f,1.f})
  {
   Face(Direction);
   Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
   Controller->PlayerInput->ProcessInputStack({},.016f,false);
   int32 AirFrames=0;
   float WorstPenetration=0.f;
   for(int32 Frame=0;Frame<120;++Frame)
   {
    Controller->PlayerTick(FrameTime);
    AirFrames+=Controller->bJumpAirborne ? 1 : 0;
    FHitResult Contact;
    const FVector Center=Pawn->GetActorLocation();
    const FCollisionQueryParams ContactQuery(SCENE_QUERY_STAT(ACEStairBody),true,Pawn);
    World->SweepSingleByChannel(Contact,Center,Center+FVector(0,0,.01f),FQuat::Identity,ECC_Pawn,
     FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Half),ContactQuery);
    if(Contact.bStartPenetrating)
    {
     if(Offset==0 && Contact.PenetrationDepth>WorstPenetration && Contact.PenetrationDepth>1.f)
      AddInfo(FString::Printf(TEXT("Stair overlap frame=%d dir=%.0f feet=%s depth=%.3f normal=%s impactnormal=%s actor=%s comp=%s"),Frame,Direction,*(Center-Origin-FVector(0,0,Half)).ToString(),Contact.PenetrationDepth,*Contact.Normal.ToString(),*Contact.ImpactNormal.ToString(),*GetNameSafe(Contact.GetActor()),*GetNameSafe(Contact.Component.Get())));
     WorstPenetration=FMath::Max(WorstPenetration,Contact.PenetrationDepth);
    }
    const FVector Feet=Pawn->GetActorLocation()-Origin-FVector(0,0,Half);
    if(Frame%30==0)AddInfo(FString::Printf(TEXT("Stairs dt=%.3f offset=%.0f dir=%.0f frame=%d feet=%s cell=%08X air=%d"),FrameTime,Offset,Direction,Frame,*Feet.ToString(),Controller->PredictedPose.CellId,Controller->bJumpAirborne));
    if(Direction<0 ? Feet.X<-9180 : Feet.X>-8540)break;
   }
   const FVector Feet=Pawn->GetActorLocation()-Origin-FVector(0,0,Half);
   TestTrue(*FString::Printf(TEXT("Stair traversal offset=%.0f direction=%.0f dt=%.3f"),Offset,Direction,FrameTime),Direction<0 ? Feet.X<-9150 && Feet.Z<850 : Feet.X>-8580 && Feet.Z>1195);
   TestFalse(TEXT("Stairs do not latch airborne"),Controller->bJumpAirborne);
   TestEqual(TEXT("Stair traversal remains grounded on every frame"),AirFrames,0);
   TestTrue(*FString::Printf(TEXT("Stair body does not enter a wall or tread (max overlap %.3f cm)"),WorstPenetration),WorstPenetration<1.f);
   Controller->PlayerInput->FlushPressedKeys();Controller->PlayerInput->ProcessInputStack({},.016f,false);
  }
 }
 // The indoor cell is loaded, but this point is above its open stairwell.
 Place(FVector(-8950,2924,1165),0x7D64014E);
 Controller->PlayerTick(.016f);
 // Testing a jump while already falling must not replace the ballistic arc.
 TestTrue(TEXT("Unsupported loaded indoor space begins a fall"),Controller->bJumpAirborne);
 Controller->BeginJumpCharge();
 TestFalse(TEXT("Jump cannot charge on unsupported indoor air"),Controller->bJumpCharging);
 for(int32 I=0;I<180;++I)Controller->PlayerTick(.016f);
 TestFalse(TEXT("Indoor fall finishes instead of holding the player in midair"),Controller->bJumpAirborne);
 AddInfo(FString::Printf(TEXT("Indoor stairwell fall feet=%s"),*(Pawn->GetActorLocation()-Origin-FVector(0,0,Half)).ToString()));
 // A grounded player pushing into a roof edge must retain walkable support.
 auto* Roof=World->SpawnActor<AActor>();auto* RoofBox=NewObject<UBoxComponent>(Roof);
 Roof->SetRootComponent(RoofBox);Roof->AddInstanceComponent(RoofBox);
 RoofBox->SetBoxExtent(FVector(200,200,50));RoofBox->SetCollisionResponseToAllChannels(ECR_Block);RoofBox->RegisterComponent();
 Roof->SetActorLocation(Origin+FVector(0,8000,1500));
 Place(FVector(0,8000,1550),0x7D640004);
 Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
 Controller->PlayerInput->ProcessInputStack({},.016f,false);
 for(int32 I=0;I<180;++I)Controller->PlayerTick(.016f);
 FVector Edge=Pawn->GetActorLocation()-Origin-FVector(0,0,Half);
 AddInfo(FString::Printf(TEXT("Pushing roof edge feet=%s airborne=%d"),*Edge.ToString(),Controller->bJumpAirborne));
 TestFalse(TEXT("Running against a ledge never enters the airborne lock"),Controller->bJumpAirborne);
 TestTrue(TEXT("Running against a ledge retains walkable height"),Edge.Z>1530 && Edge.X<235);
 auto Back=Controller->PredictedPose;Back.SetAceFacingFromUnrealDir2D(-FVector::XAxisVector);
 Controller->PredictedPose=Back;Session->SetLocalPosition(Back);Pawn->SetActorRotation(Back.ToUnrealQuat());
 for(int32 I=0;I<10;++I)Controller->PlayerTick(.016f);
 const FVector Returned=Pawn->GetActorLocation()-Origin-FVector(0,0,Half);
 TestTrue(TEXT("Player can immediately run back from the edge"),Returned.X<Edge.X-80 && Returned.Z>=1549);
 Controller->PlayerInput->FlushPressedKeys();Controller->PlayerInput->ProcessInputStack({},.016f,false);
 // Run distance must be independent of rendering frame rate. A former
 // per-frame capsule-distance cap reduced a 12 m/s runner to 6 m/s at 20 Hz.
 RoofBox->SetBoxExtent(FVector(8000,2000,50));
 Roof->SetActorLocation(Origin+FVector(-8000,9000,3000));
 for(int32 Skill : {200,600})
 for(int32 Rate : {20,30,72,90})
 {
  Client->SetRunSkill(Skill);
  Place(FVector(-10000,9000,3050),0x7D640004);
  const FVector Begin=Pawn->GetActorLocation();
  Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
  Controller->PlayerInput->ProcessInputStack({},1.f/Rate,false);
  for(int32 I=0;I<Rate;++I) Controller->PlayerTick(1.f/Rate);
  const float Expected=Client->GetLocomotionSpeed(true)*100.f;
  const float Actual=FVector::Dist2D(Begin,Pawn->GetActorLocation());
  // Retail prediction stores global coordinates as floats (~24 km in this
  // fixture), allowing up to half a 0.25 cm ULP of rounding on each frame.
  const float QuantizationTolerance=Rate*.125f+.5f;
  TestTrue(*FString::Printf(TEXT("Run skill %d at %d Hz travels %.2f cm, expected %.2f"),Skill,Rate,Actual,Expected),FMath::Abs(Actual-Expected)<QuantizationTolerance);
  TestFalse(TEXT("Fast ground movement remains supported"),Controller->bJumpAirborne);
  Controller->PlayerInput->FlushPressedKeys();Controller->PlayerInput->ProcessInputStack({},.016f,false);
 }
 auto* Wall=World->SpawnActor<AActor>();auto* WallBox=NewObject<UBoxComponent>(Wall);
 Wall->SetRootComponent(WallBox);Wall->AddInstanceComponent(WallBox);
 WallBox->SetBoxExtent(FVector(2,1000,200));WallBox->SetCollisionResponseToAllChannels(ECR_Block);WallBox->RegisterComponent();
 Wall->SetActorLocation(Origin+FVector(-9000,9000,3250));
 Place(FVector(-10000,9000,3050),0x7D640004);
 Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
 Controller->PlayerInput->ProcessInputStack({},.1f,false);
 for(int32 I=0;I<20;++I)Controller->PlayerTick(.1f);
 const float WallX=(Pawn->GetActorLocation()-Origin).X;
 TestTrue(TEXT("Full run distance still collides with a thin wall during 100 ms frames"),WallX<-9002.f && WallX>-9100.f);
 Wall->Destroy();
 GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
