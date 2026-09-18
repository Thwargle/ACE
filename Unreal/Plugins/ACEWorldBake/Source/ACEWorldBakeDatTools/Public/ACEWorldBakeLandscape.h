#pragma once

class FACEWorldBakeCellFile;
struct FACEWorldBakePortalRegion;
struct FScopedSlowTask;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeLandscapeWriter
{
	static FString ExportLandscapeImages();

	// Generate UE4-szied (2k^2) linear arrays of heights, colors and regions from Cell data
	static void GenerateLandscapeArrays(FScopedSlowTask& SlowTask, const FACEWorldBakeCellFile& CellFile, const FACEWorldBakePortalRegion& Region, TArray<uint16>& OutHeights, TArray<FColor>& OutColors, TArray<uint8>& OutRegions, TArray<uint8>& OutRoads);

	// Map of regions in a "set", ordered region numbers gathered per component
	static void GenerateLandscapeComponentRegionSets(FScopedSlowTask& SlowTask, const TArray<uint8>& LandscapeRegions, TMap<uint8, int32>& OutLandscapeComponentRegion_Count, TMap<uint32, TArray<uint8>>& OutLandscapeComponentRegionSets);

	// Extract linear arrays of a tile from the landscape arrays
	static void ExtractTileArrays(uint32 LandscapeTileX, uint32 LandscapeTileY, const TArray<uint16>& LandscapeHeights, const TArray<FColor>& LandscapeColors, const TArray<uint8>& LandscapeRegions, const TArray<uint8>& LandscapeRoads, TArray<uint8>& OutLandscapeTileHeights, TArray<FColor>& OutLandscapeTileColors, TArray<uint8>& OutLandscapeTileRegions, TArray<uint8>& OutLandscapeTileRoads);

	// Extract region set hashes and blend masks of a tile from landscape regions and region sets
	static void ExtractTileHashesAndBlendMask(uint32 LandscapeTileX, uint32 LandscapeTileY, const TArray<uint8>& LandscapeRegions, const TMap<uint8, int32>& LandscapeComponentRegion_Count, const TMap<uint32, TArray<uint8>>& LandscapeComponentRegionSets, TArray<uint32>& OutLandscapeTileHashes, TArray<FColor>& OutLandscapeTileMasks0, TArray<FColor>& OutLandscapeTileMasks1, TArray<FColor>& OutLandscapeTileMasks2);

	// Extrude the edges of each mask, decreasing strengh of extrusion by each overlap
	static void ExtrudeBlendMaskEdges(TArray<FColor>& LandscapeTileMasks0, TArray<FColor>& LandscapeTileMasks1, TArray<FColor>& LandscapeTileMasks2);

	static void SaveLandblockHeightmaps(const FACEWorldBakeCellFile& CellFile, const FACEWorldBakePortalRegion& Region, const FString& FileName);
};
