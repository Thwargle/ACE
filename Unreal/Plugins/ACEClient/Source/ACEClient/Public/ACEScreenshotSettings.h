#pragma once
#include "CoreMinimal.h"

namespace ACEScreenshotSettings
{
    ACECLIENT_API FString DefaultDirectory();
    ACECLIENT_API FString GetDirectory();
    /** Empty resets to the platform default. Directory creation happens on capture. */
    ACECLIENT_API void SetDirectory(const FString& Directory);
}
