#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEHoverTooltipWidget.h"
#include "ACEMouseCursorWidget.h"
#include "ACERuntimeOptions.h"
#include "UI/ACEUIResourceResolver.h"
#include "Misc/ConfigCacheIni.h"
#include "ACEDatSubsystem.h"
#include "Mods/ACEPluginStyle.h"
#include "ACESession.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/SWindow.h"
#include "Input/HittestGrid.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/LayerManager/STooltipPresenter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/IToolTip.h"
#include "Components/Border.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginLiveUITest,"ACE.Plugins.LiveUI",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginLiveUITest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()||!FSlateApplication::IsInitialized())return true;
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);W->SetGameInstance(GI);Context.OwningGameInstance=GI;Context.SetCurrentWorld(W);GI->OnWorldChanged(nullptr,W);GI->Init();
    FString DatDirectory=TEXT("C:/Turbine/Asheron's Call");FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),DatDirectory);
    const bool DatReady=GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(DatDirectory);
    auto* H=GI->GetSubsystem<UACEPluginSubsystem>();
    auto P=MakeShared<FACEClientPlugin>();P->Id=TEXT("live_ui_test");P->Companion=true;P->Api=2;P->Enabled=true;P->Running=true;
    P->Permissions={TEXT("ui")};H->Plugins.Add(P);
    auto Snapshot=[&](int Kills,const TCHAR* Value=TEXT("Melee"),bool Extra=false)
    {
        const FString Json=FString::Printf(TEXT(R"({"ui":[{"id":"tasks","type":"section","text":"Kill tasks","children":[
          {"id":"kills","text":"Killed %d / 50","tooltip":"Progress: %d / 50"},
          {"id":"mode","type":"select","text":"Mode","value":"%s","options":["Melee","Magic"]},
          {"id":"filter","type":"input","text":"Filter","value":"Server value"}%s]}]})"),Kills,Kills,Value,
          Extra?TEXT(",{\"id\":\"extra\",\"text\":\"New quest\"}"):TEXT(""));
        TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),O);
        if(TestTrue(TEXT("Live snapshot parsed"),O.IsValid())){P->PanelRows=O->GetArrayField(TEXT("ui"));++P->PanelRevision;}
    };
    Snapshot(12);
    auto Panel=H->MakeCompanionPanel(P->Id);auto GameView=SNew(SBorder);auto Content=SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[Panel]+SVerticalBox::Slot().FillHeight(1)[GameView];
    const FVector2D Size(640,480);auto Window=SNew(SVirtualWindow).Size(Size);Window->SetContent(Content);Window->SetIsFocusable(true);
    auto& Slate=FSlateApplication::Get();const auto OldFocus=Slate.GetUserFocusedWidget(0);Slate.RegisterVirtualWindow(Window);
    FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
    ON_SCOPE_EXIT {Slate.ReleaseAllPointerCapture();Slate.DismissAllMenus();Slate.ClearUserFocus(0);if(OldFocus)Slate.SetUserFocus(0,OldFocus);Slate.UnregisterVirtualWindow(Window);Target->ReleaseResource();GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
    FHittestGrid HitTestGrid;
    auto Frame=[&](){++GFrameCounter;Renderer.DrawWindow(Target,HitTestGrid,Window,1.f,Size,0);FlushRenderingCommands();};Frame();Frame();
    TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName,bool)> Find;
    Find=[&](TSharedRef<SWidget> Root,FName Name,bool Tag)->TSharedPtr<SWidget>
    {if(Tag?Root->GetTag()==Name:Root->GetType()==Name||Root->GetTypeAsString().StartsWith(Name.ToString()+TEXT("<")))return Root;auto* Children=Root->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto Result=Find(Children->GetChildAt(I),Name,Tag))return Result;return nullptr;};
    auto Control=[&](const TCHAR* Id){return Find(Panel,FName(*(FString(TEXT("PluginControl."))+Id)),true);};
    auto Mode=Control(TEXT("mode")),Kills=Control(TEXT("kills")),Filter=Control(TEXT("filter"));
    if(!TestTrue(TEXT("Controls created"),Mode&&Kills&&Filter))return false;
    auto Combo=StaticCastSharedPtr<SComboBox<TSharedPtr<FString>>>(Find(Mode.ToSharedRef(),TEXT("SComboBox"),false));
    auto Input=StaticCastSharedPtr<SEditableTextBox>(Find(Filter.ToSharedRef(),TEXT("SEditableTextBox"),false));
    auto Label=StaticCastSharedPtr<STextBlock>(Find(Kills.ToSharedRef(),TEXT("SACEPluginText"),false));
    auto Section=StaticCastSharedPtr<SExpandableArea>(Find(Panel,TEXT("SExpandableArea"),false));
    TestTrue(TEXT("Dropdown created"),Combo.IsValid());TestTrue(TEXT("Text input created"),Input.IsValid());
    TestTrue(TEXT("Task label created"),Label.IsValid());TestTrue(TEXT("Section created"),Section.IsValid());
    if(!Combo||!Input||!Label||!Section)return false;
    const auto LiveTooltip=Label->GetToolTip();
    if(!TestTrue(TEXT("Plugin API hover text has a retail tooltip"),LiveTooltip.IsValid()))return false;
    LiveTooltip->OnOpening();const auto TooltipWidget=LiveTooltip->AsWidget();
    auto TooltipLabel=StaticCastSharedPtr<STextBlock>(Find(TooltipWidget,TEXT("SACERetailTextBlock"),false));
    if(!TestTrue(TEXT("Plugin API hover uses inventory bitmap text"),TooltipLabel.IsValid()))return false;
    TestEqual(TEXT("Initial plugin tooltip text"),TooltipLabel->GetText().ToString(),FString(TEXT("Progress: 12 / 50")));

    Combo->SetIsOpen(true);TestTrue(TEXT("Dropdown starts open"),Combo->IsOpen());
    Snapshot(13,TEXT("Magic"));Frame();
    LiveTooltip->AsWidget();
    TestEqual(TEXT("Open plugin tooltip follows live snapshot updates"),TooltipLabel->GetText().ToString(),FString(TEXT("Progress: 13 / 50")));
    TestTrue(TEXT("Updating plugin help retains its tooltip"),Label->GetToolTip()==LiveTooltip);
    TestTrue(TEXT("Value updates keep the same control and menu anchor"),Control(TEXT("mode"))==Mode);
    TestTrue(TEXT("Lua update does not dismiss the dropdown"),Combo->IsOpen());
    TestEqual(TEXT("Other rows update while the dropdown is open"),Label->GetText().ToString(),FString(TEXT("Killed 13 / 50")));
    Snapshot(14,TEXT("Magic"),true);Frame();
    TestTrue(TEXT("Adding a sibling retains the open dropdown"),Control(TEXT("mode"))==Mode&&Combo->IsOpen());
    auto Extra=Control(TEXT("extra"));TestTrue(TEXT("New rows are measured in the update frame"),Extra&&Extra->GetDesiredSize().Y>0);
    Combo->SetIsOpen(false);Frame();
    TestTrue(TEXT("Deferred selection applies when the menu closes"),Combo->GetSelectedItem()&&*Combo->GetSelectedItem()==TEXT("Magic"));
    TestTrue(TEXT("Snapshots do not emit user change events"),P->PluginEvents.IsEmpty());

    // Give the offscreen editor a real focus path without creating an OS window.
    const auto Editor=Find(Input.ToSharedRef(),TEXT("SEditableText"),false);
    FWidgetPath FocusPath;
    TestTrue(TEXT("Input editor has a Slate path"),Editor&&Slate.GeneratePathToWidgetUnchecked(Editor.ToSharedRef(),FocusPath));
    TestTrue(TEXT("Input editor receives fixture focus"),Slate.SetUserFocus(0,FocusPath,EFocusCause::SetDirectly));
    TestTrue(TEXT("Input focus regression fixture active"),Input->HasAnyUserFocusOrFocusedDescendants());
    Input->SetText(FText::FromString(TEXT("Uncommitted draft")));
    Snapshot(15);Frame();
    TestTrue(TEXT("Input control and focus survive updates"),Control(TEXT("filter"))==Filter&&Input->HasAnyUserFocusOrFocusedDescendants());
    TestEqual(TEXT("Typing is not replaced by a server value"),Input->GetText().ToString(),FString(TEXT("Uncommitted draft")));
    TestEqual(TEXT("Focused input does not freeze task progress"),Label->GetText().ToString(),FString(TEXT("Killed 15 / 50")));
    Slate.ClearUserFocus(0);Frame();

    FWidgetPath CapturePath;TestTrue(TEXT("Game view has a Slate path"),Slate.GeneratePathToWidgetUnchecked(GameView,CapturePath));
    TestTrue(TEXT("Game view captures the mouse"),Slate.GetUser(0)->SetCursorCaptor(GameView,CapturePath));
    TestTrue(TEXT("Capture regression fixture active"),Slate.HasAnyMouseCaptor());
    for(int32 Count=16;Count<20;++Count)
    {
        Snapshot(Count);Frame();
        TestEqual(TEXT("Task counts stay current during mouselook"),Label->GetText().ToString(),FString::Printf(TEXT("Killed %d / 50"),Count));
        TestTrue(TEXT("Updating the plugin preserves game mouse capture"),GameView->HasMouseCapture());
    }
    Slate.ReleaseAllPointerCapture();
    Section->SetExpanded(false);Snapshot(20);Frame();
    TestTrue(TEXT("Collapsed section retains its widget"),Find(Panel,TEXT("SExpandableArea"),false)==Section);
    TestFalse(TEXT("Snapshot does not reopen a collapsed section"),Section->IsExpanded());
    TestTrue(TEXT("Removed rows detach without rebuilding surviving controls"),!Control(TEXT("extra"))&&Control(TEXT("kills"))==Kills);
    Section->SetExpanded(true);
    // A genuinely different type replaces just that control, not its siblings.
    auto Rows=P->PanelRows[0]->AsObject()->GetArrayField(TEXT("children"));Rows[0]->AsObject()->SetStringField(TEXT("type"),TEXT("button"));++P->PanelRevision;Frame();
    TestTrue(TEXT("Type change replaces the incompatible control"),Control(TEXT("kills"))!=Kills);
    TestTrue(TEXT("Type change preserves sibling controls"),Control(TEXT("mode"))==Mode&&Control(TEXT("filter"))==Filter);

    // Check the real retained Lua button, including live text updates. These
    // widgets used to stretch their text at the upper-left of the button.
    auto CheckButtonCenter=[&](const TSharedRef<SButton>& Button,const FString& Context)
    {
        const auto& G=Button->GetCachedGeometry();const auto& C=Button->GetContent()->GetCachedGeometry();
        const auto ChildCenter=G.AbsoluteToLocal(C.LocalToAbsolute(C.GetLocalSize()*.5));
        TestTrue(Context+TEXT(" centers content on both axes"),ChildCenter.Equals(G.GetLocalSize()*.5,.1f));
    };
    auto SetPressed=[](const TSharedRef<SButton>& Button,bool Pressed)
    {
        if(!Pressed){Button->OnMouseCaptureLost(FCaptureLostEvent(0,0));return;}
        const auto& G=Button->GetCachedGeometry();const auto At=G.LocalToAbsolute(G.GetLocalSize()*.5);
        Button->OnMouseButtonDown(G,FPointerEvent(0,At,At,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
    };
    auto LuaButton=StaticCastSharedPtr<SButton>(Find(Control(TEXT("kills")).ToSharedRef(),TEXT("SACEPluginButton"),false));
    if(TestTrue(TEXT("Lua actions use the shared plugin button"),LuaButton.IsValid()))
    {
        Frame();CheckButtonCenter(LuaButton.ToSharedRef(),TEXT("Lua action"));
        Rows[0]->AsObject()->SetStringField(TEXT("text"),TEXT("Add selected\nWatch for matches"));++P->PanelRevision;Frame();Frame();Frame();
        CheckButtonCenter(LuaButton.ToSharedRef(),TEXT("Multiline Lua action"));
        SetPressed(LuaButton.ToSharedRef(),true);Frame();CheckButtonCenter(LuaButton.ToSharedRef(),TEXT("Pressed Lua action"));
        SetPressed(LuaButton.ToSharedRef(),false);
    }

    // Exercise the actual retained Lua/companion labels with retail bitmap fonts,
    // not only Slate's fallback font metrics. All rows contain identical text.
    if(!TestTrue(TEXT("Retail DAT fonts available for size regression"),DatReady))return false;
    const auto RetailStyle=FACEPluginStyle::Use(H,P->Id);
    TestNotNull(TEXT("Text size test uses a loaded bitmap atlas"),RetailStyle->Typeface(16)->Atlas.Get());
    const int32 Sizes[]={11,13,15,16,18};P->PanelRows.Reset();
    for(int32 SizeValue:Sizes)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),FString::Printf(TEXT("size%d"),SizeValue));
        Row->SetStringField(TEXT("text"),TEXT("Frost Golems 9/20"));Row->SetNumberField(TEXT("size"),SizeValue);
        P->PanelRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    ++P->PanelRevision;Frame();Frame();
    FVector2D Previous=FVector2D::ZeroVector;
    for(int32 SizeValue:Sizes)
    {
        const auto Row=Control(*FString::Printf(TEXT("size%d"),SizeValue));
        const auto Text=Find(Row.ToSharedRef(),TEXT("SACEPluginText"),false);
        const auto Measured=Text->GetDesiredSize();
        AddInfo(FString::Printf(TEXT("Font size %d: %.2f x %.2f, authored line height %u"),SizeValue,Measured.X,Measured.Y,RetailStyle->Typeface(SizeValue)->Font.MaxCharHeight));
        // Authored DAT faces have different glyph widths; their line height
        // must follow the requested size even across a change of bitmap atlas.
        TestTrue(FString::Printf(TEXT("Size %d increases text height"),SizeValue),Measured.X>0&&Measured.Y>Previous.Y);
        TestTrue(TEXT("Requested points determine bitmap line height"),FMath::IsNearlyEqual(Measured.Y,SizeValue*4.f/3.f,.02f));
        Previous=Measured;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("PluginTextSizeScreenshot")))
    {
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(int32(Size.X),int32(Size.Y),Pixels,Png);
        FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("PluginTextSizes.png")));
    }
    const auto SmallRow=Control(TEXT("size11"));
    const auto SmallText=Find(SmallRow.ToSharedRef(),TEXT("SACEPluginText"),false);
    const auto OriginalSize=SmallText->GetDesiredSize();
    P->PanelRows[0]->AsObject()->SetNumberField(TEXT("size"),24);++P->PanelRevision;Frame();Frame();
    TestTrue(TEXT("Font changes retain the control identity"),Control(TEXT("size11"))==SmallRow);
    TestTrue(TEXT("Live size change refreshes the bitmap face and layout"),SmallText->GetDesiredSize().Y>OriginalSize.Y*2);
    P->PanelRows[0]->AsObject()->SetNumberField(TEXT("size"),11);++P->PanelRevision;Frame();Frame();
    TestTrue(TEXT("Shrinking restores the original metrics"),SmallText->GetDesiredSize().Equals(OriginalSize,.02f));
    // Real Lua tab controls: selection must survive ordinary snapshots and
    // reordering, and have a shape cue independent of bitmap font tinting.
    TSharedPtr<FJsonObject> TabData;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"({"ui":[{"id":"views","type":"tabs","children":[
      {"id":"detected","type":"column","text":"Detected","children":[{"id":"result","text":"Nearby creatures"}]},
      {"id":"settings","type":"column","text":"Settings","children":[{"id":"setting","text":"Detection options"}]}]}]})"))),TabData);
    P->PanelRows=TabData->GetArrayField(TEXT("ui"));++P->PanelRevision;Frame();Frame();
    auto Tab=[&](const TCHAR* Id){return StaticCastSharedPtr<SACEPluginTab>(Find(Panel,FName(*(FString(TEXT("PluginTab.views."))+Id)),true));};
    auto Detected=Tab(TEXT("detected")),Settings=Tab(TEXT("settings"));
    if(!TestTrue(TEXT("Lua tabs use shared selected navigation controls"),Detected&&Settings))return false;
    const auto TabSize=Settings->GetDesiredSize();
    TestTrue(TEXT("First Lua tab is visibly selected on initial draw"),Detected->IsSelected()&&!Settings->IsSelected());
    Settings->SimulateClick();Frame();
    TestTrue(TEXT("Click moves selection to exactly one Lua tab"),Settings->IsSelected()&&!Detected->IsSelected());
    Detected->OnMouseEnter(Detected->GetCachedGeometry(),FPointerEvent());Frame();
    TestTrue(TEXT("Hovering another tab preserves the active tab"),Settings->IsSelected()&&!Detected->IsSelected());
    auto Underline=[&](const TSharedPtr<SACEPluginTab>& Button)
    {
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        const auto& G=Button->GetCachedGeometry();const auto At=G.GetAbsolutePosition()+FVector2D(G.GetLocalSize().X*.5,G.GetLocalSize().Y-2);
        return Pixels[FMath::Clamp(FMath::FloorToInt(At.Y),0,479)*640+FMath::Clamp(FMath::FloorToInt(At.X),0,639)];
    };
    const FColor ActiveLine=Underline(Settings),HoverLine=Underline(Detected);
    TestTrue(TEXT("Selected tab has a bright gold underline unlike a hovered inactive tab"),ActiveLine.R>200&&ActiveLine.G>130&&ActiveLine.B<150&&int32(ActiveLine.R)>int32(HoverLine.R)+80);
    RetailStyle->SetWindowAppearance(0.f,true);Frame();
    TestEqual(TEXT("Pinned/transparent windows retain the selected-tab marker"),Underline(Settings),ActiveLine);
    RetailStyle->SetWindowAppearance(1.f,false);Detected->OnMouseLeave(FPointerEvent());
    auto Pages=P->PanelRows[0]->AsObject()->GetArrayField(TEXT("children"));Pages[1]->AsObject()->SetStringField(TEXT("text"),TEXT("Settings updated"));++P->PanelRevision;Frame();
    TestTrue(TEXT("Live Lua snapshots retain selected tab identity"),Tab(TEXT("settings"))==Settings&&Settings->IsSelected());
    TestEqual(TEXT("Live snapshots update tab labels"),StaticCastSharedPtr<STextBlock>(Find(Settings.ToSharedRef(),TEXT("SACEPluginText"),false))->GetText().ToString(),FString(TEXT("Settings updated")));
    Pages[1]->AsObject()->SetStringField(TEXT("text"),TEXT("Settings"));Pages.Swap(0,1);P->PanelRows[0]->AsObject()->SetArrayField(TEXT("children"),Pages);++P->PanelRevision;Frame();
    Settings=Tab(TEXT("settings"));Detected=Tab(TEXT("detected"));
    TestTrue(TEXT("Reordering tab pages keeps the selected page by ID"),Settings->IsSelected()&&!Detected->IsSelected());
    TestTrue(TEXT("Selected state does not change tab dimensions"),Settings->GetDesiredSize().Equals(TabSize,.01f));
    TArray<FColor> TabPixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(TabPixels);TArray64<uint8> TabPng;
    FImageUtils::PNGCompressImageArray(640,480,TabPixels,TabPng);FFileHelper::SaveArrayToFile(TabPng,*(FPaths::ProjectSavedDir()/TEXT("PluginTabs-Lua.png")));
    Pages.RemoveAt(0);P->PanelRows[0]->AsObject()->SetArrayField(TEXT("children"),Pages);++P->PanelRevision;Frame();
    TestTrue(TEXT("Removing selected page selects a remaining tab"),Tab(TEXT("detected"))->IsSelected());

    // Use the actual Lua/companion renderer: all its labels share the same
    // text widget as native panels. Narrow columns must not add mid-word lines,
    // including after a retained label changes text, size or available width.
    TSharedPtr<FJsonObject> WrapData;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"({"ui":[{"id":"wraprow","type":"row","children":[
      {"id":"word","text":"Summoning"}, {"id":"phrase","text":"Mana stones"},
      {"id":"explicit","text":"Start\nStop"}, {"id":"fallback","text":"Summoning"}]}]})"))),WrapData);
    if(!TestTrue(TEXT("Wrapping fixture parsed"),WrapData.IsValid()))return false;
    P->PanelRows=WrapData->GetArrayField(TEXT("ui"));++P->PanelRevision;
    // Pick a Latin letter absent from this DAT instead of assuming a particular
    // language is missing (retail's font includes Cyrillic and Greek glyphs).
    uint16 FallbackCode=0;
    for(uint16 Code=0x1E00;Code<0x1F00;++Code)
        if(FChar::IsAlpha(TCHAR(Code))&&!RetailStyle->Typeface(16)->Font.IndexByUnicode.Contains(Code)){FallbackCode=Code;break;}
    if(!TestTrue(TEXT("Fallback fixture has an unsupported Latin letter"),FallbackCode!=0))return false;
    P->PanelRows[0]->AsObject()->GetArrayField(TEXT("children"))[3]->AsObject()->SetStringField(TEXT("text"),TEXT("Summ")+FString::Chr(TCHAR(FallbackCode))+TEXT("oning"));
    float PanelWidth=620;
    Window->SetContent(SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
        [SNew(SBox).WidthOverride_Lambda([&PanelWidth]{return PanelWidth;})[Panel]]);
    Frame();Frame();Frame();
    auto WrapLabel=[&](const TCHAR* Id){return StaticCastSharedPtr<STextBlock>(Find(Control(Id).ToSharedRef(),TEXT("SACEPluginText"),false));};
    const auto Word=WrapLabel(TEXT("word")),Phrase=WrapLabel(TEXT("phrase")),Explicit=WrapLabel(TEXT("explicit")),Fallback=WrapLabel(TEXT("fallback"));
    const float LineHeight=Word->GetDesiredSize().Y,FallbackHeight=Fallback->GetDesiredSize().Y;
    TestFalse(TEXT("International label exercises the Slate fallback font"),RetailStyle->Typeface(16)->Font.IndexByUnicode.Contains(FallbackCode));
    TestTrue(TEXT("Wide phrase occupies one native line"),FMath::IsNearlyEqual(float(Phrase->GetDesiredSize().Y),LineHeight,.02f));
    PanelWidth=200;Frame();Frame();Frame();
    TestTrue(TEXT("An oversized native word retains one line"),FMath::IsNearlyEqual(float(Word->GetDesiredSize().Y),LineHeight,.02f));
    TestTrue(TEXT("Multiword labels wrap at the space"),FMath::IsNearlyEqual(float(Phrase->GetDesiredSize().Y),LineHeight*2,.02f));
    TestTrue(TEXT("Explicit line breaks remain intact"),FMath::IsNearlyEqual(float(Explicit->GetDesiredSize().Y),LineHeight*2,.02f));
    TestTrue(TEXT("Fallback font also keeps oversized words together"),FMath::IsNearlyEqual(float(Fallback->GetDesiredSize().Y),FallbackHeight,.02f));
    const auto WrapRows=P->PanelRows[0]->AsObject()->GetArrayField(TEXT("children"));
    WrapRows[0]->AsObject()->SetStringField(TEXT("text"),TEXT("Automatically"));WrapRows[0]->AsObject()->SetNumberField(TEXT("size"),24);++P->PanelRevision;Frame();Frame();Frame();
    TestTrue(TEXT("Live text and font changes keep the same label"),WrapLabel(TEXT("word"))==Word);
    TestTrue(TEXT("Larger updated word stays on one line"),FMath::IsNearlyEqual(float(Word->GetDesiredSize().Y),32.f,.02f));
    PanelWidth=620;Frame();Frame();Frame();
    TestTrue(TEXT("Widening restores multiword labels to one line"),FMath::IsNearlyEqual(float(Phrase->GetDesiredSize().Y),LineHeight,.02f));
    // Detach callbacks that reference fixture-local width before it goes away.
    Window->SetContent(Content);

    // Exercise authored retail faces, fallback glyphs, compact controls and
    // stretched buttons at actual Slate geometry, including the pressed state.
    auto Buttons=SNew(SVerticalBox);TArray<TSharedRef<SACEPluginButton>> Actions;
    const FString Captions[]={TEXT("Add selected"),TEXT("Buy missing supplies"),TEXT("Force Buff\nRefresh all"),
        TEXT("Summ")+FString::Chr(TCHAR(FallbackCode))+TEXT("oning"),TEXT("<"),TEXT("X"),TEXT("+"),TEXT("-")};
    for(int32 I=0;I<UE_ARRAY_COUNT(Captions);++I)
    {
        const float Inset=I>=6?0:I<4?FACEPluginStyle::ButtonPadding:I==4?FACEPluginStyle::CompactButtonPadding:FACEPluginStyle::ChromeButtonPadding;
        auto Button=SNew(SACEPluginButton).ButtonStyle(I==4||I==5?&RetailStyle->DockButton:&RetailStyle->Button).ContentPadding(FMargin(Inset))
            [SNew(SACEPluginText).Text(FText::FromString(Captions[I])).Font(FACEPluginStyle::NativeFont("Regular",I==5?10:15)).AutoWrapText(true)];
        Actions.Add(Button);
        Buttons->AddSlot().AutoHeight().Padding(8).HAlign(I>=4?HAlign_Center:HAlign_Fill)
            [SNew(SBox).MinDesiredHeight(I>=4?20:50)[Button]];
    }
    Window->SetContent(Buttons);
    for(int32 State=0;State<4;++State)
    {
        for(auto Button:Actions)
        {
            SetPressed(Button,State==2);Button->SetEnabled(State!=3);
            if(State==1)Button->OnMouseEnter(Button->GetCachedGeometry(),FPointerEvent());
            else Button->OnMouseLeave(FPointerEvent());
        }
        Frame();Frame();Frame();
        for(auto Button:Actions)
        {
            CheckButtonCenter(Button,FString::Printf(TEXT("Button state %d"),State));
            const auto& G=Button->GetCachedGeometry();const auto& C=Button->GetContent()->GetCachedGeometry();
            const auto TopLeft=G.AbsoluteToLocal(C.GetAbsolutePosition());
            TestTrue(TEXT("Captions fit inside the button border"),TopLeft.X>=.9&&TopLeft.Y>=.9);
            const auto Insets=Button->GetDesiredSize()-Button->GetContent()->GetDesiredSize();
            TestTrue(TEXT("Natural button layout reserves padding on both axes"),Insets.X>=2&&Insets.Y>=2);
            if(Button==Actions[6]||Button==Actions[7])
            {
                TestTrue(TEXT("Single-symbol retail buttons have a usable minimum size"),G.GetLocalSize().X>=28&&G.GetLocalSize().Y>=28);
                TestTrue(TEXT("Zero requested padding cannot put symbols against the retail bevel"),TopLeft.X>=5.9&&TopLeft.Y>=5.9);
            }
        }
        if(State==0)
        {
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);TArray64<uint8> Png;
            FImageUtils::PNGCompressImageArray(int32(Size.X),int32(Size.Y),Pixels,Png);
            FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("PluginButtons.png")));
        }
    }
    Window->SetContent(Content);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginTooltipTest,"ACE.Plugins.Tooltips",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginTooltipTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()||!FSlateApplication::IsInitialized())return true;
    auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT{GI->Shutdown();};
    if(!TestTrue(TEXT("Load inventory tooltip artwork"),GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
    const auto Style=FACEPluginStyle::Use(GI->GetSubsystem<UACEPluginSubsystem>());
    auto* Inventory=NewObject<UBorder>(GI);
    FText HoverText;
    const auto Button=SNew(SACEPluginButton).ButtonStyle(&Style->Button)
        .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([&HoverText]{return HoverText;})));
    const auto PluginTip=Button->GetToolTip();
    TestTrue(TEXT("An empty bound plugin tooltip stays hidden"),PluginTip->IsEmpty());
    auto Render=[&](TSharedRef<SWidget> Widget,const TCHAR* Capture)
    {
        Widget->SlatePrepass();
        const FIntPoint Size(FMath::CeilToInt(Widget->GetDesiredSize().X),FMath::CeilToInt(Widget->GetDesiredSize().Y));
        FWidgetRenderer Renderer(false,true);
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Widget,FVector2D(Size),.016f);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        if(Capture)
        {
            TArray<FColor> Display=Pixels;for(auto& Pixel:Display)Pixel=Pixel.ReinterpretAsLinear().ToFColor(true);
            TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Display,Png);
            FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/Capture));
        }
        Target->ReleaseResource();return Pixels;
    };
    TSharedPtr<SWidget> OriginalContent;
    for(const auto* Text:{TEXT("Steel Salvage (100)"),TEXT("Track selected items across your inventory.\nClick an icon or name to toggle all item names. Resize the window to move the counts.")})
    {
        HoverText=FText::FromString(Text);
        TestFalse(TEXT("Bound hover text becomes available without rebuilding the button"),PluginTip->IsEmpty());
        if(!OriginalContent)PluginTip->OnOpening();
        const auto PluginWidget=PluginTip->AsWidget();
        const auto Content=PluginTip->GetContentWidget();
        if(OriginalContent)TestTrue(TEXT("Changing hover text retains the same retail widget"),Content==OriginalContent);
        else OriginalContent=Content;
        UACEHoverTooltipWidget::SetWidgetTooltip(Inventory,HoverText,UACEHoverTooltipWidget::ObjectTemplate);
        auto* InventoryTip=CastChecked<UACEHoverTooltipWidget>(Inventory->GetToolTip());
        InventoryTip->SetResources(GI->GetSubsystem<UACEClientSubsystem>()->GetUIResourceResolver());
        const auto InventoryWidget=InventoryTip->TakeWidget();
        PluginWidget->SlatePrepass();InventoryWidget->SlatePrepass();
        TestTrue(TEXT("Plugin tooltip uses inventory sizing and padding"),PluginWidget->GetDesiredSize().Equals(InventoryWidget->GetDesiredSize(),.01f));
        TestTrue(TEXT("Long plugin help wraps at inventory tooltip width"),PluginWidget->GetDesiredSize().X<=260);
        const auto PluginPixels=Render(PluginWidget,TEXT("PluginTooltip.png"));
        const auto InventoryPixels=Render(InventoryWidget,TEXT("InventoryTooltip-reference.png"));
        TestTrue(TEXT("Plugin hover pixels exactly match inventory font, background and frame"),PluginPixels==InventoryPixels);
    }
    HoverText=FText::GetEmpty();TestTrue(TEXT("Clearing dynamic hover text hides the tooltip"),PluginTip->IsEmpty());
    PluginTip->OnClosed();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACETooltipCursorClearanceTest,"ACE.RetailParity.TooltipCursorClearance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACETooltipCursorClearanceTest::RunTest(const FString&)
{
    const FString OriginalIni=GGameUserSettingsIni;
    GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/TooltipCursorClearance.ini");
    FConfigFile EmptyConfig;GConfig->SetFile(GGameUserSettingsIni,&EmptyConfig);
    // Offscreen clients may not have received a pointer event. Initialize the
    // virtual cursor before evaluating Slate's pixel-space avoidance bounds.
    const FVector2D OriginalCursor=FSlateApplication::Get().GetCursorPos();
    FSlateApplication::Get().SetCursorPos(FVector2D(400,300));
    ON_SCOPE_EXIT{GGameUserSettingsIni=OriginalIni;FSlateApplication::Get().SetCursorPos(OriginalCursor);};
    // Exercise both UMG construction orders. A tooltip assigned before the
    // inventory icon is built must still install its Slate avoidance region.
    for(bool BuildFirst:{false,true})
    {
        auto* Icon=NewObject<UBorder>();
        if(BuildFirst)Icon->TakeWidget();
        UACEHoverTooltipWidget::SetWidgetTooltip(Icon,FText::FromString(TEXT("Ancient Falatacot Trinkets")),UACEHoverTooltipWidget::ObjectTemplate);
        const auto Widget=Icon->TakeWidget();
        TestTrue(TEXT("Inventory tooltips avoid their owner and cursor"),Widget->HasToolTipForceField());
        const auto Tip=Widget->GetToolTip();
        TestTrue(TEXT("Avoidance does not make the tooltip interactive"),Tip&&!Tip->IsInteractive());
        for(float Scale:{.5f,1.f,1.25f,2.f,3.f})
        {
            ACERuntimeOptions::Set(TEXT("CursorScale"),Scale);
            TestEqual(TEXT("Cursor scale fixture is active"),ACERuntimeOptions::Get(TEXT("CursorScale")),Scale);
            const auto Bounds=Widget->GetToolTipForceFieldExpansion();
            if(!TestTrue(TEXT("UMG cursor avoidance remains bound"),Bounds.IsSet()))return false;
            const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
            TestTrue(FString::Printf(TEXT("Avoidance covers the scaled pointer body, not only its tip (scale %.2f, pointer %.2f,%.2f, bounds %.2f,%.2f,%.2f,%.2f)"),
                Scale,Pointer.X,Pointer.Y,Bounds->Left,Bounds->Top,Bounds->Right,Bounds->Bottom),
                Bounds->Left<=Pointer.X&&Bounds->Top<=Pointer.Y&&Bounds->Right>=Pointer.X+15*Scale&&Bounds->Bottom>=Pointer.Y+28*Scale);
        }
    }
    for(float Scale:{.5f,1.f,1.25f,2.f,3.f})
    {
        ACERuntimeOptions::Set(TEXT("CursorScale"),Scale);
        for(FVector2D Screen:{FVector2D(1024,768),FVector2D(1920,1080),FVector2D(2560,1440)})
        for(float Dpi:{1.f,1.5f,2.f})
        for(FVector2D Point:{FVector2D(2,2),FVector2D(Screen.X-2,2),FVector2D(2,Screen.Y-2),Screen-FVector2D(2,2),Screen*.5})
        {
            const FVector2D Size=FVector2D(240,24)*Dpi;
            const auto Pos=UACEHoverTooltipWidget::CalculateTooltipPosition(Point,Size,Screen,Dpi);
            const FSlateRect TipRect(Pos,Pos+Size);
            TestFalse(TEXT("Screen-edge clamping never puts the name under the scaled cursor"),
                FSlateRect::DoRectanglesIntersect(TipRect,UACEHoverTooltipWidget::GetCursorExclusionRect(Point)));
            TestTrue(TEXT("Complete tooltip stays on screen"),Pos.X>=0&&Pos.Y>=0&&Pos.X+Size.X<=Screen.X&&Pos.Y+Size.Y<=Screen.Y);
        }
    }
    // Test the final Slate popup, not just the owner's force field. Slate can
    // clamp a tooltip back under the cursor after applying that force field.
    if(FApp::CanEverRender())
    {
        auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT{GI->Shutdown();};
        FString DatDirectory=TEXT("C:/Turbine/Asheron's Call");FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),DatDirectory);
        if(!TestTrue(TEXT("Load retail hover and cursor art"),GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(DatDirectory)))return false;
        auto* Resources=GI->GetSubsystem<UACEClientSubsystem>()->GetUIResourceResolver();
        auto* Cursor=NewObject<UACEMouseCursorWidget>();Cursor->Initialize();
        auto* Texture=Resources->ResolveTexture(Resources->ResolveCursorId(1));
        if(!TestNotNull(TEXT("Retail pointer texture"),Texture))return false;
        Cursor->SetCursorTexture(Texture,0,0);
        auto* Viewport=NewObject<UGameViewportClient>(GEngine);Viewport->SetUseSoftwareCursorWidgets(true);
        Viewport->SetSoftwareCursorWidget(EMouseCursor::Default,Cursor);
        const auto OriginalViewport=GEngine->GameViewport;GEngine->GameViewport=Viewport;
        auto& Slate=FSlateApplication::Get();const float OriginalAppScale=Slate.GetApplicationScale();
        ON_SCOPE_EXIT{Viewport->RemoveAllViewportWidgets();GEngine->GameViewport=OriginalViewport;Slate.SetApplicationScale(OriginalAppScale);};
        auto* Owner=NewObject<UBorder>();
        UACEHoverTooltipWidget::SetWidgetTooltip(Owner,FText::FromString(TEXT("Ancient Falatacot Trinkets")),UACEHoverTooltipWidget::ObjectTemplate);
        CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->SetResources(Resources);
        const auto InventoryTooltip=Owner->TakeWidget()->GetToolTip();
        const auto Style=FACEPluginStyle::Use(GI->GetSubsystem<UACEPluginSubsystem>());
        const auto PluginTooltip=FACEPluginStyle::Tooltip(FText::FromString(TEXT("Ancient Falatacot Trinkets")));
        auto Window=SNew(SWindow).Type(EWindowType::ToolTip).SizingRule(ESizingRule::Autosized)
            .CreateTitleBar(false).UseOSWindowBorder(false).IsPopupWindow(true);
        Slate.AddWindow(Window,false);ON_SCOPE_EXIT{Window->RequestDestroyWindow();};
        FWidgetRenderer Renderer(false,true);FHittestGrid HitTestGrid;
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(600,100),TF_Nearest,false);
        ON_SCOPE_EXIT{Target->ReleaseResource();};
        const FSlateRect WorkArea=Slate.GetWorkArea(FSlateRect(400,300,401,301));
        for(const auto& Tip:{InventoryTooltip.ToSharedRef(),PluginTooltip})
        {
            Tip->OnOpening();Window->SetContent(Tip->AsWidget());
            for(float UIScale:{1.f,1.5f,2.f})
            {
                Slate.SetApplicationScale(UIScale);Window->SlatePrepass(UIScale);
                const FVector2D TipSize=Window->GetDesiredSizeDesktopPixels();
                Window->Resize(TipSize); // Slate's 3D renderer skips windows with a zero native size.
                for(float Scale:{.5f,1.f,2.f,3.f})
                for(FVector2D Point:{FVector2D(WorkArea.Left+100,WorkArea.Top+100),
                    FVector2D(WorkArea.Right-100,WorkArea.Top+100),
                    FVector2D(WorkArea.Right-100,WorkArea.Bottom-60),
                    FVector2D(WorkArea.Left+20,WorkArea.Bottom-30)})
                {
                    ACERuntimeOptions::Set(TEXT("CursorScale"),Scale);Cursor->SetCursorScale(Scale);
                    // Leave the cursor layout stale here, as it is when the
                    // setting changes before the cursor's next paint.
                    Slate.SetCursorPos(Point);Point=Slate.GetCursorPos();
                    const auto Initial=Slate.CalculateTooltipWindowPosition(FSlateRect(Point+FVector2D(12,8),Point+FVector2D(12,8)),TipSize,false,EPopupCursorOverlapMode::PreventOverlap);
                    Window->SetCachedScreenPosition(Initial);
                    ++GFrameCounter;Renderer.DrawWindow(Target,HitTestGrid,Window,UIScale,TipSize,.016f);FlushRenderingCommands();
                    const FVector2D Position=Window->GetPositionInScreen();
                    const FSlateRect Bounds(Position,Position+TipSize);
                    // Independently compare with the actual DAT artwork size,
                    // not GetCursorExclusionRect (the implementation under test).
                    const FSlateRect CursorArt(Point,Point+FVector2D(Texture->GetSizeX(),Texture->GetSizeY())*Scale);
                    TestFalse(FString::Printf(TEXT("Final popup clears cursor art at scale %.1f UI %.1f (%.0f,%.0f)"),Scale,UIScale,Point.X,Point.Y),FSlateRect::DoRectanglesIntersect(Bounds,CursorArt));
                    TestTrue(TEXT("Final popup remains inside the monitor work area"),Bounds.Left>=WorkArea.Left&&Bounds.Top>=WorkArea.Top&&Bounds.Right<=WorkArea.Right&&Bounds.Bottom<=WorkArea.Bottom);
                    TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
                    TestTrue(TEXT("Tooltip pixels can be read"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
                    int32 TextPixels=0,FramePixels=0;
                    for(const auto& Pixel:Pixels)
                    {
                        if(Pixel.R>160 && Pixel.G>160 && Pixel.B>160)++TextPixels;
                        if(Pixel.R>100 && Pixel.G>60 && Pixel.R>Pixel.B*1.5f)++FramePixels;
                    }
                    TestTrue(TEXT("Repositioning preserves visible tooltip text"),TextPixels>40);
                    TestTrue(TEXT("Repositioning preserves the gold tooltip frame"),FramePixels>40);
                }
            }
            Tip->OnClosed();
        }
        // SGameLayerManager uses an in-viewport STooltipPresenter, not a native
        // tooltip window. Its fixed 12px placement bypasses Slate force fields.
        // Render that path and compare actual tooltip pixels to cursor artwork.
        Slate.SetApplicationScale(1.f);
        const FVector2D ViewSize(800,600);
        Target->ResizeTarget(800,600);
        auto Presenter=SNew(STooltipPresenter);
        auto DpiRoot=SNew(SDPIScaler)[Presenter];
        auto GameWindow=SNew(SVirtualWindow).Size(ViewSize);
        GameWindow->SetContent(SNew(SBorder).Padding(FMargin(40,25,60,35))
            .BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))[DpiRoot]);
        Slate.RegisterVirtualWindow(GameWindow);
        ON_SCOPE_EXIT{Slate.UnregisterVirtualWindow(GameWindow);};
        auto DrawGame=[&]
        {
            ++GFrameCounter;Renderer.DrawWindow(Target,HitTestGrid,GameWindow,1.f,ViewSize,.016f);FlushRenderingCommands();
        };
        for(const auto& Tip:{InventoryTooltip.ToSharedRef(),PluginTooltip})
        {
            Tip->OnOpening();Presenter->SetContent(Tip->AsWidget());
            for(float UIScale:{.75f,1.f,1.5f,2.f})
            {
                DpiRoot->SetDPIScale(UIScale);DrawGame();
                const FSlateRect View=Presenter->GetCachedGeometry().GetLayoutBoundingRect();
                for(float Scale:{.5f,1.f,1.25f,2.f,3.f})
                for(FVector2D Point:{FVector2D(View.Left+8,View.Top+8),FVector2D(View.Right-8,View.Top+8),
                    FVector2D(View.Left+8,View.Bottom-8),FVector2D(View.Right-8,View.Bottom-8),FVector2D(View.GetCenter())})
                {
                    ACERuntimeOptions::Set(TEXT("CursorScale"),Scale);Cursor->SetCursorScale(Scale);
                    Slate.SetCursorPos(Point);Point=Slate.GetCursorPos();DrawGame();
                    TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
                    if(!TestTrue(TEXT("Read in-game tooltip pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))return false;
                    const FSlateRect Art(Point,Point+FVector2D(Texture->GetSizeX(),Texture->GetSizeY())*Scale);
                    int32 Colored=0,Overlapping=0,Outside=0;
                    for(int32 Y=0;Y<600;++Y)for(int32 X=0;X<800;++X)
                    {
                        const auto& Pixel=Pixels[Y*800+X];
                        if(Pixel.R<80 && Pixel.G<80 && Pixel.B<80)continue;
                        ++Colored;const FVector2D P(X+.5,Y+.5);
                        Overlapping+=Art.ContainsPoint(P);Outside+=!View.ContainsPoint(P);
                    }
                    TestTrue(TEXT("In-game tooltip has rendered text and frame"),Colored>40);
                    TestEqual(FString::Printf(TEXT("In-game tooltip clears cursor: UI %.2f cursor %.2f at %.0f,%.0f"),
                        UIScale,Scale,Point.X,Point.Y),Overlapping,0);
                    TestEqual(TEXT("In-game tooltip stays inside its viewport"),Outside,0);
                }
            }
            Presenter->SetContent(nullptr);Tip->OnClosed();
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginFontRasterTest,"ACE.Plugins.FontRaster",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginFontRasterTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()||!FSlateApplication::IsInitialized())return true;
    auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT{GI->Shutdown();};
    FString Directory=TEXT("C:/Turbine/Asheron's Call");FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    if(!TestTrue(TEXT("Load bitmap fonts for thin-stroke rendering"),GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(Directory)))return false;
    const auto Style=FACEPluginStyle::Use(GI->GetSubsystem<UACEPluginSubsystem>());
    auto Canvas=SNew(SConstraintCanvas);
    const int32 Sizes[]={11,13,15,16,18,24};
    for(int32 Row=0;Row<UE_ARRAY_COUNT(Sizes);++Row)
    {
        for(int32 I=0;I<16;++I)
            Canvas->AddSlot().Offset(FMargin(16+I*16+I/16.f,16+Row*56,14,44)).Alignment(FVector2D::ZeroVector)
                [SNew(SACEPluginText).Text(FText::FromString(TEXT("l"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",Sizes[Row])).ColorAndOpacity(FLinearColor::White)];
        Canvas->AddSlot().Offset(FMargin(310,8+Row*56,470,44)).Alignment(FVector2D::ZeroVector)
            [SNew(SButton).ButtonStyle(&Style->Button).HAlign(HAlign_Left).ContentPadding(FMargin(8,4))
                [SNew(SACEPluginText).Text(FText::FromString(FString::Printf(TEXT("%d: Add selected | little details"),Sizes[Row])))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular",Sizes[Row])).ColorAndOpacity(FLinearColor::White)]];
    }
    FWidgetRenderer Renderer(false,true);
    for(float UIScale:{.75f,1.f,1.25f,1.5f})
    {
        const FVector2D Size=FVector2D(800,352)*UIScale;
        auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);Target->ClearColor=FLinearColor::Transparent;
        ON_SCOPE_EXIT{Target->ReleaseResource();};
        ++GFrameCounter;Renderer.DrawWidget(Target,Canvas,UIScale,Size,0);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
        if(!TestTrue(TEXT("Read plugin glyph coverage"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))return false;
        for(int32 Row=0;Row<UE_ARRAY_COUNT(Sizes);++Row)
        {
            uint64 Least=MAX_uint64,Most=0;
            for(int32 I=0;I<16;++I)
            {
                uint64 Coverage=0;
                const double X=(16+I*16+I/16.)*UIScale,Y=(16+Row*56)*UIScale;
                for(int32 Py=FMath::FloorToInt(Y);Py<FMath::CeilToInt(Y+44*UIScale);++Py)
                    for(int32 Px=FMath::FloorToInt(X);Px<FMath::CeilToInt(X+14*UIScale);++Px)Coverage+=Pixels[Py*int32(Size.X)+Px].A;
                Least=FMath::Min(Least,Coverage);Most=FMath::Max(Most,Coverage);
            }
            AddInfo(FString::Printf(TEXT("Size %d UI %.2f: thin-stroke coverage %llu..%llu (%.3f)"),Sizes[Row],UIScale,Least,Most,double(Least)/FMath::Max(uint64(1),Most)));
            // Subpixel strokes naturally vary below native size, but position
            // must not halve their coverage as rounded scaled quads used to do.
            const double MinimumRatio=UIScale<1.f?.62:.82;
            TestTrue(*FString::Printf(TEXT("Size %d at UI %.2f retains thin strokes across pixel phases"),Sizes[Row],UIScale),Most>0&&Least>=Most*MinimumRatio);
        }
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(int32(Size.X),int32(Size.Y),Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("PluginFontRaster-%d.png"),FMath::RoundToInt(UIScale*100))));
    }
    return true;
}
#endif
