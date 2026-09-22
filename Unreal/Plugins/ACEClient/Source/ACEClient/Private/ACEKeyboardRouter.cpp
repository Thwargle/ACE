#include "ACEKeyboardRouter.h"

uint32 FACEKeyboardRouter::NumpadVirtualKey(uint32 Key, uint32 Scan, bool Extended)
{
    if (Extended) return Key;
    struct FMapping { uint32 Scan, Navigation, Numpad; };
    static constexpr FMapping Keys[] = {
        {0x52,0x2D,0x60},{0x4F,0x23,0x61},{0x50,0x28,0x62},{0x51,0x22,0x63},
        {0x4B,0x25,0x64},{0x4C,0x0C,0x65},{0x4D,0x27,0x66},{0x47,0x24,0x67},
        {0x48,0x26,0x68},{0x49,0x21,0x69},{0x53,0x2E,0x6E}};
    for (const auto& Mapping : Keys)
        if (Scan == Mapping.Scan && Key == Mapping.Navigation) return Mapping.Numpad;
    return Key;
}

#if PLATFORM_WINDOWS
#include "Windows/WindowsApplication.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericWindow.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Windows/AllowWindowsPlatformTypes.h"

class FACEWindowsKeyboardRouter final : public IWindowsMessageHandler, public IInputProcessor
{
    struct FPendingKey { uint32 Key, Mapped; bool Down; };
    TArray<FPendingKey> Pending;
    TSet<uint32> RoutedKeys;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakPtr<FWindowsApplication> Application;
    TFunction<bool()> CanRoute;
    bool bRouting = false;
public:
    FACEWindowsKeyboardRouter(APlayerController* InController, TFunction<bool()> InCanRoute,
        TSharedPtr<FWindowsApplication> InApplication)
        : Controller(InController), Application(InApplication), CanRoute(MoveTemp(InCanRoute)) {}
    virtual ~FACEWindowsKeyboardRouter() override
    {
        if (auto App=Application.Pin()) App->RemoveMessageHandler(*this);
    }
    virtual bool ProcessMessage(HWND Window, uint32 Message, WPARAM Key, LPARAM Data, int32&) override
    {
        auto* PC=Controller.Get();auto* Viewport=PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
        const auto GameWindow=Viewport ? Viewport->GetWindow() : nullptr;
        if (!GameWindow || !GameWindow->GetNativeWindow() || GameWindow->GetNativeWindow()->GetOSWindowHandle()!=Window) return false;
        if (Message==WM_KILLFOCUS) { Pending.Reset();RoutedKeys.Reset();return false; }
        const bool Down=Message==WM_KEYDOWN || Message==WM_SYSKEYDOWN;
        if (!Down && Message!=WM_KEYUP && Message!=WM_SYSKEYUP) return false;
        const uint32 Mapped=FACEKeyboardRouter::NumpadVirtualKey(uint32(Key), (uint64(Data)>>16)&0xff, (uint64(Data)&0x01000000)!=0);
        // Keep unmapped navigation events in order too: arrows and numpad can be
        // held simultaneously and have the same VK when Num Lock is off.
        if ((Key>=0x21 && Key<=0x28) || Key==0x0C || Key==0x2D || Key==0x2E)
        {
            if (Pending.Num()>=128) Pending.Reset();
            Pending.Add({uint32(Key),Mapped,Down});
        }
        return false; // WindowsApplication still queues the normal Slate event.
    }
    virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
    bool Route(FSlateApplication& App,const FKeyEvent& Event,bool Down)
    {
        if (bRouting) return false;
        const int32 Index=Pending.IndexOfByPredicate([&](const FPendingKey& P){return P.Key==Event.GetKeyCode() && P.Down==Down;});
        if (Index==INDEX_NONE) return false;
        const uint32 Mapped=Pending[Index].Mapped;Pending.RemoveAt(Index);
        if (Mapped==Event.GetKeyCode() || !Controller.IsValid()) return false;
        // Release a camera key even if chat took focus while it was held.
        if (!CanRoute() && (Down || !RoutedKeys.Contains(Mapped))) return false;
        if (Down) RoutedKeys.Add(Mapped); else RoutedKeys.Remove(Mapped);
        const FKey Key=FInputKeyManager::Get().GetKeyFromCodes(Mapped,0);
        const FKeyEvent Translated(Key,Event.GetModifierKeys(),Event.GetUserIndex(),Event.IsRepeat(),0,Mapped);
        TGuardValue<bool> Guard(bRouting,true);
        if (Down) App.ProcessKeyDownEvent(Translated); else App.ProcessKeyUpEvent(Translated);
        return true; // Never also deliver the arrow/Home/Delete alias to gameplay.
    }
    virtual bool HandleKeyDownEvent(FSlateApplication& App,const FKeyEvent& Event) override {return Route(App,Event,true);}
    virtual bool HandleKeyUpEvent(FSlateApplication& App,const FKeyEvent& Event) override {return Route(App,Event,false);}
};
class FACEScopedKeyboardRouter final : public FACEKeyboardRouter
{
    TSharedPtr<FACEWindowsKeyboardRouter> Processor;
public:
    explicit FACEScopedKeyboardRouter(TSharedPtr<FACEWindowsKeyboardRouter> InProcessor) : Processor(MoveTemp(InProcessor)) {}
    virtual ~FACEScopedKeyboardRouter() override
    {
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
    }
};
#include "Windows/HideWindowsPlatformTypes.h"
#endif

TSharedPtr<FACEKeyboardRouter> FACEKeyboardRouter::Create(APlayerController* Controller,TFunction<bool()> CanRoute)
{
#if PLATFORM_WINDOWS
    // WindowsPlatformApplicationMisc creates FNullApplication for offscreen
    // rendering. It has no Win32 message handler; casting it corrupts memory.
    if (FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen"))) return nullptr;
    if (FSlateApplication::IsInitialized())
    {
        auto App=StaticCastSharedPtr<FWindowsApplication>(FSlateApplication::Get().GetPlatformApplication());
        if (!App) return nullptr;
        auto Router=MakeShared<FACEWindowsKeyboardRouter>(Controller,MoveTemp(CanRoute),App);
        App->AddMessageHandler(*Router);
        FSlateApplication::Get().RegisterInputPreProcessor(Router,0);
        return MakeShared<FACEScopedKeyboardRouter>(Router);
    }
#endif
    return nullptr;
}
