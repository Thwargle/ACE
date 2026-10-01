#include "VR/ACEVRChat.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "ACEVRNativeHUD.h"
#include "ACEVRUIStyle.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEChatEntry.h"
#include "ACEClientSubsystem.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
TSharedRef<STextBlock> ChatLabel(const FString& Text, FLinearColor Color=ACEVRUIStyle::TextColor)
{
    return SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",24))
        .ColorAndOpacity(Color).AutoWrapText(true);
}
}
void UACEVRChat::InitializeChat(UACEVRComponent* InRig,UACEUIGameplayBinder* InBinder)
{
    Rig=InRig;Binder=InBinder;
    Entry=NewObject<UACEChatEntry>(this);Entry->InitializeChat(Binder);
    auto Style=Entry->GetWidgetStyle();Style.SetFont(FCoreStyle::GetDefaultFontStyle("Regular",26));
    Entry->SetWidgetStyle(Style);Entry->SetForegroundColor(FLinearColor::Black);
    Entry->SetHintText(FText::FromString(TEXT("Message or /command")));
    Entry->OnTextCommitted.AddDynamic(this,&UACEVRChat::Committed);
    for(int32 I=0;I<14;++I)Channels.Add(MakeShared<int32>(I));
}
UWidget* UACEVRChat::GetEntry()const{return Entry;}
FString UACEVRChat::ChannelLabel(int32 Channel)const
{
    static const TCHAR* Names[]={TEXT("Say"),TEXT("Fellowship"),TEXT("Allegiance"),TEXT("Vassals"),TEXT("Patron"),TEXT("Monarch"),
        TEXT("Co-Vassals"),TEXT("General"),TEXT("Trade"),TEXT("LFG"),TEXT("Roleplay"),TEXT("Society"),TEXT("Tell selected"),TEXT("Olthoi")};
    if(Channel==12 && Binder && Binder->Client)
        return FString::Printf(TEXT("Tell: %s"),*Binder->Client->GetSelectedObject().Name);
    return Channel>=0 && Channel<14?Names[Channel]:Names[0];
}
TSharedRef<SWidget> UACEVRChat::RebuildWidget()
{
    auto Button=[](const FString& Name,TFunction<void()> Click,TSharedPtr<STextBlock>* StoredLabel=nullptr)
    {
        auto Label=ChatLabel(Name);
        if(StoredLabel)*StoredLabel=Label;
        return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(10,6)).OnClicked_Lambda([Click](){Click();return FReply::Handled();})[Label];
    };
    Revision=MAX_uint64;DisplayedLines.Reset();DisplayedFilterMode=INDEX_NONE;DisplayedAnchorMode=INDEX_NONE;
    return ACEVRUIStyle::Frame(SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[ChatLabel(TEXT("Chat"))]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Filter: All"),[this](){if(Binder)Binder->CycleChatFilterMode();},&FilterLabel)]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Clear"),[this](){if(Binder)Binder->ClearChatLog();})]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Close"),[this](){if(Rig)Rig->ToggleChat();})]]
        +SVerticalBox::Slot().FillHeight(1).Padding(0,10)[SAssignNew(Log,SScrollBox)
            .OnUserScrolled_Lambda([this](float Offset){bStickToBottom=Log && Offset>=Log->GetScrollOffsetOfEnd()-4;})
            +SScrollBox::Slot()[SAssignNew(Lines,SVerticalBox)]]
        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&Channels).IsFocusable(false)
                .OnGenerateWidget_Lambda([this](TSharedPtr<int32> C){return ChatLabel(ChannelLabel(*C));})
                .OnSelectionChanged_Lambda([this](TSharedPtr<int32> C,ESelectInfo::Type){if(Binder && C)Binder->SetChatSendChannel(*C);})
                [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(ChannelLabel(Binder?Binder->ChatSendChannel:0));})
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular",24)).ColorAndOpacity(ACEVRUIStyle::TextColor)]]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Latest"),[this](){bStickToBottom=true;if(Log)Log->ScrollToEnd();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SBox).MinDesiredHeight(56)[Entry->TakeWidget()]]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Send"),[this](){Send();})]]
        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Previous"),[this](){Entry->NavigateHistory(true);})]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Next"),[this](){Entry->NavigateHistory(false);})]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Anchor: Head"),[this](){if(Rig)Rig->ChangeSetting("PinChat");},&AnchorLabel)]]
        +SVerticalBox::Slot().AutoHeight()[SAssignNew(Controls,SACEVRPanelControls).Rig(Rig).Panel("Chat")],12,true);
}
bool UACEVRChat::Refresh()
{
    bool Changed=Controls && Controls->Refresh();
    const int32 FilterMode=Binder?FMath::Clamp(Binder->ChatFilterMode,0,3):0;
    if(FilterLabel && DisplayedFilterMode!=FilterMode)
    {
        static const TCHAR* Names[]={TEXT("All"),TEXT("Speech"),TEXT("Combat"),TEXT("System")};
        DisplayedFilterMode=FilterMode;FilterLabel->SetText(FText::FromString(FString::Printf(TEXT("Filter: %s"),Names[FilterMode])));Changed=true;
    }
    const int32 AnchorMode=Rig && Rig->GetSettings()?FMath::Clamp(Rig->GetSettings()->ChatAnchorMode,0,2):0;
    if(AnchorLabel && DisplayedAnchorMode!=AnchorMode)
    {
        static const TCHAR* Names[]={TEXT("Head"),TEXT("Body"),TEXT("World")};
        DisplayedAnchorMode=AnchorMode;AnchorLabel->SetText(FText::FromString(FString::Printf(TEXT("Anchor: %s"),Names[AnchorMode])));Changed=true;
    }
    if(!Binder || !Lines || Revision==Binder->ChatDisplayRevision)return Changed;
    Revision=Binder->ChatDisplayRevision;
    const float Offset=Log->GetScrollOffset();
    const auto& History=Binder->ChatDisplayLines[0];
    if(History.IsEmpty()){Lines->ClearChildren();DisplayedLines.Reset();}
    else while(!DisplayedLines.IsEmpty() && DisplayedLines[0]<History[0].Serial)
    {
        Lines->RemoveSlot(Lines->GetChildren()->GetChildAt(0));
        DisplayedLines.RemoveAt(0,1,EAllowShrinking::No);
    }
    for(const auto& Line:History)
    {
        if(!DisplayedLines.IsEmpty() && Line.Serial<=DisplayedLines.Last())continue;
        DisplayedLines.Add(Line.Serial);
        auto Row=ChatLabel(Line.Text,Line.Color);
        // The 760px panel leaves 700px after frame and scrollbar padding.
        // Auto wrapping learns its width during paint, leaving a new row's
        // height stale until a second redraw when idle chat only requests one.
        Row->SetAutoWrapText(false);Row->SetWrapTextAt(700.f);
        if(Line.Sender.IsEmpty())
        {
            Lines->AddSlot().AutoHeight().Padding(0,3)[Row];
        }
        else
        {
            Lines->AddSlot().AutoHeight().Padding(0,3)[SNew(SButton).IsFocusable(false).ContentPadding(0)
                .ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
                .OnClicked_Lambda([this,Sender=Line.Sender]()
                {
                    Entry->SetChatText(FString::Printf(TEXT("@tell %s, "),*Sender));
                    if(Rig)Rig->FocusTextEntry(Entry);
                    return FReply::Handled();
                })[Row]];
        }
    }
    if(bStickToBottom)Log->ScrollToEnd();else Log->SetScrollOffset(Offset);
    return true;
}
void UACEVRChat::Committed(const FText&,ETextCommit::Type Method){if(Method==ETextCommit::OnEnter)Send();}
void UACEVRChat::Send()
{
    if(!Binder || !Entry)return;
    const FString Message=Entry->GetText().ToString();
    if(Message.TrimStartAndEnd().IsEmpty())return;
    if(Binder->TrySendChatFromEntry(&Message))
    {
        Entry->RememberSubmitted(Message);Entry->SetChatText(TEXT(""));bStickToBottom=true;
        // Native text completion owns dismissal; never focus the hidden desktop chat entry.
        Binder->CancelPendingChatRefocus();
    }
}
void UACEVRChat::ReleaseSlateResources(bool Children)
{Super::ReleaseSlateResources(Children);Log.Reset();Lines.Reset();Controls.Reset();FilterLabel.Reset();AnchorLabel.Reset();DisplayedLines.Reset();}
