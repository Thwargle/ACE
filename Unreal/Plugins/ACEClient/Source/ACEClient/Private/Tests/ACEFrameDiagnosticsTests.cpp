#if WITH_DEV_AUTOMATION_TESTS
#include "ACEFrameDiagnostics.h"
#include "ACEProfiling.h"
#include "ACEWorldEntityActor.h"
#include "ACEClientSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEFrameDiagnosticsTest,"ACE.Rendering.FrameDiagnostics",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEFrameDiagnosticsTest::RunTest(const FString&)
{
 ACEProfiling::FFrameHistory Costs;
 using ACEProfiling::EScope;
 TestNull(TEXT("No fabricated timings before the first recorded frame"),Costs.Completed(1));
 Costs.Add(10,EScope::Remote,100);Costs.Add(10,EScope::Remote,200);Costs.Add(10,EScope::Network,50);
 TestNull(TEXT("An unfinished frame is not reported as complete"),Costs.Completed(10));
 TestEqual(TEXT("All creature ticks accumulate in one frame"),Costs.Completed(11)->Cycles[uint8(EScope::Remote)],uint64(300));
 Costs.Add(11,EScope::Network,20);
 TestEqual(TEXT("Early next-frame ticks preserve the completed frame"),Costs.Completed(11)->Cycles[uint8(EScope::Network)],uint64(50));
 TestEqual(TEXT("Frame counters do not accumulate for the whole login"),Costs.Completed(12)->Cycles[uint8(EScope::Remote)],uint64(0));
 TestNull(TEXT("Old work cannot be attributed to an unmeasured later frame"),Costs.Completed(14));
 FACEFrameReportGate Gate;
 TestFalse(TEXT("Loading stalls are ignored"),Gate.Update(0,500,false));
 TestFalse(TEXT("First gameplay frame starts a grace period"),Gate.Update(1,500,true));
 TestFalse(TEXT("The first settling frame does not spam reports"),Gate.Update(1.5,500,true));
 TestFalse(TEXT("Ordinary frames are ignored"),Gate.Update(3,16,true));
 TestFalse(TEXT("Frames above 20 FPS are ignored"),Gate.Update(4,49,true));
 TestTrue(TEXT("Facility Hub's 76 ms first-room stall is captured"),Gate.Update(5,76,true));
 TestFalse(TEXT("Consecutive slow frames are throttled"),Gate.Update(5.5,86,true));
 TestFalse(TEXT("Portal loading resets grace period"),Gate.Update(14,486,false));
 TestFalse(TEXT("Resuming gameplay remains quiet initially"),Gate.Update(24,486,true));
 TestFalse(TEXT("Grace period applies even after report cooldown"),Gate.Update(24.5,486,true));
 TestTrue(TEXT("The reported 486 ms gameplay stall is captured"),Gate.Update(25,486,true));
 int32 FurtherReports=0;
 for(int32 I=0;I<1000;++I)FurtherReports+=Gate.Update(46+11.*I,486,true);
 TestEqual(TEXT("Even sustained 2 FPS cannot fill the log indefinitely"),FurtherReports,18);
 Gate.Update(12000,0,false);Gate.Update(12001,0,true);
 TestFalse(TEXT("Loading does not reset the per-session report limit"),Gate.Update(12020,486,true));

 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;
 GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT { GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
 auto* Creature=World->SpawnActor<AACEWorldEntityActor>();Creature->ItemType=ACEItemType::Creature;
 auto* Corpse=World->SpawnActor<AACEWorldEntityActor>();Corpse->ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;
 auto* Projectile=World->SpawnActor<AACEWorldEntityActor>();Projectile->PhysicsState=ACEPhysicsState::Missile;
 const auto Lines=ACEFrameDiagnostics::Describe(World,486);
 TestTrue(TEXT("A manual report remains available without a viewport or connection"),Lines.Num()>=2);
 if(Lines.Num()>=2)
 {
  TestTrue(TEXT("Report retains the observed frame duration"),Lines[0].Contains(TEXT("frame=486.0 ms")));
  TestTrue(TEXT("Live world counts distinguish corpses and projectiles"),Lines[1].Contains(TEXT("creatures=1 dying=0 corpses=1 projectiles=1")));
  TestTrue(TEXT("Empty scene effects do not acquire new particle work"),Lines[1].Contains(TEXT("emitters=0 degraded=0 particles=0")));
 }
 TestEqual(TEXT("Diagnostic does not modify projectile state"),Projectile->PhysicsState,int32(ACEPhysicsState::Missile));
 return true;
}
#endif
