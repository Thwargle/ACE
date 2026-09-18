#pragma once

#include "CoreMinimal.h"

/** Local camera preferences, independent of the server's Mouse Turning option. */
namespace ACECameraSettings
{
    constexpr float DefaultMouseTurnSpeed = 1.f;
    constexpr float MinMouseTurnSpeed = 0.1f;
    constexpr float MaxMouseTurnSpeed = 3.f;
    constexpr float DefaultMouseDegreesPerPixel = 0.75f;

    ACECLIENT_API float GetMouseTurnSpeed();
    ACECLIENT_API void SetMouseTurnSpeed(float Speed);
    ACECLIENT_API float GetMouseDegreesPerPixel();
}
