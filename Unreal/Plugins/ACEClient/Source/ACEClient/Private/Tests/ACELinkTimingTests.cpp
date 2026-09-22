#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELinkTimingTest,"ACE.RetailParity.LinkTiming",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELinkTimingTest::RunTest(const FString&)
{
 FACESession Session;Session.State=EACESessionState::InWorld;
 // Deliberately large clock origin reproduces the loss of subsecond float precision.
 const double Start=1000000000.0;
 const float First=Session.RecordEchoRequest(Start);
 Session.UpdateLinkStatusFromEcho(First,Start+.047);
 TestTrue(TEXT("Echo retains a real 47ms sample at a large clock origin"),Session.LinkStatus.bHasPing && FMath::IsNearlyEqual(Session.LinkStatus.RoundTripSeconds,.047f,.00001f));
 Session.UpdateLinkStatusFromEcho(First,Start+2.0);
 TestTrue(TEXT("Duplicate echo cannot replace the last sample"),FMath::IsNearlyEqual(Session.LinkStatus.RoundTripSeconds,.047f,.00001f));
 const float Second=Session.RecordEchoRequest(Start+2.0);
 TestTrue(TEXT("Echo tokens remain distinct"),Second!=First);
 Session.UpdateLinkStatusFromEcho(Second,Start+2.061);
 TestTrue(TEXT("Next echo updates continuously instead of resetting to zero"),FMath::IsNearlyEqual(Session.LinkStatus.RoundTripSeconds,.061f,.00001f));
 Session.UpdateLinkStatusFromEcho(999.f,Start+4.0);
 TestTrue(TEXT("Unrecognized echo leaves the current sample intact"),FMath::IsNearlyEqual(Session.LinkStatus.RoundTripSeconds,.061f,.00001f));
 const float Stale=Session.RecordEchoRequest(Start+4.0);
 Session.UpdateLinkStatusFromEcho(Stale,Start+40.0);
 TestTrue(TEXT("Expired echo does not invent a latency spike"),FMath::IsNearlyEqual(Session.LinkStatus.RoundTripSeconds,.061f,.00001f));
 Session.LastServerPacketAt=Start;
 Session.RecordLinkTraffic(Start,90,10);
 TestTrue(TEXT("Link panel reports recent packet loss"),FMath::IsNearlyEqual(Session.GetLinkStatusAt(Start+1).PacketLossPercent,10.f));
 Session.RecordLinkTraffic(Start+9,100,0);
 TestTrue(TEXT("Window combines recent traffic buckets"),FMath::IsNearlyEqual(Session.GetLinkStatusAt(Start+9).PacketLossPercent,5.f));
 TestEqual(TEXT("Old retransmissions expire after ten seconds"),Session.GetLinkStatusAt(Start+10).PacketLossPercent,0.f);
 TestEqual(TEXT("Packet silence continues to increase without an echo"),Session.GetLinkStatusAt(Start+6).SecondsSinceLastPacket,6.f);
 TestTrue(TEXT("Connected state does not depend on getting the first ping"),Session.GetLinkStatusAt(Start).bConnected);
 Session.State=EACESessionState::Failed;
 TestFalse(TEXT("A failed connection is never marked connected"),Session.GetLinkStatusAt(Start+20).bConnected);
 Session.Disconnect();
 TestEqual(TEXT("Reconnect cannot inherit old loss buckets"),Session.GetLinkStatusAt(Start+1).PacketLossPercent,0.f);
 return true;
}
#endif
