#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEProjectileLifetimeTest,"ACE.Rendering.ProjectileLifetime",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEProjectileLifetimeTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Owner=World->SpawnActor<AActor>();auto* Presenter=NewObject<UACEWorldPresenterComponent>(Owner);
 Owner->AddInstanceComponent(Presenter);Presenter->RegisterComponent();Presenter->bApplyDatAppearance=false;
 FACEWorldObject Projectile;Projectile.Guid=12345;Projectile.Name=TEXT("missed spell");
 Projectile.bHasPosition=true;Projectile.Position.CellId=0xC98C0030;
 Projectile.PhysicsState=ACEPhysicsState::Missile;
 Presenter->HandleObjectCreated(Projectile);
 TestTrue(TEXT("Projectile spawns immediately"),Presenter->Spawned.Contains(Projectile.Guid));
 if (Presenter->ProjectileFlights.Contains(Projectile.Guid))
 {
  const double Deadline=Presenter->ProjectileFlights[Projectile.Guid].ExpiresAt;
  Presenter->HandleObjectCreated(Projectile);
  TestEqual(TEXT("Repeated updates cannot restart projectile lifetime"),Presenter->ProjectileFlights[Projectile.Guid].ExpiresAt,Deadline);
  Presenter->ExpireProjectileVisuals(Deadline-.001);
  TestTrue(TEXT("Normal flight and impact tail survive until the deadline"),Presenter->Spawned.Contains(Projectile.Guid));
  auto* Visual=Presenter->Spawned.FindRef(Projectile.Guid).Get();
  Presenter->ExpireProjectileVisuals(Deadline);
  TestFalse(TEXT("Missing ObjectDelete cannot leave a projectile actor alive"),Presenter->Spawned.Contains(Projectile.Guid));
  TestTrue(TEXT("Expiry destroys the actor and its effect components"),Visual && Visual->IsActorBeingDestroyed());
  Presenter->HandleObjectCreated(Projectile);
  TestFalse(TEXT("Late updates cannot resurrect an expired projectile"),Presenter->Spawned.Contains(Projectile.Guid));
  Presenter->HandleObjectDeleted(Projectile.Guid);
  TestFalse(TEXT("Real deletion clears the tombstone"),Presenter->ProjectileFlights.Contains(Projectile.Guid));
  Presenter->HandleObjectCreated(Projectile);
  TestTrue(TEXT("A legitimately reused GUID can spawn after deletion"),Presenter->Spawned.Contains(Projectile.Guid));
  Presenter->HandlePhysicsStateUpdate(Projectile.Guid,0);
  TestFalse(TEXT("Landed ammunition is no longer subject to the flight watchdog"),Presenter->ProjectileFlights.Contains(Projectile.Guid));
 }
 else AddError(TEXT("World missile must have a lifetime"));
 Projectile.Guid=12346;Projectile.ParentGuid=100;Projectile.WielderId=100;
 Presenter->HandleObjectCreated(Projectile);
 TestFalse(TEXT("Equipped ammunition never starts a flight timer"),Presenter->ProjectileFlights.Contains(Projectile.Guid));
 Presenter->ClearSpawned();
 TestTrue(TEXT("World reset clears projectile bookkeeping"),Presenter->ProjectileFlights.IsEmpty());
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
