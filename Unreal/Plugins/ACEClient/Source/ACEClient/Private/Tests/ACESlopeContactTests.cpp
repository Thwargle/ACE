#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACEInputBindings.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACELandblockActor.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEHoverTooltipWidget.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESlopeContactTest,"ACE.RetailParity.SlopeContact",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACESlopeContactTest::RunTest(const FString&)
{
 ON_SCOPE_EXIT { ACEInputBindings::Reload(); };
 TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/SlopeFixture.ini"));
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
 FString DatPath=Dat->GetDatDirectory();
 FParse::Value(FCommandLine::Get(),TEXT("ACEDatDirectory="),DatPath);
 if(DatPath.IsEmpty())DatPath=TEXT("C:/Turbine/Asheron's Call");
 if(!TestTrue(TEXT("Retail DAT available"),Dat->LoadDatDirectory(DatPath)))return false;
 // Reproduce the user's v122 DreamWeave slope with its actual DAT triangles.
 const auto* Built=Dat->GetOrBuildLandblockMesh(0x89650000,100);
 if(!TestNotNull(TEXT("Reported hillside loads"),Built))return false;
 auto* Land=World->SpawnActor<AACELandblockActor>();
 Land->TerrainMesh->bUseAsyncCooking=false;
 int Section=0;
 for(const auto& S:Built->Sections)
  Land->TerrainMesh->CreateMeshSection(Section++,S.Vertices,S.Triangles,S.Normals,S.UVs,TArray<FColor>(),TArray<FProcMeshTangent>(),true);
 Land->SetActorLocation(FACEPosition::AceVectorToUnreal(FVector(137*192,101*192,0),100));
 auto Session=Client->GetSession();
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48,90.75);Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
 PC->InputComponent->AxisBindings.Reset();
 PC->bRetailCursorInstalled=true;PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
 Session->PlayerGuid=12345;Session->State=EACESessionState::InWorld;
 FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bIsSelf=true;
 Session->WorldObjects.Add(Self.Guid,Self);PC->ApplyPlayerCapsuleFromSetup(Self.SetupId);
 const float Half=Capsule->GetScaledCapsuleHalfHeight();
 auto* App=NewObject<UACECharacterAppearanceComponent>(Pawn);Pawn->AddInstanceComponent(App);App->RegisterComponent();
 FACEPosition Reported;Reported.CellId=0x89650031;
 Reported.Location=FVector(165.061462,8.666930,8.821681);
 Reported.RotationW=.024776f;Reported.RotationXYZ=FVector(0,0,.999693);
 const FVector Feet=Reported.ToUnrealLocation(100);
 float Ground=0;FVector Normal;
 TestTrue(TEXT("Reported location has retail walkable hillside"),Dat->SampleOutdoorGroundZ(Feet.X,Feet.Y,100,Ground,&Normal)
  && Normal.Z>.6641741f && Normal.Z<.995f);
 FHitResult GroundHit;
 TestTrue(TEXT("The hillside has live collision, not just DAT fallback"),World->LineTraceSingleByChannel(GroundHit,
  Feet+FVector(0,0,200),Feet-FVector(0,0,200),ECC_Pawn));
 auto Place=[&]()
 {
  PC->PlayerInput->FlushPressedKeys();PC->ClearServerMoveTo();PC->bAutoRun=false;
  PC->bServerTurnActive=false;
  PC->PlayerInput->ProcessInputStack({},.016f,false);
  Session->SetLocalPosition(Reported);PC->PredictedPose=Reported;PC->bHavePredictedPose=true;PC->bHaveLastServerPose=false;
  PC->bJumpAirborne=false;PC->bJumpCharging=false;PC->bStandingJumpLocked=false;PC->StepHoldSeconds=0;PC->bLocalPredicting=false;
  PC->JumpWorldAceVelocity=PC->LandingWorldAceVelocity=FVector::ZeroVector;PC->LandingRootTrack.Reset();
  App->ClearActionMotion();
  Pawn->SetActorLocationAndRotation(Feet+FVector(0,0,Half),Reported.ToUnrealQuat());
 };
 for(float Dt:{1.f/144,1.f/60,1.f/30,.1f})for(int Mode=0;Mode<5;++Mode)
 {
  Place();
  if(Mode==1 || Mode==2)
   PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Mode==1?EKeys::A:EKeys::D,IE_Pressed,1.f));
  if(Mode>=3)App->PlayActionMotion(0x40000034,2.f,ACEMotion::StanceMagic);
  PC->PlayerInput->ProcessInputStack({},Dt,false);
  double Drift=0,HeightError=0,SettledHeight=0,HeightDrift=0;int LostContact=0;
  for(int Frame=0;Frame<FMath::CeilToInt(5.f/Dt);++Frame)
  {
   // Combat/server target facing must use the same stable footing as keys.
   if(Mode==4 && Frame%FMath::RoundToInt(1.f/Dt)==0)
   {
    PC->bServerTurnActive=true;
    PC->ServerTurnHeading=FQuat(FVector::UpVector,PI*.5)*PC->PredictedPose.GetAcQuat();
    PC->ServerTurnSpeed=1.f;
   }
   PC->PlayerTick(Dt);
   const FVector Actual=Pawn->GetActorLocation()-FVector(0,0,Half);
   Drift=FMath::Max(Drift,FVector::Dist2D(Actual,Feet));
   HeightError=FMath::Max(HeightError,FMath::Abs(Actual.Z-Ground));
   if(Frame==0)SettledHeight=Actual.Z;
   else HeightDrift=FMath::Max(HeightDrift,FMath::Abs(Actual.Z-SettledHeight));
   LostContact+=PC->bJumpAirborne || !Session->bAutoPosContact ? 1 : 0;
  }
  const FString Case=FString::Printf(TEXT("mode=%d hz=%.0f drift=%.3fcm height=%.3fcm"),Mode,1.f/Dt,Drift,HeightError);
  AddInfo(Case);
  TestTrue(TEXT("Idle/turn/cast stays planted: ")+Case,Drift<.5);
  TestEqual(TEXT("Walkable slope retains contact: ")+Case,LostContact,0);
  // Chaos uses a round foot on the same plane. Its required vertical lift
  // is radius*(1/N.z-1); it must settle once without downhill travel or bob.
  TestTrue(TEXT("Body remains at hillside support: ")+Case,
   HeightError<Capsule->GetScaledCapsuleRadius()*(1.f/Normal.Z-1.f)+1.);
  TestTrue(TEXT("Stationary elevation stays stable: ")+Case,HeightDrift<1.);
  if(Mode==1 || Mode==2)TestFalse(TEXT("Turn input actually changes heading"),Pawn->GetActorQuat().Equals(Reported.ToUnrealQuat(),.01));
 }
 // A stationary fix must not disable deliberate slope movement or jumping.
 for(float Dt:{1.f/144,1.f/30})for(const FKey& Key:{EKeys::W,EKeys::X})
 {
  Place();
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f));
  PC->PlayerInput->ProcessInputStack({},Dt,false);
  for(int Frame=0;Frame<FMath::CeilToInt(.4f/Dt);++Frame)PC->PlayerTick(Dt);
  TestTrue(TEXT("Deliberate walking still moves along the hillside"),FVector::Dist2D(Pawn->GetActorLocation(),Feet)>30.);
  TestFalse(TEXT("Walking on this gentle slope remains grounded"),PC->bJumpAirborne);
  PC->PlayerInput->FlushPressedKeys();PC->PlayerInput->ProcessInputStack({},Dt,false);
  for(int Frame=0;Frame<FMath::CeilToInt(.5f/Dt);++Frame)PC->PlayerTick(Dt);
  const FVector Stopped=Pawn->GetActorLocation();
  for(int Frame=0;Frame<FMath::CeilToInt(2.f/Dt);++Frame)PC->PlayerTick(Dt);
  TestTrue(TEXT("Stopping after slope travel stays planted"),FVector::Dist2D(Stopped,Pawn->GetActorLocation())<.5);
 }
 Place();PC->bJumpCharging=true;PC->JumpChargeExtent=1;PC->ReleaseJump(1,0);
 TestTrue(TEXT("A deliberate hillside jump launches"),PC->bJumpAirborne);
 for(int Frame=0;Frame<12;++Frame)PC->PlayerTick(1.f/60);
 TestTrue(TEXT("Ground support does not hold a deliberate jump down"),
  PC->bJumpAirborne && Pawn->GetActorLocation().Z>Feet.Z+Half+10.f);
 return !HasAnyErrors();
}
#endif
