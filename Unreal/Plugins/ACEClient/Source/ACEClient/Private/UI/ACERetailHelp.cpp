#include "ACEPlayerController.h"
#include "ACEDatSubsystem.h"
#include "ACEInputBindings.h"
#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"

namespace
{
// Retail Keystone articles use UTF-16 KSML. Display the body as text, leaving
// markup, scripts and external resources out of the game viewport.
FString ReadHelpBody(const FString& Path)
{
    FString Source;
    if (!FFileHelper::LoadFileToString(Source, *Path)) return {};
    const int32 Body = Source.Find(TEXT("<body>"));
    const int32 End = Source.Find(TEXT("</body>"));
    if (Body == INDEX_NONE || End <= Body) return {};
    FString Text;
    for (int32 I = Body + 6; I < End; ++I)
    {
        if (Source[I] == '<')
        {
            const int32 Close = Source.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I + 1);
            if (Close == INDEX_NONE || Close >= End) break;
            const FString Tag = Source.Mid(I + 1, Close - I - 1).TrimStartAndEnd().ToLower();
            if (Tag.StartsWith(TEXT("br")) || Tag == TEXT("/p") || Tag == TEXT("/h2")) Text += TEXT("\n");
            I = Close;
        }
        else if (!FChar::IsWhitespace(Source[I])) Text.AppendChar(Source[I]);
        else if (!Text.IsEmpty() && !FChar::IsWhitespace(Text[Text.Len() - 1])) Text.AppendChar(' ');
    }
    Text.ReplaceInline(TEXT("&lt;"), TEXT("<"));
    Text.ReplaceInline(TEXT("&gt;"), TEXT(">"));
    Text.ReplaceInline(TEXT("&quot;"), TEXT("\""));
    Text.ReplaceInline(TEXT("&nbsp;"), TEXT(" "));
    Text.ReplaceInline(TEXT("&amp;"), TEXT("&"));
    return Text.TrimStartAndEnd();
}

class SACERetailHelp : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SACERetailHelp) {}
        SLATE_ARGUMENT(FString, Directory)
        SLATE_EVENT(FOnClicked, OnClose)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        SetVisibility(EVisibility::SelfHitTestInvisible);
        Directory = Args._Directory;
        Topics.Add(MakeShared<FString>(TEXT("Getting started")));
        Topics.Add(MakeShared<FString>(TEXT("Current key bindings")));
        TArray<FString> Files;
        if (!Directory.IsEmpty()) IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.ksml")), true, false);
        Files.Sort();
        for (const FString& File : Files)
            if (!File.StartsWith(TEXT("ac-"))) Topics.Add(MakeShared<FString>(FPaths::GetBaseFilename(File)));
        Style = FACEPluginStyle::Current();
        ChildSlot.HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
          [ SNew(SBox).WidthOverride(510).HeightOverride(390)
            [ SNew(SBorder).BorderImage(&Style->Frame).Padding(4)
              [ SNew(SBorder).BorderImage(&Style->Background).Padding(12)
                [ SNew(SVerticalBox)
                  + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
                  [ SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [ SNew(SACEPluginText).Text(FText::FromString(TEXT("Asheron's Call Help"))) ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [ SNew(SACEPluginButton).ButtonStyle(&Style->Button).OnClicked(Args._OnClose)
                      [ SNew(SACEPluginText).Text(FText::FromString(TEXT("Close"))) ] ] ]
                  + SVerticalBox::Slot().AutoHeight()
                  [ SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Topics).InitiallySelectedItem(Topics[0])
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
                    { return SNew(SACEPluginText).Text(FText::FromString(*Item)); })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item,ESelectInfo::Type){ if (Item && Article) ShowTopic(Item); })
                    [ SAssignNew(Title,SACEPluginText) ] ]
                  + SVerticalBox::Slot().FillHeight(1).Padding(0,10,0,0)
                  [ SAssignNew(Scroll,SScrollBox) + SScrollBox::Slot()
                    [ SAssignNew(Article,SACEPluginText).AutoWrapText(true) ] ] ] ] ] ] ];
        ShowTopic(Topics[0]);
    }
private:
    void ShowTopic(const TSharedPtr<FString>& Topic)
    {
        FString Body;
        if (Topic == Topics[0])
            Body = TEXT("Click an object to select it. Double-click to use it, or right-click to examine it. Drag items to move them.\n\n")
                   TEXT("Choose Current key bindings to see your controls. You can change them or import a retail .keymap file in Options under Keyboard.\n\n")
                   TEXT("Type @help for client commands, @house_help for housing, or @acehelp for server commands.");
        else if (Topic == Topics[1])
        {
            for (const auto& Action : ACEInputBindings::Actions())
            {
                TArray<FString> Keys;
                for (int32 Slot=0; Slot<3; ++Slot)
                {
                    const FInputChord Chord=ACEInputBindings::Get(Action.Key,Slot);
                    if (Chord.Key.IsValid()) Keys.Add(Chord.GetInputText().ToString());
                }
                if (!Keys.IsEmpty()) Body += FString(Action.Label) + TEXT("    ") + FString::Join(Keys,TEXT(" / ")) + TEXT("\n");
            }
        }
        else
        {
            Body = ReadHelpBody(Directory / (*Topic + TEXT(".ksml")));
            if (Body.IsEmpty()) Body = TEXT("This help topic could not be read.");
        }
        Title->SetText(FText::FromString(*Topic));
        Article->SetText(FText::FromString(Body));
        Scroll->ScrollToStart();
    }
    FString Directory;
    TSharedPtr<FACEPluginStyle> Style;
    TArray<TSharedPtr<FString>> Topics;
    TSharedPtr<STextBlock> Title, Article;
    TSharedPtr<SScrollBox> Scroll;
};
}

void AACEPlayerController::OpenClientHelp()
{
    auto* GI = GetGameInstance();
    auto* Viewport = GI ? GI->GetGameViewportClient() : nullptr;
    if (!Viewport) return;
    CloseClientHelp();
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (auto* Plugins = GI->GetSubsystem<UACEPluginSubsystem>()) FACEPluginStyle::Use(Plugins);
    ClientHelpPanel = SNew(SACERetailHelp)
        .Directory(Dat ? Dat->GetDatDirectory() / TEXT("helpcontent") : FString())
        .OnClose(FOnClicked::CreateWeakLambda(this,[this]{CloseClientHelp();return FReply::Handled();}));
    Viewport->AddViewportWidgetContent(ClientHelpPanel.ToSharedRef(),1610);
}

bool AACEPlayerController::CloseClientHelp()
{
    if (!ClientHelpPanel) return false;
    if (auto* GI = GetGameInstance())
        if (auto* Viewport = GI->GetGameViewportClient()) Viewport->RemoveViewportWidgetContent(ClientHelpPanel.ToSharedRef());
    ClientHelpPanel.Reset();
    return true;
}

void AACEPlayerController::PollClientUtilityKeys()
{
    if (ACEInputBindings::Pressed(this,ACEInputBindings::Action(TEXT("ToggleHelp")))) OpenClientHelp();
    if (auto* GI = GetGameInstance(); GI && ACEInputBindings::Pressed(this,ACEInputBindings::Action(TEXT("TogglePluginManager"))))
        if (auto* Plugins = GI->GetSubsystem<UACEPluginSubsystem>()) Plugins->TogglePanel();
}
