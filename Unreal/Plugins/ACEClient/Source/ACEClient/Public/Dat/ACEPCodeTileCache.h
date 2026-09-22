#pragma once

#include "CoreMinimal.h"
struct FACETerrainBlend;

/** Shared TexMerge PCode bake tiles — many landblocks reuse the same palette codes. */
namespace ACEPCodeTileCache
{
	static constexpr uint32 SchemaVersion = 3u; // Lossless prepared mip chain; retail byte filtering.
	static constexpr uint32 Magic = 0x58504341u; // 'ACPX'

	FString MakeCacheFilePath(uint32 PCode, uint64 DatFingerprint);
	bool SavePrepared(uint32 PCode, uint64 DatFingerprint, const FACETerrainBlend& Blend);
	bool LoadPrepared(uint32 PCode, uint64 DatFingerprint, FACETerrainBlend& OutBlend);
	bool Save(uint32 PCode, uint64 DatFingerprint, int32 Width, int32 Height, const TArray<FColor>& Pixels);
	bool Load(uint32 PCode, uint64 DatFingerprint, int32& OutWidth, int32& OutHeight, TArray<FColor>& OutPixels);
}
