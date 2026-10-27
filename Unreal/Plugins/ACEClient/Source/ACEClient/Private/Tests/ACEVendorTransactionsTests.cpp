#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVendorTransactionsTest, "ACE.Network.VendorTransactions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEVendorTransactionsTest::RunTest(const FString&)
{
    auto* Sockets = ISocketSubsystem::Get();
    auto* Receiver = FUdpSocketBuilder(TEXT("Vendor transaction fixture")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT { if (Receiver) { Receiver->Close(); Sockets->DestroySocket(Receiver); } };
    if (!TestNotNull(TEXT("Loopback receiver"), Receiver)) return false;
    auto Destination = Sockets->CreateInternetAddr();
    Destination->SetIp(0x7f000001); Destination->SetPort(Receiver->GetPortNo());
    FACESession Session;
    Session.SocketC2S = FUdpSocketBuilder(TEXT("Vendor transaction client")).AsNonBlocking().BoundToPort(0).Build();
    if (!TestNotNull(TEXT("Loopback sender"), Session.SocketC2S)) return false;
    Session.ServerC2SAddr = Destination; Session.IssacClient = MakeUnique<FACEIsaac>(123u);
    Session.State = EACESessionState::InWorld; Session.PlayerGuid = 1;
    ON_SCOPE_EXIT { Session.State = EACESessionState::Disconnected; };

    constexpr int32 Vendor = 0x7B4703F0;
    FACEWorldObject Book; Book.Guid = int32(0xF1234567u); Book.MaxStackSize = 1; Book.VendorQuantityAvailable = -1;
    FACEWorldObject Drink; Drink.Guid = int32(0xF1234568u); Drink.MaxStackSize = 250; Drink.VendorQuantityAvailable = -1;
    auto Complete = [&](uint32 Error)
    {
        FACEBinaryWriter W; W.WriteUInt32(Error); FACEBinaryReader R(W.GetData()); Session.HandleUseDone(R);
        TestFalse(TEXT("Vendor completion releases the hourglass"), Session.IsCursorBusy());
    };
    auto CheckWire = [&](uint32 Action, const TArray<TPair<int32, int32>>& Expected, uint32 Currency)
    {
        if (!TestEqual(TEXT("One transaction sends one reliable packet"), Session.CachedC2SPackets.Num(), 1)) return;
        const auto& Packet = Session.CachedC2SPackets.CreateConstIterator().Value();
        FACEBinaryReader R(Packet.Payload); R.Skip(14);
        TestEqual(TEXT("Vendor uses the retail weenie queue"), R.ReadUInt16(), ACEQueue::WeenieQueue);
        TestEqual(TEXT("Vendor action envelope"), R.ReadUInt32(), ACEOpcode::GameAction);
        R.ReadUInt32();
        TestEqual(TEXT("Transaction opcode"), R.ReadUInt32(), Action);
        TestEqual(TEXT("Transaction addresses the open vendor"), R.ReadUInt32(), uint32(Vendor));
        TestEqual(TEXT("Transaction item count"), R.ReadUInt32(), uint32(Expected.Num()));
        for (const auto& Item : Expected)
        {
            TestEqual(TEXT("Retail ItemProfile amount"), R.ReadInt32(), Item.Key);
            TestEqual(TEXT("Full unsigned stock GUID survives serialization"), R.ReadUInt32(), uint32(Item.Value));
        }
        // CM_Vendor::Event_Buy (006AA0F0) always appends the uint32 currency,
        // including zero for pyreals. Event_Sell (006AA000) has no such trailer.
        // A strict server must be able to read the complete request before it
        // can transact or send UseDone; stock ACE currently ignores this field.
        if (Action == ACEGameAction::Buy)
        {
            if (TestEqual(TEXT("Retail Buy includes the required four-byte currency trailer"), R.Remaining(), 4))
                TestEqual(TEXT("Buy echoes the vendor's advertised currency"), R.ReadUInt32(), Currency);
        }
        TestEqual(TEXT("No missing or extra transaction bytes"), R.Remaining(), 0);
        TestTrue(TEXT("Submitted transaction waits for server completion"), Session.IsCursorBusy());
        Session.CachedC2SPackets.Reset();
    };
    // Switch back to an ordinary vendor after an alternate-currency vendor;
    // the previous shop's currency must never leak into the next purchase.
    for (uint32 Currency : {0u, 0x12345678u, 0u})
    {
        FACEBinaryWriter Offer;
        Offer.WriteUInt32(Vendor); Offer.WriteUInt32(MAX_uint32);
        Offer.WriteInt32(-1); Offer.WriteInt32(-1); Offer.WriteUInt32(0);
        Offer.WriteFloat(.9f); Offer.WriteFloat(1.1f); Offer.WriteUInt32(Currency);
        Offer.WriteUInt32(10000); Offer.WriteString16L(Currency ? TEXT("Tokens") : TEXT("")); Offer.WriteUInt32(0);
        FACEBinaryReader OfferReader(Offer.GetData()); Session.HandleApproachVendor(OfferReader);
        Session.VendorMerchandise = {Book, Drink};

        Session.SendBuyItems(Vendor, {{1, Book.Guid}});
        CheckWire(ACEGameAction::Buy, {{1, Book.Guid}}, Currency); Complete(0);
        Session.SendBuyItems(Vendor, {{2, Drink.Guid}, {3, Drink.Guid}, {1, Book.Guid}});
        CheckWire(ACEGameAction::Buy, {{5, Drink.Guid}, {1, Book.Guid}}, Currency); Complete(1);
    }
    FACEWorldObject Owned = Drink; Owned.ContainerId = Session.PlayerGuid; Owned.StackSize = 5;
    Session.WorldObjects.Add(Owned.Guid, Owned);
    Session.SendSellItems(Vendor, {{5, Owned.Guid}});
    CheckWire(ACEGameAction::Sell, {{5, Owned.Guid}}, 0); Complete(0);

    Session.SendBuyItems(Vendor + 1, {{1, Book.Guid}});
    Session.SendBuyItems(Vendor, {{0, Book.Guid}, {1, 999}});
    TestTrue(TEXT("Invalid or stale purchases send no request"), Session.CachedC2SPackets.IsEmpty());
    TestFalse(TEXT("Rejected purchase never starts an hourglass"), Session.IsCursorBusy());
    return true;
}
#endif
