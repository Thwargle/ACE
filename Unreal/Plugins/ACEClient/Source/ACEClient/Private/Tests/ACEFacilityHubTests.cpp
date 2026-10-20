#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "Engine/GameInstance.h"
#include "Misc/CommandLine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEFacilityHubVisibilityTest,"ACE.Rendering.FacilityHubVisibility",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEFacilityHubVisibilityTest::RunTest(const FString&)
{
    auto* Dat=NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    FString Directory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    if(!TestTrue(TEXT("Load Facility Hub retail geometry"),Dat->LoadDatDirectory(Directory)))return false;
    const uint32 Block=0x8A020000;
    const auto Info=Dat->GetLandblockInfo(Block);if(!TestTrue(TEXT("Facility Hub cells available"),Info.IsValid()))return false;
    const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(138*192,2*192,0),100);
    for(uint32 I=0;I<Info->NumCells;++I)Dat->GetOrBuildEnvCellMesh(Block|(0x100+I),100);
    double WorstMs=0,TotalMs=0;int32 Samples=0,MaxVisible=0;uint32 WorstCell=0;int32 WorstYaw=0;
    for(uint32 I=0;I<Info->NumCells;++I)
    {
        const uint32 Id=Block|(0x100+I);const auto* Mesh=Dat->FindEnvCellMesh(Id,100);if(!Mesh)continue;
        const FTransform Frame=Mesh->GetCellLocalToLandblock(100)*FTransform(Origin);
        const FVector Center=(Mesh->LocalBoundsMin+Mesh->LocalBoundsMax)*.5;
        const FVector Eye=Frame.TransformPosition(FVector(Center.X,Center.Y,Mesh->LocalBoundsMin.Z+170));
        if(!Dat->IsPointInsideEnvCell(Id,Eye,100))continue;
        for(int32 Yaw=0;Yaw<360;Yaw+=30)
        {
            FConvexVolume Frustum;const FQuat Rotation=FRotator(0,Yaw,0).Quaternion();
            for(const FVector& N:{FVector(-1,1,0),FVector(-1,-1,0),FVector(-.625,0,1),FVector(-.625,0,-1),FVector(-1,0,0)})
                Frustum.Planes.Add(FPlane(Eye,Rotation.RotateVector(N.GetSafeNormal())));
            Frustum.Init();TSet<int32> Visible;bool Outside=false;const double Start=FPlatformTime::Seconds();
            ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat,Id,Eye,Frustum,100,Visible,Outside,128);
            const double Ms=(FPlatformTime::Seconds()-Start)*1000;TotalMs+=Ms;++Samples;
            if(Ms>WorstMs){WorstMs=Ms;WorstCell=Id;WorstYaw=Yaw;}
            MaxVisible=FMath::Max(MaxVisible,Visible.Num());
            TestTrue(TEXT("Viewer cell remains visible"),Visible.Contains(int32(Id)));
        }
    }
    AddInfo(FString::Printf(TEXT("Facility visibility: cells=%u samples=%d average=%.4f ms worst=%.4f ms cell=%08X yaw=%d max_visible=%d"),Info->NumCells,Samples,TotalMs/FMath::Max(1,Samples),WorstMs,WorstCell,WorstYaw,MaxVisible));
    TestTrue(TEXT("Sweep covers Facility Hub"),Samples>100);
    return true;
}
#endif
