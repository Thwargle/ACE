#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACEScriptComponent.h"
#include "ACECreatureFixtures.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
bool SameMesh(const FACEBuiltSetupMesh& A, const FACEBuiltSetupMesh& B)
{
 if (A.DefaultMotionTableId!=B.DefaultMotionTableId || A.Parts.Num()!=B.Parts.Num()) return false;
 for (int32 P=0; P<A.Parts.Num(); ++P)
 {
  const auto& X=A.Parts[P];const auto& Y=B.Parts[P];
  if (X.GfxObjId!=Y.GfxObjId || X.DrawMode!=Y.DrawMode || X.SortCenter!=Y.SortCenter
   || X.DrawingSphere.Center!=Y.DrawingSphere.Center || X.DrawingSphere.W!=Y.DrawingSphere.W
   || X.MaxDegradeDistance!=Y.MaxDegradeDistance || !X.BindTransform.Equals(Y.BindTransform,0)
   || X.Sections.Num()!=Y.Sections.Num() || X.Portals.Num()!=Y.Portals.Num()) return false;
  for (int32 I=0; I<X.Sections.Num(); ++I)
  {
   const auto& S=X.Sections[I];const auto& T=Y.Sections[I];
   if (S.SurfaceId!=T.SurfaceId || S.bWrapTexture!=T.bWrapTexture || S.bClipMap!=T.bClipMap
    || S.bFullyTransparent!=T.bFullyTransparent || S.bCollisionOnly!=T.bCollisionOnly
    || S.Vertices!=T.Vertices || S.Triangles!=T.Triangles || S.Normals!=T.Normals
    || S.UVs!=T.UVs || S.VertexColors!=T.VertexColors) return false;
  }
  for (int32 I=0; I<X.Portals.Num(); ++I)
   if (X.Portals[I].PortalIndex!=Y.Portals[I].PortalIndex || X.Portals[I].Vertices!=Y.Portals[I].Vertices
    || X.Portals[I].Normal!=Y.Portals[I].Normal) return false;
 }
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECreatureSpawnCacheTest,"ACE.Rendering.CreatureSpawnCache",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECreatureSpawnCacheTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT { GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if (!TestTrue(TEXT("Retail DAT loads"),Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
 const uint32 Setups[]={0x02001A33,ACECreatureFixtures::Models[0].Setup,ACECreatureFixtures::Models[1].Setup,
  ACECreatureFixtures::Models[2].Setup,0x02000001,0x02001AC5,0x01000001};
 TSharedPtr<const FACEBuiltSetupMesh> Held;
 for (uint32 Setup : Setups)
 {
  const auto Plain=Dat->GetOrBuildSetupAppearanceShared(Setup,FACEObjDesc(),100);
  if (!TestTrue(TEXT("Fixture builds"),Plain && Plain->Parts.Num()>0)) return false;
  TestTrue(TEXT("Plain setup uses existing shared cache without a deep copy"),Plain==Dat->GetOrBuildSetupMeshShared(Setup,100));
  FACEObjDesc Appearance;
  auto& Replacement=Appearance.AnimPartChanges.AddDefaulted_GetRef();
  Replacement.PartIndex=0;Replacement.PartId=Plain->Parts[0].GfxObjId;
  if (Setup==0x02000001)
  {
   Appearance.PaletteBaseId=0x0400007E;
   auto& Palette=Appearance.SubPalettes.AddDefaulted_GetRef();Palette.SubPaletteId=0x0400008E;Palette.NumColors=256;
  }
  const auto Cached=Dat->GetOrBuildSetupAppearanceShared(Setup,Appearance,100);
  if (!TestTrue(TEXT("Overridden setup builds"),Cached.IsValid())) return false;
  if (Setup!=0x02000001) TestTrue(TEXT("Geometry-only override retains the original pixels and mesh"),SameMesh(*Plain,*Cached));
  if (Setup==0x02001A33)
   for (int32 P=0; P<Cached->Parts.Num(); ++P)
    for (const auto& Section : Cached->Parts[P].Sections)
     if (!Section.bCollisionOnly && !Section.IsEmpty())
     {
      auto* Original=Dat->GetOrCreateResolvedMaterial(Section.SurfaceId,P,FACEObjDesc(),0,0);
      auto* Replaced=Dat->GetOrCreateResolvedMaterial(Section.SurfaceId,P,Appearance,0,0);
      TestTrue(TEXT("Geometry-only creature changes reuse original opaque surface materials"),Original && Original==Replaced);
     }
  FACEBuiltSetupMesh Direct;
  TestTrue(TEXT("Reference builder succeeds"),Dat->Builder->BuildSetupWithAppearance(Setup,Appearance,100,Direct,0));
  TestTrue(TEXT("All geometry, colors, UVs, physics, portals and placement match uncached build"),SameMesh(*Cached,Direct));
  const uint64 Resolves=Dat->GetTextureResolver()->GetSurfaceResolveCount();
  const double Start=FPlatformTime::Seconds();
  for (int32 I=0; I<20; ++I)
   TestTrue(TEXT("Repeated overridden appearance shares immutable geometry"),Cached==Dat->GetOrBuildSetupAppearanceShared(Setup,Appearance,100));
  const double Warm=FPlatformTime::Seconds()-Start;
  TestEqual(TEXT("Cache hits do not re-resolve surfaces"),Dat->GetTextureResolver()->GetSurfaceResolveCount(),Resolves);
  const double DirectStart=FPlatformTime::Seconds();
  for (int32 I=0; I<20; ++I) Dat->Builder->BuildSetupWithAppearance(Setup,Appearance,100,Direct,0);
  AddInfo(FString::Printf(TEXT("SpawnGeometry setup=%08X parts=%d direct=%.3fms shared=%.3fms per request"),
   Setup,Cached->Parts.Num(),(FPlatformTime::Seconds()-DirectStart)*50,Warm*50));
  if (Setup==0x02001A33)
   TestEqual(TEXT("Warm geometry-only builds borrow pixels without copying or resolving surfaces"),
    Dat->GetTextureResolver()->GetSurfaceResolveCount(),Resolves);
  for (int32 Placement : {0,101})
  {
   const auto Scaled=Dat->GetOrBuildSetupAppearanceShared(Setup,Appearance,50,Placement);
   Dat->Builder->BuildSetupWithAppearance(Setup,Appearance,50,Direct,Placement);
   TestTrue(TEXT("World scale and placement have independent cache entries"),Scaled && Scaled!=Cached && SameMesh(*Scaled,Direct));
  }
  FACEObjDesc Changed=Appearance;Changed.AnimPartChanges[0].PartId=0;
  const auto Different=Dat->GetOrBuildSetupAppearanceShared(Setup,Changed,100);
  TestTrue(TEXT("Part replacement cannot reuse previous appearance"),Different && Different!=Cached);
  TestTrue(TEXT("Original appearance remains reusable after another variant"),Cached==Dat->GetOrBuildSetupAppearanceShared(Setup,Appearance,100));
  Held=Cached;
 }
 // Budget/entry eviction must not invalidate a mesh retained by a caller.
 FACEBuiltSetupMesh Snapshot=*Held;
 FACEObjDesc Crystal;Crystal.PaletteBaseId=0x04000BEF;
 for (int32 Placement=200; Placement<340; ++Placement) Dat->GetOrBuildSetupAppearanceShared(0x02001AC5,Crystal,100,Placement);
 TestTrue(TEXT("Appearance entry count is bounded"),Dat->AppearanceMeshCache.Num()<=128);
 TestTrue(TEXT("Appearance memory is bounded"),Dat->AppearanceMeshCacheBytes<=SIZE_T(PLATFORM_ANDROID?8*1024*1024:32*1024*1024));
 TestTrue(TEXT("Eviction leaves caller-owned geometry valid"),SameMesh(*Held,Snapshot));
 const auto Before=Dat->GetOrBuildSetupAppearanceShared(0x02001AC5,Crystal,100,339);
 for (auto& Pair : Dat->AppearanceMeshCache)
  if (Pair.Value.Mesh==Before) Pair.Value.Appearance.PaletteBaseId=0; // Force a hash-bucket identity mismatch.
 const auto After=Dat->GetOrBuildSetupAppearanceShared(0x02001AC5,Crystal,100,339);
 TestTrue(TEXT("Full appearance comparison rejects a hash collision"),After && Before!=After && SameMesh(*Before,*After));

 FACEWorldObject Object;Object.Guid=0x7100B000;Object.Name=TEXT("Snow Tusker");Object.SetupId=0x02001A33;
 Object.MotionTableId=0x0900000C;Object.ItemType=ACEItemType::Creature;Object.PhysicsState=ACEPhysicsState::Gravity|ACEPhysicsState::ReportCollisions;
 auto* A=World->SpawnActor<AACEWorldEntityActor>();A->InitializeFromObject(Object,100,true);
 ++Object.Guid;auto* B=World->SpawnActor<AACEWorldEntityActor>();B->InitializeFromObject(Object,100,true);
 TestTrue(TEXT("World creature has lighting materials"),A->Appearance->WorldLightingInstances.Num()>0);
 for (const auto& Mid : A->Appearance->WorldLightingInstances)
  TestFalse(TEXT("Separate creatures never share mutable lighting"),B->Appearance->WorldLightingInstances.Contains(Mid));
 UProceduralMeshComponent* First=nullptr;UProceduralMeshComponent* Other=nullptr;UMaterialInterface* Shared=nullptr;int32 FirstSlot=0,OtherSlot=0;
 for (int32 P=0; P<A->Appearance->GetPartCount() && !Other; ++P)
  if (auto* Part=Cast<UProceduralMeshComponent>(A->Appearance->GetPartMesh(P)))
   for (int32 I=0; I<Part->GetNumMaterials(); ++I)
   {
    auto* Mat=Part->GetMaterial(I);
    if (!Mat) continue;
    if (!First){First=Part;FirstSlot=I;Shared=Mat;}
    else if (Part!=First && Mat==Shared){Other=Part;OtherSlot=I;break;}
   }
 if (TestNotNull(TEXT("Repeated multipart surface shares one actor lighting MID"),Other))
 {
  auto* Private=A->ScriptComponent->EnsurePrivateMaterialInstance(First,FirstSlot);
  if (TestNotNull(TEXT("Part hook obtains its own material"),Private))
  {
   Private->SetScalarParameterValue(TEXT("OpacityMul"),.25f);
   TestTrue(TEXT("Part-specific effect cannot change its sibling's material"),Private!=Shared && Other->GetMaterial(OtherSlot)==Shared);
   float SiblingOpacity=0;Shared->GetScalarParameterValue(TEXT("OpacityMul"),SiblingOpacity);
   TestTrue(TEXT("Sibling opacity is unaffected"),!FMath::IsNearlyEqual(SiblingOpacity,.25f));
  }
 }
 AddInfo(FString::Printf(TEXT("Snow Tusker lighting instances=%d parts=%d"),A->Appearance->WorldLightingInstances.Num(),A->Appearance->GetPartCount()));
 A->Destroy();B->Destroy();
 Dat->ClearLoadedState();
 TestEqual(TEXT("DAT unload drops appearance entries"),Dat->AppearanceMeshCache.Num(),0);
 TestEqual(TEXT("DAT unload clears retained-byte accounting"),Dat->AppearanceMeshCacheBytes,SIZE_T(0));
 TestTrue(TEXT("Caller-held geometry survives DAT cache reset"),SameMesh(*Held,Snapshot));
 return !HasAnyErrors();
}
#endif
