#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEntityLookupTest,"ACE.Performance.EntityLookup",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEEntityLookupTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 auto* Host=World->SpawnActor<AActor>();auto* Presenter=NewObject<UACEWorldPresenterComponent>(Host);
 for(int I=1;I<=256;++I)
 {
  auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->ACEGuid=I;Presenter->Spawned.Add(I,Actor);
 }
 auto* Expected=Presenter->Spawned.FindRef(256).Get();
 TestTrue(TEXT("Selection resolves live GUID"),Presenter->FindEntityActor(256)==Expected);
 TestNull(TEXT("Pending GUID does not resolve another object"),Presenter->FindEntityActor(257));
 for(int Phase=0;Phase<4;++Phase)for(bool Indexed:{false,true})
 {
  int Found=0;const double Begin=FPlatformTime::Seconds();
  for(int I=0;I<2000;++I)
  {
   AACEWorldEntityActor* Target=nullptr;
   if(Indexed)Target=Presenter->FindEntityActor(256);
   else for(TActorIterator<AACEWorldEntityActor> It(World);It;++It)
    if(It->GetACEGuid()==256){Target=*It;break;}
   Found+=Target==Expected;
  }
  AddInfo(FString::Printf(TEXT("256-entity selection lookup %s phase %d: %.3f us/call"),Indexed?TEXT("indexed"):TEXT("scan"),Phase,
   (FPlatformTime::Seconds()-Begin)*1.e6/2000));
  TestEqual(TEXT("Indexed and scanning lookups agree"),Found,2000);
 }
 Presenter->Spawned.Remove(256);
 TestNull(TEXT("Leaving residency removes selection actor immediately"),Presenter->FindEntityActor(256));
 auto* Replacement=World->SpawnActor<AACEWorldEntityActor>();Replacement->ACEGuid=256;Presenter->Spawned.Add(256,Replacement);
 TestTrue(TEXT("Respawn resolves replacement rather than stale cached pointer"),Presenter->FindEntityActor(256)==Replacement);
 Replacement->Destroy();
 TestNull(TEXT("Destroyed actor is rejected even before map cleanup"),Presenter->FindEntityActor(256));
 Presenter->Spawned.Empty();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
