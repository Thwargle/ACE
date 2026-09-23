#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include "Protocol/ACEHash32.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELoginHandshakeTest, "ACE.Network.LoginHandshake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACELoginHandshakeTest::RunTest(const FString&)
{
    // Decode exactly the fields consumed by GDLE CNetwork::ConnectionRequest
    // and ACE AuthenticationData, rather than changing the identity elsewhere.
    FACELoginCredentials Login; Login.Account=TEXT("FiXTure"); Login.Password=TEXT("P:a SS");
    for (bool GDLE : {false,true})
    {
        Login.bGDLE=GDLE;
        const auto Bytes=FACESession::BuildLoginRequestBody(Login);
        FACEBinaryReader R(Bytes);
        TestEqual(TEXT("Retail protocol version remains shared"),R.ReadString16L(),FString(TEXT("1802")));
        const uint32 Length=R.ReadUInt32();
        TestEqual(TEXT("Authentication length matches remaining bytes"),int32(Length),R.Remaining());
        TestEqual(TEXT("Auth type matches configured emulator"),R.ReadUInt32(),GDLE?1u:2u);
        TestEqual(TEXT("Auth flags"),R.ReadUInt32(),0u); R.ReadUInt32();
        if (GDLE)
        {
            TestEqual(TEXT("GDLE matches retail -a lowercasing of both account and password"),R.ReadString16L(),FString(TEXT("fixture:p:a ss")));
            TestEqual(TEXT("GDLE crypto-data length"),R.ReadUInt32(),0u);
            TestEqual(TEXT("GDLE extra-data length"),R.ReadUInt32(),0u);
            TestEqual(TEXT("GDLE has no ACE ticket field"),R.Remaining(),0);
        }
        else
        {
            TestEqual(TEXT("ACE receives plain account identity"),R.ReadString16L(),Login.Account);
            TestTrue(TEXT("ACE account override is empty"),R.ReadString16L().IsEmpty());
            TestEqual(TEXT("ACE password packed size"),R.ReadUInt32(),uint32(Login.Password.Len()+1));
            TestEqual(TEXT("ACE password byte length"),R.ReadUInt8(),uint8(Login.Password.Len()));
            TestEqual(TEXT("ACE password keeps its original case"),R.ReadUInt8(),uint8('P'));
        }
        TestEqual(TEXT("Wire formatting never alters stored account identity"),Login.Account,FString(TEXT("FiXTure")));
        TestEqual(TEXT("Wire formatting never alters stored password"),Login.Password,FString(TEXT("P:a SS")));
    }
    // Observe the actual acknowledgement on loopback. GDLE requires the assigned
    // recipient ID; ACE's cookie-based acknowledgement must retain ID zero.
    auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    FSocket* Receiver = FUdpSocketBuilder(TEXT("ACEHandshakeFixture")).AsNonBlocking().BoundToPort(0).Build();
    if (!TestNotNull(TEXT("Handshake capture socket"), Receiver)) return false;
    auto Address = Sockets->CreateInternetAddr(); Receiver->GetAddress(*Address);
    bool ValidAddress = false; Address->SetIp(TEXT("127.0.0.1"), ValidAddress);
    for (bool GDLE : {false,true})
    {
        FACESession Session; Session.Creds.bGDLE = GDLE; Session.ClientId = 0x1234;
        Session.ConnectionCookie = 0x0123456789abcdefULL;
        Session.ServerS2CAddr = Address;
        Session.SocketS2C = FUdpSocketBuilder(TEXT("ACEHandshakeSender")).AsNonBlocking().BoundToPort(0).Build();
        Session.SendConnectResponse();
        uint8 Buffer[128]; int32 Read = 0;
        if (TestTrue(TEXT("ConnectResponse reaches configured destination"), Receiver->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(1))
            && Receiver->Recv(Buffer, sizeof(Buffer), Read) && Read == 28))
        {
            FACEBinaryReader R(Buffer, Read);
            TestEqual(TEXT("Handshake remains unsequenced"), R.ReadUInt32(), 0u);
            TestEqual(TEXT("Captured acknowledgement flags"), R.ReadUInt32(), uint32(EACEPacketHeaderFlags::ConnectResponse));
            R.ReadUInt32();
            TestEqual(TEXT("GDLE recipient ID and ACE zero-ID acknowledgement"), R.ReadUInt16(), uint16(GDLE ? 0x1234 : 0));
            R.Skip(6);
            TestEqual(TEXT("Acknowledgement returns the server cookie"), R.ReadUInt64(), Session.ConnectionCookie);
        }
    }
    Receiver->Close(); Sockets->DestroySocket(Receiver);
    constexpr uint32 Seed = 0x12345678;
    auto Packet = [](uint32 Sequence, EACEPacketHeaderFlags Flags, const TArray<uint8>& Body, uint32 PayloadHash, uint32 Key)
    {
        FACEBinaryWriter W;
        W.WriteUInt32(Sequence); W.WriteUInt32(static_cast<uint32>(Flags));
        W.WriteUInt32(FACESession::HeaderHash32(Sequence, Flags, 0, 0, Body.Num(), 1) + (PayloadHash ^ Key));
        W.WriteUInt16(0); W.WriteUInt16(0); W.WriteUInt16(Body.Num()); W.WriteUInt16(1);
        W.WriteBytes(Body);
        return W.GetData();
    };
    FACEBinaryWriter Connect;
    Connect.WriteDouble(1000.0); Connect.WriteUInt64(0x0123456789abcdefULL);
    Connect.WriteUInt32(0); Connect.WriteUInt32(Seed); Connect.WriteUInt32(0x87654321); Connect.WriteUInt32(0);
    const auto Handshake = Packet(1, EACEPacketHeaderFlags::ConnectRequest, Connect.GetData(), FACEHash32::Calculate(Connect.GetData()), 0);
    auto EncryptedMessage = [&](const TArray<uint8>& Message)
    {
        FACEBinaryWriter Fragment;
        Fragment.WriteUInt32(1); Fragment.WriteUInt32(0x80000000);
        Fragment.WriteUInt16(1); Fragment.WriteUInt16(16 + Message.Num());
        Fragment.WriteUInt16(0); Fragment.WriteUInt16(ACEQueue::UIQueue);
        const uint32 Hash = FACEHash32::Calculate(Fragment.GetData()) + FACEHash32::Calculate(Message);
        Fragment.WriteBytes(Message);
        FACEIsaac Keys(Seed);
        return Packet(2, EACEPacketHeaderFlags::EncryptedChecksum | EACEPacketHeaderFlags::BlobFragments,
            Fragment.GetData(), Hash, Keys.Next());
    };
    auto Deliver = [](FACESession& S, const TArray<uint8>& Bytes) { S.HandleDatagram(Bytes.GetData(), Bytes.Num(), false); };
    // Exercise the wire handlers and the state-change notification consumed by
    // desktop/PC VR/Quest launchers, not just the error-message lookup.
    for(const auto& Case : TArray<TPair<uint32,FString>>{
        {1,TEXT("already logged in")},{3,TEXT("account information")},{4,TEXT("server disconnected")},
        {5,TEXT("log out")},{6,TEXT("delete")},{8,TEXT("server disconnected")},
        {9,TEXT("account name or login method")},{10,TEXT("does not exist")},
        {11,TEXT("enter the world")},{12,TEXT("test character")},{13,TEXT("still in the world")},
        {14,TEXT("account for this character")},{15,TEXT("does not belong")},{16,TEXT("still in the world")},
        {17,TEXT("enter the world")},{18,TEXT("saved data")},{19,TEXT("starting area")},
        {20,TEXT("place this character")},{21,TEXT("server is full")},{23,TEXT("still saving")},
        {24,TEXT("expired")},{0xDEAD,TEXT("without a recognized explanation")}})
    {
        FACEBinaryWriter Error; Error.WriteUInt32(ACEOpcode::CharacterError); Error.WriteUInt32(Case.Key);
        for(bool Early : {false,true})
        {
            FACESession Session; Session.State=EACESessionState::AwaitConnectRequest;
            FString Observed;
            Session.OnStateChanged.AddLambda([&](EACESessionState State){if(State==EACESessionState::Failed) Observed=Session.GetConnectionError();});
            if(Early) Deliver(Session,EncryptedMessage(Error.GetData()));
            Deliver(Session,Handshake);
            if(!Early) Deliver(Session,EncryptedMessage(Error.GetData()));
            TestTrue(TEXT("Authenticated login rejection supplies the specific reason to the UI"),Observed.Contains(Case.Value));
            TestTrue(TEXT("Login rejection retains its original numeric code"),Observed.Contains(FString::Printf(TEXT("error %u"),Case.Key)));
            Session.OnStateChanged.Clear(); Session.Disconnect();
            TestTrue(TEXT("Next login starts without the old rejection"),Session.GetConnectionError().IsEmpty());
        }
    }
    for(const auto& Case : TArray<TPair<uint32,FString>>{
        {0x0C559B1E,TEXT("already logged in")},{0x00F9982C,TEXT("server is full")},
        {0x00A7E948,TEXT("client version")},{0x082E3779,TEXT("login method")},
        {0x04DF9C54,TEXT("username, password")},{0x12345678,TEXT("unrecognized connection error")}})
    for(auto Flag : {EACEPacketHeaderFlags::NetError,EACEPacketHeaderFlags::NetErrorDisconnect})
    {
        FACESession Session; Session.Creds.bGDLE=true; Session.State=EACESessionState::AwaitConnectRequest;
        FACEBinaryWriter Error; Error.WriteUInt32(Case.Key);Error.WriteUInt32(8);
        Deliver(Session,Packet(0,Flag,Error.GetData(),FACEHash32::Calculate(Error.GetData()),0));
        TestTrue(TEXT("GDLE login rejection explains the actual server reason"),Session.GetConnectionError().Contains(Case.Value));
        TestTrue(TEXT("GDLE errors preserve the original StringInfo ID"),Session.GetConnectionError().Contains(FString::Printf(TEXT("0x%08X"),Case.Key)));
        TestTrue(TEXT("Specific GDLE reasons still terminate rejected logins"),Session.GetState()==EACESessionState::Failed);
    }
    {
        FACESession Session; Session.State=EACESessionState::AwaitConnectRequest;
        FACEBinaryWriter Error;Error.WriteUInt32(0x0C559B1E);Error.WriteUInt32(9);
        Deliver(Session,Packet(0,EACEPacketHeaderFlags::NetError,Error.GetData(),FACEHash32::Calculate(Error.GetData()),0));
        TestTrue(TEXT("Unknown string tables cannot invent an account-in-use reason"),Session.GetConnectionError().Contains(TEXT("unrecognized")));
    }
    // Harvestbud sends this cleartext retail NetError before it supplies any keys.
    FACEBinaryWriter NetError;
    NetError.WriteUInt32(0x04DF9C54); NetError.WriteUInt32(8);
    for (const auto Flag : {EACEPacketHeaderFlags::NetError, EACEPacketHeaderFlags::NetErrorDisconnect})
    {
        FACESession Rejected;
        Rejected.State = EACESessionState::AwaitConnectRequest;
        FString ObservedError;
        Rejected.OnStateChanged.AddLambda([&](EACESessionState) { ObservedError = Rejected.GetConnectionError(); });
        const auto Bytes = Packet(0, Flag, NetError.GetData(), FACEHash32::Calculate(NetError.GetData()), 0);
        auto Damaged = Bytes; Damaged[8] ^= 1;
        Deliver(Rejected, Damaged);
        TestTrue(TEXT("NetError with bad checksum cannot fail login"), Rejected.GetState() == EACESessionState::AwaitConnectRequest);
        FACEBinaryWriter Truncated; Truncated.WriteUInt32(0x04DF9C54);
        Deliver(Rejected, Packet(0, Flag, Truncated.GetData(), FACEHash32::Calculate(Truncated.GetData()), 0));
        TestTrue(TEXT("Truncated NetError cannot fail login"), Rejected.GetState() == EACESessionState::AwaitConnectRequest);
        Deliver(Rejected, Bytes);
        TestTrue(TEXT("GDLE rejection fails immediately without waiting for timeout"), Rejected.GetState() == EACESessionState::Failed);
        TestTrue(TEXT("Login UI receives readable rejection and original error code"), ObservedError.Contains(TEXT("server rejected")) && ObservedError.Contains(TEXT("04DF9C54")));
        Rejected.OnStateChanged.Clear();

        FACESession Active; Active.State = EACESessionState::InWorld;
        Deliver(Active, Bytes);
        TestTrue(TEXT("Late cleartext login rejection cannot disconnect authenticated gameplay"), Active.GetState() == EACESessionState::InWorld);
        Active.IssacServer = MakeUnique<FACECryptoSystem>(Seed);
        FACEIsaac ErrorKeys(Seed);
        FACEBinaryWriter WithTime; WithTime.WriteBytes(NetError.GetData()); WithTime.WriteDouble(1000.0);
        Deliver(Active, Packet(2, Flag | EACEPacketHeaderFlags::EncryptedChecksum | EACEPacketHeaderFlags::TimeSync,
            WithTime.GetData(), FACEHash32::Calculate(WithTime.GetData()), ErrorKeys.Next()));
        TestTrue(TEXT("Encrypted NetError mixed with other optional headers fails active connection"), Active.GetState() == EACESessionState::Failed);
        TestTrue(TEXT("World connection errors allow recovery"), Active.bRecoverLostConnection);
    }
    FACEBinaryWriter Boot;
    Boot.WriteUInt32(ACEOpcode::AccountBoot); Boot.WriteString16L(TEXT(" because the password entered for this account was not correct"));
    const auto Rejection = EncryptedMessage(Boot.GetData());

    for (bool bReordered : {false, true})
    {
        FACESession Session;
        Session.State = EACESessionState::AwaitConnectRequest;
        FString ErrorAtStateChange;
        Session.OnStateChanged.AddLambda([&](EACESessionState State)
        {
            if (State == EACESessionState::Failed) ErrorAtStateChange = Session.GetConnectionError();
        });
        if (bReordered)
        {
            Deliver(Session, Rejection);
            TestEqual(TEXT("Early encrypted rejection waits for keys"), Session.PreHandshakeDatagrams.Num(), 1);
            TestTrue(TEXT("Unverified rejection cannot fail the connection"), Session.GetState() == EACESessionState::AwaitConnectRequest);
        }
        Deliver(Session, Handshake);
        if (!bReordered) Deliver(Session, Rejection);
        TestTrue(TEXT("Authenticated rejection fails promptly in either delivery order"), Session.GetState() == EACESessionState::Failed);
        TestTrue(TEXT("UI receives the rejection reason before the failure event"), ErrorAtStateChange.Contains(TEXT("password")));
        TestTrue(TEXT("Early packet queue is released"), Session.PreHandshakeDatagrams.IsEmpty());
        Deliver(Session, Handshake);
        TestTrue(TEXT("Repeated seed packet cannot revive a rejected login"), Session.GetState() == EACESessionState::Failed);
        Session.OnStateChanged.Clear();
        Session.Disconnect();
        TestTrue(TEXT("Disconnect clears stale error text"), Session.GetConnectionError().IsEmpty());
    }

    FACEBinaryWriter List;
    List.WriteUInt32(ACEOpcode::CharacterList); List.WriteUInt32(0); List.WriteUInt32(1);
    List.WriteUInt32(0x50000001); List.WriteString16L(TEXT("Fixture character")); List.WriteUInt32(0);
    List.WriteUInt32(0); List.WriteUInt32(11); List.WriteString16L(TEXT("Fixture account"));
    const auto Characters = EncryptedMessage(List.GetData());
    FACESession Success;
    Success.State = EACESessionState::AwaitConnectRequest;
    Deliver(Success, Characters); Deliver(Success, Handshake);
    TestTrue(TEXT("Early valid character list completes login"), Success.GetState() == EACESessionState::CharacterSelect);
    TestEqual(TEXT("Character list retains its entries"), Success.Characters.Num(), 1);
    Deliver(Success, Handshake);
    TestTrue(TEXT("Duplicate handshake cannot reset authenticated state"), Success.GetState() == EACESessionState::CharacterSelect);

    FACESession Corrupt;
    Corrupt.State = EACESessionState::AwaitConnectRequest;
    auto Bad = Rejection; Bad[8] ^= 1;
    Deliver(Corrupt, Bad); Deliver(Corrupt, Handshake);
    TestTrue(TEXT("Deferred data still requires a valid checksum"), Corrupt.GetState() == EACESessionState::AwaitCharacterList);
    TestTrue(TEXT("Bad deferred data cannot supply an error reason"), Corrupt.GetConnectionError().IsEmpty());
    Deliver(Corrupt, Rejection);
    TestTrue(TEXT("Valid retransmission works after a bad deferred packet"), Corrupt.GetState() == EACESessionState::Failed);

    FACESession Bounded;
    Bounded.State = EACESessionState::AwaitConnectRequest;
    for (int32 I = 0; I < 64; ++I) Deliver(Bounded, Rejection);
    TestEqual(TEXT("Pre-handshake buffering is bounded"), Bounded.PreHandshakeDatagrams.Num(), 32);
    Bounded.Disconnect();
    TestTrue(TEXT("Disconnect releases queued unauthenticated packets"), Bounded.PreHandshakeDatagrams.IsEmpty());
    return true;
}
#endif
