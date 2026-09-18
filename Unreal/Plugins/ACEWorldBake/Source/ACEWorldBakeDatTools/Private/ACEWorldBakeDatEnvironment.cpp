#include "ACEWorldBakeDatEnvironment.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

FACEWorldBakePortalEnvironmentPart::FACEWorldBakePortalEnvironmentPart()
{
}

FACEWorldBakePortalEnvironmentPart::FACEWorldBakePortalEnvironmentPart(FACEWorldBakeByteReader& Reader)
{
	uint32 NumTriangleFans = Reader.ReadUint32();
	TriangleFans.Reserve(NumTriangleFans);

	uint32 NumHitTriangleFans = Reader.ReadUint32();
	HitTriangleFans.Reserve(NumHitTriangleFans);

	uint32 NumPortals = Reader.ReadUint32();

	uint32 Unk1 = Reader.ReadUint32();
	check(Unk1 == 1);

	uint32 NumVertices = Reader.ReadUint32();
	Vertices.Reserve(NumVertices);

	for (uint32 i = 0; i < NumVertices; i++)
	{
		Vertices.Add(FACEWorldBakePortalModelVertex(Reader));
	}

	for (uint32 i = 0; i < NumTriangleFans; i++)
	{
		TriangleFans.Add(FACEWorldBakePortalModelTriangleFan(Reader));
	}

	for (uint32 i = 0; i < NumPortals; i++)
	{
		Portals.Add(Reader.ReadUint16());
	}

	Reader.Align();

	TACEWorldBakeBSPNodePtr CellBSP;
	ReadBSP(Reader, CellBSP,  EACEWorldBakePortalModelBSPTree::kCell);

	for (uint32 i = 0; i < NumHitTriangleFans; i++)
	{
		HitTriangleFans.Add(FACEWorldBakePortalModelTriangleFan(Reader));
	}

	TACEWorldBakeBSPNodePtr physicsBSP;
	ReadBSP(Reader, CellBSP, EACEWorldBakePortalModelBSPTree::kPhysics);

	uint32 HasDrawingBSP = Reader.ReadUint32();
	check(HasDrawingBSP == 0 || HasDrawingBSP == 1);

	if (HasDrawingBSP)
	{
		TACEWorldBakeBSPNodePtr DrawingBSP;
		ReadBSP(Reader, DrawingBSP, EACEWorldBakePortalModelBSPTree::kDrawing);
	}

	Reader.Align();
}

TArray<uint16> FACEWorldBakePortalEnvironmentPart::GetSurfaceIndices(EACEWorldBakePortalEnvironmentPartSurface::Type SurfaceType) const
{
	TSet<uint16> SurfaceIndices;
	const TArray<FACEWorldBakePortalModelTriangleFan>& Fans = (EACEWorldBakePortalEnvironmentPartSurface::kHitTriangles == SurfaceType) ? HitTriangleFans : TriangleFans;
	int32 LastSurfaceIndex = INDEX_NONE;

	if (EACEWorldBakePortalEnvironmentPartSurface::kPortals == SurfaceType)
	{
		const TArray<uint16>& PortalFans = Portals;
		for (int32 TriangleFanIndex = 0; TriangleFanIndex < PortalFans.Num(); ++TriangleFanIndex)
		{
			const uint16 SurfaceIndex = Fans[PortalFans[TriangleFanIndex]].SurfaceIndex;
			if (SurfaceIndex != LastSurfaceIndex || INDEX_NONE == LastSurfaceIndex)
			{
				LastSurfaceIndex = SurfaceIndex;
				SurfaceIndices.Add(SurfaceIndex);
			}
		}
	}
	else
	{
		for (int32 TriangleFanIndex = 0; TriangleFanIndex < Fans.Num(); ++TriangleFanIndex)
		{
			const uint16 SurfaceIndex = Fans[TriangleFanIndex].SurfaceIndex;
			if (SurfaceIndex != LastSurfaceIndex || INDEX_NONE == LastSurfaceIndex)
			{
				LastSurfaceIndex = SurfaceIndex;
				SurfaceIndices.Add(SurfaceIndex);
			}
		}
	}

	TArray<uint16> AsArray = SurfaceIndices.Array();
	AsArray.Sort();
	return AsArray;
}

FACEWorldBakePortalEnvironment::FACEWorldBakePortalEnvironment()
{
}

FACEWorldBakePortalEnvironment::FACEWorldBakePortalEnvironment(FACEWorldBakeByteReader& Reader)
: ResourceId(Reader.ReadUint32())
, NumParts(Reader.ReadUint32())
{
	Parts.Reserve(NumParts);

	for (uint32 PartIndex = 0; PartIndex < NumParts; ++PartIndex)
	{
		const uint32 PartIndexRedundant = Reader.ReadUint32();
		Parts.Add(FACEWorldBakePortalEnvironmentPart(Reader));
	}

	check(0 == Reader.BytesLeft());
}
