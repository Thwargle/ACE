#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEInputBindings.h"
#include "ACERetailTurn.h"
#include "ACECameraRetail.h"
#include "ACECameraSettings.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPvPCombatParityTest, "ACE.RetailParity.PvPCombat",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FACEPvPCombatParityTest::RunTest(const FString&)
{
 const FString OriginalSettings=GGameUserSettingsIni;
 GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/PvPCombatFixture.ini");
 FConfigFile Config;Config.NoSave=false;Config.bCanSaveAllSections=true;
 GConfig->SetFile(GGameUserSettingsIni,&Config);ACEInputBindings::Reload();
 ON_SCOPE_EXIT {GGameUserSettingsIni=OriginalSettings;ACEInputBindings::Reload();};
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Capsule->InitCapsuleSize(22,88);Pawn->SetRootComponent(Capsule);Capsule->RegisterComponent();
 Pawn->SetActorEnableCollision(false);PC->Possess(Pawn);PC->PlayerInput=NewObject<UPlayerInput>(PC);
 Session->PlayerGuid=0x50000001;Session->State=EACESessionState::InWorld;
 FACEPosition Start;Start.CellId=0x7D640019;Start.Location=FVector(50,100,50);
 Session->PlayerPosition=PC->PredictedPose=Start;PC->bHavePredictedPose=true;
 Pawn->SetActorRotation(Start.ToUnrealQuat());
 FACEWorldObject Self;Self.Guid=Session->PlayerGuid;Self.bIsPlayer=Self.bIsSelf=Self.bHasPosition=true;
 Self.ItemType=ACEItemType::Creature;Self.Position=Start;
 Self.ObjectDescriptionFlags=ACEObjectDescFlag::PlayerKiller;
 FACEWorldObject Enemy=Self;Enemy.Guid=0x50000002;Enemy.bIsSelf=false;Enemy.Name=TEXT("PK opponent");
 Enemy.Position.Location+=FVector(10,0,0);
 Session->WorldObjects.Add(Self.Guid,Self);Session->WorldObjects.Add(Enemy.Guid,Enemy);Client->SelectObject(Enemy.Guid);

 auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
 auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("PvP loopback receiver"),false);
 const auto Address=Sockets->CreateInternetAddr();bool Valid=false;
 Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
 if(!Receiver || !Receiver->Bind(*Address))return false;
 Receiver->GetAddress(*Address);
 Session->SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("PvP loopback sender"),false);
 Session->ServerC2SAddr=Address;Session->IssacClient=MakeUnique<FACEIsaac>(123u);
 ON_SCOPE_EXIT {Session->State=EACESessionState::Disconnected;Receiver->Close();Sockets->DestroySocket(Receiver);};
 auto Packets=[&]
 {
  TArray<TPair<uint32,TArray<uint8>>> Result;
  TArray<uint32> Sequences;Session->CachedC2SPackets.GetKeys(Sequences);Sequences.Sort();
  for(uint32 Sequence:Sequences)
  {
   const auto& Payload=Session->CachedC2SPackets[Sequence].Payload;
   FACEBinaryReader R(Payload);R.Skip(24);const uint32 Action=R.ReadUInt32();
   TArray<uint8> Body;Body.Append(Payload.GetData()+28,Payload.Num()-28);Result.Emplace(Action,Body);
  }
  return Result;
 };
 ACEInputBindings::BeginEdit();ACEInputBindings::Defaults();ACEInputBindings::Commit();
 auto Key=[&](FKey K,bool Down)
 {
  const auto Event=FInputKeyEventArgs::CreateSimulated(K,Down?IE_Pressed:IE_Released,Down?1.f:0.f);
  PC->InputKey(Event);PC->PlayerInput->InputKey(Event);PC->PlayerInput->ProcessInputStack({},1.f/90,false);
 };
 // ClientMagicSystem::FreeHandsAndCastSpell orders MoveToState before the cast.
 // Test a real spell from the DAT against a player, not a substitute monster.
 Key(EKeys::W,true);PC->bAutoRun=true;Session->CachedC2SPackets.Reset();
 TestTrue(TEXT("Manual PK spell accepts the selected player"),Client->SendCastSpell(1));
 auto CastPackets=Packets();
 TestEqual(TEXT("Manual cast sends a stop and one cast request"),CastPackets.Num(),2);
 if(CastPackets.Num()==2)
 {
  TestEqual(TEXT("Stop precedes cast on the wire"),CastPackets[0].Key,ACEGameAction::MoveToState);
  TestEqual(TEXT("Targeted cast retains the retail opcode"),CastPackets[1].Key,ACEGameAction::CastTargetedSpell);
  FACEBinaryWriter Expected;Expected.WriteUInt32(Enemy.Guid);Expected.WriteUInt32(1);
  TestTrue(TEXT("Targeted spell body is byte-for-byte retail target then spell"),Expected.GetData()==CastPackets[1].Value);
 }
 TestFalse(TEXT("Starting a cast clears autorun"),PC->bAutoRun);
 TestEqual(TEXT("Held forward command stays cleared until pressed again"),
  ACEInputBindings::MovementAxis(PC,EKeys::W,EKeys::S,PC->MovementKeyPressOrder,FKey(),FKey(),PC->MovementSuppressionSequence),0.f);
 Key(EKeys::W,false);Key(EKeys::W,true);
 TestEqual(TEXT("New movement remains available for retail casting movement"),
  ACEInputBindings::MovementAxis(PC,EKeys::W,EKeys::S,PC->MovementKeyPressOrder,FKey(),FKey(),PC->MovementSuppressionSequence),1.f);
 Key(EKeys::W,false);
 TestTrue(TEXT("Casting has not teleported local facing"),Pawn->GetActorQuat().Equals(Start.ToUnrealQuat()));
 PC->bServerControlledMotion=true;Session->CachedC2SPackets.Reset();
 TestTrue(TEXT("A subsequent cast retains the retail action path"),Client->SendCastSpell(1));
 TestEqual(TEXT("MaybeStopCompletely does not cancel a server-owned cast with another stop"),Packets().Num(),1);
 PC->bServerControlledMotion=false;

 auto* Binder=NewObject<UACEUIGameplayBinder>(GI);Binder->Client=Client;Binder->PlayerController=PC;
 Binder->LastSelection=Client->GetSelectedObject();Binder->RequestedAttackPower=.5f;Binder->bCombatAutoRepeat=false;
 // A tab click must not resend magic stance (ACE cancels the running spell).
 Binder->CombatMode=int32(ACECombatMode::Magic);Session->SetCurrentStance(ACEMotion::StanceMagic);
 PC->bServerControlledMotion=true;Session->CachedC2SPackets.Reset();
 for(int32 Tab=1;Tab<=8;++Tab)
 {
  TestTrue(TEXT("Spell tab click is handled"),Binder->HandleNamedClick(FString::Printf(TEXT("Spellcast_Tab%d"),Tab)));
  TestEqual(TEXT("Selected spell bank changes during casting"),Client->GetActiveSpellBar(),Tab-1);
 }
 TestTrue(TEXT("Changing banks sends no combat, movement or cancel packet"),Packets().IsEmpty());
 TestTrue(TEXT("Switching banks preserves server-owned casting"),PC->bServerControlledMotion);
 for(uint32 Status:{ACEObjectDescFlag::PlayerKiller,ACEObjectDescFlag::PkLiteStatus})
 for(int32 Mode:{int32(ACECombatMode::Melee),int32(ACECombatMode::Missile)})
 {
  Session->WorldObjects[Self.Guid].ObjectDescriptionFlags=Status;
  Session->WorldObjects[Enemy.Guid].ObjectDescriptionFlags=Status;
  Session->bServerAttackInProgress=false;Session->LastAttackError=0;
  Session->SetCurrentStance(Mode==int32(ACECombatMode::Melee)?ACEMotion::StanceHandCombat:ACEMotion::StanceBowCombat);
  Binder->CombatMode=Mode;Binder->bCombatRequestSent=false;Binder->bCombatRepeatActive=false;
  Binder->bCombatMovementBlocked=false;Binder->LastCombatEventRevision=Session->GetCombatEventRevision();
  PC->bServerControlledMotion=false;PC->bJumpCharging=true;PC->bStandingJumpLocked=true;
  Session->CachedC2SPackets.Reset();Binder->BeginCombatPowerCharge(ACEAttackHeight::Medium,true);
  TestFalse(TEXT("Attack entry finishes pending jump charge without launching"),PC->bJumpCharging);
  TestEqual(TEXT("Charging sends only the retail stop"),Packets().Num(),1);
  Binder->CombatPowerBuildStartTime=FPlatformTime::Seconds()-.6;
  Binder->ReleaseCombatPowerCharge();Binder->TickCombatAutoAttack(0);
  const auto AttackPackets=Packets();
  TestEqual(TEXT("PK/PKLite attack has only stop then attack"),AttackPackets.Num(),2);
  if(AttackPackets.Num()==2)
  {
   TestEqual(TEXT("No AutoPos or facing shortcut before player attack"),AttackPackets[0].Key,ACEGameAction::MoveToState);
   TestEqual(TEXT("Attack uses retail melee or missile request"),AttackPackets[1].Key,
    Mode==int32(ACECombatMode::Melee)?ACEGameAction::TargetedMeleeAttack:ACEGameAction::TargetedMissileAttack);
   FACEBinaryReader R(AttackPackets[1].Value);
   TestEqual(TEXT("Player target GUID survives serialization"),R.ReadUInt32(),uint32(Enemy.Guid));
   TestEqual(TEXT("Retail requested attack height survives serialization"),R.ReadUInt32(),ACEAttackHeight::Medium);
   TestTrue(TEXT("Held charge survives serialization"),FMath::IsNearlyEqual(R.ReadFloat(),.6f,.03f));
  }
  TestTrue(TEXT("Attack request does not instantly face opponent"),PC->PredictedPose.GetAcQuat().Equals(Start.GetAcQuat()));
 }

 // EndAttackRequest uses actual charge in AdvancedCombatUI, even below the
 // slider. Normal mode waits for the slider. Verify both melee/missile wires.
 for(bool Advanced:{false,true}) for(int32 Mode:{int32(ACECombatMode::Melee),int32(ACECombatMode::Missile)})
 {
  Client->SendSetSingleCharacterOption(0x0C,Advanced);
  Session->bServerAttackInProgress=false;
  Session->SetCurrentStance(Mode==int32(ACECombatMode::Melee)?ACEMotion::StanceHandCombat:ACEMotion::StanceBowCombat);
  Binder->CombatMode=Mode;Binder->bCombatRequestSent=Binder->bCombatRepeatActive=false;
  Binder->LastCombatEventRevision=Session->GetCombatEventRevision();Binder->RequestedAttackPower=.8f;
  Binder->bCombatAutoRepeat=true;
  PC->bServerControlledMotion=false;
  Binder->BeginCombatPowerCharge(ACEAttackHeight::Medium,true);Session->CachedC2SPackets.Reset();
  Binder->CombatPowerBuildStartTime=FPlatformTime::Seconds()-.2;
  Binder->ReleaseCombatPowerCharge();Binder->TickCombatAutoAttack(0);
  if(!Advanced)
  {
   TestTrue(TEXT("Normal quick release waits for the selected mark"),Packets().IsEmpty());
   Binder->CombatPowerBuildStartTime=FPlatformTime::Seconds()-.81;
   Binder->TickCombatAutoAttack(0);
  }
  const auto Requests=Packets();
  TestEqual(TEXT("Each combat mode sends exactly one released attack"),Requests.Num(),1);
  if(Requests.Num()==1)
  {
   FACEBinaryReader R(Requests[0].Value);R.Skip(8);
   TestTrue(TEXT("Advanced release sends actual charge; normal sends the mark"),
    FMath::IsNearlyEqual(R.ReadFloat(),Advanced?.2f:.8f,.03f));
  }
  Binder->RequestedAttackPower=.9f;Session->CachedC2SPackets.Reset();
  FACEBinaryWriter Done;Done.WriteUInt32(Self.Guid);Done.WriteUInt32(1);Done.WriteUInt32(ACEGameEvent::AttackDone);Done.WriteUInt32(0);
  FACEBinaryReader DoneReader(Done.GetData());Session->HandleGameEvent(DoneReader);Binder->TickCombatAutoAttack(0);
  TestEqual(TEXT("Advanced repeat keeps released power; normal repeat follows the slider"),Packets().Num(),Advanced?0:1);
 }
 Client->SendSetSingleCharacterOption(0x0C,false);Binder->RequestedAttackPower=.5f;Binder->bCombatAutoRepeat=false;

 // Actual interpreted Reload/Aim packets block manual missile charging.
 // Ready restarts a held build without credit for the waiting time.
 uint16 ReadySequence=10;
 auto MissilePose=[&](uint32 Style,uint16 Command)
 {
  FACEBinaryWriter W;W.WriteUInt32(Self.Guid);W.WriteUInt16(0);
  W.WriteUInt16(ReadySequence++);W.WriteUInt16(1);W.WriteUInt8(0);W.Align();
  W.WriteUInt8(0);W.WriteUInt8(0);W.WriteUInt16(uint16(Style));
  W.WriteUInt32(2);W.WriteUInt16(Command);W.Align();
  FACEBinaryReader R(W.GetData());Session->HandleUpdateMotion(R);
 };
 for(uint32 Style:{ACEMotion::StanceBowCombat,ACEMotion::StanceCrossbow,ACEMotion::StanceSling,
     ACEMotion::StanceThrownWeapon,ACEMotion::StanceAtlatl,ACEMotion::StanceThrownShield})
 for(uint16 Command:{uint16(0x16),uint16(0x1e)})
 {
  PC->bServerControlledMotion=true;Session->bServerAttackInProgress=false;
  Binder->bCombatRequestSent=Binder->bCombatRepeatActive=false;Binder->CombatMode=int32(ACECombatMode::Missile);
  MissilePose(Style,Command);Session->CachedC2SPackets.Reset();
  Binder->BeginCombatPowerCharge(ACEAttackHeight::Medium,true);
  TestFalse(TEXT("Reload and aim cannot begin a missile power build"),Binder->bCombatPowerCharging);
  Binder->CombatPowerBuildStartTime=FPlatformTime::Seconds()-5;
  Binder->TickCombatAutoAttack(0);
  TestTrue(TEXT("Reload waiting sends no stop or premature shot"),Packets().IsEmpty());
  TestEqual(TEXT("Reload waiting does not fill the power bar"),Binder->CombatPowerOrAccuracy,0.f);
  MissilePose(Style,3);Binder->TickCombatAutoAttack(0);
  TestTrue(TEXT("Ready restarts the held attack"),Binder->bCombatPowerCharging);
  TestTrue(TEXT("Reload time is not credited as charge time"),Binder->CombatPowerOrAccuracy<.01f);
  Binder->CombatPowerBuildStartTime=FPlatformTime::Seconds()-.6;
  Binder->ReleaseCombatPowerCharge();Binder->TickCombatAutoAttack(0);
  TestEqual(TEXT("Each launcher sends one shot after Ready and real charge"),Packets().Num(),1);
 }

 // The keyboard target-cycle path must cancel ACE's old repeat and retain
 // the new target through Reload and AttackDone, without a second key press.
 FACEWorldObject Other=Enemy;Other.Guid=Enemy.Guid+1;Other.Position.Location.X+=5;
 Other.ObjectDescriptionFlags=Session->WorldObjects[Enemy.Guid].ObjectDescriptionFlags;
 Session->WorldObjects.Add(Other.Guid,Other);
 Client->OnSelectionChanged.AddDynamic(Binder,&UACEUIGameplayBinder::HandleSelectionChanged);
 ON_SCOPE_EXIT {Client->OnSelectionChanged.RemoveDynamic(Binder,&UACEUIGameplayBinder::HandleSelectionChanged);};
 auto AttackDone=[&]
 {
  FACEBinaryWriter W;W.WriteUInt32(Self.Guid);W.WriteUInt32(1);W.WriteUInt32(ACEGameEvent::AttackDone);W.WriteUInt32(0);
  FACEBinaryReader R(W.GetData());Session->HandleGameEvent(R);
 };
 for(uint32 Style:{ACEMotion::StanceBowCombat,ACEMotion::StanceCrossbow,ACEMotion::StanceAtlatl})
 {
  Binder->CancelCombatAttack();Binder->bCombatAutoRepeat=true;Client->SelectObject(Enemy.Guid);
  Binder->LastSelection=Client->GetSelectedObject();Binder->LastCombatAttackTarget=Enemy.Guid;
  Binder->bCombatRepeatActive=Binder->bCombatRequestSent=true;
  Session->bServerAttackInProgress=true;MissilePose(Style,0x1e);
  Session->CachedC2SPackets.Reset();Binder->CycleKeyboardSelection(TEXT("Player"),1);
  TestEqual(TEXT("Keyboard cycle selected the other opponent"),Client->GetSelectedObject().Guid,Other.Guid);
  auto CancelPackets=Packets();
  CancelPackets.RemoveAll([](const auto& Packet){return Packet.Key==ACEGameAction::QueryHealth;});
  TestEqual(TEXT("Target change sends one cancel before replacing the shot"),CancelPackets.Num(),1);
  if(CancelPackets.Num()==1)TestEqual(TEXT("Old repeat cancelled on the wire"),CancelPackets[0].Key,ACEGameAction::CancelAttack);
  Session->CachedC2SPackets.Reset();Binder->TickCombatAutoAttack(0);AttackDone();Binder->TickCombatAutoAttack(0);
  TestTrue(TEXT("Retarget survives acknowledgement while reloading"),Binder->bCombatRetargetPending && Packets().IsEmpty());
  MissilePose(Style,3);Binder->TickCombatAutoAttack(0);
  auto Replacement=Packets();TestEqual(TEXT("Ready sends a single replacement attack"),Replacement.Num(),1);
  if(Replacement.Num()==1)
  {
   TestEqual(TEXT("Replacement uses missile opcode"),Replacement[0].Key,ACEGameAction::TargetedMissileAttack);
   FACEBinaryReader R(Replacement[0].Value);TestEqual(TEXT("Replacement targets the newly selected creature"),R.ReadUInt32(),uint32(Other.Guid));
  }
  Session->CachedC2SPackets.Reset();
  auto Health=Client->GetSelectedObject();Health.HealthFraction=.5f;Binder->HandleSelectionChanged(Health);
  Binder->TickCombatAutoAttack(0);Binder->TickCombatAutoAttack(0);
  TestTrue(TEXT("Health refreshes and subsequent ticks do not resend attacks"),Packets().IsEmpty());
  Client->SelectObject(Self.Guid);TestFalse(TEXT("Selecting self cancels rather than queues an attack"),Binder->bCombatRetargetPending);
  Binder->CancelCombatAttack();AttackDone();
 }
 Binder->bCombatAutoRepeat=false;Client->SelectObject(Enemy.Guid);
 Session->CachedC2SPackets.Reset();Binder->CycleKeyboardSelection(TEXT("Player"),1);
 auto SelectionOnly=Packets();SelectionOnly.RemoveAll([](const auto& Packet){return Packet.Key==ACEGameAction::QueryHealth;});
 TestTrue(TEXT("Keyboard selection with auto-repeat off does not attack"),SelectionOnly.IsEmpty() && !Binder->bCombatRetargetPending);
 Binder->bCombatRetargetPending=true;Binder->BeginCombatPowerCharge(ACEAttackHeight::High,true);
 TestFalse(TEXT("An explicit attack supersedes a queued retarget"),Binder->bCombatRetargetPending);
 Binder->CancelCombatAttack();Binder->bCombatRetargetPending=true;Binder->ShowPluginAttack(Other.Guid,ACEAttackHeight::Medium,.5f);
 TestFalse(TEXT("Plugin-issued attack cannot leave a duplicate retarget queued"),Binder->bCombatRetargetPending);
 Binder->CancelCombatAttack();Client->SelectObject(Enemy.Guid);

 // UCM selects targets before finishing stance/equipment transitions. The
 // native power-bar observer must not attack on that selection, bypassing the
 // plugin's own line-of-sight, pursuit-distance and looting decisions.
 Client->SendSetSingleCharacterOption(0x00,true);
 for(int32 Mode:{int32(ACECombatMode::Melee),int32(ACECombatMode::Missile)})
 {
  Binder->CombatMode=Mode;Binder->bCombatAutoRepeat=true;
  Client->SelectObject(Enemy.Guid);Binder->ShowPluginAttack(Enemy.Guid,ACEAttackHeight::Medium,.5f);
  Session->bServerAttackInProgress=true;Session->CachedC2SPackets.Reset();
  Client->SelectObject(Other.Guid);AttackDone();Binder->TickCombatAutoAttack(0);
  auto Requests=Packets();Requests.RemoveAll([](const auto& Packet){return Packet.Key==ACEGameAction::QueryHealth;});
  TestFalse(TEXT("Plugin target selection does not queue a native repeat"),Binder->bCombatRetargetPending);
  TestEqual(TEXT("Plugin switching targets only cancels the old attack"),Requests.Num(),1);
  if(Requests.Num()==1)TestEqual(TEXT("Only plugin policy may send the replacement attack"),Requests[0].Key,ACEGameAction::CancelAttack);
 }

 // Death-driven auto-selection must not inherit a held/repeating attack.
 // Retail can select across rooms/radar range; selection alone cannot charge
 // toward that creature or fire at the wall separating it from the player.
 Client->SendSetSingleCharacterOption(0x0D,true);
 for(int32 Mode:{int32(ACECombatMode::Melee),int32(ACECombatMode::Missile)})
 for(int32 DeathNotice:{0,1,2})
 {
  Binder->CancelCombatAttack();AttackDone();
  Session->WorldObjects.Add(Enemy.Guid,Enemy);
  Session->WorldObjects[Enemy.Guid].ObjectDescriptionFlags=ACEObjectDescFlag::PlayerKiller;
  Binder->CombatMode=Mode;Binder->bCombatAutoRepeat=true;
  Client->SelectObject(Enemy.Guid);Binder->PendingAutoTargetLoss=0;
  Binder->LastSelection=Client->GetSelectedObject();Binder->LastCombatAttackTarget=Enemy.Guid;
  Binder->bCombatRepeatActive=Binder->bCombatRequestSent=true;
  Binder->bCombatAttackHeld=Binder->bCombatAttackRequestPending=Binder->bCombatPowerCharging=true;
  Session->bServerAttackInProgress=true;
  MissilePose(ACEMotion::StanceBowCombat,3);
  Session->CachedC2SPackets.Reset();
  if(DeathNotice==0)
  {
   // Health can reach zero before the death animation or deletion arrives.
   auto Dead=Client->GetSelectedObject();Dead.bShowHealth=true;Dead.HealthFraction=0;
   Binder->HandleSelectionChanged(Dead);
   // Session's ordinary health notification also updates its selection cache.
   Session->SelectedObject=Dead;
  }
  else if(DeathNotice==1)
  {
   Session->WorldObjects[Enemy.Guid].bDying=true;Client->SelectObject(0);
  }
  else
  {
   FACEBinaryWriter W;W.WriteUInt32(Enemy.Guid);FACEBinaryReader R(W.GetData());Session->HandleObjectDelete(R);
  }
  Binder->TickCombatAutoAttack(0);AttackDone();
  for(int32 Frame=0;Frame<4;++Frame)Binder->TickCombatAutoAttack(.016f);
  TestEqual(TEXT("Death auto-target selects the next eligible opponent"),Client->GetSelectedObject().Guid,Other.Guid);
  TestFalse(TEXT("Automatic selection clears held, pending and repeated attacks"),
   Binder->bCombatRetargetPending || Binder->bCombatRepeatActive || Binder->bCombatAttackRequestPending || Binder->bCombatAttackHeld);
  auto Automatic=Packets();Automatic.RemoveAll([](const auto& Packet){return Packet.Key==ACEGameAction::QueryHealth;});
  TestEqual(TEXT("Post-kill auto-target sends only cancellation, never a new attack"),Automatic.Num(),1);
  if(Automatic.Num()==1)TestEqual(TEXT("Post-kill packet cancels old attack"),Automatic[0].Key,ACEGameAction::CancelAttack);
 }
 Session->WorldObjects.Add(Enemy.Guid,Enemy);Client->SendSetSingleCharacterOption(0x0D,false);
 Binder->CancelCombatAttack();AttackDone();Client->SelectObject(Enemy.Guid);

 // Independently expected 1.5 rad/s from the retail TurnRight motion. Rate
 // multiplication, shortest direction and completion must not depend on FPS.
 for(float FPS:{30.f,60.f,144.f,240.f})for(float Speed:{.5f,1.f,2.f})for(bool Run:{false,true})
 {
  PC->PredictedPose=Start;
  FACEObjectMotionState Turn;Turn.MovementType=9;Turn.MoveToDesiredHeading=179;Turn.TurnToSpeed=Speed;
  PC->bRunningSent=Run;Session->CachedC2SPackets.Reset();PC->HandleMotionUpdate(Self.Guid,Turn);
  TestTrue(TEXT("Receiving TurnTo sends no immediate position packet"),Packets().IsEmpty());
  const int32 Frames=FMath::RoundToInt(FPS*.5f);
  for(int32 Frame=0;Frame<Frames;++Frame)PC->AdvanceServerTurn(PC->PredictedPose,1.f/FPS);
  TestTrue(TEXT("Half-second turn matches retail angular speed at every render rate"),
   FMath::IsNearlyEqual(Start.GetAcQuat().AngularDistance(PC->PredictedPose.GetAcQuat()),.75*Speed*(Run?1.5:1.),.00003));
  PC->AdvanceServerTurn(PC->PredictedPose,5.f);
  TestFalse(TEXT("Turn completes instead of asymptotically approaching destination"),PC->bServerTurnActive);
  TestTrue(TEXT("Turn stops at the requested heading"),FMath::IsNearlyEqual(PC->PredictedPose.GetAceHeadingDegrees(),179.f,.001f));
 }
 // MovementManager::unpack_movement uses the first heading only when target
 // is absent. A zero offset is meaningful and must not inherit that fallback.
 FACEBinaryWriter TurnPacket;TurnPacket.WriteUInt32(Self.Guid);
 TurnPacket.WriteUInt16(0);TurnPacket.WriteUInt16(1);TurnPacket.WriteUInt16(1);TurnPacket.WriteUInt8(0);TurnPacket.Align();
 TurnPacket.WriteUInt8(8);TurnPacket.WriteUInt8(0);TurnPacket.WriteUInt16(0);
 TurnPacket.WriteUInt32(Enemy.Guid);TurnPacket.WriteFloat(87.f);TurnPacket.WriteUInt32(0x10000);
 TurnPacket.WriteFloat(.5f);TurnPacket.WriteFloat(0.f);
 bool ReceivedTurn=false;
 const auto TurnHandle=Session->OnMotionUpdate.AddLambda([&](int32 Guid,const FACEObjectMotionState& Motion)
 {
  if(Guid!=Self.Guid)return;
  ReceivedTurn=true;
  TestEqual(TEXT("Wire turn keeps zero offset separate from fallback"),Motion.MoveToDesiredHeading,0.f);
  TestEqual(TEXT("Wire turn preserves fallback heading"),Motion.TurnToFallbackHeading,87.f);
  TestEqual(TEXT("Wire turn preserves requested speed"),Motion.TurnToSpeed,.5f);
  PC->HandleMotionUpdate(Guid,Motion);
 });
 FACEBinaryReader TurnReader(TurnPacket.GetData());Session->HandleUpdateMotion(TurnReader);
 TestTrue(TEXT("Retail TurnToObject packet decoded"),ReceivedTurn);
 Session->OnMotionUpdate.Remove(TurnHandle);
 PC->AdvanceServerTurn(PC->PredictedPose,5.f);
 FACEPosition FaceEnemy=Start;FaceEnemy.SetAceFacingFromUnrealDir2D(
  Enemy.Position.ToUnrealLocation(100)-Start.ToUnrealLocation(100));
 TestTrue(TEXT("Resident target uses object bearing, not fallback heading"),PC->PredictedPose.GetAcQuat().Equals(FaceEnemy.GetAcQuat(),.00001f));
 // Explicitly exercise the controller's missing-object fallback too.
 FACEObjectMotionState Missing;Missing.MovementType=8;Missing.MoveToTargetGuid=123;
 Missing.MoveToDesiredHeading=0;Missing.TurnToFallbackHeading=87;PC->HandleMotionUpdate(Self.Guid,Missing);
 PC->AdvanceServerTurn(PC->PredictedPose,5);
 TestTrue(TEXT("Unavailable target uses transmitted absolute heading"),FMath::IsNearlyEqual(PC->PredictedPose.GetAceHeadingDegrees(),87.f,.001f));
 TestTrue(TEXT("A zero speed turn cannot snap the heading"),ACERetailTurn::Advance(Start.GetAcQuat(),PC->PredictedPose.GetAcQuat(),0,1).Equals(Start.GetAcQuat()));

 // Raw commands inherit Run/Walk (HoldKey_Invalid), whereas retail mouse
 // turning explicitly applies Run and permits a raw speed up to 1.5.
 for(bool Run:{false,true})for(bool Mouse:{false,true})
 {
  Session->CachedC2SPackets.Reset();
  Session->SendMoveToState(1,-1,Mouse?1.5f:1.f,Run,true,false,false,Mouse);
  const auto Movement=Packets();
  TestEqual(TEXT("Combined input has one retail movement packet"),Movement.Num(),1);
  if(Movement.Num()!=1)continue;
  FACEBinaryReader R(Movement[0].Value);const uint32 Flags=R.ReadUInt32();
  TestTrue(TEXT("All three axes have raw command and hold fields"),(Flags&0xFFFu)==0x7FFu);
  TestEqual(TEXT("Global hold key represents the user's Run/Walk choice"),R.ReadUInt32(),Run?2u:1u);
  R.ReadUInt32(); // style
  TestEqual(TEXT("Forward remains raw WalkForward"),R.ReadUInt32(),ACEMotion::WalkForward);
  TestEqual(TEXT("Forward inherits the global hold key"),R.ReadUInt32(),0u);R.ReadFloat();
  TestEqual(TEXT("Left strafe retains its raw command"),R.ReadUInt32(),ACEMotion::SideStepLeft);
  TestEqual(TEXT("Strafe inherits the global hold key"),R.ReadUInt32(),0u);R.ReadFloat();
  TestEqual(TEXT("Turn uses the retail modifier"),R.ReadUInt32(),ACEMotion::TurnRight);
  const uint32 Hold=R.ReadUInt32();const float Speed=R.ReadFloat();
  TestEqual(TEXT("Mouse turn explicitly runs; keyboard turn inherits"),Hold,Mouse?2u:0u);
  TestEqual(TEXT("Raw mouse turn retains retail's 1.5 limit"),Speed,Mouse?1.5f:1.f);
  const float ServerRate=Speed*1.5f*((Hold==2||(Hold==0&&Run))?1.5f:1.f);
  TestEqual(TEXT("Server interprets the same angular speed as local prediction"),ServerRate,Mouse?3.375f:Run?2.25f:1.5f);
 }
 for(float FPS:{30.f,60.f,144.f,240.f})
 {
  float Yaw=270.f;
  for(int32 Frame=0;Frame<int32(FPS*.5f);++Frame)
   Yaw-=ACECameraRetail::FollowTurnDegrees(Yaw,1.f/FPS,true,true);
  TestTrue(TEXT("Half-second mouse turn cannot instantly rotate 180 degrees at any FPS"),
   FMath::IsNearlyEqual(270.f-Yaw,FMath::RadiansToDegrees(3.375f)*.5f,.001f));
 }

 Session->CachedC2SPackets.Reset();Session->PlayerPosition=Start;Session->bPositionReportingSuspended=false;
 Session->bHaveReportedPosition=true;Session->LastReportedPosition=Start;Session->bLastReportedContact=Session->bAutoPosContact=true;
 Session->AutoPosTimer=.99f;Session->SendMoveToState(1,0,0,true,true,false);
 Session->TickPositionReporting(.02f);
 TestEqual(TEXT("Movement packet resets periodic AutoPos timer"),Packets().Num(),1);
 for(float FPS:{30.f,60.f,144.f,240.f})
 {
  Session->CachedC2SPackets.Reset();Session->AutoPosTimer=0;
  for(int32 Frame=0;Frame<int32(FPS*3);++Frame)
  {Session->PlayerPosition.Location.X+=1.f/FPS;Session->TickPositionReporting(1.f/FPS);}
  TestTrue(TEXT("Position cadence stays bounded across render rates"),Packets().Num()>=2 && Packets().Num()<=3);
 }
 Session->FlushAutonomousPosition(true);Session->CachedC2SPackets.Reset();
 Session->bMoving=true;Session->bForcePositionReporting=true;Session->TickPositionReporting(5);
 TestTrue(TEXT("Holding movement against a wall does not send unchanged positions"),Packets().IsEmpty());
 Session->bAutoPosContact=false;Session->PlayerPosition.Location.Z+=2;
 Session->TickPositionReporting(5);Session->FlushAutonomousPosition(false);
 TestTrue(TEXT("Ordinary airborne actions cannot replace the retail jump with autonomous positions"),Packets().IsEmpty());

 // Switching from free camera orbit to mouse-facing must turn the character
 // toward the view, not reset the camera to the character's old heading.
 auto* Boom=NewObject<USpringArmComponent>(Pawn);Pawn->AddInstanceComponent(Boom);
 Boom->SetupAttachment(Capsule);Boom->RegisterComponent();Boom->bDoCollisionTest=false;
 PC->bCameraDefaultsCaptured=true;PC->ClearServerMoveTo();PC->bServerControlledMotion=false;
 PC->bServerTurnActive=false;Session->bUseBusy=false;Session->bServerAttackInProgress=false;
 PC->DatGameplayBinder=nullptr;Client->SendSetSingleCharacterOption(0x31,true);
 ACECameraSettings::SetMouseInversion(false,false);
 for(float FPS:{30.f,60.f,144.f})for(float Orbit:{-120.f,179.f})for(bool Toggle:{false,true})
 {
  const float Dt=1.f/FPS;
  PC->PlayerInput->FlushPressedKeys();PC->PlayerInput->ProcessInputStack({},Dt,false);
  PC->SetMouseLookActive(false);PC->UpdateMouseButtons(false,false,false,false);
  ACECameraSettings::SetToggleMouseLook(Toggle);
  PC->bJumpAirborne=PC->bJumpCharging=PC->bStandingJumpLocked=false;
  PC->bHaveLastServerPose=false;PC->bAwaitingUseDone=false;PC->ApproachUseSendCount=0;
  PC->bAutoRun=false;PC->ForwardSent=PC->RightSent=PC->TurnSent=0;PC->bWasMoving=false;
  Session->SetLocalPosition(Start);PC->PredictedPose=Start;PC->bHavePredictedPose=true;
  Pawn->SetActorLocationAndRotation(Start.ToUnrealLocation(100)+FVector(0,0,88),Start.ToUnrealQuat());
  Boom->SetRelativeRotation(FRotator(-20,ACECameraRetail::BoomYawToFaceAcForward,0));
  Key(EKeys::LeftMouseButton,true);PC->UpdateMouseButtons(false,true,false,false);PC->SetMouseLookActive(true);
  PC->ApplyMouseLookDelta(Orbit/ACECameraSettings::GetMouseDegreesPerPixel(),0,Boom);
  const float ViewYaw=Boom->GetComponentRotation().Yaw;
  TestTrue(TEXT("Free orbit preserves the character's facing"),PC->PredictedPose.GetAcQuat().Equals(Start.GetAcQuat()));
  Key(EKeys::RightMouseButton,true);
  for(int32 Frame=0;Frame<int32(FPS*1.25f);++Frame)
  {
   const float BeforeYaw=Pawn->GetActorRotation().Yaw;
   PC->PlayerTick(Dt);
   const float BodyTurn=FMath::Abs(FMath::FindDeltaAngleDegrees(BeforeYaw,Pawn->GetActorRotation().Yaw));
   if(!TestTrue(TEXT("Orbit handoff keeps the chosen world-space camera heading"),
    FMath::Abs(FMath::FindDeltaAngleDegrees(ViewYaw,Boom->GetComponentRotation().Yaw))<.03f))return false;
   if(!TestTrue(TEXT("Orbit handoff respects retail's character turn rate"),
    BodyTurn<=FMath::RadiansToDegrees(ACECameraRetail::MouseTurnRadiansPerSecond)*Dt+.01f))return false;
   if(Frame==0)
   {
    TestTrue(TEXT("Right click starts facing even when toggle mode began in left orbit"),PC->bMouseLookActive&&BodyTurn>.01f);
    Key(EKeys::LeftMouseButton,false);
    if(Toggle)Key(EKeys::RightMouseButton,false);
   }
  }
  TestTrue(TEXT("Character finishes facing the free-orbit view"),
   FMath::Abs(FMath::FindDeltaAngleDegrees(Boom->GetRelativeRotation().Yaw,ACECameraRetail::BoomYawToFaceAcForward))<.03f);
  // A large new mouse delta must not leave several seconds of queued turns.
  PC->CameraInputFrameSeconds=Dt;PC->ApplyMouseLookDelta(100000.f,0,Boom);PC->PlayerTick(Dt);
  const FQuat AfterMouse=PC->PredictedPose.GetAcQuat();PC->PlayerTick(Dt);
  TestTrue(TEXT("Excess new mouse input does not keep turning after the mouse stops"),PC->PredictedPose.GetAcQuat().Equals(AfterMouse,.00001));
  Key(EKeys::RightMouseButton,false);PC->SetMouseLookActive(false);PC->UpdateMouseButtons(false,false,false,false);
 }
 ACECameraSettings::SetToggleMouseLook(false);
 PC->UnPossess();Pawn->Destroy();PC->Destroy();
 return true;
}
#endif
