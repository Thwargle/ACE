#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "UI/ACEChatEntry.h"
#include "UI/ACEUIGameplayBinder.h"
#include "VR/ACEVRPlatformTextEntry.h"
#include "VR/ACEVRKeyboardSubmitInput.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEDatSubsystem.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ScopeExit.h"
#include "Dat/ACEDatTextLayout.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEChatParityTest, "ACE.RetailParity.ChatInputAndCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEChatParityTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Client = NewObject<UACEClientSubsystem>(GI);
    Client->Session = MakeShared<FACESession>();
    auto& Session = *Client->Session;
    Session.State = EACESessionState::InWorld; Session.PlayerGuid = 0x50000100;
    // Exercise actual serialization against an isolated local UDP receiver.
    auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    FSocket* Receiver = Sockets->CreateSocket(NAME_DGram,TEXT("Chat test receiver"),false);
    auto Address = Sockets->CreateInternetAddr(); bool Valid = false;
    Address->SetIp(TEXT("127.0.0.1"),Valid); Address->SetPort(0);
    if (!TestTrue(TEXT("Isolated chat receiver binds"),Receiver && Receiver->Bind(*Address))) return false;
    Receiver->GetAddress(*Address);
    Session.SocketC2S = Sockets->CreateSocket(NAME_DGram,TEXT("Chat test sender"),false);
    Session.ServerC2SAddr = Address; Session.IssacClient = MakeUnique<FACEIsaac>(123u);
    auto* Binder = NewObject<UACEUIGameplayBinder>(); Binder->Client = Client;
    auto* Main = NewObject<UACEChatEntry>(); Main->InitializeChat(Binder);
    auto* Other = NewObject<UACEChatEntry>(); Other->InitializeChat(Binder);
    auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* Resources=NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Output=NewObject<UACERetailTextBlock>(); Output->SetSelectable(true);
    Output->SetText(FText::FromString(TEXT("Copy this chat message")));
    Output->SetRetailElement(Resources,nullptr,FVector2D(1,1),200,false);
    Binder->ChatEntry = Main; Binder->FloatyChatEntries.SetNum(4); Binder->FloatyChatEntries[0] = Other;
    auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Chat input regression"))).ClientSize(FVector2D(500,140))
        [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Main->TakeWidget()]
            + SVerticalBox::Slot().AutoHeight()[Other->TakeWidget()]
            + SVerticalBox::Slot().AutoHeight()[Output->TakeWidget()]];
    auto& Slate = FSlateApplication::Get(); Slate.AddWindow(Window, false);
    const auto OldFocus = Slate.GetUserFocusedWidget(0);
    auto Focus = [&](UACEChatEntry* Entry, uint32 User = 0) { Slate.SetUserFocus(User, Entry->TakeWidget()); };
    auto Key = [&](FKey K, uint32 User = 0)
    {
        Slate.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),User,false,0,0));
        Slate.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),User,false,0,0));
    };
    auto Type = [&](const FString& Text)
    { for (TCHAR C : Text) Slate.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false)); };
    {
        FString PreviousClipboard; FPlatformApplicationMisc::ClipboardPaste(PreviousClipboard);
        ON_SCOPE_EXIT { FPlatformApplicationMisc::ClipboardCopy(*PreviousClipboard); };
        auto Ctrl=[&](FKey K)
        {
            const FModifierKeysState Mod(false,false,true,false,false,false,false,false,false);
            Slate.ProcessKeyDownEvent(FKeyEvent(K,Mod,0,false,0,0));
            Slate.ProcessKeyUpEvent(FKeyEvent(K,Mod,0,false,0,0));
        };
        auto ReadClipboard=[](FString& Value)
        {
            // Clipboard listeners can briefly own the Windows clipboard just
            // after SetClipboardData. Model the delay between real key presses.
            for (int32 Try=0; Try<20; ++Try)
            {
                FPlatformProcess::Sleep(.01f);
                FPlatformApplicationMisc::ClipboardPaste(Value);
                if (!Value.IsEmpty()) break;
            }
        };
        Slate.SetUserFocus(0,Output->TakeWidget()); Ctrl(EKeys::A); Ctrl(EKeys::C);
        FString Copied; ReadClipboard(Copied);
        TestEqual(TEXT("Read-only retail chat output can be selected and copied"),Copied,Output->GetText().ToString());
        if (const auto* GlyphFont=Output->GetBitmapFont())
        {
            auto TextWidget=Output->TakeWidget();
            const auto Geometry=FGeometry::MakeRoot(FVector2D(200,60),FSlateLayoutTransform());
            TextWidget->OnMouseButtonDown(Geometry,FPointerEvent(0,FVector2D(0,1),FVector2D(0,1),
                {EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            float Width=0;
            for (TCHAR C:FString(TEXT("Copy"))) Width+=ACEDatText::Advance(*GlyphFont,C);
            TextWidget->OnMouseButtonDown(Geometry,FPointerEvent(0,FVector2D(Width,1),FVector2D(Width,1),
                {EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState(true,false,false,false,false,false,false,false,false)));
            Ctrl(EKeys::C); ReadClipboard(Copied);
            TestEqual(TEXT("Mouse selection follows native glyph advances"),Copied,FString(TEXT("Copy")));
        }
        Main->SetChatText(TEXT("")); Focus(Main); Ctrl(EKeys::V);
        TestEqual(TEXT("Copied chat pastes into the editable entry"),Main->GetText().ToString(),Copied);
        Ctrl(EKeys::A); Ctrl(EKeys::C); Main->SetChatText(TEXT(""));
        Ctrl(EKeys::V);
        TestEqual(TEXT("The input field supports copy as well as paste"),Main->GetText().ToString(),Copied);
        Main->SetChatText(TEXT(""));
    }
    auto Submit = [&](const FString& Text, int32 W = 0)
    { Binder->TrySendChatFromEntry(&Text,W); };
    auto LastAction = [&]()
    {
        uint32 Last = 0; for (const auto& Packet : Session.CachedC2SPackets) Last = FMath::Max(Last,Packet.Key);
        return Session.CachedC2SPackets.Contains(Last) ? Session.CachedC2SPackets[Last].Payload : TArray<uint8>();
    };
    auto TellPacket = [&](const FString& Name, const FString& Body)
    {
        const auto Bytes = LastAction();
        if (!TestTrue(TEXT("A private chat packet was serialized"),Bytes.Num() >= 32)) return;
        FACEBinaryReader R(Bytes); R.Skip(16);
        TestEqual(TEXT("Private chat uses a game action"),R.ReadUInt32(),ACEOpcode::GameAction); R.ReadUInt32();
        TestEqual(TEXT("Private chat uses name tells across landblocks"),R.ReadUInt32(),ACEGameAction::Tell);
        TestEqual(TEXT("Tell payload preserves the message"),R.ReadString16L(),Body);
        TestEqual(TEXT("Tell payload has the intended recipient"),R.ReadString16L(),Name);
    };
    Submit(TEXT("first line")); Submit(TEXT("last line")); Submit(TEXT("last line"));
    Focus(Main); Key(EKeys::Up);
    TestEqual(TEXT("Focused text widget receives Up and recalls last send"),Main->GetText().ToString(),FString(TEXT("last line")));
    Key(EKeys::Up); TestEqual(TEXT("Retail history retains repeated sends"),Main->GetText().ToString(),FString(TEXT("last line")));
    Key(EKeys::Up); Key(EKeys::Up);
    TestEqual(TEXT("Up stops at the oldest line"),Main->GetText().ToString(),FString(TEXT("first line")));
    Key(EKeys::Down); Key(EKeys::Down); Key(EKeys::Down);
    TestTrue(TEXT("Down past newest clears the entry"),Main->GetText().IsEmpty());
    Type(TEXT("draft")); Key(EKeys::Down); TestTrue(TEXT("Retail Down also clears an unsent draft"),Main->GetText().IsEmpty());
    Key(EKeys::Up); Type(TEXT("!"));
    TestEqual(TEXT("Recall positions the caret after the recalled message"),Main->GetText().ToString(),FString(TEXT("last line!")));
    Submit(TEXT("auxiliary message"),1); Focus(Other); Key(EKeys::Up);
    TestEqual(TEXT("Auxiliary entry owns its history"),Other->GetText().ToString(),FString(TEXT("auxiliary message")));
    Focus(Main); Main->RememberSubmitted(TEXT("main still separate")); Key(EKeys::Up);
    TestEqual(TEXT("Auxiliary sends cannot contaminate main history"),Main->GetText().ToString(),FString(TEXT("main still separate")));
    for (int32 I=0;I<105;++I) Main->RememberSubmitted(FString::Printf(TEXT("line %d"),I));
    for (int32 I=0;I<110;++I) Key(EKeys::Up);
    TestEqual(TEXT("History retains exactly the last hundred entries"),Main->GetText().ToString(),FString(TEXT("line 5")));

    const auto VirtualUserHandle = Slate.FindOrCreateVirtualUser(31);
    const uint32 VirtualUser = VirtualUserHandle->GetUserIndex();
    Other->RememberSubmitted(TEXT("VR last message")); Focus(Other,VirtualUser); Key(EKeys::Up,VirtualUser);
    TestEqual(TEXT("VR virtual-user arrow input recalls the focused window"),Other->GetText().ToString(),FString(TEXT("VR last message")));

    Session.LastTellSenderGuid = 0x50000002; Session.LastTellSenderName = TEXT("Old Friend");
    for (const TCHAR* Alias : {TEXT("/r "),TEXT("@r "),TEXT("/RP "),TEXT("@reply ")})
    {
        Main->SetText(FText::GetEmpty()); Focus(Main); Type(Alias);
        TestEqual(TEXT("Reply expands while typing the delimiter, before Enter"),Main->GetText().ToString(),FString(TEXT("@tell Old Friend, ")));
        Type(TEXT("hello"));
        TestEqual(TEXT("Typing continues after the expanded recipient"),Main->GetText().ToString(),FString(TEXT("@tell Old Friend, hello")));
    }
    Main->SetText(FText::GetEmpty()); Type(TEXT("/r"));
    TestEqual(TEXT("Partial command stays editable until its delimiter"),Main->GetText().ToString(),FString(TEXT("/r")));
    Type(TEXT("eally ")); TestEqual(TEXT("Unknown command is not mistaken for reply"),Main->GetText().ToString(),FString(TEXT("/really ")));

    Main->SetText(FText::GetEmpty()); Session.CachedC2SPackets.Reset(); int Finished = 0;
    Main->OnTextCommitted.AddDynamic(Binder,&UACEUIGameplayBinder::HandleChatTextCommitted);
    auto Native = MakeShared<FACEVRPlatformTextEntry>(Main,[&]{++Finished;});
    Native->SetTextFromVirtualKeyboard(FText::FromString(TEXT("/r ")),ETextEntryType::TextEntryUpdated);
    TestEqual(TEXT("Quest native keyboard gets reply expansion too"),Main->GetText().ToString(),FString(TEXT("@tell Old Friend, ")));
    Session.LastTellSenderName = TEXT("New Caller");
    Native->SetTextFromVirtualKeyboard(FText::FromString(TEXT("/r hello")),ETextEntryType::TextEntryUpdated);
    TestEqual(TEXT("Another incoming tell cannot redirect an in-progress native reply"),Main->GetText().ToString(),FString(TEXT("@tell Old Friend, hello")));
    Native->SetTextFromVirtualKeyboard(FText::FromString(TEXT("/r hello")),ETextEntryType::TextEntryAccepted);
    TellPacket(TEXT("Old Friend"),TEXT("hello"));
    const int32 Sent = Session.CachedC2SPackets.Num();
    Native->SetTextFromVirtualKeyboard(FText::FromString(TEXT("/r late")),ETextEntryType::TextEntryAccepted);
    TestEqual(TEXT("Late native accept cannot send twice"),Session.CachedC2SPackets.Num(),Sent);
    TestEqual(TEXT("Native keyboard closes once"),Finished,1);
    Main->NavigateHistory(true);
    TestEqual(TEXT("History keeps the expanded recipient, not a retargetable shortcut"),Main->GetText().ToString(),FString(TEXT("@tell Old Friend, hello")));

    // Both native IME acceptance and the PC VR keyboard must send directly.
    auto CheckSay = [&](const FString& Expected)
    {
        const auto Bytes=LastAction();
        if (!TestTrue(TEXT("Keyboard confirmation serializes chat"),Bytes.Num() >= 30)) return;
        FACEBinaryReader R(Bytes); R.Skip(24);
        TestEqual(TEXT("Keyboard confirmation sends Say"),R.ReadUInt32(),ACEGameAction::Talk);
        TestEqual(TEXT("Confirmation includes the final typed characters"),R.ReadString16L(),Expected);
    };
    Session.CachedC2SPackets.Reset(); Main->SetChatText(TEXT("draft"));
    auto Confirm=MakeShared<FACEVRPlatformTextEntry>(Main,[]{});
    Confirm->SetTextFromVirtualKeyboard(FText::FromString(TEXT("partial")),ETextEntryType::TextEntryUpdated);
    Confirm->SetTextFromVirtualKeyboard(FText::FromString(TEXT("complete message")),ETextEntryType::TextEntryAccepted);
    CheckSay(TEXT("complete message"));
    TestEqual(TEXT("Native OK sends exactly once without Submit"),Session.CachedC2SPackets.Num(),1);
    TestTrue(TEXT("Native OK clears the sent chat entry"),Main->GetText().IsEmpty());
    Confirm->SetTextFromVirtualKeyboard(FText::FromString(TEXT("complete message")),ETextEntryType::TextEntryCanceled);
    TestTrue(TEXT("Dismissal after OK cannot restore the sent draft"),Main->GetText().IsEmpty());
    auto Cancel=MakeShared<FACEVRPlatformTextEntry>(Main,[]{});
    Cancel->SetTextFromVirtualKeyboard(FText::FromString(TEXT("unsent")),ETextEntryType::TextEntryCanceled);
    TestEqual(TEXT("Closing without confirmation never sends"),Session.CachedC2SPackets.Num(),1);

    Other->OnTextCommitted.AddDynamic(Binder,&UACEUIGameplayBinder::HandleFloatyChat1Committed);
    Session.CachedC2SPackets.Reset(); Other->SetChatText(TEXT("second window"));
    auto Auxiliary=MakeShared<FACEVRPlatformTextEntry>(Other,[]{});
    Auxiliary->SetTextFromVirtualKeyboard(FText::FromString(TEXT("auxiliary final")),ETextEntryType::TextEntryAccepted);
    CheckSay(TEXT("auxiliary final"));
    TestEqual(TEXT("Auxiliary native OK sends without Submit"),Session.CachedC2SPackets.Num(),1);
    Session.CachedC2SPackets.Reset(); Main->SetChatText(TEXT("PC VR enter")); Focus(Main,VirtualUser); Key(EKeys::Enter,VirtualUser);
    CheckSay(TEXT("PC VR enter"));
    TestEqual(TEXT("PC VR Enter sends without Submit"),Session.CachedC2SPackets.Num(),1);

    int NativeRequests=0;
    FACEVRKeyboardSubmitInput NativeKeys([&]{++NativeRequests;});
    const FKeyEvent Enter(EKeys::Enter,FModifierKeysState(),0,false,0,0);
    TestTrue(TEXT("Native hardware-user Enter is consumed before viewport input"),NativeKeys.HandleKeyDownEvent(Slate,Enter));
    NativeKeys.HandleKeyDownEvent(Slate,Enter); NativeKeys.HandleKeyUpEvent(Slate,Enter);
    TestEqual(TEXT("Repeated native Enter requests one confirmation"),NativeRequests,1);
    TestFalse(TEXT("Native submit filter leaves cancellation alone"),NativeKeys.HandleKeyDownEvent(Slate,FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0)));

    Session.CachedC2SPackets.Reset(); Binder->LastOutgoingTellName.Reset();
    Binder->TryDispatchChatCommand(TEXT("/rt not a reply"));
    TestEqual(TEXT("Retell never silently targets the incoming teller"),Session.CachedC2SPackets.Num(),0);
    Binder->TryDispatchChatCommand(TEXT("/tell \"Old Friend\", hello, again")); TellPacket(TEXT("Old Friend"),TEXT("hello, again"));
    Binder->TryDispatchChatCommand(TEXT("/rt another message")); TellPacket(TEXT("Old Friend"),TEXT("another message"));
    Binder->TryDispatchChatCommand(TEXT("/r\thello with tabs")); TellPacket(TEXT("New Caller"),TEXT("hello with tabs"));
    for (const auto& Pair : {TPair<uint32,FString>(ACEChatChannel::Patron,TEXT("Vassal Friend")), {ACEChatChannel::Monarch,TEXT("Follower Friend")}})
    {
        FACEBinaryWriter W; W.WriteUInt32(Pair.Key); W.WriteString16L(Pair.Value); W.WriteString16L(TEXT("hello"));
        FACEBinaryReader R(W.GetData()); Session.HandleChannelBroadcast(R);
        const FString Shortcut = Pair.Key==ACEChatChannel::Patron ? TEXT("/pr ") : TEXT("/mr ");
        TestEqual(TEXT("Relationship reply expands to the last corresponding sender"),Binder->ExpandChatReply(Shortcut),TEXT("@tell ")+Pair.Value+TEXT(", "));
        Binder->TryDispatchChatCommand(Shortcut+TEXT("answer")); TellPacket(Pair.Value,TEXT("answer"));
    }
    TestEqual(TEXT("Channel senders do not overwrite private reply target"),Session.LastTellSenderName,FString(TEXT("New Caller")));
    for (const auto& Pair : {TPair<FString,uint32>(TEXT("/f hello"),ACEChatChannel::Fellow),{TEXT("@ab hello"),ACEChatChannel::AllegianceBroadcast},
        {TEXT("/p hello"),ACEChatChannel::Patron},{TEXT("/m hello"),ACEChatChannel::Monarch},{TEXT("/v hello"),ACEChatChannel::Vassals},{TEXT("/c hello"),ACEChatChannel::CoVassals}})
    {
        Binder->TryDispatchChatCommand(Pair.Key); const auto Bytes=LastAction();
        if (!TestTrue(TEXT("Channel packet serialized"),Bytes.Num() >= 34)) continue;
        FACEBinaryReader R(Bytes); R.Skip(24);
        TestEqual(TEXT("Channel shortcut sends channel action"),R.ReadUInt32(),ACEGameAction::ChatChannel);
        TestEqual(TEXT("Channel shortcut preserves destination"),R.ReadUInt32(),Pair.Value);
        TestEqual(TEXT("Channel shortcut preserves content"),R.ReadString16L(),FString(TEXT("hello")));
    }
    Binder->TryDispatchChatCommand(TEXT("/server_only_command arg"));
    { const auto Bytes=LastAction(); if (TestTrue(TEXT("Command packet serialized"),Bytes.Num() >= 30)) { FACEBinaryReader R(Bytes); R.Skip(24);
      TestEqual(TEXT("Server commands continue through Talk"),R.ReadUInt32(),ACEGameAction::Talk);
      TestEqual(TEXT("Server command keeps the required prefix"),R.ReadString16L(),FString(TEXT("@server_only_command arg"))); } }
    Session.LastTellSenderGuid=0; Session.LastTellSenderName.Reset();
    TestEqual(TEXT("No teller leaves the draft unexpanded"),Binder->ExpandChatReply(TEXT("/r ")),FString(TEXT("/r ")));
    Session.CachedC2SPackets.Reset(); Binder->TryDispatchChatCommand(TEXT("/r hello"));
    TestEqual(TEXT("No teller cannot broadcast a private message as say"),Session.CachedC2SPackets.Num(),0);
    Session.SendSoulEmoteMotion(0x13000084u);
    { const auto Bytes=LastAction(); if(TestTrue(TEXT("Point emote packet serialized"),Bytes.Num()>=50))
      { FACEBinaryReader R(Bytes);R.Skip(24);
        TestEqual(TEXT("Point uses MoveToState"),R.ReadUInt32(),ACEGameAction::MoveToState);
        const uint32 Flags=R.ReadUInt32();TestEqual(TEXT("Point contains one emote"),Flags>>11,1u);
        R.ReadUInt32();R.ReadUInt32();
        TestEqual(TEXT("Point sends accepted retail PointState rather than its transition"),R.ReadUInt16(),uint16(0xf0)); } }
    Slate.ClearUserFocus(VirtualUser); Slate.ClearUserFocus(0); Slate.RequestDestroyWindow(Window);
    if (OldFocus) Slate.SetUserFocus(0,OldFocus);
    Client->Session.Reset(); Receiver->Close(); Sockets->DestroySocket(Receiver);
    AddInfo(TEXT("Compared with retail ChatInterface::ProcessCommand/SelectCommandFromHistory/HandleTextReplacements and ClientCommunicationSystem::DoTell/DoReply/DoReTell/OnChannelBroadcast. No live chat was sent."));
    return true;
}
#endif
