#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDesktopPointer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "Widgets/SViewport.h"
#include "UObject/StrongObjectPtr.h"
#if PLATFORM_LINUX
#include "Misc/ScopeExit.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include <SDL3/SDL.h>
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDesktopPointerTest, "ACE.Input.DesktopPointerConfinement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEDesktopPointerTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UGameViewportClient> Viewport(NewObject<UGameViewportClient>(GEngine));
    const auto Widget = SNew(SViewport);

    // A visible pointer may roam outside an ordinary window. Both fullscreen
    // modes retain the monitor boundary, and releasing focus/VR always lets go.
    for (EWindowMode::Type Mode : {EWindowMode::Windowed, EWindowMode::Fullscreen, EWindowMode::WindowedFullscreen})
    {
        TestEqual(TEXT("Foreground confinement follows the actual display mode"),
            FACEDesktopPointer::ShouldConfine(Mode, true, true), Mode != EWindowMode::Windowed);
        TestFalse(TEXT("Alt-tab releases native confinement"), FACEDesktopPointer::ShouldConfine(Mode, false, true));
        TestFalse(TEXT("Entering VR releases desktop confinement"), FACEDesktopPointer::ShouldConfine(Mode, true, false));
    }

    // Reproduce the operations left by all three engine input modes. Checking
    // both the reply and viewport catches a lock restored on the next click,
    // even when the first frame after switching modes appears correct.
    for (EMouseLockMode Lock : {EMouseLockMode::LockAlways, EMouseLockMode::LockInFullscreen, EMouseLockMode::LockOnCapture})
    {
        Viewport->SetMouseLockMode(Lock);
        Viewport->SetIgnoreInput(false);
        Viewport->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
        FReply Look = FReply::Handled().UseHighPrecisionMouseMovement(Widget).SetUserFocus(Widget).LockMouseToWidget(Widget);
        FACEDesktopPointer::PrepareInputMode(Look, *Viewport);
        TestTrue(TEXT("Mouse look removes the desktop-coordinate rectangle"), Look.ShouldReleaseMouseLock() && !Look.GetMouseLockWidget());
        TestEqual(TEXT("A later viewport click cannot reinstall the bad rectangle"), Viewport->GetMouseLockMode(), EMouseLockMode::DoNotLock);
        TestTrue(TEXT("Relative input and camera capture survive the fix"), Look.ShouldUseHighPrecisionMouse() && Look.GetMouseCaptor() == Widget);
        TestTrue(TEXT("Keyboard focus survives the fix"), Look.GetUserFocusRecepient() == Widget);
        TestEqual(TEXT("Permanent mouse-look capture survives the fix"), Viewport->GetMouseCaptureMode(), EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
        TestFalse(TEXT("Gameplay input remains enabled"), Viewport->IgnoreInput());

        Viewport->SetMouseLockMode(Lock);
        Viewport->SetIgnoreInput(true);
        Viewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
        FReply UI = FReply::Handled().SetUserFocus(Widget).LockMouseToWidget(Widget).ReleaseMouseCapture();
        FACEDesktopPointer::PrepareInputMode(UI, *Viewport);
        TestTrue(TEXT("Login and mouse-look release remove the stale rectangle"), UI.ShouldReleaseMouseLock() && !UI.GetMouseLockWidget());
        TestTrue(TEXT("UI focus and capture release survive the fix"), UI.GetUserFocusRecepient() == Widget && UI.ShouldReleaseMouse());
        TestEqual(TEXT("Login remains free of gameplay capture"), Viewport->GetMouseCaptureMode(), EMouseCaptureMode::NoCapture);
        TestTrue(TEXT("Login still ignores gameplay input"), Viewport->IgnoreInput());
    }
    return true;
}

