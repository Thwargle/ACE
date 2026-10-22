#pragma once
#include "CoreMinimal.h"

class UGameInstance;
class ISceneViewExtension;

/** Final world-image adjustments; Slate menus are drawn afterwards. Captures opt in explicitly. */
ACERETAILRENDER_API TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> CreateACEDisplayView(
    UGameInstance* GameInstance, bool bIncludeSceneCaptures = false);
