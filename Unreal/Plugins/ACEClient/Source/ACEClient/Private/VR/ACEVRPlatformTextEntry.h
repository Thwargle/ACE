#pragma once
#include "Components/EditableTextBox.h"
#include "UI/ACERetailTextEntry.h"
#include "UI/ACEChatEntry.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUICharGenBinder.h"
#include "Widgets/Input/IVirtualKeyboardEntry.h"

void ACEVRUpdateNativeKeyboardText(const FString& Expected, const FString& Replacement);

// One native editing session per explicit click. Android retains a weak entry
// identity after some dismissals; reusing Slate's permanent entry toggles Hide.
class FACEVRPlatformTextEntry final : public IVirtualKeyboardEntry, public TSharedFromThis<FACEVRPlatformTextEntry>
{
public:
    ~FACEVRPlatformTextEntry();
    void EnableNativeSubmit();
    void Cancel() { bFinished = true; }
    bool HasFinished() const { return bFinished; }
    FACEVRPlatformTextEntry(UWidget* InEntry, TFunction<void()> InFinished)
        : Entry(InEntry), Finished(MoveTemp(InFinished))
    {
        if (auto* Canvas = Cast<UACEUICanvasWidget>(InEntry))
        { bCharacterName = true; CharacterCreator = Canvas->GetCharGenBinder(); }
        Original = GetText();
    }
    void SetTextFromVirtualKeyboard(const FText& Text, ETextEntryType Type) override
    {
        if (bFinished || !Entry.IsValid()) return;
        // A late IME callback must not edit a different creator on a reused
        // canvas, a hidden page, or a draft already submitted to the server.
        if (bCharacterName && (!CharacterCreator.IsValid() || !CharacterCreator->CanEditName()
            || Cast<UACEUICanvasWidget>(Entry.Get())->GetCharGenBinder() != CharacterCreator.Get())) return;
        // Text/commit delegates may dismiss their owner and drop its last
        // reference. Keep this session alive, and mark a terminal callback
        // before any delegate can reenter dismissal or native text handling.
        const auto KeepAlive = AsShared();
        const bool bCompleting = Type != ETextEntryType::TextEntryUpdated;
        if (bCompleting) bFinished = true;
        const FText Value = Type == ETextEntryType::TextEntryCanceled ? Original : Text;
        if (auto* Chat = Cast<UACEChatEntry>(Entry.Get())) Chat->ApplyNativeText(Value, Type == ETextEntryType::TextEntryCanceled);
        else if (auto* Box = Cast<UEditableTextBox>(Entry.Get()))
        {
            const bool bChanged = !Box->GetText().EqualTo(Value);
            Box->SetText(Value);
            // SetText changes the UMG value before Slate's callback, so UMG
            // suppresses OnTextChanged. Native edits still need that delegate
            // for live consumers such as the spellbook search filter.
            if (bChanged) Box->OnTextChanged.Broadcast(Value);
        }
        if (auto* Retail = Cast<UACERetailTextEntry>(Entry.Get())) Retail->SetText(Value);
        if (bCharacterName) CharacterCreator->SetNameFromKeyboard(Value.ToString());
        // Reply expansion must update the native edit buffer as well as Slate.
        // Do not reopen the keyboard: UE treats that as a request to hide it.
        if (Type == ETextEntryType::TextEntryUpdated && !GetText().EqualTo(Value))
            ACEVRUpdateNativeKeyboardText(Value.ToString(), GetText().ToString());
        if (bCompleting)
        {
            auto OnFinished = MoveTemp(Finished);
            const auto Commit = Type == ETextEntryType::TextEntryAccepted ? ETextCommit::OnEnter : ETextCommit::OnCleared;
            if (auto* Box = Cast<UEditableTextBox>(Entry.Get())) Box->OnTextCommitted.Broadcast(Box->GetText(), Commit);
            if (auto* Retail = Cast<UACERetailTextEntry>(Entry.Get())) Retail->Commit(Commit);
            if (bCharacterName && Commit == ETextCommit::OnEnter) CharacterCreator->CommitNameFromKeyboard();
            if (OnFinished) OnFinished();
        }
    }
    void SetSelectionFromVirtualKeyboard(int, int) override {}
    bool GetSelection(int& Start, int& End) override { Start = End = GetText().ToString().Len(); return true; }
    FText GetText() const override
    {
        if (auto* Box = Cast<UEditableTextBox>(Entry.Get())) return Box->GetText();
        if (auto* Retail = Cast<UACERetailTextEntry>(Entry.Get())) return Retail->GetText();
        if (CharacterCreator.IsValid()) return FText::FromString(CharacterCreator->Model.Selection.Name);
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
    TWeakObjectPtr<UACEUICharGenBinder> CharacterCreator;
    bool bCharacterName = false;
    FText Original;
    TFunction<void()> Finished;
    bool bFinished = false;
    TSharedPtr<class IInputProcessor> SubmitInput;
};
