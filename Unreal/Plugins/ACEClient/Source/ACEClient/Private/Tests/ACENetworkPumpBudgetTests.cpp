#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACENetworkPumpBudgetTest,"ACE.Network.PumpBudget",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACENetworkPumpBudgetTest::RunTest(const FString&)
{
 auto* Limit=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Network.PumpMaxPackets"));
 const int32 Saved=Limit->GetInt();Limit->Set(8,ECVF_SetByCode);
 ON_SCOPE_EXIT { Limit->Set(Saved,ECVF_SetByCode); };
 FACESession Session;
 Session.SocketC2S=FUdpSocketBuilder(TEXT("ACE_PumpTestReceive")).AsNonBlocking().BoundToPort(0).WithReceiveBufferSize(1024*1024).Build();
 auto* Sender=FUdpSocketBuilder(TEXT("ACE_PumpTestSend")).AsNonBlocking().Build();
 ON_SCOPE_EXIT { if(Sender){Sender->Close();ISocketSubsystem::Get()->DestroySocket(Sender);} };
 if(!TestNotNull(TEXT("Receive socket"),Session.SocketC2S) || !TestNotNull(TEXT("Send socket"),Sender))return false;
 auto Address=ISocketSubsystem::Get()->CreateInternetAddr();Address->SetIp(0x7F000001);Address->SetPort(Session.SocketC2S->GetPortNo());
 uint8 Byte=0;int32 Sent=0;
 for(int32 I=0;I<64;++I)Sender->SendTo(&Byte,1,Sent,*Address);
 // Malformed one-byte datagrams are harmless, but still exercise the real UDP
 // receive pump. The cap must leave queued packets for subsequent frames.
 uint32 Pending=0;
 TestTrue(TEXT("Burst reaches local receive queue"),Session.SocketC2S->HasPendingData(Pending));
 Session.PollSockets();
 TestTrue(TEXT("One frame does not drain the entire burst"),Session.SocketC2S->HasPendingData(Pending));
 for(int32 I=0;I<64;++I)Session.PollSockets();
 TestFalse(TEXT("Later frames finish draining the queue"),Session.SocketC2S->HasPendingData(Pending));
 return true;
}
#endif
