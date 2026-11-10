#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEBookContainerParityTest, "ACE.Network.BookContainerParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEBookContainerParityTest::RunTest(const FString&)
{
    FACESession Session;
    Session.PlayerGuid = 100;
    Session.State = EACESessionState::InWorld;
    auto* Sockets = ISocketSubsystem::Get();
    auto* Receiver = FUdpSocketBuilder(TEXT("Book parity receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT { Session.State=EACESessionState::Disconnected; if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);} };
    if (!TestNotNull(TEXT("Loopback receiver"), Receiver)) return false;
    Session.SocketC2S = FUdpSocketBuilder(TEXT("Book parity sender")).AsNonBlocking().BoundToPort(0).Build();
    if (!TestNotNull(TEXT("Loopback sender"), Session.SocketC2S)) return false;
    Session.ServerC2SAddr = Sockets->CreateInternetAddr();
    Session.ServerC2SAddr->SetIp(0x7f000001); Session.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    Session.IssacClient = MakeUnique<FACEIsaac>(123u);
    auto Event = [&](uint32 Opcode, const TArray<uint8>& Payload, int32 Length=INDEX_NONE)
    {
        FACEBinaryWriter W; W.WriteUInt32(100); W.WriteUInt32(1); W.WriteUInt32(Opcode);
        W.WriteBytes(Payload.GetData(), Length==INDEX_NONE ? Payload.Num() : Length);
        FACEBinaryReader R(W.GetData()); Session.HandleGameEvent(R);
    };
    auto Page = [](FACEBinaryWriter& W, bool Modern, bool Included, const FString& Text)
    {
        W.WriteUInt32(123); W.WriteString16L(TEXT("Page author")); W.WriteString16L(TEXT(""));
        if (Modern) { W.WriteUInt32(0xFFFF0002); W.WriteUInt32(Included ? 1 : 0); W.WriteUInt32(1); }
        else W.WriteUInt32(Included ? 1 : 0);
        if (Included) W.WriteString16L(Text);
    };
    auto Book = [&](int32 Guid, bool Included, int32 Count=3)
    {
        FACEBinaryWriter W; W.WriteUInt32(Guid); W.WriteInt32(15); W.WriteInt32(15); W.WriteInt32(1000); W.WriteInt32(Count);
        for (int32 I=0; I<Count; ++I) Page(W,I!=1,Included,I==2 ? TEXT("") : FString::Printf(TEXT("Page %d"),I));
        W.WriteString16L(TEXT("Inscription")); W.WriteUInt32(123); W.WriteString16L(TEXT("Scribe"));
        return W;
    };
    auto Reply = [&](int32 Guid, int32 Index, bool Modern, bool Included, const FString& Text)
    {
        FACEBinaryWriter W; W.WriteUInt32(Guid); W.WriteInt32(Index); Page(W,Modern,Included,Text); return W;
    };
    auto Requests = [&](int32 Guid, int32 Index)
    {
        int32 Count=0;
        for (const auto& Packet : Session.CachedC2SPackets)
        {
            FACEBinaryReader R(Packet.Value.Payload);
            if (!R.CanRead(36)) continue;
            R.Skip(16); if (R.ReadUInt32()!=ACEOpcode::GameAction) continue; R.ReadUInt32();
            if (R.ReadUInt32()==ACEGameAction::BookPageData && R.ReadInt32()==Guid && R.ReadInt32()==Index)
            { TestEqual(TEXT("Book request contains exactly ID and zero-based page"),R.Remaining(),0); ++Count; }
        }
        return Count;
    };
    int32 Notices=0;
    Session.OnBookChanged.AddLambda([&](){++Notices;});
    const auto Complete=Book(500,true);
    Event(ACEGameEvent::BookDataResponse,Complete.GetData());
    TestTrue(TEXT("Both retail page forms decode and capacity is separate from readable pages"),
        Session.Book.NumPages==3 && Session.Book.MaxPages==15 && Session.Book.Pages.Num()==3
        && Session.Book.Pages[1]==TEXT("Page 1") && Session.Book.HasPageText(2) && Session.Book.Pages[2].IsEmpty());
    TestEqual(TEXT("Inline text avoids redundant first-page request"),Requests(500,0),0);
    const auto Replacement=Book(501,false);
    for (int32 Size=0; Size<Replacement.GetData().Num(); ++Size)
    {
        Event(ACEGameEvent::BookDataResponse,Replacement.GetData(),Size);
        if (!TestTrue(TEXT("Incomplete book never replaces current text or notifies"),Session.Book.BookGuid==500
            && Session.Book.Pages[0]==TEXT("Page 0") && Session.Book.AuthorName==TEXT("Scribe") && Notices==1)) return false;
    }
    FACEBinaryWriter BadCount; BadCount.WriteUInt32(501); BadCount.WriteInt32(15); BadCount.WriteInt32(15);
    BadCount.WriteInt32(1000); BadCount.WriteInt32(MAX_int32);
    Event(ACEGameEvent::BookDataResponse,BadCount.GetData());
    TestEqual(TEXT("Impossible page count does not allocate or replace"),Session.Book.BookGuid,500);
    Event(ACEGameEvent::BookDataResponse,Replacement.GetData());
    TestTrue(TEXT("Metadata opens reader and requests first missing page once"),Notices==2 && Requests(501,0)==1 && Session.PendingBookPage==0);
    Session.SendBookPageData(501,0); Session.SendBookPageData(501,1);
    TestTrue(TEXT("Repeated clicks wait for the pending page like retail"),Requests(501,0)==1 && Requests(501,1)==0 && Session.Book.CurrentPage==0);
    const auto First=Reply(501,0,true,true,TEXT("First page"));
    for (int32 Size=0; Size<First.GetData().Num(); ++Size)
    {
        Event(ACEGameEvent::BookPageDataResponse,First.GetData(),Size);
        if (!TestTrue(TEXT("Truncated page cannot finish request or install empty text"),Notices==2
            && Session.PendingBookPage==0 && !Session.Book.HasPageText(0))) return false;
    }
    Event(ACEGameEvent::BookPageDataResponse,First.GetData());
    TestTrue(TEXT("Complete page supplies text and releases navigation"),Session.Book.Pages[0]==TEXT("First page")
        && Session.Book.HasPageText(0) && Session.PendingBookPage==INDEX_NONE && Notices==3);
    Session.SendBookPageData(501,1);
    TestTrue(TEXT("Navigation updates selected page and sends one fetch"),Session.Book.CurrentPage==1 && Requests(501,1)==1 && Notices==4);
    const auto NoText=Reply(501,1,false,false,TEXT(""));
    Event(ACEGameEvent::BookPageDataResponse,NoText.GetData());
    TestTrue(TEXT("Metadata-only reply is not an empty completed page"),Session.PendingBookPage==1 && !Session.Book.HasPageText(1) && Notices==4);
    Session.BookPageRequestedAt-=6.; // A host that never supplies text must not lock the reader forever.
    Session.SendBookPageData(501,2);
    const auto Second=Reply(501,1,false,true,TEXT("Second page"));
    Event(ACEGameEvent::BookPageDataResponse,Second.GetData());
    TestTrue(TEXT("Late reply caches its text without moving the reader or finishing another request"),Session.Book.CurrentPage==2
        && Session.PendingBookPage==2 && Session.Book.HasPageText(1) && Session.Book.HasPageText(0) && Notices==5);
    const auto Blank=Reply(501,2,true,true,TEXT(""));
    Event(ACEGameEvent::BookPageDataResponse,Blank.GetData());
    TestTrue(TEXT("Empty received page is loaded and releases the wait"),Session.Book.HasPageText(2)
        && Session.Book.Pages[2].IsEmpty() && Session.PendingBookPage==INDEX_NONE && Notices==6);
    Session.SendBookPageData(501,1); Session.SendBookPageData(501,0); Session.SendBookPageData(501,2);
    TestTrue(TEXT("Previously read pages navigate locally without network refetch"),Session.Book.CurrentPage==2
        && Requests(501,0)==1 && Requests(501,1)==1 && Requests(501,2)==1 && Notices==9);
    const int32 PacketCount=Session.CachedC2SPackets.Num();
    for (int32 Index : {-1,3,MAX_int32})
    {
        Session.SendBookPageData(501,Index);
        Event(ACEGameEvent::BookPageDataResponse,Reply(501,Index,true,true,TEXT("Invalid")).GetData());
    }
    Session.SendBookPageData(999,0);
    Event(ACEGameEvent::BookPageDataResponse,Reply(999,0,true,true,TEXT("Wrong book")).GetData());
    TestTrue(TEXT("Wrong book and out-of-range page cannot change or grow state"),Session.Book.Pages.Num()==3
        && Session.Book.CurrentPage==2 && Notices==9 && Session.CachedC2SPackets.Num()==PacketCount);
    Session.ClearBook(); Event(ACEGameEvent::BookPageDataResponse,Blank.GetData());
    TestTrue(TEXT("Closed reader ignores late page replies"),!Session.Book.bOpen && Session.Book.Pages.IsEmpty() && Notices==10);
    Event(ACEGameEvent::BookDataResponse,Book(502,false,0).GetData());
    TestTrue(TEXT("Empty book has no phantom fetch for unwritten capacity"),Session.Book.NumPages==0 && Session.Book.Pages.IsEmpty() && Requests(502,0)==0);
    Event(ACEGameEvent::BookDataResponse,Complete.GetData()); Session.SendBookPageData(500,2);
    Event(ACEGameEvent::BookDataResponse,Book(500,false).GetData());
    TestTrue(TEXT("Refreshing the same book keeps the selected page and fetches only that page"),Session.Book.CurrentPage==2
        && Requests(500,2)==1 && Requests(500,0)==0);
    Event(ACEGameEvent::BookDataResponse,Book(500,true,2).GetData());
    TestTrue(TEXT("Shorter refreshed book clamps selection to its final existing page"),Session.Book.CurrentPage==1
        && Session.Book.Pages[1]==TEXT("Page 1") && Session.PendingBookPage==INDEX_NONE);
    Event(ACEGameEvent::BookDataResponse,Replacement.GetData()); Session.ClearWorldState();
    TestTrue(TEXT("World reset discards previous character's book and pending request"),!Session.Book.bOpen && Session.PendingBookPage==INDEX_NONE);

    Session.PlayerGuid=100;
    Session.OpenExternalContainerGuid=700;
    Session.ContainerContents.Add(700,{{703,0}});
    FACEWorldObject Item; Item.Guid=801; Item.ItemType=ACEItemType::Misc; Session.WorldObjects.Add(Item.Guid,Item);
    Session.ClosedContainerItemDeadlines.Add(801,123.);
    Session.BeginPendingUse(800,0);
    int32 OpenNotices=0,CloseNotices=0;
    Session.OnViewContentsExternal.AddLambda([&](int32){++OpenNotices;});
    Session.OnCloseGroundContainer.AddLambda([&](int32){++CloseNotices;});
    FACEBinaryWriter Contents; Contents.WriteUInt32(800); Contents.WriteUInt32(2);
    Contents.WriteUInt32(801); Contents.WriteUInt32(0); Contents.WriteUInt32(802); Contents.WriteUInt32(0);
    for (int32 Size=0; Size<Contents.GetData().Num(); ++Size)
    {
        Event(ACEGameEvent::ViewContents,Contents.GetData(),Size);
        if (!TestTrue(TEXT("Partial container snapshot leaves ownership, expiry and UI unchanged"),Session.OpenExternalContainerGuid==700
            && Session.ContainerContents.Contains(700) && !Session.ContainerContents.Contains(800)
            && Session.WorldObjects[801].ContainerId==0 && Session.ClosedContainerItemDeadlines.Contains(801)
            && OpenNotices==0 && CloseNotices==0)) return false;
    }
    FACEBinaryWriter Huge; Huge.WriteUInt32(800); Huge.WriteUInt32(MAX_uint32);
    Event(ACEGameEvent::ViewContents,Huge.GetData());
    TestEqual(TEXT("Impossible container count cannot replace open loot"),Session.OpenExternalContainerGuid,700);
    Event(ACEGameEvent::ViewContents,Contents.GetData());
    TestTrue(TEXT("Complete contents install ownership and replace old loot UI together"),Session.OpenExternalContainerGuid==800
        && Session.ContainerContents[800].Num()==2 && Session.WorldObjects[801].ContainerId==800
        && !Session.ClosedContainerItemDeadlines.Contains(801) && OpenNotices==1 && CloseNotices==1);
    FACEBinaryWriter Empty; Empty.WriteUInt32(800); Empty.WriteUInt32(0);
    Event(ACEGameEvent::ViewContents,Empty.GetData());
    TestTrue(TEXT("Valid empty contents still clears the displayed list"),Session.ContainerContents[800].IsEmpty());
    Event(ACEGameEvent::BookDataResponse,Replacement.GetData()); Session.Disconnect();
    TestTrue(TEXT("Disconnect discards book cache and pending request"),!Session.Book.bOpen && Session.PendingBookPage==INDEX_NONE);
    return true;
}
#endif
