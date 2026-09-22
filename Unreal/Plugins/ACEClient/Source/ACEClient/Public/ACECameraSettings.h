#pragma once

#include "CoreMinimal.h"

/** Local camera preferences shared by characters on this installation. */
namespace ACECameraSettings
{
    constexpr float DefaultMouseTurnSpeed = 1.f;
    constexpr float MinMouseTurnSpeed = 0.1f;
    constexpr float MaxMouseTurnSpeed = 3.f;
    constexpr float DefaultMouseDegreesPerPixel = 0.75f;

    ACECLIENT_API float GetMouseTurnSpeed();
    ACECLIENT_API void SetMouseTurnSpeed(float Speed);
    ACECLIENT_API float GetMouseDegreesPerPixel();
    ACECLIENT_API bool GetUseMouseTurning();
    ACECLIENT_API void SetUseMouseTurning(bool Enabled);
    ACECLIENT_API bool GetInvertMouseX();
    ACECLIENT_API bool GetInvertMouseY();
    ACECLIENT_API void SetMouseInversion(bool InvertX, bool InvertY);
}