#if PLATFORM_LINUX
namespace
{
class FDesktopPointerWindow final : public FGenericWindow
{
public:
    SDL_Window* Handle = nullptr;
    EWindowMode::Type Mode = EWindowMode::Windowed;
    bool bForeground = true;
    virtual void* GetOSWindowHandle() const override { return Handle; }
    virtual EWindowMode::Type GetWindowMode() const override { return Mode; }
    virtual bool IsForegroundWindow() const override { return bForeground; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDesktopPointerSDLTest, "ACE.Input.DesktopPointerSDL",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEDesktopPointerSDLTest::RunTest(const FString& Parameters)
{
    // NullRHI deliberately skips the engine's SDL video startup. This fixture
    // needs real native window/input state, but no Vulkan device or renderer.
    const bool bOwnVideo = !(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO);
    if (bOwnVideo && !SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        AddError(FString::Printf(TEXT("SDL video initialization failed: %s"), UTF8_TO_TCHAR(SDL_GetError())));
        return false;
    }
    ON_SCOPE_EXIT { if (bOwnVideo) SDL_QuitSubSystem(SDL_INIT_VIDEO); };
    AddInfo(FString::Printf(TEXT("Native pointer fixture using SDL video driver: %s"), UTF8_TO_TCHAR(SDL_GetCurrentVideoDriver())));
    // Keep this fixture hidden: query SDL's requested confinement state without
    // grabbing the player's mouse or changing the host's fullscreen/focus state.
    auto Native = MakeShared<FDesktopPointerWindow>();
    Native->Handle = SDL_CreateWindow("ACE pointer regression", 960, 640, SDL_WINDOW_HIDDEN);
    if (!TestNotNull(TEXT("SDL fixture window"), Native->Handle)) return false;
    // SDL_GetWindowMouseGrab reports an effective OS grab, which is always
    // false for a hidden/unfocused window. Its flag retains the requested grab.
    const auto IsGrabRequested = [&] { return (SDL_GetWindowFlags(Native->Handle) & SDL_WINDOW_MOUSE_GRABBED) != 0; };
    const auto SlateWindow = SNew(SWindow);
    SlateWindow->SetNativeWindow(Native);
    TStrongObjectPtr<UGameViewportClient> Viewport(NewObject<UGameViewportClient>(GEngine));
    Viewport->SetViewportOverlayWidget(SlateWindow, SNew(SOverlay));
    {
        FACEDesktopPointer Pointer(Viewport.Get());
        const SDL_Rect StaleBounds[] = {
            {320, 180, 960, 640}, // window displaced from the desktop origin
            {1920, 0, 960, 640}, // monitor right of the primary
            {-1920, -1080, 960, 640}, // monitor left/above the primary
            {0, 0, 957, 637} // engine's extra right/bottom percentage inset
        };
        for (const SDL_Rect& Bounds : StaleBounds)
        {
            for (EWindowMode::Type Mode : {EWindowMode::Windowed, EWindowMode::Fullscreen, EWindowMode::WindowedFullscreen})
            {
                Native->Mode = Mode;
                TestTrue(TEXT("Install the engine-style stale rectangle"), SDL_SetWindowMouseRect(Native->Handle, &Bounds));
                Pointer.Update(true);
                TestNull(TEXT("All client-area edges are free of stale absolute/inset bounds"), SDL_GetWindowMouseRect(Native->Handle));
                TestEqual(TEXT("Native fullscreen grab follows every mode transition"), IsGrabRequested(), Mode != EWindowMode::Windowed);
            }
        }
        Native->bForeground = false;
        Pointer.Update(true);
        TestFalse(TEXT("Focus loss ungrabs the native window"), IsGrabRequested());
        Native->bForeground = true;
        Pointer.Update(true);
        TestTrue(TEXT("Focus return restores fullscreen confinement"), IsGrabRequested());
        Pointer.Update(false);
        TestFalse(TEXT("Leaving desktop releases the native window"), IsGrabRequested());
        Pointer.Update(true);
        TestTrue(TEXT("Desktop return restores confinement"), IsGrabRequested());
    }
    TestFalse(TEXT("Controller teardown releases the native grab"), IsGrabRequested());
    TestNull(TEXT("Controller teardown clears any rectangle"), SDL_GetWindowMouseRect(Native->Handle));
    SDL_DestroyWindow(Native->Handle);
    Native->Handle = nullptr;
    return true;
}
#endif
#endif
