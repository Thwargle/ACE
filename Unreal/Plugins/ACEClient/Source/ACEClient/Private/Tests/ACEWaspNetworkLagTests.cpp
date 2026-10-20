#if WITH_DEV_AUTOMATION_TESTS
#include "ACEWaspLagLink.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACECreatureFixtures.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWaspNetworkLagTest,"ACE.Network.WaspDungeonLag",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEWaspNetworkLagTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
  .CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(World->WorldType);Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;GI->Init();
 ON_SCOPE_EXIT { GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 auto* Pawn=World->SpawnActor<APawn>();auto* Root=NewObject<UCapsuleComponent>(Pawn);Root->InitCapsuleSize(22,88);
 Pawn->SetRootComponent(Root);Root->RegisterComponent();Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);
 PC->bUseEnterWorldLoadScreen=false;PC->bUsePortalTransitionOnTeleport=false;
 auto* CountLimit=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Network.PumpMaxPackets"));
 auto* TimeLimit=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Network.PumpBudgetMs"));
 const int32 SavedCount=CountLimit->GetInt();const float SavedTime=TimeLimit->GetFloat();
 CountLimit->Set(16,ECVF_SetByCode);TimeLimit->Set(0.f,ECVF_SetByCode);
 ON_SCOPE_EXIT { CountLimit->Set(SavedCount,ECVF_SetByCode);TimeLimit->Set(SavedTime,ECVF_SetByCode); };

 for(double RttMs : {0.,200.,2000.,23000.})
 {
  Session->Disconnect();Session->PlayerGuid=0x7100F000;Session->State=EACESessionState::InWorld;
  FACEWorldObject Self;Self.Guid=Session->PlayerGuid;Self.bIsSelf=Self.bIsPlayer=Self.bHasPosition=true;
  Self.ItemType=ACEItemType::Creature;Self.Position.CellId=0x0143015F;Self.Position.Location=FVector(43.304642,-68.413086,-.002981);
  Self.bHasPhysicsTimestamps=true;
  Session->WorldObjects.Add(Self.Guid,Self);Session->PlayerPosition=Self.Position;
  for(int32 I=0;I<120;++I)
  {
   FACEWorldObject Wasp;ACECreatureFixtures::Apply(Wasp,2);Wasp.Guid=Self.Guid+I+1;
   Wasp.Position=Self.Position;Wasp.bHasPosition=Wasp.bHasPhysicsTimestamps=true;Session->WorldObjects.Add(Wasp.Guid,Wasp);
  }
  PC->PredictedPose=Self.Position;PC->bHavePredictedPose=PC->bLocalPredicting=PC->bSyncPossessedPawn=true;
  PC->LastAppliedTeleportSeq=PC->LastAppliedForcePositionSeq=0;
  PC->PredictedPose.SetAceFacingFromUnrealDir2D(FVector(1,0,0));
  Pawn->SetActorLocation(Self.Position.ToUnrealLocation(100)+FVector(0,0,88));Pawn->SetActorRotation(PC->PredictedPose.ToUnrealQuat());
  int32 Motions=0,Corrections=0,PeakBuffered=0,MaxDispatch=0;double LargestCorrection=0;
  TMap<int32,uint16> PresentedStamps;
  const auto MotionHandle=Session->OnMotionUpdate.AddLambda([&](int32 Guid,const FACEObjectMotionState&)
  {
   const uint16 Presented=Session->WorldObjects[Guid].PhysicsTimestamps[ACEPhysicsTimeStamp::Movement];
   TestTrue(TEXT("Delayed wasp movement cannot replay an older pose"),Presented>PresentedStamps.FindOrAdd(Guid));
   PresentedStamps.Add(Guid,Presented);++Motions;
  });
  const auto PositionHandle=Session->OnPositionUpdate.AddLambda([&](int32 Guid,const FACEPosition& Position)
  {
   const FVector Before=Pawn->GetActorLocation();const FQuat Heading=Pawn->GetActorQuat();
   PC->HandlePositionUpdate(Guid,Position);++Corrections;
   LargestCorrection=FMath::Max(LargestCorrection,FVector::Dist(Before,Pawn->GetActorLocation()));
   TestTrue(TEXT("Delayed correction applies authoritative feet"),PC->PredictedPose.ToUnrealLocation(100).Equals(Position.ToUnrealLocation(100),.01));
   TestTrue(TEXT("Delayed correction preserves local facing"),Pawn->GetActorQuat().Equals(Heading,.0001));
  });
  ON_SCOPE_EXIT { Session->OnPositionUpdate.Remove(PositionHandle);Session->OnMotionUpdate.Remove(MotionHandle); };
  FACEWaspLagLink Link(*Session);if(!TestTrue(TEXT("Loopback lag transport opens"),Link.Open()))return false;
  Link.RttMs=RttMs;Link.JitterMs=RttMs>0?80:0;Link.RecoveryEvery=RttMs>0?97:0;Link.RecoveryMs=800;
  int32 QueuedMotions=0,QueuedCorrections=0;uint16 Stamp=0,Force=0;
  double Now=0;
  // Virtual time makes the 23-second case fast and deterministic. This test
  // checks bounded work and reconciliation; rendered FPS is measured separately.
  for(int32 Frame=0;Frame<1800;++Frame,Now+=1./60.)
  {
   if(Frame%30==0)
   {
    ++Stamp;
    for(int32 I=0;I<120;++I){Link.Queue(FACEWaspLagLink::Cast(Self.Guid+I+1,Stamp),Now);++QueuedMotions;}
    if(Frame%120==0){++Force;Link.Queue(FACEWaspLagLink::Position(Self.Guid,PC->PredictedPose,Force,Force),Now);++QueuedCorrections;}
   }
   // Simulated local input advances independently of network arrivals.
   PC->PredictedPose.Location.X+=.02;Pawn->SetActorLocation(PC->PredictedPose.ToUnrealLocation(100)+FVector(0,0,88));
   Link.Advance(Now);const int32 Before=Motions+Corrections;Session->PollSockets();
   MaxDispatch=FMath::Max(MaxDispatch,Motions+Corrections-Before);PeakBuffered=FMath::Max(PeakBuffered,Link.BufferedGameplay());
   if(RttMs==23000 && Now<10)TestEqual(TEXT("Long lag cannot deliver corrections early"),Corrections,0);
  }
  for(int32 Frame=0;Frame<3600 && !Link.IsDrained();++Frame,Now+=1./60.)
  {
   Link.Advance(Now);const int32 Before=Motions+Corrections;Session->PollSockets();
   MaxDispatch=FMath::Max(MaxDispatch,Motions+Corrections-Before);
  }
  TestTrue(TEXT("Latency and recovery cannot bypass the per-frame dispatch cap"),MaxDispatch<=16);
  TestTrue(TEXT("Reliable backlog drains after the link recovers"),Link.IsDrained());
  if(RttMs==0)TestEqual(TEXT("Lossless link delivers every wasp cast"),Motions,QueuedMotions);
  TestTrue(TEXT("Loss recovery never duplicates presented casts"),Motions<=QueuedMotions);
  for(int32 I=0;I<120;++I)
   TestEqual(TEXT("Every wasp finishes on its latest authoritative motion"),PresentedStamps.FindRef(Self.Guid+I+1),Stamp);
  TestEqual(TEXT("All authoritative corrections arrive exactly once"),Corrections,QueuedCorrections);
  TestEqual(TEXT("Synthetic link did not drop writes"),Link.SendErrors,0);
  if(RttMs>0)
  {
   TestTrue(TEXT("Test exercised a gameplay backlog"),PeakBuffered>0);
   TestTrue(TEXT("Delayed authoritative corrections visibly rubber-band the pawn"),LargestCorrection>10);
  }
  AddInfo(FString::Printf(TEXT("RTT %.0f ms: casts=%d corrections=%d largest=%.1f cm peak backlog=%d max dispatch=%d"),
   RttMs,Motions,Corrections,LargestCorrection,PeakBuffered,MaxDispatch));
 }
 return true;
}
#endif
