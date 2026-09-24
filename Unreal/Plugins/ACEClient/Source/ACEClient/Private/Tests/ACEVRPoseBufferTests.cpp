#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VR/ACEVRPose.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRPoseBufferTest,"ACE.VR.PoseBuffer",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEVRPoseBufferTest::RunTest(const FString&)
{
 FACERemoteVRPose Buffer;
 for(uint32 I=0;I<20;++I)
 {
  FACEVRPose P;P.Sequence=I+1;P.Version=2;P.ReceivedAt=10.+I*.05;
  P.Root=FVector(I*.5,0,I*.1);P.Poses[0].SetLocation(FVector(0,0,1.7));
  TestTrue(TEXT("Ordered pose accepted"),Buffer.Add(P));
  if(I>=2)for(int Frame=0;Frame<4;++Frame)
  {
   const double Now=P.ReceivedAt+Frame*.0125;
   const auto Rendered=Buffer.Sample(Now);
   TestTrue(TEXT("20Hz roots interpolate continuously at render cadence"),
    FMath::IsNearlyEqual(Rendered.Root.X,(Now-10.-.075)*10.,.0001));
   TestTrue(TEXT("Root height follows the same interpolation on stairs"),
    FMath::IsNearlyEqual(Rendered.Root.Z,(Now-10.-.075)*2.,.0001));
  }
 }
 TestEqual(TEXT("History bounded"),Buffer.History.Num(),8);
 auto Old=Buffer.Current;--Old.Sequence;TestFalse(TEXT("Reordered pose rejected"),Buffer.Add(Old));
 auto Portal=Buffer.Current;++Portal.Sequence;++Portal.Teleport;Portal.Root=FVector(500,500,500);Portal.ReceivedAt+=.05;
 Buffer.Add(Portal);TestEqual(TEXT("Teleport clears interpolation history"),Buffer.History.Num(),1);
 TestTrue(TEXT("No slide across portal"),Buffer.Sample(Portal.ReceivedAt).Root==Portal.Root);
 TestTrue(TEXT("Packet loss holds the last safe root"),Buffer.Sample(Portal.ReceivedAt+.4).Root==Portal.Root);
 return !HasAnyErrors();
}
#endif
