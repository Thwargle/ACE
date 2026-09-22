#pragma once
#include "CoreMinimal.h"

class APlayerController;
class FACEKeyboardRouter
{
public:
    virtual ~FACEKeyboardRouter() = default;
    static TSharedPtr<FACEKeyboardRouter> Create(APlayerController* Controller, TFunction<bool()> CanRoute);
    // Windows separates navigation-cluster keys from the physical numpad using
    // the extended/scancode bits, before Slate discards that distinction.
    static uint32 NumpadVirtualKey(uint32 VirtualKey, uint32 ScanCode, bool Extended);
};
