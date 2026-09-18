#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2DArray.h"
#include "UObject/StrongObjectPtr.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACEDatTextureResolver.h"

/**
 * WorldBuilder-style land TexMerge atlases: one Texture2DArray for terrain/road surfaces
 * and one for alpha maps. Layer indices are stable for the lifetime of a DAT load.
 */
class ACECLIENT_API FACELandSurfaceAtlas
{
public:
	static constexpr int32 AtlasSize = 512;
	static constexpr int32 InvalidLayer = -1;

	void Reset();

	/** Decode every TexMerge terrain + alpha texture into two arrays. Safe on game thread. */
	bool Build(FACEDatDatabase* Portal, FACEDatTextureResolver* Textures, const FACEDatTexMerge& TexMerge);

	bool IsReady() const { return bReady; }

	int32 FindTerrainLayerByType(uint32 TerrainType) const;
	int32 FindTextureLayer(uint32 TexGID) const;
	int32 FindAlphaLayer(uint32 TexGID) const;

	UTexture2DArray* GetTerrainArray() const { return TerrainArray.Get(); }
	UTexture2DArray* GetAlphaArray() const { return AlphaArray.Get(); }

	int32 GetTerrainLayerCount() const { return TerrainLayerCount; }
	int32 GetAlphaLayerCount() const { return AlphaLayerCount; }

private:
	bool AppendSurfaceTexture(FACEDatTextureResolver* Textures, uint32 TexGID, TArray<TArray<FColor>>& OutLayers, TMap<uint32, int32>& OutLookup, bool bAsAlpha);
	static void ResizeToAtlas(const FACEDatDecodedSurface& Src, TArray<FColor>& Out, bool bAsAlpha);

	TStrongObjectPtr<UTexture2DArray> TerrainArray;
	TStrongObjectPtr<UTexture2DArray> AlphaArray;
	TMap<uint32, int32> TextureGidToLayer;
	TMap<uint32, int32> AlphaGidToLayer;
	TMap<uint32, int32> TerrainTypeToLayer;
	int32 TerrainLayerCount = 0;
	int32 AlphaLayerCount = 0;
	bool bReady = false;
};

/** Per-cell GPU TexMerge descriptors (layer indices into FACELandSurfaceAtlas). */
struct FACELandGpuBlend
{
	int32 BaseLayer = FACELandSurfaceAtlas::InvalidLayer;
	int32 OverlayLayers[3] = { FACELandSurfaceAtlas::InvalidLayer, FACELandSurfaceAtlas::InvalidLayer, FACELandSurfaceAtlas::InvalidLayer };
	int32 OverlayAlphaLayers[3] = { FACELandSurfaceAtlas::InvalidLayer, FACELandSurfaceAtlas::InvalidLayer, FACELandSurfaceAtlas::InvalidLayer };
	int32 OverlayRots[3] = { 0, 0, 0 };
	int32 OverlayCount = 0;
	int32 RoadLayer = FACELandSurfaceAtlas::InvalidLayer;
	int32 RoadAlphaLayers[2] = { FACELandSurfaceAtlas::InvalidLayer, FACELandSurfaceAtlas::InvalidLayer };
	int32 RoadRots[2] = { 0, 0 };
	int32 RoadCount = 0;

	bool IsValid() const { return BaseLayer >= 0; }
};
