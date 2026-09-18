#pragma once

#include "CoreMinimal.h"
#include "Dat/ACEDatFileTypes.h"

/** On-disk RegionDesc scenery placement list per landblock. */
namespace ACESceneryTileCache
{
	static constexpr uint32 SchemaVersion = 1u;
	static constexpr uint32 Magic = 0x43534341u; // 'ACSC'

	FString MakeCacheFilePath(uint32 LandblockId, float WorldScale, uint64 DatFingerprint);
	bool Save(const FString& Path, const TArray<FACEDatRegionSceneryItem>& Items, float WorldScale, uint64 DatFingerprint);
	bool Load(const FString& Path, TArray<FACEDatRegionSceneryItem>& OutItems, float ExpectedWorldScale, uint64 ExpectedFingerprint);
}
