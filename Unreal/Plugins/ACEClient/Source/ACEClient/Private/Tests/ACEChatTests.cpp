#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEClientSubsystem.h"
#include "ACEInputBindings.h"
#include "Misc/ConfigCacheIni.h"
#include "ACESession.h"
#include "UI/ACEChatEntry.h"
#include "UI/ACEUIGameplayBinder.h"
#include "VR/ACEVRPlatformTextEntry.h"
#include "VR/ACEVRKeyboardSubmitInput.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
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
#include "Slate/WidgetRenderer.h"
#include "Misc/App.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEChatParityTest, "ACE.RetailParity.ChatInputAndCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEChatParityTest::RunTest(const FString&)
{
    const FString OriginalConfig = GGameUserSettingsIni;
    GGameUserSettingsIni = FPaths::ProjectSavedDir() / TEXT("Automation/ChatToggleFixture.ini");
    FConfigFile EmptyConfig; GConfig->SetFile(GGameUserSettingsIni, &EmptyConfig);
    ACEInputBindings::Reload();
    ON_SCOPE_EXIT { GGameUserSettingsIni = OriginalConfig; ACEInputBindings::Reload(); };
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
    auto* Output2=NewObject<UACERetailTextBlock>(); Output2->SetSelectable(true);
    Output2->SetText(FText::FromString(TEXT("Second message")));
    Output2->SetRetailElement(Resources,nullptr,FVector2D(1,1),200,false);
    auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Chat input regression"))).ClientSize(FVector2D(500,140))
        [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Main->TakeWidget()]
            + SVerticalBox::Slot().AutoHeight()[Other->TakeWidget()]
            + SVerticalBox::Slot().AutoHeight()[Output->TakeWidget()]
            + SVerticalBox::Slot().AutoHeight()[Output2->TakeWidget()]];
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
        Output->GetSelectionPeers=Output2->GetSelectionPeers=[Output,Output2]{return TArray<UACERetailTextBlock*>{Output,Output2};};
        Window->SlatePrepass();
        if (FApp::CanEverRender())
        {
            FWidgetRenderer Renderer;
            Renderer.DrawWidget(Window,FVector2D(500,140));
        }
        auto First=Output->GetSelectionWidget().ToSharedRef();
        auto Second=Output2->GetSelectionWidget().ToSharedRef();
        FWidgetPath FirstPath,SecondPath;
        Slate.GeneratePathToWidgetUnchecked(First,FirstPath);
        Slate.GeneratePathToWidgetUnchecked(Second,SecondPath);
        if (TestTrue(TEXT("Both chat rows are arranged"),FirstPath.IsValid() && SecondPath.IsValid()))
        {
            const auto G1=FirstPath.Widgets.Last().Geometry,G2=SecondPath.Widgets.Last().Geometry;
            const FVector2D Start=G1.LocalToAbsolute(FVector2D(0,1));
            const FVector2D End=G2.LocalToAbsolute(FVector2D(199,1));
            Slate.SetUserFocus(0,First);
            First->OnMouseButtonDown(G1,FPointerEvent(0,Start,Start,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            Slate.GetUser(0)->SetCursorCaptor(First,FirstPath);
            First->OnMouseMove(G1,FPointerEvent(0,End,Start,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
            Ctrl(EKeys::C); ReadClipboard(Copied);
            TestEqual(TEXT("Dragging across chat messages copies both complete lines"),Copied,FString(TEXT("Copy this chat message\r\nSecond message")));
            Slate.GetUser(0)->ReleaseCursorCapture();
        }
        // The editable chat command is intentionally single-line; test its
        // clipboard behavior separately from copying multiline chat history.
        Copied=TEXT("Copy"); FPlatformApplicationMisc::ClipboardCopy(*Copied);
        Main->SetChatText(TEXT("")); Focus(Main); Ctrl(EKeys::V);
        TestEqual(TEXT("Copied chat pastes into the editable entry"),Main->GetText().ToString(),Copied);
        Ctrl(EKeys::A); Ctrl(EKeys::C);
        FString EntryCopied; ReadClipboard(EntryCopied);
        TestEqual(TEXT("The input field copies its selection"),EntryCopied,Copied);
        Main->SetChatText(TEXT(""));
        Ctrl(EKeys::V);
        TestEqual(TEXT("The input field supports copy as well as paste"),Main->GetText().ToString(),Copied);
        Main->SetChatText(TEXT(""));
    }
    auto Submit = [&](const FString& Text, int32 W = 0)
    { Binder->TrySendChatFromEntry(&Text,W); };
    Main->SetChatText(TEXT("hello world")); Focus(Main); Key(EKeys::End);
    for (int I=0; I<5; ++I) Key(EKeys::Left);
    Key(EKeys::Tab);
    TestFalse(TEXT("Tab returns from chat to game input"), Binder->IsChatEntryFocused());
    TestEqual(TEXT("Toggling chat leaves the unsent draft intact"), Main->GetText().ToString(), FString(TEXT("hello world")));
    Binder->ToggleChatEntryFocus(); Type(TEXT("new "));
    TestEqual(TEXT("Toggling back restores the middle-of-line caret"), Main->GetText().ToString(), FString(TEXT("hello new world")));
    Other->SetChatText(TEXT("floaty draft")); Focus(Other); Key(EKeys::Home); Key(EKeys::Tab);
    Binder->ToggleChatEntryFocus(); Type(TEXT("my "));
    TestEqual(TEXT("Floaty chat resumes its own draft and caret"), Other->GetText().ToString(), FString(TEXT("my floaty draft")));
    TestEqual(TEXT("Toggle does not submit a network chat message"), Session.CachedC2SPackets.Num(), 0);
    Main->SetChatText(TEXT("")); Other->SetChatText(TEXT(""));
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

    // Placement diagnostics are local even on custom servers and must not use
    // the selected prop, request an appraisal, or broadcast diagnostic data.
    Client->SelectObject(0);
    Session.CachedC2SPackets.Reset();
    TestTrue(TEXT("Object report handles missing selection locally"),Binder->TryDispatchChatCommand(TEXT("/aceobject")));
    FACEWorldObject Crystal; Crystal.Guid=0x71001001; Crystal.Name=TEXT("Tou-Tou");
    Crystal.WeenieClassId=1050067; Crystal.SetupId=0x02001AC5; Crystal.MotionTableId=0x090001FC;
    Crystal.ItemType=ACEItemType::Creature; Crystal.Scale=.75f;
    Session.WorldObjects.Add(Crystal.Guid,Crystal); Client->SelectObject(Crystal.Guid);
    Session.CachedC2SPackets.Reset();
    TestTrue(TEXT("Object report handles unloaded world actors"),Binder->TryDispatchChatCommand(TEXT("/aceobject")));
    TestEqual(TEXT("Object report never sends diagnostics or an action to the server"),Session.CachedC2SPackets.Num(),0);
    TestEqual(TEXT("Object report preserves the selected crystal"),Client->GetSelectedObject().Guid,Crystal.Guid);
    Client->SelectObject(0); Session.WorldObjects.Remove(Crystal.Guid);

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
    {
        // Route real Slate Enter/W events through an edit box and a registered
        // game viewport; merely checking that chat lost focus misses this bug.
        class FChatReturnViewport : public ISlateViewport
        {
        public:
            int32 MovementKeys=0;
            FIntPoint GetSize() const override { return FIntPoint(320,100); }
            FSlateShaderResource* GetViewportRenderTargetTexture() const override { return nullptr; }
            bool RequiresVsync() const override { return false; }
            FReply OnKeyDown(const FGeometry&,const FKeyEvent& Event) override
            {
                if(Event.GetKey()==EKeys::W) { ++MovementKeys; return FReply::Handled(); }
                return FReply::Unhandled();
            }
        };
        auto Input=MakeShared<FChatReturnViewport>();
        auto Viewport=SNew(SViewport).ViewportInterface(Input);
        auto* FocusBinder=NewObject<UACEUIGameplayBinder>(); FocusBinder->Client=Client;
        auto* EmptyMain=NewObject<UACEChatEntry>(); EmptyMain->InitializeChat(FocusBinder);
        auto* EmptyOther=NewObject<UACEChatEntry>(); EmptyOther->InitializeChat(FocusBinder);
        FocusBinder->ChatEntry=EmptyMain; FocusBinder->FloatyChatEntries.SetNum(4); FocusBinder->FloatyChatEntries[0]=EmptyOther;
        EmptyMain->SetClearKeyboardFocusOnCommit(true); EmptyOther->SetClearKeyboardFocusOnCommit(true);
        EmptyMain->OnTextCommitted.AddDynamic(FocusBinder,&UACEUIGameplayBinder::HandleChatTextCommitted);
        EmptyOther->OnTextCommitted.AddDynamic(FocusBinder,&UACEUIGameplayBinder::HandleFloatyChat1Committed);
        auto FocusWindow=SNew(SWindow).ClientSize(FVector2D(320,180))
            [SNew(SVerticalBox) + SVerticalBox::Slot()[Viewport]
                + SVerticalBox::Slot().AutoHeight()[EmptyMain->TakeWidget()]
                + SVerticalBox::Slot().AutoHeight()[EmptyOther->TakeWidget()]];
        const auto PreviousViewport=Slate.GetGameViewport();
        const uint32 PreviousOptions=Session.CharacterOptions1;
        Slate.AddWindow(FocusWindow,false); Slate.RegisterGameViewport(Viewport);
        ON_SCOPE_EXIT {
            Slate.UnregisterGameViewport();
            if(PreviousViewport) Slate.RegisterGameViewport(PreviousViewport.ToSharedRef());
            Slate.RequestDestroyWindow(FocusWindow); Session.CharacterOptions1=PreviousOptions;
        };
        for(bool StayInChat : {false,true}) for(auto* Entry : {EmptyMain,EmptyOther})
            for(const TCHAR* Blank : {TEXT(""),TEXT("   "),TEXT("\u200B\uFEFF\u00A0")})
        {
            Session.CharacterOptions1=StayInChat ? 0x00000800u : 0;
            Session.CachedC2SPackets.Reset(); FocusBinder->CancelPendingChatRefocus();
            Entry->SetChatText(Blank); Focus(Entry); Key(EKeys::Enter);
            TestTrue(TEXT("Blank Enter schedules a handoff after Slate commit"),FocusBinder->bPendingChatRefocus);
            FocusBinder->ApplyPendingChatFocus();
            TestEqual(TEXT("Blank Enter sends no chat packet"),Session.CachedC2SPackets.Num(),0);
            const int32 Before=Input->MovementKeys;
            Key(EKeys::W);
            if(StayInChat)
            {
                TestTrue(TEXT("Stay-in-chat preserves the source window after blank Enter"),Entry->HasKeyboardFocus());
                Type(TEXT("x"));
                TestTrue(TEXT("Typing still works after blank Enter"),Entry->GetText().ToString().EndsWith(TEXT("x")));
                TestEqual(TEXT("Chat retains keys when requested"),Input->MovementKeys,Before);
            }
            else
            {
                TestTrue(TEXT("Blank Enter restores actual game viewport focus"),Viewport->HasKeyboardFocus());
                TestEqual(TEXT("W reaches gameplay without a mouse click after blank Enter"),Input->MovementKeys,Before+1);
            }
        }
    }
    {
        constexpr int32 BlockedGuid=0x50000999;
        auto SetDB=[&](TArray<uint32> Words)
        {
            FACEBinaryWriter W;W.WriteUInt16(0);W.WriteUInt16(0); // account hash is server-side in ACE
            W.WriteUInt16(Words.IsEmpty()?0:1);W.WriteUInt16(0);
            if(!Words.IsEmpty())
            {
                W.WriteInt32(BlockedGuid);W.WriteInt32(Words.Num());
                for(uint32 Word:Words)W.WriteUInt32(Word);
                W.WriteString16L(TEXT("Blocked Player"));W.WriteUInt32(0);
            }
            W.WriteUInt32(0);W.WriteString16L(TEXT(""));W.WriteUInt32(0);
            FACEBinaryReader R(W.GetData());Session.HandleSetSquelchDB(R);
        };
        SetDB({1u<<ACEChatMessageType::Tell,1u<<ACEChatMessageType::Speech,0,0});
        TestTrue(TEXT("Squelch DB blocks the requested tell channel"),Session.IsSenderSquelched(BlockedGuid,TEXT("Changed display name"),ACEChatMessageType::Tell));
        TestFalse(TEXT("High mask words do not incorrectly squelch speech"),Session.IsSenderSquelched(BlockedGuid,TEXT("Blocked Player"),ACEChatMessageType::Speech));
        TestTrue(TEXT("Retail high channel bits are preserved"),Session.IsSenderSquelched(BlockedGuid,TEXT("Blocked Player"),32+ACEChatMessageType::Speech));
        TestFalse(TEXT("Partial squelch is not an all-channel menu check"),Session.Squelches[0].Blocks(1));
        SetDB({MAX_uint32,MAX_uint32,MAX_uint32,MAX_uint32});
        TestTrue(TEXT("All-channel squelch checks the menu"),Session.Squelches[0].Blocks(1));
        TestTrue(TEXT("Name-only channels use case-insensitive exact names"),Session.IsSenderSquelched(0,TEXT("blocked player"),ACEChatMessageType::Social));
        TestFalse(TEXT("A similarly named player is not blocked"),Session.IsSenderSquelched(0,TEXT("Blocked Player Two"),ACEChatMessageType::Tell));
        TestFalse(TEXT("Retail personal squelches preserve spellcasting text"),Session.IsSenderSquelched(BlockedGuid,TEXT("Blocked Player"),17));
        int32 Messages=0,Tells=0;
        const auto ChatHandle=Session.OnChatMessage.AddLambda([&](const FString&,const FString&,int32){++Messages;});
        const auto TellHandle=Session.OnPlayerTell.AddLambda([&](const FString&,const FString&,int32){++Tells;});
        auto Incoming=[&]()
        {
            FACEBinaryWriter Speech;Speech.WriteString16L(TEXT("hidden"));Speech.WriteString16L(TEXT("Blocked Player"));
            Speech.WriteInt32(BlockedGuid);Speech.WriteInt32(ACEChatMessageType::Speech);
            FACEBinaryReader SR(Speech.GetData());Session.HandleHearSpeech(SR);
            FACEBinaryWriter Ranged;Ranged.WriteString16L(TEXT("hidden"));Ranged.WriteString16L(TEXT("Blocked Player"));
            Ranged.WriteInt32(BlockedGuid);Ranged.WriteFloat(30);Ranged.WriteInt32(ACEChatMessageType::Speech);
            FACEBinaryReader RR(Ranged.GetData());Session.HandleHearRangedSpeech(RR);
            FACEBinaryWriter Emote;Emote.WriteInt32(BlockedGuid);Emote.WriteString16L(TEXT("Blocked Player"));Emote.WriteString16L(TEXT("waves."));
            FACEBinaryReader ER(Emote.GetData());Session.HandleSoulEmote(ER);
            FACEBinaryWriter Tell;Tell.WriteString16L(TEXT("hidden"));Tell.WriteString16L(TEXT("Blocked Player"));
            Tell.WriteInt32(BlockedGuid);Tell.WriteInt32(Session.PlayerGuid);Tell.WriteInt32(ACEChatMessageType::Tell);Tell.WriteUInt32(0);
            FACEBinaryReader TR(Tell.GetData());Session.HandleTell(TR);
            FACEBinaryWriter Channel;Channel.WriteUInt32(ACEChatChannel::Fellow);Channel.WriteString16L(TEXT("Blocked Player"));Channel.WriteString16L(TEXT("hidden"));
            FACEBinaryReader CR(Channel.GetData());Session.HandleChannelBroadcast(CR);
            FACEBinaryWriter Turbine;Turbine.WriteUInt32(0);Turbine.WriteUInt32(ACETurbineChat::BlobEventBinary);
            for(int32 I=0;I<7;++I)Turbine.WriteUInt32(0);
            Turbine.WriteUInt32(ACETurbineChat::General);Turbine.WritePackedUnicode(TEXT("Blocked Player"));Turbine.WritePackedUnicode(TEXT("hidden"));
            Turbine.WriteUInt32(12);Turbine.WriteInt32(BlockedGuid);Turbine.WriteUInt32(0);Turbine.WriteUInt32(ACETurbineChat::General);
            FACEBinaryReader TCR(Turbine.GetData());Session.HandleTurbineChat(TCR);
        };
        Incoming();TestEqual(TEXT("All incoming player chat paths suppress a squelched sender"),Messages,0);
        TestEqual(TEXT("Squelched tells cannot trigger plugin buff requests"),Tells,0);
        SetDB({});Incoming();
        TestEqual(TEXT("Server removal restores all six incoming chat paths"),Messages,6);
        TestEqual(TEXT("Unsquelched tells reach plugins again"),Tells,1);
        Session.OnChatMessage.Remove(ChatHandle);Session.OnPlayerTell.Remove(TellHandle);
    }
    Slate.ClearUserFocus(VirtualUser); Slate.ClearUserFocus(0); Slate.RequestDestroyWindow(Window);
    if (OldFocus) Slate.SetUserFocus(0,OldFocus);
    Client->Session.Reset(); Receiver->Close(); Sockets->DestroySocket(Receiver);
    AddInfo(TEXT("Compared with retail ChatInterface::ProcessCommand/SelectCommandFromHistory/HandleTextReplacements and ClientCommunicationSystem::DoTell/DoReply/DoReTell/OnChannelBroadcast. No live chat was sent."));
    return true;
}
#endif
