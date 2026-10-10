#pragma once
#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"

class AACEPlayerController;
/** Desktop display shortcuts and window placement, independent of gameplay focus. */
class ACECLIENT_API FACEDesktopDisplay
{
public:
    virtual ~FACEDesktopDisplay() = default;
    static TSharedPtr<FACEDesktopDisplay> Create(AACEPlayerController* Controller);
    static EWindowMode::Type ToggleMode(EWindowMode::Type Actual, EWindowMode::Type Preferred)
    {
        return Actual == EWindowMode::Windowed
            ? (Preferred == EWindowMode::Fullscreen ? EWindowMode::Fullscreen : EWindowMode::WindowedFullscreen)
            : EWindowMode::Windowed;
    }
};
