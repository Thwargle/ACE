#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACERetailUseRange.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUseRangeTest,"ACE.RetailParity.UseRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUseRangeTest::RunTest(const FString&)
{
    auto Gap=[](FVector Target,float TargetHeight=0.f,float Scale=1.f)
    { return ACERetailUseRange::CylinderDistance(FVector::ZeroVector,30*Scale,180*Scale,Target,25*Scale,TargetHeight*Scale); };
    TestEqual(TEXT("Level ground retains the authored cylinder radius gap"),Gap(FVector(100,0,0)),45.f);
    TestEqual(TEXT("Slope origins use full 3D distance even while cylinders overlap vertically"),Gap(FVector(80,0,60)),45.f);
    TestTrue(TEXT("A downhill corpse includes vertical separation below the player's feet"),
        FMath::IsNearlyEqual(Gap(FVector(80,0,-60)),75.f,.001f));
    TestEqual(TEXT("Target height removes vertical separation when cylinders overlap"),Gap(FVector(80,0,-60),80),45.f);
    TestTrue(TEXT("Targets directly below on another floor are not in reach"),Gap(FVector(0,0,-150))>150);
    TestTrue(TEXT("Overlap remains negative for negative portal use radii"),Gap(FVector(10,0,0),180)<0);
    TestEqual(TEXT("Large authored creature scales are not capped to ordinary human dimensions"),Gap(FVector(1000,0,0),180,10),450.f);
    TestTrue(TEXT("A downhill corpse outside use radius is not flattened into range"),Gap(FVector(80,0,-60))>60);
    return true;
}
#endif
