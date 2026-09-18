#pragma once

#include "CoreMinimal.h"
#include "Dat/ACELandblockMeshBuilder.h"

/**
 * Versioned on-disk cache for FACEBuiltLandblockMesh geometry (+ heights/terrain).
 * v7+: TexMerge BakedPixels are omitted — hydrate from shared PCode tile cache / BakePCode.
 * Format is independent of UE FVector precision — always writes float32.
 */
namespace ACELandblockMeshCache
{
	/** Bump when section layout / bake rules change (must match land mesh format bumps). */
	static constexpr uint32 SchemaVersion = 8u; // v8: honor zero draw-distance DAT degradation records
	static constexpr uint32 Magic = 0x424C4341u; // 'ACLB' little-endian

	/** Saved/ACEClient/MeshCache/Landblocks/<id>_<scale>_<fp>.aclb */
	FString MakeCacheFilePath(uint32 LandblockId, float WorldScale, uint64 DatFingerprint);

	bool Save(const FString& Path, const FACEBuiltLandblockMesh& Mesh, float WorldScale, uint64 DatFingerprint);
	bool Load(const FString& Path, FACEBuiltLandblockMesh& OutMesh, float ExpectedWorldScale, uint64 ExpectedFingerprint);
}
