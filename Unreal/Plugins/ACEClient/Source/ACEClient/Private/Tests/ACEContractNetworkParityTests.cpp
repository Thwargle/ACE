#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformTime.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEContractNetworkParityTest, "ACE.Network.ContractParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEContractNetworkParityTest::RunTest(const FString&)
{
    FACESession Session;
    Session.PlayerGuid=100; Session.State=EACESessionState::InWorld;
    auto* Sockets=ISocketSubsystem::Get();
    auto* Receiver=FUdpSocketBuilder(TEXT("Contract parity receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT { Session.State=EACESessionState::Disconnected; if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);} };
    if (!TestNotNull(TEXT("Loopback receiver"),Receiver)) return false;
    Session.SocketC2S=FUdpSocketBuilder(TEXT("Contract parity sender")).AsNonBlocking().BoundToPort(0).Build();
    if (!TestNotNull(TEXT("Loopback sender"),Session.SocketC2S)) return false;
    Session.ServerC2SAddr=Sockets->CreateInternetAddr();
    Session.ServerC2SAddr->SetIp(0x7f000001); Session.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    Session.IssacClient=MakeUnique<FACEIsaac>(123u);
    auto Event=[&](uint32 Opcode,const FACEBinaryWriter& Payload,int32 Length=INDEX_NONE)
    {
        FACEBinaryWriter W; W.WriteUInt32(100); W.WriteUInt32(1); W.WriteUInt32(Opcode);
        W.WriteBytes(Payload.GetData().GetData(),Length==INDEX_NONE?Payload.GetData().Num():Length);
        FACEBinaryReader R(W.GetData()); Session.HandleGameEvent(R);
    };
    auto Tracker=[](FACEBinaryWriter& W,int32 Id,int32 Stage,double Done,double Repeat)
    {
        W.WriteUInt32(0); W.WriteInt32(Id); W.WriteInt32(Stage); W.WriteDouble(Done); W.WriteDouble(Repeat);
    };
    auto Delta=[&](int32 Id,int32 Stage,double Done,double Repeat,bool Delete=false)
    {
        FACEBinaryWriter W; Tracker(W,Id,Stage,Done,Repeat); W.WriteUInt32(Delete?1:0); W.WriteUInt32(0); return W;
    };
    int32 Notices=0; Session.OnContractsChanged.AddLambda([&](){++Notices;});
    FACEContractEntry Old; Old.ContractId=9; Old.Stage=2; Old.ReceivedAt=123.; Session.Contracts.Add(Old);
    FACEBinaryWriter Table; Table.WriteUInt16(2); Table.WriteUInt16(32);
    Table.WriteUInt32(101); Tracker(Table,101,2,16777217.125,0.0);
    Table.WriteUInt32(102); Tracker(Table,102,3,0.0,16777219.875);
    for (int32 Size=0;Size<Table.GetData().Num();++Size)
    {
        Event(ACEGameEvent::SendClientContractTrackerTable,Table,Size);
        if (!TestTrue(TEXT("Every truncated snapshot preserves contracts, timestamps and notifications"),Notices==0
            && Session.Contracts.Num()==1 && Session.Contracts[0].ContractId==9 && Session.Contracts[0].ReceivedAt==123.)) return false;
    }
    FACEBinaryWriter Impossible; Impossible.WriteUInt16(MAX_uint16); Impossible.WriteUInt16(32);
    Event(ACEGameEvent::SendClientContractTrackerTable,Impossible);
    TestEqual(TEXT("Impossible table count cannot clear journal"),Session.Contracts.Num(),1);
    const double ReceiptStart=FPlatformTime::Seconds();
    Event(ACEGameEvent::SendClientContractTrackerTable,Table);
    if (!TestEqual(TEXT("Complete snapshot replaces old entries"),Session.Contracts.Num(),2)) return false;
    TestTrue(TEXT("Wire double timers retain precision and share receipt timestamp"),Notices==1
        && Session.Contracts[0].TimeWhenDone==16777217.125 && Session.Contracts[1].TimeWhenRepeats==16777219.875
        && Session.Contracts[0].ReceivedAt==Session.Contracts[1].ReceivedAt
        && Session.Contracts[0].ReceivedAt>=ReceiptStart && Session.Contracts[0].ReceivedAt<=FPlatformTime::Seconds());
    const double ReceivedAt=Session.Contracts[0].ReceivedAt;
    const auto Update=Delta(101,4+12,1800.25,0.0);
    for (int32 Size=0;Size<Update.GetData().Num();++Size)
    {
        Event(ACEGameEvent::SendClientContractTracker,Update,Size);
        if (!TestTrue(TEXT("Delta needs both trailer flags before changing state"),Notices==1
            && Session.Contracts[0].Stage==2 && Session.Contracts[0].ReceivedAt==ReceivedAt)) return false;
    }
    for (double Bad : {std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    {
        Event(ACEGameEvent::SendClientContractTracker,Delta(101,3,0.0,Bad));
        FACEBinaryWriter Invalid; Invalid.WriteUInt16(2); Invalid.WriteUInt16(32);
        Invalid.WriteUInt32(500); Tracker(Invalid,500,2,100.0,0.0);
        Invalid.WriteUInt32(501); Tracker(Invalid,501,3,Bad,0.0);
        Event(ACEGameEvent::SendClientContractTrackerTable,Invalid);
        TestTrue(TEXT("Nonfinite timer cannot partially install a table or enter UI sorting"),Notices==1
            && Session.Contracts.Num()==2 && Session.Contracts[0].ContractId==101 && Session.Contracts[0].Stage==2);
    }
    Event(ACEGameEvent::SendClientContractTracker,Update);
    TestTrue(TEXT("Delta updates existing contract without duplicating it"),Notices==2
        && Session.Contracts.Num()==2 && Session.Contracts[0].Stage==16 && Session.Contracts[0].TimeWhenDone==1800.25);
    Event(ACEGameEvent::SendClientContractTracker,Delta(103,1,0.0,0.0));
    TestTrue(TEXT("New contract is added"),Notices==3 && Session.Contracts.Num()==3);
    Event(ACEGameEvent::SendClientContractTracker,Delta(102,3,0.0,0.0,true));
    TestTrue(TEXT("Abandon confirmation removes only that contract"),Notices==4 && Session.Contracts.Num()==2
        && Session.Contracts[0].ContractId==101 && Session.Contracts[1].ContractId==103);
    Event(ACEGameEvent::SendClientContractTracker,Delta(999,0,0.0,0.0,true));
    TestEqual(TEXT("Unknown delete never inserts a contract"),Session.Contracts.Num(),2);
    FACEBinaryWriter Repeated; Repeated.WriteUInt16(2); Repeated.WriteUInt16(32);
    Repeated.WriteUInt32(104); Tracker(Repeated,0,2,10.0,0.0);
    Repeated.WriteUInt32(104); Tracker(Repeated,104,3,0.0,25.0);
    Event(ACEGameEvent::SendClientContractTrackerTable,Repeated);
    TestTrue(TEXT("Hash-table identity retains last value and tolerates key-only custom ID"),Session.Contracts.Num()==1
        && Session.Contracts[0].ContractId==104 && Session.Contracts[0].Stage==3);

    const int32 BeforeSend=Session.CachedC2SPackets.Num();
    Session.SendAbandonContract(104);
    TestEqual(TEXT("Abandon sends one request"),Session.CachedC2SPackets.Num(),BeforeSend+1);
    TestEqual(TEXT("Abandon waits for server confirmation before removing data"),Session.Contracts.Num(),1);
    bool Found=false;
    for (const auto& Packet:Session.CachedC2SPackets)
    {
        FACEBinaryReader R(Packet.Value.Payload);
        if (!R.CanRead(32)) continue;
        R.Skip(16); if (R.ReadUInt32()!=ACEOpcode::GameAction) continue; R.ReadUInt32();
        if (R.ReadUInt32()==ACEGameAction::AbandonContract)
            Found=R.ReadInt32()==104 && R.Remaining()==0;
    }
    TestTrue(TEXT("Abandon uses retail opcode and contract ID payload"),Found);
    Session.SendAbandonContract(0); Session.State=EACESessionState::Disconnected; Session.SendAbandonContract(104);
    TestEqual(TEXT("Invalid or disconnected abandon sends nothing"),Session.CachedC2SPackets.Num(),BeforeSend+1);
    FACEBinaryWriter Empty; Empty.WriteUInt16(0); Empty.WriteUInt16(32);
    Event(ACEGameEvent::SendClientContractTrackerTable,Empty);
    TestTrue(TEXT("Complete empty snapshot clears journal"),Session.Contracts.IsEmpty());
    return true;
}
#endif
