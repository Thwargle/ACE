#include "ACUnreal.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"

#if (PLATFORM_WINDOWS || PLATFORM_LINUX) && !WITH_EDITOR
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "ACEWindowsExitGuard.h"
#else
#include "ACELinuxExitGuard.h"
#endif
#include <atomic>
namespace
{
    std::atomic<bool> ExitStarted{false};
    void MarkExit() { ExitStarted.store(true, std::memory_order_relaxed); }
    bool IsStopping() { return ExitStarted.load(std::memory_order_relaxed); }
}
#endif

void FACUnrealModule::StartupModule()
{
#if (PLATFORM_WINDOWS || PLATFORM_LINUX) && !WITH_EDITOR
    // Independent of frame ticks and window focus. Covers Quit/update teardown
    // and fatal-error handling, without killing slow or minimized gameplay.
    // Windows broadcasts this as soon as Quit/window-close/update requests
    // exit, before the main loop and render teardown can hang. PreExit alone
    // is too late for a stall before engine cleanup begins.
    FCoreDelegates::GetApplicationWillTerminateDelegate().AddStatic(MarkExit);
    FCoreDelegates::OnEnginePreExit.AddStatic(MarkExit);
    FCoreDelegates::OnPreExit.AddStatic(MarkExit);
    FCoreDelegates::OnHandleSystemError.AddStatic(MarkExit);
    FCoreDelegates::OnShutdownAfterError.AddStatic(MarkExit);
#if PLATFORM_WINDOWS
    const bool Started = ACEWindowsExitGuard::Start(IsStopping);
#else
    const bool Started = ACELinuxExitGuard::Start(IsStopping);
#endif
    if (!Started)
        UE_LOG(LogTemp, Warning, TEXT("AC:Unreal could not start the shutdown guard"));
#endif
}

IMPLEMENT_PRIMARY_GAME_MODULE(FACUnrealModule, ACUnreal, "ACUnreal");
