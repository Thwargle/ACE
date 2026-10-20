#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Protocol/ACEHash32.h"
#include "ACEOpcodes.h"

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

 // Recovering one lost event must not dispatch an entire reliable backlog
 // inside that packet's receive call. Use real F7B0 messages to check order,
 // exactly-once delivery, and progress after the socket becomes idle.
 auto* TimeLimit=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Network.PumpBudgetMs"));
 const float SavedTime=TimeLimit->GetFloat();TimeLimit->Set(0.f,ECVF_SetByCode);
 ON_SCOPE_EXIT { TimeLimit->Set(SavedTime,ECVF_SetByCode); };
 auto Packet=[](uint32 Sequence)
 {
  FACEBinaryWriter Message;Message.WriteUInt32(ACEOpcode::GameEvent);Message.WriteUInt32(1);Message.WriteUInt32(Sequence-1);
  Message.WriteUInt32(ACEGameEvent::CommunicationTransientString);
  Message.WriteString16L(FString::FromInt(Sequence));
  FACEBinaryWriter Body;Body.WriteUInt32(Sequence);Body.WriteUInt32(0x80000000);
  Body.WriteUInt16(1);Body.WriteUInt16(16+Message.GetData().Num());Body.WriteUInt16(0);
  Body.WriteUInt16(ACEQueue::UIQueue);Body.WriteBytes(Message.GetData());
  const auto Flags=EACEPacketHeaderFlags::BlobFragments;
  const uint32 Hash=FACESession::HeaderHash32(Sequence,Flags,0,0,Body.GetData().Num(),1)+FACEHash32::Calculate(Body.GetData());
  FACEBinaryWriter Wire;Wire.WriteUInt32(Sequence);Wire.WriteUInt32(static_cast<uint32>(Flags));Wire.WriteUInt32(Hash);
  Wire.WriteUInt16(0);Wire.WriteUInt16(0);Wire.WriteUInt16(Body.GetData().Num());Wire.WriteUInt16(1);Wire.WriteBytes(Body.GetData());
  return Wire.GetData();
 };
 FACESession Backlog;
 FACEWorldObject Owner;Owner.Guid=1;Backlog.WorldObjects.Add(1,Owner);
 TArray<int32> Delivered;
 Backlog.OnChatMessage.AddLambda([&](const FString& Line,const FString&,int32)
 { Delivered.Add(FCString::Atoi(*Line)); });
 auto Deliver=[&](uint32 Sequence)
 { const auto Wire=Packet(Sequence);Backlog.HandleDatagram(Wire.GetData(),Wire.Num(),true); };
 for(uint32 Sequence=3;Sequence<=66;++Sequence)Deliver(Sequence);
 TestEqual(TEXT("A missing event stamp holds later events for that owner"),Delivered.Num(),0);
 TestEqual(TEXT("Packet gaps do not retain already assembled gameplay payloads"),Backlog.PendingGameEventCount,64);
 Deliver(2);
 TestTrue(TEXT("Gap recovery obeys the dispatch count limit"),Delivered.Num()<=8);
 TestTrue(TEXT("Gap recovery makes progress"),Delivered.Num()>0);
 while(!Backlog.OutOfOrderS2CPackets.IsEmpty() || !Backlog.ReadyGameEventOwners.IsEmpty())
 {
  const int32 Before=Delivered.Num();Backlog.PollSockets();
  if(!TestTrue(TEXT("An idle receive pump drains a bounded batch"),Delivered.Num()>Before && Delivered.Num()-Before<=8))break;
 }
 TestEqual(TEXT("Every buffered message is eventually dispatched"),Delivered.Num(),65);
 for(int32 I=0;I<Delivered.Num();++I)TestEqual(TEXT("Reliable messages retain exact order"),Delivered[I],I+2);
 Deliver(2);Deliver(66);
 TestEqual(TEXT("Retransmitted messages are not dispatched twice"),Delivered.Num(),65);
 TestEqual(TEXT("ACK cursor covers only delivered packets"),Backlog.LastReceivedPacketSequence,66u);

 FACESession Slow;
 Slow.WorldObjects.Add(1,Owner);
 int32 SlowDelivered=0;
 Slow.OnChatMessage.AddLambda([&](const FString&,const FString&,int32)
 {
  ++SlowDelivered;FPlatformProcess::Sleep(.002f);
 });
 TimeLimit->Set(1.f,ECVF_SetByCode);
 Slow.bDeferOrderedDispatch=true;
 for(uint32 Sequence : {3u,4u,5u,2u})
 { const auto Wire=Packet(Sequence);Slow.HandleDatagram(Wire.GetData(),Wire.Num(),true); }
 Slow.bDeferOrderedDispatch=false;Slow.DrainOutOfOrderS2C();
 TestEqual(TEXT("Slow atomic dispatch yields at the elapsed-time limit"),SlowDelivered,1);
 TestEqual(TEXT("Received packets can be acknowledged while events await their turn"),Slow.LastReceivedPacketSequence,5u);
 for(int32 I=0;I<3;++I)Slow.PollSockets();
 TestEqual(TEXT("Time-limited backlog progresses on idle frames"),SlowDelivered,4);
 return true;
}
#endif
