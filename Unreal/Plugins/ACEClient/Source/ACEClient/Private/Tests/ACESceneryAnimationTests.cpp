#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatCursor.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESceneryAnimationTest,"ACE.RetailParity.SceneryAnimation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACESceneryAnimationTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>(); auto* Dat=NewObject<UACEDatSubsystem>(GI);
    FString Directory=TEXT("C:/Turbine/Asheron's Call");FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(Directory)))return false;
    FACEDatLandblockInfo Info;
    if(!TestTrue(TEXT("Reported windmill landblock exists"),Dat->LoadLandblockInfo(0xCF950000,Info)))return false;
    TestTrue(TEXT("Reported landblock uses the windmill setup"),Info.Objects.ContainsByPredicate([](const auto& O){return O.Id==0x020003E5;}));
    TArray<uint8> Bytes;
    if(!TestTrue(TEXT("Windmill setup reads"),Dat->GetPortalDat()->ReadFile(0x020003E5,Bytes)))return false;
    FACEDatSetupModel Setup;FACEDatCursor Cursor(Bytes);
    if(!TestTrue(TEXT("Windmill setup decodes"),ACEDatUnpack::UnpackSetupModel(Cursor,Setup)))return false;
    if(!Dat->GetPortalDat()->ReadFile(Setup.DefaultAnimation,Bytes))return false;
    FACEDatAnimation Animation;FACEDatCursor AnimCursor(Bytes);
    if(!TestTrue(TEXT("Windmill animation decodes"),ACEDatUnpack::UnpackAnimation(AnimCursor,Animation)))return false;
    TestEqual(TEXT("Windmill uses an authored quarter-turn cycle"),Animation.NumFrames,60u);
    TestEqual(TEXT("Windmill has hub and four interchangeable blades"),Animation.NumParts,5u);
    TArray<FTransform> Previous,Current;int32 Count=0;float MaxTravel=0;
    // Include subframes on both sides of eight complete loop boundaries.
    // At 30 authored frames/second the largest permitted visual blade step is
    // one frame (14 cm at this radius), not a backwards quarter-turn crossfade.
    for(int32 Tick=0;Tick<=1920;++Tick)
    {
        if(!TestTrue(TEXT("Scenery pose evaluates"),Dat->EvaluateAnimationLoop(Setup.DefaultAnimation,Tick/120.f,5,Current,100,Count)))return false;
        if(!Previous.IsEmpty())for(int32 Part:{0,2,3,4})
        {
            double Nearest=MAX_dbl;
            for(int32 Old:{0,2,3,4})Nearest=FMath::Min(Nearest,FVector::Distance(Current[Part].GetLocation(),Previous[Old].GetLocation()));
            MaxTravel=FMath::Max(MaxTravel,float(Nearest));
        }
        Previous=Current;
    }
    TestTrue(TEXT("No reverse sweep when interchangeable blades reset"),MaxTravel<15);
    TestTrue(TEXT("The blades actually rotate"),MaxTravel>1);
    TArray<FTransform> A,B;
    Dat->EvaluateAnimationLoop(Setup.DefaultAnimation,.01f,5,A,100,Count);
    Dat->EvaluateAnimationLoop(Setup.DefaultAnimation,.02f,5,B,100,Count);
    TestFalse(TEXT("Within-clip subframes remain interpolated"),A[0].Equals(B[0],.01));
    Dat->EvaluateAnimationLoop(Setup.DefaultAnimation,59.f/30,5,A,100,Count);
    Dat->EvaluateAnimationLoop(Setup.DefaultAnimation,59.75f/30,5,B,100,Count);
    for(int32 Part=0;Part<5;++Part)TestTrue(TEXT("Hold the authored last frame until the sequence wraps"),A[Part].Equals(B[Part],.001));
    AddInfo(FString::Printf(TEXT("Windmill: maximum blade travel %.3f cm across eight cycles at 120 Hz"),MaxTravel));
    return true;
}
#endif
