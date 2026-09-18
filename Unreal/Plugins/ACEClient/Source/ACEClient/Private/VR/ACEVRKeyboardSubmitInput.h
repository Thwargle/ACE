#pragma once
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"

// Android emits native keyboard Enter as a hardware-user event, while the VR
// text field belongs to a virtual Slate user. Consume it before viewport input.
class FACEVRKeyboardSubmitInput final : public IInputProcessor
{
public:
    explicit FACEVRKeyboardSubmitInput(TFunction<void()> InSubmit) : Submit(MoveTemp(InSubmit)) {}
    void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
    bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        if (Event.GetKey() != EKeys::Enter) return false;
        if (!bSubmitted && !Event.IsRepeat()) { bSubmitted = true; Submit(); }
        return true;
    }
    bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
    { return Event.GetKey() == EKeys::Enter; }
private:
    TFunction<void()> Submit;
    bool bSubmitted = false;
};
