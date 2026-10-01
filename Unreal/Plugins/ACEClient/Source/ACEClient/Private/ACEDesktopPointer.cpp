#include "ACEDesktopPointer.h"
#include "Engine/GameViewportClient.h"
#include "Input/Reply.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SWindow.h"

#if PLATFORM_LINUX
#include <SDL3/SDL.h>
#endif

FACEDesktopPointer::FACEDesktopPointer(UGameViewportClient* InViewport) : Viewport(InViewport) {}

TSharedPtr<FACEDesktopPointer> FACEDesktopPointer::Create(UGameViewportClient* Viewport)
{
#if PLATFORM_LINUX
    // A PIE viewport may occupy only part of its editor window. Native window
    // confinement is for the standalone desktop client, never an offscreen run.
    if (Viewport && !GIsEditor && FApp::CanEverRender()
        && !FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen")))
        return MakeShared<FACEDesktopPointer>(Viewport);
#endif
    return nullptr;
}

void FACEDesktopPointer::PrepareInputMode(FReply& Operations, UGameViewportClient& Viewport)
{
    // UE 5.8 FLinuxCursor::Lock passes desktop-absolute Slate bounds to
    // SDL_SetWindowMouseRect, which takes window-client-relative coordinates.
    // A displaced window/monitor therefore loses its top/left input region.
    // Prevent both the pending mode lock and SceneViewport's capture-time lock.
    // Keep mouse capture/high-precision input intact for unlimited mouse look.
    Operations.ReleaseMouseLock();
    Viewport.SetMouseLockMode(EMouseLockMode::DoNotLock);
}

bool FACEDesktopPointer::ShouldConfine(EWindowMode::Type Mode, bool bForeground, bool bDesktopActive)
{
    return bDesktopActive && bForeground && Mode != EWindowMode::Windowed;
}

FACEDesktopPointer::~FACEDesktopPointer()
{
    ReleaseWindow();
}

void FACEDesktopPointer::ReleaseWindow()
{
#if PLATFORM_LINUX
    if (const auto Previous = Window.Pin())
    {
        if (auto* Native = static_cast<SDL_Window*>(Previous->GetOSWindowHandle()))
        {
            SDL_SetWindowMouseRect(Native, nullptr);
            SDL_SetWindowMouseGrab(Native, false);
        }
    }
#endif
    Window.Reset();
}

void FACEDesktopPointer::Update(bool bDesktopActive)
{
#if PLATFORM_LINUX
    const auto SlateWindow = Viewport.IsValid() ? Viewport->GetWindow() : nullptr;
    const auto Current = bDesktopActive && SlateWindow ? SlateWindow->GetNativeWindow() : nullptr;
    if (Window.Pin() != Current)
    {
        ReleaseWindow();
        Window = Current;
    }
    if (!Current) return;
    auto* Native = static_cast<SDL_Window*>(Current->GetOSWindowHandle());
    if (!Native) return;

    // Remove any stale capture rectangle, including one installed at startup.
    // SDL's full-window grab follows moves, resizes, monitor offsets, display
    // mode changes, and native DPI without rebuilding a pixel rectangle.
    if (SDL_GetWindowMouseRect(Native)) SDL_SetWindowMouseRect(Native, nullptr);
    const bool bConfine = ShouldConfine(Current->GetWindowMode(), Current->IsForegroundWindow(), bDesktopActive);
    // The effective grab becomes false while unfocused even if SDL retains a
    // grab request. Compare that request so focus loss really clears it instead
    // of allowing it to reappear when focus returns in a different window mode.
    const bool bGrabRequested = (SDL_GetWindowFlags(Native) & SDL_WINDOW_MOUSE_GRABBED) != 0;
    if (bGrabRequested != bConfine) SDL_SetWindowMouseGrab(Native, bConfine);
#endif
}
