#pragma once

// The caller includes the Windows API (WindowsHWrapper.h in Unreal). Keep this
// worker independent of engine ticks, task pools, logging and graphics locks:
// those may already have stopped or deadlocked when the game window closes.
#include <new>

namespace ACEWindowsExitGuard
{
    struct FRequest { bool (*ShouldExit)(); ULONGLONG GraceMilliseconds; };

    inline DWORD WINAPI Run(void* Data)
    {
        const FRequest Request = *static_cast<FRequest*>(Data);
        delete static_cast<FRequest*>(Data);
        ULONGLONG Deadline = 0;
        for (;;)
        {
            if (!Deadline && Request.ShouldExit()) Deadline = GetTickCount64() + Request.GraceMilliseconds;
            if (Deadline && GetTickCount64() >= Deadline)
            {
                // Do not use RequestExit(true): it flushes the engine logger,
                // which may itself be the reason shutdown is stuck. Terminate
                // only this process, never another client or the launcher.
                TerminateProcess(GetCurrentProcess(), 124);
            }
            Sleep(100);
        }
    }

    inline bool Start(bool (*ShouldExit)(), ULONGLONG GraceMilliseconds = 15000)
    {
        auto* Request = new (std::nothrow) FRequest{ShouldExit, GraceMilliseconds};
        if (!Request) return false;
        HANDLE Thread = CreateThread(nullptr, 0, Run, Request, 0, nullptr);
        if (!Thread) { delete Request; return false; }
        CloseHandle(Thread);
        return true;
    }
}
