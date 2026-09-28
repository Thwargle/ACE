#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACETypes.h"
#include "Dat/ACECellTransit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
    bool SameInfo(const FACEDatLandblockInfo& A, const FACEDatLandblockInfo& B)
    {
        if (A.Id != B.Id || A.NumCells != B.NumCells || A.Objects.Num() != B.Objects.Num()
            || A.Buildings.Num() != B.Buildings.Num()) return false;
        for (int I = 0; I < A.Objects.Num(); ++I)
        {
            const auto& X = A.Objects[I]; const auto& Y = B.Objects[I];
            if (X.Id != Y.Id || X.Origin != Y.Origin || X.Orientation != Y.Orientation) return false;
        }
        for (int I = 0; I < A.Buildings.Num(); ++I)
        {
            const auto& X = A.Buildings[I]; const auto& Y = B.Buildings[I];
            if (X.ModelId != Y.ModelId || X.Origin != Y.Origin || X.Orientation != Y.Orientation
                || X.PortalCellIds != Y.PortalCellIds || X.Portals.Num() != Y.Portals.Num()) return false;
            for (int P = 0; P < X.Portals.Num(); ++P)
            {
                const auto& U = X.Portals[P]; const auto& V = Y.Portals[P];
                if (U.Flags != V.Flags || U.OtherCellId != V.OtherCellId || U.OtherPortalId != V.OtherPortalId
                    || U.StabCells != V.StabCells) return false;
            }
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELandblockInfoTest, "ACE.Performance.LandblockInfo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACELandblockInfoTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI = NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(GI); Context.SetCurrentWorld(World); Context.OwningGameInstance = GI;
    GI->OnWorldChanged(nullptr, World); GI->Init();
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    const bool Loaded = Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"));
    TestTrue(TEXT("Retail DAT loads"), Loaded);
    if (Loaded)
    {
        const uint32 Blocks[] = {0x7D640000, 0xC6A90000, 0xE4540000, 0x25810000};
        uint64 Checksum = 0;
        for (uint32 Block : Blocks)
        {
            FACEDatLandblockInfo Info;
            TestTrue(TEXT("Fixture has landblock info"), Dat->LoadLandblockInfo(Block, Info));
            uint64 Bytes = Info.Objects.GetAllocatedSize() + Info.Buildings.GetAllocatedSize();
            for (const auto& Building : Info.Buildings)
            {
                Bytes += Building.Portals.GetAllocatedSize() + Building.PortalCellIds.GetAllocatedSize();
                for (const auto& Portal : Building.Portals) Bytes += Portal.StabCells.GetAllocatedSize();
            }
            AddInfo(FString::Printf(TEXT("Block %08X: %d buildings, %d objects, %u cells, %llu copied bytes"),
                Block, Info.Buildings.Num(), Info.Objects.Num(), Info.NumCells, Bytes));
            for (uint32 I = 0; I < Info.NumCells; ++I) Dat->GetOrBuildEnvCellMesh(Block | (0x100 + I), 100);
        }
        for (int Phase = 0; Phase < 4; ++Phase)
        {
            const double Begin = FPlatformTime::Seconds();
            constexpr int Iterations = 20000;
            for (int I = 0; I < Iterations; ++I)
            {
                FACEDatLandblockInfo Info;
                if (Dat->LoadLandblockInfo(Blocks[I % UE_ARRAY_COUNT(Blocks)], Info)) Checksum += Info.Buildings.Num();
            }
            AddInfo(FString::Printf(TEXT("Landblock copy phase %d: %.3f us/read"), Phase,
                (FPlatformTime::Seconds() - Begin) * 1.e6 / Iterations));
        }
        for (int Phase = 0; Phase < 4; ++Phase)
        {
            const double Begin = FPlatformTime::Seconds();
            constexpr int Iterations = 20000;
            for (int I = 0; I < Iterations; ++I)
                if (const auto Info = Dat->GetLandblockInfo(Blocks[I % UE_ARRAY_COUNT(Blocks)])) Checksum += Info->Buildings.Num();
            AddInfo(FString::Printf(TEXT("Landblock shared phase %d: %.3f us/read"), Phase,
                (FPlatformTime::Seconds() - Begin) * 1.e6 / Iterations));
        }
        // Captured from the unchanged collision traversal, including the reported
        // stair/post and fort locations. Do not merely compare a query to itself.
        const struct { uint32 Cell; FVector Local; TArray<uint32> Cells; bool Interior; } Samples[] = {
            {0x7D64001C, FVector(95, 95, 46), {0x7D64001C}, false},
            {0xC6A901AE, FVector(19.925781, 21.117188, 43.084137), {0xC6A901AE, 0xC6A90001}, true},
            {0xE454001E, FVector(79.808594, 133.745117, 21.761185), {0xE454001E}, false},
            {0x25810019, FVector(85.27, 23.52, 221), {0x25810019, 0x2581001A}, false}};
        for (const auto& Sample : Samples)
        {
            FACEPosition Position; Position.CellId = Sample.Cell; Position.Location = Sample.Local;
            TArray<uint32> Expected;
            bool Interior = false;
            ACECellTransit::FindCellList(*Dat, Sample.Cell, Position.ToUnrealLocation(100), 90, 100, Expected, &Interior);
            TestTrue(TEXT("Transit preserves baseline candidate cells and indoor status"), Expected == Sample.Cells && Interior == Sample.Interior);
            FString Cells;
            for (uint32 Cell : Expected) Cells += FString::Printf(TEXT(" %08X"), Cell);
            AddInfo(FString::Printf(TEXT("Transit %08X interior=%d:%s"), Sample.Cell, Interior, *Cells));
            for (int Phase = 0; Phase < 4; ++Phase)
            {
                const double Begin = FPlatformTime::Seconds();
                constexpr int Iterations = 3000;
                bool Stable = true;
                for (int I = 0; I < Iterations; ++I)
                {
                    TArray<uint32> CellsNow;
                    bool InteriorNow = false;
                    ACECellTransit::FindCellList(*Dat, Sample.Cell, Position.ToUnrealLocation(100), 90, 100, CellsNow, &InteriorNow);
                    Stable &= CellsNow == Expected && InteriorNow == Interior;
                    Checksum += CellsNow.Num();
                }
                AddInfo(FString::Printf(TEXT("Transit %08X phase %d: %.3f us/query"), Sample.Cell, Phase,
                    (FPlatformTime::Seconds() - Begin) * 1.e6 / Iterations));
                TestTrue(TEXT("Warmed transit returns the same collision candidates"), Stable);
            }
        }
        TestTrue(TEXT("Benchmarks consume real data"), Checksum > 0);
        for (uint32 Block : Blocks)
        {
            const auto Snapshot = Dat->GetLandblockInfo(Block);
            if (!TestTrue(TEXT("Immutable snapshot exists"), Snapshot.IsValid())) continue;
            TestTrue(TEXT("Cell and file IDs resolve to the same cached block"),
                Snapshot == Dat->GetLandblockInfo(Block | 0x100) && Snapshot == Dat->GetLandblockInfo(Block | 0xFFFE));
            FACEDatLandblockInfo Copy;
            TestTrue(TEXT("Owned copy remains available"), Dat->LoadLandblockInfo(Block, Copy));
            TestTrue(TEXT("Every object, frame, doorway and PVS list matches"), SameInfo(Copy, *Snapshot));
            Copy.Objects.Reset(); Copy.Buildings.Reset(); Copy.NumCells = 0;
            TestTrue(TEXT("Mutating an owned copy leaves the shared record intact"), Snapshot->NumCells > 0 && !Snapshot->Buildings.IsEmpty());
            Dat->LandblockInfoCache.Reset();
            const auto Fresh = Dat->GetLandblockInfo(Block);
            TestTrue(TEXT("Eviction reloads without invalidating a held reader"), Fresh && Fresh != Snapshot && SameInfo(*Fresh, *Snapshot));
        }
        TestFalse(TEXT("A missing record is not an empty successful load"), Dat->GetLandblockInfo(0xFFFF0000).IsValid());
        FACEDatLandblockInfo Missing; Missing.NumCells = 123;
        TestFalse(TEXT("Owned API reports a missing record"), Dat->LoadLandblockInfo(0xFFFF0000, Missing));
        TestEqual(TEXT("Failure clears previous output"), Missing.NumCells, 0u);
        auto BeforeReload = Dat->GetLandblockInfo(Blocks[0]);
        TWeakPtr<const FACEDatLandblockInfo> Old = BeforeReload;
        TestTrue(TEXT("DAT reload succeeds"), Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
        const auto AfterReload = Dat->GetLandblockInfo(Blocks[0]);
        TestTrue(TEXT("Reload gives new readers fresh data while old readers remain valid"),
            BeforeReload && AfterReload && BeforeReload != AfterReload && SameInfo(*BeforeReload, *AfterReload));
        BeforeReload.Reset();
        TestFalse(TEXT("Evicted snapshot frees when its last reader releases it"), Old.IsValid());
    }
    GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
