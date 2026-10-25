#pragma once
#include "CoreMinimal.h"
namespace ACEPluginContent
{
    // The server still checks access level. This bridge is deliberately limited
    // to content authoring, not arbitrary slash commands or executable files.
    bool Command(const FString& Text, bool& NeedsTarget);
    bool RelativeFile(const FString& Name);
}
