#pragma once

#include "Kismet/KismetSystemLibrary.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeEditorFunctionLibrary.generated.h"

UCLASS()
class ACEWORLDBAKEEDITOR_API UACEWorldBakeEditorFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Convert Hex string to int32
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static int32 HexStringToInt32(const FString& HexString);

	// Generic directory choser dialog
	UFUNCTION(BlueprintCallable, Category="FilePicker")
	static bool OpenDirectoryDialog(const FString& DialogTitle, const FString& DefaultPath, FString& OutFolderName);

	// Get the current dat file directory
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void GetDatFileDirectory(FString& OutPath, bool& OutHasCell, bool& OutHasHighRes, bool& OutHasLocale, bool& OutHasPortal);

	// Set the current dat file directory
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void SetDatFileDirectory(const FString& Path);

	// Export landblock from .dat as .t3d files
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ExportLandblocks();

	// Export a resource type from .dat
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ExportPortalResource(EACEWorldBakeResource::Type ResourceType, int32 Identifier);

	// Export a resource type from .dat, for all ids matching Mask
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ExportPortalResources(EACEWorldBakeResource::Type ResourceType, int32 Mask = 0x0000FFFF);

	// Dump stats for a given resource type to the logs
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void DumpDatFileStatsToLog(EACEWorldBakeDatFile::Type DatFileType);

	// Import textures from SourceFolder into TargetSubPath
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportTextures(const FString& SourceFolder, const FString& TargetSubPath);

	// Import models from SourceFolder into TargetSubPath
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportModels(const FString& SourceFolder, const FString& TargetSubPath);

	// Get the texture settings for a particular portal image
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static bool CorrectPortalImageTextureSettings(UTexture2D* Texture, int32 Identifier = 0);

	// Import a scene for Identifier
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportScene(int32 Identifier);

	// Import a setup for Identifier
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportSetup(int32 Identifier);

	// Import all setups for Mask
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportSetups(int32 Mask);

	// Generate a material from a Surface
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static bool ImportSurface(int32 SurfaceId, UMaterial* DiffuseParentMat, UMaterial* DiffuseMaskedParentMat, FName DiffuseParamName, UMaterial* PalettedParentMat, UMaterial* PalettedMaskedParentMat, FName IndexedParamName, FName PaletteParamName);

	// Generate materials from a Surfaces matching mask
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportSurfaces(int32 Mask, UMaterial* DiffuseParentMat, UMaterial* DiffuseMaskedParentMat, FName DiffuseParamName, UMaterial* PalettedParentMat, UMaterial* PalettedMaskedParentMat, FName IndexedParamName, FName PaletteParamName);

	// Generate landscape materials
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static bool ImportLandscapeMaterials(UMaterialInterface* LandscapeParentMat);

	// Generate landscape material instances
	UFUNCTION(BlueprintCallable, Category = "ACEWorldBakeEditor")
	static bool ImportLandscapeMaterialInstances(UMaterialInterface* LandscapeParentMat);

	// Export landblock info
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ExportLandblockInfo(int32 LandblockX, int32 LandblockY);

	// Import a .t3d file into the world, optionally in a specific level and optionally focus on that actor once pasted
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportT3D(const FString& FilePath, const FString& LevelName, bool FocusImportedActor);

	// Import a .t3d file and save it as a blueprint
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportT3DToBlueprint(const FString& FilePath, const FName AssetName);

	// Import landblock info
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportLandblockInfo(int32 LandblockX, int32 LandblockY, bool FocusImportedActor);

	// Import scenes for a landblock
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void ImportLandblockScene(int32 LandblockX, int32 LandblockY, bool FocusImportedActor);

	// Mark landscape component dirty
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void MarkLandscapeComponentDirty(class ULandscapeComponent* LandscapeComponent);

	// Update the layer info map on a landscape proxy, after 1 or more ULandscapeComponent::OverrideMaterial
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void UpdateLandscapeStreamingLayerInfoMap(class ALandscapeStreamingProxy* LandscapeProxy);

	// Stop and clear the editor import queue
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void StopImports();

	// Stop focusing imported actors
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void StopImportFocus();

	// Start focusing imported actors
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void StartImportFocus();

	// Get an array of GCFs used by a tile
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static TArray<int32> GetLandscapeTileGCFs(int32 TileX, int32 TileY);

	// Force editor cameras to focus an actor
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void MoveViewportCamerasToActors(const TArray<AActor*>& Actors, bool bActiveViewportOnly);

	/** Export DAT resources, import content, build Dereth tiled landscape, and queue all landblock T3D imports. */
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static bool BuildEntireWorld();

	/** Queue ImportLandblockInfo + ImportLandblockScene for the full 256x256 grid (async via EdEngine). */
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void QueueAllLandblockImports();

	//
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static bool IsAsyncImportOrExportPending();

	//
	UFUNCTION(BlueprintCallable, Category="ACEWorldBakeEditor")
	static void SetMinDrawDistance(UPrimitiveComponent* Component, float Distance);
};
