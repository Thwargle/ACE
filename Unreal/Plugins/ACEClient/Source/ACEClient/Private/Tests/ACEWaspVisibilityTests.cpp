#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEDatFileTypes.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWaspVisibilityTest, "ACE.Performance.WaspVisibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FACEWaspVisibilityTest::RunTest(const FString&)
{
    auto* Dat = NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    double WorstMs = 0, TotalMs = 0;
    int32 Samples = 0;
    for (uint32 Block : {0x01430000u,0x01E30000u})
    {
    const auto Info = Dat->GetLandblockInfo(Block);
    if (!TestTrue(TEXT("Reported dungeon has real DAT cells"),Info.IsValid() && Info->NumCells>0)) continue;
    TArray<uint32> Cells;
    for (uint32 I=0; I<Info->NumCells; ++I)
    {
        const uint32 Id=Block| (0x100+I);
        Dat->GetOrBuildEnvCellMesh(Id,100);
        if (Block==0x01E30000u || Id==0x01430150u || Id==0x01430159u || Id==0x0143015Fu || Id==0x01430170u || Id==0x01430171u) Cells.Add(Id);
    }
    const FVector Origin = FACEPosition::AceVectorToUnreal(FVector((Block>>24)*192, ((Block>>16)&255)*192, 0), 100);
    for (uint32 Id : Cells)
    {
        const auto* Mesh = Dat->FindEnvCellMesh(Id, 100);
        if (!TestNotNull(TEXT("Reported wasp room is present"), Mesh)) continue;
        const FTransform Frame = Mesh->GetCellLocalToLandblock(100) * FTransform(Origin);
        for (int32 X = 0; X < 5; ++X)
        for (int32 Y = 0; Y < 5; ++Y)
        {
            const FVector Local(FMath::Lerp(Mesh->LocalBoundsMin.X, Mesh->LocalBoundsMax.X, (X + .5) / 5),
                FMath::Lerp(Mesh->LocalBoundsMin.Y, Mesh->LocalBoundsMax.Y, (Y + .5) / 5), Mesh->LocalBoundsMin.Z + 170);
            const FVector Eye = Frame.TransformPosition(Local);
            if (!Dat->IsPointInsideEnvCell(Id, Eye, 100)) continue;
            for (int32 Yaw = 0; Yaw < 360; Yaw += 30)
            for (int32 Pitch : {-30, 0, 30})
            {
                const FQuat Rotation = FRotator(Pitch, Yaw, 0).Quaternion();
                FConvexVolume Frustum;
                for (const FVector& N : {FVector(-1,1,0), FVector(-1,-1,0), FVector(-.5,0,1), FVector(-.5,0,-1), FVector(-1,0,0)})
                    Frustum.Planes.Add(FPlane(Eye, Rotation.RotateVector(N.GetSafeNormal())));
                Frustum.Init();
                TSet<int32> Draw; bool Outside = false;
                const double Start = FPlatformTime::Seconds();
                ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat, Id, Eye, Frustum, 100, Draw, Outside, 128);
                const double Ms = (FPlatformTime::Seconds() - Start) * 1000;
                TotalMs += Ms; ++Samples;
                if (Ms > WorstMs)
                {
                    WorstMs = Ms;
                    AddInfo(FString::Printf(TEXT("Worst visibility %.3f ms cell=%08X local=%s yaw=%d pitch=%d cells=%d"),
                        Ms, Id, *Local.ToString(), Yaw, Pitch, Draw.Num()));
                }
            }
        }
    }
    }
    AddInfo(FString::Printf(TEXT("Wasp/Glenden visibility samples=%d mean=%.3f ms max=%.3f ms"), Samples, TotalMs / FMath::Max(Samples, 1), WorstMs));
    TestTrue(TEXT("Resident portal traversal remains below a catastrophic frame stall"),WorstMs<100);
    TestTrue(TEXT("Room sweep covers camera positions inside the real DAT"), Samples > 100);
    return true;
}
#endif
