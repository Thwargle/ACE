#pragma once
#include "Components/EditableTextBox.h"
#include "UI/ACERetailTextEntry.h"
#include "UI/ACEChatEntry.h"
#include "Widgets/Input/IVirtualKeyboardEntry.h"

void ACEVRUpdateNativeKeyboardText(const FString& Expected, const FString& Replacement);

// One native editing session per explicit click. Android retains a weak entry
// identity after some dismissals; reusing Slate's permanent entry toggles Hide.
class FACEVRPlatformTextEntry final : public IVirtualKeyboardEntry
{
public:
    ~FACEVRPlatformTextEntry();
    void EnableNativeSubmit();
    FACEVRPlatformTextEntry(UWidget* InEntry, TFunction<void()> InFinished)
        : Entry(InEntry), Finished(MoveTemp(InFinished)) { Original = GetText(); }
    void SetTextFromVirtualKeyboard(const FText& Text, ETextEntryType Type) override
    {
        if (bFinished || !Entry.IsValid()) return;
        const FText Value = Type == ETextEntryType::TextEntryCanceled ? Original : Text;
        if (auto* Chat = Cast<UACEChatEntry>(Entry.Get())) Chat->ApplyNativeText(Value, Type == ETextEntryType::TextEntryCanceled);
        else if (auto* Box = Cast<UEditableTextBox>(Entry.Get())) Box->SetText(Value);
        if (auto* Retail = Cast<UACERetailTextEntry>(Entry.Get())) Retail->SetText(Value);
        // Reply expansion must update the native edit buffer as well as Slate.
        // Do not reopen the keyboard: UE treats that as a request to hide it.
        if (Type == ETextEntryType::TextEntryUpdated && !GetText().EqualTo(Value))
            ACEVRUpdateNativeKeyboardText(Value.ToString(), GetText().ToString());
        if (Type != ETextEntryType::TextEntryUpdated)
        {
            bFinished = true;
            const auto Commit = Type == ETextEntryType::TextEntryAccepted ? ETextCommit::OnEnter : ETextCommit::OnCleared;
            if (auto* Box = Cast<UEditableTextBox>(Entry.Get())) Box->OnTextCommitted.Broadcast(Box->GetText(), Commit);
            if (auto* Retail = Cast<UACERetailTextEntry>(Entry.Get())) Retail->Commit(Commit);
            if (Finished) Finished();
        }
    }
    void SetSelectionFromVirtualKeyboard(int, int) override {}
    bool GetSelection(int& Start, int& End) override { Start = End = GetText().ToString().Len(); return true; }
    FText GetText() const override
    {
        if (auto* Box = Cast<UEditableTextBox>(Entry.Get())) return Box->GetText();
        if (auto* Retail = Cast<UACERetailTextEntry>(Entry.Get())) return Retail->GetText();
        return FText::GetEmpty();
    }
    FText GetHintText() const override { return FText::GetEmpty(); }
    EKeyboardType GetVirtualKeyboardType() const override
    {
        if (const auto* Box = Cast<UEditableTextBox>(Entry.Get()); Box && Box->GetIsPassword()) return Keyboard_Password;
        if (const auto* Retail = Cast<UACERetailTextEntry>(Entry.Get()); Retail && Retail->bDigitsOnly) return Keyboard_Number;
        return Keyboard_Default;
    }
    FVirtualKeyboardOptions GetVirtualKeyboardOptions() const override { return {}; }
    bool IsMultilineEntry() const override
    { const auto* Retail = Cast<UACERetailTextEntry>(Entry.Get()); return Retail && Retail->bMultiline; }
private:
    TWeakObjectPtr<UWidget> Entry;
    FText Original;
    TFunction<void()> Finished;
    bool bFinished = false;
    TSharedPtr<class IInputProcessor> SubmitInput;
};
