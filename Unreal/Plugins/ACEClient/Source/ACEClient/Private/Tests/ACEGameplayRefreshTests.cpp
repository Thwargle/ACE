#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/GameInstance.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEGameplayRefreshTest, "ACE.Performance.GameplayRefresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEGameplayRefreshTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>(); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* Resources = NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Manager = NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto* Layout = NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat, Manager);
    if (!Layout->LoadLayout(0x21000005)) return false;
    auto* Canvas = NewObject<UACEUICanvasWidget>(); Canvas->Initialize();
    Canvas->InitializeCanvas(Manager); Canvas->SetResourceResolver(Resources);
    const auto Slate = Canvas->TakeWidget();
    auto* Binder = NewObject<UACEUIGameplayBinder>();
    Binder->Initialize(GI->GetSubsystem<UACEClientSubsystem>(), Manager, Canvas, nullptr);
    Canvas->SetGameplayBinder(Binder);
    ON_SCOPE_EXIT { Binder->Shutdown(); Canvas->SetGameplayBinder(nullptr); Manager->Shutdown(); };
    TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter + 1);
    auto Native = [&] { Canvas->NativeTick(FGeometry(), 1.f / 90.f); };
    auto VR = [&] { Canvas->TickGameplayState(); };
    // Observe real queued gameplay work rather than a test-only tick counter.
    // A request queued after this frame's refresh must wait until the next frame.
    auto QueueWork = [&] { Binder->bPendingChatRefocus = true; Binder->PendingChatRefocusWindow = INDEX_NONE; };
    QueueWork(); Native();
    TestFalse(TEXT("Desktop tick handles queued gameplay work"), Binder->bPendingChatRefocus);
    QueueWork(); VR(); Native();
    TestTrue(TEXT("Native then VR and repeated paints refresh only once"), Binder->bPendingChatRefocus);
    ++GFrameCounter; VR();
    TestFalse(TEXT("Hidden VR canvas still handles work on the next frame"), Binder->bPendingChatRefocus);
    QueueWork(); Native(); VR();
    TestTrue(TEXT("VR then Native refreshes only once"), Binder->bPendingChatRefocus);
    ++GFrameCounter; Native();
    TestFalse(TEXT("Returning to desktop continues refreshing"), Binder->bPendingChatRefocus);
    Canvas->SetGameplayBinder(nullptr); ++GFrameCounter; VR(); Native();
    Canvas->SetGameplayBinder(Binder); QueueWork(); VR();
    TestFalse(TEXT("Missing binder does not consume the new binder's tick"), Binder->bPendingChatRefocus);
    Canvas->SetGameplayBinder(nullptr); Canvas->SetGameplayBinder(Binder); QueueWork(); VR();
    TestFalse(TEXT("Rebinding in the same frame gets an initial refresh"), Binder->bPendingChatRefocus);
    QueueWork(); Canvas->SetGameplayBinder(Binder); Native();
    TestTrue(TEXT("Assigning the same binder cannot duplicate its refresh"), Binder->bPendingChatRefocus);
    ++GFrameCounter; Native();
    for (int32 Phase = 0; Phase < 3; ++Phase)
    {
        for (bool Duplicate : {Phase % 2 == 0, Phase % 2 != 0})
        {
            const double Start = FPlatformTime::Seconds();
            for (int32 Frame = 0; Frame < 300; ++Frame)
            {
                ++GFrameCounter;
                if ((Frame & 1) == 0) Native(); // Quest paints the retail canvas at half rate.
                VR();
                // Controlled same-build comparison: recreate the former extra refresh.
                if (Duplicate && (Frame & 1) == 0) Binder->TickRefresh();
            }
            AddInfo(FString::Printf(TEXT("GameplayRefresh %s phase %d: %.3f us/game frame (300 frames, half-rate widget)"),
                Duplicate ? TEXT("duplicate") : TEXT("coalesced"), Phase,
                (FPlatformTime::Seconds() - Start) * 1.e6 / 300));
        }
    }
    return true;
}
#endif
