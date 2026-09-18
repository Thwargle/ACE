#pragma once

#include "ACEWorldBakeFbxWriter.h"
#include "ACEWorldBakeDatModel.h"

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeFbxModel : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxModel(const TArray<FACEWorldBakePortalModelTriangleFan>& InFans, const TArray<FACEWorldBakePortalModelVertex>& InVertices, int32 InSurfaceIndex, bool bInFilterSurfaceFans = true, bool bInCollisionOnly = false, bool bInCollisionHasTexture = false, const FString& SurfaceName = TEXT("Surface"));

	virtual void AppendBody(FString& OutContent) const override;

	TArray<FACEWorldBakePortalModelTriangleFan> Fans;
	TArray<FACEWorldBakePortalModelVertex> Vertices;
	int32 SurfaceIndex;
	bool bFilterSurfaceFans;
	bool bCollisionOnly;
	bool bCollisionHasTexture;
};
