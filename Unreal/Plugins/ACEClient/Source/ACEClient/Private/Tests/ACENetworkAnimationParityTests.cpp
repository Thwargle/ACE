#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACECreatureFixtures.h"
#include "ACEMotionCommandNames.generated.h"
#include "ACESession.h"
#include "Dat/ACEDatMotionPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACENetworkAnimationParityTest,"ACE.RetailParity.NetworkAnimationParity",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACENetworkAnimationParityTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.SetCurrentWorld(World);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 FString DatDirectory=TEXT("C:/Turbine/Asheron's Call");
 FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),DatDirectory);
 if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(DatDirectory)))return false;
 FACEDatMotionPlayer Reference(Dat->GetPortalDat());
 auto Session=GI->GetSubsystem<UACEClientSubsystem>()->GetSession();Session->PlayerGuid=1;
 FACEPosition P;P.CellId=0xAAB30004;P.Location=FVector(50,50,100);
 P.bHasContactState=true;P.bIsGrounded=true;P.bHasTeleportSequence=true;
 Session->SetLocalPosition(P);
 FACEWorldObject Object;Object.Guid=2;Object.SetupId=0x02000001;Object.MotionTableId=0x09000001;
 Object.ItemType=ACEItemType::Creature;Object.bHasPosition=true;Object.Position=P;
 auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Object,100,true);
 Actor->bClampToGround=false;Actor->SetActorEnableCollision(false);
 auto* App=Actor->Appearance.Get();App->SetPreviewCapture(true);
 Session->WorldObjects.Add(Object.Guid,Object);
 Session->OnMotionUpdate.AddLambda([&](int32,const FACEObjectMotionState& M){Actor->ApplyMotionState(M);});
 ON_SCOPE_EXIT {Session->OnMotionUpdate.Clear();Session->WorldObjects.Reset();};
 uint16 Sequence=0,ActionStamp=0;
 constexpr uint32 Style=ACEMotion::StanceNonCombat;
 auto Header=[&](FACEBinaryWriter& W,uint8 Type)
 {
  W.WriteUInt32(Object.Guid);W.WriteUInt16(0);W.WriteUInt16(++Sequence);W.WriteUInt16(0);
  W.WriteUInt8(0);W.Align();W.WriteUInt8(Type);W.WriteUInt8(0);W.WriteUInt16(uint16(Style&0xffffu));
 };
 auto Receive=[&](FACEBinaryWriter& W){W.Align();FACEBinaryReader R(W.GetData());Session->HandleUpdateMotion(R);};
 auto PoseError=[&](const TArray<FTransform>& Expected,int32 Count)
 {
  double Error=0;
  for(int32 Part=0;Part<Count;++Part)
  {
   const auto Actual=App->GetPartMesh(Part)->GetRelativeTransform();
   Error=FMath::Max(Error,FVector::Distance(Actual.GetTranslation(),Expected[Part].GetTranslation()));
   Error=FMath::Max(Error,Actual.GetRotation().AngularDistance(Expected[Part].GetRotation()));
  }
  return Error;
 };
 // Every motion-bearing retail chat alias must resolve and produce its authored
 // entry/action pose for both human bodies. Packets supply their own speed.
 TArray<FString> Aliases;Dat->GetChatPoseKeys(Aliases);TSet<uint32> Commands;
 for(const auto& Alias:Aliases)
 {
  FString Name,My,Other;Dat->TryGetChatPose(Alias,Name,My,Other);if(Name.IsEmpty())continue;
  auto Normalize=[](FString S){S.ToLowerInline();S.ReplaceInline(TEXT(" "),TEXT(""));S.ReplaceInline(TEXT("_"),TEXT(""));S.ReplaceInline(TEXT("-"),TEXT(""));return S;};
  uint32 Command=0;
  if(TestTrue(TEXT("Every retail chat pose resolves a motion"),ACEMotionName::TryResolve(Normalize(Name),Command)
   || ACEMotionName::TryResolve(Normalize(Alias),Command)))Commands.Add(Command);
 }
 Reference.SetMotionTable(Object.MotionTableId);
 for(uint32 Setup:{0x02000001u,0x0200004eu})
 {
  Object.SetupId=Setup;TestTrue(TEXT("Human emote body builds"),Actor->ApplyDatAppearanceFromObject(Object));
  for(uint32 Command:Commands)for(float Rate:{.5f,1.f,2.f})
  {
   App->ClearActionMotion();App->SetPreferredStyle(Style);App->SetLocomotionInput(0,0,false,1);
   FACEBinaryWriter W;Header(W,0);
   const bool Action=(Command&0x10000000u)!=0;
   W.WriteUInt32(Action?1u<<7:6u);
   W.WriteUInt16(uint16(Command));if(Action)W.WriteUInt16(++ActionStamp);W.WriteFloat(Rate);Receive(W);
   const FString Case=FString::Printf(TEXT("%08X body=%08X rate=%.2f"),Command,Setup,Rate);
   TestEqual(*FString::Printf(TEXT("Wire emote rate %s"),*Case),App->ActionPlayRate,Rate);
   App->TickComponent(.3f,LEVELTICK_All,nullptr);
   TArray<FTransform> Expected;int32 Count=0;bool Finished=false;
   bool Found=Reference.EvaluateLink(ACEMotion::Ready,Command,.3f*Rate,App->GetPartCount(),Expected,100,Count,Finished,nullptr,nullptr,Style);
   if(!Found)Found=Reference.EvaluateMotion(Command,.3f*Rate,App->GetPartCount(),Expected,100,Count,nullptr,nullptr,Style,false,&Finished);
   TestTrue(*FString::Printf(TEXT("Authored emote exists %s"),*Case),Found);
   if(Found)TestTrue(*FString::Printf(TEXT("Rendered emote follows DAT %s"),*Case),PoseError(Expected,Count)<.05);
   for(int32 Frame=0;Frame<1200 && App->ActionCommand && !App->bHoldActionFinal;++Frame)App->TickComponent(1.f/30,LEVELTICK_All,nullptr);
   if(Action)TestTrue(*FString::Printf(TEXT("One-shot returns to idle %s"),*Case),App->ActionCommand==0 && !App->bHoldActionFinal);
   else
   {
    TestTrue(*FString::Printf(TEXT("Held substate remains until replaced %s"),*Case),App->ActionCommand==Command && App->bHoldActionFinal);
    for(int Frame=1;Frame<=5;++Frame)
    {
     App->TickComponent(.2f,LEVELTICK_All,nullptr);
     if(Reference.EvaluateMotion(Command,Frame*.2f*Rate,App->GetPartCount(),Expected,100,Count,nullptr,nullptr,Style,true))
      TestTrue(*FString::Printf(TEXT("Persistent substate follows its authored cycle %s frame=%d"),*Case,Frame),PoseError(Expected,Count)<.05);
    }
    if(Rate==1.f)
    {
     App->ClearActionMotion();App->SetHeldActionMotion(Command,Style);
     App->TickComponent(.3f,LEVELTICK_All,nullptr);
     if(Reference.EvaluateMotion(Command,.3f,App->GetPartCount(),Expected,100,Count,nullptr,nullptr,Style,true))
      TestTrue(*FString::Printf(TEXT("An already-active spawn starts in the authored cycle %s"),*Case),PoseError(Expected,Count)<.05);
    }
   }
  }
 }
 AddInfo(FString::Printf(TEXT("Verified %d retail chat aliases / %d distinct motions on two bodies at three wire speeds"),Aliases.Num(),Commands.Num()));
 // Same interpreted substate with a new speed changes rate without replaying
 // its entry; a stamped one-shot remains a distinct performance.
 App->ClearActionMotion();App->SetPreferredStyle(Style);
 for(float Rate:{.5f,2.f})
 {
  const float Before=App->AnimTime;
  FACEBinaryWriter W;Header(W,0);W.WriteUInt32(6);W.WriteUInt16(uint16(ACEMotion::Sitting&0xffffu));W.WriteFloat(Rate);Receive(W);
  TestEqual(TEXT("Updated held-state speed is applied"),App->ActionPlayRate,Rate);
  TestEqual(TEXT("Updated held-state speed preserves animation progress"),App->AnimTime,Before);
  App->TickComponent(.1f,LEVELTICK_All,nullptr);
 }
 App->ClearActionMotion();
 // Retail keeps the wire speed, including a paused substate and rates below
 // 0.05. Changing that rate must neither advance nor restart the current pose.
 for(float Rate:{.01f,0.f,1.f})
 {
  const float Before=App->AnimTime;
  FACEBinaryWriter W;Header(W,0);W.WriteUInt32(6);W.WriteUInt16(uint16(ACEMotion::Sitting&0xffffu));W.WriteFloat(Rate);Receive(W);
  TestEqual(TEXT("Slow and paused interpreted rates survive the network path"),App->ActionPlayRate,Rate);
  TestEqual(TEXT("Pausing/resuming preserves the current emote frame"),App->AnimTime,Before);
  App->TickComponent(.1f,LEVELTICK_All,nullptr);
  TestTrue(TEXT("Emote clock advances only by the received rate"),FMath::IsNearlyEqual(App->AnimTime,Before+.1f*Rate,.00001f));
 }
 App->ClearActionMotion();
 for(uint16 Command:{uint16(0x87),uint16(0x7e)})
 {
  FACEBinaryWriter W;Header(W,0);W.WriteUInt32(1u<<7);W.WriteUInt16(Command);W.WriteUInt16(++ActionStamp);W.WriteFloat(1);Receive(W);
  App->TickComponent(.1f,LEVELTICK_All,nullptr);
 }
 TestEqual(TEXT("A different stamped gesture cannot truncate an in-flight wave"),App->ActionCommand,0x13000087u);
 TestEqual(TEXT("The next distinct gesture is queued"),App->PendingActionCommands.Num(),1);
 App->ClearActionMotion();
 for(float Rate:{1.f,0.f})
 {
  FACEBinaryWriter W;Header(W,0);W.WriteUInt32(1u<<7);W.WriteUInt16(0x87);W.WriteUInt16(++ActionStamp);W.WriteFloat(Rate);Receive(W);
 }
 TestTrue(TEXT("A queued paused action retains its wire rate"),App->PendingActionPlayRates==TArray<float>{0.f});
 for(int Frame=0;Frame<600 && !App->PendingActionCommands.IsEmpty();++Frame)App->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("The queued paused action starts after its predecessor"),App->PendingActionCommands.IsEmpty() && App->ActionCommand==0x13000087u);
 TestEqual(TEXT("Dequeuing does not impose a minimum action rate"),App->ActionPlayRate,0.f);
 const float PausedTime=App->AnimTime;
 App->TickComponent(.25f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("Paused stamped action holds its animation clock"),App->AnimTime,PausedTime);
 App->ClearActionMotion();
 // Death's authored root movement must share the same paused rate as its mesh.
 const FVector BeforeDeath=Actor->GetActorLocation();
 FACEObjectMotionState PausedDeath;PausedDeath.CurrentStyle=Style;
 PausedDeath.ForwardCommand=PausedDeath.ActionCommand=ACEMotion::Dead;PausedDeath.ActionSpeed=0.f;
 Actor->ApplyMotionState(PausedDeath);
 for(int Frame=0;Frame<30;++Frame){Actor->Tick(1.f/30.f);App->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);}
 TestTrue(TEXT("A paused death cannot displace its body while its mesh is stopped"),Actor->GetActorLocation().Equals(BeforeDeath,.001));
 // TurnRight is the interpreted modifier; TurnLeft canonicalizes to negative
 // speed. Directed TurnTo and MoveTo must drive the same visible footwork.
 for(int32 Model=-2;Model<UE_ARRAY_COUNT(ACECreatureFixtures::Models);++Model)
 {
  if(Model<0){Object.SetupId=0x02000001;Object.MotionTableId=0x09000001;Object.Scale=1;Object.bIsPlayer=Model==-2;}
  else ACECreatureFixtures::Apply(Object,Model);
  Actor->InitializeFromObject(Object,100,true);Actor->bClampToGround=false;Actor->SetActorEnableCollision(false);
  App->SetPreviewCapture(true);Reference.SetMotionTable(Object.MotionTableId);
  for(int FPS:{30,60,144})for(float Turn:{-.5f,.5f,1.5f})
  {
   App->ClearActionMotion();Actor->ApplyACEPosition(P,false);
   FACEBinaryWriter W;Header(W,0);W.WriteUInt32(0x60);W.WriteUInt16(0xd);W.WriteFloat(Turn);Receive(W);
   const FQuat Start=Actor->GetActorQuat();
   double MaxError=0;
   for(int I=0;I<FPS/2;++I)
   {
    Actor->Tick(1.f/FPS);App->TickComponent(1.f/FPS,LEVELTICK_All,nullptr);
    TArray<FTransform> Expected;int32 Count=0;
    if(Reference.EvaluateMotion(ACEMotion::TurnRight,App->AnimTime*Turn,App->GetPartCount(),Expected,100,Count,nullptr,nullptr,Style))
     MaxError=FMath::Max(MaxError,PoseError(Expected,Count));
   }
   TestEqual(TEXT("Remote interpreted turn reaches the animation channel"),App->LocomotionTurn,Turn);
   TestTrue(TEXT("Remote turn pose matches authored DAT parts"),MaxError<.05);
   TestFalse(TEXT("Turning changes the remote physics heading"),Actor->GetActorQuat().Equals(Start));
   FACEBinaryWriter Stop;Header(Stop,0);Stop.WriteUInt32(0);Receive(Stop);
   TestEqual(TEXT("Ready clears remote turn animation without another actor tick"),App->LocomotionTurn,0.f);
  }
  for(uint8 Type:{uint8(8),uint8(9),uint8(7)})
  {
   Actor->RemotePredictRotation=FQuat::Identity;Actor->SetActorRotation(FQuat::Identity);
   FACEBinaryWriter W;Header(W,Type);
   if(Type==7)
   {
    W.WriteUInt32(P.CellId);W.WriteFloat(60);W.WriteFloat(50);W.WriteFloat(100);
    W.WriteUInt32(0x40);W.WriteFloat(0);W.WriteFloat(0);W.WriteFloat(MAX_flt);
    W.WriteFloat(.5f);W.WriteFloat(15);W.WriteFloat(90);W.WriteFloat(1);
   }
   else {if(Type==8){W.WriteUInt32(0);W.WriteFloat(90);}W.WriteUInt32(0);W.WriteFloat(.5f);W.WriteFloat(90);}
   Receive(W);Actor->Tick(1.f/60);App->TickComponent(1.f/60,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Directed turn drives a half-speed turn pose"),FMath::IsNearlyEqual(FMath::Abs(App->LocomotionTurn),.5f));
   const float Time=App->AnimTime;
   Actor->Tick(1.f/60);App->TickComponent(1.f/60,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Directed turn animation clock is not reset every frame"),App->AnimTime>Time);
   for(int I=0;I<1800 && Actor->RemoteMotion.MovementType;++I){Actor->Tick(1.f/60);App->TickComponent(1.f/60,LEVELTICK_All,nullptr);}
   TestEqual(TEXT("Completed directed movement releases turn footwork"),App->LocomotionTurn,0.f);
  }
 }
 // Retail chooses interpolation using viewer distance, not correction size.
 Actor->RemoteMotion={};Actor->RemoteWalkingMotion.Reset();Actor->RemoteCycleRootTrack.Reset();
 Actor->bHavePhysicsVelocity=false;Actor->bRemoteCorrectionPending=false;
 for(float Distance:{95.f,96.f,110.f})
 {
  Actor->bHaveRemotePredict=false;Actor->ApplyACEPosition(P,false);
  FACEPosition Viewer=P;Viewer.Location.X+=Distance;Session->SetLocalPosition(Viewer);
  FACEPosition Next=P;Next.Location.Y+=1;Actor->ApplyACEPosition(Next);
  TestEqual(TEXT("96 AC is the retail immediate-position boundary"),Actor->GetActorLocation().Equals(Next.ToUnrealLocation(100),.01),Distance>=96);
 }
 Session->SetLocalPosition(P);Actor->bHaveRemotePredict=false;Actor->ApplyACEPosition(P,false);
 FACEPosition Large=P;Large.Location.Y+=50;Actor->ApplyACEPosition(Large);
 TestTrue(TEXT("A large nearby correction is not mistaken for a teleport"),Actor->GetActorLocation().Equals(P.ToUnrealLocation(100),.01));
 Large.bIsGrounded=false;Actor->ApplyACEPosition(Large);
 TestTrue(TEXT("Large non-contact echo cannot relocate an existing nearby body"),Actor->GetActorLocation().Equals(P.ToUnrealLocation(100),.01));
 Large.TeleportSequence=1;Actor->ApplyACEPosition(Large);
 TestTrue(TEXT("A real teleport still places the body immediately"),Actor->GetActorLocation().Equals(Large.ToUnrealLocation(100),.01));
 return !HasAnyErrors();
}
#endif
