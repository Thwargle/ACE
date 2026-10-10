#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEInputBindings.h"
#include "ACEHoverTooltipWidget.h"
#include "Dat/ACEDatCursor.h"
#include "ACEWorldEntityActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWalkingStepTest,"ACE.RetailParity.WalkingStep",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEWalkingStepTest::RunTest(const FString&)
{
 const FString OriginalSettings=GGameUserSettingsIni;
 GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/WalkingStepFixture.ini");
 FConfigFile Config;Config.NoSave=true;GConfig->SetFile(GGameUserSettingsIni,&Config);
 ACEInputBindings::Reload();ACEInputBindings::BeginEdit();ACEInputBindings::Defaults();ACEInputBindings::Commit();
 ON_SCOPE_EXIT{GGameUserSettingsIni=OriginalSettings;ACEInputBindings::Reload();};
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT{GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
 auto Session=Client->GetSession();
 if(!TestTrue(TEXT("Retail DAT loads"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
 // Independent retail data: the start and stop clips total one 0.780484m step.
 double RetailStep=0;
 for(uint32 Id:{0x03000006u,0x03000009u})
 {
  TArray<uint8> Blob;Dat->GetPortalDat()->ReadFile(Id,Blob);
  FACEDatCursor Cursor(Blob);FACEDatAnimation Anim;
  if(!ACEDatUnpack::UnpackAnimation(Cursor,Anim))return false;
  for(const auto& Frame:Anim.PositionFrames)RetailStep+=Frame.GetTranslation().Y;
 }
 TestTrue(TEXT("Retail walk links cover one full step"),FMath::Abs(RetailStep-.780484)<.00001);
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,88);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
 PC->InputComponent->AxisBindings.Reset();PC->bRetailCursorInstalled=true;
 PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.MotionTableId=0x09000001;Self.bIsPlayer=Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);
 auto* App=NewObject<UACECharacterAppearanceComponent>(Pawn);Pawn->AddInstanceComponent(App);App->RegisterComponent();
 if(!TestTrue(TEXT("Player appearance loads"),App->ApplyWorldObject(Self,100,false)))return false;
 App->PreferredStyle=ACEMotion::StanceNonCombat;
 auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);
 Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(8000,8000,50));
 Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Floor->SetActorLocation(FVector(-25000,25000,9950));
 const FVector StartFeet(-24000,25000,10000);
 auto Reset=[&]
 {
  PC->PlayerInput->FlushPressedKeys();PC->PlayerInput->ProcessInputStack({},1.f/90,false);
  App->ResetWalkingMotion();App->ClearActionMotion();
  FACEPosition Pose;Pose.CellId=0x01010001;Pose.SetLocationFromUnreal(StartFeet,100);
  Pose.SetAceFacingFromUnrealDir2D(FVector::XAxisVector);Session->SetLocalPosition(Pose);
  PC->PredictedPose=Pose;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bLocalPredicting=false;PC->bJumpAirborne=false;PC->bJumpCharging=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;
  PC->LandingWorldAceVelocity=FVector::ZeroVector;PC->LandingRootTrack.Reset();
  PC->ForwardSent=PC->RightSent=0;PC->bWasMoving=false;
  Pawn->SetActorLocationAndRotation(StartFeet+FVector(0,0,88),Pose.ToUnrealQuat());
 };
 auto Key=[&](FKey K,bool Down)
 {PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Down?IE_Pressed:IE_Released,Down?1.f:0.f));
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(K,Down?IE_Pressed:IE_Released,Down?1.f:0.f));
  PC->PlayerInput->ProcessInputStack({},1.f/90,false);};
 auto Tick=[&](float Dt){PC->PlayerTick(Dt);App->TickComponent(Dt,LEVELTICK_All,nullptr);};
 for(float Dt:{1.f/30,1.f/90,1.f/144})for(int Hold:{1,3,7})for(int Direction:{1,-1})
 {
  Reset();Key(EKeys::LeftShift,true);const FKey K=Direction>0?EKeys::W:EKeys::X;Key(K,true);
  for(int I=0;I<Hold;++I)Tick(Dt);
  Key(K,false);Key(EKeys::LeftShift,false);Tick(Dt);
  TestFalse(TEXT("Key release stops raw network motion immediately"),Session->bMoving);
  TestTrue(TEXT("Remaining step owns position reporting after key-up"),Session->bForcePositionReporting);
  TestTrue(TEXT("Remaining step owns local collision prediction"),PC->bLocalPredicting);
  for(int I=0;I<2/Dt;++I)Tick(Dt);
  const double Travel=(Pawn->GetActorLocation().X-StartFeet.X)/100.;
  const FString Label=FString::Printf(TEXT("%.0fHz %d-frame tap direction=%d travels %.6fm"),1/Dt,Hold,Direction,Travel);
  AddInfo(Label);TestTrue(*Label,FMath::Abs(Travel-RetailStep*Direction)<.002);
  TestFalse(TEXT("Completed step releases forced reporting"),Session->bForcePositionReporting);
  TestFalse(TEXT("Completed step has no pending animation"),App->HasWalkingTransition());
 }
 // Held walking retains its cycle after entry; stopping adds only the exit.
 Reset();Key(EKeys::LeftShift,true);Key(EKeys::W,true);
 for(int I=0;I<90;++I)Tick(1.f/90);
 Key(EKeys::W,false);for(int I=0;I<90;++I)Tick(1.f/90);
 TestTrue(TEXT("Held walking continues after the entry clip"),FMath::Abs((Pawn->GetActorLocation().X-StartFeet.X)/100.-(RetailStep+(1.-11./30)*3.12))<.003);
 Reset();Key(EKeys::LeftShift,true);Key(EKeys::C,true);Tick(1.f/90);Key(EKeys::C,false);Tick(1.f/90);
 const FVector StrafeEnd=Pawn->GetActorLocation();for(int I=0;I<90;++I)Tick(1.f/90);
 TestTrue(TEXT("Strafe still permits a micro-step and stops on release"),FVector::Dist2D(StartFeet,StrafeEnd)<10 && FVector::Dist2D(StrafeEnd,Pawn->GetActorLocation())<.01);
 TestFalse(TEXT("Strafe never queues forward links"),App->HasWalkingTransition());
 // Remaining authored movement is collision-resolved, not a teleport after key-up.
 auto* Wall=World->SpawnActor<AActor>();auto* WallBox=NewObject<UBoxComponent>(Wall);
 Wall->SetRootComponent(WallBox);Wall->AddInstanceComponent(WallBox);WallBox->SetBoxExtent(FVector(10,200,500));
 WallBox->SetCollisionResponseToAllChannels(ECR_Block);WallBox->RegisterComponent();Wall->SetActorLocation(StartFeet+FVector(80,0,0));
 Reset();Key(EKeys::LeftShift,true);Key(EKeys::W,true);Tick(1.f/90);Key(EKeys::W,false);
 for(int I=0;I<150;++I)Tick(1.f/90);
 TestTrue(TEXT("Finishing a step cannot carry the player through a wall"),Pawn->GetActorLocation().X-StartFeet.X<65);
 TestFalse(TEXT("A blocked step finishes without accumulating movement"),App->HasWalkingTransition());
 Wall->Destroy();
 // Other Unreal clients use the same retained links for the remote pose.
 auto* Remote=World->SpawnActor<AACEWorldEntityActor>();Self.Guid=23456;Self.bIsSelf=false;
 Remote->InitializeFromObject(Self,100,true);auto* RemoteApp=Remote->Appearance.Get();
 if(!TestEqual(TEXT("Remote fixture builds the same avatar parts"),RemoteApp->GetPartCount(),App->GetPartCount()))return false;
 RemoteApp->PreferredStyle=ACEMotion::StanceNonCombat;RemoteApp->SetPreviewCapture(true);
 Reset();Key(EKeys::LeftShift,true);Key(EKeys::W,true);RemoteApp->SetLocomotionInput(1,0,false,1);
 Tick(1.f/90);RemoteApp->TickComponent(1.f/90,LEVELTICK_All,nullptr);
 Key(EKeys::W,false);RemoteApp->SetLocomotionInput(0,0,true,1);
 for(int I=0;I<60;++I)
 {
  Tick(1.f/90);RemoteApp->TickComponent(1.f/90,LEVELTICK_All,nullptr);
  if(I>25)for(int32 Part=0;Part<App->GetPartCount();++Part)
   if(App->GetPartMesh(Part) && RemoteApp->GetPartMesh(Part))
    TestTrue(TEXT("Remote and local step poses agree after key release"),
     App->GetPartMesh(Part)->GetRelativeTransform().Equals(RemoteApp->GetPartMesh(Part)->GetRelativeTransform(),.002));
 }
 Reset();Key(EKeys::LeftShift,true);Key(EKeys::W,true);Tick(1.f/90);
 PC->bJumpAirborne=true;Tick(1.f/90);
 TestFalse(TEXT("Jump discards ground-only step motion"),App->HasWalkingTransition());
 Reset();Key(EKeys::LeftShift,true);Key(EKeys::W,true);Tick(1.f/90);Key(EKeys::LeftShift,false);Tick(1.f/90);
 TestFalse(TEXT("Starting a run cancels pending walk links"),App->HasWalkingTransition());
 Reset();Key(EKeys::LeftShift,true);Key(EKeys::W,true);Tick(1.f/90);
 Session->State=EACESessionState::CharacterSelect;Tick(1.f/90);
 TestFalse(TEXT("Leaving the world discards the unfinished step"),App->HasWalkingTransition());
 return true;
}
#endif
