#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACELandblockActor.h"
#include "Dat/ACEDatFileTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ACETypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRSceneryCollisionTest,"ACE.VR.SceneryCollision",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEVRSceneryCollisionTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))return false;
 TArray<FACEDatRegionSceneryItem> Objects;
 for(uint32 Landblock : {0xA9B40000u, 0x7D640000u})
 {
  TArray<FACEDatRegionSceneryItem> Region;
  Dat->CollectRegionScenery(Landblock,100,Region);Objects.Append(Region);
 }
 TSet<uint32> Seen;
 bool Tested=false;
 for(const auto& Object:Objects)
 {
  if(Seen.Contains(Object.SetupId))continue;Seen.Add(Object.SetupId);
  const auto Built=Dat->GetOrBuildSetupMeshShared(Object.SetupId,100,0);
  if(!Built)continue;
  FVector Point,Normal; bool Physics=false;
  for(const auto& Part:Built->Parts)for(const auto& S:Part.Sections)
  {
   if(S.bCollisionOnly && S.Triangles.Num()>=3)
   {
    const auto A=Part.BindTransform.TransformPosition(S.Vertices[S.Triangles[0]]);
    const auto B=Part.BindTransform.TransformPosition(S.Vertices[S.Triangles[1]]);
    const auto C=Part.BindTransform.TransformPosition(S.Vertices[S.Triangles[2]]);
    Point=(A+B+C)/3;Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();Physics=!Normal.IsNearlyZero();
   }
  }
  TArray<FACEDatCollisionShape> Shapes;bool HasBSP=false;
  Dat->GetSetupCollisionShapes(Object.SetupId,Shapes,HasBSP);
  if(!Physics && !Shapes.IsEmpty() && Shapes[0].Radius>0)
  {
   const auto& Shape=Shapes[0];
   Point=FACEPosition::AceVectorToUnreal(FVector(Shape.Origin),100)+FVector(Shape.Radius*100,0,Shape.Height*50);
   Normal=FVector::ForwardVector;Physics=true;
  }
  if(!Physics)continue;
  auto* Block=World->SpawnActor<AACELandblockActor>();
  AACELandblockActor::FPendingScenery Item;Item.ModelId=Object.SetupId;Item.bRegionDesc=true;Item.Scale=1;
  TestTrue(TEXT("Region scenery completes from its cached DAT setup"),Block->TrySpawnOneScenery(Dat,Item));
  auto* Found=Block->SceneryHisms.Find(AACELandblockActor::SceneryMeshKey(Object.SetupId,0,true));
  if(TestTrue(TEXT("Region tree uses an instance"),Found && *Found))
  {
   TestTrue(TEXT("Region tree collision is enabled"),(*Found)->IsCollisionEnabled());
   FHitResult Hit;
   TestTrue(TEXT("Authored trunk physics blocks the same pawn query used by desktop and VR"),
    World->LineTraceSingleByChannel(Hit,Point+Normal*10,Point-Normal*10,ECC_Pawn));
   if(Hit.bBlockingHit)TestTrue(TEXT("The collision belongs to the scenery instance"),Hit.GetComponent()==*Found);
   auto* Instance=Found->Get();
   Block->TrySpawnOneScenery(Dat,Item);
   TestEqual(TEXT("Repeated identical scenery is not drawn or collided twice"),Instance->GetInstanceCount(),1);
   Dat->GetOrBuildSetupMeshShared(Object.SetupId,100,UACEDatSubsystem::ACEPlacementResting);
   auto* Resting=Block->GetOrCreateSceneryHism(Dat,Object.SetupId,true,UACEDatSubsystem::ACEPlacementResting);
   TestTrue(TEXT("A different authored placement cannot reuse the wrong mesh"),Resting && Resting!=Instance);
  }
  AddInfo(FString::Printf(TEXT("Region scenery authored collision tested: %08X"),Object.SetupId));
  Tested=true;break;
 }
 TestTrue(TEXT("Region DAT supplies scenery with authored solid geometry"),Tested);
 GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
