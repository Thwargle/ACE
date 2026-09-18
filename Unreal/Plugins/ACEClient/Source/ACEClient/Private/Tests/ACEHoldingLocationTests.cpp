#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dat/ACESetupMeshBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEHoldingLocationTest, "ACE.VR.HoldingLocations",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEHoldingLocationTest::RunTest(const FString&)
{
 FACEDatDatabase Portal;
 if (!TestTrue(TEXT("Open retail portal DAT"), Portal.Open(TEXT("C:/Turbine/Asheron's Call/client_portal.dat")))) return false;
 FACESetupMeshBuilder Builder(&Portal, nullptr, nullptr);
 int32 Left, Right; FTransform3f LeftFrame, RightFrame;
 if (!TestTrue(TEXT("Resolve human left grip"), Builder.GetHoldingLocation(0x0200004E, 8, Left, LeftFrame))) return false;
 if (!TestTrue(TEXT("Resolve human right grip"), Builder.GetHoldingLocation(0x0200004E, 1, Right, RightFrame))) return false;
 TestEqual(TEXT("Left grip belongs to left hand"), Left, 12);
 TestEqual(TEXT("Right grip belongs to right hand"), Right, 15);
 // Closing the archive proves repeated rig queries do no DAT I/O, including
 // a different holding location from the one that originally populated the cache.
 Portal.Close();
 for (int32 Pass = 0; Pass < 100; ++Pass)
 {
  int32 Part; FTransform3f Frame;
  TestTrue(TEXT("Cached right grip remains available without archive I/O"), Builder.GetHoldingLocation(0x0200004E, 1, Part, Frame));
  TestTrue(TEXT("Right grip retains authored transform"), Part == Right && Frame.Equals(RightFrame));
  TestTrue(TEXT("Cached left grip remains available without archive I/O"), Builder.GetHoldingLocation(0x0200004E, 8, Part, Frame));
  TestTrue(TEXT("Left grip retains authored transform"), Part == Left && Frame.Equals(LeftFrame));
  TestFalse(TEXT("Absent socket never resolves to a cached different socket"), Builder.GetHoldingLocation(0x0200004E, MAX_int32, Part, Frame));
  TestTrue(TEXT("Missing socket resets outputs"), Part == 0 && Frame.Equals(FTransform3f::Identity));
 }
 FACESetupMeshBuilder Replacement(&Portal, nullptr, nullptr);
 TestFalse(TEXT("A replacement archive builder does not inherit stale cached sockets"), Replacement.GetHoldingLocation(0x0200004E, 8, Left, LeftFrame));
 return true;
}
#endif
