#pragma once

#include "ACEWorldBakeDatFile.h"
#include "ACEWorldBakeDatRegion.h"

#pragma pack(push,1)
struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeCellLandblock
{
	//
	static const uint32 kDimension = 9;
	//
	static const uint32 kUnitSize = 24;
	//
	static const uint32 kLandblockSize = 192;
	//
	uint32 Identifier;
	//
	uint32 HasLandblockObject;
	//
	uint16 Topography[kDimension][kDimension];
	//
	uint8 RegionHeightIndices[kDimension][kDimension];
	//
	uint8 Unk1;

	bool IsRoad(uint32 IndexX, uint32 IndexY) const
	{
		return 0 != GetRoad(IndexX, IndexY);
	}

	uint8 GetRoad(uint32 IndexX, uint32 IndexY) const
	{
		check(IndexX < kDimension && IndexY < kDimension);
		return static_cast<uint8>(Topography[IndexX][IndexY] & 0x3);
	}

	bool OnRoad(const FVector3f& v, const FACEWorldBakePortalRegion& region) const;

	uint8 GetRegionTerrain(uint32 IndexX, uint32 IndexY) const
	{
		check(IndexX < kDimension && IndexY < kDimension);
		return static_cast<uint8>((Topography[IndexX][IndexY] >> 2) & 0x1f);
	}

	TSet<uint8> GetUniqueRegionTerrains() const
	{
		TSet<uint8> UniqueTerrains;

		for (uint32 IndexX = 0; IndexX < kDimension; ++IndexX)
		{
			for (uint32 IndexY = 0; IndexY < kDimension; ++IndexY)
			{
				UniqueTerrains.Add(GetRegionTerrain(IndexX, IndexY));
			}
		}

		return UniqueTerrains;
	}

	uint8 GetRegionScene(uint32 IndexX, uint32 IndexY) const
	{
		check(IndexX < kDimension && IndexY < kDimension);
		return static_cast<uint8>(Topography[IndexX][IndexY] >> 11);
	}

	uint8 GetHeightIndexAt(uint32 IndexX, uint32 IndexY) const
	{
		check(IndexX < kDimension && IndexY < kDimension);
		return RegionHeightIndices[IndexX][IndexY];
	}

	float GetHeightAt(uint32 IndexX, uint32 IndexY, const TArray<float>& Heights) const
	{
		check(IndexX < kDimension && IndexY < kDimension);
		check(RegionHeightIndices[IndexX][IndexY] < Heights.Num());
		return Heights[RegionHeightIndices[IndexX][IndexY]];
	}

	float GetHeight(float PositionX, float PositionY, const TArray<float>& Heights) const;

	FPlane4f GetTrianglePlane(float PositionX, float PositionY, const TArray<float>& Heights) const;
};
#pragma pack(pop)

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeCellStructure
{
	FACEWorldBakeCellStructure(FACEWorldBakeByteReader& Reader);
	FACEWorldBakeCellStructure();
	//
	uint32 Identifier;
	//
	FACEWorldBakeResourceLocation Location;
	//
	TResourceInfoArray StaticObjects;
	//
	uint8 SurfaceCount;
	//
	TArray<uint32> SurfaceIDs;
	//
	uint16 EnvironmentId;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeCellLandblockObject
{
	FACEWorldBakeCellLandblockObject(FACEWorldBakeByteReader& Reader);
	FACEWorldBakeCellLandblockObject();
	//
	uint32 Identifier;
	//
	uint32 StructureCount;
	//
	uint16 StaticObjectCount;
	//
	uint16 Unk1;
	//
	TResourceInfoArray StaticObjects;
	//
	uint16 StaticObjectExCount = 0;
	//
	TResourceInfoArray StaticObjectsEx;
};

class ACEWORLDBAKEDATTOOLS_API FACEWorldBakeCellFile
{
public:
	FACEWorldBakeCellFile(FACEWorldBakeDatFilePtr InCellFile);

	const FACEWorldBakeCellLandblock* GetLandblock(uint32 LandblockX, uint32 LandblockY) const;

	bool GetLandblockObject(uint32 LandblockX, uint32 LandblockY, FACEWorldBakeCellLandblockObject& outLandblockInfos) const;

	int32 GetLandblockStructures(const FACEWorldBakeCellLandblockObject& LandblockObject, TArray<FACEWorldBakeCellStructure>& OutLandblockStructures) const;

	template <typename T>
	bool GetResource(uint32 identifier, T& OutResource) const
	{
		return CellFile->GetResource(identifier, OutResource);
	}

	static const uint32 kWidthInLandblocks = 255;
	static const uint32 kHeightInLandblocks = 255;

private:
	FACEWorldBakeDatFilePtr CellFile;
};
