#include "ACECameraSettings.h"
#include "Misc/ConfigCacheIni.h"

namespace ACECameraSettings
{
    namespace
    {
        constexpr const TCHAR* Section = TEXT("ACE.Camera");
        constexpr const TCHAR* SpeedKey = TEXT("MouseTurnSpeed");

        float ValidSpeed(float Speed)
        {
            return FMath::IsFinite(Speed)
                ? FMath::Clamp(Speed, MinMouseTurnSpeed, MaxMouseTurnSpeed)
                : DefaultMouseTurnSpeed;
        }
    }

    float GetMouseTurnSpeed()
    {
        float Speed = DefaultMouseTurnSpeed;
        if (GConfig) GConfig->GetFloat(Section, SpeedKey, Speed, GGameUserSettingsIni);
        return ValidSpeed(Speed);
    }

    void SetMouseTurnSpeed(float Speed)
    {
        if (!GConfig) return;
        // Save locally with video settings/key bindings; the retail server has no
        // sensitivity property. Reading the config cache makes changes immediate.
        GConfig->SetFloat(Section, SpeedKey, ValidSpeed(Speed), GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }

    float GetMouseDegreesPerPixel()
    {
        return DefaultMouseDegreesPerPixel * GetMouseTurnSpeed();
    }
}
