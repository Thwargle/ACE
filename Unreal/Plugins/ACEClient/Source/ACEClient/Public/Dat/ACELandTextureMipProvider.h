#pragma once
#include "Engine/TextureAllMipDataProviderFactory.h"
#include "Dat/ACETerrainBlendCache.h"
#include "ACELandTextureMipProvider.generated.h"

class UTexture2D;

/** Upload worker-prepared full-resolution mips from a shared lossless CPU backup.
 * GPU textures remain uncompressed; resource recreation uses the same exact data. */
UCLASS()
class ACECLIENT_API UACELandTextureMipProvider : public UTextureAllMipDataProviderFactory
{
    GENERATED_BODY()
public:
    static UTexture2D* CreateTexture(UObject* Outer, FACETerrainBlendCache::FBlendPtr Source);
    virtual bool GetInitialMipData(int32 FirstMipToLoad, TArrayView<void*> OutMipData,
        TArrayView<int64> OutMipSize, FStringView DebugContext) override;
    virtual FStreamableRenderResourceState GetResourcePostInitState(const UTexture* Owner, bool bAllowStreaming) override;
    FACETerrainBlendCache::FBlendPtr Blend;
};
