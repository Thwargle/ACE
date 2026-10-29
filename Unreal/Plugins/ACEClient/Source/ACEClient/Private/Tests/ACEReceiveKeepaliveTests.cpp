#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Protocol/ACEHash32.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEReceiveKeepaliveTest,"ACE.Network.ReceivePortKeepalive",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEReceiveKeepaliveTest::RunTest(const FString&)
{
 auto* Sockets=ISocketSubsystem::Get();
 auto* Server=FUdpSocketBuilder(TEXT("Receive port fixture")).AsNonBlocking().BoundToPort(0).Build();
 ON_SCOPE_EXIT { if(Server){Server->Close();Sockets->DestroySocket(Server);} };
 if(!TestNotNull(TEXT("Loopback listener"),Server))return false;
 auto Destination=Sockets->CreateInternetAddr();Destination->SetIp(0x7f000001);Destination->SetPort(Server->GetPortNo());
 for(bool GDLE:{false,true})
 for(double Interval:{20.0,110.0})
 {
  FACESession Session;Session.Creds.bGDLE=GDLE;Session.State=EACESessionState::InWorld;
  Session.ReceivePortKeepaliveInterval=Interval;
  Session.SocketS2C=FUdpSocketBuilder(TEXT("Receive port client")).AsNonBlocking().BoundToPort(0).Build();
  Session.ServerS2CAddr=Destination;Session.LastReceivePortKeepaliveAt=100;
  const uint32 Sequence=Session.NextPacketSequence;
  // Model mobile (60-second) and desktop (180-second) NAT expiry without an
  // outbound datagram. Verify actual socket, destination, and retail wire bytes
  // over eleven minutes, without sleeping or contacting a real game server.
  double LastOutbound=100;int Count=0;
  for(double Now=100;Now<=760;Now+=1)
  {
   Session.MaintainReceivePort(Now);
   uint32 Pending=0;
   if(Server->HasPendingData(Pending))
   {
    uint8 Bytes[128];int32 Read=0;auto From=Sockets->CreateInternetAddr();
    if(!TestTrue(TEXT("Keepalive received"),Server->RecvFrom(Bytes,sizeof(Bytes),Read,*From)))return false;
    TestEqual(TEXT("Keepalive uses world-update receive socket"),From->GetPort(),Session.SocketS2C->GetPortNo());
    TestEqual(TEXT("NOP is exactly one eight-byte optional header"),Read,28);
    FACEBinaryReader R(Bytes,Read);
    TestEqual(TEXT("Keepalive is unsequenced"),R.ReadUInt32(),0u);
    const auto Flags=EACEPacketHeaderFlags(R.ReadUInt32());
    TestEqual(TEXT("Plain CICMD, not an encrypted gameplay packet"),uint32(Flags),0x00400000u);
    const uint32 Checksum=R.ReadUInt32();TestEqual(TEXT("Retail keepalive recipient ID is zero"),R.ReadUInt16(),uint16(0));
    const uint16 Time=R.ReadUInt16(),Size=R.ReadUInt16(),Iteration=R.ReadUInt16();
    TestEqual(TEXT("Connectionless interval"),Time,uint16(0));TestEqual(TEXT("Connectionless iteration"),Iteration,uint16(0));
    TestEqual(TEXT("Valid cleartext CRC"),Checksum,FACESession::HeaderHash32(0,Flags,0,Time,Size,Iteration)+FACEHash32::Calculate(Bytes+20,8));
    TestEqual(TEXT("cmdNOP"),R.ReadUInt32(),1u);TestEqual(TEXT("NOP param zero"),R.ReadUInt32(),0u);
    TestEqual(TEXT("Platform keepalive cadence"),Now-LastOutbound,Interval);
    LastOutbound=Now;++Count;
   }
   TestTrue(TEXT("Receive NAT mapping never expires"),Now-LastOutbound<(Interval==20.0?60.0:180.0));
  }
  TestEqual(TEXT("Keepalives continue across multiple reported disconnect windows"),Count,int32(660/Interval));
  TestEqual(TEXT("Keepalives never consume gameplay sequence numbers"),Session.NextPacketSequence,Sequence);
  TestTrue(TEXT("No keepalive enters the gameplay retransmission cache"),Session.CachedC2SPackets.IsEmpty());
  for(auto State:{EACESessionState::Disconnected,EACESessionState::AwaitConnectRequest,EACESessionState::Failed})
  {
   Session.State=State;Session.MaintainReceivePort(1000);uint32 Pending=0;
   TestFalse(TEXT("Inactive/login states cannot send keepalives"),Server->HasPendingData(Pending));
  }
 }
 return true;
}
#endif
