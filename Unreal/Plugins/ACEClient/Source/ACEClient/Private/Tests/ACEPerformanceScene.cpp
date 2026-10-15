#if WITH_DEV_AUTOMATION_TESTS
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEPlaySessionRedirect.h"
#include "ACERenderAudit.h"
#include "ACEFrameDiagnostics.h"
#include "ACETerrainPresenterComponent.h"
#include "ACELandblockActor.h"
#include "ACEWorldEntityActor.h"
#include "ACEScriptComponent.h"
#include "ACECreatureFixtures.h"
#include "ACEInputBindings.h"
#include "GameFramework/PlayerInput.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "UnrealClient.h"

// Retail ACE weenie 43592; the model/effect inputs are shared by the rendered
// Frozen Valley benchmark and the lifecycle regression below.
static void ACEPerfApplySnowTusker(FACEWorldObject& Object)
{
 Object.Name=TEXT("Snow Tusker");Object.SetupId=0x02001A33;Object.MotionTableId=0x0900000C;
 Object.SoundTableId=0x20000011;Object.PhysicsEffectTableId=0x34000027;
 Object.ItemType=ACEItemType::Creature;Object.Scale=1.5f;
 Object.PhysicsState=ACEPhysicsState::Gravity | ACEPhysicsState::ReportCollisions;
}

