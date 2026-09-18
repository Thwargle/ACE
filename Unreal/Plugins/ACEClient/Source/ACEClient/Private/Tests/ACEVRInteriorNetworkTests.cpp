#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACETerrainPresenterComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRInteriorNetworkTest, "ACE.VR.InteriorNetwork",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEVRInteriorNetworkTest::RunTest(const FString&)
{
 const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
 auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI);
 Context.OwningGameInstance = GI; Context.SetCurrentWorld(World); GI->Init();
 auto* Client = GI->GetSubsystem<UACEClientSubsystem>(); auto Session = Client->GetSession();
 Session->PlayerGuid = 12345; Session->State = EACESessionState::InWorld;
 auto* PC = World->SpawnActor<AACEPlayerController>(); PC->Client = Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine)); World->AddController(PC);
 PC->bEnterWorldLoading = false; PC->bWorldRevealActive = false; PC->bUsePortalTransitionOnTeleport = false;
 auto* Pawn = World->SpawnActor<APawn>(); auto* Capsule = NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(48, 88); Pawn->SetRootComponent(Capsule); Pawn->AddInstanceComponent(Capsule);
 Capsule->RegisterComponent(); Pawn->SetActorEnableCollision(false); PC->Possess(Pawn);
 auto* PresenterOwner = World->SpawnActor<AActor>();
 auto* Presenter = NewObject<UACETerrainPresenterComponent>(PresenterOwner);
 PresenterOwner->AddInstanceComponent(Presenter); Presenter->RegisterComponent();
 Presenter->Client = Client; Presenter->TrackedPlayerGuid = 12345;
 FACEPosition Local; Local.CellId = 0xC98C010B; Local.Location = FVector(90, 70, 20);
 const FVector LocalFeet = Local.ToUnrealLocation(100), LocalCenter = LocalFeet + FVector(0,0,88);
 auto* Wall = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Wall);
 Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(1,200,200));
 Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block);
 Box->RegisterComponent(); Wall->SetActorLocation(LocalCenter + FVector(59,0,0));
 // The stale server pose slightly overlaps a wall. It must not invoke the
 // portal-arrival radial placement search while crossing ordinary room cells.
 for (bool Predicting : {false, true}) for (bool PresenterFirst : {false, true})
 for (auto Cells : {TPair<uint32,uint32>(0xC98C010B,0xC98C0109),
                   TPair<uint32,uint32>(0xC98C010B,0xC98C0028),
                   TPair<uint32,uint32>(0xC98C0028,0xC98C010B)})
 {
  Local.CellId = Cells.Key; FACEPosition Lagged = Local; Lagged.CellId = Cells.Value;
  Lagged.SetLocationFromUnreal(LocalFeet + FVector(15,0,0),100);
  PC->PredictedPose = Local; PC->bHavePredictedPose = true; PC->bLocalPredicting = Predicting;
  PC->bHaveLastServerPose = false; Pawn->SetActorLocation(LocalCenter);
  Presenter->LastKnownCellId = Cells.Key; Presenter->LastCenterLB = int32(Cells.Key >> 16);
  Presenter->bLastEnvSyncComplete = true;
  Session->SetLocalPosition(Lagged); // UpdatePosition parser writes this before broadcasting.
  if (PresenterFirst) Presenter->HandlePositionUpdate(12345,Lagged);
  PC->HandlePositionUpdate(12345,Lagged);
  if (!PresenterFirst) Presenter->HandlePositionUpdate(12345,Lagged);
  TestTrue(TEXT("Ordinary interior/doorway updates never reposition a predicted body"), Pawn->GetActorLocation().Equals(LocalCenter,.001));
  TestEqual(TEXT("Local collision solver retains its resolved cell"), PC->PredictedPose.CellId, Local.CellId);
  TestEqual(TEXT("Streaming follows that same local cell regardless of delegate order"), Presenter->LastKnownCellId, Cells.Key);
  TestTrue(TEXT("A stale cell update does not invalidate completed interior residency"), Presenter->bLastEnvSyncComplete);
  TestTrue(TEXT("Authoritative anchor remains available for reconciliation"), PC->bHaveLastServerPose && PC->LastServerPose.CellId == Lagged.CellId);
 }
 // A genuine force-position update remains authoritative, even across cells.
 Local.CellId = 0xC98C010B; PC->PredictedPose = Local; PC->bHavePredictedPose = true;
 PC->bLocalPredicting = true; Pawn->SetActorLocation(LocalCenter); PC->bJumpAirborne = true;
 FACEPosition Forced = Local; Forced.CellId = 0xC98C0109;
 Forced.SetLocationFromUnreal(LocalFeet + FVector(-120,0,40),100);
 ++Session->ForcePositionSeq; Session->SetLocalPosition(Forced);
 FACEPosition Presentation;
 TestFalse(TEXT("Pending forced placement cannot be hidden by presentation delegate order"), PC->TryGetLocallyPredictedPosition(Presentation));
 PC->HandlePositionUpdate(12345,Forced);
 TestTrue(TEXT("Explicit force-position applies its exact authoritative location"),
  Pawn->GetActorLocation().Equals(Forced.ToUnrealLocation(100)+FVector(0,0,88),.001));
 TestFalse(TEXT("Forced correction clears the prior jump"), PC->bJumpAirborne);
 // Teleport sequence changes still relocate immediately and invalidate movement.
 FACEPosition Teleport = Forced; Teleport.CellId = 0xC98C0028;
 Teleport.SetLocationFromUnreal(LocalFeet + FVector(-600,0,0),100);
 ++Session->TeleportSeq; Session->SetLocalPosition(Teleport);
 TestFalse(TEXT("A pending teleport exposes the destination instead of stale prediction"), PC->TryGetLocallyPredictedPosition(Presentation));
 PC->HandlePositionUpdate(12345,Teleport);
 TestTrue(TEXT("A real teleport still arrives at the server destination"),
  Pawn->GetActorLocation().Equals(Teleport.ToUnrealLocation(100)+FVector(0,0,88),.001));
 TestEqual(TEXT("Teleport sequence is committed"), PC->LastAppliedTeleportSeq, Session->TeleportSeq);
 TestFalse(TEXT("Teleport clears previous local movement"), PC->bLocalPredicting);
 Session->State = EACESessionState::Disconnected; Session->PlayerGuid = 0;
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
}
#endif
