#pragma once
#include "CoreMinimal.h"

class UWorld;
class ISceneViewExtension;

// Reserved only on desktop SDR views that composite the isolated particle pass.
inline constexpr int32 ACERetailParticleViewFlag = 1 << 30;
ACERETAILRENDER_API TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> CreateACERetailParticleView(UWorld* World);
