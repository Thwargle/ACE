#pragma once
#include "Engine/TextureAllMipDataProviderFactory.h"
#include "Dat/ACETerrainBlendCache.h"
#include "ACELandTextureMipProvider.generated.h"

class UTexture2D;

/** Rebuild upload mips from the mesh's shared, full-resolution terrain pixels.
 * Avoid retaining another entire CPU mip chain beside every GPU texture. */
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
