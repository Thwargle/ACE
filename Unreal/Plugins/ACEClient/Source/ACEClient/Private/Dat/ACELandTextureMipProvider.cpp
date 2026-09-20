#include "Dat/ACELandTextureMipProvider.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Engine/Texture2D.h"

namespace
{
    int32 LandMipCount(int32 W, int32 H)
    {
        int32 Count=1;
        while(W>1 || H>1) { W=FMath::Max(1,W/2); H=FMath::Max(1,H/2); ++Count; }
        return Count;
    }
}

UTexture2D* UACELandTextureMipProvider::CreateTexture(UObject* Outer, FACETerrainBlendCache::FBlendPtr Source)
{
    if (!Source || Source->Width<=0 || Source->Height<=0
        || Source->Pixels.Num()!=int64(Source->Width)*Source->Height) return nullptr;
    auto* Texture=UTexture2D::CreateTransient(Source->Width,Source->Height,PF_B8G8R8A8);
    if (!Texture) return nullptr;
    Texture->Rename(nullptr,Outer);
    Texture->SRGB=true; Texture->NeverStream=true; Texture->Filter=TF_Trilinear;
    Texture->AddressX=Texture->AddressY=TA_Clamp;
    auto* Data=Texture->GetPlatformData(); Data->Mips.Empty();
    int32 W=Source->Width,H=Source->Height;
    for(int32 I=0,Count=LandMipCount(W,H);I<Count;++I)
    {
        auto* Mip=new FTexture2DMipMap(); Mip->SizeX=W; Mip->SizeY=H; Data->Mips.Add(Mip);
        W=FMath::Max(1,W/2); H=FMath::Max(1,H/2);
    }
    auto* Provider=NewObject<UACELandTextureMipProvider>(Texture);
    Provider->Blend=MoveTemp(Source); Texture->AddAssetUserData(Provider);
    Texture->UpdateResource();
    return Texture;
}

FStreamableRenderResourceState UACELandTextureMipProvider::GetResourcePostInitState(const UTexture*, bool)
{
    FStreamableRenderResourceState State;
    if (!Blend) return State;
    const int32 Count=LandMipCount(Blend->Width,Blend->Height);
    State.MaxNumLODs=State.NumNonStreamingLODs=State.NumNonOptionalLODs=Count;
    State.NumResidentLODs=State.NumRequestedLODs=Count;
    return State;
}

bool UACELandTextureMipProvider::GetInitialMipData(int32 FirstMipToLoad, TArrayView<void*> OutMipData,
    TArrayView<int64> OutMipSize, FStringView)
{
    if (!Blend || FirstMipToLoad<0 || OutMipData.Num()!=LandMipCount(Blend->Width,Blend->Height)-FirstMipToLoad
        || (OutMipSize.Num()!=0 && OutMipSize.Num()!=OutMipData.Num())) return false;
    // The provider remains usable after UpdateResource/device recreation. The
    // RHI owns/frees only these temporary upload buffers, never our shared bake.
    const TArray<FColor>* Pixels=&Blend->Pixels;
    TArray<FColor> Current;
    int32 W=Blend->Width,H=Blend->Height;
    for(int32 M=0,Count=LandMipCount(W,H);M<Count;++M)
    {
        if (M>=FirstMipToLoad)
        {
            const int64 Bytes=int64(W)*H*sizeof(FColor);
            void* Upload=FMemory::Malloc(Bytes); FMemory::Memcpy(Upload,Pixels->GetData(),Bytes);
            OutMipData[M-FirstMipToLoad]=Upload;
            if (OutMipSize.Num()) OutMipSize[M-FirstMipToLoad]=Bytes;
        }
        if (M+1<Count)
        {
            TArray<FColor> Next;
            FACEDatTextureResolver::BuildOpaqueMip(*Pixels,W,H,Next);
            Current=MoveTemp(Next); Pixels=&Current;
            W=FMath::Max(1,W/2); H=FMath::Max(1,H/2);
        }
    }
    return true;
}
