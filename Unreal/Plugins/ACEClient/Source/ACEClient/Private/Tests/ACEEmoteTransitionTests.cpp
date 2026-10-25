#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACETestDatDirectory.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatMotionPlayer.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEInputBindings.h"
#include "GameFramework/PlayerInput.h"
#include "ACESession.h"
#include "Engine/LocalPlayer.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEmoteTransitionTest, "ACE.RetailParity.EmoteTransitions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEEmoteTransitionTest::RunTest(const FString&)
{
 FACEDatDatabase Portal;
 if (!TestTrue(TEXT("Retail DAT opens"), Portal.Open(FPaths::Combine(ACETestDatDirectory(), TEXT("client_portal.dat"))))) return false;
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
 if (!TestTrue(TEXT("World DAT subsystem opens"), GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(ACETestDatDirectory()))) return false;
 FACEWorldObject Obj; Obj.Guid = 12345; Obj.SetupId = 0x02000001; Obj.MotionTableId = 0x09000001; Obj.bIsPlayer = true;
 auto* Actor = World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Obj, 100, false);
 auto* App = Actor->Appearance.Get();
 // Bronze Statue of a Golem (19276) sleeps in NonCombat, then wakes into
 // HandCombat. Retail GetObjectSequence always exits the old substate before
 // changing style, even when a style-wide combat link exists.
 {
  FACEDatMotionPlayer Golem(&Portal);
  if (!TestTrue(TEXT("Stock golem motion table loads"), Golem.SetMotionTable(0x090000EF))) return false;
  auto Link = [&](uint32 From, uint32 To) -> const FACEDatMotionData*
  {
   for (uint32 Key : {(Style << 16) | (From & 0xFFFFFFu), Style << 16})
    if (const auto* Links = Golem.MotionTable->Links.Find(Key))
     if (const auto* Data = Links->Find(To)) return Data;
   return nullptr;
  };
  const auto* Rise = Link(ACEMotion::Sleeping, ACEMotion::Ready);
  if (!TestNotNull(TEXT("Golem has an authored rising animation"), Rise)) return false;
  TArray<FACEDatAnimData> ExpectedClips = Rise->Anims;
  if (const auto* Enter = Link(ACEMotion::Ready, ACEMotion::StanceHandCombat)) ExpectedClips.Append(Enter->Anims);
  TArray<FACEDatAnimData> ActualClips;
  TestTrue(TEXT("Golem sleeping-to-combat transition resolves"), Golem.FindTransitionAnims(ACEMotion::Sleeping, ACEMotion::StanceHandCombat, ActualClips, Style));
  TestEqual(TEXT("Combat entry retains every rising clip"), ActualClips.Num(), ExpectedClips.Num());
  for (int32 I = 0; I < FMath::Min(ActualClips.Num(), ExpectedClips.Num()); ++I)
   TestEqual(TEXT("Combat entry plays the retail clip order"), ActualClips[I].AnimId, ExpectedClips[I].AnimId);
  float Duration = 0;
  for (const auto& Clip : ExpectedClips) Duration += Golem.GetAnimDataDuration(Clip);
  AddInfo(FString::Printf(TEXT("Golem activation: %d clips, %.3f seconds"), ExpectedClips.Num(), Duration));
  for (float Dt : {1.f/30, 1.f/90}) for (bool SpawnAsleep : {false, true})
  {
   FACEWorldObject Statue; Statue.Guid = 19276; Statue.SetupId = 0x020007CA; Statue.MotionTableId = 0x090000EF;
   Statue.Scale = 1.5f; Statue.ItemType = ACEItemType::Creature;
   Statue.InitialMotionStyle = Style; Statue.InitialMotionCommand = SpawnAsleep ? ACEMotion::Sleeping : 0;
   auto* Body = World->SpawnActor<AACEWorldEntityActor>(); Body->InitializeFromObject(Statue, 100, true);
   auto* Pose = Body->Appearance.Get(); Pose->SetPreviewCapture(true);
   if (!SpawnAsleep)
   {
    FACEObjectMotionState Sleep; Sleep.CurrentStyle = Style; Sleep.ForwardCommand = ACEMotion::Sleeping;
    Body->ApplyMotionState(Sleep);
    for (int Frame = 0; Frame < 600 && !Pose->bHoldActionFinal; ++Frame) Pose->TickComponent(Dt, LEVELTICK_All, nullptr);
   }
   TestTrue(TEXT("Spawn and heartbeat both establish the resting cycle"), Pose->ActionCommand == ACEMotion::Sleeping && Pose->bHoldActionFinal);
   // ApplyMotionState is the receiving end of UpdateMotion. Ready repeats
   // must neither cancel nor restart the combat-stance activation.
   FACEObjectMotionState Wake; Wake.CurrentStyle = ACEMotion::StanceHandCombat; Wake.ForwardCommand = ACEMotion::Ready;
   Body->ApplyMotionState(Wake);
   double MaxError = 0; bool Completed = false;
   for (int Frame = 1; Frame <= FMath::CeilToInt((Duration+1.f)/Dt); ++Frame)
   {
    const float Time = Pose->AnimTime + Dt;
    Body->ApplyMotionState(Wake);
    Pose->TickComponent(Dt, LEVELTICK_All, nullptr);
    TArray<FTransform> Expected; int32 Count = 0; bool Finished = false;
    Golem.EvaluateAnimSequence(ExpectedClips, Time, Pose->GetPartCount(), Expected, 100, Count, false, &Finished);
    if (Time > .3f && !Finished)
     for (int Part = 0; Part < Count; ++Part)
     {
      const auto Actual = Pose->GetPartMesh(Part)->GetRelativeTransform();
      MaxError = FMath::Max(MaxError, FVector::Distance(Actual.GetTranslation(), Expected[Part].GetTranslation()));
      MaxError = FMath::Max(MaxError, Actual.GetRotation().AngularDistance(Expected[Part].GetRotation()));
     }
    if (Pose->ActionCommand == 0) { Completed = true; TestTrue(TEXT("Golem cannot skip its rising time"), Finished); break; }
   }
   TestTrue(TEXT("Golem rising body follows the authored frames"), MaxError < .05);
   TestTrue(TEXT("Golem activation completes without further input"), Completed);
   // Combat can arrive while a heartbeat's lay-down entry is still playing.
   // Finish that entry first, then rise and change stance; never pop upright.
   Pose->ClearActionMotion(); Pose->SetPreferredStyle(Style); Pose->ClearActionMotion();
   FACEObjectMotionState Sleep; Sleep.CurrentStyle = Style; Sleep.ForwardCommand = ACEMotion::Sleeping;
   Body->ApplyMotionState(Sleep); Pose->TickComponent(Dt, LEVELTICK_All, nullptr);
   const float EntryTime = Pose->AnimTime;
   Body->ApplyMotionState(Wake);
   TestTrue(TEXT("Activation preserves an unfinished lay-down entry"), Pose->ActionCommand == ACEMotion::Sleeping && Pose->AnimTime == EntryTime);
   TestTrue(TEXT("Combat stance queues behind lay-down"), Pose->PendingActionCommands.Contains(ACEMotion::StanceHandCombat));
   bool SawActivation = false;
   for (int Frame = 0; Frame < FMath::CeilToInt(15.f/Dt) && Pose->ActionCommand; ++Frame)
   {
    Body->ApplyMotionState(Wake); Pose->TickComponent(Dt, LEVELTICK_All, nullptr);
    SawActivation |= Pose->ActionCommand == ACEMotion::StanceHandCombat && Pose->ActionFromCommand == ACEMotion::Sleeping;
   }
   TestTrue(TEXT("Interrupted lay-down plays activation and reaches combat"), SawActivation && Pose->ActionCommand == 0 && Pose->PendingActionCommands.IsEmpty());
   Body->Destroy();
  }
 }
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
   // RawMotionState and InterpretedMotionState keep sidestep/turn separate
   // from the held forward substate, including during its entry link.
   App->ClearActionMotion();App->SetLocomotionInput(0,0,false,1);
   App->PlayActionMotion(Pose,1,Style,true);Tick();
   const float BeforeStrafe=App->AnimTime;
   App->SetLocomotionInput(0,1,true,1);
   TestTrue(TEXT("Strafing does not queue a stand-up during emote entry"),
    App->ActionCommand==Pose && App->AnimTime==BeforeStrafe && App->PendingActionCommands.IsEmpty());
   for(int Frame=0;Frame<FMath::CeilToInt(12.f/Dt)&&!App->bHoldActionFinal;++Frame)Tick();
   // The completed entry link hands off to its held cycle on the next tick.
   Tick();
   for(float Strafe:{-1.f,0.f,1.f})
   {
    App->SetLocomotionInput(0,Strafe,true,1);Tick();
    TestTrue(TEXT("A held pose survives sidestep start/reverse/release"),App->ActionCommand==Pose&&App->bHoldActionFinal);
    TArray<FTransform> Expected;int32 Count=0;
    TestTrue(TEXT("Persistent substate has a DAT cycle"),Player.EvaluateMotion(Pose,App->AnimTime,App->GetPartCount(),Expected,100,Count,nullptr,nullptr,Style,true));
    for(int Part=0;Part<Count;++Part)
    {
     const auto Actual=App->GetPartMesh(Part)->GetRelativeTransform();
     if(!TestTrue(FString::Printf(TEXT("Sidestep preserves authored cycle %08X, setup %08X, part %d at %.0f Hz"),Pose,Setup,Part,1.f/Dt),
      Actual.GetTranslation().Equals(Expected[Part].GetTranslation(),.04001)
      && Actual.GetRotation().Equals(Expected[Part].GetRotation(),.000011)))return false;
    }
   }
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
 // RecipeManager sends one freshly stamped ClapHands for every combine.
 // Replay it at rest; retain the guard for non-action state echoes.
 for (float Dt : {1.f/30, 1.f/90})
 {
  App->ClearActionMotion(); App->SetPreferredStyle(Style); App->SetLocomotionInput(0,0,false,1);
  for (int Combine=0; Combine<4; ++Combine)
  {
   App->PlayActionMotion(0x1300007eu,1,Style,true,true);
   TestEqual(TEXT("Each combine restarts its authored gesture"),App->AnimTime,0.f);
   TestFalse(TEXT("Each combine releases the previous final pose"),App->bHoldActionFinal);
   App->TickComponent(Dt,LEVELTICK_All,nullptr);
   const float Playing=App->AnimTime;
   App->PlayActionMotion(0x1300007eu,1,Style,true,false);
   TestEqual(TEXT("State echo does not restart an in-flight gesture"),App->AnimTime,Playing);
   for(int Frame=0; Frame<FMath::CeilToInt(10.f/Dt) && App->ActionCommand; ++Frame)App->TickComponent(Dt,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Repeated combine returns to its idle cycle without walking"),App->ActionCommand==0 && !App->bHoldActionFinal);
  }
  App->ClearActionMotion();App->PlayActionMotion(0x1300007eu,1,Style,true,true);
  App->TickComponent(Dt,LEVELTICK_All,nullptr);
  App->PlayActionMotion(0x1300007eu,1,Style,true,true);
  TestEqual(TEXT("Early next combine is queued instead of lost"),App->PendingActionCommands.Num(),1);
  for(int Frame=0;Frame<FMath::CeilToInt(20.f/Dt) && App->ActionCommand;++Frame)App->TickComponent(Dt,LEVELTICK_All,nullptr);
  TestTrue(TEXT("Both queued combines finish"),App->PendingActionCommands.IsEmpty() && App->ActionCommand==0);
 }
 // Multiple distinct combines may be coalesced into one server command list.
 App->ClearActionMotion();
 FACEObjectMotionState Combines;Combines.CurrentStyle=Style;Combines.ActionCommand=0x1300007eu;
 Combines.bNewAction=true;Combines.ActionFollowups={int32(0x1300007eu),int32(0x1300007eu)};
 Actor->ApplyMotionState(Combines);
 TestEqual(TEXT("Batched crafting actions all enter the animation queue"),App->PendingActionCommands.Num(),2);
 for(int Frame=0;Frame<1800 && App->ActionCommand;++Frame)App->TickComponent(1.f/90,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Batched combines finish without movement"),App->PendingActionCommands.IsEmpty()&&App->ActionCommand==0);
 // Landing uses the real Falling->movement link, not a blend from a frozen
 // airborne endpoint. Compare every rendered part against the DAT recovery.
 for(float Forward:{0.f,1.f})for(bool Run:{false,true})for(float Dt:{1.f/30,1.f/90})
 {
  App->ClearActionMotion();App->SetPreferredStyle(Style);App->SetLocomotionInput(Forward,0,Run,1);
  App->SetHeldActionMotion(0x40000015u,Style);
  const uint32 Destination=Forward==0?ACEMotion::Ready:Run?0x44000007u:0x45000005u;
  const auto* Link=Direct(0x40000015u,Destination);
  if(!TestNotNull(TEXT("Retail supplies a landing link for this movement"),Link))return false;
  App->ClearJumpMotionIfAny(true);
  TestTrue(TEXT("Landing plays Falling->current movement"),App->ActionFromCommand==0x40000015u && App->ActionCommand==Destination && App->bActionUsesStateTransition);
  ACELandingMotion::FRootTrack Track;
  TestTrue(TEXT("Landing exposes authored position frames to the movement controller"),App->BuildLandingRootTrack(Track));
  FVector Travel=FVector::ZeroVector;
  for(int32 Step=0;Step<300 && Track.IsActive();++Step) Travel+=Track.Advance(Dt).GetTranslation();
  // Independently integrate the DAT frames crossed by CSequence: High is
  // the positive clip's exit boundary, then the next positive clip's first
  // frame is entered. Hard-coded totals used to count High as another step.
  FTransform ExpectedTravel=FTransform::Identity;
  auto StepRoot=[&](const FTransform& Delta)
  {
   ExpectedTravel.SetTranslation(ExpectedTravel.GetTranslation()+ExpectedTravel.GetRotation().RotateVector(Delta.GetTranslation()));
   ExpectedTravel.SetRotation((ExpectedTravel.GetRotation()*Delta.GetRotation()).GetNormalized());
  };
  for(int32 ClipIndex=0;ClipIndex<Link->Anims.Num();++ClipIndex)
  {
   const auto& Clip=Link->Anims[ClipIndex];const auto* Animation=Player.LoadAnimation(Clip.AnimId);
   if(!TestNotNull(TEXT("Landing root animation exists"),Animation))return false;
   const int32 High=Clip.HighFrame<0?int32(Animation->NumFrames)-1:Clip.HighFrame;
   if(ClipIndex>0 && Clip.Framerate>0 && Animation->PositionFrames.IsValidIndex(Clip.LowFrame))
    StepRoot(FTransform(Animation->PositionFrames[Clip.LowFrame]));
   for(int32 I=Clip.LowFrame;I<=High;++I)
   {
    const int32 Frame=Clip.Framerate<0?High-(I-Clip.LowFrame):I;
    if((Clip.Framerate>0 && Frame==High)||!Animation->PositionFrames.IsValidIndex(Frame))continue;
    const FTransform Delta(Animation->PositionFrames[Frame]);
    StepRoot(Clip.Framerate<0?Delta.Inverse():Delta);
   }
  }
  if(const auto* Cycle=Player.FindCycle(Destination,Style);Cycle && !Cycle->Anims.IsEmpty())
  {
   const auto& Clip=Cycle->Anims[0];const auto* Animation=Player.LoadAnimation(Clip.AnimId);
   if(Clip.Framerate>0 && Animation && Animation->PositionFrames.IsValidIndex(Clip.LowFrame))
    StepRoot(FTransform(Animation->PositionFrames[Clip.LowFrame]));
  }
  TestTrue(*FString::Printf(TEXT("Landing consumes authored displacement forward=%.0f run=%d dt=%.4f actual=%s expected=%s"),
   Forward,Run,Dt,*Travel.ToString(),*ExpectedTravel.GetTranslation().ToString()),Travel.Equals(ExpectedTravel.GetTranslation(),.0001));
  TestTrue(TEXT("Completed recovery cannot apply its final frame twice"),Track.Advance(Dt).Equals(FTransform::Identity));
  const FTransform Root=Actor->GetActorTransform();bool Completed=false;
  for(int32 Frame=0;Frame<120;++Frame)
  {
   const float Time=App->AnimTime+Dt;
   App->ClearJumpMotionIfAny(); // ordinary grounded controller refresh
   App->SetLocomotionInput(Forward,0,Run,1);
   App->TickComponent(Dt,LEVELTICK_All,nullptr);
   TArray<FTransform> Expected;int32 Count=0;bool Finished=false;
   TestTrue(TEXT("Authored landing clips evaluate"),Player.EvaluateAnimSequence(Link->Anims,Time,App->GetPartCount(),Expected,100,Count,false,&Finished));
   if(Time>.08f)for(int32 Part=0;Part<Count;++Part)
   {
    const auto Actual=App->GetPartMesh(Part)->GetRelativeTransform();
    TestTrue(TEXT("Landing recovery follows authored body motion"),Actual.GetTranslation().Equals(Expected[Part].GetTranslation(),.04001)
     && Actual.GetRotation().Equals(Expected[Part].GetRotation(),.000011));
   }
   TestTrue(TEXT("Landing animation cannot change the network/collision root"),Actor->GetActorTransform().Equals(Root));
   if(Finished){Completed=true;break;}
   TestTrue(TEXT("Grounded refresh preserves the unfinished recovery"),App->ActionCommand==Destination && App->bActionUsesStateTransition);
  }
  TestTrue(TEXT("Landing recovery completes and resumes locomotion"),Completed && App->ActionCommand==0);
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
 // Controller jump policy and the actual outgoing movement writer must agree
 // with the rendered pose, not just with an isolated animation helper.
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
 Session->State=EACESessionState::InWorld;Session->PlayerGuid=0x50000001;
 FACEPosition Position;Position.CellId=0x7D640019;Position.Location=FVector(50,100,50);
 Session->SetLocalPosition(Position);
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Root=NewObject<USceneComponent>(Pawn);
 Pawn->SetRootComponent(Root);Root->RegisterComponent();PC->Possess(Pawn);
 auto* LocalApp=NewObject<UACECharacterAppearanceComponent>(Pawn);LocalApp->RegisterComponent();
 LocalApp->ApplyWorldObject(Obj,100,false);
 PC->PredictedPose=Position;PC->bHavePredictedPose=true;
 auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
 auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Emote movement receiver"),false);
 auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
 ON_SCOPE_EXIT {Session->State=EACESessionState::Disconnected;if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);}};
 if(!Receiver||!Receiver->Bind(*Address))return false;
 Receiver->GetAddress(*Address);Session->ServerC2SAddr=Address;
 Session->SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Emote movement sender"),false);
 Session->IssacClient=MakeUnique<FACEIsaac>(123u);
 auto WireForward=[&]()
 {
  if(!TestEqual(TEXT("One movement packet per input change"),Session->CachedC2SPackets.Num(),1))return 0u;
  FACEBinaryReader R(Session->CachedC2SPackets.CreateConstIterator()->Value.Payload);R.Skip(24);
  TestEqual(TEXT("Emotes and sidesteps use retail MoveToState"),R.ReadUInt32(),ACEGameAction::MoveToState);
  const uint32 Flags=R.ReadUInt32();R.Skip(8);
  TestEqual(TEXT("Held substates do not enter the one-shot action list"),Flags>>11,0u);
  return (Flags&ACERawMotionFlags::ForwardCommand)?R.ReadUInt32():ACEMotion::Ready;
 };
 for(uint32 Pose:{ACEMotion::Sleeping,ACEMotion::Sitting,ACEMotion::Crouch,0x43000145u})
 {
  LocalApp->ClearActionMotion();LocalApp->SetLocomotionInput(0,0,false,1);LocalApp->SetHeldActionMotion(Pose,Style);
  Session->CachedC2SPackets.Reset();Session->SendSoulEmoteMotion(Pose);
  TestEqual(TEXT("Held pose is serialized as the retail forward command"),WireForward(),Pose);
  for(float Strafe:{1.f,-1.f,0.f})
  {
   Session->CachedC2SPackets.Reset();Session->SendMoveToState(0,Strafe,.5f,true);
   TestEqual(TEXT("Turn and sidestep packets retain the current pose"),WireForward(),Pose);
  }
  if(Pose!=0x43000145u)
  {
   Session->CachedC2SPackets.Reset();PC->BeginJumpCharge();PC->ReleaseJump(0,0);
   TestFalse(TEXT("Resting pose cannot start a jump charge"),PC->bJumpCharging);
   TestFalse(TEXT("Resting pose never becomes airborne"),PC->bJumpAirborne);
   TestTrue(TEXT("Rejected jump sends neither a jump nor fabricated Ready"),Session->CachedC2SPackets.IsEmpty());
   TestEqual(TEXT("Rejected jump leaves the rendered pose intact"),LocalApp->ActionCommand,Pose);
   Session->SendMoveToState(1,0,0,true);LocalApp->SetLocomotionInput(1,0,true,1);
   Session->CachedC2SPackets.Reset();PC->BeginJumpCharge();PC->ReleaseJump(1,0);
   TestTrue(TEXT("Unfinished stand-up link still blocks jumping"),Session->CachedC2SPackets.IsEmpty()&&!PC->bJumpAirborne);
   for(int Frame=0;Frame<1200&&LocalApp->ActionCommand;++Frame)LocalApp->TickComponent(1.f/90,LEVELTICK_All,nullptr);
   TestFalse(TEXT("Completed get-up permits the next jump"),PC->IsJumpBlockedByMotion(false));
  }
  else
  {
   PC->BeginJumpCharge();
   TestTrue(TEXT("Play Possum can charge without being forced to stand"),PC->bJumpCharging&&LocalApp->ActionCommand==Pose);
   TestFalse(TEXT("A held emote is not a Ready standing-long-jump"),PC->bStandingJumpLocked);
   PC->ReleaseJump(0,0);TestTrue(TEXT("Retail permits jumping from Play Possum"),PC->bJumpAirborne);
   PC->bJumpAirborne=false;LocalApp->ClearJumpMotionIfAny(true,Session->GetCurrentForwardCommand());
   for(int Frame=0;Frame<1200&&!LocalApp->bHoldActionFinal;++Frame)LocalApp->TickComponent(1.f/90,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Landing reapplies the preserved forward emote"),LocalApp->ActionCommand==Pose&&LocalApp->bHoldActionFinal);
  }
  Session->CachedC2SPackets.Reset();Session->SendStopMovement();
  TestEqual(TEXT("Explicit Stop clears the forward pose"),WireForward(),ACEMotion::Ready);
 }
 // Release rechecks the current state: starting charge while Ready does not
 // permit a jump if a lay-down arrives before the key is released.
 LocalApp->ClearActionMotion();Session->CurrentForwardCommand=ACEMotion::Ready;
 PC->BeginJumpCharge();LocalApp->SetHeldActionMotion(ACEMotion::Sleeping,Style);
 Session->CurrentForwardCommand=ACEMotion::Sleeping;Session->CachedC2SPackets.Reset();PC->ReleaseJump(0,0);
 TestTrue(TEXT("Pose change during charging rejects release without changing animation"),
  !PC->bJumpAirborne&&Session->CachedC2SPackets.IsEmpty()&&LocalApp->ActionCommand==ACEMotion::Sleeping);
 // Reading is a server-owned substate, not a short gesture followed by running.
 LocalApp->ClearActionMotion();LocalApp->SetLocomotionInput(1,0,true,1);
 const FKey ForwardKey=ACEInputBindings::Get(EKeys::W,0).Key;
 PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(ForwardKey,IE_Pressed,1.f));
 PC->PlayerInput->ProcessInputStack({},.016f,false);
 TestTrue(TEXT("Fixture presses the configured forward binding"),ACEInputBindings::Down(PC,EKeys::W));
 PC->MovementKeyPressOrder.Add(ForwardKey,++PC->MovementKeySequence);PC->bAutoRun=true;
 PC->bServerMoveToActive=true;PC->ServerMoveToTargetGuid=123;PC->ServerMoveToFailDistance=15.f;
 FACEObjectMotionState Reading;Reading.ForwardCommand=0x1c;Reading.ActionCommand=0x4000001c;Reading.CurrentStyle=Style;
 Session->CachedC2SPackets.Reset();PC->HandleMotionUpdate(Session->PlayerGuid,Reading);
 TestTrue(TEXT("Server reading clears autorun and suspends already-held movement"),!PC->bAutoRun&&PC->ReadingMovementSequence>0);
 TestFalse(TEXT("An old approach cannot keep walking through Reading"),PC->bServerMoveToActive);
 auto Forward=[&](){return ACEInputBindings::MovementAxis(PC,EKeys::W,EKeys::S,PC->MovementKeyPressOrder,FKey(),FKey(),PC->ReadingMovementSequence);};
 TestEqual(TEXT("Holding forward cannot steal server reading control every frame"),Forward(),0.f);
 TestEqual(TEXT("Reading immediately stops recorded locomotion"),LocalApp->LocomotionForward,0.f);
 TestTrue(TEXT("Accepting server reading does not send a cancelling movement packet"),Session->CachedC2SPackets.IsEmpty());
 for(int Frame=0;Frame<600;++Frame)LocalApp->TickComponent(1.f/60,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Reading remains posed until the server completes it"),LocalApp->ActionCommand==0x4000001cu&&LocalApp->bHoldActionFinal);
 PC->MovementKeyPressOrder.Add(ForwardKey,++PC->MovementKeySequence);
 TestTrue(TEXT("A fresh movement press can explicitly reclaim control like retail"),Forward()>0.f);
 PC->HandleUseDone(0);
 TestEqual(TEXT("Use completion releases the suspended movement generation"),PC->ReadingMovementSequence,uint64(0));
 for(int Frame=0;Frame<600&&LocalApp->ActionCommand;++Frame)LocalApp->TickComponent(1.f/60,LEVELTICK_All,nullptr);
 TestEqual(TEXT("Completion plays reading-to-ready and leaves no stuck pose"),LocalApp->ActionCommand,0u);
 PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(ForwardKey,IE_Released,0.f));
 PC->PlayerInput->ProcessInputStack({},.016f,false);
 Pawn->Destroy();PC->Destroy();
 AddInfo(FString::Printf(TEXT("Verified %d DAT emote transitions across %d held poses, both human setups, at 30 and 90 Hz."), Cases, Poses.Num()));
 Actor->Destroy();
 return !HasAnyErrors();
}
#endif
