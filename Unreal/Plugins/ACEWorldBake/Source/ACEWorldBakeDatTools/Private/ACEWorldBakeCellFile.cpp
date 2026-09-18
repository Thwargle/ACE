#include "ACEWorldBakeCellFile.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakePortalFile.h"
#include "ACEWorldBakeDatSetup.h"

enum EnvCellFlags
{
	kSeenOutside = 1,
	kHasStaticObjects = 2,
	kHasWeenieObjects = 4,
	kHasRestrictionObject = 8
};

bool FACEWorldBakeCellLandblock::OnRoad(const FVector3f& v, const FACEWorldBakePortalRegion& region) const
{
	int32 x0 = (int32)(v.X / region.SquareLength);
	int32 y0 = (int32)(v.Y / region.SquareLength);

	// road min/max
	float rn = region.RoadWidth;
	float rx = region.SquareLength - region.RoadWidth;

	int32 x1 = x0 + 1;
	int32 y1 = y0 + 1;

	uint8 r0 = GetRoad(x0, y0);
	uint8 r1 = GetRoad(x0, y1);
	uint8 r2 = GetRoad(x1, y0);
	uint8 r3 = GetRoad(x1, y1);

	if (r0 == r1 == r2 == r3 == 0)
		return false;

	float dx = v.X - (float)x0 * region.SquareLength;
	float dy = v.Y - (float)y0 * region.SquareLength;

	if (r0 > 0)
	{
		if (r1 > 0)
		{
			if (r2 > 0)
			{
				if (r3 > 0)
					return true;
				else
					return dx < rn || dy < rn;
			}
			else
			{
				if (r3 > 0)
					return dx < rn || dy > rx;
				else
					return dx < rn;
			}
		}
		else
		{
			if (r2 > 0)
			{
				if (r3 > 0)
					return dx > rx || dy < rn;
				else
					return dy < rn;
			}
			else
			{
				if (r3 > 0)
					return abs(dx - dy) < rn;
				else
					return (dx + dy) < rn;
			}
		}
	}
	else
	{
		if (r1 > 0)
		{
			if (r2 > 0)
			{
				if (r3 > 0)
					return dx > rx || dy > rx;
				else
					return abs(dx + dy - region.SquareLength) < rn;
			}
			else
			{
				if (r3 > 0)
					return dy > rx;
				else
					return (region.SquareLength + dx - dy) < rn;
			}
		}
		else
		{
			if (r2 > 0)
			{
				if (r3 > 0)
					return dx > rx;
				else
					return (region.SquareLength - dx + dy) < rn;
			}
			else
			{
				if (r3 > 0)
					return (region.SquareLength * 2.0f - dx - dy) < rn;
				else
					return false;
			}
		}
	}
}

float FACEWorldBakeCellLandblock::GetHeight(float PositionX, float PositionY, const TArray<float>& Heights) const
{
	const FVector3f PositionVec(PositionX, PositionY, 0.0f);
	const FPlane4f Plane = GetTrianglePlane(PositionX, PositionY, Heights);
	return FMath::LinePlaneIntersection(PositionVec + FVector3f(0.0f, 0.0f, MIN_flt), PositionVec + FVector3f(0.0f, 0.0f, MAX_flt), Plane).Z;
}

