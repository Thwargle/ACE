#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEWorldEntityActor.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERemoteTurnTargetsTest,"ACE.RetailParity.RemoteTurnTargets",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACERemoteTurnTargetsTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.SetCurrentWorld(World);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
 World->SetGameMode(FURL(nullptr,TEXT("/Game/Test?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
 auto* Presenter=NewObject<UACEWorldPresenterComponent>(World->GetAuthGameMode());
 World->GetAuthGameMode()->AddInstanceComponent(Presenter);Presenter->RegisterComponent();
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
 auto* PC=World->SpawnActor<AACEPlayerController>();World->AddController(PC);PC->Client=Client;
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
 Session->State=EACESessionState::InWorld;Session->PlayerGuid=0x50000001;
 PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;PC->bHavePredictedPose=true;
 PC->LastAppliedTeleportSeq=Session->GetTeleportSeq();PC->LastAppliedForcePositionSeq=Session->GetForcePositionSeq();
 FACEPosition P;P.CellId=0x7D64000E;P.Location=FVector(50,50,100);P.bHasContactState=true;P.bIsGrounded=true;
 const FVector Origin=P.ToUnrealLocation(100);
 FACEWorldObject O;O.Guid=0x70000001;O.SetupId=0x020014E0;O.MotionTableId=0x09000028;
 O.ItemType=ACEItemType::Creature;O.bHasPosition=true;O.Position=P;
 auto* Buffer=World->SpawnActor<AACEWorldEntityActor>();Buffer->InitializeFromObject(O,100,false);
 Buffer->MotionTableId=O.MotionTableId;Buffer->bClampToGround=false;Buffer->SetActorEnableCollision(false);
 Session->WorldObjects.Add(O.Guid,O);
 Session->OnMotionUpdate.AddLambda([&](int32 Guid,const FACEObjectMotionState& M){if(Guid==O.Guid)Buffer->ApplyMotionState(M);});
 uint16 Sequence=0;
 auto Turn=[&](int32 Target,float Fallback,float Offset)
 {
  FACEBinaryWriter W;W.WriteUInt32(O.Guid);W.WriteUInt16(0);W.WriteUInt16(++Sequence);W.WriteUInt16(0);
  W.WriteUInt8(0);W.Align();W.WriteUInt8(8);W.WriteUInt8(0);W.WriteUInt16(0x3d);
  W.WriteUInt32(Target);W.WriteFloat(Fallback);W.WriteUInt32(0);W.WriteFloat(1);W.WriteFloat(Offset);
  FACEBinaryReader R(W.GetData());Session->HandleUpdateMotion(R);
 };
 auto Place=[&](float Heading)
 {
  FACEPosition Pos=P;Pos.SetLocationFromUnreal(Origin+FACEPosition::UnrealDirFromAceHeadingDegrees(Heading)*500,100);
  return Pos;
 };
 auto Face=[&](float Heading){return FACEPosition::UnrealDirFromAceHeadingDegrees(Heading);};
 auto Reset=[&](float Heading)
 {
  Buffer->RemoteMotion={};Buffer->RemotePredictLocation=Origin;Buffer->RemoteAnchorLocation=Origin;
  Buffer->RemotePredictRotation=FACEPosition::QuatFromUnrealTravelDir2D(Face(Heading));
  Buffer->bHaveRemotePredict=true;Buffer->bRemoteCorrectionPending=false;
  Buffer->SetActorLocationAndRotation(Origin,Buffer->RemotePredictRotation);
 };
 // MovementManager::unpack_movement -> TargetManager::SendVoyeurUpdate samples
 // the live physics frame. A stale F748 may be in the session during dispatch.
 Session->SetLocalPosition(Place(10));PC->PredictedPose=Place(30);Reset(0);
 Turn(Session->PlayerGuid,5,7);
 TestTrue(TEXT("TurnTo uses local physics, including relative heading"),Buffer->RemoteTurnFaceDir.Equals(Face(37),.00001));
 PC->PredictedPose=Place(90);Buffer->Tick(.01f);
 TestTrue(TEXT("TurnTo freezes its target once, rather than tracking the mouse/player"),Buffer->RemoteTurnFaceDir.Equals(Face(37),.00001));
 // A pending teleport/forced position must not reuse prediction from the old cell.
 for(bool Teleport:{false,true})
 {
  if(Teleport)++Session->TeleportSeq;else ++Session->ForcePositionSeq;
  Reset(0);Turn(Session->PlayerGuid,5,0);
  TestTrue(TEXT("Unapplied authoritative relocation wins over old local prediction"),Buffer->RemoteTurnFaceDir.Equals(Face(10),.00001));
  PC->LastAppliedTeleportSeq=Session->GetTeleportSeq();PC->LastAppliedForcePositionSeq=Session->GetForcePositionSeq();
 }
 FACEWorldObject Remote=O;Remote.Guid=0x50000002;Remote.Position=Place(15);
 Session->WorldObjects.Add(Remote.Guid,Remote);
 auto* Target=World->SpawnActor<AACEWorldEntityActor>();Target->InitializeFromObject(Remote,100,false);
 Target->SetActorLocation(Place(50).ToUnrealLocation(100));Presenter->Spawned.Add(Remote.Guid,Target);
 Reset(0);Turn(Remote.Guid,9,4);
 TestTrue(TEXT("TurnTo uses a remote target's live physics rather than sparse F748"),Buffer->RemoteTurnFaceDir.Equals(Face(54),.00001));
 Presenter->Spawned.Remove(Remote.Guid);Reset(0);Turn(Remote.Guid,75,4);
 TestTrue(TEXT("An object without a live physics body uses the packed absolute fallback"),Buffer->RemoteTurnFaceDir.Equals(Face(75),.00001));
 Presenter->Spawned.Add(Remote.Guid,Target);Buffer->Tick(.01f);
 TestTrue(TEXT("A target appearing later cannot replace the frozen fallback"),Buffer->RemoteTurnFaceDir.Equals(Face(75),.00001));
 // Slow orbit around the actual Pumpkin Buffer, across heading wrap, with
 // delayed local position echoes. Each packet must choose the current bearing
 // and the DAT's angular rate; no backwards frame is permitted in this replay.
 float Rate=0;TestTrue(TEXT("Pumpkin authored turn rate exists"),Dat->GetMotionTurnRate(O.MotionTableId,ACEMotion::StanceNonCombat,Rate));
 for(int FPS:{30,60,144,240})
 {
  Reset(350);double MaxError=0;bool Monotonic=true;
  for(int Step=0;Step<30;++Step)
  {
   const float Heading=350+(Step+1)*2;
   Session->SetLocalPosition(Place(Heading-6));PC->PredictedPose=Place(Heading);
   Turn(Session->PlayerGuid,Heading-8,0);
   const FQuat Want=FACEPosition::QuatFromUnrealTravelDir2D(Face(Heading));
   float Remaining=FMath::DegreesToRadians(2.f);
   for(int Frame=0;Frame<FPS/10;++Frame)
   {
    const double Before=Buffer->GetActorQuat().AngularDistance(Want);
    Buffer->Tick(1.f/FPS);Remaining=FMath::Max(0.f,Remaining-FMath::Abs(Rate)/FPS);
    const double After=Buffer->GetActorQuat().AngularDistance(Want);
    Monotonic &= After<=Before+.00001;MaxError=FMath::Max(MaxError,FMath::Abs(After-Remaining));
   }
  }
  TestTrue(TEXT("Slow orbit progresses monotonically at retail DAT rate"),Monotonic && MaxError<.0001);
  AddInfo(FString::Printf(TEXT("Pumpkin orbit %d FPS: max angular error %.6f degrees"),FPS,FMath::RadiansToDegrees(MaxError)));
 }
 Session->OnMotionUpdate.Clear();Presenter->Spawned.Reset();
 GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
