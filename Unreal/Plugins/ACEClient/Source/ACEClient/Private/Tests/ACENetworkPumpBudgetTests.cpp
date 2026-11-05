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
 auto* CatchUp=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Network.CatchUpBudgetMs"));
 const float SavedCatchUp=CatchUp->GetFloat();CatchUp->Set(0.f,ECVF_SetByCode);
 ON_SCOPE_EXIT { CatchUp->Set(SavedCatchUp,ECVF_SetByCode); };
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

 // Sustained event traffic through real UDP: callbacks represent ordinary
 // per-object work, not GPU rendering. The old fixed .5 ms pass falls behind.
 // Catch-up must service that demand without disabling either frame bound.
 Limit->Set(128,ECVF_SetByCode);TimeLimit->Set(.5f,ECVF_SetByCode);
 auto Burst=[&](float CatchUpMs)
 {
  CatchUp->Set(CatchUpMs,ECVF_SetByCode);
  FACESession Crowd;Crowd.WorldObjects.Add(1,Owner);
  Crowd.SocketS2C=FUdpSocketBuilder(TEXT("Crowd receive")).AsNonBlocking().BoundToPort(0).WithReceiveBufferSize(1024*1024).Build();
  auto* Source=FUdpSocketBuilder(TEXT("Crowd source")).AsNonBlocking().BoundToPort(0).Build();
  ON_SCOPE_EXIT { if(Source){Source->Close();ISocketSubsystem::Get()->DestroySocket(Source);} };
  if(!TestNotNull(TEXT("Crowd receive socket"),Crowd.SocketS2C)||!TestNotNull(TEXT("Crowd source socket"),Source))return MAX_int32;
  auto To=ISocketSubsystem::Get()->CreateInternetAddr();To->SetIp(0x7f000001);To->SetPort(Crowd.SocketS2C->GetPortNo());
  Crowd.ServerS2CAddr=ISocketSubsystem::Get()->CreateInternetAddr();Crowd.ServerS2CAddr->SetIp(0x7f000001);Crowd.ServerS2CAddr->SetPort(Source->GetPortNo());
  int32 Count=0,Peak=0;uint32 Seq=2;
  Crowd.OnChatMessage.AddLambda([&](const FString& Line,const FString&,int32)
  {
   TestEqual(TEXT("Burst events retain exact order and are never duplicated"),FCString::Atoi(*Line),++Count+1);
   const double Until=FPlatformTime::Seconds()+.0001;
   while(FPlatformTime::Seconds()<Until){} // bounded 0.1 ms of object/event work
  });
  for(int32 Frame=0;Frame<60;++Frame)
  {
   // Advance queue ages at a simulated 60 Hz without sleeping a full second.
   for(auto& Pending:Crowd.OutOfOrderS2CPackets)Pending.Value.ReceivedAt-=1./60.;
   for(auto& Stream:Crowd.OrderedGameEvents)
    for(auto& Event:Stream.Value.Pending)Event.Value.QueuedAt-=1./60.;
   for(int32 I=0;I<24;++I)
   {
    const auto Wire=Packet(Seq++);int32 Bytes=0;
    TestTrue(TEXT("Burst datagram reaches loopback"),Source->SendTo(Wire.GetData(),Wire.Num(),Bytes,*To)&&Bytes==Wire.Num());
   }
   Crowd.PollSockets();
   Peak=FMath::Max(Peak,Crowd.PendingS2CDispatchCount+Crowd.PendingGameEventCount);
   TestTrue(TEXT("Catch-up retains the packet/work cap"),Crowd.LastNetworkDispatchedWork<=128);
   TestTrue(TEXT("Catch-up retains the elapsed-time allowance"),Crowd.LastNetworkDispatchBudgetMs<=FMath::Max(.5f,CatchUpMs)+.001);
  }
  for(int32 Frame=0;Frame<4096 && Count<1440;++Frame)Crowd.PollSockets();
  TestEqual(TEXT("Burst drains without losing reliable gameplay events"),Count,1440);
  TestEqual(TEXT("Burst transport ACK catches up"),Crowd.LastReceivedPacketSequence,1441u);
  TestEqual(TEXT("Packet queue accounting returns to zero"),Crowd.PendingS2CDispatchCount,0);
  TestEqual(TEXT("Ordered event accounting returns to zero"),Crowd.PendingGameEventCount,0);
  TestTrue(TEXT("Performance output exposes dispatch and socket backlog"),Crowd.DescribeNetworkPump().Contains(TEXT("dispatchAge="))&&Crowd.DescribeNetworkPump().Contains(TEXT("socketPending=")));
  TestEqual(TEXT("Idle frames return to the normal budget"),Crowd.NetworkPumpBudgetSeconds(false),.0005);
  AddInfo(FString::Printf(TEXT("Crowd burst catch-up %.1f ms: peak work queue=%d, events=%d"),CatchUpMs,Peak,Count));
  return Peak;
 };
 const int32 FixedPeak=Burst(0.f),CatchUpPeak=Burst(8.f);
 TestTrue(TEXT("Fixed half-millisecond budget reproduces growing event backlog"),FixedPeak>512);
 TestTrue(TEXT("Bounded catch-up materially reduces sustained event backlog"),CatchUpPeak<FixedPeak/2);
 return true;
}
#endif
