#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "Protocol/ACEHash32.h"
#include "ACEOpcodes.h"

// A loopback server for the offline wasp tests. Advancing its clock schedules
// bytes, never sleeps the game thread. The client uses its normal UDP pump,
// checksum validation, reliable ordering and gameplay message handlers.
class FACEWaspLagLink
{
public:
 explicit FACEWaspLagLink(FACESession& InSession) : Session(InSession) {}
 ~FACEWaspLagLink()
 {
  if(Sender){Sender->Close();ISocketSubsystem::Get()->DestroySocket(Sender);}
  if(Receiver && Session.SocketS2C==Receiver)
  { Receiver->Close();ISocketSubsystem::Get()->DestroySocket(Receiver);Session.SocketS2C=nullptr; }
  if(bInstalled)Session.ServerS2CAddr.Reset();
 }
 bool Open()
 {
  // Never install a test transport over a real connection.
  if(Session.SocketC2S || Session.SocketS2C)return false;
  bInstalled=true;
  Receiver=FUdpSocketBuilder(TEXT("ACE_WaspLagClient")).AsNonBlocking().BoundToAddress(FIPv4Address::InternalLoopback)
   .BoundToPort(0).WithReceiveBufferSize(4*1024*1024).Build();
  Sender=FUdpSocketBuilder(TEXT("ACE_WaspLagServer")).AsNonBlocking().BoundToAddress(FIPv4Address::InternalLoopback).BoundToPort(0).Build();
  Session.SocketS2C=Receiver;
  if(!Receiver || !Sender)return false;
  Session.IssacServer=MakeUnique<FACECryptoSystem>(CryptoSeed);
  Destination=ISocketSubsystem::Get()->CreateInternetAddr();Destination->SetIp(0x7F000001);Destination->SetPort(Receiver->GetPortNo());
  Session.ServerS2CAddr=ISocketSubsystem::Get()->CreateInternetAddr();Session.ServerS2CAddr->SetIp(0x7F000001);
  Session.ServerS2CAddr->SetPort(Sender->GetPortNo());
  return true;
 }
 double RttMs=0,JitterMs=0,RecoveryMs=0;
 int32 RecoveryEvery=0;
 uint32 Sent=0,Queued=0,Recovered=0,Duplicates=0;
 int32 MaxPending=0,SendErrors=0;
 void Queue(const TArray<uint8>& Message,double Now,uint16 QueueId=ACEQueue::UIQueue)
 {
  const uint32 Sequence=NextSequence++;
  FACEBinaryWriter Body;Body.WriteUInt32(Sequence);Body.WriteUInt32(0x80000000);
  Body.WriteUInt16(1);Body.WriteUInt16(16+Message.Num());Body.WriteUInt16(0);Body.WriteUInt16(QueueId);Body.WriteBytes(Message);
  const auto Flags=EACEPacketHeaderFlags::BlobFragments|EACEPacketHeaderFlags::EncryptedChecksum;
  const uint32 Hash=FACESession::HeaderHash32(Sequence,Flags,0,0,Body.GetData().Num(),1)+(FACEHash32::Calculate(Body.GetData())^Keys.Next());
  FACEBinaryWriter Wire;Wire.WriteUInt32(Sequence);Wire.WriteUInt32(static_cast<uint32>(Flags));Wire.WriteUInt32(Hash);
  Wire.WriteUInt16(0);Wire.WriteUInt16(0);Wire.WriteUInt16(Body.GetData().Num());Wire.WriteUInt16(1);Wire.WriteBytes(Body.GetData());
  // Deterministic jitter can reorder arrival. Every Nth packet models a lost
  // transmission recovered later; an extra duplicate checks exactly-once delivery.
  const double Jitter=(int32((Sequence*37)%101)-50)/50.0*JitterMs;
  double Due=Now+FMath::Max(0.0,RttMs*.5+Jitter)*.001;
  const bool Recover=RecoveryEvery>0 && Sequence%RecoveryEvery==0;
  if(Recover){Due+=RecoveryMs*.001;++Recovered;}
  Pending.HeapPush(FPacket{Due,Sequence,Wire.GetData()},Later());++Queued;
  if(Recover){Pending.HeapPush(FPacket{Due+.01,Sequence,Wire.GetData()},Later());++Duplicates;}
  MaxPending=FMath::Max(MaxPending,Pending.Num());
 }
 void Advance(double Now)
 {
  // Bound synthetic ingress, as a link does. Do not overflow a UDP socket with
  // minutes of simulated traffic in a single host call.
  for(int32 I=0;I<64 && !Pending.IsEmpty() && Pending[0].At<=Now;++I)
  {
   FPacket Packet;Pending.HeapPop(Packet,Later(),EAllowShrinking::No);
   int32 Bytes=0;
   if(!Sender->SendTo(Packet.Bytes.GetData(),Packet.Bytes.Num(),Bytes,*Destination) || Bytes!=Packet.Bytes.Num())++SendErrors;
   else ++Sent;
  }
 }
 bool IsDrained() const
 {
  uint32 Bytes=0;
  return Pending.IsEmpty() && Session.OutOfOrderS2CPackets.IsEmpty() && Receiver && !Receiver->HasPendingData(Bytes);
 }
 int32 BufferedGameplay() const { return Session.OutOfOrderS2CPackets.Num(); }
 uint32 DispatchedSequence() const { return Session.LastReceivedPacketSequence; }
 static TArray<uint8> Position(int32 Guid,const FACEPosition& P,uint16 Stamp,uint16 Force=0)
 {
  FACEBinaryWriter W;W.WriteUInt32(ACEOpcode::UpdatePosition);W.WriteUInt32(Guid);W.WriteUInt32(4);W.WriteUInt32(P.CellId);
  W.WriteFloat(P.Location.X);W.WriteFloat(P.Location.Y);W.WriteFloat(P.Location.Z);
  W.WriteFloat(P.RotationW);W.WriteFloat(P.RotationXYZ.X);W.WriteFloat(P.RotationXYZ.Y);W.WriteFloat(P.RotationXYZ.Z);
  W.WriteUInt16(0);W.WriteUInt16(Stamp);W.WriteUInt16(0);W.WriteUInt16(Force);return W.GetData();
 }
 static TArray<uint8> Cast(int32 Guid,uint16 Stamp)
 {
  FACEBinaryWriter W;W.WriteUInt32(ACEOpcode::UpdateMotion);W.WriteUInt32(Guid);W.WriteUInt16(0);W.WriteUInt16(Stamp);
  W.WriteUInt16(0);W.WriteUInt8(0);W.Align();W.WriteUInt8(0);W.WriteUInt8(0);W.WriteUInt16(0x3D);
  W.WriteUInt32(1u<<7);W.WriteUInt16(0xD3);W.WriteUInt16(Stamp);W.WriteFloat(1);W.Align();return W.GetData();
 }
private:
 struct FPacket { double At=0;uint32 Sequence=0;TArray<uint8> Bytes; };
 struct Later { bool operator()(const FPacket& A,const FPacket& B) const
  { return A.At==B.At ? A.Sequence<B.Sequence : A.At<B.At; } };
 FACESession& Session;
 static constexpr uint32 CryptoSeed=0x7143;
 FACEIsaac Keys{CryptoSeed};
 bool bInstalled=false;
 FSocket* Sender=nullptr;
 FSocket* Receiver=nullptr;
 TSharedPtr<FInternetAddr> Destination;
 TArray<FPacket> Pending;
 uint32 NextSequence=2;
};
#endif
