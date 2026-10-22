#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEWorldEntityActor.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACECreatureFixtures.h"
#include "ACELandingMotion.h"
#include "ACESession.h"
#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Dat/ACEDatCursor.h"
#include "Dat/ACEDatFileTypes.h"
#include "VR/ACEVRPose.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERemoteMovementReplayTest,"ACE.RetailParity.RemoteMovementReplay",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACERemoteMovementReplayTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.SetCurrentWorld(World);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 TestTrue(TEXT("Retail player body DAT loads"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
 FACEPosition P;P.CellId=0x016C0101;P.Location=FVector(50,50,100);P.bIsGrounded=true;
 P.bHasContactState=true;P.bHasTeleportSequence=true;
 const FVector Origin=P.ToUnrealLocation(100);
 TArray<AActor*> Geometry;
 auto Box=[&](FVector Center,FVector Extent)
 {
  auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UBoxComponent>(A);A->SetRootComponent(C);
  C->SetBoxExtent(Extent);C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  C->SetCollisionObjectType(ECC_WorldStatic);C->SetCollisionResponseToAllChannels(ECR_Block);
  C->RegisterComponent();A->SetActorLocation(Origin+Center);Geometry.Add(A);return A;
 };
 Box(FVector(0,0,-10),FVector(2000,2000,10));
 auto Spawn=[&]()
 {
  FACEWorldObject O;O.SetupId=0x02000001;O.bIsPlayer=true;O.ItemType=ACEItemType::Creature;
  O.PhysicsState=ACEPhysicsState::Gravity;O.bHasPosition=true;O.Position=P;
  auto* A=World->SpawnActor<AACEWorldEntityActor>();A->InitializeFromObject(O,100,false);return A;
 };
 // Retail's Ready/Walk links contain position frames. Compare each physics
 // step with independently decoded DAT clips, including a stop during entry.
 for(int FPS:{30,60,144,240})for(int Direction:{1,-1})for(bool Tap:{false,true})for(bool Visible:{false,true})for(bool Strafing:{false,true})
 {
  const float Dt=1.f/FPS,Rate=Direction>0?1.f:.65f;
  auto ReadRoot=[&](uint32 Id,float Speed,float FrameRate=30.f,bool Loop=false)
  {
   TArray<uint8> Bytes;Dat->GetPortalDat()->ReadFile(Id,Bytes);
   FACEDatCursor Cursor(Bytes);FACEDatAnimation Anim;ACEDatUnpack::UnpackAnimation(Cursor,Anim);
   ACELandingMotion::FRootTrack Track;FTransform Pose=FTransform::Identity;
   Track.Keys.Add({0,Pose});float Time=0;
   if(Speed>0)Track.Entry=FTransform(Anim.PositionFrames[0]);
   for(int I=0;I<Anim.PositionFrames.Num();++I)
   {
    FTransform Frame(Anim.PositionFrames[Speed<0?Anim.PositionFrames.Num()-1-I:I]);
    if(Speed>0 && I==Anim.PositionFrames.Num()-1)Frame=Loop?Track.Entry:FTransform::Identity;
    if(Speed<0)Frame=Frame.Inverse();
    Pose.SetTranslation(Pose.GetTranslation()+Pose.GetRotation().RotateVector(Frame.GetTranslation()));
    Pose.SetRotation((Pose.GetRotation()*Frame.GetRotation()).GetNormalized());
    Time+=1.f/(FrameRate*FMath::Abs(Speed));Track.Keys.Add({Time,Pose});
   }
   return Track;
  };
  auto Entry=ReadRoot(Direction>0?0x03000006:0x03000009,Direction*Rate);
  auto Exit=ReadRoot(Direction>0?0x03000009:0x03000006,float(Direction));
  auto Cycle=ReadRoot(0x03000003,Direction*Rate,66.9f,true);float CycleElapsed=0;
  Cycle.bApplyEntry=true;
  auto* A=Spawn();A->MotionTableId=0x09000001;A->bClampToGround=false;A->SetActorEnableCollision(false);
  if(Visible)
  {
   FACEWorldObject O;O.SetupId=0x02000001;O.MotionTableId=A->MotionTableId;O.ItemType=ACEItemType::Creature;
   TestTrue(TEXT("Remote walk fixture builds its DAT parts"),A->ApplyDatAppearanceFromObject(O));
   A->Appearance->SetPreviewCapture(true);
  }
  FACEObjectMotionState Walk;Walk.CurrentStyle=ACEMotion::StanceNonCombat;Walk.ForwardCommand=5;
  Walk.Forward=Direction;Walk.ForwardUnitsPerSecond=Direction*3.12f*Rate;Walk.AnimPlayRate=Rate;Walk.bMoving=true;
  Walk.Strafe=Strafing?.5f:0;Walk.StrafeUnitsPerSecond=Walk.Strafe*1.25f;
  A->ApplyMotionState(Walk);
  FVector Reference=A->GetActorLocation();double MaxError=0,MaxPoseError=0;bool ExitWasTicking=false;
  float CycleTime=0;
  const int Hold=Tap?1:FPS;
  for(int I=0;I<Hold+FPS*2;++I)
  {
   if(I==Hold)
   {
    Exit.bApplyEntry=Entry.IsActive();
    FACEObjectMotionState Stop;Stop.CurrentStyle=Walk.CurrentStyle;Stop.ForwardCommand=ACEMotion::Ready;
    A->ApplyMotionState(Stop);ExitWasTicking=A->IsActorTickEnabled();
   }
   float Remaining=Dt;
   ACEWalkingMotion::FLink PoseLink;
   bool HadTransition=false;
   for(auto* Track:{&Entry,&Exit})
   {
    if(Track==&Exit && I<Hold)break;
    if(!Track->IsActive() || Remaining<=0)continue;
    HadTransition=true;
    const float Used=FMath::Min(Remaining,Track->Keys.Last().Time-Track->Time);
    Reference+=FACEPosition::AceVectorToUnreal(Track->Advance(Used).GetTranslation(),100);
    Remaining-=Used;
    FACEDatAnimData Clip;const bool IsEntry=Track==&Entry;
    Clip.AnimId=(Direction>0)==IsEntry?0x03000006:0x03000009;
    Clip.LowFrame=0;Clip.HighFrame=-1;Clip.Framerate=30.f*Direction*(IsEntry?Rate:1.f);
    PoseLink.Clips={Clip};PoseLink.Root.Time=Track->Time;
   }
   if(I<Hold)
   {
    Reference+=FACEPosition::AceVectorToUnreal(Cycle.AdvanceLooping(Remaining).GetTranslation(),100);
    CycleElapsed+=Remaining;
    Reference.X-=Walk.StrafeUnitsPerSecond*100*Dt;
   }
   if(HadTransition)CycleTime=0;
   CycleTime+=Remaining*(I<Hold?Rate:1.f);
   A->Tick(Dt);MaxError=FMath::Max(MaxError,FVector::Distance(Reference,A->GetActorLocation()));
   if(Visible)
   {
    A->Appearance->TickComponent(Dt,LEVELTICK_All,nullptr);
    TArray<FTransform> Expected;TArray<FACEDatAnimationHook> Hooks;int32 Count=0;
    if(!PoseLink.Clips.IsEmpty() && Remaining<=KINDA_SMALL_NUMBER)
     Dat->EvaluateWalkingLink(PoseLink,A->Appearance->GetPartCount(),Expected,100,Count,Hooks);
    else
     Dat->EvaluateMotionCommand(A->MotionTableId,I<Hold?0x45000005u:ACEMotion::Ready,
      CycleTime*(I<Hold?Direction:1),A->Appearance->GetPartCount(),Expected,100,Count,nullptr,nullptr,Walk.CurrentStyle);
    for(int Part=0;Part<Count;++Part)
    {
     const FTransform Actual=A->Appearance->GetPartMesh(Part)->GetRelativeTransform();
     MaxPoseError=FMath::Max(MaxPoseError,FVector::Distance(Actual.GetTranslation(),Expected[Part].GetTranslation()));
     MaxPoseError=FMath::Max(MaxPoseError,Actual.GetRotation().AngularDistance(Expected[Part].GetRotation()));
    }
   }
  }
  const FString Case=FString::Printf(TEXT("%d FPS direction=%d tap=%d mesh=%d strafe=%d"),FPS,Direction,Tap,Visible,Strafing);
  AddInfo(FString::Printf(TEXT("Remote transitions %s: position error %.6f cm, pose error %.6f"),*Case,MaxError,MaxPoseError));
  TestTrue(*FString::Printf(TEXT("Remote authored start/stop trajectory %s"),*Case),MaxError<.05);
  TestTrue(*FString::Printf(TEXT("Remote entry/exit/cycle poses share the physics clock %s"),*Case),MaxPoseError<.05);
  TestTrue(TEXT("A stop packet keeps physics ticking until the authored exit finishes"),ExitWasTicking);
  TestFalse(TEXT("Completed walk links release the actor tick"),A->IsActorTickEnabled());
  A->Destroy();
 }
 // Replay actual movement/position payloads through the session, including
 // sparse corrections during a turn and the turn-before-walk patrol sequence.
 for(int FPS:{30,60,144,240})
 {
  const float Dt=1.f/FPS;
  auto* A=Spawn();A->bClampToGround=false;A->SetActorEnableCollision(false);A->MotionTableId=0x09000001;
  FACESession Session;FACEWorldObject O;O.Guid=0x70004201;O.bHasPosition=true;O.Position=P;
  Session.WorldObjects.Add(O.Guid,O);uint16 MotionSeq=0,PositionSeq=0;
  Session.OnMotionUpdate.AddLambda([&](int32,const FACEObjectMotionState& M){A->ApplyMotionState(M);});
  Session.OnPositionUpdate.AddLambda([&](int32,const FACEPosition& V){A->ApplyACEPosition(V);});
  auto Header=[&](FACEBinaryWriter& W,uint8 Type)
  {
   W.WriteUInt32(O.Guid);W.WriteUInt16(0);W.WriteUInt16(++MotionSeq);W.WriteUInt16(0);W.WriteUInt8(0);W.Align();
   W.WriteUInt8(Type);W.WriteUInt8(0);W.WriteUInt16(0x3d);
  };
  auto Position=[&](const FACEPosition& V)
  {
   FACEBinaryWriter W;W.WriteUInt32(O.Guid);W.WriteUInt32(4);W.WriteUInt32(V.CellId);
   W.WriteFloat(V.Location.X);W.WriteFloat(V.Location.Y);W.WriteFloat(V.Location.Z);
   W.WriteFloat(V.RotationW);W.WriteFloat(V.RotationXYZ.X);W.WriteFloat(V.RotationXYZ.Y);W.WriteFloat(V.RotationXYZ.Z);
   W.WriteUInt16(0);W.WriteUInt16(++PositionSeq);W.WriteUInt16(V.TeleportSequence);W.WriteUInt16(0);
   FACEBinaryReader R(W.GetData());Session.HandleUpdatePosition(R);
  };
  auto Turn=[&](float Speed)
  {
   FACEBinaryWriter W;Header(W,0);W.WriteUInt32(0x60);W.WriteUInt16(0xd);W.WriteFloat(Speed);W.Align();
   FACEBinaryReader R(W.GetData());Session.HandleUpdateMotion(R);
  };
  Turn(1);const FQuat Initial=A->GetActorQuat();A->Tick(Dt);
  TestTrue(TEXT("Interpreted player turn is not delayed by a second presentation filter"),
   FMath::IsNearlyEqual(Initial.AngularDistance(A->GetActorQuat()),1.5*Dt,.00001));
  for(int I=1;I<FPS/2;++I)A->Tick(Dt);
  const FQuat Reversal=A->GetActorQuat();Turn(-1);A->Tick(Dt);
  TestTrue(TEXT("Player direction reversal takes effect on the next frame"),
   A->GetActorQuat().Equals((FQuat(FVector::UpVector,-1.5*Dt)*Reversal).GetNormalized(),.00001));
  Turn(0);const FQuat Stopped=A->GetActorQuat();for(int I=0;I<FPS/4;++I)A->Tick(Dt);
  TestTrue(TEXT("Stopping a player turn leaves no trailing rotation"),A->GetActorQuat().Equals(Stopped,.00001));

  FACEPosition Start=P;Start.TeleportSequence=1;Position(Start);
  Turn(1);double MaxTurnError=0;
  for(int I=0;I<FPS*3;++I)
  {
   if(I>0 && I%FPS==0)
   {
    FACEPosition Packet=Start;
    Packet.SetAceFacingFromUnrealDir2D(FQuat(FVector::UpVector,1.5*I/FPS).RotateVector(FVector::YAxisVector));
    Position(Packet);
   }
   A->Tick(Dt);
   MaxTurnError=FMath::Max(MaxTurnError,A->GetActorQuat().AngularDistance(FQuat(FVector::UpVector,1.5*(I+1)/FPS)));
  }
  TestTrue(TEXT("Turning through heading wrap and 1Hz positions stays on the retail heading every frame"),MaxTurnError<.0001);
  AddInfo(FString::Printf(TEXT("Packet-driven turning %d FPS: %.6f degrees maximum error"),FPS,FMath::RadiansToDegrees(MaxTurnError)));
  Start.TeleportSequence=2;Position(Start);
  FACEBinaryWriter W;Header(W,9);W.WriteUInt32(0);W.WriteFloat(1);W.WriteFloat(170);
  FACEBinaryReader R(W.GetData());Session.HandleUpdateMotion(R);
  for(int I=0;I<FPS/2;++I)A->Tick(Dt);
  const FQuat BeforePacket=A->GetActorQuat();Position(Start);A->Tick(Dt);
  TestTrue(TEXT("F748 preserves an active TurnTo heading, like retail IsMovingTo"),
   A->GetActorQuat().Equals((FQuat(FVector::UpVector,1.5*Dt)*BeforePacket).GetNormalized(),.00001));
  FACEPosition Shift=Start;Shift.Location.Y+=.2;
  const FQuat CorrectionHeading=A->GetActorQuat();Position(Shift);A->Tick(Dt);
  TestTrue(TEXT("Directed turn waits while interpolation owns the complete physics offset"),A->GetActorQuat().Equals(CorrectionHeading,.00001));

  // Reported Sentry cell/position. Isolate movement timing from terrain so
  // comparisons are against retail's command sequence, not our floor solver.
  Start.CellId=0xAAB30004;Start.Location=FVector(18.244883,89.502439,116.005996);
  Start.RotationW=.293540f;Start.RotationXYZ=FVector(0,0,-.955947f);Start.TeleportSequence=3;Position(Start);
  const FVector PatrolStart=A->GetActorLocation();const FQuat PatrolFacing=A->GetActorQuat();
  const FQuat DestinationFacing=(FQuat(FVector::UpVector,-UE_HALF_PI)*PatrolFacing).GetNormalized();
  FACEPosition Destination=Start;Destination.SetLocationFromUnreal(PatrolStart+DestinationFacing.RotateVector(FVector(0,1200,0)),100);
  FACEBinaryWriter Patrol;Header(Patrol,7);Patrol.WriteUInt32(Destination.CellId);
  Patrol.WriteFloat(Destination.Location.X);Patrol.WriteFloat(Destination.Location.Y);Patrol.WriteFloat(Destination.Location.Z);
  Patrol.WriteUInt32(1);Patrol.WriteFloat(.1f);Patrol.WriteFloat(0);Patrol.WriteFloat(MAX_flt);
  Patrol.WriteFloat(1);Patrol.WriteFloat(1);Patrol.WriteFloat(0);Patrol.WriteFloat(1);
  FACEBinaryReader PR(Patrol.GetData());Session.HandleUpdateMotion(PR);
  TestFalse(TEXT("A new patrol never starts a walk link before its turn"),A->RemoteWalkingMotion.HasPending());
  for(int I=0;I<FPS/2;++I)A->Tick(Dt);
  TestTrue(TEXT("Patrol turns before translating, rather than getting ahead of the server"),A->GetActorLocation().Equals(PatrolStart,.01));
  TestTrue(TEXT("Patrol turns at the authored rate"),FMath::IsNearlyEqual(PatrolFacing.AngularDistance(A->GetActorQuat()),.75,.0001));
  for(int I=0;I<FPS*6;++I)A->Tick(Dt);
  const FVector Arrived=A->GetActorLocation();
  ACEWalkingMotion::FLink PatrolExit;Dat->BuildWalkingLink(A->MotionTableId,1,0,ACEMotion::StanceNonCombat,PatrolExit);
  const float ExitDistance=PatrolExit.Root.Keys.Last().Transform.GetTranslation().Y*100;
  TestTrue(TEXT("Patrol reaches its destination and finishes the authored exit step"),FMath::IsNearlyEqual(FVector::Dist2D(PatrolStart,Arrived),1190.+ExitDistance,.2));
  FACEPosition Correction=Destination;Correction.SetLocationFromUnreal(Arrived-DestinationFacing.RotateVector(FVector(0,30,0)),100);
  Position(Correction);for(int I=0;I<FPS;++I)A->Tick(Dt);
  TestTrue(TEXT("A completed patrol is not restarted by a position correction"),A->GetActorLocation().Equals(Correction.ToUnrealLocation(100),5.));
  Start.TeleportSequence=4;Start.RotationW=1;Start.RotationXYZ=FVector::ZeroVector;Position(Start);
  const FVector RunStart=A->GetActorLocation();Destination=Start;Destination.Location.Y+=3;
  FACEBinaryWriter Run;Header(Run,7);Run.WriteUInt32(Destination.CellId);
  Run.WriteFloat(Destination.Location.X);Run.WriteFloat(Destination.Location.Y);Run.WriteFloat(Destination.Location.Z);
  Run.WriteUInt32(0x43);Run.WriteFloat(.1f);Run.WriteFloat(0);Run.WriteFloat(MAX_flt);
  Run.WriteFloat(1);Run.WriteFloat(1);Run.WriteFloat(270);Run.WriteFloat(1.4f);
  FACEBinaryReader RR(Run.GetData());Session.HandleUpdateMotion(RR);
  for(int I=0;I<FMath::RoundToInt(FPS*.4);++I)A->Tick(Dt);
  TestTrue(TEXT("Patrol retains its selected run gait inside the walk/run threshold"),
   A->RemoteMotion.bRunning && FVector::Dist2D(RunStart,A->GetActorLocation())>1);
  AddInfo(FString::Printf(TEXT("Run entry %d FPS: %.3f cm"),FPS,FVector::Dist2D(RunStart,A->GetActorLocation())));
  bool SawNearDestination=false,KeptRunGait=true;
  for(int I=0;I<FPS*3;++I)
  {
   const double Travel=FVector::Dist2D(RunStart,A->GetActorLocation());
   if(Travel>200 && Travel<289 && A->RemoteMotion.bMoving)
   { SawNearDestination=true;KeptRunGait&=A->RemoteMotion.bRunning; }
   A->Tick(Dt);
  }
  TestTrue(TEXT("Run gait stays latched inside the 1m walk threshold"),SawNearDestination && KeptRunGait);
  ACEWalkingMotion::FLink RunExit;Dat->BuildWalkingLink(A->MotionTableId,2,0,ACEMotion::StanceNonCombat,RunExit);
  const float RunExitDistance=RunExit.Root.Keys.IsEmpty()?0:RunExit.Root.Keys.Last().Transform.GetTranslation().Y*100;
  AddInfo(FString::Printf(TEXT("Run arrived %d FPS: %.3f cm; expected %.3f cm"),FPS,FVector::Dist2D(RunStart,A->GetActorLocation()),290.f+RunExitDistance));
  const FQuat FinalHeading=FACEPosition::QuatFromUnrealTravelDir2D(FACEPosition::UnrealDirFromAceHeadingDegrees(270));
  TestTrue(TEXT("MoveTo completes its optional final heading after stopping at the requested distance"),
   A->RemoteMotion.MovementType==0 && A->GetActorQuat().Equals(FinalHeading,.00001)
   && FMath::IsNearlyEqual(FVector::Dist2D(RunStart,A->GetActorLocation()),290.+RunExitDistance,.1));
  A->Destroy();
 }
 // Physics-only turn modifiers must survive DAT loading even without any
 // animation clips. Compare against independently decoded retail records.
 for(int Model=-1;Model<int(UE_ARRAY_COUNT(ACECreatureFixtures::Models));++Model)
 {
  FACEWorldObject O;O.SetupId=0x02000001;O.MotionTableId=0x09000001;O.bIsPlayer=true;O.ItemType=ACEItemType::Creature;
  if(Model>=0)ACECreatureFixtures::Apply(O,Model);
  O.bHasPosition=true;O.Position=P;
  TArray<uint8> Bytes;Dat->GetPortalDat()->ReadFile(O.MotionTableId,Bytes);
  FACEDatCursor Cursor(Bytes);FACEDatMotionTable Table;
  TestTrue(TEXT("Read retail motion table"),ACEDatUnpack::UnpackMotionTable(Cursor,Table));
  for(uint32 Command:{ACEMotion::Ready,0x45000005u,0x44000007u,ACEMotion::Dead})
   if(const auto* Cycle=Table.Cycles.Find((Table.DefaultStyle<<16)|(Command&0xffffffu)))
   {
    AddInfo(FString::Printf(TEXT("DAT %08X cycle %08X velocity=%s explicit=%d clips=%d"),O.MotionTableId,Command,*FVector(Cycle->Velocity).ToString(),Cycle->bHasVelocity,Cycle->Anims.Num()));
    for(const auto& Clip:Cycle->Anims)
    {
     TArray<uint8> Raw;Dat->GetPortalDat()->ReadFile(Clip.AnimId,Raw);FACEDatCursor C(Raw);FACEDatAnimation Anim;
     ACEDatUnpack::UnpackAnimation(C,Anim);FVector Total=FVector::ZeroVector;
     for(const auto& Frame:Anim.PositionFrames)Total+=FVector(Frame.GetTranslation());
     AddInfo(FString::Printf(TEXT("  clip %08X [%d,%d] %.2f fps positionFrames=%d delta=%s"),Clip.AnimId,Clip.LowFrame,Clip.HighFrame,Clip.Framerate,Anim.PositionFrames.Num(),*Total.ToString()));
    }
   }
  const auto* TurnData=Table.Modifiers.Find((Table.DefaultStyle<<16)|0x0du);
  if(!TurnData)TurnData=Table.Modifiers.Find(0x0du);
  if(!TestNotNull(TEXT("Retail turn modifier is retained"),TurnData))continue;
  AddInfo(FString::Printf(TEXT("Retail turn modifier %08X: omega Z %.3f"),O.MotionTableId,TurnData->Omega.Z));
  auto* A=World->SpawnActor<AACEWorldEntityActor>();A->bClampToGround=false;A->InitializeFromObject(O,100,false);
  FACEObjectMotionState Turn;Turn.Turn=1;Turn.CurrentStyle=Table.DefaultStyle;A->ApplyMotionState(Turn);
  const FQuat Before=A->GetActorQuat();for(int I=0;I<12;++I)A->Tick(1.f/60);
  TestTrue(TEXT("Each model turns using its authored modifier, independent of body scale"),
   FMath::IsNearlyEqual(Before.AngularDistance(A->GetActorQuat()),FMath::Abs(TurnData->Omega.Z)*.2,.0001));
  TestTrue(TEXT("Interpreted turn direction follows the signed DAT omega"),
   A->GetActorQuat().Equals((FQuat(FVector::UpVector,-TurnData->Omega.Z*.2)*Before).GetNormalized(),.00001));
  A->Destroy();
 }
 // Replay each model's actual position frames, including short taps and a
 // death before the entry/exit completes. Reference clips come directly from
 // the DAT table, not the client's speed/transition lookup.
 for(int Model=0;Model<int(UE_ARRAY_COUNT(ACECreatureFixtures::Models));++Model)
 for(int FPS:{30,60,144,240})for(int Gait:{-1,1,2})for(bool Die:{false,true})
 {
  FACEWorldObject O;ACECreatureFixtures::Apply(O,Model);O.bHasPosition=true;O.Position=P;O.Position.Location.Z+=10;
  TArray<uint8> Bytes;Dat->GetPortalDat()->ReadFile(O.MotionTableId,Bytes);
  FACEDatCursor Cursor(Bytes);FACEDatMotionTable Table;ACEDatUnpack::UnpackMotionTable(Cursor,Table);
  const uint32 Command=Gait==2?0x44000007u:0x45000005u,Style=Table.DefaultStyle;
  const float Rate=Gait<0?.65f:1.f,Dt=1.f/FPS;
  auto ReadTrack=[&](const FACEDatMotionData* Data,float Speed,bool Loop=false)
  {
   ACELandingMotion::FRootTrack Track;FTransform Root=FTransform::Identity;float Time=0;
   Track.Keys.Add({0,Root});if(!Data)return Track;
   for(const auto& Clip:Data->Anims)
   {
    TArray<uint8> Raw;Dat->GetPortalDat()->ReadFile(Clip.AnimId,Raw);FACEDatCursor C(Raw);FACEDatAnimation Anim;
    ACEDatUnpack::UnpackAnimation(C,Anim);
    const int High=Clip.HighFrame<0?int(Anim.NumFrames)-1:FMath::Min(Clip.HighFrame,int(Anim.NumFrames)-1);
    const int Low=FMath::Clamp(Clip.LowFrame,0,High);
    if(FMath::IsNearlyZero(Clip.Framerate))continue;
    const FTransform First=Speed>0 && Anim.PositionFrames.IsValidIndex(Low)?FTransform(Anim.PositionFrames[Low]):FTransform::Identity;
    if(Time==0)Track.Entry=First;
    else
    {
     Root.SetTranslation(Root.GetTranslation()+Root.GetRotation().RotateVector(First.GetTranslation()));
     Root.SetRotation((Root.GetRotation()*First.GetRotation()).GetNormalized());
     Track.Keys.Last().Transform=Root;
    }
    for(int I=0;I<=High-Low;++I)
    {
     const int Index=Speed<0?High-I:Low+I;
     FTransform Delta=Anim.PositionFrames.IsValidIndex(Index)?FTransform(Anim.PositionFrames[Index]):FTransform::Identity;
     if(Speed>0 && Index==High)Delta=FTransform::Identity;
     if(Speed<0)Delta=Delta.Inverse();
     const float Step=1.f/FMath::Abs(Clip.Framerate*Speed);
     Delta.AddToTranslation(FVector(Data->Velocity)*Step*Speed);
     Root.SetTranslation(Root.GetTranslation()+Root.GetRotation().RotateVector(Delta.GetTranslation()));
     Root.SetRotation((Root.GetRotation()*Delta.GetRotation()).GetNormalized());
     Time+=Step;Track.Keys.Add({Time,Root});
    }
   }
   if(Loop)
   {
    auto& Last=Track.Keys.Last().Transform;
    Last.SetTranslation(Last.GetTranslation()+Last.GetRotation().RotateVector(Track.Entry.GetTranslation()));
    Last.SetRotation((Last.GetRotation()*Track.Entry.GetRotation()).GetNormalized());
   }
   return Track;
  };
  auto Link=[&](uint32 From,uint32 To)
  {
   const FACEDatMotionData* Result=nullptr;
   for(uint32 Key:{(Style<<16)|(From&0xffffffu),Style<<16})
    if(const auto* Links=Table.Links.Find(Key))if((Result=Links->Find(To)))break;
   return Result;
  };
  auto Entry=ReadTrack(Link(Gait<0?Command:ACEMotion::Ready,Gait<0?ACEMotion::Ready:Command),Gait<0?-Rate:Rate);
  auto Exit=ReadTrack(Link(Gait<0?ACEMotion::Ready:Command,Gait<0?Command:ACEMotion::Ready),Gait<0?-1.f:1.f);
  auto Cycle=ReadTrack(Table.Cycles.Find((Style<<16)|(Command&0xffffffu)),Gait<0?-Rate:Rate,true);
  Cycle.bApplyEntry=Entry.Keys.Num()>1;
  auto Death=ReadTrack(Link(ACEMotion::Ready,ACEMotion::Dead),1);
  TestTrue(TEXT("Critter locomotion has authored position frames"),Cycle.Keys.Num()>1);
  auto* A=World->SpawnActor<AACEWorldEntityActor>();A->bClampToGround=false;A->InitializeFromObject(O,100,FPS==60);
  if(FPS==60)A->Appearance->SetPreviewCapture(true);
  FACEObjectMotionState Walk;Walk.CurrentStyle=Style;Walk.ForwardCommand=Gait==2?7:5;
  Walk.Forward=Gait<0?-1:1;Walk.bRunning=Gait==2;Walk.AnimPlayRate=Rate;Walk.bMoving=true;
  Walk.ForwardUnitsPerSecond=Walk.Forward*(Gait==2?4.f:3.12f)*Rate;
  A->ApplyMotionState(Walk);FVector Reference=A->GetActorLocation();FQuat Facing=A->GetActorQuat();
  float CycleElapsed=0;double Worst=0;
  const int StopFrame=Die?1:FMath::CeilToInt((Entry.Keys.Last().Time+1.25f)*FPS);
  const int End=StopFrame+FMath::CeilToInt((Entry.Keys.Last().Time+Exit.Keys.Last().Time+Death.Keys.Last().Time+.2f)*FPS);
  for(int I=0;I<End;++I)
  {
   if(I==StopFrame)
   {
    Exit.bApplyEntry=Entry.IsActive();
    Death.bApplyEntry=Entry.IsActive() || Exit.IsActive();
    FACEObjectMotionState Stop;Stop.CurrentStyle=Style;Stop.ForwardCommand=ACEMotion::Ready;
    A->ApplyMotionState(Stop);
    if(Die){Stop.ForwardCommand=ACEMotion::Dead;A->ApplyMotionState(Stop);}
   }
   float Remaining=Dt;
   auto Apply=[&](const FTransform& Delta)
   {
    Reference+=Facing.RotateVector(FACEPosition::AceVectorToUnreal(Delta.GetTranslation(),100*O.Scale));
    Facing=(Facing*FACEPosition::AceQuatToUnreal(Delta.GetRotation())).GetNormalized();
   };
   for(auto* Track:{&Entry,&Exit,&Death})
   {
    if(Track!=&Entry && I<StopFrame)break;
    if(Track==&Death && !Die)break;
    if(!Track->IsActive() || Remaining<=0)continue;
    const float Use=FMath::Min(Remaining,Track->Keys.Last().Time-Track->Time);
    Apply(Track->Advance(Use));Remaining-=Use;
   }
   if(I<StopFrame && Remaining>0 && Cycle.Keys.Num()>1)
   {
    Apply(Cycle.AdvanceLooping(Remaining));CycleElapsed+=Remaining;
   }
   A->Tick(Dt);if(FPS==60)A->Appearance->TickComponent(Dt,LEVELTICK_All,nullptr);
   Worst=FMath::Max(Worst,FVector::Distance(A->GetActorLocation(),Reference));
  }
  const FString Label=FString::Printf(TEXT("%s %d FPS gait=%d death=%d"),ACECreatureFixtures::Models[Model].Name,FPS,Gait,Die);
  AddInfo(FString::Printf(TEXT("Authored creature replay %s: max error %.4f cm"),*Label,Worst));
  TestTrue(*FString::Printf(TEXT("DAT trajectory including start/stop/death %s"),*Label),Worst<.1);
  if(Die && FPS==60)
  {
   FACEWorldObject Corpse=O;Corpse.ItemType=ACEItemType::Container;Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;
   Corpse.InitialMotionCommand=ACEMotion::Dead;Corpse.InitialMotionStyle=ACEMotion::StanceNonCombat;
   Corpse.Position.SetLocationFromUnreal(Reference,100);
   auto* C=World->SpawnActor<AACEWorldEntityActor>();C->bClampToGround=false;C->InitializeFromObject(Corpse,100,true);
   C->SetActorRotation(Facing);C->Appearance->SetPreviewCapture(true);C->Appearance->TickComponent(.1f,LEVELTICK_All,nullptr);
   double Error=0;
   for(int Part=0;Part<A->Appearance->GetPartCount();++Part)
    if(auto* Body=A->Appearance->GetPartMesh(Part))if(auto* Remains=C->Appearance->GetPartMesh(Part))
     Error=FMath::Max(Error,FVector::Distance(Body->GetComponentLocation(),Remains->GetComponentLocation()));
   TestTrue(*FString::Printf(TEXT("Death during movement ends in the corpse pose %s (%.3f cm)"),*Label,Error),Error<1.);
   C->Destroy();
  }
  A->Destroy();
 }
 // Independent ACE.Server Sequence.Update results from the retail DAT. These
 // endpoints deliberately do not use our root-track builder as the oracle.
 // Walk is stopped before its first tick: unfinished entry/exit links survive.
 const FVector ServerIdle[]={{0,-1.1852243,0},{0,0,0},{.08027343,-.7655278,.00023439527},
  {0,-.35933387,0},{0,-.5655651,0},{-.23063329,.05075494,.0048333392},
  {-.23063329,.05075494,.0048333392},{-.23063329,.05075494,.0048333392}};
 const FVector ServerWalking[]={{0,-.43529978,0},{0,.749758,0},{.09525208,.21668129,.018451784},
  {0,-.35933387,0},{0,-.54217184,0},{-.22427224,.9993388,.005000006},
  {-.22427224,.9993388,.005000006},{-.22427224,.9993388,.005000006}};
 for(int Model=-1;Model<int(UE_ARRAY_COUNT(ACECreatureFixtures::Models));++Model)
 for(int FPS:{30,60,144,240})for(bool Walking:{false,true})
 {
  FACEWorldObject O;O.SetupId=0x02000001;O.MotionTableId=0x09000001;O.Scale=1;O.ItemType=ACEItemType::Creature;
  if(Model>=0)ACECreatureFixtures::Apply(O,Model);
  O.bHasPosition=true;O.Position=P;O.Position.Location.Z+=10;
  auto* A=World->SpawnActor<AACEWorldEntityActor>();A->bClampToGround=false;A->InitializeFromObject(O,100,false);
  const FVector Start=A->GetActorLocation();const FQuat Facing=A->GetActorQuat();
  FACEObjectMotionState M;M.CurrentStyle=ACEMotion::StanceNonCombat;
  if(Walking)
  {
   M.ForwardCommand=5;M.Forward=1;M.bMoving=true;A->ApplyMotionState(M);
   M.ForwardCommand=ACEMotion::Ready;M.Forward=0;M.bMoving=false;A->ApplyMotionState(M);
  }
  M.ForwardCommand=ACEMotion::Dead;A->ApplyMotionState(M);
  for(int I=0;I<FPS*6;++I)A->Tick(1.f/FPS);
  const FVector Expected=Start+Facing.RotateVector(FACEPosition::AceVectorToUnreal(
   Walking?ServerWalking[Model+1]:ServerIdle[Model+1],100*O.Scale));
  const FQuat RootRotation=Model==1?FQuat(0,0,Walking?.059767455:.05516466,Walking?.998213:.99847794):FQuat::Identity;
  const FString Label=FString::Printf(TEXT("ACE physics endpoint setup=%08X FPS=%d interruptedWalk=%d error=%.5fcm"),O.SetupId,FPS,Walking,FVector::Distance(Expected,A->GetActorLocation()));
  TestTrue(*Label,A->GetActorLocation().Equals(Expected,.02));
  TestTrue(TEXT("ACE death root rotation matches"),A->GetActorQuat().Equals(Facing*FACEPosition::AceQuatToUnreal(RootRotation),.00001));
  AddInfo(Label);A->Destroy();
 }
 // Retail InterpolationManager::adjust_offset replaces the cycle's displacement
 // with a bounded step to the received position. It does not extrapolate that
 // destination while exponentially blending a second, displayed body toward it.
 for(int FPS:{30,90,144,240})for(bool Moving:{false,true})
 {
  auto* A=Spawn();A->bClampToGround=false;
  A->RemoteMotion.bMoving=Moving;A->RemoteMotion.Forward=Moving?1:0;
  A->RemoteMotion.ForwardUnitsPerSecond=Moving?4:0;
  FACEPosition Correction=P;Correction.SetLocationFromUnreal(A->GetActorLocation()+FVector(0,20,0),100);
  A->ApplyACEPosition(Correction);A->Tick(1.f/FPS);
  const double Expected=FMath::Min(20.,800./FPS);
  TestTrue(*FString::Printf(TEXT("Retail correction replaces locomotion at %d FPS (moving=%d)"),FPS,Moving),
   FMath::Abs(A->GetActorLocation().Y-Origin.Y-Expected)<.01);
  FACEObjectMotionState Dead;Dead.ForwardCommand=ACEMotion::Dead;
  Dead.CurrentStyle=ACEMotion::StanceNonCombat;A->ApplyMotionState(Dead);
  for(int I=0;I<FPS/4;++I)A->Tick(1.f/FPS);
  TestTrue(TEXT("Death settles at the authoritative feet without carrying an extrapolated offset"),
   FVector::Dist2D(A->GetActorLocation(),Correction.ToUnrealLocation(100))<5.);
  A->Destroy();
 }
 // The server creates a corpse at the physics position AFTER the authored
 // death sequence. Playing only the part poses drops the position frames.
 for(int Model=-1;Model<int(UE_ARRAY_COUNT(ACECreatureFixtures::Models));++Model)for(int FPS:{30,144})for(int Surface:{0,1,2})for(bool Deferred:{false,true})
 {
  const bool Grounded=Surface!=0;
  Geometry[0]->SetActorRotation(FRotator(Surface==2?10.f:0.f,0,0));
  FACEWorldObject O;O.Guid=0x70001000+Model;O.SetupId=0x02000001;
  O.MotionTableId=0x09000001;O.bIsPlayer=true;O.ItemType=ACEItemType::Creature;
  if(Model>=0)ACECreatureFixtures::Apply(O,Model);
  O.PhysicsState=ACEPhysicsState::Gravity;
  O.bHasPosition=true;O.Position=P;if(!Grounded)O.Position.Location.Z+=10;
  O.Position.SetAceFacingFromUnrealDir2D(FVector(1,1,0));
  ACELandingMotion::FRootTrack Track;
  if(!TestTrue(TEXT("Fixture has an authored death transition"),
   Dat->BuildTransitionRootTrack(O.MotionTableId,ACEMotion::Ready,ACEMotion::Dead,ACEMotion::StanceNonCombat,Track)))continue;
  const auto End=Track.Keys.Last();
  auto* A=World->SpawnActor<AACEWorldEntityActor>();A->bClampToGround=Grounded;A->InitializeFromObject(O,100,true);
  A->Appearance->SetPreviewCapture(true);
  const FVector Start=A->GetActorLocation();const FQuat Facing=A->GetActorQuat();
  FACEObjectMotionState Dead;Dead.ForwardCommand=ACEMotion::Dead;Dead.CurrentStyle=ACEMotion::StanceNonCombat;
  A->ApplyMotionState(Dead);
  for(int I=0;I<FMath::CeilToInt((End.Time+.1)*FPS);++I)
  {A->Tick(1.f/FPS);A->Appearance->TickComponent(1.f/FPS,LEVELTICK_All,nullptr);}
  const FVector Expected=Start+Facing.RotateVector(FACEPosition::AceVectorToUnreal(End.Transform.GetTranslation(),100*O.Scale));
  AddInfo(FString::Printf(TEXT("Death root %08X: authored %s, actual %s"),O.SetupId,*(Expected-Start).ToString(),*(A->GetActorLocation()-Start).ToString()));
  TestTrue(TEXT("Dying body consumes the DAT position frames before the corpse replaces it"),(Grounded?FVector::Dist2D(A->GetActorLocation(),Expected)<.05:A->GetActorLocation().Equals(Expected,.05)));
  // A newly created corpse holds the final pose at the server's final root.
  // Compare actual world-space mesh parts, not only two position variables.
  FACEWorldObject Corpse=O;Corpse.Guid+=100;Corpse.bIsPlayer=false;Corpse.ItemType=ACEItemType::Container;
  Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;Corpse.InitialMotionCommand=ACEMotion::Dead;
  Corpse.InitialMotionStyle=ACEMotion::StanceNonCombat;Corpse.Position.SetLocationFromUnreal(Grounded?A->GetActorLocation():Expected,100);
  const FQuat FinalFacing=O.Position.GetAcQuat()*End.Transform.GetRotation();
  Corpse.Position.RotationW=FinalFacing.W;Corpse.Position.RotationXYZ=FVector(FinalFacing.X,FinalFacing.Y,FinalFacing.Z);
  auto* C=World->SpawnActor<AACEWorldEntityActor>();C->bClampToGround=Grounded;Corpse.Position.bHasContactState=false;Corpse.Position.bIsGrounded=false;C->InitializeFromObject(Corpse,100,!Deferred);
  if(Deferred){C->Tick(1.f/FPS);C->ApplyDatAppearanceFromObject(Corpse);}
  C->Appearance->SetPreviewCapture(true);C->Appearance->TickComponent(.3f,LEVELTICK_All,nullptr);
  if(Grounded)for(int I=0;I<FPS;++I)C->Tick(1.f/FPS);
  TestEqual(TEXT("Corpse keeps the dying creature's part count"),C->Appearance->GetPartCount(),A->Appearance->GetPartCount());
  double WorstPartError=0;
  for(int Part=0;Part<A->Appearance->GetPartCount();++Part)
   if(auto* LivePart=A->Appearance->GetPartMesh(Part))
    if(auto* CorpsePart=C->Appearance->GetPartMesh(Part))
     WorstPartError=FMath::Max(WorstPartError,FVector::Distance(LivePart->GetComponentLocation(),CorpsePart->GetComponentLocation()));
  AddInfo(FString::Printf(TEXT("Corpse handoff %08X %d FPS surface=%d deferred=%d: %.6f cm mesh error"),O.SetupId,FPS,Surface,Deferred,WorstPartError));
  TestTrue(*FString::Printf(TEXT("Corpse replaces the final death pose without a mesh jump (%08X, %.3f cm)"),O.SetupId,WorstPartError),WorstPartError<1.);
  C->Destroy();
  const FVector Finished=A->GetActorLocation();A->ApplyMotionState(Dead);
  for(int I=0;I<FPS;++I)A->Tick(1.f/FPS);
  TestTrue(TEXT("Repeated Dead does not replay root displacement"),A->GetActorLocation().Equals(Finished,.05));
  A->Destroy();
 }
 Geometry[0]->SetActorRotation(FRotator::ZeroRotator);
 // Authored death movement is physics movement, not a mesh-only offset through
 // walls. The human falls backward 1.18m; this wall permits only a short slide.
 {
  auto* Wall=Box(FVector(0,-90,100),FVector(400,10,100));
  FACEWorldObject O;O.SetupId=0x02000001;O.MotionTableId=0x09000001;
  O.ItemType=ACEItemType::Creature;O.bIsPlayer=true;O.bHasPosition=true;O.Position=P;
  auto* A=World->SpawnActor<AACEWorldEntityActor>();A->InitializeFromObject(O,100,false);
  FACEObjectMotionState Dead;Dead.ForwardCommand=ACEMotion::Dead;Dead.CurrentStyle=ACEMotion::StanceNonCombat;
  A->ApplyMotionState(Dead);
  TestTrue(TEXT("A stationary dying body keeps ticking for authored movement"),A->IsActorTickEnabled());
  for(int I=0;I<360;++I)A->Tick(1.f/90);
  const double Travel=Origin.Y-A->GetActorLocation().Y;
  TestTrue(TEXT("Death displacement stops at the wall with the authored body"),Travel>1 && Travel<80);
  TestFalse(TEXT("Completed death movement can sleep again"),A->IsActorTickEnabled());
  A->Destroy();Wall->Destroy();
 }
 {
  FACEWorldObject O;O.SetupId=0x02000001;O.MotionTableId=0x09000001;O.ItemType=ACEItemType::Creature;
  O.bIsPlayer=true;O.PhysicsState=ACEPhysicsState::Gravity;O.bHasPosition=true;O.Position=P;
  auto* A=World->SpawnActor<AACEWorldEntityActor>();A->InitializeFromObject(O,100,false);
  const FVector Start=A->GetActorLocation();A->ApplyPhysicsVelocity(FVector(0,4,5));
  for(int I=0;I<18;++I)A->Tick(1.f/90);
  FACEObjectMotionState Dead;Dead.ForwardCommand=ACEMotion::Dead;Dead.CurrentStyle=ACEMotion::StanceNonCombat;
  A->ApplyMotionState(Dead);
  for(int I=0;I<18;++I)A->Tick(1.f/90);
  const FVector Expected=Start+FVector(0,160,(5*.4-4.9*.4*.4)*100);
  TestTrue(TEXT("Dying during a jump preserves physical flight instead of freezing above the corpse"),
   A->bHavePhysicsVelocity && A->GetActorLocation().Equals(Expected,.1));
  TestTrue(TEXT("Airborne death advances its animation without applying grounded root translation"),
   FMath::IsNearlyEqual(A->RemoteDeathRootTrack.Time,.2f,.001f));
  FACEPosition Respawn=P;Respawn.TeleportSequence=1;Respawn.Location.Y+=10;
  A->ApplyACEPosition(Respawn);FACEObjectMotionState Ready;Ready.ForwardCommand=ACEMotion::Ready;
  A->ApplyMotionState(Ready);const FVector Respawned=A->GetActorLocation();
  for(int I=0;I<90;++I)A->Tick(1.f/90);
  TestTrue(TEXT("Respawning cancels unfinished death displacement"),A->GetActorLocation().Equals(Respawned,.01));
  A->Destroy();
 }
 for(int FPS:{30,90,144,240})
 {
  const float Dt=1.f/FPS;
  {
   auto* Turning=Spawn();FACEObjectMotionState Turn;
   Turn.MovementType=9;Turn.MoveToDesiredHeading=179.f;Turn.TurnToSpeed=1.f;
   const FQuat Before=Turning->GetActorQuat();Turning->ApplyMotionState(Turn);
   TestTrue(TEXT("Remote server turn starts without a heading snap"),Turning->GetActorQuat().Equals(Before));
   for(int I=0;I<FPS/2;++I)Turning->Tick(Dt);
   TestTrue(TEXT("Remote player turn is 1.5 radians per second without additional visual lag"),
    FMath::IsNearlyEqual(Before.AngularDistance(Turning->GetActorQuat()),.75,.0001));
   for(int I=0;I<FPS*2;++I)Turning->Tick(Dt);
   FACEPosition Expected=P;Expected.SetAceFacingFromUnrealDir2D(FACEPosition::UnrealDirFromAceHeadingDegrees(179.f));
   TestTrue(TEXT("Remote turn arrives at the same heading as the local player"),Turning->GetActorQuat().Equals(Expected.ToUnrealQuat(),.00001));
   Turning->Destroy();
  }
  // F748 has its own sequence, not a newer VectorUpdate. A late airborne
  // position must not restart the already-running jump or replay its velocity.
  auto* A=Spawn();A->ApplyPhysicsVelocity(FVector(0,4,5));
  const FVector Start=A->GetActorLocation();double MaxArcError=0;
  for(int I=1;I<=FPS*.8;++I)
  {
   if(I%FMath::Max(1,FPS/5)==0)
   {
    const double Sent=FMath::Max(0.,(I-1.)/FPS-.15);
    FACEPosition Late=P;Late.bIsGrounded=false;Late.bHasVelocity=true;
    Late.SetLocationFromUnreal(Start+FVector(0,400*Sent,500*Sent-490*Sent*Sent),100);
    Late.Velocity=FVector(0,4,5-9.8*Sent);A->ApplyACEPosition(Late);
   }
   A->Tick(Dt);const double T=double(I)/FPS;
   MaxArcError=FMath::Max(MaxArcError,FVector::Distance(A->GetActorLocation(),Start+FVector(0,400*T,500*T-490*T*T)));
  }
  AddInfo(FString::Printf(TEXT("Delayed jump %d FPS: %.3f cm arc error"),FPS,MaxArcError));
  TestTrue(TEXT("Late airborne position packets do not rewind the jump"),MaxArcError<1);
  A->Destroy();

  A=Spawn();A->ApplyPhysicsVelocity(FVector(0,4,5));
  for(int I=0;I<FPS/2;++I)A->Tick(Dt);
  FACEPosition Land=P;Land.Location.Y+=4.3;A->ApplyACEPosition(Land);
  TestTrue(TEXT("Early landing confirmation waits for local physical contact"),A->bHavePhysicsVelocity && A->PendingRemoteLanding.IsSet());
  for(int I=0;I<FPS*2;++I)if(A->IsActorTickEnabled())A->Tick(Dt);
  TestTrue(TEXT("Queued landing correction keeps ticking until consumed"),!A->PendingRemoteLanding.IsSet());
  TestTrue(TEXT("Landing converges to the confirmed position without replaying the jump"),
   FVector::Dist2D(A->GetActorLocation(),Land.ToUnrealLocation(100))<5 && FMath::Abs(A->GetActorLocation().Z-Origin.Z-1.5)<.1);
  A->Destroy();

  // Same network latency for spawn and motion, then +/-50ms arrival jitter
  // around 1Hz positions. Retail corrects even a position behind the body;
  // carrying forward velocity during that correction advances its endpoint.
  A=Spawn();A->RemoteMotion.bMoving=true;A->RemoteMotion.Forward=1;A->RemoteMotion.ForwardUnitsPerSecond=4;
  FVector Previous=A->GetActorLocation();double MinAdvance=DBL_MAX,MaxSide=0;int Packet=1;
  for(int I=1;I<=FPS*4;++I)
  {
   const double Now=double(I-1)/FPS;
   if(Packet<=3 && Now>=Packet+(Packet%2?-.05:.05))
   {
    FACEPosition Update=P;Update.Location.Y+=Packet*4;Update.Location.X+=(Packet%2?.02:-.02);
    Update.Location.Z+=.15;A->ApplyACEPosition(Update);++Packet;
   }
   A->Tick(Dt);const FVector Current=A->GetActorLocation();
   MinAdvance=FMath::Min(MinAdvance,Current.Y-Previous.Y);
   MaxSide=FMath::Max(MaxSide,FMath::Abs(Current.X-Origin.X));Previous=Current;
  }
  AddInfo(FString::Printf(TEXT("Jittered walking %d FPS: minimum step %.3f cm, lateral %.3f cm"),FPS,MinAdvance,MaxSide));
  TestTrue(TEXT("Late packets correct at the retail speed bound, without an unbounded backwards snap"),
   MinAdvance>=-800.*Dt-.1);
  // Position timing legitimately requires an XY correction. Bound lateral
  // noise by the supplied 2cm rather than letting it accumulate between packets.
  TestTrue(TEXT("Jittered corrections preserve the server's lateral corridor"),MaxSide<=2.01);
  A->Destroy();

  // A ground-only height discrepancy cannot convert otherwise sub-tolerance
  // sideways noise into a correction while standing beside another player.
  A=Spawn();double SideNoise=0;
  for(int I=0;I<FPS*2;++I)
  {
   if(I%(FPS/2)==0){FACEPosition Update=P;Update.Location.X+=(I%FPS?.02:-.02);Update.Location.Z+=.15;A->ApplyACEPosition(Update);}
   A->Tick(Dt);SideNoise=FMath::Max(SideNoise,FMath::Abs(A->GetActorLocation().X-Origin.X));
  }
  TestTrue(TEXT("Sphere seating differences do not defeat the 5cm position tolerance"),SideNoise<.01);
  A->Destroy();

  // A real teleport is an epoch change, even when the destination is nearby.
  A=Spawn();FACEPosition Portal=P;Portal.Location.Y+=3;Portal.TeleportSequence=1;
  A->ApplyACEPosition(Portal);
  TestTrue(TEXT("Nearby server teleport snaps once instead of walking through the intervening wall"),
   FVector::Dist2D(A->GetActorLocation(),Portal.ToUnrealLocation(100))<.1);
  A->Destroy();
  A=Spawn();FACEVRPose JumpPose;JumpPose.Version=2;JumpPose.Root=(Origin+FVector(0,0,100))/100;
  // Simulate a newer F748 landing/zero velocity while an older jump is buffered.
  A->ApplyPhysicsVelocity(FVector::ZeroVector);
  for(int I=0;I<FPS;++I)A->ApplyRemoteVRRoot(JumpPose,Dt);
  TestTrue(TEXT("Newer ground state cannot pull a buffered VR jump onto the floor"),A->GetActorLocation().Z-Origin.Z>99);
  A->Destroy();

  // Ordinary correction is still physics movement in retail. Test the visible
  // root and its selection capsule, not just a collision-constrained predictor.
  auto* Wall=Box(FVector(0,200,200),FVector(400,10,200));
  A=Spawn();FACEPosition Correction=P;Correction.Location.Y+=4;A->ApplyACEPosition(Correction);
  TestTrue(TEXT("A normal position packet initially interpolates rather than teleporting"),A->GetActorLocation().Y-Origin.Y<190);
  double MaxY=0,MaxPickError=0;
  for(int I=0;I<FPS;++I)
  {
   A->Tick(Dt);MaxY=FMath::Max(MaxY,A->GetActorLocation().Y-Origin.Y);
   const FVector Pick=A->CollisionProxy->GetComponentLocation();
   const FVector Expected=A->GetActorTransform().TransformPosition(A->CollisionProxy->GetRelativeLocation());
   MaxPickError=FMath::Max(MaxPickError,FVector::Distance(Pick,Expected));
  }
  TestTrue(TEXT("Blocked correction resynchronizes to the authoritative side of the wall"),FMath::Abs(MaxY-400)<1);
  TestTrue(TEXT("Selection capsule follows the displayed actor throughout reconciliation"),MaxPickError<.01);
  FHitResult PickHit;const FVector PickCenter=A->CollisionProxy->GetComponentLocation();
  World->LineTraceSingleByChannel(PickHit,PickCenter-FVector(200,0,0),PickCenter+FVector(200,0,0),ECC_Visibility);
  TestTrue(TEXT("A selection ray hits the corrected visual body rather than the cached server position"),PickHit.GetActor()==A);
  A->Destroy();Wall->Destroy();

  // Server contact on a different story: collision must not hold the visible
  // body on its old platform while targeting/radar use the new position.
  auto* Platform=Box(FVector(0,0,290),FVector(400,400,10));
  for(bool Player : {true,false})for(bool Falling : {true,false})
  {
   A=Spawn();A->bIsPlayer=Player;
   FACEPosition Upper=P;Upper.Location.Z+=3;Upper.TeleportSequence=1;
   if(Falling)A->ApplyACEPosition(Upper);
   FACEPosition Contact=Falling?P:Upper;Contact.TeleportSequence=Falling?1:0;
   A->ApplyACEPosition(Contact);
   for(int I=0;I<FPS;++I)A->Tick(Dt);
   TestTrue(TEXT("Player/monster corrections recover from both sides of an obstructing floor"),
    FMath::Abs(A->GetActorLocation().Z-Contact.ToUnrealLocation(100).Z)<2);
   A->Destroy();
  }
  Platform->Destroy();

  // Use the real 20 Hz pose buffer at rendering cadence. Applying another
  // locomotion filter must not add a speed-dependent trailing body offset.
  A=Spawn();FACERemoteVRPose Buffer;uint32 Sequence=0;double MaxLag=0;
  for(int I=0;I<FPS*3;++I)
  {
   const double Now=10.+double(I)/FPS;
   while(Sequence*.05<=double(I)/FPS+1.e-7)
   {
    FACEVRPose Pose;Pose.Version=2;Pose.Sequence=Sequence+1;Pose.ReceivedAt=10.+Sequence*.05;
    Pose.Root=(Origin+FVector(0,400*Sequence*.05,1))/100;Buffer.Add(Pose);++Sequence;
   }
   const auto Sample=Buffer.Sample(Now);A->ApplyRemoteVRRoot(Sample,Dt);
   if(I>FPS)MaxLag=FMath::Max(MaxLag,FMath::Abs(A->GetActorLocation().Y-Sample.Root.Y*100));
  }
  AddInfo(FString::Printf(TEXT("Buffered VR %d FPS: %.3f cm extra root lag"),FPS,MaxLag));
  TestTrue(TEXT("VR root and tracked limbs use one buffered movement timeline"),MaxLag<1);
  A->EndRemoteVRRoot();const FVector Before=A->GetActorLocation();
  FACEVRPose Reacquired=Buffer.Current;Reacquired.Root.Y+=2;
  A->ApplyRemoteVRRoot(Reacquired,Dt);
  TestTrue(TEXT("Tracking reacquisition blends into an existing avatar without teleporting"),
   FVector::Distance(A->GetActorLocation(),Before)<800.*Dt+.1);
  A->Destroy();
 }
 // Packet corrections while ascending/descending real risers. Sample the
 // reference from the same DAT body, then replay jittered 1Hz wire positions
 // against an independently simulated/rendered observer.
 for(int S=0;S<4;++S)Box(FVector(0,200+S*180,10+S*10),FVector(400,90,10+S*10));
 for(int FPS:{30,90,144})for(int Direction:{1,-1})
 {
  const float Dt=1.f/FPS;FACEPosition Start=P;
  Start.Location.Y+=Direction>0?-1:8;Start.Location.Z+=Direction>0?0:.8;
  auto MakeWalker=[&]()
  {
   auto* A=Spawn();A->bHaveRemotePredict=false;A->ApplyACEPosition(Start);
   A->RemoteMotion.bMoving=true;A->RemoteMotion.Forward=Direction;A->RemoteMotion.ForwardUnitsPerSecond=4*Direction;
   return A;
  };
  auto* Reference=MakeWalker();TArray<FVector> Path;Path.Add(Reference->GetActorLocation());
  for(int I=0;I<FPS*2;++I){Reference->Tick(Dt);Path.Add(Reference->GetActorLocation());}
  Reference->Destroy();auto* Observer=MakeWalker();double MaxPenetration=0;
  for(int I=1;I<=FPS*2;++I)
  {
   if(I==FPS+FMath::CeilToInt(.05*FPS))
   {
    FACEPosition Packet=Start;Packet.SetLocationFromUnreal(Path[FPS],100);Observer->ApplyACEPosition(Packet);
   }
   Observer->Tick(Dt);const FVector Feet=Observer->GetActorLocation()-Origin;
   const double Radius=Observer->MovementSweepRadius;
   const FVector Center=Feet+FVector(0,0,Observer->MovementBodyOffsetZ-Observer->MovementHalfHeight+Radius);
   for(int S=0;S<4;++S)
   {
    const FBox Tread(FVector(-400,110+S*180,0),FVector(400,290+S*180,20+S*20));
    MaxPenetration=FMath::Max(MaxPenetration,Radius-FMath::Sqrt(Tread.ComputeSquaredDistanceToPoint(Center)));
   }
  }
  const double Travel=(Observer->GetActorLocation().Y-Path[0].Y)*Direction;
  AddInfo(FString::Printf(TEXT("Packet stairs %d FPS direction %d: %.3f cm travel, %.3f cm penetration"),FPS,Direction,Travel,MaxPenetration));
  TestTrue(TEXT("Packet-driven stairs retain forward progress"),Travel>700);
  TestTrue(TEXT("Packet correction cannot embed the visible body in stair risers"),MaxPenetration<1);
  Observer->Destroy();
 }
 for(auto* A:Geometry)if(IsValid(A))A->Destroy();
 GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
