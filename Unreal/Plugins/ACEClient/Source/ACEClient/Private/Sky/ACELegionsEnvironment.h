#pragma once

#include "Sky/ACELegionsSky.h"
#include "Dat/ACEDatTextureResolver.h"

namespace ACELegionsSky
{
enum EArtwork : uint8 { Ground = 1, Grass = 2, Trees = 4, Boulders = 8, AllEnvironment = Ground | Grass | Trees | Boulders };

struct FTreeSection
{
	uint32 Texture = 0;
	bool Masked = false;
	TArray<int32> Indices;
};

struct FTree
{
	TArray<FVector> Vertices, Normals;
	TArray<FVector2D> UVs;
	TArray<FTreeSection> Sections;
	FBox Bounds{ForceInit};
};

// Legions meadow frill groups and mesh use metres in the native AC2 frame.
struct FFrillRule
{
	FVector MinOffset, MaxOffset, MinScale, MaxScale;
	float MinRotation, MaxRotation, Frequency, MinNormalZ, MaxNormalZ, CheckRadius, MinCheckZ, MaxCheckZ;
	uint32 ChildIndex;
};
struct FGrassMeadow
{
	TArray<FVector> Vertices;
	TArray<FVector2D> UVs;
	TArray<int32> Indices;
	TArray<FFrillRule> Rules;
	FBox Bounds{ForceInit};
	float DirectWeight = 0, NestedWeight = 0, NestedFrequency = 0;
	uint32 DirectScene = 0, NestedScene = 0, FrillScene = 0;
};
struct FEnvironment
{
	uint8 Enabled = 0;
	uint32 ContentCrc = 0;
	TMap<uint32, TArray<FACEDatDecodedSurface>> Surfaces; // Native mips for AC1 TexMerge filtering
	// Legions region 13000000 uses shared material 2b000007 on these ground types.
	static constexpr uint32 DetailTextureId = 0x4000003c;
	static constexpr float DetailRepeatMetres = 3.f;
	float DetailMean = .5f;
	FImage GroundDetail;
	TMap<uint32, FImage> SceneryTextures;
	FImage GrassCards;
	FGrassMeadow Meadow;
	TMap<uint32, FTree> TreeModels;
	bool bDerethTrees = false;

	const FACEDatDecodedSurface* Terrain(uint32 TerrainType, int32 MaxSize = MAX_int32) const;
	// A 120 m square contains five AC1 cells and an integral number of native
	// 20 m and 10 m ground repeats, so it can be shared by every matching palette.
	static constexpr int32 TerrainCellsAcross = 5;
	int32 TerrainTileCells(uint32 PCode) const;
	uint32 TerrainBlendPair(uint32 PCode, uint32& NativeType) const;
	static float TerrainRepeatMetres(uint32 SurfaceId) { return SurfaceId == 0x40000b0fu ? 10.f : 20.f; }
	static bool IsFullRoad(uint32 PCode)
	{
		return ((PCode | (PCode >> 1)) & 0x05500000u) == 0x05500000u;
	}
	static float UniformTerrainRepeats(uint32 PCode);
	const FTree* FindTree(uint32 SetupId) const;
	bool ReplacesTree(uint32 SetupId) const { return FindTree(SetupId) != nullptr; }
	uint32 BoulderTexture(uint32 SetupId) const;
	uint64 SceneryCacheKey() const;
	uint64 TerrainCacheKey(uint64 Original) const;
};

bool IsMappedTree(uint32 SetupId);
bool IsMappedScenery(uint32 SetupId);
bool IsBoulderTexture(uint32 TextureId);
bool LoadEnvironment(const FString& Path, uint8 Selected, FEnvironment& Out);
using FEnvironmentPtr = TSharedPtr<const FEnvironment, ESPMode::ThreadSafe>;
FEnvironmentPtr ActiveEnvironment();
void RefreshEnvironment();
UTexture2D* CreateImportedTexture(const FImage& Image, TextureFilter Filter = TF_Trilinear);
}
