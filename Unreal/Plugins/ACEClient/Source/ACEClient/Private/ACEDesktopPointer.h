#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"

class FReply;
class UGameViewportClient;

// Linux SDL confinement belongs to the native client area, independently of
// viewport resolution, desktop position, UI DPI, and the software cursor art.
class FACEDesktopPointer
{
public:
    static TSharedPtr<FACEDesktopPointer> Create(UGameViewportClient* Viewport);
    static void PrepareInputMode(FReply& Operations, UGameViewportClient& Viewport);
    static bool ShouldConfine(EWindowMode::Type Mode, bool bForeground, bool bDesktopActive);

    explicit FACEDesktopPointer(UGameViewportClient* InViewport);
    ~FACEDesktopPointer();
    void Update(bool bDesktopActive);

private:
    void ReleaseWindow();
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakPtr<FGenericWindow> Window;
};
