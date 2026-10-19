#pragma once

// This guard deliberately has no engine/logging dependency. A render-driver
// or logger deadlock during shutdown must not deadlock the guard as well.
#include <pthread.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

namespace ACELinuxExitGuard
{
    struct FRequest { bool (*ShouldExit)(); double GraceSeconds; };
    inline double Now()
    {
        timespec Time{};
        clock_gettime(CLOCK_MONOTONIC, &Time);
        return double(Time.tv_sec) + double(Time.tv_nsec) / 1.e9;
    }
    inline void* Run(void* Data)
    {
        const FRequest Request = *static_cast<FRequest*>(Data);
        delete static_cast<FRequest*>(Data);
        double Deadline = 0;
        for (;;)
        {
            if (!Deadline && Request.ShouldExit()) Deadline = Now() + Request.GraceSeconds;
            if (Deadline && Now() >= Deadline)
            {
                static constexpr char Message[] = "AC:Unreal: shutdown exceeded its grace period; terminating the remaining client process.\n";
                const int Flags = fcntl(STDERR_FILENO, F_GETFL);
                if (Flags >= 0 && fcntl(STDERR_FILENO, F_SETFL, Flags | O_NONBLOCK) == 0)
                    (void)!write(STDERR_FILENO, Message, sizeof(Message)-1);
                _exit(124);
            }
            timespec Pause{0, 100000000};
            nanosleep(&Pause, nullptr);
        }
    }
    inline bool Start(bool (*ShouldExit)(), double GraceSeconds = 45.)
    {
        auto* Request = new FRequest{ShouldExit, GraceSeconds};
        pthread_t Thread;
        if (pthread_create(&Thread, nullptr, Run, Request) != 0) { delete Request; return false; }
        pthread_detach(Thread);
        return true;
    }
}
