#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/ACERetailTextEntry.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEInputBindings.h"
#include "ACERuntimeOptions.h"
#include "ACECharacterOptions.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEVideoSettingsWidget.h"
#include "Components/ComboBoxString.h"
#include "Components/ScrollBox.h"
#include "Blueprint/WidgetTree.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UnrealClient.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Components/EditableTextBox.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPanelResizeSocialTest, "ACE.RetailParity.PanelResizeSocial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEPanelResizeSocialTest::RunTest(const FString&)
{
    const FString OriginalSettings=GGameUserSettingsIni;
    TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/PanelSocialPreferences.ini"));
    FConfigFile Preferences; Preferences.NoSave=true; GConfig->SetFile(GGameUserSettingsIni,&Preferences);
    auto* GI=NewObject<UGameInstance>();
    auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    const FString ArtDirectory=FPaths::ProjectSavedDir()/TEXT("Automation/PanelResizeSocial");
    IFileManager::Get().MakeDirectory(*ArtDirectory,true);
    auto* Resources=NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Manager=NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto* Layout=NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat,Manager);
    if (!Layout->LoadLayout(0x21000005)) return false;
    auto* Canvas=NewObject<UACEUICanvasWidget>(); Canvas->Initialize();
    Canvas->InitializeCanvas(Manager); Canvas->SetResourceResolver(Resources);
    const auto Slate=Canvas->TakeWidget();
    auto* Client=NewObject<UACEClientSubsystem>(GI); Client->Session=MakeShared<FACESession>();
    auto& Session=*Client->Session; Session.State=EACESessionState::InWorld; Session.PlayerGuid=1234;
    FACEWorldObject Self; Self.Guid=1234; Self.Name=TEXT("UI regression"); Session.WorldObjects.Add(Self.Guid,Self);
    auto* Binder=NewObject<UACEUIGameplayBinder>(); Binder->Initialize(Client,Manager,Canvas,nullptr);
    Canvas->SetGameplayBinder(Binder);
    ON_SCOPE_EXIT { Binder->Shutdown(); Canvas->SetGameplayBinder(nullptr); Manager->Shutdown(); Dat->Deinitialize(); TGuardValue<FString> Restore(GGameUserSettingsIni,OriginalSettings); ACEInputBindings::Reload(); };
    FWidgetRenderer Renderer(true,true);
    auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1600,900),TF_Bilinear,true);
    auto Draw=[&](const TCHAR* Name)
    {
        for (int32 Pass=0;Pass<4;++Pass)
        {
            Canvas->NativeTick(Canvas->GetCachedGeometry(),0.f);
            // All offscreen draws run in one engine frame. NativeTick deduplicates
            // gameplay refresh by GFrameCounter, so refresh after each resize too.
            Binder->TickRefresh();
            Renderer.DrawWidget(Target,Slate,FVector2D(1600,900),0.f); FlushRenderingCommands();
        }
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(1600,900,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/PanelResizeSocial")/(FString(Name)+TEXT(".png"))));
    };
    Manager->ApplyEdgeAnchoredLayout(1600,900);

    ISocketSubsystem* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Address=Sockets->CreateInternetAddr(); bool Valid=false; Address->SetIp(TEXT("127.0.0.1"),Valid); Address->SetPort(0);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Social test receiver"),false);
    if (!Receiver || !Receiver->Bind(*Address)) return false;
    Receiver->GetAddress(*Address);
    Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Social test sender"),false); Session.ServerC2SAddr=Address; Session.IssacClient=MakeUnique<FACEIsaac>(123u);
    Session.OnSelectionChanged.AddLambda([Client](const FACESelectedObject& Selection){Client->OnSelectionChanged.Broadcast(Selection);});
    ON_SCOPE_EXIT { Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S=nullptr; Receiver->Close(); Sockets->DestroySocket(Receiver); };
    auto HasAction=[&](uint32 Opcode,uint32 Payload)
    {
        for (const auto& P:Session.CachedC2SPackets)
        { FACEBinaryReader R(P.Value.Payload); R.Skip(24); if (R.ReadUInt32()==Opcode && R.ReadUInt32()==Payload) return true; }
        return false;
    };
    auto ClickPoint=[&](FVector2D LayoutPoint)
    {
        const auto& G=Canvas->GetCachedGeometry();
        const FVector2D P=G.LocalToAbsolute(Canvas->LayoutToViewport(LayoutPoint));
        const FPointerEvent Down(0,P,P,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
        const FPointerEvent Up(0,P,P,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
        Canvas->NativeOnMouseButtonDown(G,Down); Canvas->NativeOnMouseButtonUp(G,Up);
    };
    auto Click=[&](const TCHAR* Scope,const TCHAR* Name)
    {
        const auto El=Manager->FindElementUnder(Scope,Name);
        if (!TestTrue(FString::Printf(TEXT("Control exists: %s"),Name),El.IsValid())) return;
        ClickPoint(FVector2D(El->GetScreenOrigin())+FVector2D(El->Width,El->Height)*.5);
    };
    // Decode the same full-update record emitted by ACE and packed by retail.
    FACEBinaryWriter W; W.WriteUInt16(2); W.WriteUInt16(16);
    for (uint32 Guid:{1234u,5678u})
    {
        W.WriteUInt32(Guid); W.WriteUInt32(0); W.WriteUInt32(0); W.WriteUInt32(100);
        for (uint32 Vital:{300u,250u,200u,150u,125u,100u}) W.WriteUInt32(Vital);
        W.WriteUInt32(16); W.WriteString16L(Guid==1234 ? TEXT("Leader") : TEXT("Fellow"));
    }
    W.WriteString16L(TEXT("Panel regression")); W.WriteUInt32(1234);
    for (uint32 Flag:{1u,1u,0u,0u}) W.WriteUInt32(Flag);
    W.WriteUInt16(0);W.WriteUInt16(16);W.WriteUInt16(0);W.WriteUInt16(16);
    FACEBinaryReader Read(W.GetData());Session.HandleFellowshipFullUpdate(Read);
    TestEqual(TEXT("Decoded leader matches local player"),Session.Fellowship.LeaderGuid,1234);
    TestEqual(TEXT("Decoded fellowship includes both members"),Session.Fellowship.Members.Num(),2);
    for(const auto& Member:Session.Fellowship.Members)TestTrue(TEXT("Fellow vitals carry a fresh receive timestamp"),Member.VitalsReceivedAt>0 && FPlatformTime::Seconds()-Member.VitalsReceivedAt<1);
    FACEWorldObject Other;Other.Guid=5678;Other.Name=TEXT("Fellow");Other.bIsPlayer=true;Session.WorldObjects.Add(Other.Guid,Other);
    Other.Guid=9999;Other.Name=TEXT("Recruit");Session.WorldObjects.Add(Other.Guid,Other);
    // Decode titled/online members from the actual allegiance wire records.
    FACEBinaryWriter AllegianceWire;
    AllegianceWire.WriteUInt32(3); AllegianceWire.WriteUInt32(100); AllegianceWire.WriteUInt32(23);
    AllegianceWire.WriteUInt16(19); AllegianceWire.WriteUInt16(11);
    AllegianceWire.WriteUInt16(0); AllegianceWire.WriteUInt16(16); // officers
    AllegianceWire.WriteInt32(0); // officer titles
    for (int32 I=0; I<4; ++I) AllegianceWire.WriteUInt32(0);
    AllegianceWire.WriteString16L(TEXT("")); AllegianceWire.WriteString16L(TEXT(""));
    AllegianceWire.WriteUInt32(0); for (int32 I=0; I<8; ++I) AllegianceWire.WriteUInt32(0);
    AllegianceWire.WriteString16L(TEXT("The Test Allegiance"));
    for (int32 I=0; I<3; ++I) AllegianceWire.WriteUInt32(0);
    auto Member = [&](uint32 Guid, const TCHAR* Name, uint16 Rank, uint8 Gender, uint8 Heritage, bool Online)
    {
        AllegianceWire.WriteUInt32(Guid); AllegianceWire.WriteUInt32(12); AllegianceWire.WriteUInt32(1234567);
        AllegianceWire.WriteUInt32(8 | (Online ? 1 : 0));
        AllegianceWire.WriteUInt8(Gender); AllegianceWire.WriteUInt8(Heritage); AllegianceWire.WriteUInt16(Rank);
        AllegianceWire.WriteUInt32(100); AllegianceWire.WriteUInt16(150); AllegianceWire.WriteUInt16(200);
        AllegianceWire.WriteUInt64(0); AllegianceWire.WriteString16L(Name);
    };
    Member(7777,TEXT("Monarch"),9,2,1,true);
    AllegianceWire.WriteUInt32(7777); Member(8888,TEXT("Patron"),5,1,3,false);
    AllegianceWire.WriteUInt32(8888); Member(1234,TEXT("UI regression"),3,1,1,true);
    for (int32 I=0; I<16; ++I)
    { AllegianceWire.WriteUInt32(1234); Member(20000+I,*FString::Printf(TEXT("Vassal %d"),I+1),1,1,1,I%2==0); }
    FACEBinaryReader AR(AllegianceWire.GetData()); Session.HandleAllegianceUpdate(AR);
    auto& A=Session.Allegiance;
    TestEqual(TEXT("Wire preserves monarch title gender"),A.Monarch.Gender,2);
    TestEqual(TEXT("Wire preserves patron heritage"),A.Patron.HeritageGroup,3);
    TestEqual(TEXT("Wire preserves self rank"),A.Self.Rank,3);
    TestEqual(TEXT("Wire resolves all direct vassals"),A.Vassals.Num(),16);
    const auto Panel=Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field"));
    TestEqual(TEXT("Native panel minimum height"),Panel->MinHeight,372);
    struct FPage { const TCHAR* Panel; const TCHAR* Tab; const TCHAR* Footer; };
    const FPage Pages[]={
        {TEXT("SocialPanel_Field"),TEXT("AllegiancePage"),TEXT("BreakButton")},
        {TEXT("SocialPanel_Field"),TEXT("FellowshipPage"),TEXT("FellowDisbandButton")},
        {TEXT("SkillManagementPanel_Field"),TEXT("AttributePage"),TEXT("StatManagement_Footer_Default")},
        {TEXT("QuestManagementPanel_Field"),TEXT("JournalPage"),TEXT("LastPageButton")},
        {TEXT("WorldPanel_Field"),TEXT("MapPage"),TEXT("BookPaper_Bottom")},
        {TEXT("OptionsPanel_Field"),TEXT("CharacterSettingsPage"),TEXT("ApplyButton")},
        {TEXT("OptionsPanel_Field"),TEXT("ChatPage"),TEXT("ApplyButton")},
        {TEXT("OptionsPanel_Field"),TEXT("ConfigPage"),TEXT("ApplyButton")},
    };
    for (int32 H:{372,680,410,800,372})
    {
        Panel->UserResizeH=H-Panel->AuthoredHeight; UACEUIElementManager::ApplyFloatyResizeLayout(Panel);
        for (const auto& P:Pages)
        {
            Binder->ShowPanelPage(P.Panel);
            const FString Tab=P.Tab;
            if (Tab==TEXT("AllegiancePage") || Tab==TEXT("FellowshipPage")) Binder->SyncSocialPanelTab(Tab);
            else if (Tab==TEXT("AttributePage")) Binder->SyncSkillPanelTab(Tab);
            else if (Tab==TEXT("JournalPage")) Binder->ActiveQuestTab=Tab;
            else if (FString(P.Panel)==TEXT("OptionsPanel_Field")) Binder->SyncOptionsPanelTab(Tab);
            Draw(*(Tab+FString::FromInt(H)));
            const auto Footer=Manager->FindElementUnder(P.Tab,P.Footer);
            if (!TestTrue(TEXT("Native footer exists"),Footer.IsValid())) continue;
            const int32 Top=Footer->GetScreenOrigin().Y, Bottom=Top+Footer->Height;
            const int32 PanelTop=Panel->GetScreenOrigin().Y;
            TestTrue(Tab+TEXT(" footer remains inside resized panel"),Top>=PanelTop && Bottom<=PanelTop+Panel->Height-5);
            AddInfo(FString::Printf(TEXT("%s H=%d footer=%d..%d panel=%d..%d"),P.Tab,H,Top,Bottom,PanelTop,PanelTop+Panel->Height));
            if (Tab==TEXT("FellowshipPage"))
            {
                Client->SelectObject(9999);Binder->RefreshFellowshipOverlays();
                for (const auto& Action:TArray<TPair<const TCHAR*,uint32>>{{TEXT("FellowOpenButton"),ACEGameAction::FellowshipChangeOpenness},{TEXT("FellowRecruitButton"),ACEGameAction::FellowshipRecruit},{TEXT("FellowDisbandButton"),ACEGameAction::FellowshipQuit}})
                {
                    Session.CachedC2SPackets.Reset(); Click(P.Tab,Action.Key);
                    TestTrue(FString::Printf(TEXT("Visible %s click emits action at height %d"),Action.Key,H),HasAction(Action.Value,Action.Value==ACEGameAction::FellowshipRecruit ? 9999 : 1));
                }
            }
        }
    }
    Panel->UserResizeH=80-Panel->AuthoredHeight;UACEUIElementManager::ApplyFloatyResizeLayout(Panel);
    TestEqual(TEXT("Retail minimum prevents inaccessible controls"),Panel->Height,372);
    Binder->ShowPanelPage(TEXT("SocialPanel_Field"));Binder->SyncSocialPanelTab(TEXT("FellowshipPage"));Draw(TEXT("FellowshipActions"));
    const auto Row=Binder->FellowRowElements[1];ClickPoint(FVector2D(Row->GetScreenOrigin())+FVector2D(15,10));
    TestEqual(TEXT("Second member row selects second member, not first"),Binder->SelectedFellowGuid,5678);
    for (const auto& Action:TArray<TPair<const TCHAR*,uint32>>{{TEXT("FellowLeaderButton"),ACEGameAction::FellowshipAssignNewLeader},{TEXT("FellowDismissButton"),ACEGameAction::FellowshipDismiss}})
    { Session.CachedC2SPackets.Reset();Click(TEXT("FellowshipPage"),Action.Key);TestTrue(TEXT("Selected member action uses selected GUID"),HasAction(Action.Value,5678)); }
    Session.Fellowship.LeaderGuid=5678;Binder->RefreshFellowshipOverlays();
    Session.CachedC2SPackets.Reset();Click(TEXT("FellowshipPage"),TEXT("FellowDisbandButton"));TestFalse(TEXT("Nonleader cannot disband"),HasAction(ACEGameAction::FellowshipQuit,1));
    Session.CachedC2SPackets.Reset();Click(TEXT("FellowshipPage"),TEXT("FellowQuitButton"));TestTrue(TEXT("Member can leave"),HasAction(ACEGameAction::FellowshipQuit,0));
    Session.Fellowship=FACEFellowshipInfo();Binder->RefreshFellowshipOverlays();Binder->RefreshSocialButtonLabels();
    // Programmatic SetText does not emit the user-edit event in UE 5.8.
    // Simulate that event before exercising the actual Create button click.
    Draw(TEXT("CreateFellowshipEmpty"));
    const FText NewFellowshipName=FText::FromString(TEXT("New fellowship"));
    Binder->FellowshipNameEntry->SetText(NewFellowshipName);
    Binder->FellowshipNameEntry->OnTextChanged.Broadcast(NewFellowshipName);Draw(TEXT("CreateFellowship"));
    const uint32 Before=Session.NextGameActionSequence;Click(TEXT("FellowshipPage"),TEXT("CreateFellowshipButton"));TestEqual(TEXT("Actual Create click sends one action"),int32(Session.NextGameActionSequence-Before),1);
    Binder->SyncSocialPanelTab(TEXT("AllegiancePage"));Draw(TEXT("AllegianceActions"));
    const auto VList=Manager->FindElementUnder(TEXT("AllegiancePage"),TEXT("VassalsListBox"));
    ClickPoint(FVector2D(VList->GetScreenOrigin())+FVector2D(VList->Width-10,48));TestEqual(TEXT("Second vassal row can be selected"),Binder->SelectedVassalGuid,20001);
    Draw(TEXT("AllegianceSelected"));
    TestEqual(TEXT("Vassal row uses retail two-line height"),Binder->VassalRowElements[0]->Height,32);
    TestEqual(TEXT("Full row including XP selects retail state"),Binder->VassalRowElements[1]->DefaultState,6u);
    TestEqual(TEXT("Vassal name includes allegiance rank title"),Binder->VassalRows[0]->GetText().ToString(),FString(TEXT("Yeoman Vassal 1")));
    TestEqual(TEXT("Monarch uses gender-specific rank title"),Binder->MonarchNameLabel->GetText().ToString(),FString(TEXT("Queen Monarch")));
    TestEqual(TEXT("Patron uses heritage-specific rank title"),Binder->PatronNameLabel->GetText().ToString(),FString(TEXT("Ta-chueh Patron")));
    TestEqual(TEXT("Header is allegiance name, not local player's name and level"),Binder->AllegianceNameLabel->GetText().ToString(),FString(TEXT("The Test Allegiance")));
    TestTrue(TEXT("Offline vassal has second-line status"),Binder->VassalStatusRows[1]->GetVisibility()!=ESlateVisibility::Collapsed);
    TestTrue(TEXT("Online vassal hides offline status"),Binder->VassalStatusRows[0]->GetVisibility()==ESlateVisibility::Collapsed);
    TestFalse(TEXT("Indirect monarch hides XP tithed to patron"),bool(Manager->FindElementUnder(TEXT("MonarchField"),TEXT("XPProducedFrame"))->bVisible));
    TestEqual(TEXT("Player followers counts descendants, not all allegiance members"),Binder->AllegianceCaptionLabels[1]->GetText().ToString(),FString(TEXT("Followers: 23")));
    TestEqual(TEXT("Monarch followers excludes monarch"),Binder->AllegianceCaptionLabels[2]->GetText().ToString(),FString(TEXT("Followers: 99")));
    TestEqual(TEXT("Vassal XP retains numeric value"),Binder->VassalXPRows[0]->GetText().ToString(),FString(TEXT("1,234,567")));
    TestEqual(TEXT("Rank is titled, numbered, and has no markup escapes"),Binder->AllegianceCaptionLabels[0]->GetText().ToString(),FString(TEXT("Rank: Baron [3]")));
    FACEBinaryWriter Login; Login.WriteUInt32(8888);Login.WriteUInt32(1);
    FACEBinaryReader LR(Login.GetData());Session.HandleAllegianceLoginNotification(LR);
    Binder->RefreshAllegianceOverlays();
    TestEqual(TEXT("Login notification updates patron immediately"),Manager->FindElementUnder(TEXT("AllegiancePage"),TEXT("PatronField"))->DefaultState,1u);
    FACEPlayerVitals RankVitals; RankVitals.StatQualityInts.Add(30,3);
    FACEActiveEnchantment RankBuff; RankBuff.StatModType=0x8004;RankBuff.StatModKey=30;RankBuff.StatModValue=2;RankBuff.SpellCategory=123;RankBuff.PowerLevel=100;
    FACEActiveEnchantment WeakRank=RankBuff;WeakRank.PowerLevel=50;WeakRank.StatModValue=1;
    Dat->RecomputePlayerStats(RankVitals,{RankBuff,WeakRank});
    TestEqual(TEXT("Rank enchantments only apply winning category"),RankVitals.EffectiveAllegianceRank,5);
    Session.PlayerVitals=RankVitals; Binder->RefreshAllegianceOverlays(); Draw(TEXT("AllegianceBuffedRank"));
    TestEqual(TEXT("Buffed rank includes delta"),Binder->AllegianceCaptionLabels[0]->GetText().ToString(),FString(TEXT("Rank: Baron [5 (+2)]")));
    TestEqual(TEXT("Buffed rank uses retail green text state"),Manager->FindElementUnder(TEXT("AllegiancePage"),TEXT("PlayerRank"))->DefaultState,0x10000014u);
    Session.PlayerVitals.EffectiveAllegianceRank=-1;Session.PlayerVitals.StatQualityInts.Remove(30);
    Session.CachedC2SPackets.Reset();Click(TEXT("AllegiancePage"),TEXT("KickButton"));
    TestEqual(TEXT("Kick opens confirmation for chosen vassal"),Binder->PendingAllegianceGuid,20001);TestTrue(TEXT("No destructive action before confirmation"),Session.CachedC2SPackets.IsEmpty());
    Binder->FinishServerConfirmation(false);TestTrue(TEXT("Cancel sends no action"),Session.CachedC2SPackets.IsEmpty());
    Click(TEXT("AllegiancePage"),TEXT("KickButton"));Draw(TEXT("AllegianceConfirm"));
    Click(TEXT("RootGameplay_FloatyServerConfirmation_Field"),TEXT("ServerConfirmationYes"));TestTrue(TEXT("Confirm kicks captured vassal"),HasAction(ACEGameAction::BreakAllegiance,20001));
    Binder->VassalScrollOffset=100;Binder->RefreshAllegianceOverlays();Draw(TEXT("AllegianceScrolled"));
    TestEqual(TEXT("Last vassal reachable"),Binder->VassalRowGuids[Binder->VassalVisibleRows-1],20015);
    Session.CachedC2SPackets.Reset();Click(TEXT("AllegiancePage"),TEXT("BreakButton"));Binder->FinishServerConfirmation(true);TestTrue(TEXT("Break targets patron"),HasAction(ACEGameAction::BreakAllegiance,8888));
    A.PatronGuid=0;Binder->RefreshAllegianceOverlays();TestFalse(TEXT("Monarch alone does not enable Break"),Binder->CanActivateAllegianceControl(TEXT("BreakButton")));
    Client->SelectObject(9999);Binder->RefreshAllegianceOverlays();Session.CachedC2SPackets.Reset();Click(TEXT("AllegiancePage"),TEXT("SwearButton"));
    TestEqual(TEXT("Swear needs confirmation"),Binder->PendingAllegianceGuid,9999);Client->SelectObject(5678);Binder->FinishServerConfirmation(true);
    TestTrue(TEXT("Confirmation retains original patron despite selection change"),HasAction(ACEGameAction::SwearAllegiance,9999));
    A.PatronGuid=A.MonarchGuid; A.Patron=A.Monarch;A.PatronName=A.MonarchName;
    Binder->RefreshAllegianceOverlays(); Binder->RefreshSocialButtonLabels();Draw(TEXT("AllegianceDirectMonarch"));
    TestFalse(TEXT("Direct monarch does not duplicate patron field"),bool(Manager->FindElementUnder(TEXT("AllegiancePage"),TEXT("PatronField"))->bVisible));
    TestTrue(TEXT("Direct monarch shows tithed XP"),bool(Manager->FindElementUnder(TEXT("MonarchField"),TEXT("XPProducedFrame"))->bVisible));
    A.MonarchGuid=Session.PlayerGuid;A.PatronGuid=0;
    Binder->RefreshAllegianceOverlays();Binder->RefreshSocialButtonLabels();Draw(TEXT("AllegianceIsMonarch"));
    TestFalse(TEXT("Monarch does not show self as upstream monarch"),bool(Manager->FindElementUnder(TEXT("AllegiancePage"),TEXT("MonarchField"))->bVisible));
    A=FACEAllegianceInfo();Binder->RefreshAllegianceOverlays();Binder->RefreshSocialButtonLabels();Draw(TEXT("AllegianceNone"));
    TestEqual(TEXT("No allegiance leaves header blank like retail"),Binder->AllegianceNameLabel->GetText().ToString(),FString());
    TestFalse(TEXT("No allegiance disables booting a stale vassal"),Binder->CanActivateAllegianceControl(TEXT("KickButton")));
    Session.CachedC2SPackets.Reset();Client->SetPluginFellowshipUpdates(true);
    TestTrue(TEXT("Recovery independently subscribes to fellowship vitals"),HasAction(ACEGameAction::FellowshipUpdateRequest,1));
    Session.CachedC2SPackets.Reset();Client->SendFellowshipUpdateRequest(false);
    TestTrue(TEXT("Closing desktop panel preserves recovery updates"),HasAction(ACEGameAction::FellowshipUpdateRequest,1));
    Client->SetVRFellowshipUpdates(true);Session.CachedC2SPackets.Reset();Client->SetPluginFellowshipUpdates(false);
    TestTrue(TEXT("Stopping recovery preserves VR panel updates"),HasAction(ACEGameAction::FellowshipUpdateRequest,1));
    Session.CachedC2SPackets.Reset();Client->SetVRFellowshipUpdates(false);
    TestTrue(TEXT("Last subscriber releases server updates"),HasAction(ACEGameAction::FellowshipUpdateRequest,0));
    Session.Friends.Reset();
    auto FriendUpdate=[&](uint32 Kind,bool Online)
    {
        FACEBinaryWriter W;W.WriteUInt32(1);W.WriteUInt32(70099);W.WriteUInt32(Online?1:0);W.WriteUInt32(0);
        W.WriteString16L(TEXT("Wire Friend"));W.WriteUInt32(0);W.WriteUInt32(0);W.WriteUInt32(Kind);
        FACEBinaryReader R(W.GetData());Session.HandleFriendsListUpdate(R);
    };
    FriendUpdate(0,true);TestEqual(TEXT("Friends full update restores the list"),Session.Friends.Num(),1);
    FriendUpdate(4,false);TestFalse(TEXT("Friend presence update changes online state"),Session.Friends[0].bOnline);
    FriendUpdate(3,false);TestEqual(TEXT("Retail silent removal removes rather than re-adds a friend"),Session.Friends.Num(),0);
    FriendUpdate(1,true);TestEqual(TEXT("Friend-added acknowledgement adds one row"),Session.Friends.Num(),1);
    FriendUpdate(2,false);TestEqual(TEXT("Friend-removed acknowledgement removes the row"),Session.Friends.Num(),0);
    for (int32 I=0;I<30;++I)
    {
        FACEFriendInfo F;F.Guid=70000+I;F.Name=FString::Printf(TEXT("Friend %02d"),I);F.bOnline=(I%2)==1;
        Session.Friends.Add(F);
    }
    Binder->SyncSocialPanelTab(TEXT("FriendsPage"));Binder->RefreshFriendsOverlays();Draw(TEXT("FriendsRetail"));
    TestEqual(TEXT("Friends sorts online before offline"),Binder->FriendRowGuids[0],70001);
    TestEqual(TEXT("Friend name excludes invented status suffix"),Binder->FriendRows[0]->GetText().ToString(),FString(TEXT("Friend 01")));
    TestEqual(TEXT("Friends row uses authored 24 pixel height"),Binder->FriendRowElements[0]->Height,24);
    const auto FriendRow=Binder->FriendRowElements[0];
    ClickPoint(FVector2D(FriendRow->GetScreenOrigin())+FVector2D(FriendRow->Width-15,12));
    TestEqual(TEXT("Clicking the friend status column selects the entire row"),Binder->SelectedFriendGuid,70001);
    Binder->RefreshFriendsOverlays();Draw(TEXT("FriendsSelected"));
    TestEqual(TEXT("Selected friend has retail selection state"),Binder->FriendRowElements[0]->DefaultState,6u);
    Binder->FriendScrollOffset=100;Binder->RefreshFriendsOverlays();Draw(TEXT("FriendsScrolled"));
    TestEqual(TEXT("All friends remain reachable beyond the first page"),Binder->FriendRowGuids[Binder->FriendVisibleRows-1],70028);
    Binder->SelectedFriendGuid=70001;Binder->RefreshFriendsOverlays();
    Click(TEXT("FriendsPage"),TEXT("TellButton"));
    TestEqual(TEXT("Friend Tell preserves a multiword recipient"),Binder->ChatEntry->GetText().ToString(),FString(TEXT("/tell Friend 01, ")));
    Session.CachedC2SPackets.Reset();Click(TEXT("FriendsPage"),TEXT("RemoveButton"));
    TestTrue(TEXT("Remove friend sends the selected GUID"),HasAction(ACEGameAction::RemoveFriend,70001));
    Binder->SelectedFriendGuid=70000;Binder->RefreshFriendsOverlays();
    TestTrue(TEXT("Offline friend Tell is disabled like retail"),Manager->FindElementUnder(TEXT("FriendsPage"),TEXT("TellButton"))->bGhosted);
    Binder->FriendNameEntry->SetText(FText::FromString(TEXT("New Friend")));Session.CachedC2SPackets.Reset();
    Click(TEXT("FriendsPage"),TEXT("AddButton"));
    bool AddedFriend=false;
    for(const auto& P:Session.CachedC2SPackets)
    {
        FACEBinaryReader R(P.Value.Payload);R.Skip(24);if(R.ReadUInt32()!=ACEGameAction::AddFriend)continue;
        AddedFriend=R.ReadString16L()==TEXT("New Friend");
    }
    TestTrue(TEXT("Add friend sends the entered multiword name"),AddedFriend);
    Session.CachedC2SPackets.Reset();Click(TEXT("FriendsPage"),TEXT("AppearOffline_Checkbox"));
    TestTrue(TEXT("Appear offline sends the retail character option"),HasAction(ACEGameAction::SetSingleCharacterOption,0x27));
    Session.Squelches.Reset();
    for(int32 I=49;I>=0;--I)
    {
        FACESquelchEntry E;E.Guid=80000+I;E.Name=FString::Printf(TEXT("Ignored %02d"),I);E.Mask=-1;E.bAccount=I%2==1;
        Session.Squelches.Add(E);
    }
    Binder->SyncSocialPanelTab(TEXT("SquelchPage"));Binder->RefreshSquelchOverlays();Draw(TEXT("SquelchRetail"));
    TestEqual(TEXT("Squelches use retail alphabetical order"),Binder->SquelchRowGuids[0],80000);
    TestEqual(TEXT("Squelch names have no invented suffix"),Binder->SquelchRows[1]->GetText().ToString(),FString(TEXT("Ignored 01")));
    TestEqual(TEXT("Squelch rows use retail template height"),Binder->SquelchRowElements[0]->Height,24);
    TestEqual(TEXT("Character status uses the DAT string"),Binder->SquelchStatusRows[0]->GetText().ToString(),Binder->SocialStrings.Strings.FindRef(0x0C3DF83C));
    TestEqual(TEXT("Account status uses the DAT string"),Binder->SquelchStatusRows[1]->GetText().ToString(),Binder->SocialStrings.Strings.FindRef(0x0CD7089C));
    TestTrue(TEXT("Retail squelch capacity disables adding entries"),Manager->FindElementUnder(TEXT("SquelchPage"),TEXT("SquelchCharacterButton"))->bGhosted);
    Binder->SquelchScrollOffset=100;Binder->RefreshSquelchOverlays();Draw(TEXT("SquelchScrolled"));
    const int32 Last=Binder->SquelchVisibleRows-1;
    TestEqual(TEXT("Last squelch remains reachable beyond row 24"),Binder->SquelchRowGuids[Last],80049);
    const auto SquelchRow=Binder->SquelchRowElements[Last];
    ClickPoint(FVector2D(SquelchRow->GetScreenOrigin())+FVector2D(SquelchRow->Width-15,12));
    TestEqual(TEXT("Clicking the account column selects the whole squelch row"),Binder->SelectedSquelchGuid,80049);
    Draw(TEXT("SquelchSelected"));
    TestEqual(TEXT("Squelch selection uses retail highlight"),SquelchRow->DefaultState,6u);
    Session.CachedC2SPackets.Reset();Click(TEXT("SquelchPage"),TEXT("SquelchRemoveButton"));
    TestTrue(TEXT("Account removal sends unsquelch rather than character removal"),HasAction(ACEGameAction::ModifyAccountSquelch,0));
    Binder->SelectedSquelchGuid=80048;Binder->SelectedSquelchName=TEXT("Ignored 48");Binder->RefreshSquelchOverlays();
    Session.CachedC2SPackets.Reset();Click(TEXT("SquelchPage"),TEXT("SquelchRemoveButton"));
    TestTrue(TEXT("Character removal sends a character unsquelch"),HasAction(ACEGameAction::ModifyCharacterSquelch,0));
    Session.Squelches.Reset();Binder->RefreshSquelchOverlays();Draw(TEXT("SquelchEmpty"));
    for(const auto& SquelchText:Binder->SquelchRows)TestTrue(TEXT("Clearing squelches hides old rows"),SquelchText->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("No selection disables Remove"),Manager->FindElementUnder(TEXT("SquelchPage"),TEXT("SquelchRemoveButton"))->bGhosted);
    Binder->SquelchNameEntry->SetText(FText::FromString(TEXT("New Player")));Session.CachedC2SPackets.Reset();
    Click(TEXT("SquelchPage"),TEXT("SquelchCharacterButton"));
    TestTrue(TEXT("Character button sends an enabled squelch"),HasAction(ACEGameAction::ModifyCharacterSquelch,1));
    for(const auto& P:Session.CachedC2SPackets)
    {
        FACEBinaryReader R(P.Value.Payload);R.Skip(24);if(R.ReadUInt32()!=ACEGameAction::ModifyCharacterSquelch)continue;
        R.ReadUInt32();TestEqual(TEXT("Typed-name squelch has no selected world GUID"),R.ReadUInt32(),0u);
        TestEqual(TEXT("Squelch request retains full name"),R.ReadString16L(),FString(TEXT("New Player")));
        TestEqual(TEXT("Squelch UI requests retail AllChannels type"),R.ReadUInt32(),1u);
    }
    return true;
}
#endif
