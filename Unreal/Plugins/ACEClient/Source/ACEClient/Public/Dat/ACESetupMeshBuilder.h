#pragma once

#include "CoreMinimal.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACEDatTextureResolver.h"
#include "ACETypes.h"

struct FACEBuiltMeshSection
{
	uint32 SurfaceId = 0;
	/** Foliage / keyed textures — masked materials. Door openings are solid portal planes, not this. */
	bool bClipMap = false;
	/** Solid ColorValue portal fills (Translucency≈1) — skip draw + collision. */
	bool bFullyTransparent = false;
	/** Collision-only section (physics polys) — not drawn. */
	bool bCollisionOnly = false;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;

	bool IsEmpty() const { return Vertices.Num() == 0 || Triangles.Num() < 3; }
};

struct FACEBuiltSetupPortal
{
	int32 PortalIndex = INDEX_NONE;
	TArray<FVector> Vertices;
	FVector Normal = FVector::ZeroVector; // Polygon normal before CBldPortal sidedness.
};

struct FACEBuiltSetupPart
{
	uint32 GfxObjId = 0;
	uint32 DrawMode = 1;
	float MaxDegradeDistance = 100.f;
	FTransform BindTransform = FTransform::Identity;
	TArray<FACEBuiltMeshSection> Sections;
	TArray<FACEBuiltSetupPortal> Portals;
};

struct FACEBuiltSetupMesh
{
	TArray<FACEBuiltSetupPart> Parts;
	uint32 DefaultMotionTableId = 0;

	bool IsEmpty() const
	{
		if (Parts.Num() == 0)
		{
			return true;
		}
		for (const FACEBuiltSetupPart& P : Parts)
		{
			for (const FACEBuiltMeshSection& S : P.Sections)
			{
				if (!S.IsEmpty())
				{
					return false;
				}
			}
		}
		return true;
	}
};

class ACECLIENT_API FACESetupMeshBuilder
{
public:
	FACESetupMeshBuilder(FACEDatDatabase* InPortal, FACEDatDatabase* InHighRes, FACEDatTextureResolver* InTextures)
		: Portal(InPortal), HighRes(InHighRes), Textures(InTextures)
	{
	}

	bool BuildSetup(uint32 SetupId, float WorldScale, FACEBuiltSetupMesh& OutMesh, int32 PlacementId = 0);
	bool BuildSetupWithAppearance(uint32 SetupId, const FACEObjDesc& Appearance, float WorldScale, FACEBuiltSetupMesh& OutMesh, int32 PlacementId = 0);
	bool GetHoldingLocation(uint32 SetupId, int32 ParentLocation, int32& OutPartIndex, FTransform3f& OutFrame);
	void TrimGfxCache(int32 MaxEntries);
	bool LoadGfxObj(uint32 GfxObjId, FACEDatGfxObj& Out);

private:
	bool BuildGfxObjOnly(uint32 GfxObjId, const FACEObjDesc& Appearance, float WorldScale, FACEBuiltSetupMesh& OutMesh);
	void AppendGfxObjLocal(const FACEDatGfxObj& Gfx, int32 PartIndex, const FACEObjDesc* Appearance, float WorldScale, FACEBuiltSetupPart& OutPart);
	/** Emit invisible collision-only sections from GfxObj PhysicsPolygons (retail HasPhysics). */
	void AppendGfxObjPhysicsLocal(const FACEDatGfxObj& Gfx, float WorldScale, FACEBuiltSetupPart& OutPart);

	FACEDatDatabase* Portal = nullptr;
	FACEDatDatabase* HighRes = nullptr;
	FACEDatTextureResolver* Textures = nullptr;
	TMap<uint32, FACEDatGfxObj> GfxCache;
	// Immutable attachment metadata; local and remote VR rigs query this every frame.
	// The builder is replaced when DAT archives change. Bound rare setup variants.
	TMap<uint32, TMap<int32, FACEDatLocationType>> HoldingLocationCache;
};
