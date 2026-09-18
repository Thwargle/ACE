#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformMemory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEColdTerrainLoadTest, "ACE.RetailParity.ColdTerrainLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEColdTerrainLoadTest::RunTest(const FString& Parameters)
{
    FACEDatDatabase Portal, Cell, HighRes;
    const double OpenStart = FPlatformTime::Seconds();
    if (!Portal.Open(TEXT("C:/Turbine/Asheron's Call/client_portal.dat"))
        || !Cell.Open(TEXT("C:/Turbine/Asheron's Call/client_cell_1.dat"))
        || !HighRes.Open(TEXT("C:/Turbine/Asheron's Call/client_highres.dat"))) return false;
    AddInfo(FString::Printf(TEXT("DAT indexing: %.3fs"), FPlatformTime::Seconds()-OpenStart));
    // Both destinations are from the user's >1 minute first-login logs. Build all
    // 121 blocks with the runtime's two-worker limit, without a warm disk cache.
    for (uint32 Center : {0x2B120000u, 0x7E650000u})
    {
        TArray<uint32> Blocks;
        for (int32 X=-5; X<=5; ++X) for (int32 Y=-5; Y<=5; ++Y)
            Blocks.Add((uint32(int32(Center>>24)+X)<<24) | (uint32(int32((Center>>16)&255)+Y)<<16));
        TSet<uint32> Codes; FCriticalSection Mutex;
        int32 Errors=0, Sections=0;
        uint64 Pixels=0;
        const double Start=FPlatformTime::Seconds();
        auto Blends = MakeShared<FACETerrainBlendCache, ESPMode::ThreadSafe>();
        ParallelFor(2,[&](int32 Lane)
        {
            for (int32 I=Lane; I<Blocks.Num(); I+=2)
            {
                FACEDatTextureResolver Textures(&Portal,&HighRes);
                FACELandblockMeshBuilder Builder(&Portal,&Cell,&Textures,Blends);
                FACEBuiltLandblockMesh Mesh;
                const bool Ok=Builder.BuildLandblock(Blocks[I],100,Mesh);
                FScopeLock Lock(&Mutex);
                if (!Ok || !Mesh.bHasHeights || !Mesh.bHasTerrain || Mesh.IsEmpty()) ++Errors;
                for (const auto& Section : Mesh.Sections)
                {
                    ++Sections; Codes.Add(Section.PCode);
                    Pixels+=Section.GetBakedPixels().Num();
                    if (!Section.HasBakedTexture()) ++Errors;
                }
            }
        });
        TestEqual(TEXT("All visible terrain and textures build"),Errors,0);
        TestTrue(TEXT("Recent blend retention is bounded"),Blends->GetRetainedBytes() <= (128ull << 20));
        AddInfo(FString::Printf(TEXT("ColdTerrain %08X: %.3fs, %d blocks, %d sections, %d unique blends, %.1f MiB pixel output, %.1f MiB process memory"),
            Center,FPlatformTime::Seconds()-Start,Blocks.Num(),Sections,Codes.Num(),Pixels*4.0/(1024*1024),
            FPlatformMemory::GetStats().UsedPhysical/(1024.0*1024.0)));
    }
    // Independent raw TexMerge output is the oracle: sharing cannot change pixels,
    // including a road transition, nor the terrain's height/geometry at that blend.
    FACEDatTextureResolver Textures(&Portal,&HighRes);
    FACELandblockMeshBuilder Builder(&Portal,&Cell,&Textures);
    FACEBuiltLandblockMesh Mesh;
    TestTrue(TEXT("Comparison terrain loads"),Builder.BuildLandblock(0x7E650000,100,Mesh));
    for (const auto& Section : Mesh.Sections)
    {
        TArray<FColor> Raw; int32 W=0,H=0;
        TestTrue(TEXT("Raw reference TexMerge bake"),Builder.BakePCode(Section.PCode,Raw,W,H));
        TestTrue(TEXT("Shared pixels exactly match raw TexMerge"),Raw==Section.GetBakedPixels()
            && W==Section.BakeWidth && H==Section.BakeHeight);
    }
    // Concurrent neighboring jobs must neither duplicate the bake nor corrupt it.
    FACETerrainBlendCache Cache(0);
    TArray<FACETerrainBlendCache::FBlendPtr> Held; Held.SetNum(16);
    FThreadSafeCounter Builds;
    auto Build=[&]() -> FACETerrainBlendCache::FBlendPtr
    {
        Builds.Increment();
        auto B=MakeShared<FACETerrainBlend,ESPMode::ThreadSafe>();
        B->Width=1; B->Height=1; B->Pixels={FColor::Red}; return B;
    };
    ParallelFor(16,[&](int32 I) { Held[I]=Cache.GetOrBuild(7,11,Build); });
    TestEqual(TEXT("Concurrent requests bake one immutable result"),Builds.GetValue(),1);
    for (const auto& B:Held) TestTrue(TEXT("Neighbors share the same storage"),B==Held[0]);
    TestEqual(TEXT("Visible users keep data alive without retained cache memory"),Cache.GetRetainedBytes(),uint64(0));
    auto Changed=Cache.GetOrBuild(7,12,Build);
    TestTrue(TEXT("Different DAT version gets different pixels"),Changed!=Held[0]);
    Held.Empty();
    auto Rebuilt=Cache.GetOrBuild(7,11,Build);
    TestEqual(TEXT("Unreferenced evicted data is rebuilt safely"),Builds.GetValue(),3);
    return !HasAnyErrors();
}
#endif
