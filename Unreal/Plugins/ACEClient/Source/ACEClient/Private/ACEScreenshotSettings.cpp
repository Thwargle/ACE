#include "ACEScreenshotSettings.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace ACEScreenshotSettings
{
    FString DefaultDirectory()
    {
#if PLATFORM_WINDOWS
        // Known Documents folder, including OneDrive or redirected user folders.
        const FString Documents = FPlatformProcess::UserDir();
        if (!Documents.IsEmpty()) return Documents / TEXT("Asheron's Call");
#endif
        return FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir());
    }

    FString GetDirectory()
    {
        FString Directory;
        if (GConfig) GConfig->GetString(TEXT("ACE.Screenshots"), TEXT("Directory"), Directory, GGameUserSettingsIni);
        Directory.TrimStartAndEndInline();
        return Directory.IsEmpty() ? DefaultDirectory() : FPaths::ConvertRelativePathToFull(Directory);
    }

    void SetDirectory(const FString& Directory)
    {
        if (!GConfig) return;
        FString Path = Directory.TrimStartAndEnd();
        if (!Path.IsEmpty()) Path = FPaths::ConvertRelativePathToFull(Path);
        GConfig->SetString(TEXT("ACE.Screenshots"), TEXT("Directory"), *Path, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
}
