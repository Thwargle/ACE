#include "ACUnreal.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"

#if PLATFORM_LINUX && !WITH_EDITOR
#include "ACELinuxExitGuard.h"
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
#if PLATFORM_LINUX && !WITH_EDITOR
    // Independent of frame ticks and window focus. Covers Quit/update teardown
    // and fatal-error handling, without killing slow or minimized gameplay.
    FCoreDelegates::OnEnginePreExit.AddStatic(MarkExit);
    FCoreDelegates::OnPreExit.AddStatic(MarkExit);
    FCoreDelegates::OnHandleSystemError.AddStatic(MarkExit);
    FCoreDelegates::OnShutdownAfterError.AddStatic(MarkExit);
    if (!ACELinuxExitGuard::Start(IsStopping))
        UE_LOG(LogTemp, Warning, TEXT("AC:Unreal could not start the Linux exit guard"));
#endif
}

IMPLEMENT_PRIMARY_GAME_MODULE(FACUnrealModule, ACUnreal, "ACUnreal");