// Deliberately offline: exercise the normal presenters/HUD without credentials,
// database writes, or benchmark entities leaking onto a connected server.
class FACEPerformanceScene : public TSharedFromThis<FACEPerformanceScene>
{
public:
 static void Start(const TArray<FString>& Args,UWorld* World)
 {
  if(!World || !World->GetGameInstance())return;
  auto* Client=World->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
  auto* Dat=World->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
  auto* PC=Cast<AACEPlayerController>(World->GetFirstPlayerController());
  auto Session=Client?Client->GetSession():nullptr;
  if(!PC || !Dat || !Session || Session->GetState()!=EACESessionState::Disconnected)
  { UE_LOG(LogTemp,Error,TEXT("ACE PerfScene requires a disconnected standalone client."));return; }
  if(!Dat->EnsureLoaded())
  {
   const TWeakObjectPtr<UWorld> PendingWorld=World;
   const double Deadline=FPlatformTime::Seconds()+30;
   FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([PendingWorld,Args,Deadline](float)
   {
    if(!PendingWorld.IsValid() || FPlatformTime::Seconds()>Deadline)return false;
    auto* ReadyDat=PendingWorld->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    if(!ReadyDat || !ReadyDat->EnsureLoaded())return true;
    Start(Args,PendingWorld.Get());return false;
   }));
   return;
  }
  const FString Scene=Args.Num()?Args[0]:TEXT("outdoor");
  if(Scene!=TEXT("outdoor") && Scene!=TEXT("indoor") && Scene!=TEXT("effects") && Scene!=TEXT("caul") && Scene!=TEXT("swarm") && Scene!=TEXT("wasps") && Scene!=TEXT("frozen") && Scene!=TEXT("frozen-tuskers"))return;
  UE_LOG(LogTemp,Display,TEXT("ACE PerfScene preparing %s"),*Scene);
  // The login path intentionally defers cell DAT indexing. This offline setup
  // phase is outside the timed sample and needs both databases immediately.
  if(!Dat->LoadDatDirectory(Dat->GetDatDirectory()))return;
  auto Run=MakeShared<FACEPerformanceScene>();Run->World=World;Run->Scene=Scene;
  Run->bControlResolution=FParse::Value(FCommandLine::Get(),TEXT("ACEPerfResolution="),Run->PerfResolution)
   || FParse::Param(FCommandLine::Get(),TEXT("ACEPerfCleanView"));
  FACEPosition Spawn;Spawn.CellId=0x7D640001;Spawn.Location=FVector(100,100,0);
  // Dense authored ambient particles at the landblock captured on Quest.
  if(Scene==TEXT("caul"))Spawn.CellId=0x09050001;
  if(Scene==TEXT("frozen") || Scene==TEXT("frozen-tuskers"))
  {
   // 83.7N, 4.5W: use the reported location, with authored region scenery.
   const FVector Global((101.95-4.5)*240.,(101.95+83.7)*240.,0);
   const int32 X=FMath::FloorToInt(Global.X/192.),Y=FMath::FloorToInt(Global.Y/192.);
   Spawn.CellId=(uint32(X)<<24)|(uint32(Y)<<16)|1;
   Spawn.Location=Global-FVector(X*192,Y*192,0);
  }
  if(Scene==TEXT("swarm") || Scene==TEXT("wasps"))
  {
   Spawn.CellId=0x01430171;Spawn.Location=FVector(49.011993,-74.999023,0);
   Spawn.RotationW=.932723f;Spawn.RotationXYZ=FVector(0,0,-.360594);
   if(Scene==TEXT("wasps"))
   {
    Spawn.CellId=0x0143015F;Spawn.Location=FVector(43.304642,-68.413086,-.002981);
    Spawn.RotationW=.898748f;Spawn.RotationXYZ=FVector(0,0,-.438466);
   }
   if(!Dat->GetOrBuildEnvCellMesh(Spawn.CellId,100))return;
  }
  else if(Scene==TEXT("indoor"))
  {
   Spawn.CellId=0xC88C0143;
   const auto* Built=Dat->GetOrBuildEnvCellMesh(Spawn.CellId,100);
   if(!Built)return;
   const FTransform Frame=Built->GetCellLocalToLandblock(100);
   bool Found=false;
   for(const auto& Section:Built->CollisionSections)
   {
    for(int32 I=0;I+2<Section.Triangles.Num();I+=3)
    {
     const FVector A=Frame.TransformPosition(Section.Vertices[Section.Triangles[I]]);
     const FVector B=Frame.TransformPosition(Section.Vertices[Section.Triangles[I+1]]);
     const FVector C=Frame.TransformPosition(Section.Vertices[Section.Triangles[I+2]]);
     if(FMath::Abs(FVector::CrossProduct(B-A,C-A).GetSafeNormal().Z)>.99 && FMath::Abs(A.Z-2200)<1)
     { Spawn.Location=FACEPosition::AceVectorToUnreal((A+B+C)/3,.01f);Found=true;break; }
    }
    if(Found)break;
   }
   if(!Found)return;
  }
  else
  {
   Dat->GetOrBuildLandblockMesh(Spawn.CellId & 0xFFFF0000,100);
   const FVector P=Spawn.ToUnrealLocation(100);float Z=0;
   if(!Dat->SampleOutdoorGroundZ(P.X,P.Y,100,Z))return;
   Spawn.Location.Z=Z/100;Spawn.NormalizeOutdoorLandblock();
  }
  Session->PlayerGuid=0x7100F000;Session->PlayerPosition=Spawn;
  FACEWorldObject Self;Self.Guid=Session->PlayerGuid;Self.Name=TEXT("Offline performance fixture");
  Self.SetupId=0x02000001;Self.MotionTableId=0x09000001;Self.ItemType=ACEItemType::Creature;
  Self.bIsPlayer=true;Self.bIsSelf=true;Self.bHasPosition=true;Self.Position=Spawn;
  Session->WorldObjects.Add(Self.Guid,Self);
  Session->ApplyServerTime(39755.0*7620.0-3600.0+3810.0,FPlatformTime::Seconds(),TEXT("offline performance fixture"));
  Session->SetState(EACESessionState::InWorld);
  ACEPlaySessionRedirect::GPendingWcTravelMapAfterLogin.Empty();
  PC->bUseEnterWorldLoadScreen=false;
  Client->OnEnteredWorld.Broadcast(Self.Guid,Spawn);
  // A static camera benchmark has no walking input. Keep the reported cell
  // while the asynchronous dungeon loads instead of predicting a fall in it.
  if(Scene==TEXT("wasps"))PC->bSyncPossessedPawn=false;
  int32 Count=Scene==TEXT("frozen")?0:Scene==TEXT("frozen-tuskers")?12:Scene==TEXT("swarm")?64:Scene==TEXT("indoor")?8:32;
  if(Scene==TEXT("wasps"))
  {
   FParse::Value(FCommandLine::Get(),TEXT("ACEPerfWaspCount="),Count);Count=FMath::Clamp(Count,1,128);
   FParse::Value(FCommandLine::Get(),TEXT("ACEPerfVolleyInterval="),Run->VolleyInterval);
   Run->VolleyInterval=FMath::Clamp(Run->VolleyInterval,.5f,3.f);
   Run->bPlayerMotion=FParse::Param(FCommandLine::Get(),TEXT("ACEPerfPlayerMotion"));
   Run->PlayerOrigin=Spawn.ToUnrealLocation(100);
  }
  for(int32 I=0;I<Count;++I)
  {
   FACEWorldObject NPC=Self;NPC.Guid+=I+1;NPC.bIsSelf=false;NPC.bIsPlayer=false;
   NPC.Position.Location+=FVector(3+(I%8)*1.4,(I/8-1.5)*1.4,0);
   if(Scene==TEXT("frozen-tuskers"))ACEPerfApplySnowTusker(NPC);
   if(Scene==TEXT("swarm"))
   {
    ACECreatureFixtures::Apply(NPC,I);
    NPC.Position.Location=Spawn.Location+FVector((I%8-3.5)*.45,(I/8-3.5)*.45,0);
   }
   if(Scene==TEXT("wasps"))
   {
    ACECreatureFixtures::Apply(NPC,2);NPC.SoundTableId=0x2000000E;NPC.PhysicsEffectTableId=0x34000022;
    // WCID 7105: clothing 10000564, White (61), shade .5.
    NPC.Appearance.PaletteBaseId=0x040018FE;
    FACEObjDescSubPalette White;White.SubPaletteId=0x04001906;White.NumColors=2048;
    NPC.Appearance.SubPalettes.Add(White);
    NPC.Position.Location=Spawn.Location+FVector(-2-(I%8)*.5,-1.5+(I/8-1.5)*.5,0);
    Run->Wasps.Add(NPC);
   }
   if(Scene!=TEXT("indoor") && Scene!=TEXT("swarm") && Scene!=TEXT("wasps"))
   {
    const FVector P=NPC.Position.ToUnrealLocation(100);float Z=0;
    if(Dat->SampleOutdoorGroundZ(P.X,P.Y,100,Z))NPC.Position.Location.Z=Z/100;
    NPC.Position.NormalizeOutdoorLandblock();
   }
   Session->WorldObjects.Add(NPC.Guid,NPC);Client->OnObjectCreated.Broadcast(NPC);
   if(Scene==TEXT("frozen-tuskers"))Run->SnowCreatures.Add(NPC);
  }
  Run->Camera=World->SpawnActor<ACameraActor>();
  const FVector Center=Spawn.ToUnrealLocation(100);
  const FVector Forward=FACEPosition::AceVectorToUnreal(FVector(Scene==TEXT("wasps")?-1:1,0,0),1);
  // The standalone fixture has a full desktop avatar. An eye at its head clips
  // through the model and hides much of the measured scene. Keep outdoor runs
  // behind/above it, looking over the synthetic crowd. The indoor eye sits
  // just in front of the avatar, still inside the same authored room.
  const bool Interior=Scene==TEXT("indoor") || Scene==TEXT("swarm") || Scene==TEXT("wasps");
  FVector Eye=Interior ? Center+Forward*100+FVector(0,0,170)
   : Center-Forward*900+FVector(0,0,600);
  const FVector LookAt=Center+Forward*800+FVector(0,0,100);
  if(!Interior)
  {
   // Caul is steep enough that a fixed backward offset can put the camera
   // inside a hillside. Clear the sampled ground along the viewing segment.
   for(int32 I=0;I<=8;++I)
   {
    const FVector P=FMath::Lerp(Eye,LookAt,I/8.f); float GroundZ=0;
    if(Dat->SampleOutdoorGroundZ(P.X,P.Y,100,GroundZ)) Eye.Z=FMath::Max(Eye.Z,GroundZ+600.f);
   }
  }
  Run->Camera->SetActorLocation(Eye);
  Run->CameraOrigin=Eye;
  Run->Camera->SetActorRotation(Interior ? Forward.Rotation()
   : (LookAt-Eye).Rotation());
  Run->Camera->GetCameraComponent()->SetFieldOfView(90);
  Run->Camera->GetCameraComponent()->bConstrainAspectRatio=false;
  PC->SetViewTarget(Run->Camera.Get());
  Run->StartTime=FPlatformTime::Seconds();
  FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float){return Run->Tick();}));
  UE_LOG(LogTemp,Display,TEXT("ACE PerfScene %s: %d synthetic NPCs, 45-second warmup, %d-second sample."),*Scene,Count,Scene==TEXT("wasps")?60:30);
 }
