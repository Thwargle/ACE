#pragma once

#include "CoreMinimal.h"

/** acunreal / retail Dereth world grid constants. */
namespace ACEWorldBakeMap
{
	constexpr int32 LandscapeTileCount = 8;
	constexpr int32 TileQuadsPerSection = 15;
	constexpr int32 TileSectionsPerComponent = 1;
	constexpr int32 TileComponentsPerSide = 17;
	constexpr int32 TileSizeVerts = TileComponentsPerSide * TileQuadsPerSection + 1;
	/** ~6.1 km tile width — must reach diagonal tile centers from any corner in-tile. */
	constexpr float StreamingDistanceCm = 950000.f;
	static const FString MapPackage = TEXT("/Game/Maps/Dereth/Dereth");
	static const FString TileContentRoot = TEXT("/Game/Maps/Dereth/Tile");
	/** Landblock/scene maps live outside the WC scan tree so only landscape tiles are WC tiles. */
	static const FString SubLevelContentRoot = TEXT("/Game/DerethBake/Tile");
	static FVector GetLandscapeScale();
	static FIntPoint GetTileCoordOffset();
}

struct FACEWorldBakeWorldBuildOptions
{
	bool bExportResources = true;
	bool bExportLandblocks = true;
	bool bImportTextures = true;
	bool bImportModels = true;
	bool bImportSetups = true;
	bool bImportSurfaces = true;
	bool bImportLandscapeMaterials = true;
	bool bCreateDerethMap = true;
	bool bImportTiledLandscape = true;
	bool bImportLandscapeMaterialInstances = true;
	bool bImportLandblockT3D = true;
	bool bUpdateDefaultMap = true;
	bool bReconfigureWorldComposition = false;
	bool bSaveAll = true;

	FString DatDirectory;
	FString LandscapeMaterialPath = TEXT("/Game/Landblocks/LandscapeGCF_0.LandscapeGCF_0");
	FString DiffuseParentMatPath = TEXT("/Game/Materials/M_Diffuse.M_Diffuse");
	FString DiffuseMaskedParentMatPath = TEXT("/Game/Materials/M_DiffuseMasked.M_DiffuseMasked");
	FString PalettedParentMatPath = TEXT("/Game/Materials/M_Paletted.M_Paletted");
	FString PalettedMaskedParentMatPath = TEXT("/Game/Materials/M_PalettedMasked.M_PalettedMasked");
	FString LandscapeParentMatPath = TEXT("/Game/Landblocks/LandscapeMaster.LandscapeMaster");
};

class ACEWORLDBAKEEDITOR_API FACEWorldBakeWorldBuilder
{
public:
	static bool ParseOptionsFromCommandLine(const FString& Params, FACEWorldBakeWorldBuildOptions& OutOptions);

	/** Full DAT export → content import → Dereth map + tiled landscape + landblock T3D. */
	static bool Run(const FACEWorldBakeWorldBuildOptions& Options);

	/** Queue all landblock info + scene T3D imports (processed async by ACEWorldBakeEdEngine). */
	static bool QueueAllLandblockImports();

private:
	static bool ExportPhase(const FACEWorldBakeWorldBuildOptions& Options);
	static bool ImportContentPhase(const FACEWorldBakeWorldBuildOptions& Options);
	static bool EnsureDerethPersistentMap(const FACEWorldBakeWorldBuildOptions& Options);
	static bool ImportTiledLandscapeFromSaved();
	static bool ConfigureWorldCompositionStreaming(UWorld* World);
	static bool UnloadAllWorldCompositionTiles(UWorld* World);
	static bool MigrateSubLevelPackagesOutOfWorldComposition();
	static bool ClearWorldCompositionTileInfoOnSubLevels();
	static bool NestSubLevelsInLandscapeTiles();
	static bool StripSubLevelStreamingFromLandscapeTiles();
	static void FinalizeWorldCompositionForEditor(UWorld* World);
	static void PumpImportQueueUntilIdle(double MaxSeconds);
	static void UpdateDefaultMapSetting();
};