FPlane4f FACEWorldBakeCellLandblock::GetTrianglePlane(float PositionX, float PositionY, const TArray<float>& Heights) const
{
	check(PositionX < kLandblockSize && PositionY < kLandblockSize);
	float IndexXFloat;
	float OffsetX = FMath::Modf(PositionX / kUnitSize, &IndexXFloat);
	uint32 IndexX = static_cast<int>(IndexXFloat);

	float IndexYFloat;
	float OffsetY = FMath::Modf(PositionY / kUnitSize, &IndexYFloat);
	uint32 IndexY = static_cast<int>(IndexYFloat);

	FVector3f Vert00(IndexXFloat * kUnitSize, IndexY * kUnitSize, GetHeightAt(IndexX, IndexY, Heights));
	FVector3f Vert10((IndexXFloat + 1) * kUnitSize, IndexY * kUnitSize, GetHeightAt(FMath::Min(IndexX + 1, kDimension - 1), IndexY, Heights));
	FVector3f Vert01(IndexXFloat * kUnitSize, (IndexY + 1) * kUnitSize, GetHeightAt(IndexXFloat, FMath::Min(IndexY + 1, kDimension - 1), Heights));
	FVector3f Vert11((IndexXFloat + 1) * kUnitSize, (IndexY + 1) * kUnitSize, GetHeightAt(FMath::Min(IndexX + 1, kDimension - 1), FMath::Min(IndexY + 1, kDimension - 1), Heights));

	FVector3f PositionVec(PositionX, PositionY, 0.0f);
	FPlane4f Plane;
	if (OffsetY > 1.0 - OffsetX)
	{
		Plane = FPlane4f(Vert11, Vert01, Vert00);
	}
	else
	{
		Plane = FPlane4f(Vert00, Vert10, Vert11);
	}

	return Plane;
}

FACEWorldBakeCellStructure::FACEWorldBakeCellStructure(FACEWorldBakeByteReader& Reader)
: Identifier(0)
, SurfaceCount(0)
, EnvironmentId(0)
{
	Identifier = Reader.ReadUint32();

	uint32 Flags = Reader.ReadUint32();
	check(Flags <= 0xF);

	uint32 ResourceId2 = Reader.ReadUint32();
	check(ResourceId2 == Identifier);

	SurfaceCount = Reader.ReadUint8();

	uint8 numConnected = Reader.ReadUint8();
	uint16 numVisible = Reader.ReadUint16();

	for (uint8 SurfaceIndex = 0; SurfaceIndex < SurfaceCount; ++SurfaceIndex)
	{
		SurfaceIDs.Add(Reader.ReadUint16());
	}

	EnvironmentId = Reader.ReadUint16();
	uint16 partNum_ = Reader.ReadUint16();
	Location = Reader.ReadValue<FACEWorldBakeResourceLocation>();

	for (uint8 i = 0; i < numConnected; i++)
	{
		uint16 portalSide = Reader.ReadUint16();
		uint16 portalId = Reader.ReadUint16();
		uint16 cellId = Reader.ReadUint16();
		uint16 exactMatch = Reader.ReadUint16();
	}

	for (uint16 i = 0; i < numVisible; i++)
	{
		uint16 cellId = Reader.ReadUint16();
	}

	if (Flags & kHasStaticObjects)
	{
		uint32 NumStaticObjects = Reader.ReadUint32();

		for (uint8 StaticObjectIndex = 0; StaticObjectIndex < NumStaticObjects; ++StaticObjectIndex)
		{
			FACEWorldBakeResourceInfo* StaticObject = Reader.ReadStructure<FACEWorldBakeResourceInfo>();
			StaticObjects.Add(*StaticObject);
		}
	}

	if (Flags & kHasRestrictionObject)
	{
		Reader.ReadUint32();
	}

	check(Reader.BytesLeft() == 0);
}

FACEWorldBakeCellStructure::FACEWorldBakeCellStructure()
: Identifier(0)
, SurfaceCount(0)
, EnvironmentId(0)
{

}

