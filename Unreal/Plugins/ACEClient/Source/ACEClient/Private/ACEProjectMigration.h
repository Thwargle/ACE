#pragma once

#include "CoreMinimal.h"

namespace ACEProjectMigration
{
    // Copies only user-owned settings; never overwrites the new profile or copies caches.
    int32 CopyMissingProfileFiles(const FString& OldSaved, const FString& NewSaved);
    void ImportWindowsProfile();
}
