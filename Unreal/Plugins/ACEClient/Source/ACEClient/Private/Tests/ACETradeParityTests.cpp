#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACETradeParityTest,"ACE.Network.TradeParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACETradeParityTest::RunTest(const FString&)
{
    auto* Sockets=ISocketSubsystem::Get();
    auto* Receiver=FUdpSocketBuilder(TEXT("Retail trade receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT { if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);} };
    if(!TestNotNull(TEXT("Trade loopback receiver"),Receiver))return false;
    FACESession S;
    S.SocketC2S=FUdpSocketBuilder(TEXT("Retail trade client")).AsNonBlocking().BoundToPort(0).Build();
    if(!TestNotNull(TEXT("Trade loopback sender"),S.SocketC2S))return false;
    S.ServerC2SAddr=Sockets->CreateInternetAddr();S.ServerC2SAddr->SetIp(0x7f000001);S.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    S.IssacClient=MakeUnique<FACEIsaac>(123u);S.State=EACESessionState::InWorld;S.PlayerGuid=1;
    ON_SCOPE_EXIT { S.State=EACESessionState::Disconnected; };
    auto Register=[&](bool Initiator)
    {
        FACEBinaryWriter W;W.WriteUInt32(Initiator?1:2);W.WriteUInt32(2);W.WriteDouble(1234.5);
        FACEBinaryReader R(W.GetData());S.HandleRegisterTrade(R);
    };
    auto Event=[&](uint32 Opcode,TArray<uint32> Data)
    {
        FACEBinaryWriter W;W.WriteUInt32(1);W.WriteUInt32(1);W.WriteUInt32(Opcode);
        for(uint32 Value:Data)W.WriteUInt32(Value);
        FACEBinaryReader R(W.GetData());S.HandleGameEvent(R);
    };
    for(bool Initiator:{true,false})
    {
        Register(Initiator);
        Event(ACEGameEvent::AddToTrade,{0xF0000002,1,0});
        Event(ACEGameEvent::AddToTrade,{0xF0000001,1,0});
        Event(ACEGameEvent::AddToTrade,{0xF0000003,1,99});
        Event(ACEGameEvent::AddToTrade,{0xF0000004,2,0});
        TestEqual(TEXT("Trade insert location determines displayed order"),uint32(S.TradeSelfItems[0]),0xF0000001u);
        Event(ACEGameEvent::AcceptTrade,{2});
        Event(ACEGameEvent::AddToTrade,{0xF0000001,1,2});
        Event(ACEGameEvent::AddToTrade,{0xF0000001,2,0});
        Event(ACEGameEvent::AddToTrade,{0xF0000005,7,0});
        TestTrue(TEXT("Duplicates and invalid sides do not change accepted offers"),S.HasPartnerAcceptedTrade() && S.TradeSelfItems.Num()==3 && S.TradePartnerItems.Num()==1);
        S.CachedC2SPackets.Reset();S.SendAcceptTrade();
        if(!TestEqual(TEXT("Acceptance sends one reliable packet"),S.CachedC2SPackets.Num(),1))return false;
        const auto& Packet=S.CachedC2SPackets.CreateConstIterator().Value();
        FACEBinaryReader R(Packet.Payload);R.Skip(14);
        TestEqual(TEXT("Trade uses weenie queue"),R.ReadUInt16(),ACEQueue::WeenieQueue);
        TestEqual(TEXT("Trade uses game action envelope"),R.ReadUInt32(),ACEOpcode::GameAction);R.ReadUInt32();
        TestEqual(TEXT("Acceptance action"),R.ReadUInt32(),ACEGameAction::AcceptTrade);
        TestEqual(TEXT("Partner GUID"),R.ReadUInt32(),2u);
        TestEqual(TEXT("Registered timestamp is echoed exactly"),R.ReadDouble(),1234.5);
        TestEqual(TEXT("Retail registered status"),R.ReadUInt32(),2u);
        TestEqual(TEXT("Initiator is a boolean, not a GUID"),R.ReadUInt32(),Initiator?1u:0u);
        TestEqual(TEXT("Self acceptance flag"),R.ReadUInt32(),1u);
        TestEqual(TEXT("Existing partner acceptance flag"),R.ReadUInt32(),1u);
        for(const auto* Items:{&S.TradeSelfItems,&S.TradePartnerItems})
        {
            TestEqual(TEXT("ContentProfile list count"),R.ReadUInt32(),uint32(Items->Num()));
            for(int32 Guid:*Items)
            {
                TestEqual(TEXT("Full GUID and list order survive serialization"),R.ReadUInt32(),uint32(Guid));
                TestEqual(TEXT("Retail Trade::AddItem container flags"),R.ReadUInt32(),0u);
            }
        }
        TestEqual(TEXT("Both complete lists consume the entire payload"),R.Remaining(),0);
        TestTrue(TEXT("Local accept updates immediately without losing partner state"),S.HasAcceptedTrade()&&S.HasPartnerAcceptedTrade());
        Event(ACEGameEvent::AcceptTrade,{2});
        TestTrue(TEXT("Partner acknowledgement cannot overwrite self acceptance"),S.HasAcceptedTrade()&&S.HasPartnerAcceptedTrade());
        Event(ACEGameEvent::DeclineTrade,{2});
        TestTrue(TEXT("Partner decline leaves self acceptance intact"),S.HasAcceptedTrade()&&!S.HasPartnerAcceptedTrade());
        Event(ACEGameEvent::AcceptTrade,{2});Event(ACEGameEvent::AcceptTrade,{0});
        TestTrue(TEXT("Retail zero-source reset only clears self"),!S.HasAcceptedTrade()&&S.HasPartnerAcceptedTrade());
        Event(ACEGameEvent::AcceptTrade,{1});S.SendDeclineTrade();
        TestTrue(TEXT("Local decline only withdraws self acceptance"),!S.HasAcceptedTrade()&&S.HasPartnerAcceptedTrade());
        Event(ACEGameEvent::AcceptTrade,{99});Event(ACEGameEvent::DeclineTrade,{});
        TestTrue(TEXT("Unknown or truncated acceptance cannot mutate flags"),!S.HasAcceptedTrade()&&S.HasPartnerAcceptedTrade());
        Event(ACEGameEvent::TradeFailure,{0xF0000001,1});
        TestFalse(TEXT("Rejected offer item is removed"),S.TradeSelfItems.Contains(int32(0xF0000001)));
        TestFalse(TEXT("Changed offer clears acceptance"),S.HasAcceptedTrade()||S.HasPartnerAcceptedTrade());
        Event(ACEGameEvent::AcceptTrade,{1});Event(ACEGameEvent::RemoveFromTrade,{0xF0000002,1});
        TestFalse(TEXT("Removal requires reviewing the changed offer"),S.HasAcceptedTrade());
        for(int32 Bytes=0;Bytes<16;++Bytes)
        {
            FACEBinaryWriter W;W.WriteUInt32(99);W.WriteUInt32(100);W.WriteDouble(999.0);
            FACEBinaryReader Partial(W.GetData().GetData(),Bytes);S.HandleRegisterTrade(Partial);
            TestEqual(TEXT("Truncated registration preserves current partner"),S.GetTradePartnerGuid(),2);
            TestEqual(TEXT("Truncated registration preserves timestamp"),S.TradeStamp,1234.5);
        }
        Event(ACEGameEvent::ClearTradeAcceptance,{});
        TestTrue(TEXT("ACE clear acceptance retains its authoritative offer reset"),S.TradeSelfItems.IsEmpty()&&S.TradePartnerItems.IsEmpty());
        Event(ACEGameEvent::CloseTrade,{0});Event(ACEGameEvent::AcceptTrade,{2});
        TestTrue(TEXT("Closed trade cannot regain acceptance from late packets"),!S.HasAcceptedTrade()&&!S.HasPartnerAcceptedTrade());
    }
    return true;
}
#endif
