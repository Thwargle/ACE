#pragma once

#include "CoreMinimal.h"

/** Shared TexMerge PCode bake tiles — many landblocks reuse the same palette codes. */
namespace ACEPCodeTileCache
{
	static constexpr uint32 SchemaVersion = 2u; // Retail encoded-byte terrain blending.
	static constexpr uint32 Magic = 0x58504341u; // 'ACPX'

	FString MakeCacheFilePath(uint32 PCode, uint64 DatFingerprint);
	bool Save(uint32 PCode, uint64 DatFingerprint, int32 Width, int32 Height, const TArray<FColor>& Pixels);
	bool Load(uint32 PCode, uint64 DatFingerprint, int32& OutWidth, int32& OutHeight, TArray<FColor>& OutPixels);
}
