#pragma once

struct ACEWORLDBAKEEDITOR_API FACEWorldBakeMaterialImporter
{
	static UMaterialInstanceConstant* CreateMIC(UMaterialInterface* Material, FString InName, bool bSyncBrowser = false);
	static bool EnsureStubParentMaterials();
	static bool ImportSurfaceToMaterial(uint32 SurfaceId, UMaterial* DiffuseParentMat, UMaterial* DiffuseMaskedParentMat, FName DiffuseParamName, UMaterial* PalettedParentMat, UMaterial* PalettedMaskedParentMat, FName IndexedParamName, FName PaletteParamName);
	static bool ImportLandscapeMaterials(UMaterialInterface* LandscapeParentMat);
	static bool ImportLandscapeMaterialInstances(const FString& LandscapeParentMatName, int32 TileX, int32 TileY);
};
