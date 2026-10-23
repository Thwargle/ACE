#pragma once
#include "Engine/TextureAllMipDataProviderFactory.h"
#include "ACEWorldTextureMipProvider.generated.h"

/** Runtime DAT textures are not cooked/streamed assets. Select their uploaded
 * mip range explicitly, retaining lossless CPU data for quality changes. */
UCLASS()
class ACECLIENT_API UACEWorldTextureMipProvider : public UTextureAllMipDataProviderFactory
{
    GENERATED_BODY()
public:
    void AddMip(TConstArrayView<FColor> Pixels);
    int64 GetRetainedBytes() const;
    int64 GetRetainedAllocatedBytes() const;
    static void RefreshQuality();
    virtual FStreamableRenderResourceState GetResourcePostInitState(const UTexture* Owner, bool bAllowStreaming) override;
    virtual bool GetInitialMipData(int32 FirstMipToLoad, TArrayView<void*> OutMipData,
        TArrayView<int64> OutMipSize, FStringView DebugContext) override;
private:
    struct FMip { TArray<uint8> Data; int32 Bytes=0; bool Packed=false; };
    TArray<FMip> Mips;
    int32 UploadedFirstMip=INDEX_NONE;
};
