#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatCursor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/ScopeExit.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEWorldEntityActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "../ACEBodySweep.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECollisionSizingTest,"ACE.Collision.RetailSizing",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACECollisionSizingTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;GI->Init();
 ON_SCOPE_EXIT{World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
 auto* Controller=World->SpawnActor<AACEPlayerController>();Controller->Client=Client;
 auto* Pawn=World->SpawnActor<APawn>();auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
 Pawn->SetRootComponent(Capsule);Pawn->AddInstanceComponent(Capsule);Capsule->InitCapsuleSize(34,88);Capsule->RegisterComponent();
 Controller->Possess(Pawn);Pawn->SetActorEnableCollision(false);
 FACEWorldObject Self;Self.Guid=12345;Session->PlayerGuid=Self.Guid;
 int32 Cases=0;
 for(uint32 Id:{0x02000001u,0x02000004u,0x02000A95u,0x02000037u,0x02001121u,0x02000059u,0x0200003Du})
 {
  TArray<uint8> Bytes; if(!TestTrue(TEXT("Retail sizing fixture exists in DAT"),Dat->GetPortalDat()->ReadFile(Id,Bytes)))continue;
  FACEDatCursor Cursor(Bytes);FACEDatSetupModel Setup;
  if(!TestTrue(TEXT("Retail sizing fixture unpacks"),ACEDatUnpack::UnpackSetupModel(Cursor,Setup)))continue;
  AddInfo(FString::Printf(TEXT("Setup %08X height %.6f radius %.6f"),Id,Setup.Height,Setup.Radius));
  for(const auto& S:Setup.Spheres)AddInfo(FString::Printf(TEXT("Sphere %s radius %.6f"),*FVector(S.Origin).ToString(),S.Radius));
  for(const auto& S:Setup.CylSpheres)AddInfo(FString::Printf(TEXT("Cylinder %s radius %.6f height %.6f"),*FVector(S.Origin).ToString(),S.Radius,S.Height));
  for(float Scale:{.1f,.7f,1.f,2.5f,6.f})
  {
   Self.SetupId=Id;Self.Scale=Scale;Session->WorldObjects.Add(Self.Guid,Self);
   Pawn->SetActorScale3D(FVector(Scale));Controller->ApplyPlayerCapsuleFromSetup(Id);
   const auto Body=Controller->GetPlayerCollisionBody();
   TestEqual(TEXT("Movement uses retail's first two spheres, not an enclosing capsule"),Body.Count,FMath::Min(2,Setup.Spheres.Num()));
   for(int32 I=0;I<Body.Count;++I)
   {
    const auto& S=Setup.Spheres[I];
    TestTrue(TEXT("Player sphere radius follows DAT and object scale without clamps"),FMath::IsNearlyEqual(Body.Radii[I],S.Radius*Scale*100,.001f));
    const FVector FromOrigin=Body.Centers[I]+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
    TestTrue(TEXT("Player sphere center retains the authored foot/head offset"),FromOrigin.Equals(FACEPosition::AceVectorToUnreal(FVector(S.Origin),Scale*100),.001));
   }
   FACEWorldObject Mob;Mob.Guid=1000;Mob.SetupId=Id;Mob.Scale=Scale;Mob.ItemType=ACEItemType::Creature;
   Mob.PhysicsState=ACEPhysicsState::Gravity|ACEPhysicsState::ReportCollisions;Mob.bHasPosition=true;
   Mob.Position.CellId=0x7D640001;Mob.Position.Location=FVector(20,20,12);
   auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Mob,100,true);
   TArray<USphereComponent*> Components;Actor->GetComponents(Components);
   Components.RemoveAll([](const auto* C){return !C->ComponentHasTag(TEXT("ACECollisionOnly"));});
   if(Setup.CylSpheres.IsEmpty())
   {
    TestEqual(TEXT("Blocking creatures retain every authored sphere"),Components.Num(),Setup.Spheres.Num());
    for(int32 I=0;I<FMath::Min(Components.Num(),Setup.Spheres.Num());++I)
    {
     const auto& S=Setup.Spheres[I];
     TestTrue(TEXT("Creature blocking radius scales exactly once"),FMath::IsNearlyEqual(Components[I]->GetScaledSphereRadius(),S.Radius*Scale*100,.01f));
     TestTrue(TEXT("Creature blocking center retains the DAT offset"),Components[I]->GetComponentLocation().Equals(
      Actor->GetActorTransform().TransformPosition(FACEPosition::AceVectorToUnreal(FVector(S.Origin),100)),.01));
    }
   }
   TestEqual(TEXT("Selection capsule does not enlarge a creature's physical body"),Actor->CollisionProxy->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
   Actor->Destroy();++Cases;
  }
 }
 TestEqual(TEXT("All player morph and creature scale fixtures executed"),Cases,35);
 // A known setup with no physics body must not inherit the enlarged pick
 // capsule. Retail's object collision loop has no shapes to collide against.
 constexpr uint32 EmptySetup=0x02000054;
 TArray<FACEDatCollisionShape> EmptyShapes;bool EmptyHasBsp=false;
 if(TestTrue(TEXT("DAT fixture has no physical collision"),Dat->GetSetupCollisionShapes(EmptySetup,EmptyShapes,EmptyHasBsp)
  && EmptyShapes.IsEmpty() && !EmptyHasBsp))
 {
  FACEWorldObject Prop;Prop.Guid=1001;Prop.SetupId=EmptySetup;
  Prop.PhysicsState=ACEPhysicsState::ReportCollisions;Prop.bHasPosition=true;
  Prop.Position.CellId=0x7D640001;Prop.Position.Location=FVector(20,20,12);
  auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Prop,100,true);
  AddInfo(FString::Printf(TEXT("Nonphysical setup fixture: %08X"),EmptySetup));
  TestEqual(TEXT("An empty authored body never falls back to selection-volume blocking"),Actor->CollisionProxy->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
  Actor->Destroy();
 }
 // Distinct radii and horizontal offsets must survive every query, even if
 // the ordinary human happens to use equal, centered spheres.
 FACECollisionBody OffsetBody(FCollisionShape::MakeCapsule(10,30));
 OffsetBody.Centers[0]=FVector(8,0,-20);OffsetBody.Radii[0]=4;
 OffsetBody.Centers[1]=FVector(-9,0,15);OffsetBody.Radii[1]=8;
 auto* Obstacle=World->SpawnActor<AActor>();auto* Sphere=NewObject<USphereComponent>(Obstacle);
 Obstacle->SetRootComponent(Sphere);Sphere->InitSphereRadius(2);Sphere->ComponentTags.Add(TEXT("ACECollisionOnly"));
 Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
 Sphere->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);Sphere->RegisterComponent();
 FCollisionQueryParams Q(SCENE_QUERY_STAT(OffsetBodySizing),false,Pawn);
 for(int32 I=0;I<2;++I)
 {
  Obstacle->SetActorLocation(OffsetBody.Centers[I]+FVector(30,0,0));FHitResult Hit;
  TestTrue(TEXT("Authored offset sphere hits the obstacle"),ACEBodySweep::SweepBody(*World,Hit,FVector::ZeroVector,FVector(50,0,0),OffsetBody,Q));
  TestTrue(TEXT("Contact distance uses that sphere's radius"),FMath::IsNearlyEqual(Hit.Location.X,30-2-OffsetBody.Radii[I],.01));
 }
 return true;
}
#endif
