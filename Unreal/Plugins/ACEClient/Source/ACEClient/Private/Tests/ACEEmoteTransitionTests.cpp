#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatMotionPlayer.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEmoteTransitionTest, "ACE.RetailParity.EmoteTransitions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEEmoteTransitionTest::RunTest(const FString&)
{
 FACEDatDatabase Portal;
 if (!TestTrue(TEXT("Retail DAT opens"), Portal.Open(TEXT("C:/Turbine/Asheron's Call/client_portal.dat")))) return false;
 FACEDatMotionPlayer Player(&Portal);
 if (!TestTrue(TEXT("Retail human motion table loads"), Player.SetMotionTable(0x09000001))) return false;
 constexpr uint32 Style = ACEMotion::StanceNonCombat;
 auto Direct = [&](uint32 From, uint32 To) -> const FACEDatMotionData*
 {
  const auto* Links = Player.MotionTable->Links.Find((Style << 16) | (From & 0xFFFFFF));
  return Links ? Links->Find(To) : nullptr;
 };
 const auto* StandUp = Direct(ACEMotion::Sleeping, ACEMotion::Ready);
 if (!TestNotNull(TEXT("Retail supplies a lying-to-standing link"), StandUp)) return false;
 if (!TestEqual(TEXT("Get-up contains three authored intermediate segments"), StandUp->Anims.Num(), 3)) return false;
 TestEqual(TEXT("Lying to sitting animation"), StandUp->Anims[0].AnimId, 0x03000477u);
 TestEqual(TEXT("Sitting to crouching animation"), StandUp->Anims[1].AnimId, 0x0300046Du);
 TestEqual(TEXT("Crouching to standing animation"), StandUp->Anims[2].AnimId, 0x03000466u);
 for (const auto& Clip : StandUp->Anims) TestTrue(TEXT("Get-up plays each authored clip in reverse"), Clip.Framerate < 0);

 const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
 auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI);
 Context.OwningGameInstance = GI; Context.SetCurrentWorld(World); GI->OnWorldChanged(nullptr, World); GI->Init();
 ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
 if (!TestTrue(TEXT("World DAT subsystem opens"), GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
 FACEWorldObject Obj; Obj.Guid = 12345; Obj.SetupId = 0x02000001; Obj.MotionTableId = 0x09000001; Obj.bIsPlayer = true;
 auto* Actor = World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Obj, 100, false);
 auto* App = Actor->Appearance.Get();
 TArray<uint32> Poses;
 for (const auto& Pair : Player.MotionTable->Links.FindChecked((Style << 16) | (ACEMotion::Ready & 0xFFFFFF)))
  if ((Pair.Key & 0x40000000u) && Pair.Key != ACEMotion::Dead && ACEMotion::IsHeldRestCommand(Pair.Key)
   && Direct(Pair.Key, ACEMotion::Ready)) Poses.Add(Pair.Key);
 TestTrue(TEXT("The coverage includes held chat poses beyond sit/sleep/crouch"), Poses.Num() > 3);
 int32 Cases = 0;
 for (uint32 Setup : {0x02000001u, 0x0200004eu}) for (float Dt : {1.f/90, 1.f/30})
 {
  Obj.SetupId = Setup;
  if (!TestTrue(TEXT("Real avatar geometry builds"), App->ApplyWorldObject(Obj, 100, false))) return false;
  auto Tick = [&]() { App->TickComponent(Dt, LEVELTICK_All, nullptr); };
  // Compare displayed part transforms to the actual DAT clips, not a straight
  // interpolation between endpoint poses. Exercise local movement and the
  // remote Ready path, plus direct and default-state pose transitions.
  auto CheckTransition = [&](uint32 From, uint32 To, const TArray<FACEDatAnimData>& Clips, bool Move)
  {
   App->ClearActionMotion(); App->SetLocomotionInput(0, 0, false, 1);
   App->SetHeldActionMotion(From, Style);
   const FTransform CollisionRoot = Actor->GetActorTransform();
   if (To != ACEMotion::Ready) App->PlayActionMotion(To, 1, Style, true);
   else if (Move) App->SetLocomotionInput(1, 0, true, 1);
   else App->CancelHeldActionMotion();
   TestTrue(TEXT("Changing a held pose starts its source-specific transition"), App->ActionCommand == To && App->ActionFromCommand == From && !App->bHoldActionFinal);
   bool Completed = false;
   for (int32 Frame = 0; Frame < FMath::CeilToInt(12.f/Dt); ++Frame)
   {
    const float BeforeTime = App->AnimTime;
    const float ExpectedTime = BeforeTime + Dt;
    if (To == ACEMotion::Ready)
    {
     if (Move) App->SetLocomotionInput(1, 0, true, 1);
     else App->CancelHeldActionMotion();
     TestEqual(TEXT("Repeated movement/Ready does not restart get-up"), App->AnimTime, BeforeTime);
    }
    Tick();
    TArray<FTransform> Expected; int32 Count = 0; bool Finished = false;
    TestTrue(TEXT("Reference retail clips evaluate"), Player.EvaluateAnimSequence(Clips, ExpectedTime, App->GetPartCount(), Expected, 100, Count, false, &Finished));
    if (ExpectedTime > .08f)
     for (int32 Part = 0; Part < Count; ++Part)
     {
      Expected[Part].SetScale3D(App->BindTransforms[Part].GetScale3D());
      const auto Actual = App->GetPartMesh(Part)->GetRelativeTransform();
      // ApplyPartTransform suppresses sub-0.04 cm translations and sub-1e-5
      // quaternion changes. Preserve that render-work optimization in this test.
      if (!TestTrue(FString::Printf(TEXT("%08X -> %08X: part %d follows retail at %.3f"), From, To, Part, ExpectedTime),
       Actual.GetTranslation().Equals(Expected[Part].GetTranslation(), .04001)
       && Actual.GetRotation().Equals(Expected[Part].GetRotation(), .000011)
       && Actual.GetScale3D().Equals(Expected[Part].GetScale3D(), .000011))) return;
     }
    TestTrue(TEXT("Pose transitions never move the collision root"), Actor->GetActorTransform().Equals(CollisionRoot));
    if (Finished)
    {
     TestTrue(TEXT("Exit resumes locomotion; a pose transition holds its destination"), To == ACEMotion::Ready
      ? App->ActionCommand == 0 : App->ActionCommand == To && App->bHoldActionFinal);
     Completed = true; break;
    }
    TestTrue(TEXT("Intermediate segments cannot be skipped"), App->ActionCommand == To && !App->bHoldActionFinal);
   }
   TestTrue(TEXT("Authored sequence completes"), Completed); ++Cases;
  };
  for (uint32 Pose : Poses)
  {
   CheckTransition(Pose, ACEMotion::Ready, Direct(Pose, ACEMotion::Ready)->Anims, true);
   CheckTransition(Pose, ACEMotion::Ready, Direct(Pose, ACEMotion::Ready)->Anims, false);
  }
  for (uint32 From : {ACEMotion::Sleeping, ACEMotion::Sitting, ACEMotion::Crouch})
   for (uint32 To : {ACEMotion::Sleeping, ACEMotion::Sitting, ACEMotion::Crouch})
    if (From != To) CheckTransition(From, To, Direct(From, To)->Anims, false);
  const uint32 OtherPose = 0x43000141u;
  TestNull(TEXT("This pose pair requires routing through Ready"), Direct(ACEMotion::Sleeping, OtherPose));
  TArray<FACEDatAnimData> ViaReady = StandUp->Anims;
  if (const auto* Entry = Direct(ACEMotion::Ready, OtherPose))
  {
   ViaReady.Append(Entry->Anims);
   CheckTransition(ACEMotion::Sleeping, OtherPose, ViaReady, false);
  }
  else AddError(TEXT("Missing reference entry for the fallback route"));

  App->ClearActionMotion(); App->SetLocomotionInput(0, 0, false, 1);
  App->PlayActionMotion(ACEMotion::Sleeping, 1, Style, true); Tick();
  const float EntryTime = App->AnimTime;
  App->SetLocomotionInput(1, 0, true, 1);
  App->CancelHeldActionMotion(); App->CancelHeldActionMotion();
  TestTrue(TEXT("Moving mid-entry preserves the unfinished retail link"), App->ActionCommand == ACEMotion::Sleeping && App->AnimTime == EntryTime);
  TestEqual(TEXT("Repeated Ready queues exactly one exit"), App->PendingActionCommands.Num(), 1);
  bool SawExit = false;
  for (int32 Frame=0; Frame<FMath::CeilToInt(12.f/Dt) && App->ActionCommand != 0; ++Frame)
  {
   Tick();
   SawExit |= App->ActionCommand == ACEMotion::Ready && App->ActionFromCommand == ACEMotion::Sleeping;
  }
  TestTrue(TEXT("The pending entry finishes, plays get-up, then runs"), SawExit && App->ActionCommand == 0 && App->LocomotionForward == 1);

  App->SetHeldActionMotion(ACEMotion::Sleeping, Style);
  App->PlayActionMotion(ACEMotion::Sitting, 1, Style, true); Tick();
  App->PlayActionMotion(ACEMotion::Crouch, 1, Style, true);
  App->PlayActionMotion(ACEMotion::Ready, 1, Style, false);
  for (int32 Frame=0; Frame<FMath::CeilToInt(12.f/Dt) && App->ActionCommand != 0; ++Frame) Tick();
  TestTrue(TEXT("Queued pose changes do not get stuck holding an intermediate state"), App->ActionCommand == 0 && App->PendingActionCommands.IsEmpty());

  App->PlayActionMotion(ACEMotion::Sleeping, 1, Style, true); Tick();
  App->PlayActionMotion(0x13000087u, 1, Style, true); // Wave action, not a held substate.
  App->CancelHeldActionMotion();
  bool SawWave = false;
  for (int32 Frame=0; Frame<FMath::CeilToInt(12.f/Dt) && App->ActionCommand != 0; ++Frame)
  {
   Tick(); SawWave |= App->ActionCommand == 0x13000087u;
  }
  TestTrue(TEXT("A gesture queued during lay-down retains the get-up and does not block Ready"), SawWave && App->ActionCommand == 0);
 }
 // The state-link path must not interfere with the separate scarab/cast queue,
 // immediate airborne motions or the final death hold.
 App->SetPreferredStyle(ACEMotion::StanceMagic);
 App->PlayActionMotion(0x1000006fu, 1, ACEMotion::StanceMagic);
 App->PlayActionMotion(0x40000034u, 1, ACEMotion::StanceMagic);
 TestEqual(TEXT("Cast still queues behind the scarab windup"), App->PendingActionCommands.Num(), 1);
 bool SawCast = false;
 for (int32 Frame=0; Frame<1800 && App->ActionCommand != 0; ++Frame)
 {
  App->TickComponent(1.f/90, LEVELTICK_All, nullptr);
  SawCast |= App->ActionCommand == 0x40000034u;
 }
 TestTrue(TEXT("Scarab/cast sequence still plays and returns to locomotion"), SawCast && App->ActionCommand == 0);
 App->SetHeldActionMotion(ACEMotion::Sleeping, Style); App->CancelHeldActionMotion();
 App->PlayActionMotion(0x40000015u, 1, Style);
 TestTrue(TEXT("Takeoff interrupts get-up rather than deferring airborne animation"), App->ActionCommand == 0x40000015u && App->PendingActionCommands.IsEmpty());
 App->PlayActionMotion(ACEMotion::Dead, 1, Style);
 for (int32 Frame=0; Frame<900 && !App->bHoldActionFinal; ++Frame) App->TickComponent(1.f/90, LEVELTICK_All, nullptr);
 App->SetLocomotionInput(1, 0, true, 1);
 TestTrue(TEXT("Death finishes and cannot be cancelled by movement"), App->ActionCommand == ACEMotion::Dead && App->bHoldActionFinal);
 AddInfo(FString::Printf(TEXT("Verified %d DAT emote transitions across %d held poses, both human setups, at 30 and 90 Hz."), Cases, Poses.Num()));
 Actor->Destroy();
 return !HasAnyErrors();
}
#endif
