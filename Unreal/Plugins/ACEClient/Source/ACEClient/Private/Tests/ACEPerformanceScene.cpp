#if WITH_DEV_AUTOMATION_TESTS
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEPlaySessionRedirect.h"
#include "ACERenderAudit.h"
#include "ACETerrainPresenterComponent.h"
#include "ACELandblockActor.h"
#include "ACEWorldEntityActor.h"
#include "ACEScriptComponent.h"
#include "ACECreatureFixtures.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "UnrealClient.h"

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
  if(Scene!=TEXT("outdoor") && Scene!=TEXT("indoor") && Scene!=TEXT("effects") && Scene!=TEXT("caul") && Scene!=TEXT("swarm") && Scene!=TEXT("frozen"))return;
  UE_LOG(LogTemp,Display,TEXT("ACE PerfScene preparing %s"),*Scene);
  // The login path intentionally defers cell DAT indexing. This offline setup
  // phase is outside the timed sample and needs both databases immediately.
  if(!Dat->LoadDatDirectory(Dat->GetDatDirectory()))return;
  auto Run=MakeShared<FACEPerformanceScene>();Run->World=World;Run->Scene=Scene;
  FACEPosition Spawn;Spawn.CellId=0x7D640001;Spawn.Location=FVector(100,100,0);
  // Dense authored ambient particles at the landblock captured on Quest.
  if(Scene==TEXT("caul"))Spawn.CellId=0x09050001;
  if(Scene==TEXT("frozen"))
  {
   // 83.7N, 4.5W: use the reported location, with authored region scenery.
   const FVector Global((101.95-4.5)*240.,(101.95+83.7)*240.,0);
   const int32 X=FMath::FloorToInt(Global.X/192.),Y=FMath::FloorToInt(Global.Y/192.);
   Spawn.CellId=(uint32(X)<<24)|(uint32(Y)<<16)|1;
   Spawn.Location=Global-FVector(X*192,Y*192,0);
  }
  if(Scene==TEXT("swarm"))
  {
   Spawn.CellId=0x01430171;Spawn.Location=FVector(49.011993,-74.999023,0);
   Spawn.RotationW=.932723f;Spawn.RotationXYZ=FVector(0,0,-.360594);
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
  const int32 Count=Scene==TEXT("frozen")?0:Scene==TEXT("swarm")?64:Scene==TEXT("indoor")?8:32;
  for(int32 I=0;I<Count;++I)
  {
   FACEWorldObject NPC=Self;NPC.Guid+=I+1;NPC.bIsSelf=false;NPC.bIsPlayer=false;
   NPC.Position.Location+=FVector(3+(I%8)*1.4,(I/8-1.5)*1.4,0);
   if(Scene==TEXT("swarm"))
   {
    ACECreatureFixtures::Apply(NPC,I);
    NPC.Position.Location=Spawn.Location+FVector((I%8-3.5)*.45,(I/8-3.5)*.45,0);
   }
   if(Scene!=TEXT("indoor") && Scene!=TEXT("swarm"))
   {
    const FVector P=NPC.Position.ToUnrealLocation(100);float Z=0;
    if(Dat->SampleOutdoorGroundZ(P.X,P.Y,100,Z))NPC.Position.Location.Z=Z/100;
    NPC.Position.NormalizeOutdoorLandblock();
   }
   Session->WorldObjects.Add(NPC.Guid,NPC);Client->OnObjectCreated.Broadcast(NPC);
  }
  Run->Camera=World->SpawnActor<ACameraActor>();
  const FVector Center=Spawn.ToUnrealLocation(100);
  const FVector Forward=FACEPosition::AceVectorToUnreal(FVector(1,0,0),1);
  // The standalone fixture has a full desktop avatar. An eye at its head clips
  // through the model and hides much of the measured scene. Keep outdoor runs
  // behind/above it, looking over the synthetic crowd. The indoor eye sits
  // just in front of the avatar, still inside the same authored room.
  const bool Interior=Scene==TEXT("indoor") || Scene==TEXT("swarm");
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
  PC->SetViewTarget(Run->Camera.Get());
  Run->StartTime=Run->LastTime=FPlatformTime::Seconds();
  FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float){return Run->Tick();}));
  UE_LOG(LogTemp,Display,TEXT("ACE PerfScene %s: %d synthetic NPCs, 45-second warmup, 30-second sample."),*Scene,Count);
 }
private:
 TWeakObjectPtr<UWorld> World;
 TWeakObjectPtr<ACameraActor> Camera;
 FVector CameraOrigin=FVector::ZeroVector;
 FString Scene,Directory;
 double StartTime=0,LastTime=0;
 double LastEffectTime=0;
 bool bSampling=false, bWarmupCapture=false, bControlledView=false;
 double SampleStartTime=0;
 uint64 PreviousFrame=uint64(-1);
 TArray<double> Frames;
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
  const double Now=FPlatformTime::Seconds(),Elapsed=Now-StartTime,Ms=(Now-LastTime)*1000;LastTime=Now;
  if(FParse::Param(FCommandLine::Get(),TEXT("ACEPerfCameraMotion")))
   Camera->SetActorLocation(CameraOrigin+FVector(8*FMath::Sin(Elapsed),8*FMath::Cos(Elapsed),2*FMath::Sin(2*Elapsed)));
  auto* PC=World->GetFirstPlayerController();PC->SetViewTarget(Camera.Get());
  if(!bControlledView && Elapsed>=5 && FParse::Param(FCommandLine::Get(),TEXT("ACEPerfCleanView")))
  {
   // Apply after login preferences have restored the user's ordinary window.
   // This changes only the offline run, not the saved preferences.
   bControlledView=true;
   GEngine->Exec(World.Get(),TEXT("r.SetRes 1920x1080w"));
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
  if(!bWarmupCapture && Elapsed>=20)
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
  }
  if(!bSampling || Now-SampleStartTime<30)return true;
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
 TEXT("Disconnected development benchmark: outdoor, indoor, effects, caul, swarm or frozen. Writes Saved/Performance after 75 seconds. -ACEPerfCleanView uses a hidden HUD at 1920x1080."),
 FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FACEPerformanceScene::Start));
#endif
