#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "Protocol/ACEIsaac.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRObserverProtocolTest, "ACE.VR.DesktopObserverNegotiation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEVRObserverProtocolTest::RunTest(const FString& Parameters)
{
    FACESession Session;
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Address=Sockets->CreateInternetAddr(); bool Valid=false;
    Address->SetIp(TEXT("127.0.0.1"),Valid); Address->SetPort(0);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Desktop pose observer test"),false);
    if (!Receiver) return false;
    ON_SCOPE_EXIT { Receiver->Close(); Sockets->DestroySocket(Receiver); };
    if (!TestTrue(TEXT("Loopback receiver binds"),Receiver->Bind(*Address))) return false;
    Receiver->GetAddress(*Address);
    Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Desktop observer sender"),false);
    Session.SocketC2S->SetNonBlocking(true);
    Session.ServerC2SAddr=Address; Session.IssacClient=MakeUnique<FACEIsaac>(123);
    Session.State=EACESessionState::EnteringWorld; Session.PlayerGuid=100;
    Session.PlayerPosition.CellId=0x7D640019;
    Session.MaybeEnterWorldComplete();
    TestEqual(TEXT("Desktop enters world without a VR component"),Session.State,EACESessionState::InWorld);
    auto LastPayload=[&]()
    {
        uint32 Last=0; for(const auto& Pair:Session.CachedC2SPackets) Last=FMath::Max(Last,Pair.Key);
        return Last ? Session.CachedC2SPackets[Last].Payload : TArray<uint8>();
    };
    auto Hello=LastPayload();
    if (!TestEqual(TEXT("Discovery has a fixed eight-byte trailer"),Hello.Num(),36)) return false;
    FACEBinaryReader HelloWire(Hello); HelloWire.Skip(24);
    TestEqual(TEXT("Discovery uses stock ACE's supported ping action"),HelloWire.ReadUInt32(),ACEGameAction::PingRequest);
    TestEqual(TEXT("Discovery marker"),HelloWire.ReadUInt32(),0x45554341u);
    TestEqual(TEXT("Discovery version"),HelloWire.ReadUInt32(),1u);
    const uint32 NextSequence=Session.NextGameActionSequence;
    Session.MaybeEnterWorldComplete();
    TestEqual(TEXT("Completed entry does not poll or resubscribe per frame"),Session.NextGameActionSequence,NextSequence);
    // Ordinary ACE replies only with PingResponse. Repeated headset refreshes
    // must never fall back to sending the unsupported F7D0 action.
    FACEBinaryWriter Ping; for(uint32 V:{100u,1u,ACEGameEvent::PingResponse}) Ping.WriteUInt32(V);
    FACEBinaryReader PingWire(Ping.GetData()); Session.HandleGameEvent(PingWire);
    Session.CachedC2SPackets.Reset();
    for(int32 I=0;I<20;++I) Session.RequestVRCapabilities(false);
    TestTrue(TEXT("Unsupported servers receive no extension probes or repeated pings"),Session.CachedC2SPackets.IsEmpty());

    auto Ack=[&](uint32 Flags,uint32 Version=1)
    {
        Session.CachedC2SPackets.Reset();
        FACEBinaryWriter W; for(uint32 V:{100u,1u,0xF7D0u,Version,Flags}) W.WriteUInt32(V);
        if(Flags&32u) { W.WriteUInt32(0); W.WriteFloat(20); }
        FACEBinaryReader R(W.GetData()); Session.HandleGameEvent(R);
    };
    for(uint32 Flags:{65527u|65536u,65527u|65536u|131072u,16u|131072u})
    {
        Ack(Flags);
        auto Payload=LastPayload();
        if (!(Flags&131072u))
        {
            TestTrue(TEXT("Older servers receive no unsupported desktop subscription"),Payload.IsEmpty());
            continue;
        }
        if (!TestEqual(TEXT("Subscription has a fixed twelve-byte payload"),Payload.Num(),40)) return false;
        FACEBinaryReader R(Payload); R.Skip(28);
        TestEqual(TEXT("Subscription version"),R.ReadUInt32(),1u);
        TestEqual(TEXT("Subscription kind"),R.ReadUInt32(),4u);
        const uint32 Features=R.ReadUInt32();
        TestEqual(TEXT("Desktop does not request unused VR combat/UI feedback"),Features&15u,0u);
        TestEqual(TEXT("Desktop opts in only when server advertises receive-only support"),(Features&16u)!=0,(Flags&131072u)!=0);
        TestEqual(TEXT("Equipment/root pose version is explicitly negotiated"),(Features&32u)!=0,(Flags&(131072u|32768u))==(131072u|32768u));
        TestEqual(TEXT("Negotiation does not impersonate a headset"),Session.VRPoseSequence,0u);
        TestEqual(TEXT("Receive-only desktop keeps normal position report cadence"),Session.LastVRPoseSent,-100.);
    }
    Ack(65527u|65536u|131072u);
    TestTrue(TEXT("Capability mask preserves retail combat power"),Session.SupportsVRCombatPower());
    Session.CachedC2SPackets.Reset(); Session.RequestVRCapabilities();
    bool EnabledFeedback=false;
    for(const auto& Packet:Session.CachedC2SPackets)
    {
        FACEBinaryReader R(Packet.Value.Payload); R.Skip(32);
        if(R.ReadUInt32()==4u) EnabledFeedback=R.ReadUInt32()==63u;
    }
    TestTrue(TEXT("A subsequently enabled headset subscribes to VR feedback without waiting for another ACK"),EnabledFeedback);
    FACEVRPose Pose; Pose.Cell=Session.PlayerPosition.CellId; Pose.Flags=7; Pose.EyeHeight=1.75f;
    Session.CachedC2SPackets.Reset();Session.bAutoPosContact=false;Session.AutoPosTimer=.04f;
    TestTrue(TEXT("Actual headset can send an equipment/root pose"),Session.SendVRPose(Pose));
    uint32 PositionPacket=0,TrackingPacket=0;
    for(const auto& Packet:Session.CachedC2SPackets)
    {
        FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);const auto Action=R.ReadUInt32();
        if(Action==ACEGameAction::AutonomousPosition)
        {
            PositionPacket=Packet.Key;R.Skip(40);
            TestEqual(TEXT("Pose-cadence feet retain the real airborne contact bit"),R.ReadUInt8(),uint8(0));
        }
        if(Action==0xF7D1u)TrackingPacket=Packet.Key;
    }
    TestTrue(TEXT("Tracked pose is sent after its current authoritative feet report"),PositionPacket>0 && TrackingPacket>PositionPacket);
    TestEqual(TEXT("Paired feet reset the independent report timer"),Session.AutoPosTimer,0.f);
    auto SentPose=LastPayload(); FACEBinaryReader PoseWire(SentPose); PoseWire.Skip(24);
    TestEqual(TEXT("Tracked pose action"),PoseWire.ReadUInt32(),0xF7D1u);
    TestEqual(TEXT("Negotiated outgoing v2 size"),PoseWire.Remaining(),176);
    TestEqual(TEXT("High capability bits select version two"),PoseWire.ReadUInt32(),2u);
    const uint32 AfterPose=Session.NextGameActionSequence;
    Session.bMoving=true;Session.AutoPosTimer=Session.AutonomousPositionInterval;
    Session.Tick(.02f);
    TestEqual(TEXT("Active tracking does not send duplicate timed feet reports"),Session.NextGameActionSequence,AfterPose);
    // Timed retail movement reports resume on landing; unchanged airborne
    // positions deliberately do not generate normal AutonomousPosition actions.
    Session.LastVRPoseSent=-100.;Session.bAutoPosContact=true;Session.Tick(.02f);
    auto Fallback=LastPayload();FACEBinaryReader FallbackWire(Fallback);FallbackWire.Skip(24);
    TestEqual(TEXT("Paused tracking resumes normal movement reports"),FallbackWire.ReadUInt32(),ACEGameAction::AutonomousPosition);
    Ack(262143u,2);
    TestFalse(TEXT("Unknown protocol version does not enable tracking"),Session.SupportsVRPoses());
    TestTrue(TEXT("Unknown protocol version sends no subscription"),Session.CachedC2SPackets.IsEmpty());
    Session.RequestVRCapabilities(true);
    TestTrue(TEXT("Unsupported protocol does not trigger a legacy hello"),Session.CachedC2SPackets.IsEmpty());
    Session.ClearWorldState(); Session.State=EACESessionState::InWorld;
    Session.RequestVRCapabilities(false);
    TestEqual(TEXT("New character can discover capabilities again"),Session.CachedC2SPackets.Num(),1);
    return true;
}
#endif
