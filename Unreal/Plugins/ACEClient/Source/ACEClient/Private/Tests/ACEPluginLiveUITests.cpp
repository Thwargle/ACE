#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "Mods/ACEPluginStyle.h"
#include "ACESession.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
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
#include "Input/HittestGrid.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

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
          {"id":"kills","text":"Killed %d / 50"},
          {"id":"mode","type":"select","text":"Mode","value":"%s","options":["Melee","Magic"]},
          {"id":"filter","type":"input","text":"Filter","value":"Server value"}%s]}]})"),Kills,Value,
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

    Combo->SetIsOpen(true);TestTrue(TEXT("Dropdown starts open"),Combo->IsOpen());
    Snapshot(13,TEXT("Magic"));Frame();
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