private:
 TWeakObjectPtr<UWorld> World;
 TWeakObjectPtr<ACameraActor> Camera;
 FVector CameraOrigin=FVector::ZeroVector;
 FString Scene,Directory;
 FString PerfResolution=TEXT("1920x1080");
 bool bControlResolution=false;
 double StartTime=0;
 double LastEffectTime=0;
 bool bSampling=false, bWarmupCapture=false, bControlledView=false;
 double SampleStartTime=0;
 uint64 PreviousFrame=uint64(-1);
 TArray<double> Frames;
 TArray<FACEWorldObject> SnowCreatures;
 int32 SnowPhase=0;
 TArray<FACEWorldObject> Wasps;
 TArray<TWeakObjectPtr<AACEWorldEntityActor>> FrostBolts;
 TArray<double> WaspFrames[4];
 int32 WaspPhase=-1;
 double LastVolley=0;
 bool bVolleyImpacted=false;
 bool bWaspCorpses=false;
 bool bWaspFlightReported=false;
 float VolleyInterval=3;
 bool bPlayerMotion=false,bMotionStarted=false,bWasAirborne=false;
 FVector PlayerOrigin=FVector::ZeroVector,PreviousPlayerPosition=FVector::ZeroVector;
 TSet<FKey> HeldKeys;
 int32 JumpCount=0,AirborneFrames=0,MovementFrames=0,MotionTarget=0,VolleyCount=0;
 double TravelCm=0,MaxHeightCm=0;
 FString MovementCSV=TEXT("elapsed_s,wall_ms,phase,airborne,x_cm,y_cm,z_cm\n");
 void SetMovementKeys(AACEPlayerController* PC,const TArray<FKey>& Actions)
 {
  TSet<FKey> Desired;
  for(const auto& Action:Actions)
  {
   const auto Chord=ACEInputBindings::Get(Action,0);
   if(Chord.Key.IsValid())Desired.Add(Chord.Key);
   if(Chord.bShift)Desired.Add(EKeys::LeftShift);
   if(Chord.bCtrl)Desired.Add(EKeys::LeftControl);
   if(Chord.bAlt)Desired.Add(EKeys::LeftAlt);
   if(Chord.bCmd)Desired.Add(EKeys::LeftCommand);
  }
  for(const auto& Key:HeldKeys)if(!Desired.Contains(Key))
   PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Released,0.f));
  for(const auto& Key:Desired)if(!HeldKeys.Contains(Key))
   PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f));
  HeldKeys=MoveTemp(Desired);
 }
 void TickPlayerMotion(double Elapsed,double Ms)
 {
  if(!bPlayerMotion || Elapsed<15)return;
  auto* PC=Cast<AACEPlayerController>(World->GetFirstPlayerController());
  APawn* Pawn=PC?PC->GetPawn():nullptr;if(!Pawn)return;
  // Wait for the room to load, then use the real keyboard/controller path.
  // Never teleport the pawn along a curve: collision, jumping and landing are
  // part of the workload. The small circuit stays in the reported wasp room.
  PC->bSyncPossessedPawn=true;
  const FVector At=Pawn->GetActorLocation();
  if(!bMotionStarted){bMotionStarted=true;PreviousPlayerPosition=At;}
  static const FVector Offsets[]={FVector(0,-110,0),FVector(130,-110,0),FVector(130,110,0),FVector(0,110,0)};
  FVector To=PlayerOrigin+Offsets[MotionTarget]-At;To.Z=0;
  if(To.Size()<45){MotionTarget=(MotionTarget+1)%UE_ARRAY_COUNT(Offsets);To=PlayerOrigin+Offsets[MotionTarget]-At;To.Z=0;}
  // AC faces local +Y, unlike Unreal's conventional actor +X forward.
  const FVector Facing=Pawn->GetActorQuat().RotateVector(FVector::YAxisVector);
  const double Angle=FMath::FindDeltaAngleDegrees(Facing.Rotation().Yaw,To.Rotation().Yaw);
  TArray<FKey> Keys;
  if(Angle>8)Keys.Add(EKeys::D);else if(Angle<-8)Keys.Add(EKeys::A);
  if(FMath::Abs(Angle)<65)Keys.Add(EKeys::W);
  else if(FMath::Abs(Angle)<110)Keys.Add(Angle>0?EKeys::E:EKeys::Q);
  // Alternate short and fully charged jumps; keep turning/strafe inputs held
  // in the air, just as a player circling through a packed room would.
  const double Cycle=FMath::Fmod(Elapsed-15,2.4);
  const double Charge=(int32((Elapsed-15)/2.4)%2)?1.0:.35;
  if(Cycle<Charge)Keys.Add(EKeys::SpaceBar);
  SetMovementKeys(PC,Keys);
  const bool Airborne=PC->IsJumpAirborne();
  if(bSampling)
  {
   ++MovementFrames;AirborneFrames+=Airborne?1:0;if(Airborne && !bWasAirborne)++JumpCount;
   TravelCm+=FVector::Dist2D(At,PreviousPlayerPosition);
   MaxHeightCm=FMath::Max(MaxHeightCm,At.Z-PlayerOrigin.Z-90.75);
   const FVector Local=At-PlayerOrigin;
   MovementCSV+=FString::Printf(TEXT("%.6f,%.6f,%d,%d,%.3f,%.3f,%.3f\n"),Elapsed,Ms,WaspPhase,Airborne,Local.X,Local.Y,Local.Z);
  }
  PreviousPlayerPosition=At;bWasAirborne=Airborne;
 }
 void TickWasps(double Now,double Elapsed)
 {
  if(Wasps.IsEmpty())return;
  // Warm shaders/meshes first, then compare idle, casts, stopped casts and death
  // in the same process. Movement is opt-in; no network traffic is sent.
  const int32 Phase=bSampling?FMath::Clamp(int32((Now-SampleStartTime)/15),0,3):(Elapsed>20 && Elapsed<35?1:0);
  if(Phase!=WaspPhase)
  {
   WaspPhase=Phase;UE_LOG(LogTemp,Display,TEXT("ACE PerfScene wasps phase=%d"),Phase);
   if(Phase==3)
   {
    FACEObjectMotionState Death;Death.ForwardCommand=ACEMotion::Dead;Death.CurrentStyle=ACEMotion::StanceNonCombat;
    for(TActorIterator<AACEWorldEntityActor> It(World.Get());It;++It)
     if(It->ACEGuid>0x7100F000 && It->ACEGuid<=0x7100F000+Wasps.Num())It->ApplyMotionState(Death);
   }
  }
  if(!FrostBolts.IsEmpty() && !bVolleyImpacted && Now-LastVolley>.45)
  {
   bVolleyImpacted=true;
   for(auto& Bolt:FrostBolts)if(Bolt.IsValid())
   {
    Bolt->ApplyPhysicsVelocity(FVector::ZeroVector);
    Bolt->ApplyPhysicsState(Bolt->PhysicsState|ACEPhysicsState::NoDraw|ACEPhysicsState::Cloaked);
    Bolt->ScriptComponent->PlayEffect(5,1);
   }
  }
  if(bSampling && Phase==1 && !bWaspFlightReported && Now-LastVolley>.2 && !bVolleyImpacted)
  {
   bWaspFlightReported=true;
   for(const auto& Line:ACEFrameDiagnostics::Describe(World.Get(),0))
    UE_LOG(LogTemp,Display,TEXT("ACE PerfScene wasp flight: %s"),*Line);
  }
  if(!FrostBolts.IsEmpty() && Now-LastVolley>2.5)
  {
   for(auto& Bolt:FrostBolts)if(Bolt.IsValid())Bolt->Destroy();
   FrostBolts.Reset();
  }
  if(Phase==3 && !bWaspCorpses && Now-SampleStartTime>47)
  {
   bWaspCorpses=true;
   auto* Client=World->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
   auto Session=Client->GetSession();
   for(const auto& Wasp:Wasps)
   {
    Session->WorldObjects.Remove(Wasp.Guid);Client->OnObjectDeleted.Broadcast(Wasp.Guid);
    FACEWorldObject Corpse=Wasp;Corpse.Guid+=0x100;Corpse.Name=TEXT("Corpse of White Phyntos Wasp");
    Corpse.ItemType=ACEItemType::Container;Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse|ACEObjectDescFlag::Openable;
    Corpse.PhysicsState=ACEPhysicsState::Ethereal;
    Session->WorldObjects.Add(Corpse.Guid,Corpse);Client->OnObjectCreated.Broadcast(Corpse);
   }
  }
  if(Phase!=1 || Now-LastVolley<VolleyInterval)return;
  for(auto& Bolt:FrostBolts)if(Bolt.IsValid())Bolt->Destroy();
  FrostBolts.Reset();LastVolley=Now;bVolleyImpacted=false;if(bSampling)++VolleyCount;
  for(TActorIterator<AACEWorldEntityActor> It(World.Get());It;++It)
   if(It->ACEGuid>0x7100F000 && It->ACEGuid<=0x7100F000+Wasps.Num())
   {
    FACEObjectMotionState Cast;Cast.CurrentStyle=ACEMotion::StanceNonCombat;
    Cast.ActionCommand=0x400000D3;Cast.bNewAction=true;It->ApplyMotionState(Cast);
   }
  for(int32 I=0;I<Wasps.Num()*3;++I)
  {
   // ACE weenie 1503, used by both Frost Bolt IV and Frost Volley IV.
   FACEWorldObject Bolt;Bolt.Guid=0x71010000+I;Bolt.Name=TEXT("Frost Bolt");
   Bolt.SetupId=0x020003F4;Bolt.SoundTableId=0x20000036;Bolt.PhysicsEffectTableId=0x34000006;
   Bolt.Scale=.4f;Bolt.PhysicsState=166728;Bolt.Position=Wasps[I/3].Position;
   Bolt.Position.Location+=FVector(0,(I%3-1)*.25,1.2);Bolt.bHasPosition=true;
   Bolt.Velocity=FVector(8,0,0);Bolt.bHasVelocity=true;
   auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Bolt,100,true);
   Actor->ScriptComponent->PlayEffect(4,1);FrostBolts.Add(Actor);
  }
 }
 // Alternate inside one process to reduce startup/GPU-clock/background noise.
 // This mode is opt-in and applies only to the disconnected swarm fixture.
 int32 CrowdMode=-1;
 TArray<double> CrowdFrames[6];
 bool Tick()
 {
  if(!World.IsValid() || !Camera.IsValid())return false;
  // Slate/loading pumps may tick the core ticker more than once per frame.
  if(PreviousFrame==GFrameCounter)return true;
  PreviousFrame=GFrameCounter;
  const double Now=FPlatformTime::Seconds(),Elapsed=Now-StartTime;
  // The fixture runs in the core ticker after world ticking. Measuring from
  // one callback to the next combines last frame's volley creation with this
  // frame's component ticks. Use engine frame boundaries instead, so a burst
  // spanning two rendered frames is not reported as one larger stall.
  const double Ms=FApp::GetDeltaTime()*1000;
  TickWasps(Now,Elapsed);
  TickPlayerMotion(Elapsed,Ms);
  if(FParse::Param(FCommandLine::Get(),TEXT("ACEPerfCameraMotion")))
   Camera->SetActorLocation(CameraOrigin+FVector(8*FMath::Sin(Elapsed),8*FMath::Cos(Elapsed),2*FMath::Sin(2*Elapsed)));
  auto* PC=World->GetFirstPlayerController();
  PC->SetViewTarget(bPlayerMotion && bMotionStarted ? Cast<AActor>(PC->GetPawn()) : Camera.Get());
  if(!bControlledView && Elapsed>=5 && bControlResolution)
  {
   // Apply after login preferences have restored the user's ordinary window.
   // This changes only the offline run, not the saved preferences.
   bControlledView=true;
   GEngine->Exec(World.Get(),*FString::Printf(TEXT("r.SetRes %sw"),*PerfResolution));
   if(FParse::Param(FCommandLine::Get(),TEXT("ACEPerfCleanView")))
    if(auto* Controller=Cast<AACEPlayerController>(PC))Controller->SetDesktopInterfaceHidden(true);
  }
  if(Scene==TEXT("effects") && Elapsed>20 && Now-LastEffectTime>2)
  {
   int32 Played=0;
   for(TActorIterator<AACEWorldEntityActor> It(World.Get());It && Played<4;++It)
    if(It->ScriptComponent && It->ACEGuid>=0x7100F001 && It->ACEGuid<=0x7100F020)
    { It->ScriptComponent->PlayScriptId(0x330000D5,1);++Played; }
   LastEffectTime=Now;
  }
  if(!SnowCreatures.IsEmpty() && Elapsed>20 && Now-LastEffectTime>3)
  {
   auto* Client=World->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
   auto Session=Client ? Client->GetSession() : nullptr;
   if(!Session || Session->PlayerGuid!=0x7100F000 || Session->GetState()!=EACESessionState::InWorld)return false;
   if(SnowPhase==0)
   {
    FACEObjectMotionState Death;Death.ForwardCommand=ACEMotion::Dead;Death.CurrentStyle=ACEMotion::StanceNonCombat;
    for(TActorIterator<AACEWorldEntityActor> It(World.Get());It;++It)
     if(SnowCreatures.ContainsByPredicate([&](const FACEWorldObject& O){return O.Guid==It->ACEGuid;}))It->ApplyMotionState(Death);
   }
   else for(const auto& Creature:SnowCreatures)
   {
    const uint32 Removed=Creature.Guid+(SnowPhase==2?0x100:0);
    Session->WorldObjects.Remove(Removed);Client->OnObjectDeleted.Broadcast(Removed);
    FACEWorldObject Replacement=Creature;
    if(SnowPhase==1)
    {
     Replacement.Guid+=0x100;Replacement.Name=TEXT("Corpse of Snow Tusker");Replacement.ItemType=ACEItemType::Container;
     Replacement.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse|ACEObjectDescFlag::Openable;
     Replacement.PhysicsState=ACEPhysicsState::Ethereal;
    }
    Session->WorldObjects.Add(Replacement.Guid,Replacement);Client->OnObjectCreated.Broadcast(Replacement);
   }
   UE_LOG(LogTemp,Display,TEXT("ACE PerfScene Snow Tusker lifecycle phase=%d"),SnowPhase);
   SnowPhase=(SnowPhase+1)%3;LastEffectTime=Now;
  }
  if(!bWarmupCapture && Elapsed>=(Wasps.IsEmpty()?20:26))
  {
   bWarmupCapture=true;Directory=FPaths::ProjectSavedDir()/TEXT("Performance")/Scene;
   IFileManager::Get().MakeDirectory(*Directory,true);
   // Screenshot readback/encoding is diagnostic work, not gameplay. It can
   // stall for seconds on a first ES3.1 capture, so finish it before sampling.
   FACERenderAudit::Collect(World.Get()).Log();
   FScreenshotRequest::RequestScreenshot(Directory/TEXT("scene.png"),true,false);
   return true;
  }
  if(!bSampling && Elapsed>=45)
  {
   bSampling=true;SampleStartTime=Now;
   if(!FTraceAuxiliary::IsConnected())
   {
    const FString TracePath=Directory/FString::Printf(TEXT("scene-%s.utrace"),*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
    GEngine->Exec(World.Get(),*FString::Printf(TEXT("Trace.File \"%s\" cpu,gpu,frame,bookmark"),*TracePath));
   }
   UE_LOG(LogTemp,Display,TEXT("ACE PerfScene sampling %s"),*Scene);
   return true;
  }
  if(bSampling)
  {
   if(Scene==TEXT("swarm") && FParse::Param(FCommandLine::Get(),TEXT("ACEPerfCompareCrowd")))
   {
    const int32 Block=FMath::Clamp(int32((Now-SampleStartTime)/5),0,5);
    if(CrowdMode!=Block)
    {
     CrowdMode=Block;
     IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Collision.BatchCrowdQueries"))->Set(Block%2,ECVF_SetByCode);
     UE_LOG(LogTemp,Display,TEXT("ACE PerfScene crowd comparison block=%d batch=%d"),Block,Block%2);
     return true; // discard the frame spanning the change
    }
    CrowdFrames[Block].Add(Ms);
   }
   Frames.Add(Ms);
   if(!Wasps.IsEmpty())WaspFrames[WaspPhase].Add(Ms);
  }
  if(!bSampling || Now-SampleStartTime<(Wasps.IsEmpty()?30:60))return true;
  if(bPlayerMotion)
  {
   SetMovementKeys(Cast<AACEPlayerController>(PC),{});
   FFileHelper::SaveStringToFile(MovementCSV,*(Directory/TEXT("movement.csv")));
   const FString Motion=FString::Printf(TEXT("{\"wasps\":%d,\"volleys\":%d,\"projectiles\":%d,\"jumps\":%d,\"airborne_frames\":%d,\"frames\":%d,\"travel_m\":%.3f,\"max_jump_height_cm\":%.3f}"),
    Wasps.Num(),VolleyCount,VolleyCount*Wasps.Num()*3,JumpCount,AirborneFrames,MovementFrames,TravelCm/100,MaxHeightCm);
   FFileHelper::SaveStringToFile(Motion,*(Directory/TEXT("movement-summary.json")));
   UE_LOG(LogTemp,Display,TEXT("ACE PerfScene movement %s"),*Motion);
  }
  GEngine->Exec(World.Get(),TEXT("Trace.Stop"));
  FString CSV=TEXT("frame,wall_ms\n");double Total=0;
  for(int32 I=0;I<Frames.Num();++I){Total+=Frames[I];CSV+=FString::Printf(TEXT("%d,%.6f\n"),I,Frames[I]);}
  FFileHelper::SaveStringToFile(CSV,*(Directory/TEXT("frames.csv")));
  Frames.Sort();
  auto Percentile=[&](double Q){return Frames[FMath::Clamp(FMath::CeilToInt(Frames.Num()*Q)-1,0,Frames.Num()-1)];};
  FVector2D Size;GEngine->GameViewport->GetViewportSize(Size);
  const FString Summary=FString::Printf(TEXT("{\"scene\":\"%s\",\"synthetic\":true,\"width\":%.0f,\"height\":%.0f,\"frames\":%d,\"mean_ms\":%.3f,\"p50_ms\":%.3f,\"p95_ms\":%.3f,\"p99_ms\":%.3f,\"mean_fps\":%.2f}"),
   *Scene,Size.X,Size.Y,Frames.Num(),Total/Frames.Num(),Percentile(.5),Percentile(.95),Percentile(.99),1000*Frames.Num()/Total);
  FFileHelper::SaveStringToFile(Summary,*(Directory/TEXT("summary.json")));
  if(!Wasps.IsEmpty())
  {
   FString Phases=TEXT("[");
   for(int32 I=0;I<4;++I)
   {
    double Sum=0;for(double Frame:WaspFrames[I])Sum+=Frame;
    WaspFrames[I].Sort();const int32 N=WaspFrames[I].Num();
    const TCHAR* Names[]={TEXT("idle"),TEXT("casting"),TEXT("stopped_casting"),TEXT("death_and_corpses")};
    Phases+=FString::Printf(TEXT("%s{\"phase\":\"%s\",\"frames\":%d,\"mean_ms\":%.3f,\"p99_ms\":%.3f,\"max_ms\":%.3f}"),
     I?TEXT(","):TEXT(""),Names[I],N,Sum/FMath::Max(1,N),N?WaspFrames[I][FMath::Clamp(FMath::CeilToInt(N*.99)-1,0,N-1)]:0,N?WaspFrames[I].Last():0);
   }
   Phases+=TEXT("]");FFileHelper::SaveStringToFile(Phases,*(Directory/TEXT("wasp-phases.json")));
  }
  if(CrowdMode>=0)
  {
   FString Comparison=TEXT("[");
   for(int32 Block=0;Block<6;++Block)
   {
    const auto& Samples=CrowdFrames[Block];double Sum=0;for(double Sample:Samples)Sum+=Sample;
    Comparison+=FString::Printf(TEXT("%s{\"block\":%d,\"batch\":%d,\"frames\":%d,\"mean_ms\":%.6f}"),
     Block?TEXT(","):TEXT(""),Block,Block%2,Samples.Num(),Samples.IsEmpty()?0:Sum/Samples.Num());
   }
   Comparison+=TEXT("]");FFileHelper::SaveStringToFile(Comparison,*(Directory/TEXT("crowd-comparison.json")));
  }
  UE_LOG(LogTemp,Display,TEXT("ACE PerfScene result %s"),*Summary);
  if(FParse::Param(FCommandLine::Get(),TEXT("ACEPerfQuit")))GEngine->Exec(World.Get(),TEXT("Quit"));
  return false;
 }
};
static FAutoConsoleCommandWithWorldAndArgs GACEPerfScene(TEXT("ace.PerfScene"),
 TEXT("Disconnected development benchmark: outdoor, indoor, effects, caul, swarm, wasps (idle/cast/stop/death), frozen or frozen-tuskers (spawn/death/corpse cycles). Writes Saved/Performance after 75 seconds (wasps: 105). -ACEPerfCleanView hides HUD; -ACEPerfResolution=WxH overrides 1920x1080. Wasps: -ACEPerfPlayerMotion exercises keyboard movement/jumps, -ACEPerfWaspCount=32 (1-128), -ACEPerfVolleyInterval=3 (.5-3 seconds)."),
 FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FACEPerformanceScene::Start));
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEScriptComponent.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESnowTuskerPerformanceTest, "ACE.Rendering.SnowTuskerLifecycle",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACESnowTuskerPerformanceTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
 GI->OnWorldChanged(nullptr,World); GI->Init();
 ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!TestTrue(TEXT("Retail DAT loads"),Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
 // ACE retail Snow Tusker (43592), also used throughout Frozen Valley.
 FACEWorldObject Object; Object.Guid=0x7100A000;ACEPerfApplySnowTusker(Object);
 for(int32 Pass=0;Pass<4;++Pass)
 {
  const uint64 Resolves=Dat->GetTextureResolver()->GetSurfaceResolveCount();
  const double Start=FPlatformTime::Seconds();
  auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Object,100,true);
  const double Spawned=FPlatformTime::Seconds();
  if(!TestTrue(TEXT("Snow Tusker appearance builds"),Actor->Appearance->HasAppearance()))return false;
  int32 Vertices=0,Sections=0;
  for(int32 P=0;P<Actor->Appearance->GetPartCount();++P)
   if(auto* Part=Cast<UProceduralMeshComponent>(Actor->Appearance->GetPartMesh(P)))
    for(int32 S=0;S<Part->GetNumSections();++S){++Sections;Vertices+=Part->GetProcMeshSection(S)->ProcVertexBuffer.Num();}
  FACEObjectMotionState Death;Death.ForwardCommand=ACEMotion::Dead;Death.CurrentStyle=ACEMotion::StanceNonCombat;
  Actor->ApplyMotionState(Death);
  for(int32 Frame=0;Frame<180;++Frame)
  {Actor->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);Actor->ScriptComponent->TickComponent(1.f/60,LEVELTICK_All,nullptr);}
  const double Died=FPlatformTime::Seconds();
  FACEWorldObject Corpse=Object;Corpse.Guid+=100;Corpse.Name=TEXT("Corpse of Snow Tusker");Corpse.ItemType=ACEItemType::Container;
  Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse|ACEObjectDescFlag::Openable;Corpse.PhysicsState=ACEPhysicsState::Ethereal;
  auto* Body=World->SpawnActor<AACEWorldEntityActor>();Body->InitializeFromObject(Corpse,100,true);
  const double BodyBuilt=FPlatformTime::Seconds();
  TestTrue(TEXT("Corpse retains the Snow Tusker model"),Body->Appearance->HasAppearance());
  for(int32 Frame=0;Frame<180;++Frame)Body->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
  const double Finished=FPlatformTime::Seconds();
  TestEqual(TEXT("Tusker retains its authored multipart setup"),Actor->Appearance->GetPartCount(),25);
  TestEqual(TEXT("Optimization preserves the complete drawing geometry"),Vertices,7753);
  TestTrue(TEXT("Corpse remains an openable object"),Body->IsCorpse() && Body->IsOpenable());
  if(Pass>0)TestEqual(TEXT("Warm creature and corpse spawns do not re-resolve decoded pixels"),Dat->GetTextureResolver()->GetSurfaceResolveCount(),Resolves);
  AddInfo(FString::Printf(TEXT("SnowTusker pass=%d parts=%d sections=%d vertices=%d spawn=%.3fms death180=%.3fms corpse=%.3fms corpse180=%.3fms surfaceResolves=%llu emitters=%d"),
   Pass,Actor->Appearance->GetPartCount(),Sections,Vertices,(Spawned-Start)*1000,(Died-Spawned)*1000,
   (BodyBuilt-Died)*1000,(Finished-BodyBuilt)*1000,Dat->GetTextureResolver()->GetSurfaceResolveCount()-Resolves,Actor->ScriptComponent->ActiveEmitters.Num()));
  Actor->Destroy();Body->Destroy();++Object.Guid;
 }
 return !HasAnyErrors();
}
#endif
