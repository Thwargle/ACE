#include "UI/ACEChatEntry.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Widgets/Input/SEditableTextBox.h"

void UACEChatEntry::InitializeChat(UACEUIGameplayBinder* InBinder)
{
    Binder = InBinder;
    OnTextChanged.AddUniqueDynamic(this, &UACEChatEntry::HandleChanged);
}

TSharedRef<SWidget> UACEChatEntry::RebuildWidget()
{
    const auto Widget = Super::RebuildWidget();
    MyEditableTextBlock->SetOnKeyDownHandler(FOnKeyDown::CreateUObject(this, &UACEChatEntry::HandleChatKey));
    return Widget;
}

FReply UACEChatEntry::HandleChatKey(const FGeometry&, const FKeyEvent& Event)
{
    if (!Event.IsControlDown() && !Event.IsAltDown() && !Event.IsShiftDown()
        && (Event.GetKey() == EKeys::Up || Event.GetKey() == EKeys::Down))
    {
        NavigateHistory(Event.GetKey() == EKeys::Up);
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

void UACEChatEntry::SetChatText(const FString& Value)
{
    TGuardValue<bool> Setting(bSettingText, true);
    SetText(FText::FromString(Value));
    if (MyEditableTextBlock)
    {
        MyEditableTextBlock->ClearSelection();
        MyEditableTextBlock->GoTo(ETextLocation::EndOfDocument);
    }
}

void UACEChatEntry::RememberSubmitted(const FString& Message)
{
    // ChatInterface::ProcessCommand stores every nonempty submission, including
    // repeated commands. Each retail chat window owns its own 100-entry history.
    if (!Message.IsEmpty()) History.Add(Message);
    if (History.Num() > 100) History.RemoveAt(0, History.Num() - 100);
    HistoryPosition = INDEX_NONE;
    ShortcutPrefix.Reset(); ExpandedPrefix.Reset();
}

void UACEChatEntry::ApplyNativeText(const FText& Value, bool RestoreDraft)
{
    if (RestoreDraft)
    {
        ShortcutPrefix.Reset(); ExpandedPrefix.Reset();
        SetChatText(Value.ToString());
        return;
    }
    // UEditableTextBox::SetText is a programmatic change and does not issue
    // OnTextChanged. Native keyboard callbacks must enter the same editing path.
    SetText(Value);
    HandleChanged(Value);
}

void UACEChatEntry::NavigateHistory(bool Previous)
{
    if (Previous)
    {
        if (History.IsEmpty()) return;
        HistoryPosition = HistoryPosition == INDEX_NONE ? History.Num() - 1 : FMath::Max(0, HistoryPosition - 1);
    }
    else if (HistoryPosition != INDEX_NONE && ++HistoryPosition >= History.Num()) HistoryPosition = INDEX_NONE;
    ShortcutPrefix.Reset(); ExpandedPrefix.Reset();
    // Retail Down clears a live draft too; it does not wrap or resend a line.
    SetChatText(HistoryPosition == INDEX_NONE ? FString() : History[HistoryPosition]);
}

void UACEChatEntry::HandleChanged(const FText& Value)
{
    if (bSettingText || !Binder.IsValid()) return;
    const FString Input = Value.ToString();
    if (!ShortcutPrefix.IsEmpty() && Input.StartsWith(ShortcutPrefix, ESearchCase::IgnoreCase))
    {
        SetChatText(ExpandedPrefix + Input.Mid(ShortcutPrefix.Len()));
        return;
    }
    if (!ExpandedPrefix.IsEmpty() && Input.StartsWith(ExpandedPrefix)) return;
    ShortcutPrefix.Reset(); ExpandedPrefix.Reset();
    const FString Expanded = Binder->ExpandChatReply(Input);
    if (Expanded == Input) return;
    int32 Boundary = 0;
    while (Boundary < Input.Len() && FChar::IsWhitespace(Input[Boundary])) ++Boundary;
    while (Boundary < Input.Len() && !FChar::IsWhitespace(Input[Boundary])) ++Boundary;
    ++Boundary; // Include the delimiter that triggered expansion.
    ShortcutPrefix = Input.Left(Boundary);
    ExpandedPrefix = Expanded.Left(Expanded.Len() - (Input.Len() - Boundary));
    SetChatText(Expanded);
}
