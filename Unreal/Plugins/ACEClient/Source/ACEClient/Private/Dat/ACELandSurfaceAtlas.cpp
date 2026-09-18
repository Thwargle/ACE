#include "Dat/ACELandSurfaceAtlas.h"
#include "Engine/Texture2DArray.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

void FACELandSurfaceAtlas::Reset()
{
	TerrainArray.Reset();
	AlphaArray.Reset();
	TextureGidToLayer.Reset();
	AlphaGidToLayer.Reset();
	TerrainTypeToLayer.Reset();
	TerrainLayerCount = 0;
	AlphaLayerCount = 0;
	bReady = false;
}

void FACELandSurfaceAtlas::ResizeToAtlas(const FACEDatDecodedSurface& Src, TArray<FColor>& Out, bool bAsAlpha)
{
	Out.SetNumUninitialized(AtlasSize * AtlasSize);
	if (!Src.bHasPixels || Src.Width <= 0 || Src.Height <= 0)
	{
		FMemory::Memzero(Out.GetData(), Out.Num() * sizeof(FColor));
		return;
	}

	for (int32 Y = 0; Y < AtlasSize; ++Y)
	{
		const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(AtlasSize);
		for (int32 X = 0; X < AtlasSize; ++X)
		{
			const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(AtlasSize);
			FColor C = Src.SampleUV(U, V);
			if (bAsAlpha)
			{
				const uint8 A = C.R;
				C = FColor(A, A, A, A);
			}
			Out[Y * AtlasSize + X] = C;
		}
	}
}

bool FACELandSurfaceAtlas::AppendSurfaceTexture(FACEDatTextureResolver* Textures, uint32 TexGID,
	TArray<TArray<FColor>>& OutLayers, TMap<uint32, int32>& OutLookup, bool bAsAlpha)
{
	if (!Textures || TexGID == 0 || OutLookup.Contains(TexGID))
	{
		return OutLookup.Contains(TexGID);
	}

	FACEDatDecodedSurface Decoded;
	if (!Textures->DecodeSurfaceTexture(TexGID, Decoded) || !Decoded.bHasPixels)
	{
		return false;
	}

	TArray<FColor> Layer;
	ResizeToAtlas(Decoded, Layer, bAsAlpha);
	const int32 Index = OutLayers.Num();
	OutLayers.Add(MoveTemp(Layer));
	OutLookup.Add(TexGID, Index);
	return true;
}

static UTexture2DArray* CreateArrayFromLayers(const TArray<TArray<FColor>>& Layers, const FName Name)
{
	if (Layers.Num() == 0)
	{
		return nullptr;
	}

	const int32 Size = FACELandSurfaceAtlas::AtlasSize;
	UTexture2DArray* Array = UTexture2DArray::CreateTransient(Size, Size, Layers.Num(), PF_B8G8R8A8, Name);
	if (!Array || !Array->GetPlatformData() || Array->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	FTexture2DMipMap& Mip = Array->GetPlatformData()->Mips[0];
	const int32 BytesPerSlice = Size * Size * 4;
	void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!Dest)
	{
		return nullptr;
	}
	uint8* Bytes = static_cast<uint8*>(Dest);
	for (int32 Slice = 0; Slice < Layers.Num(); ++Slice)
	{
		check(Layers[Slice].Num() == Size * Size);
		FMemory::Memcpy(Bytes + Slice * BytesPerSlice, Layers[Slice].GetData(), BytesPerSlice);
	}
	Mip.BulkData.Unlock();
	Array->UpdateResource();
	return Array;
}

bool FACELandSurfaceAtlas::Build(FACEDatDatabase* Portal, FACEDatTextureResolver* Textures, const FACEDatTexMerge& TexMerge)
{
	Reset();
	if (!Portal || !Textures || TexMerge.TerrainDesc.Num() == 0)
	{
		return false;
	}

	TArray<TArray<FColor>> TerrainLayers;
	TArray<TArray<FColor>> AlphaLayers;

	for (const FACEDatTMTerrainDesc& Desc : TexMerge.TerrainDesc)
	{
		if (Desc.TerrainTex.TexGID == 0)
		{
			continue;
		}
		if (AppendSurfaceTexture(Textures, Desc.TerrainTex.TexGID, TerrainLayers, TextureGidToLayer, /*bAsAlpha*/ false))
		{
			TerrainTypeToLayer.FindOrAdd(Desc.TerrainType) = TextureGidToLayer.FindChecked(Desc.TerrainTex.TexGID);
		}
	}

	auto AddAlphaList = [&](const TArray<FACEDatTerrainAlphaMap>& Maps)
	{
		for (const FACEDatTerrainAlphaMap& Map : Maps)
		{
			AppendSurfaceTexture(Textures, Map.TexGID, AlphaLayers, AlphaGidToLayer, /*bAsAlpha*/ true);
		}
	};
	AddAlphaList(TexMerge.CornerTerrainMaps);
	AddAlphaList(TexMerge.SideTerrainMaps);
	for (const FACEDatRoadAlphaMap& Map : TexMerge.RoadMaps)
	{
		AppendSurfaceTexture(Textures, Map.RoadTexGID, AlphaLayers, AlphaGidToLayer, /*bAsAlpha*/ true);
	}

	// Road overlay uses the road terrain type surface (0x20) — already in TerrainDesc usually.
	TerrainArray.Reset(CreateArrayFromLayers(TerrainLayers, TEXT("ACE_LandTerrainArray")));
	AlphaArray.Reset(CreateArrayFromLayers(AlphaLayers, TEXT("ACE_LandAlphaArray")));
	TerrainLayerCount = TerrainLayers.Num();
	AlphaLayerCount = AlphaLayers.Num();
	bReady = TerrainArray.IsValid() && TerrainLayerCount > 0;

	UE_LOG(LogTemp, Log, TEXT("ACEDat: LandSurfaceAtlas ready — terrain layers=%d alpha layers=%d"),
		TerrainLayerCount, AlphaLayerCount);
	return bReady;
}

int32 FACELandSurfaceAtlas::FindTerrainLayerByType(uint32 TerrainType) const
{
	if (const int32* Found = TerrainTypeToLayer.Find(TerrainType))
	{
		return *Found;
	}
	return InvalidLayer;
}

int32 FACELandSurfaceAtlas::FindTextureLayer(uint32 TexGID) const
{
	if (const int32* Found = TextureGidToLayer.Find(TexGID))
	{
		return *Found;
	}
	return InvalidLayer;
}

int32 FACELandSurfaceAtlas::FindAlphaLayer(uint32 TexGID) const
{
	if (const int32* Found = AlphaGidToLayer.Find(TexGID))
	{
		return *Found;
	}
	return InvalidLayer;
}