FACEWorldBakeCellLandblockObject::FACEWorldBakeCellLandblockObject(FACEWorldBakeByteReader& Reader)
: Identifier(0)
, StructureCount(0)
, StaticObjectCount(0)
, Unk1(0)
{
	Identifier = Reader.ReadUint32();
	StructureCount = Reader.ReadUint32();		// cell count
	StaticObjectCount = Reader.ReadUint16();	// this should be int32?
	Unk1 = Reader.ReadUint16();
	check(0 == Unk1);

	for (int32 ObjectIndex = 0; ObjectIndex < StaticObjectCount; ++ObjectIndex)
	{
		FACEWorldBakeResourceInfo* StaticObject = Reader.ReadStructure<FACEWorldBakeResourceInfo>();
		StaticObjects.Add(*StaticObject);
	}

	StaticObjectExCount = Reader.ReadUint16();	// building count
	uint16 Unk2 = Reader.ReadUint16();			// flags (restriction table)
	check((0 == Unk2) || (1 == Unk2));

	for (int32 ObjectIndex = 0; ObjectIndex < StaticObjectExCount; ++ObjectIndex)
	{
		FACEWorldBakeResourceInfo* staticObject = Reader.ReadStructure<FACEWorldBakeResourceInfo>();
		StaticObjectsEx.Add(*staticObject);

		uint32 Unk3 = Reader.ReadUint32();			// leaves
		uint32 NumPortals = Reader.ReadUint32();
		for (uint32 PortalIndex = 0; PortalIndex < NumPortals; ++PortalIndex)
		{
			// should be:
			// u16: flags
			// u16: cell
			// u16: other portal id
			// u16: stab count
			// u16[]: stabs

			uint32 Unk4 = Reader.ReadUint32();
			uint16 Unk5 = Reader.ReadUint16();
			uint16 NumVisible = Reader.ReadUint16();

			for (uint32 VisibleIndex = 0; VisibleIndex < NumVisible; ++VisibleIndex)
			{
				uint16 CellId = Reader.ReadUint16();
			}

			Reader.Align();
		}
	}

	//check(0 == Reader.BytesLeft());
}

FACEWorldBakeCellLandblockObject::FACEWorldBakeCellLandblockObject()
{
}

FACEWorldBakeCellFile::FACEWorldBakeCellFile(FACEWorldBakeDatFilePtr InCellFile)
: CellFile(InCellFile)
{
	check(2 == CellFile->GetHeader().Version); // must be a cell file
}

const FACEWorldBakeCellLandblock* FACEWorldBakeCellFile::GetLandblock(uint32 LandblockX, uint32 LandblockY) const
{
	const uint32 LandblockIdentifier = (LandblockX << 24) | (LandblockY << 16) | 0x0000ffff;

	TByteArrayPtr FileData = CellFile->GetFileData(LandblockIdentifier);
	if (FileData.IsValid())
	{
		check(sizeof(FACEWorldBakeCellLandblock) == FileData->Num());
		return reinterpret_cast<FACEWorldBakeCellLandblock*>(FileData->GetData());
	}

	return nullptr;
}

bool FACEWorldBakeCellFile::GetLandblockObject(uint32 LandblockX, uint32 LandblockY, FACEWorldBakeCellLandblockObject& outLandblockInfos) const
{
	const uint32 LandblockIdentifier = (LandblockX << 24) | (LandblockY << 16) | 0x0000fffe;

	TByteArrayPtr FileData = CellFile->GetFileData(LandblockIdentifier);
	if (FileData.IsValid())
	{
		FACEWorldBakeByteReader Reader(FileData);
		outLandblockInfos = FACEWorldBakeCellLandblockObject(Reader);
		return true;
	}

	return false;
}

int32 FACEWorldBakeCellFile::GetLandblockStructures(const FACEWorldBakeCellLandblockObject& LandblockObject, TArray<FACEWorldBakeCellStructure>& OutLandblockStructures) const
{
	int32 NumAdded = 0;

	for (uint32 StructureIndex = 0; StructureIndex < LandblockObject.StructureCount; ++StructureIndex)
	{
		const uint32 LandblockIdentiferHighBits = LandblockObject.Identifier & 0xFFFF0000;
		const uint32 LandblockStructureIdentifer = LandblockIdentiferHighBits | (0x0000100 + StructureIndex);

		TByteArrayPtr FileData = CellFile->GetFileData(LandblockStructureIdentifer);
		if (FileData.IsValid())
		{
			FACEWorldBakeByteReader Reader(FileData);
			OutLandblockStructures.Add(FACEWorldBakeCellStructure(Reader));
			++NumAdded;
		}
	}

	return NumAdded;
}
