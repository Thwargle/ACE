#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include "Protocol/ACEHash32.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELoginHandshakeTest, "ACE.Network.LoginHandshake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACELoginHandshakeTest::RunTest(const FString&)
{
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
