#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatTextureResolver.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEResolvedAppearanceCacheTest,"ACE.Rendering.ResolvedAppearanceCache",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEResolvedAppearanceCacheTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;
 GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT { GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!TestNotNull(TEXT("DAT subsystem"),Dat))return false;
 // Opaque material classifications can grow without adding resolved textures.
 for(uint64 I=0;I<256;++I)Dat->ResolvedMaterialCache.Add(I,NewObject<UMaterialInstanceDynamic>(Dat));
 Dat->TrimResolvedTextureCache(64);
 TestTrue(TEXT("Material retention is bounded even with no resolved textures"),Dat->ResolvedMaterialCache.Num()<=64);
 Dat->ResolvedMaterialCache.Reset();
 FString DatDirectory=TEXT("C:/Turbine/Asheron's Call");
 FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),DatDirectory);
 if(!TestTrue(TEXT("Retail DAT loads"),Dat->LoadDatDirectory(DatDirectory)))return false;
 // Apply the normal first-use material-version invalidation before warming.
 if(!TestTrue(TEXT("DAT material version is ready"),Dat->EnsureLoaded()))return false;
 TArray<TStrongObjectPtr<UMaterialInterface>> Visible;
 for(int32 I=0;I<128;++I)
 {
  auto* Material=Dat->GetOrCreateResolvedMaterial(0x080000DF,0,FACEObjDesc(),float(I+1)/255,1);
  if(!TestNotNull(TEXT("Appearance material builds"),Material))return false;
  Visible.Emplace(Material);
 }
 const uint64 Resolves=Dat->GetTextureResolver()->GetSurfaceResolveCount();
 int32 Rebuilt=0;
 for(int32 Pass=0;Pass<8;++Pass)
 {
  Dat->TrimResolvedTextureCache(64);
  TestTrue(TEXT("Cold texture retention remains bounded"),Dat->ResolvedTextureCache.Num()<=64);
  TestTrue(TEXT("Cold material retention remains bounded"),Dat->ResolvedMaterialCache.Num()<=64);
  for(int32 I=0;I<Visible.Num();++I)
   Rebuilt+=Dat->GetOrCreateResolvedMaterial(0x080000DF,0,FACEObjDesc(),float(I+1)/255,1)!=Visible[I].Get();
 }
 TestEqual(TEXT("A visible crowd larger than the retention budget reuses its materials"),Rebuilt,0);
 TestEqual(TEXT("Repeated crowd refreshes do not decode pixels again"),Dat->GetTextureResolver()->GetSurfaceResolveCount(),Resolves);
 AddInfo(FString::Printf(TEXT("128 live appearances, 8 trim/refresh cycles, rebuilt=%d, extra resolves=%llu"),Rebuilt,Dat->GetTextureResolver()->GetSurfaceResolveCount()-Resolves));
 // Private actor lighting copies must not lose their shared source at GC.
 FACEWorldObject Object;Object.Guid=0x7100C000;Object.SetupId=0x02001A33;Object.MotionTableId=0x0900000C;
 Object.ItemType=ACEItemType::Creature;Object.Name=TEXT("Cache lifetime fixture");
 auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Object,100,true);
 TestTrue(TEXT("Lit appearance retains its shared sources"),!Actor->Appearance->AppearanceSourceMaterials.IsEmpty());
 Dat->ResolvedMaterialCache.Reset();Dat->ResolvedTextureCache.Reset();Dat->TexturedMaterialCache.Reset();
 CollectGarbage(RF_NoFlags);
 for(const auto& Source:Actor->Appearance->AppearanceSourceMaterials)
  TestTrue(TEXT("Actor-owned source survives cache eviction and GC"),IsValid(Source.Get()));
 Dat->TrimResolvedTextureCache(64);
 const uint64 AfterGC=Dat->GetTextureResolver()->GetSurfaceResolveCount();
 for(int32 I=0;I<Visible.Num();++I)
  TestTrue(TEXT("Visible source survives GC and reuses cache identity"),Dat->GetOrCreateResolvedMaterial(0x080000DF,0,FACEObjDesc(),float(I+1)/255,1)==Visible[I].Get());
 TestEqual(TEXT("GC cannot reintroduce repeated appearance decoding"),Dat->GetTextureResolver()->GetSurfaceResolveCount(),AfterGC);
 Actor->Appearance->ClearAppearance();
 TestTrue(TEXT("Appearance retirement releases source ownership"),Actor->Appearance->AppearanceSourceMaterials.IsEmpty());
 Actor->Destroy();
 TArray<TWeakObjectPtr<UMaterialInterface>> Retired;
 for(const auto& Material:Visible)Retired.Add(Material.Get());
 Visible.Reset();Dat->ResolvedMaterialCache.Reset();Dat->ResolvedTextureCache.Reset();
 CollectGarbage(RF_NoFlags);
 Dat->TrimResolvedTextureCache(64);
 for(const auto& Material:Retired)TestFalse(TEXT("Weak reuse does not retain retired appearances"),Material.IsValid());
 return !HasAnyErrors();
}
#endif
