#include "Dat/ACEWorldTextureMipProvider.h"
#include "Engine/Texture2D.h"
#include "Misc/Compression.h"
#include "Scalability.h"
#include "UObject/UObjectIterator.h"

namespace
{
    int32 FirstMip(int32 Count)
    {
        const int32 Detail=Scalability::GetQualityLevels().TextureQuality;
        const int32 Bias=Detail<=0?2:Detail==1?1:0;
        return FMath::Clamp(FMath::Max(Bias,Count-FMath::Min(GMaxTextureMipCount,
            int32(FStreamableRenderResourceState::MAX_LOD_COUNT))),0,FMath::Max(0,Count-1));
    }
}

void UACEWorldTextureMipProvider::AddMip(TConstArrayView<FColor> Pixels)
{
    auto& Mip=Mips.AddDefaulted_GetRef(); Mip.Bytes=Pixels.Num()*sizeof(FColor);
    int32 PackedBytes=FCompression::CompressMemoryBound(NAME_LZ4,Mip.Bytes);
    Mip.Data.SetNumUninitialized(PackedBytes);
    Mip.Packed=FCompression::CompressMemory(NAME_LZ4,Mip.Data.GetData(),PackedBytes,Pixels.GetData(),Mip.Bytes)
        && PackedBytes<Mip.Bytes;
    Mip.Data.SetNum(Mip.Packed?PackedBytes:Mip.Bytes);
    if(!Mip.Packed)FMemory::Memcpy(Mip.Data.GetData(),Pixels.GetData(),Mip.Bytes);
}

FStreamableRenderResourceState UACEWorldTextureMipProvider::GetResourcePostInitState(const UTexture* Owner, bool)
{
    FStreamableRenderResourceState State;
    const auto* Texture=Cast<UTexture2D>(Owner);
    const int32 Count=Texture?Texture->GetNumMips():0;
    if(Count<=0)return State;
    UploadedFirstMip=FirstMip(Count);
    State.AssetLODBias=UploadedFirstMip;
    State.MaxNumLODs=State.NumNonStreamingLODs=State.NumNonOptionalLODs=Count-UploadedFirstMip;
    State.NumResidentLODs=State.NumRequestedLODs=State.MaxNumLODs;
    return State;
}

bool UACEWorldTextureMipProvider::GetInitialMipData(int32 First, TArrayView<void*> Out,
    TArrayView<int64> Sizes, FStringView)
{
    if(First<0 || First>=Mips.Num() || Out.Num()!=Mips.Num()-First || (Sizes.Num() && Sizes.Num()!=Out.Num()))return false;
    for(int32 I=0;I<Out.Num();++I)
    {
        const auto& Mip=Mips[I+First]; Out[I]=FMemory::Malloc(Mip.Bytes);
        if(Mip.Packed && !FCompression::UncompressMemory(NAME_LZ4,Out[I],Mip.Bytes,Mip.Data.GetData(),Mip.Data.Num()))
        {
            for(int32 J=0;J<=I;++J){FMemory::Free(Out[J]);Out[J]=nullptr;if(Sizes.Num())Sizes[J]=0;}
            return false;
        }
        if(!Mip.Packed)FMemory::Memcpy(Out[I],Mip.Data.GetData(),Mip.Bytes);
        if(Sizes.Num())Sizes[I]=Mip.Bytes;
    }
    return true;
}

void UACEWorldTextureMipProvider::RefreshQuality()
{
    // Only called when preferences are applied, never during world ticking.
    // Keep texture objects/material bindings: changing quality must not require a relog.
    for(TObjectIterator<UACEWorldTextureMipProvider> It;It;++It)
        if(auto* Texture=Cast<UTexture2D>(It->GetOuter());IsValid(Texture) && Texture->GetResource()
            && It->UploadedFirstMip!=FirstMip(Texture->GetNumMips())) Texture->UpdateResource();
}
