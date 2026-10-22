#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ACEWindowsExitGuard.h"
#include <atomic>
#include <cstring>
#include <mutex>

std::atomic<bool> Stopping{false};
bool ShouldExit() { return Stopping.load(std::memory_order_relaxed); }

int main(int argc, char** argv)
{
    if (argc != 2 || !ACEWindowsExitGuard::Start(ShouldExit, 250)) return 2;
    if (!strcmp(argv[1], "normal")) { Stopping = true; Sleep(50); return 0; }
    if (!strcmp(argv[1], "stuck")) { Stopping = true; Sleep(INFINITE); }
    if (!strcmp(argv[1], "repeated"))
        for (;;) { Stopping = true; Sleep(20); }
    if (!strcmp(argv[1], "locked"))
    {
        // A stalled teardown thread holds a lock indefinitely. The shutdown
        // guard must not acquire engine/logger locks before terminating it.
        static std::mutex Lock;
        Lock.lock();
        Stopping = true;
        Sleep(INFINITE);
    }
    // Active, idle and unfocused clients must outlive the grace interval.
    Sleep(1500);
    return 0;
}
