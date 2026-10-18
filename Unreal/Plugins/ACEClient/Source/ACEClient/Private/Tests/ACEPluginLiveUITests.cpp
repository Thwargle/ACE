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
    return true;
}
#endif
