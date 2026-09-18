#pragma once

#include "ACEWorldBakeDatModel.h"

class FACEWorldBakeByteReader;

namespace EACEWorldBakePortalEnvironmentPartSurface {
	enum Type
	{
		kTriangles,
		kHitTriangles,
		kPortals
	};
}

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalEnvironmentPart
{
	FACEWorldBakePortalEnvironmentPart();
	FACEWorldBakePortalEnvironmentPart(FACEWorldBakeByteReader& Reader);

	TArray<FACEWorldBakePortalModelVertex> Vertices;
	TArray<FACEWorldBakePortalModelTriangleFan> TriangleFans;
	TArray<FACEWorldBakePortalModelTriangleFan> HitTriangleFans;
	TArray<uint16> Portals;
	mutable TACEWorldBakeBSPNodePtr HitTree;

	TArray<uint16> GetSurfaceIndices(EACEWorldBakePortalEnvironmentPartSurface::Type SurfaceType = EACEWorldBakePortalEnvironmentPartSurface::kTriangles) const;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalEnvironment
{
	FACEWorldBakePortalEnvironment();
	FACEWorldBakePortalEnvironment(FACEWorldBakeByteReader& Reader);

	//
	uint32 ResourceId;
	//
	uint32 NumParts;
	//
	TArray<FACEWorldBakePortalEnvironmentPart> Parts;

	TArray<uint16> GetPartSurfaceIndices(bool bUnique) const
	{
		TArray<uint16> PartSurfaceIndices;

		for (const FACEWorldBakePortalEnvironmentPart& Part : Parts)
		{
			PartSurfaceIndices.Append(Part.GetSurfaceIndices());
		}

		if (bUnique)
		{
			TSet<uint16> Uniques;
			Uniques.Append(PartSurfaceIndices);
			PartSurfaceIndices = Uniques.Array();
		}

		PartSurfaceIndices.Sort();

		return PartSurfaceIndices;
	}
};
