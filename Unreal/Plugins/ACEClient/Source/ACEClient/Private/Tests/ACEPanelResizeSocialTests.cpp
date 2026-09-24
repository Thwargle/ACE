#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
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
    FACEWorldObject Other;Other.Guid=5678;Other.Name=TEXT("Fellow");Other.bIsPlayer=true;Session.WorldObjects.Add(Other.Guid,Other);
    Other.Guid=9999;Other.Name=TEXT("Recruit");Session.WorldObjects.Add(Other.Guid,Other);
    auto& A=Session.Allegiance; A.bValid=true; A.PatronGuid=8888; A.PatronName=TEXT("Patron"); A.MonarchGuid=7777;A.MonarchName=TEXT("Monarch");
    for (int32 I=0;I<16;++I) { FACEAllegianceMember V;V.Guid=20000+I;V.Name=FString::Printf(TEXT("Vassal %d"),I+1);V.Level=100;V.bOnline=true;A.Vassals.Add(V); }
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
    Binder->FellowshipNameEntry->SetText(FText::FromString(TEXT("New fellowship")));Draw(TEXT("CreateFellowship"));
    const uint32 Before=Session.NextGameActionSequence;Click(TEXT("FellowshipPage"),TEXT("CreateFellowshipButton"));TestEqual(TEXT("Actual Create click sends one action"),Session.NextGameActionSequence,Before+1);
    Binder->SyncSocialPanelTab(TEXT("AllegiancePage"));Draw(TEXT("AllegianceActions"));
    const auto VList=Manager->FindElementUnder(TEXT("AllegiancePage"),TEXT("VassalsListBox"));
    ClickPoint(FVector2D(VList->GetScreenOrigin())+FVector2D(20,27));TestEqual(TEXT("Second vassal row can be selected"),Binder->SelectedVassalGuid,20001);
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
    return true;
}
#endif
