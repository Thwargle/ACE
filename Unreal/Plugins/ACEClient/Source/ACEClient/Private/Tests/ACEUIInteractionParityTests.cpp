#include "VR/ACEVRMenu.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACETestDatDirectory.h"
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
#include "ACEScreenshotSettings.h"
#include "ACEDesktopDisplay.h"
#include "GameFramework/GameUserSettings.h"
#include "ACESpellTargeting.h"
#include "Protocol/ACECombatChat.h"
#include "ACERetailChat.h"
#include "HAL/PlatformProcess.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "ACECharacterOptions.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEVideoSettingsWidget.h"
#include "Components/ComboBoxString.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUIInteractionParityTest, "ACE.RetailParity.UIInteractions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUIInteractionParityTest::RunTest(const FString&)
{
    // Offscreen passes represent separate frames; the HUD now coalesces same-frame callbacks.
    TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
    const FString OriginalSettings=GGameUserSettingsIni;
    TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/InteractionPreferences.ini"));
    FConfigFile Preferences; Preferences.NoSave=true; GConfig->SetFile(GGameUserSettingsIni,&Preferences);
    auto* GI=NewObject<UGameInstance>();
    GI->Init();
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat->LoadDatDirectory(ACETestDatDirectory())) return false;
    const FString ArtDirectory=FPaths::ProjectSavedDir()/TEXT("Automation/UIInteractions");
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
    FACEWorldObject Self; Self.Guid=1234; Self.Name=TEXT("UI regression"); Self.bIsPlayer=true; Self.ItemType=ACEItemType::Creature; Session.WorldObjects.Add(Self.Guid,Self);
    Session.OnRoutedChatMessage.AddLambda([Client](const FString& Text,const FString& Sender,int32 Type,int32 FilterType){Client->ReceiveChatMessage(Text,Sender,Type,FilterType);});
    Session.OnSpellLearned.AddLambda([Client](int32 Id){Client->OnSpellLearned.Broadcast(Id);});
    FACEBinaryWriter Welcome;Welcome.WriteString16L(TEXT("Welcome to Asheron's Call\n powered by ACEmulator"));Welcome.WriteInt32(ACEChatMessageType::Broadcast);
    FACEBinaryReader WelcomeReader(Welcome.GetData());Session.HandleServerMessage(WelcomeReader);
    FACEBinaryWriter Joined;Joined.WriteUInt32(0x051B);Joined.WriteString16L(TEXT("General"));
    FACEBinaryReader JoinedReader(Joined.GetData());Session.HandleWeenieErrorWithString(JoinedReader);
    auto* Binder=NewObject<UACEUIGameplayBinder>(); Binder->Initialize(Client,Manager,Canvas,nullptr);
    Canvas->SetGameplayBinder(Binder);
    TestTrue(TEXT("Login messages received before HUD creation are replayed"),Binder->ChatDisplayLines[0].ContainsByPredicate([](const auto& Line){return Line.Text.Contains(TEXT("Welcome to Asheron's Call"));}));
    TestTrue(TEXT("Early channel notices survive HUD creation"),Binder->ChatDisplayLines[0].ContainsByPredicate([](const auto& Line){return Line.Text.Contains(TEXT("entered the General"));}));
    const int32 LoginLines=Binder->ChatDisplayLines[0].Num();
    Client->ReceiveChatMessage(TEXT("Live message"),FString(),ACEChatMessageType::System);
    TestEqual(TEXT("Live messages are delivered once"),Binder->ChatDisplayLines[0].Num(),LoginLines+1);
    for (auto Preferred:{EWindowMode::Fullscreen,EWindowMode::WindowedFullscreen})
    {
        auto Mode=Preferred;
        for(int32 I=0;I<12;++I)
        {
            Mode=FACEDesktopDisplay::ToggleMode(Mode,Preferred);
            TestEqual(TEXT("Fullscreen and borderless each toggle back to windowed repeatedly"),Mode,I%2==0?EWindowMode::Windowed:Preferred);
        }
    }
    for(FIntPoint Desktop:{FIntPoint(1920,1080),FIntPoint(3440,1440),FIntPoint(3840,2160)})
    {
        TestEqual(TEXT("Mode-only fullscreen uses the monitor instead of the saved small window"),
            FACEDesktopDisplay::ResolveResolution(EWindowMode::Windowed,EWindowMode::Fullscreen,FIntPoint(1024,768),Desktop,false),Desktop);
        TestEqual(TEXT("Explicit lower fullscreen resolution remains available for performance"),
            FACEDesktopDisplay::ResolveResolution(EWindowMode::Windowed,EWindowMode::Fullscreen,FIntPoint(1280,720),Desktop,true),FIntPoint(1280,720));
        TestEqual(TEXT("Borderless always follows native monitor resolution"),
            FACEDesktopDisplay::ResolveResolution(EWindowMode::Fullscreen,EWindowMode::WindowedFullscreen,FIntPoint(800,600),Desktop,true),Desktop);
        TestEqual(TEXT("Invalid saved resolution recovers to the monitor"),
            FACEDesktopDisplay::ResolveResolution(EWindowMode::Windowed,EWindowMode::Fullscreen,FIntPoint(0,0),Desktop,true),Desktop);
    }

    ON_SCOPE_EXIT { Binder->Shutdown(); Canvas->SetGameplayBinder(nullptr); Manager->Shutdown(); Dat->Deinitialize(); TGuardValue<FString> Restore(GGameUserSettingsIni,OriginalSettings); ACEInputBindings::Reload(); };
    FWidgetRenderer Renderer(true,true);
    auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1600,900),TF_Bilinear,true);
    auto Draw=[&](const TCHAR* Name)
    {
        for (int32 Pass=0;Pass<4;++Pass)
        {
            ++GFrameCounter; Canvas->NativeTick(Canvas->GetCachedGeometry(),0.f);
            Renderer.DrawWidget(Target,Slate,FVector2D(1600,900),0.f); FlushRenderingCommands();
        }
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(1600,900,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/UIInteractions")/(FString(Name)+TEXT(".png"))));
    };
    Manager->ApplyEdgeAnchoredLayout(1600,900);

    // Click the visible routing row, then deliver each wire LogTextType. Cover
    // composite Combat/Magic groups, all five windows and scaled HUD coordinates.
    {
        TGuardValue<uint64> Global(Binder->GlobalChatTypeFilter,MAX_uint64);
        TGuardValue<uint64> Main(Binder->MainChatTypeFilter,Binder->MainChatTypeFilter);
        TGuardValue<TArray<uint64>> Floaty(Binder->FloatyChatFilters,Binder->FloatyChatFilters);
        TGuardValue<int32> QuickFilter(Binder->ChatFilterMode,0);
        for(int32 W=1;W<=4;++W) Manager->FindElementByName(FString::Printf(TEXT("RootGameplay_FloatyChat%d_Field"),W))->bVisible=true;
        Binder->ShowPanelPage(TEXT("OptionsPanel_Field")); Binder->SyncOptionsPanelTab(TEXT("ChatPage"));
        for(float Scale : {1.f,1.25f,2.f})
        {
            TGuardValue<float> ScaleX(Canvas->LastScaleX,Scale), ScaleY(Canvas->LastScaleY,Scale);
            for(int32 Window=0;Window<5;++Window) for(int32 Group=Window?0:1;Group<UE_ARRAY_COUNT(ACERetailChat::FilterGroups);++Group)
            {
                Binder->MainChatTypeFilter=MAX_uint64;
                for(auto& Mask:Binder->FloatyChatFilters) Mask=MAX_uint64;
                const int32 Option=1000+Window*UE_ARRAY_COUNT(ACERetailChat::FilterGroups)+Group;
                Binder->OptionsScrollOffset=0; Binder->RefreshOptionsOverlays();
                while(!Binder->OptionRowOptions.Contains(Option) && Binder->OptionsScrollOffset<Binder->GetOptionsRowCount())
                {++Binder->OptionsScrollOffset;Binder->RefreshOptionsOverlays();}
                const auto List=Manager->FindElementUnder(TEXT("ChatPage"),TEXT("ChatOptionsListBox"));
                const FVector2D Point=(FVector2D(List->GetScreenOrigin())+FVector2D(8,16*Binder->OptionRowOptions.Find(Option)+8))*Scale;
                TestTrue(TEXT("Scaled routing row accepts the pointer"),Binder->TryHandleOptionsListClick(Point));
                const uint64 Disabled=ACERetailChat::FilterGroups[Group].Mask;
                TestEqual(TEXT("The clicked category is the disabled category"),Window?Binder->FloatyChatFilters[Window-1]:Binder->MainChatTypeFilter,MAX_uint64&~Disabled);
                for(int32 Type=0;Type<=32;++Type)
                {
                    for(int32 W=0;W<5;++W) Binder->ChatDisplayLines[W].Reset();
                    FACEBinaryWriter Message;Message.WriteString16L(FString::Printf(TEXT("Routing %d %d %d %.2f"),Window,Group,Type,Scale));Message.WriteInt32(Type);
                    FACEBinaryReader Wire(Message.GetData());Session.HandleServerMessage(Wire);
                    for(int32 W=0;W<5;++W) TestEqual(*FString::Printf(TEXT("Routing window %d group %d type %d destination %d scale %.2f"),Window,Group,Type,W,Scale),Binder->ChatDisplayLines[W].Num(),W==Window&&(Disabled&(1ull<<Type))?0:1);
                }
                Binder->TryHandleOptionsListClick(Point);
                TestEqual(TEXT("Second click restores the group"),Window?Binder->FloatyChatFilters[Window-1]:Binder->MainChatTypeFilter,MAX_uint64);
            }
        }
        Binder->SaveFloatyChatSettings();
    }

    // Combat routing must use packet provenance, not the green Default color or
    // English death-message wording. ACE sends player and pet kills identically.
    {
        TGuardValue<uint64> Global(Binder->GlobalChatTypeFilter,MAX_uint64);
        TGuardValue<uint64> Main(Binder->MainChatTypeFilter,Binder->MainChatTypeFilter);
        TGuardValue<TArray<uint64>> Floaty(Binder->FloatyChatFilters,Binder->FloatyChatFilters);
        TGuardValue<int32> Quick(Binder->ChatFilterMode,0);
        int32 Observed=0,ObservedType=-1;
        const auto Handle=Session.OnChatMessage.AddLambda([&](const FString&,const FString&,int32 Type){++Observed;ObservedType=Type;});
        ON_SCOPE_EXIT { Session.OnChatMessage.Remove(Handle); };
        auto ResetLines=[&](){for(int32 W=0;W<5;++W) Binder->ChatDisplayLines[W].Reset();};
        auto Send=[&](const TCHAR* Text,int32 Type,bool Kill=false,bool Victim=false)
        {
            FACEBinaryWriter Message;
            Message.WriteUInt32(ACEOpcode::GameEvent);Message.WriteUInt32(Session.PlayerGuid);Message.WriteUInt32(1);
            Message.WriteUInt32(Kill?ACEGameEvent::KillerNotification:ACEGameEvent::VictimNotification);
            if(Kill||Victim) Message.WriteString16L(Text);
            else {Message.Reset();Message.WriteUInt32(ACEOpcode::ServerMessage);Message.WriteString16L(Text);Message.WriteInt32(Type);}
            Session.HandleGameMessage(Message.GetData());
        };
        for(int32 Window=0;Window<5;++Window)
        {
            Binder->MainChatTypeFilter=MAX_uint64;
            for(auto& Mask:Binder->FloatyChatFilters) Mask=MAX_uint64;
            (Window?Binder->FloatyChatFilters[Window-1]:Binder->MainChatTypeFilter)&=~0x600040ull;
            for(int32 Type:{ACEChatMessageType::Combat,ACEChatMessageType::CombatSelf,ACEChatMessageType::CombatEnemy})
            {
                ResetLines();Send(TEXT("Fire Elemental hits Drudge for 42 points of fire damage!"),Type);
                for(int32 W=0;W<5;++W) TestEqual(TEXT("Pet/creature attack respects each window's Combat setting"),Binder->ChatDisplayLines[W].Num(),W==Window?0:1);
            }
            for(const TCHAR* Death:{TEXT("Your attack killed the Drudge!"),TEXT("Pet kill wording supplied by server")})
            {
                ResetLines();const int32 Before=Observed;Send(Death,0,true);
                TestEqual(TEXT("Hidden kill remains available once to plugin observers"),Observed,Before+1);
                TestEqual(TEXT("Plugin kill type is not changed by UI filtering"),ObservedType,ACEChatMessageType::Broadcast);
                for(int32 W=0;W<5;++W)
                {
                    TestEqual(TEXT("Player/summon killing blow obeys Combat in every window"),Binder->ChatDisplayLines[W].Num(),W==Window?0:1);
                    if(W!=Window) TestTrue(TEXT("Kill keeps retail green color"),Binder->ChatDisplayLines[W].Last().Color.Equals(ACERetailChat::Color(0)));
                }
                const auto& Buffered=Client->GetChatHistory().Last();
                TestEqual(TEXT("HUD replay retains kill routing metadata"),Buffered.FilterType,ACEChatMessageType::CombatSelf);
                ResetLines();Binder->HandleChatMessage(Buffered.Text,Buffered.Sender,Buffered.Type,Buffered.FilterType);
                TestEqual(TEXT("Replayed kill cannot bypass main-window Combat setting"),Binder->ChatDisplayLines[0].Num(),Window==0?0:1);
            }
            for(bool Victim:{false,true})
            {
                ResetLines();Send(Victim?TEXT("You have died!"):TEXT("Your summoning device does not have enough charges to function!"),0,false,Victim);
                for(int32 W=0;W<5;++W) TestEqual(TEXT("Combat filtering preserves own death and gameplay notices"),Binder->ChatDisplayLines[W].Num(),1);
            }
        }
        Binder->MainChatTypeFilter=MAX_uint64;for(auto& Mask:Binder->FloatyChatFilters) Mask=MAX_uint64;
        Binder->ChatFilterMode=2;ResetLines();Send(TEXT("Pet killing blow"),0,true);
        TestEqual(TEXT("Combat-only quick filter includes killing blows"),Binder->ChatDisplayLines[0].Num(),1);
        ResetLines();Send(TEXT("Fire Elemental casts a spell"),ACEChatMessageType::Spellcasting);
        TestEqual(TEXT("Combat/magic quick filter includes spellcasting"),Binder->ChatDisplayLines[0].Num(),1);
        Binder->ChatFilterMode=0;Binder->GlobalChatTypeFilter&=~0x600040ull;
        ResetLines();Send(TEXT("Global-filtered pet killing blow"),0,true);
        for(int32 W=0;W<5;++W) TestEqual(TEXT("Global combat squelch also applies to routed kills"),Binder->ChatDisplayLines[W].Num(),0);
    }

    {
        TGuardValue<uint64> MainFilter(Binder->MainChatTypeFilter,MAX_uint64);
        TGuardValue<uint64> GlobalFilter(Binder->GlobalChatTypeFilter,MAX_uint64);
        for (int32 Type=0;Type<34;++Type)
        {
            FACEBinaryWriter W;W.WriteString16L(FString::Printf(TEXT("Retail chat color %d"),Type));W.WriteInt32(Type);
            FACEBinaryReader R(W.GetData());Session.HandleServerMessage(R);
            TestTrue(TEXT("Live HUD retains the retail message color"),Binder->ChatDisplayLines[0].Last().Color.Equals(ACERetailChat::Color(Type)));
            if (auto* Row=Cast<UTextBlock>(Binder->ChatLog->GetChildAt(Binder->ChatLog->GetChildrenCount()-1)))
                TestTrue(TEXT("Painted chat label uses the same color as VR's shared history"),Row->GetColorAndOpacity().GetSpecifiedColor().Equals(ACERetailChat::Color(Type)));
        }
        Binder->ClearChatLog(0);
        FACEBinaryWriter D;D.WriteString16L(TEXT("Your attack killed the Drudge!"));FACEBinaryReader R(D.GetData());Session.HandleCombatKillerNotification(R);
        Client->ReceiveChatMessage(ACECombatChat::FormatAttackerNotification(TEXT("Drudge"),4,.2f,42,false,6),FString(),22);
        Client->ReceiveChatMessage(ACECombatChat::FormatDefenderNotification(TEXT("Drudge"),4,.2f,12,1,false,6),FString(),21);
        Client->ReceiveChatMessage(TEXT("Dirty Fighting! Player delivers a Bleeding Assault to Drudge!"),FString(),6);
        Client->ReceiveChatMessage(TEXT("You cast Strength Self VII on yourself."),FString(),7);
        Client->ReceiveChatMessage(TEXT("You have earned 1,000 experience!"),FString(),13);
        Client->ReceiveChatMessage(TEXT("[General] Friend says, Hello!"),FString(),27);
        auto ChatRoot=Manager->FindElementByName(TEXT("RootGameplay_FloatyMainChat_Field"));
        if (ChatRoot)
        {
            const int32 OldResize=ChatRoot->UserResizeH,OldY=ChatRoot->Y;
            ChatRoot->UserResizeH+=140;UACEUIElementManager::ApplyFloatyResizeLayout(ChatRoot);ChatRoot->Y-=140;
            Draw(TEXT("RetailCombatChatColors"));
            ChatRoot->UserResizeH=OldResize;UACEUIElementManager::ApplyFloatyResizeLayout(ChatRoot);ChatRoot->Y=OldY;
        }
        Binder->ClearChatLog(0);
        for(double& Time:Binder->TransientInfoTimes) Time-=6.;Binder->TickTransientInfo();
        const auto OriginalSkills=Binder->LastVitals.Skills;
        Binder->ApplyCombatMode(int32(ACECombatMode::Melee));
        auto Fill=Manager->FindElementByName(TEXT("basic_recklessness_fill"));
        TestTrue(TEXT("Authored Recklessness band exists"),Fill.IsValid());
        for(int32 Training:{0,1,2,3,1})
        {
            Binder->LastVitals.Skills.Reset();FACESkillInfo Skill;Skill.SkillId=50;Skill.AdvancementClass=Training;Binder->LastVitals.Skills.Add(Skill);
            Binder->RefreshCombatPanelOverlays();
            if(Fill) TestEqual(TEXT("Recklessness band follows trained/untrained state"),Fill->bVisible,Training>=2);
        }
        Binder->LastVitals.Skills=OriginalSkills;
        const auto Stance=Session.GetCurrentStance();
        Session.SetCurrentStance(ACEMotion::StanceDualWield);
        TestEqual(TEXT("Dual wield charges at retail's 0.8 second period"),Binder->GetCombatPowerChargeDuration(),.8f);
        Session.SetCurrentStance(ACEMotion::StanceSwordCombat);
        TestEqual(TEXT("Single weapon charges at retail's one second period"),Binder->GetCombatPowerChargeDuration(),1.f);
        Session.SetCurrentStance(Stance);
        Binder->ApplyCombatMode(int32(ACECombatMode::NonCombat));
    }

    // Retail gmSpewBoxUI listens to LogTextType 26 independently of chat
    // filtering. Exercise the wire handlers, subsystem relay and actual HUD.
    const int32 BeforeFailures = Binder->ChatDisplayLines[0].Num();
    const int32 BeforeHistory = Client->GetChatHistory().Num();
    auto FailureEvent = [&](uint32 Event, uint32 Code, const TCHAR* Argument = nullptr)
    {
        FACEBinaryWriter W; W.WriteUInt32(1234); W.WriteUInt32(1); W.WriteUInt32(Event); W.WriteUInt32(Code);
        if (Argument) W.WriteString16L(Argument);
        FACEBinaryReader R(W.GetData()); Session.HandleGameEvent(R);
    };
    for (uint32 Code : {0x03F5u, 0x0420u, 0x0421u, 0x042Fu, 0x0468u, 0x04C7u, 0x0585u, 0x04FCu})
    {
        FailureEvent(ACEGameEvent::WeenieError, Code);
        TestTrue(TEXT("Wield/use failure is visible at the top of the screen"), Binder->TransientInfoText && Binder->TransientInfoText->IsVisible());
        if (Binder->TransientInfoText) TestEqual(TEXT("Banner keeps retail failure wording"),
            Binder->TransientInfoText->GetText().ToString(), ACECombatChat::LookupWeenieError(Code));
    }
    FailureEvent(ACEGameEvent::WeenieErrorWithString, 0x04CB, TEXT("Summoning"));
    TestEqual(TEXT("Named requirements expand in the notice area"), Binder->TransientInfoText->GetText().ToString(),
        FString(TEXT("You must have Summoning specialized to use that item's magic.")));
    FailureEvent(ACEGameEvent::UseDone, 0x0468);
    TestEqual(TEXT("UseDone errors share the same banner"), Binder->TransientInfoText->GetText().ToString(), FString(TEXT("Your skill is too low.")));
    TestEqual(TEXT("Notices do not appear in retail's default chat window"), Binder->ChatDisplayLines[0].Num(), BeforeFailures);
    TestEqual(TEXT("Expired failures cannot replay when a HUD is recreated"), Client->GetChatHistory().Num(), BeforeHistory);
    TestEqual(TEXT("Notice rows are bounded to the retail maximum"), Binder->TransientInfoLines.Num(), 4);
    const FText PreviousSecond = Binder->TransientInfoLines[1]->GetText();
    Binder->TransientInfoTimes[0] -= 4.5;
    Binder->TickTransientInfo();
    TestTrue(TEXT("A notice fades before expiring"), Binder->TransientInfoText->GetRenderOpacity() < 1.f);
    FailureEvent(ACEGameEvent::UseDone, 0x0468);
    TestEqual(TEXT("Repeating a failure refreshes it instead of pushing out other notices"), Binder->TransientInfoLines[1]->GetText().ToString(), PreviousSecond.ToString());
    TestEqual(TEXT("Repeat resets the notice fade"), Binder->TransientInfoText->GetRenderOpacity(), 1.f);
    const FString LastNotice = Binder->TransientInfoText->GetText().ToString();
    FailureEvent(ACEGameEvent::WeenieErrorWithString, 0x050D, TEXT("Friend"));
    FailureEvent(ACEGameEvent::WeenieErrorWithString, 0x0052, TEXT("Other Player"));
    TestEqual(TEXT("System and magic errors keep their chat destinations"), Binder->ChatDisplayLines[0].Num(), BeforeFailures + 2);
    TestEqual(TEXT("Ordinary chat does not replace action failure notices"), Binder->TransientInfoText->GetText().ToString(), LastNotice);
    {
        TGuardValue<uint64> Filter(Binder->GlobalChatTypeFilter, 0);
        FailureEvent(ACEGameEvent::WeenieError, 0x0420);
        TestEqual(TEXT("Chat filtering cannot suppress the notice area"), Binder->TransientInfoText->GetText().ToString(), FString(TEXT("Your level is too low.")));
    }
    Draw(TEXT("RetailFailureNotices"));
    const auto VitalsPanel = Manager->FindElementByName(TEXT("RootGameplay_FloatyVitals_Field"));
    const FVector2D NoticeOrigin = Canvas->GetCachedGeometry().AbsoluteToLocal(
        Binder->TransientInfoBox->GetCachedGeometry().LocalToAbsolute(FVector2D::ZeroVector));
    const float VitalsBottom = Canvas->LayoutToViewport(FVector2D(VitalsPanel->GetScreenOrigin())
        + FVector2D(0, VitalsPanel->GetLayoutHeight())).Y;
    TestTrue(TEXT("Top-center vitals cannot hide the newest failure notices"), NoticeOrigin.Y >= VitalsBottom);
    auto* NoticeFont = CastChecked<UACERetailTextBlock>(Binder->TransientInfoText)->GetBitmapFont();
    TestTrue(TEXT("Notice text uses the native retail font atlas"), NoticeFont && NoticeFont->Id == 0x40000001u);
    for (double& Time : Binder->TransientInfoTimes) Time -= 6.0;
    Binder->TickTransientInfo();
    for (const auto& Line : Binder->TransientInfoLines) TestFalse(TEXT("Expired notices leave the viewport"), Line->IsVisible());
    FACEBinaryWriter Banner; Banner.WriteString16L(TEXT("Still a banner"));
    FACEBinaryReader BannerReader(Banner.GetData()); Session.HandleTransientString(BannerReader);
    TestTrue(TEXT("Explicit transient strings still display after prior notices expire"), Binder->TransientInfoText->IsVisible());
    TestEqual(TEXT("Transient string wording is intact"), Binder->TransientInfoText->GetText().ToString(), FString(TEXT("Still a banner")));

    // Real tab input, hotkey navigation, and the VR selection entry point share state.
    Session.SpellBars.SetNum(8);
    for (int32 I=1;I<=20;++I) { Session.SpellBars[0].Add(I); Session.SpellBars[1].Add(I+20); }
    Binder->ApplyCombatMode(int32(ACECombatMode::Magic));
    Binder->SetCombatSpellBar(0); Binder->SelectVRSpell(7); Binder->RefreshSpellHotbarOverlays();
    Binder->HandleNamedClick(TEXT("Spellcast_Tab2"));
    TestEqual(TEXT("First visit selects the first spell"),Binder->SelectedCombatSpellSlot,0);
    Binder->SelectVRSpell(29); Binder->RefreshSpellHotbarOverlays();
    Binder->HandleNamedClick(TEXT("Spellcast_Tab1"));
    TestEqual(TEXT("Tab one restores seventh spell"),Binder->SelectedCombatSpellSlot,6);
    Binder->HandleNamedClick(TEXT("Spellcast_Tab2"));
    TestEqual(TEXT("Tab two restores ninth spell"),Binder->SelectedCombatSpellSlot,8);
    Binder->StepCombatSpellSelection(1,false,true); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Last spell selected"),Binder->SelectedCombatSpellSlot,19);
    TestTrue(TEXT("Last selection is inside viewport"),19>=Binder->SpellHotbarScrollOffset && 19<Binder->SpellHotbarScrollOffset+13);
    Binder->StepCombatSpellSelection(1,false,false); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Next wraps last to first"),Binder->SelectedCombatSpellSlot,0);
    TestEqual(TEXT("Wrapping scrolls left to first"),Binder->SpellHotbarScrollOffset,0);
    Binder->StepCombatSpellSelection(-1,false,false); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Previous wraps first to last"),Binder->SelectedCombatSpellSlot,19);
    Session.SpellBars[1].SetNumZeroed(56);
    Binder->StepCombatSpellSelection(1,false,false); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Padded 56-entry bar wraps directly to its first spell"),Binder->SelectedCombatSpellSlot,0);
    Binder->StepCombatSpellSelection(-1,false,false); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Reverse navigation skips every trailing empty entry"),Binder->SelectedCombatSpellSlot,19);
    Binder->SetCombatSpellBar(0); Binder->SetCombatSpellBar(1);
    TestEqual(TEXT("Returning restores scrolled selection"),Binder->SelectedCombatSpellSlot,19);
    TestTrue(TEXT("Restored cursor is visible"),19<Binder->SpellHotbarScrollOffset+13);
    Draw(TEXT("SpellSelectionLast"));
    Session.SpellBars[1].Swap(0,19); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Reordering preserves selected spell identity"),Binder->SelectedCombatSpellSlot,0);
    Binder->SpellHotbarScrollOffset=3; Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Manual scrolling is not forcibly undone"),Binder->SpellHotbarScrollOffset,3);
    Binder->SelectVRSpell(40); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Explicit re-selection reveals even the same spell"),Binder->SpellHotbarScrollOffset,0);
    Session.SpellBars[1].Reset(); Binder->RefreshSpellHotbarOverlays();
    Binder->StepCombatSpellSelection(1,false,false);
    TestEqual(TEXT("Empty bar has a safe cursor"),Binder->SelectedCombatSpellSlot,0);

    // Actual drag gesture: lift immediately, move the destination marker, then
    // insert exactly where shown, without a second removal on release.
    Binder->SetCombatSpellBar(1); Session.SpellBars[1]={1,2,3,4};
    Binder->RefreshSpellHotbarOverlays(); Draw(TEXT("SpellDragBefore"));
    auto SlotPoint=[&](int32 Index) { const auto& G=Binder->SpellBarSlotBgs[Index]->GetCachedGeometry();
        return Canvas->GetCachedGeometry().AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()*.5)); };
    TestTrue(TEXT("Spell press begins gesture"),Binder->TryBeginSpellDrag(SlotPoint(0)));
    Binder->UpdateSpellDrag(SlotPoint(2));
    TestFalse(TEXT("Dragged spell immediately leaves favorites"),Session.SpellBars[1].Contains(1));
    TestEqual(TEXT("Remaining spells close the source gap"),Session.SpellBars[1][0],2);
    TestTrue(TEXT("Insertion marker uses original retail green accept art"),Binder->SpellDropMarker
        && Binder->SpellDropMarker->Background.GetResourceObject()==Resources->ResolveIconTexture(0x060011F9)
        && Binder->SpellDropMarker->GetVisibility()!=ESlateVisibility::Collapsed);
    const auto PreviousCursor=FSlateApplication::Get().GetCursorPos();
    FSlateApplication::Get().SetCursorPos(Canvas->GetCachedGeometry().LocalToAbsolute(SlotPoint(2)));
    Draw(TEXT("SpellDragging")); Binder->TryFinishSpellDrag(SlotPoint(2));
    FSlateApplication::Get().SetCursorPos(PreviousCursor);
    TestTrue(TEXT("Drop inserts at indicated index after source removal"),Session.SpellBars[1]==TArray<int32>({2,3,1,4}));
    TestTrue(TEXT("Drop hides destination marker"),Binder->SpellDropMarker->GetVisibility()==ESlateVisibility::Collapsed);

    Session.SpellBars[2]={5,6};
    Draw(TEXT("SpellCrossTabStart"));
    Binder->TryBeginSpellDrag(SlotPoint(0));
    const auto DestinationTab=Manager->FindElementByName(TEXT("Spellcast_Tab3"));
    if(TestTrue(TEXT("Third spell tab exists"),DestinationTab.IsValid()))
    {
        const FVector2D TabPoint=(FVector2D(DestinationTab->GetScreenOrigin())+FVector2D(DestinationTab->Width,DestinationTab->Height)*.5)*FVector2D(Canvas->GetLastScaleX(),Canvas->GetLastScaleY());
        Binder->UpdateSpellDrag(TabPoint);
        TestEqual(TEXT("Hovering another tab during drag opens that bar"),Client->GetActiveSpellBar(),2);
        Binder->TryFinishSpellDrag(TabPoint);
        TestTrue(TEXT("Drop on a tab appends the lifted spell exactly once"),Session.SpellBars[2]==TArray<int32>({5,6,2}));
        TestFalse(TEXT("Cross-tab move removes the original"),Session.SpellBars[1].Contains(2));
        Binder->SetCombatSpellBar(1);Binder->RefreshSpellHotbarOverlays();Draw(TEXT("SpellCrossTabEnd"));
    }

    const auto Combat=Manager->FindElementByName(TEXT("RootGameplay_FloatyCombatPanel_Field"));
    Manager->SetUiLocked(false); Manager->BringFloatyToFront(Combat);
    Combat->UserDragX+=200-Combat->GetScreenOrigin().X; Combat->UserDragY+=200-Combat->GetScreenOrigin().Y; Combat->RecomputeLayoutOffset();
    Binder->RefreshSpellHotbarOverlays(); Draw(TEXT("SpellResizeBefore"));
    const auto Edge=Manager->FindElementByName(TEXT("CombatPanelRightBorder"));
    const FVector2D Start=Canvas->LayoutToViewport(FVector2D(Edge->GetScreenOrigin())+FVector2D(Edge->Width,Edge->Height)*.5);
    const auto ResizeCanvasSize=Canvas->GetCachedGeometry().GetLocalSize();
    TestTrue(TEXT("Spellbar right border advertises horizontal resize"),Manager->GetWindowCursor(Start,ResizeCanvasSize)==EMouseCursor::ResizeLeftRight);
    Manager->NotifyMouseDown(Start,ResizeCanvasSize,EKeys::LeftMouseButton);
    const int32 BeforeWidth=Combat->Width;
    Manager->NotifyMouseMove(Start+FVector2D(17*Canvas->GetLastScaleX(),0),ResizeCanvasSize); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Spellbar frame follows a 17-pixel drag without snapping"),Combat->Width,BeforeWidth+17);
    Manager->NotifyMouseMove(Start+FVector2D(-170*Canvas->GetLastScaleX(),0),ResizeCanvasSize); Binder->RefreshSpellHotbarOverlays();
    Manager->NotifyMouseUp(Start+FVector2D(-170*Canvas->GetLastScaleX(),0),ResizeCanvasSize,EKeys::LeftMouseButton);
    TestTrue(TEXT("Narrowing frame reduces visible spell slots"),Binder->SpellBarSpellIds.Num()<13);
    const auto CastButton=Manager->FindElementByName(TEXT("CastSpellButton"));
    TestTrue(TEXT("Cast stays inside narrowed frame"),CastButton->GetScreenOrigin().X+CastButton->Width<=Combat->GetScreenOrigin().X+Combat->Width);
    Draw(TEXT("SpellResizeNarrow"));
    TestEqual(TEXT("Right frame edge stays aligned after ticks"),Edge->GetScreenOrigin().X+Edge->Width,Combat->GetScreenOrigin().X+Combat->Width);
    for (auto SlotBackground : Binder->SpellBarSlotBgs)
        if (SlotBackground->GetVisibility()!=ESlateVisibility::Collapsed)
            TestTrue(TEXT("Cast does not overlap a visible slot after reflow"),CastButton->GetScreenOrigin().X>=SlotBackground->GetCachedGeometry().GetAbsolutePosition().X+32);
    Combat->UserResizeW=0; Binder->RefreshSpellHotbarOverlays(); Manager->SetUiLocked(true);

    // DAT-backed retail compatibility, not spell-name heuristics. Buffs,
    // debuffs and heals use the same formula target rules in every school.
    auto Other=Self; Other.Guid=1235; Other.Name=TEXT("Other player"); Session.WorldObjects.Add(Other.Guid,Other);
    int32 Resolved=0;
    uint32 SpellFlags=0, RetailType=0; bool Projectile=false;
    TestTrue(TEXT("DAT exposes retail formula targeting"),Dat->TryGetRetailSpellTargeting(37,SpellFlags,RetailType,Projectile));
    TestEqual(TEXT("Blade Bane uses retail equipment mask instead of narrow server target type"),RetailType,560015u);
    for (int32 Spell : {1,3,5,7,15,1237,27,5387})
    {
        TestFalse(TEXT("Other creature spell rejects caster"),Client->ResolveSpellCastTarget(Spell,Self.Guid,Resolved));
        TestTrue(TEXT("Other creature spell accepts another player"),Client->ResolveSpellCastTarget(Spell,Other.Guid,Resolved));
        TestEqual(TEXT("Accepted recipient is unchanged"),Resolved,Other.Guid);
    }
    TestTrue(TEXT("Self healing ignores selected enemy"),Client->ResolveSpellCastTarget(6,Other.Guid,Resolved) && Resolved==Self.Guid);
    TestTrue(TEXT("Item bane may target caster's worn equipment"),Client->ResolveSpellCastTarget(37,Self.Guid,Resolved));
    TestTrue(TEXT("Self portal recall remains valid without selection"),Client->ResolveSpellCastTarget(2645,0,Resolved));
    int32 CasterSpellCount=0, LongInspectionSpell=2, LongestInspection=0;
    for (int32 Spell=1;Spell<=65535;++Spell)
    {
        if (!Dat->TryGetRetailSpellTargeting(Spell,SpellFlags,RetailType,Projectile) || !(SpellFlags&8u)) continue;
        ++CasterSpellCount;
        FString Details;
        if (Dat->TryGetSpellExamination(Spell,Details) && Details.Len()>LongestInspection)
        { LongInspectionSpell=Spell; LongestInspection=Details.Len(); }
        for (int32 Selected : {0,Other.Guid,0x7654321})
            TestTrue(FString::Printf(TEXT("Caster-only DAT spell %d ignores selection %d"),Spell,Selected),
                Client->ResolveSpellCastTarget(Spell,Selected,Resolved) && Resolved==Self.Guid);
    }
    TestTrue(TEXT("Caster targeting audit covers the retail spell table"),CasterSpellCount>100);
    AddInfo(FString::Printf(TEXT("Verified %d caster-only spells against retail DAT targeting flags."),CasterSpellCount));
    TestTrue(TEXT("VR Flame Bolt retains free aiming even without Projectile flag"),Client->ResolveSpellCastTarget(27,0,Resolved,true));
    TestFalse(TEXT("VR Other healing cannot fall back to self"),Client->ResolveSpellCastTarget(5,Self.Guid,Resolved,true));
    auto Npc=Other; Npc.Guid=1236; Npc.bIsPlayer=false; Session.WorldObjects.Add(Npc.Guid,Npc);
    TestFalse(TEXT("Protected NPC is not a spell target"),Client->ResolveSpellCastTarget(5,Npc.Guid,Resolved));
    Npc.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable; Session.WorldObjects.Add(Npc.Guid,Npc);
    TestTrue(TEXT("Attackable creature follows retail eligibility"),Client->ResolveSpellCastTarget(7,Npc.Guid,Resolved));
    Npc.PetOwnerId=Other.Guid; Session.WorldObjects.Add(Npc.Guid,Npc);
    TestFalse(TEXT("Combat pet is excluded like retail"),Client->ResolveSpellCastTarget(7,Npc.Guid,Resolved));
    auto Stack=Other; Stack.Guid=1237; Stack.bIsPlayer=false; Stack.ItemType=ACEItemType::MeleeWeapon;
    Stack.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable; Stack.StackSize=2; Session.WorldObjects.Add(Stack.Guid,Stack);
    TestFalse(TEXT("Stacked item cannot be enchanted"),Client->ResolveSpellCastTarget(37,Stack.Guid,Resolved));
    Session.SpellBars[1]={5}; Binder->SelectCombatSpellSlot(0); Client->SelectObject(Self.Guid); Binder->RefreshSpellHotbarOverlays();
    TestFalse(TEXT("Heal Other Cast button disabled for self"),CastButton->bActivatable);
    Session.CachedC2SPackets.Reset(); TestFalse(TEXT("Cast API cannot bypass disabled button"),Client->SendCastSpell(5));
    TestTrue(TEXT("Rejected cast sends no action"),Session.CachedC2SPackets.IsEmpty()); Draw(TEXT("HealOtherSelfDisabled"));
    Client->SelectObject(Other.Guid); Binder->RefreshSpellHotbarOverlays();
    TestTrue(TEXT("Heal Other Cast button enables for another player"),CastButton->bActivatable); Draw(TEXT("HealOtherPlayerEnabled"));
    Session.SpellBars[1].Reset();

    // Learn a spell beyond the first page using the actual sorted DAT book.
    TArray<int32> BookSpells;
    for (uint32 Id=1;Id<=500;++Id)
    {
        uint32 School=0,Level=0;
        if (Dat->TryGetSpellSchoolAndLevel(Id,School,Level) && School>=1 && School<=5 && Level>=1 && Level<=8)
            BookSpells.Add(Id);
    }
    BookSpells.Sort([&](int32 A,int32 B)
    {
        uint32 OA=0,OB=0; Dat->TryGetSpellDisplayOrder(A,OA); Dat->TryGetSpellDisplayOrder(B,OB);
        return OA!=OB ? OA<OB : A<B;
    });
    if (!TestTrue(TEXT("Fixture has multiple pages of real spells"),BookSpells.Num()>40)) return false;
    Session.KnownSpells=BookSpells;
    const int32 NewSpell=Session.KnownSpells.Pop();
    uint32 NewSchool=0,NewLevel=0; Dat->TryGetSpellSchoolAndLevel(NewSpell,NewSchool,NewLevel);
    Binder->SpellbookSchoolFilterMask=0x1Fu & ~(1u<<(NewSchool-1));
    Binder->SpellbookLevelFilterMask=0xFFu & ~(1u<<(NewLevel-1));
    Binder->HandleNamedClick(TEXT("SpellComponentTab"));
    Binder->ShowPanelPage(TEXT("InventoryPanel_Field"));
    const int32 CombatSelection=Binder->SelectedCombatSpellSlot;
    auto Learn=[&](int32 Id)
    {
        FACEBinaryWriter W; W.WriteUInt32(1234); W.WriteUInt32(1); W.WriteUInt32(ACEGameEvent::MagicUpdateSpell);
        W.WriteUInt16(Id); W.WriteUInt16(0); FACEBinaryReader R(W.GetData()); Session.HandleGameEvent(R);
    };
    Learn(NewSpell);
    TestEqual(TEXT("Learning opens spell management"),Binder->ActivePanelPage,FString(TEXT("SpellManagementPanel_Field")));
    TestEqual(TEXT("Learning switches from components to the spellbook"),Binder->ActiveSpellPanelTab,FString(TEXT("SpellbookPage")));
    TestEqual(TEXT("The new spell is selected"),Binder->SelectedSpellbookId,NewSpell);
    TestTrue(TEXT("Selection scrolls beyond the first page"),Binder->SpellbookScrollOffset>0);
    TestTrue(TEXT("Learned spell is present in visible rows"),Binder->SpellbookRowIds.Contains(NewSpell));
    TestEqual(TEXT("Learning preserves the combat hotbar selection"),Binder->SelectedCombatSpellSlot,CombatSelection);
    TestEqual(TEXT("The new spell's school is revealed"),Binder->SpellbookSchoolFilterMask,0x1Fu);
    TestEqual(TEXT("The new spell's level is revealed"),Binder->SpellbookLevelFilterMask,0xFFu);
    Draw(TEXT("NewlyLearnedSpell"));
    const int32 LearnedRow=Binder->SpellbookRowIds.Find(NewSpell);
    TestTrue(TEXT("New spell has the visible retail selection highlight"),LearnedRow!=INDEX_NONE && Binder->SpellbookRowSelections[LearnedRow]->IsVisible());
    Binder->SpellbookScrollOffset=0; Binder->RefreshSpellbookOverlays();
    TestEqual(TEXT("Reveal does not override later manual scrolling"),Binder->SpellbookScrollOffset,0);
    Binder->ShowPanelPage(TEXT("InventoryPanel_Field")); Learn(NewSpell);
    TestEqual(TEXT("Duplicate learn packets cannot steal the open panel"),Binder->ActivePanelPage,FString(TEXT("InventoryPanel_Field")));
    const int32 EarlySpell=BookSpells[0]; Session.KnownSpells.Remove(EarlySpell);
    Binder->SpellbookScrollOffset=100; Learn(EarlySpell);
    TestTrue(TEXT("Learning near the beginning also scrolls the spell into view"),Binder->SpellbookRowIds.Contains(EarlySpell));

    // Retail gmSpellbookUI double-click appends via AddFavorite(-1, false),
    // rather than casting, replacing an occupied slot, or relocating a duplicate.
    Session.KnownSpells={41,42};
    Binder->ShowPanelPage(TEXT("SpellManagementPanel_Field")); Binder->HandleNamedClick(TEXT("SpellbookTab"));
    Binder->SetCombatSpellBar(0); Binder->TickRefresh(); Draw(TEXT("SpellbookBeforeAppend"));
    auto ClickBook=[&](int32 SpellId,bool bDoubleClick)
    {
        const int32 Row=Binder->SpellbookRowIds.Find(SpellId);
        if (!TestTrue(TEXT("Known spell has a visible row"),Row!=INDEX_NONE)) return;
        const auto& RowGeometry=Binder->SpellbookRowBackgrounds[Row]->GetCachedGeometry();
        const FVector2D Position=RowGeometry.LocalToAbsolute(RowGeometry.GetLocalSize()*.5);
        const FPointerEvent Press(0,Position,Position,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
        const FPointerEvent Release(0,Position,Position,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
        if (bDoubleClick) Canvas->NativeOnMouseButtonDoubleClick(Canvas->GetCachedGeometry(),Press);
        else Canvas->NativeOnMouseButtonDown(Canvas->GetCachedGeometry(),Press);
        Canvas->NativeOnMouseButtonUp(Canvas->GetCachedGeometry(),Release);
    };
    const uint32 BeforeBook=Session.NextGameActionSequence;
    ClickBook(41,false);
    TestEqual(TEXT("Single spellbook click sends no cast or favorite action"),Session.NextGameActionSequence,BeforeBook);
    ClickBook(41,true);
    TestEqual(TEXT("Double-click appends after all twenty existing spells"),Session.SpellBars[0].Find(41),20);
    TestEqual(TEXT("Double-click sends exactly one favorite action"),Session.NextGameActionSequence,BeforeBook+1);
    TestEqual(TEXT("New shortcut is selected as in retail"),Binder->SelectedCombatSpellSlot,20);
    TestTrue(TEXT("Appended shortcut scrolls into view"),20>=Binder->SpellHotbarScrollOffset && 20<Binder->SpellHotbarScrollOffset+13);
    TestTrue(TEXT("Other tab is unchanged"),Session.SpellBars[1].IsEmpty());
    const auto BeforeDuplicate=Session.SpellBars[0];
    ClickBook(41,false); ClickBook(41,true);
    TestTrue(TEXT("An existing shortcut is not duplicated or moved"),Session.SpellBars[0]==BeforeDuplicate);
    TestEqual(TEXT("Duplicate sends no remove/add packets"),Session.NextGameActionSequence,BeforeBook+1);
    Binder->SetCombatSpellBar(1); Draw(TEXT("SpellbookEmptyTab"));
    ClickBook(41,false); ClickBook(41,true);
    TestEqual(TEXT("Double-click uses the currently open tab"),Session.SpellBars[1].Find(41),0);
    TestTrue(TEXT("Adding to another tab preserves the first tab"),Session.SpellBars[0]==BeforeDuplicate);
    TestEqual(TEXT("Adding a favorite leaves combat stance unchanged"),Binder->CombatMode,int32(ACECombatMode::Magic));
    const int32 DragRow=Binder->SpellbookRowIds.Find(42);
    const auto& DragGeometry=Binder->SpellbookRowBackgrounds[DragRow]->GetCachedGeometry();
    const FVector2D DragStart=Canvas->GetCachedGeometry().AbsoluteToLocal(DragGeometry.LocalToAbsolute(DragGeometry.GetLocalSize()*.5));
    Binder->TryBeginSpellDrag(DragStart); Binder->UpdateSpellDrag(DragStart+FVector2D(-80,0));
    Binder->TryFinishSpellDrag(DragStart+FVector2D(-80,0));
    ClickBook(42,false);
    TestFalse(TEXT("A completed drag cannot become the first half of a double-click"),Session.SpellBars[1].Contains(42));
    ClickBook(42,true);
    TestEqual(TEXT("Next double-click still appends at the end"),Session.SpellBars[1].Find(42),1);
    Draw(TEXT("SpellbookAppended"));

    // The original DAT edge anchors grow the list, not the fixed description.
    const auto SharedPanel=Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field"));
    UACEUIElementManager::ApplyFloatyResizeLayout(SharedPanel);
    for(const TCHAR* PageName:{TEXT("PositiveEffectsPanel_Field"),TEXT("NegativeEffectsPanel_Field"),TEXT("SpellManagementPanel_Field")})
    {
        Binder->ShowPanelPage(PageName); if(FString(PageName)==TEXT("SpellManagementPanel_Field")) Binder->HandleNamedClick(TEXT("SpellbookTab"));
        const bool Book=FString(PageName)==TEXT("SpellManagementPanel_Field");
        const auto ListEl=Manager->FindElementUnder(PageName,Book?TEXT("SpellBook_SpellList"):TEXT("Effects_SpellList"));
        const auto Footer=Manager->FindElementUnder(PageName,Book?TEXT("FilterBox"):TEXT("InfoBackground"));
        int32 SmallHeight=0;
        for(int32 H:{380,680,380})
        {
            SharedPanel->UserResizeH=H-SharedPanel->AuthoredHeight;
            UACEUIElementManager::ApplyFloatyResizeLayout(SharedPanel);Binder->TickRefresh();
            Draw(*(FString(PageName)+FString::FromInt(H)));
            AddInfo(FString::Printf(TEXT("Resize %s H=%d frame=%d list=%d footerY=%d"),PageName,H,SharedPanel->Height,ListEl->Height,Footer->GetScreenOrigin().Y));
            TestEqual(TEXT("Spell footer keeps its retail height"),Footer->Height,Book?113:88);
            TestEqual(TEXT("Footer stays against the bottom frame"),Footer->GetScreenOrigin().Y+Footer->Height,
                SharedPanel->GetScreenOrigin().Y+SharedPanel->Height-5);
            TestTrue(TEXT("List never overlaps its bottom controls"),ListEl->GetScreenOrigin().Y+ListEl->Height<=Footer->GetScreenOrigin().Y);
            if(H==380 && SmallHeight==0)SmallHeight=ListEl->Height;
            else TestEqual(TEXT("Additional panel height goes entirely to the spell list"),ListEl->Height,SmallHeight+H-380);
        }
    }
    // Character edits are staged; Chat and Config resets cannot change character options.
    Binder->ShowPanelPage(TEXT("OptionsPanel_Field"));Binder->SyncOptionsPanelTab(TEXT("CharacterSettingsPage"));
    Draw(TEXT("RetailCharacterOptions"));
    TestTrue(TEXT("Character starts with UI behavior in retail order"),Binder->OptionRowOptions.Num()>1 && Binder->OptionRowOptions[1]==0x07);
    TestEqual(TEXT("Retail display option belongs to Character"),ACECharacterOptions::Find(0x13)->Page,uint8(ACECharacterOptions::PageCharacter));
    TestEqual(TEXT("Channel subscription belongs to Character, not Chat routing"),ACECharacterOptions::Find(0x23)->Page,uint8(ACECharacterOptions::PageCharacter));
    const uint32 Original=Session.CharacterOptions1;
    const auto CharacterList=Manager->FindElementUnder(TEXT("CharacterSettingsPage"),TEXT("CharacterOptionsListBox"));
    Binder->TryHandleOptionsListClick(FVector2D(CharacterList->GetScreenOrigin())+FVector2D(12,24));
    TestEqual(TEXT("Editing a Character checkbox does not prematurely send it"),Session.CharacterOptions1,Original);
    TestTrue(TEXT("Checkbox toggles the draft bit"),Binder->OptionsDraft1!=(Original));
    Binder->HandleOptionsNamedClick(TEXT("ResetButton"));TestEqual(TEXT("Reset restores saved checkbox state"),Binder->OptionsDraft1,Original);
    Binder->TryHandleOptionsListClick(FVector2D(CharacterList->GetScreenOrigin())+FVector2D(12,24));
    Binder->HandleOptionsNamedClick(TEXT("ApplyButton"));TestEqual(TEXT("Apply saves the chosen checkbox"),Session.CharacterOptions1,Original^0x80u);
    Binder->SyncOptionsPanelTab(TEXT("ChatPage"));Binder->HandleOptionsNamedClick(TEXT("DefaultButton"));
    TestEqual(TEXT("Chat defaults leave character options alone"),Session.CharacterOptions1,Original^0x80u);

    Binder->SyncOptionsPanelTab(TEXT("ConfigPage"));Draw(TEXT("RetailConfigOptions"));
    auto* Video=Cast<UACEVideoSettingsWidget>(Binder->VideoSettings);
    if (Video)
    {
        auto* Settings=UGameUserSettings::GetGameUserSettings();
        const auto SavedMode=Settings->GetFullscreenMode();const auto SavedSize=Settings->GetScreenResolution();
        ON_SCOPE_EXIT {Settings->SetFullscreenMode(SavedMode);Settings->SetScreenResolution(SavedSize);Video->ResetVideo();};
        auto* Mode=Cast<UComboBoxString>(Video->WidgetTree->FindWidget(TEXT("DisplayMode")));
        auto* Size=Cast<UComboBoxString>(Video->WidgetTree->FindWidget(TEXT("DisplayResolution")));
        if (TestNotNull(TEXT("Display mode control exists"),Mode) && TestNotNull(TEXT("Resolution control exists"),Size))
        {
            Settings->SetFullscreenMode(EWindowMode::Windowed);Settings->SetScreenResolution(FIntPoint(1024,768));
            Video->NativeTick(FGeometry(),0.f);
            TestEqual(TEXT("External display changes update the mode selector"),Mode->GetSelectedIndex(),2);
            TestEqual(TEXT("External resolution changes update the selector"),Size->GetSelectedOption(),FString(TEXT("1024 x 768")));
            Mode->SetSelectedIndex(0);Video->NativeTick(FGeometry(),0.f);
            TestEqual(TEXT("Polling preserves an unapplied display selection"),Mode->GetSelectedIndex(),0);
            Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);Video->NativeTick(FGeometry(),0.f);
            TestEqual(TEXT("Alt+Enter to borderless cannot leave Fullscreen displayed"),Mode->GetSelectedIndex(),1);
        }
    }
    auto* Glow=Video?Cast<UCheckBox>(Video->WidgetTree->FindWidget(TEXT("ObjectGlow"))):nullptr;
    if(TestNotNull(TEXT("Graphics options exposes object glow"),Glow))
    {
        TestTrue(TEXT("Glow defaults checked"),Glow->IsChecked());
        Glow->SetIsChecked(false);Glow->OnCheckStateChanged.Broadcast(false);
        TestEqual(TEXT("Disabling glow takes effect without Apply"),ACERuntimeOptions::Get(TEXT("ObjectGlow")),0.f);
        Video->ResetVideo();
        TestFalse(TEXT("Glow preference survives UI reload"),Glow->IsChecked());
        Glow->SetIsChecked(true);Glow->OnCheckStateChanged.Broadcast(true);
        TestEqual(TEXT("Re-enabling glow takes effect without Apply"),ACERuntimeOptions::Get(TEXT("ObjectGlow")),1.f);
    }
    auto* ScreenshotFolder=Video?Cast<UEditableTextBox>(Video->WidgetTree->FindWidget(TEXT("ScreenshotDirectory"))):nullptr;
    if(TestNotNull(TEXT("Config exposes screenshot save folder"),ScreenshotFolder))
    {
        const FString CustomFolder=FPaths::ConvertRelativePathToFull(ArtDirectory/TEXT("Custom Screenshots"));
        ScreenshotFolder->SetText(FText::FromString(CustomFolder)); Video->ApplyInterfaceOptions(); Video->ResetVideo();
        TestEqual(TEXT("Screenshot folder applies and reloads"),ScreenshotFolder->GetText().ToString(),CustomFolder);
        Video->SetScrollOffset(Video->GetScrollEnd()); Draw(TEXT("ScreenshotDirectory")); Video->SetScrollOffset(0);
        ScreenshotFolder->SetText(FText::GetEmpty()); Video->ApplyInterfaceOptions();
        TestEqual(TEXT("Empty setting restores default folder"),ACEScreenshotSettings::GetDirectory(),ACEScreenshotSettings::DefaultDirectory());
    }
    auto* Face=Video?Cast<UComboBoxString>(Video->WidgetTree->FindWidget(TEXT("ChatFontFace"))):nullptr;
    auto* FontSize=Video?Cast<UComboBoxString>(Video->WidgetTree->FindWidget(TEXT("ChatFontSize"))):nullptr;
    if(TestNotNull(TEXT("Retail chat font face control"),Face) && TestNotNull(TEXT("Retail chat font size control"),FontSize))
    {
        TestEqual(TEXT("All five retail font faces"),Face->GetOptionCount(),5);TestEqual(TEXT("All five retail font sizes"),FontSize->GetOptionCount(),5);
        Binder->AppendChatLineToLog(0,TEXT("Existing text changes font and wraps without losing its copy selection."),FLinearColor::White,FString());
        auto* Log=Binder->GetChatLogWidget(0);auto* Row=Cast<UACERetailTextBlock>(Log->GetChildAt(Log->GetChildrenCount()-1));
        for(int32 F=0;F<5;++F)for(int32 Size=0;Size<5;++Size)
        {
            Face->SetSelectedIndex(F);FontSize->SetSelectedIndex(Size);Video->ApplyInterfaceOptions();Binder->RefreshChatRowLayout(0);
            const auto* Font=Row->GetBitmapFont();
            TestTrue(TEXT("Every chosen face/size resolves a real retail atlas"),Font && Font->Id==ACERuntimeOptions::ChatFontId());
            TestTrue(TEXT("Changing fonts preserves selectable chat"),Row->IsSelectable());
        }
        Face->SetSelectedIndex(3);FontSize->SetSelectedIndex(4);Video->ApplyInterfaceOptions();
        Video->ResetVideo();TestEqual(TEXT("Reset retains applied Tahoma"),Face->GetSelectedIndex(),3);
        TestEqual(TEXT("Reset retains applied extra large size"),FontSize->GetSelectedIndex(),4);
        Draw(TEXT("ChatFontExtraLarge"));
        Video->DefaultsVideo();Video->ApplyInterfaceOptions();Binder->RefreshChatRowLayout(0);
        TestEqual(TEXT("Defaults restore original retail chat font"),Row->GetBitmapFont()->Id,0x40000000u);
    }

    // Retail separates skill-credit training from XP advancement, including the
    // authored footer and its hit targets (gmSkillUI::DisplaySelectionFooter_Untrained).
    {
        const auto SavedVitals = Session.PlayerVitals;
        ON_SCOPE_EXIT { Session.PlayerVitals = SavedVitals; Client->OnVitalsUpdated.Broadcast(SavedVitals); };
        auto& Stats = Session.PlayerVitals;
        Stats.bValid = true; Stats.AvailableSkillCredits = 100; Stats.AvailableExperience = 1000000;
        FACESkillInfo Skill; Skill.SkillId = 39; Skill.Name = TEXT("Cooking"); Skill.AdvancementClass = 1;
        Stats.Skills = {Skill};
        Binder->SelectedSkillId = Skill.SkillId;
        Binder->ShowPanelPage(TEXT("SkillManagementPanel_Field")); Binder->SyncSkillPanelTab(TEXT("SkillPage"));
        Client->OnVitalsUpdated.Broadcast(Stats);
        Draw(TEXT("SkillTrainingAvailable"));
        const auto Training = Manager->FindElementUnder(TEXT("SkillPage"), TEXT("StatManagement_Footer_Text"));
        const auto Meter = Manager->FindElementUnder(TEXT("SkillPage"), TEXT("StatManagement_Footer_Meter"));
        if (!TestTrue(TEXT("Both native skill footer templates exist"), Training.IsValid() && Meter.IsValid())) return false;
        auto Button = [](const TSharedPtr<FACEUIElement>& Footer, const TCHAR* Name)
        {
            const auto* Found = Footer->Children.FindByPredicate([Name](const auto& Child) { return Child->ElementName == Name; });
            return Found ? *Found : TSharedPtr<FACEUIElement>();
        };
        const auto Train = Button(Training, TEXT("StatManagement_Footer_RaiseButton"));
        const auto Train10 = Button(Training, TEXT("StatManagement_Footer_Raise10Button"));
        const auto Raise10 = Button(Meter, TEXT("StatManagement_Footer_Raise10Button"));
        if (!TestTrue(TEXT("Native skill arrows exist"), Train.IsValid() && Train10.IsValid() && Raise10.IsValid())) return false;
        TestTrue(TEXT("Untrained skills show the text footer without an XP meter"), Training->bVisible && !Meter->bVisible);
        TestFalse(TEXT("Training never shows a +10 arrow"), bool(Train10->bVisible));
        TestFalse(TEXT("Hidden training +10 cannot activate"), Train10->bActivatable);
        TestTrue(TEXT("Sufficient credits enable the single training arrow"), Train->bActivatable);
        TestEqual(TEXT("Single training arrow uses retail's enabled artwork"), Train->ResolvePaintState(false,false,false)->ImageFileId, 0x06004CB6u);
        TestEqual(TEXT("Training title is the skill name, without a level"), Binder->AttrFooterTitle->GetText().ToString(), Skill.Name);
        int32 Cost = 0; Dat->TryGetSkillTrainedCost(Skill.SkillId, Cost);
        TestTrue(TEXT("Fixture skill has a real training cost"), Cost > 0);
        TestEqual(TEXT("Training displays the DAT skill-credit cost"), Binder->AttrFooterLine1Value->GetText().ToString(), FString::FromInt(Cost));
        const uint32 BeforeTraining = Session.NextGameActionSequence;
        Binder->HandleNamedClick(TEXT("StatManagement_Footer_Raise10Button"));
        Binder->TryHandleOverlayClick(FVector2D(Train10->GetScreenOrigin()) + FVector2D(15,13), false);
        TestEqual(TEXT("Neither named +10 input nor its hidden hit area can train"), Session.NextGameActionSequence, BeforeTraining);
        TestTrue(TEXT("The text footer's single arrow accepts pointer input"),
            Binder->TryHandleOverlayClick(FVector2D(Train->GetScreenOrigin()) + FVector2D(15,13), false));
        TestEqual(TEXT("Single training click sends exactly one action"), Session.NextGameActionSequence, BeforeTraining + 1);
        Stats.AvailableSkillCredits = Cost - 1; Client->OnVitalsUpdated.Broadcast(Stats);
        Draw(TEXT("SkillTrainingInsufficient"));
        TestFalse(TEXT("Insufficient credits disable training"), Train->bActivatable);
        TestEqual(TEXT("Unaffordable training uses retail's disabled arrow"), Train->ResolvePaintState(false,false,false)->ImageFileId, 0x06004CB7u);
        Binder->HandleNamedClick(TEXT("StatManagement_Footer_RaiseButton"));
        TestEqual(TEXT("Unaffordable training sends no action"), Session.NextGameActionSequence, BeforeTraining + 1);
        Stats.Skills[0].AdvancementClass = 2; Client->OnVitalsUpdated.Broadcast(Stats);
        Draw(TEXT("SkillTrainedAdvancement"));
        TestTrue(TEXT("Learning the skill switches to the XP meter footer"), !Training->bVisible && Meter->bVisible);
        TestTrue(TEXT("Trained skills retain the +10 XP option"), Raise10->bVisible && Raise10->bActivatable);
        Binder->HandleNamedClick(TEXT("StatManagement_Footer_Raise10Button"));
        TestEqual(TEXT("Trained +10 still sends an advancement action"), Session.NextGameActionSequence, BeforeTraining + 2);
    }

    // Resizing, wheel, arrow buttons, thumb dragging and row identity use the real DAT tree.
    Binder->ShowPanelPage(TEXT("SkillManagementPanel_Field")); Binder->SyncSkillPanelTab(TEXT("AttributePage"));
    const auto Floaty=Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field"));
    const auto List=Manager->FindElementUnder(TEXT("AttributePage"),TEXT("StatManagement_List"));
    const auto Bar=Manager->FindElementUnder(TEXT("AttributePage"),TEXT("StatManagement_List_Scrollbar"));
    if (!Floaty || !List || !Bar) return false;
    Floaty->UserResizeH=360-(Floaty->AuthoredHeight>=0?Floaty->AuthoredHeight:Floaty->Height);
    UACEUIElementManager::ApplyFloatyResizeLayout(Floaty);
    Draw(TEXT("AttributesSmall"));
    const int32 Page=FMath::Max(1,List->Height/18),MaxOffset=FMath::Max(0,9-Page);
    TestTrue(TEXT("Small attribute panel needs scrolling"),MaxOffset>0);
    Binder->ScrollStatList(-1); Draw(TEXT("AttributesScrolled"));
    TestTrue(TEXT("Wheel changes attribute offset"),Binder->AttributeScrollOffset>0);
    TestEqual(TEXT("Last visible attribute is Mana"),Binder->AttributeRows[FMath::Min(Page,9)-1]->GetText().ToString(),FString(TEXT("Mana")));
    const auto Up=Manager->FindElementUnder(TEXT("AttributePage"),TEXT("ScrollBar_Up"));
    Binder->OnElementActivated(Up);
    TestEqual(TEXT("Arrow steps one attribute"),Binder->AttributeScrollOffset,FMath::Max(0,MaxOffset-1));
    Draw(TEXT("AttributesArrow"));
    const FVector2D Bottom=FVector2D(Bar->GetScreenOrigin())+FVector2D(8,Bar->Height-18);
    TestTrue(TEXT("Attribute track starts a drag"),Binder->TryBeginScrollbarDrag(Bottom));
    Binder->UpdateScrollbarDrag(Bottom+FVector2D(0,500)); Binder->TryFinishScrollbarDrag();
    Draw(TEXT("AttributesDragEnd"));
    TestEqual(TEXT("Thumb reaches last attribute"),Binder->AttributeScrollOffset,MaxOffset);
    const auto* Paint=Canvas->PaintStates.Find(Bar->InstanceId);
    TestTrue(TEXT("Vertical chain track repeats instead of stretching"),Paint && Paint->Tiling==uint8(ESlateBrushTileType::Vertical));
    const int32 VisibleIndex=FMath::Min(Page,9)-1;
    const auto& Geometry=Binder->AttributeRowHighlights[VisibleIndex]->GetCachedGeometry();
    const auto ClickPoint=Canvas->GetElementLayer()->GetCachedGeometry().AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5));
    TestTrue(TEXT("Scrolled attribute row can be clicked"),Binder->TryHandleOverlayClick(ClickPoint,false));
    TestEqual(TEXT("Scrolled click selects Mana, not the first-page row"),Binder->SelectedAttributeRow,8);
    Floaty->UserResizeH=600-Floaty->AuthoredHeight; UACEUIElementManager::ApplyFloatyResizeLayout(Floaty); Draw(TEXT("AttributesLarge"));
    TestEqual(TEXT("Expanding clamps the offset back to zero"),Binder->AttributeScrollOffset,0);
    TestFalse(TEXT("All nine attributes fit without scrollbar"),bool(Bar->bVisible));

    // No use packet or busy state for a quest-controlled door, including live changes.
    FACEWorldObject Door; Door.Guid=0x80001234; Door.ObjectDescriptionFlags=ACEObjectDescFlag::Door; Door.ItemUseable=1;
    Session.WorldObjects.Add(Door.Guid,Door); Session.bUseBusy=false;
    FString DoorMessage;
    const auto Chat=Session.OnChatMessage.AddLambda([&](const FString& Text,const FString&,int32){DoorMessage=Text;});
    const uint32 UseSequence=Session.NextGameActionSequence;
    Session.SendUseItem(Door.Guid);
    TestEqual(TEXT("Forbidden direct door use produces no action packet"),Session.NextGameActionSequence,UseSequence);
    TestFalse(TEXT("Forbidden door does not start use"),Session.bUseBusy);
    TestEqual(TEXT("Door feedback explains restriction"),DoorMessage,FString(TEXT("This door cannot be activated from here.")));
    for (int32 Use : {32,1})
    {
        FACEBinaryWriter W; W.WriteUInt8(1); W.WriteUInt32(Door.Guid); W.WriteUInt32(16); W.WriteInt32(Use);
        FACEBinaryReader R(W.GetData()); Session.HandlePublicUpdatePropertyInt(R);
        TestEqual(TEXT("Direct use follows live ItemUseable updates"),Session.WorldObjects[Door.Guid].IsDirectDoorUseBlocked(),Use==1);
        Session.bUseBusy=false; const uint32 BeforeUse=Session.NextGameActionSequence;
        Session.SendUseItem(Door.Guid);
        TestEqual(TEXT("Only an allowed door goes to the server for quest/lock checks"),Session.NextGameActionSequence,BeforeUse+(Use==32?1u:0u));
    }
    Door.ItemUseable=0; TestFalse(TEXT("Missing Undef property is not an explicit prohibition"),Door.IsDirectDoorUseBlocked());
    Session.OnChatMessage.Remove(Chat);

    FACEWorldObject Defeated; Defeated.Guid=90010; Defeated.WeenieClassId=900001;
    Defeated.ItemType=ACEItemType::Creature; Defeated.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
    Defeated.bHasPosition=true; Defeated.bDying=true;
    Session.WorldObjects.Add(Defeated.Guid,Defeated);
    auto Refresh=Defeated; Refresh.bDying=false;
    Session.UpsertWorldObject(Refresh);
    TestTrue(TEXT("Stale same-creature description cannot resurrect a defeated creature"),Session.WorldObjects[Defeated.Guid].bDying);
    auto Reward=Refresh; Reward.WeenieClassId=900002; Reward.ObjectDescriptionFlags=0;
    Reward.ItemUseable=32; Reward.Name=TEXT("Custom reward NPC");
    Session.UpsertWorldObject(Reward);
    TestFalse(TEXT("Replacement weenie does not inherit the defeated creature's death state"),Session.WorldObjects[Reward.Guid].bDying);
    TestTrue(TEXT("Custom reward NPC can be selected without a name whitelist"),Session.WorldObjects[Reward.Guid].IsSelectableWorldObject());
    Session.bUseBusy=false; const auto BeforeRewardUse=Session.NextGameActionSequence;
    Session.SendUseItem(Reward.Guid);
    TestEqual(TEXT("Custom reward NPC use reaches the server"),Session.NextGameActionSequence,BeforeRewardUse+1);
    Session.bUseBusy=false;

    // Spell and item inspections share a scroll box. Opening a spell used to
    // leave its Slate scrollbar visible alongside the next item's DAT bar.
    Binder->ExaminedSpellId=LongInspectionSpell; Binder->bExaminationDismissed=false;
    Binder->ShowExamination(true); Binder->RefreshExaminationOverlay();
    Draw(TEXT("SpellExamination")); Binder->RefreshExaminationOverlay();
    TestFalse(TEXT("Fixed spell formula divider cannot cross the responsive description"),
        Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellExamBackground_Divider_Lower"))->bVisible);
    TestTrue(TEXT("Spell examination includes authored ingredient names"),Binder->ExamBody->GetText().ToString().Contains(TEXT("Ingredients:")));
    TestTrue(TEXT("Spell inspection creates the shared scroll box"),Binder->ExamScroll!=nullptr);
    if (Binder->ExamScroll)
    {
        TestEqual(TEXT("Spell inspection uses only the retail scrollbar"),Binder->ExamScroll->GetScrollBarVisibility(),ESlateVisibility::Collapsed);
        const auto ExamWindow=Manager->FindElementByName(TEXT("RootGameplay_FloatyExamination_Field"));
        const auto SpellBody=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellDisplayText"));
        const int32 SmallHeight=SpellBody->Height;
        ExamWindow->UserResizeH+=180; UACEUIElementManager::ApplyFloatyResizeLayout(ExamWindow);
        TestEqual(TEXT("Spell description grows with the examination window"),SpellBody->Height,SmallHeight+180);
        ExamWindow->UserResizeH-=180; UACEUIElementManager::ApplyFloatyResizeLayout(ExamWindow);
        const auto SpellBar=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellDisplayTextScrollbar"));
        TestTrue(TEXT("Spell inspection scrollbar follows actual overflow after resizing"),SpellBar && SpellBar->bVisible==(Binder->ExamScroll->GetScrollOffsetOfEnd()>.5f));
        if (SpellBar && Binder->ExamScroll->GetScrollOffsetOfEnd()>.5f)
        {
            Binder->OnElementActivated(Manager->FindElementUnder(TEXT("SpellDisplayTextScrollbar"),TEXT("ScrollBar_Down")));
            TestTrue(TEXT("Spell inspection retail arrow scrolls the text"),Binder->ExamScroll->GetScrollOffset()>0);
            const FVector2D Track=FVector2D(SpellBar->GetScreenOrigin())+FVector2D(6,SpellBar->Height-18);
            TestTrue(TEXT("Spell inspection retail thumb can be dragged"),Binder->TryBeginScrollbarDrag(Track));
            Binder->UpdateScrollbarDrag(Track+FVector2D(0,1000)); Binder->TryFinishScrollbarDrag();
            TestTrue(TEXT("Spell inspection thumb reaches the end"),FMath::IsNearlyEqual(Binder->ExamScroll->GetScrollOffset(),Binder->ExamScroll->GetScrollOffsetOfEnd(),1.f));
        }
        Binder->ExaminedSpellId=0;
        Binder->LastAppraisal={}; Binder->LastAppraisal.ObjectGuid=90001;
        Binder->LastAppraisal.bSuccess=true; Binder->LastAppraisal.Name=TEXT("Long item inspection");
        Binder->LastAppraisal.bHasValue=Binder->LastAppraisal.bHasBurden=true;
        for (int32 I=0;I<70;++I) Binder->LastAppraisal.Summary+=FString::Printf(TEXT("Inspection detail %d\n"),I);
        Binder->ShowExamination(true); Binder->RefreshExaminationOverlay();
        Draw(TEXT("ItemAfterSpellExamination")); Binder->RefreshExaminationOverlay();
        const auto ItemBar=Manager->FindElementUnder(TEXT("ItemExamineUI"),TEXT("ItemDisplayTextScrollbar"));
        TestEqual(TEXT("Returning to an item does not expose a second scrollbar"),Binder->ExamScroll->GetScrollBarVisibility(),ESlateVisibility::Collapsed);
        TestTrue(TEXT("Long item inspection shows the DAT scrollbar"),ItemBar && ItemBar->bVisible && Binder->ExamScroll->GetScrollOffsetOfEnd()>0);
        const auto ItemDown=Manager->FindElementUnder(TEXT("ItemDisplayTextScrollbar"),TEXT("ScrollBar_Down"));
        Binder->OnElementActivated(ItemDown);
        TestTrue(TEXT("Retail inspection arrow scrolls the shared text"),Binder->ExamScroll->GetScrollOffset()>0);
        if (ItemBar)
        {
            const FVector2D Track=FVector2D(ItemBar->GetScreenOrigin())+FVector2D(6,ItemBar->Height-18);
            TestTrue(TEXT("Retail inspection thumb can be dragged"),Binder->TryBeginScrollbarDrag(Track));
            Binder->UpdateScrollbarDrag(Track+FVector2D(0,1000)); Binder->TryFinishScrollbarDrag();
            TestTrue(TEXT("Inspection thumb reaches the end"),FMath::IsNearlyEqual(Binder->ExamScroll->GetScrollOffset(),Binder->ExamScroll->GetScrollOffsetOfEnd(),1.f));
        }
        Draw(TEXT("ItemExaminationScrolled"));
        Binder->ExaminedSpellId=2; Binder->ShowExamination(true); Binder->RefreshExaminationOverlay();
        Draw(TEXT("SpellAfterItemExamination"));
        TestEqual(TEXT("Repeated inspection switches retain one scrollbar"),Binder->ExamScroll->GetScrollBarVisibility(),ESlateVisibility::Collapsed);
        Binder->ShowExamination(false);
    }

    // An open inspection follows selections, including out-of-order appraisal replies.
    {
        auto SelectForInspection=[&](const FACESelectedObject& S)
        {
            Session.SelectedObject=S; // The session updates its snapshot before broadcasting.
            Client->OnSelectionChanged.Broadcast(S);
        };
        FACEWorldObject A;A.Guid=91001;A.Name=TEXT("First inspected item");A.ItemType=ACEItemType::Misc;
        FACEWorldObject B=A;B.Guid=91002;B.Name=TEXT("New inspected target");B.ItemType=ACEItemType::Creature;
        Session.WorldObjects.Add(A.Guid,A);Session.WorldObjects.Add(B.Guid,B);
        FACESelectedObject Selection;Selection.Guid=A.Guid;Selection.Name=A.Name;Selection.bValid=true;
        SelectForInspection(Selection);
        Client->SendIdentifyObject(A.Guid);
        FACEAppraisalInfo First;First.ObjectGuid=A.Guid;First.Name=A.Name;First.bSuccess=true;
        Binder->HandleAppraisal(First);
        auto* Menu=NewObject<UACEVRMenu>();Menu->InitializeMenu(nullptr,Client,Binder);
        Menu->bInspectionOpen=true;Menu->InspectItem=A.Guid;Menu->InspectionAppraisal=First;
        const uint64 Before=Client->GetIdentifyRequestSerial();
        Selection.Guid=B.Guid;Selection.Name=B.Name;
        SelectForInspection(Selection);
        TestEqual(TEXT("Open desktop and VR panels share one new appraisal request"),Client->GetIdentifyRequestSerial(),Before+1);
        TestEqual(TEXT("Desktop immediately replaces old target"),Binder->LastAppraisal.ObjectGuid,B.Guid);
        TestEqual(TEXT("Desktop clears stale appraisal fields while waiting"),Binder->LastAppraisal.Summary,FString(TEXT("Waiting for the server's appraisal...")));
        TestEqual(TEXT("VR follows a world target even outside the inventory page"),Menu->InspectItem,B.Guid);
        TestEqual(TEXT("VR discards the previous target's qualities"),Menu->InspectionAppraisal.ObjectGuid,0);
        Selection.bShowHealth=true;Selection.HealthFraction=.75f;
        SelectForInspection(Selection);
        TestEqual(TEXT("Health updates do not spam identification requests"),Client->GetIdentifyRequestSerial(),Before+1);
        Client->OnAppraisal.Broadcast(First);
        TestEqual(TEXT("Late appraisal cannot restore old desktop target"),Binder->LastAppraisal.ObjectGuid,B.Guid);
        TestEqual(TEXT("Late appraisal cannot restore old VR target"),Menu->InspectionAppraisal.ObjectGuid,0);
        FACEAppraisalInfo Second;Second.ObjectGuid=B.Guid;Second.Name=B.Name;Second.bIsCreature=true;Second.bSuccess=true;
        Client->OnAppraisal.Broadcast(Second);
        TestTrue(TEXT("New creature appraisal switches desktop panel type"),Binder->LastAppraisal.bIsCreature);
        TestEqual(TEXT("VR accepts new target appraisal"),Menu->InspectionAppraisal.ObjectGuid,B.Guid);
        Draw(TEXT("InspectionFollowsTarget"));
        SelectForInspection(FACESelectedObject{});
        TestFalse(TEXT("Clearing selection closes desktop inspection"),Manager->FindElementByName(TEXT("RootGameplay_FloatyExamination_Field"))->bVisible);
        TestFalse(TEXT("Clearing selection closes VR inspection"),Menu->IsInspectionOpen());
        Client->OnAppraisal.Broadcast(Second);
        TestFalse(TEXT("A late reply cannot reopen dismissed inspection"),Manager->FindElementByName(TEXT("RootGameplay_FloatyExamination_Field"))->bVisible);
        const uint64 ClosedSerial=Client->GetIdentifyRequestSerial();
        SelectForInspection(Selection);
        TestEqual(TEXT("Closed inspection stays closed when targets change"),Client->GetIdentifyRequestSerial(),ClosedSerial);
        // VR-only inspection still requests updates when the desktop panel is hidden.
        Menu->bInspectionOpen=true;Menu->InspectItem=B.Guid;
        Selection.Guid=A.Guid;Selection.Name=A.Name;SelectForInspection(Selection);
        TestEqual(TEXT("VR-only inspection follows selection"),Menu->InspectItem,A.Guid);
        TestEqual(TEXT("VR-only selection requests appraisal"),Client->GetIdentifyRequestSerial(),ClosedSerial+1);
        Menu->InspectSpell=1;Menu->InspectItem=0;Binder->ExaminedSpellId=1;Binder->ShowExamination(true);
        const uint64 SpellSerial=Client->GetIdentifyRequestSerial();
        SelectForInspection(Selection);
        TestEqual(TEXT("Health-only selection update leaves desktop spell inspection intact"),Binder->ExaminedSpellId,1);
        TestEqual(TEXT("Health-only selection update leaves VR spell inspection intact"),Menu->InspectSpell,1);
        TestEqual(TEXT("Spell inspection does not trigger object appraisal on health updates"),Client->GetIdentifyRequestSerial(),SpellSerial);
        Binder->ShowExamination(false);Menu->NativeDestruct();
    }

    // Existing keymap UI can rebind screenshot; exercise the actual engine save path.
    ACEInputBindings::Reload(); ACEInputBindings::BeginEdit(); ACEInputBindings::Defaults();
    const auto Screenshot=ACEInputBindings::Action(TEXT("CaptureScreenshot"));
    TestEqual(TEXT("Retail screenshot default is numpad multiply"),ACEInputBindings::Get(Screenshot,0).Key,EKeys::Multiply);
    ACEInputBindings::Set(Screenshot,0,FInputChord(EKeys::F12)); ACEInputBindings::Commit();
    TestTrue(TEXT("Rebound screenshot key is recognized"),ACEInputBindings::Matches(Screenshot,FInputChord(EKeys::F12)));
    TestFalse(TEXT("Old key is no longer active"),ACEInputBindings::Matches(Screenshot,FInputChord(EKeys::Multiply)));
#if PLATFORM_WINDOWS
    TestEqual(TEXT("Windows default is the redirected Documents/Asheron's Call folder"),ACEScreenshotSettings::DefaultDirectory(),FString(FPlatformProcess::UserDir())/TEXT("Asheron's Call"));
#endif
    const FString CaptureFolder=FPaths::ConvertRelativePathToFull(ArtDirectory/TEXT("Screenshot Output"));
    ACEScreenshotSettings::SetDirectory(CaptureFolder);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
    Binder->RequestGameplayScreenshot();
    const FString Saved=Binder->PendingScreenshotFilename;
    TestFalse(TEXT("Screenshot request has a destination"),Saved.IsEmpty());
    TestTrue(TEXT("Capture uses configured path with spaces"),Saved.StartsWith(CaptureFolder));
    Binder->RequestGameplayScreenshot();
    TestEqual(TEXT("Repeated request cannot replace in-flight capture"),Binder->PendingScreenshotFilename,Saved);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
        GEngine->GameViewport->ProcessScreenShots(GEngine->GameViewport->Viewport);
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes,*Saved);
    TestTrue(TEXT("Screenshot writes a real PNG"),Bytes.Num()>100 && Bytes[0]==137 && Bytes[1]=='P' && Bytes[2]=='N' && Bytes[3]=='G');
    TestTrue(TEXT("Saved screenshot feedback includes its path"),Binder->TransientInfoText && Binder->TransientInfoText->GetText().ToString().Contains(Saved));
    TestTrue(TEXT("Completion clears pending capture"),Binder->PendingScreenshotFilename.IsEmpty());
    Binder->PendingScreenshotFilename=FPaths::ProjectSavedDir()/TEXT("Automation/nonexistent-screenshot.png");
    Binder->FinishGameplayScreenshot();
    TestTrue(TEXT("Failed save is reported as failure"),Binder->TransientInfoText->GetText().ToString().StartsWith(TEXT("Unable to save screenshot")));
    if (!Saved.IsEmpty()) IFileManager::Get().Delete(*Saved);
    }
    else AddInfo(TEXT("Screenshot capture needs -game; editor-only run verifies folder settings."));
    Binder->Shutdown();
    TestEqual(TEXT("A new session starts tab memory from scratch"),Binder->SelectionSpellTab,INDEX_NONE);
    for (const auto& Tab:Binder->SpellTabSelections) TestEqual(TEXT("New tab default is first spell"),Tab.Slot,0);
    return true;
}
#endif
