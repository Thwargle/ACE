#pragma once

class FACEWorldBakeByteReader;

namespace EACEWorldBakePortalModelBSPTree
{
	enum Type : int32
	{
		kDrawing = 0,
		kPhysics = 1,
		kCell = 2
	};
}

namespace EACEWorldBakePortalModelBSPNode
{
	enum Type : uint32
	{
		kNone = 0x0,
		kLeaf = 0x4c454146,
		kPortal = 0x504f5254,
		kBpnn = 0x42506e6e,
		kBPIn = 0x4250496e,
		kBpIN = 0x4270494e,
		kBpnN = 0x42706e4e,
		kBPIN = 0x4250494e,
		kBPnN = 0x42506e4e,
		kUnkn1 = 0x42504f4c,
		kUnkn2 = 0x4270496e,
		kUnkn3 = 0x4250464c,
	};
}

namespace EACEWorldBakePortalModelFlags
{
	enum Type : uint32
	{
		kNone = 0x0,
		kHasPhysicsBSP = 0x1,
		kHasDrawingBSP = 0x2,
		kHasDegrade = 0x8
	};
}

namespace EACEWorldBakePortalModelStippling
{
	enum Type : uint8
	{
		kNoStippling = 0,
		kPositiveStippling = 1,
		kNegativeStippling = 2,
		kNoPosUVs = 4,
		kNoNegUVs = 8,
	};
}

namespace EACEWorldBakePortalModelSides
{
	enum Type : int32
	{
		kSingle = 0,
		kDouble = 1,
		kBoth = 2
	};
}

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalModelVertex
{
	FACEWorldBakePortalModelVertex();
	FACEWorldBakePortalModelVertex(const FACEWorldBakePortalModelVertex& Other);
	FACEWorldBakePortalModelVertex(FACEWorldBakeByteReader& Reader);
	FACEWorldBakePortalModelVertex& operator=(const FACEWorldBakePortalModelVertex& Other);
	//
	uint16 Index;
	//
	uint16 NumTexCoords;
	//
	FVector3f Position;
	//
	FVector3f Normal;
	//
	TArray<FVector2f> UVs;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalModelTriangleFan
{
	FACEWorldBakePortalModelTriangleFan();
	FACEWorldBakePortalModelTriangleFan(const FACEWorldBakePortalModelTriangleFan& Other);
	FACEWorldBakePortalModelTriangleFan(FACEWorldBakeByteReader& Reader);
	FACEWorldBakePortalModelTriangleFan& operator=(const FACEWorldBakePortalModelTriangleFan& Other);
	bool operator<(const FACEWorldBakePortalModelTriangleFan& Other) const;

	uint16 FanIndex;
	uint8 NumIndices;
	EACEWorldBakePortalModelStippling::Type Stippling;
	EACEWorldBakePortalModelSides::Type Sides;
	uint16 SurfaceIndex;
	uint16 SurfaceUnk;
	TArray<uint16> VertexIndices;
	TArray<uint8> TexCoordIndices;
};

typedef TSharedPtr<struct FACEWorldBakePortalModelBSPNode> TACEWorldBakeBSPNodePtr;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalModelBSPNode
{
	FACEWorldBakePortalModelBSPNode(EACEWorldBakePortalModelBSPTree::Type TreeType, EACEWorldBakePortalModelBSPNode::Type PlaneType);
	FACEWorldBakePortalModelBSPNode(const FACEWorldBakePortalModelBSPNode& Other);
	FACEWorldBakePortalModelBSPNode(EACEWorldBakePortalModelBSPTree::Type TreeType, EACEWorldBakePortalModelBSPNode::Type PlaneType, FACEWorldBakeByteReader& Reader);
	FACEWorldBakePortalModelBSPNode& operator=(const FACEWorldBakePortalModelBSPNode& Other);

	EACEWorldBakePortalModelBSPTree::Type Tree;
	EACEWorldBakePortalModelBSPNode::Type Node;
	FSphere3f Bounds;
	FPlane4f Partition;
	mutable TACEWorldBakeBSPNodePtr FrontChild;
	mutable TACEWorldBakeBSPNodePtr BackChild;
	TArray<uint16> TriangleIndices;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalModelBSPLeaf : public FACEWorldBakePortalModelBSPNode
{
	FACEWorldBakePortalModelBSPLeaf(EACEWorldBakePortalModelBSPTree::Type TreeType);
	FACEWorldBakePortalModelBSPLeaf(const FACEWorldBakePortalModelBSPLeaf& Other);
	FACEWorldBakePortalModelBSPLeaf(EACEWorldBakePortalModelBSPTree::Type TreeType, FACEWorldBakeByteReader& Reader);
	FACEWorldBakePortalModelBSPLeaf& operator=(const FACEWorldBakePortalModelBSPLeaf& Other);

	uint32 Index;
	uint32 Solid;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalModelBSPPortalPoly
{
	FACEWorldBakePortalModelBSPPortalPoly(uint16 InPortalIndex, uint16 InPolygonIndex);
	FACEWorldBakePortalModelBSPPortalPoly(const FACEWorldBakePortalModelBSPPortalPoly& Other);
	FACEWorldBakePortalModelBSPPortalPoly(FACEWorldBakeByteReader& Reader);
	FACEWorldBakePortalModelBSPPortalPoly& operator=(const FACEWorldBakePortalModelBSPPortalPoly& Other);

	uint16 PortalIndex;
	uint16 PolygonIndex;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalModelBSPPortal : public FACEWorldBakePortalModelBSPNode
{
	FACEWorldBakePortalModelBSPPortal(EACEWorldBakePortalModelBSPTree::Type TreeType);
	FACEWorldBakePortalModelBSPPortal(const FACEWorldBakePortalModelBSPPortal& Other);
	FACEWorldBakePortalModelBSPPortal(EACEWorldBakePortalModelBSPTree::Type TreeType, FACEWorldBakeByteReader& Reader);
	FACEWorldBakePortalModelBSPPortal& operator=(const FACEWorldBakePortalModelBSPPortal& Other);

	TArray<FACEWorldBakePortalModelBSPPortalPoly> PortalPolys;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalModel
{
	FACEWorldBakePortalModel();
	FACEWorldBakePortalModel(const FACEWorldBakePortalModel& Other);
	FACEWorldBakePortalModel(FACEWorldBakeByteReader& Reader);
	FACEWorldBakePortalModel& operator=(const FACEWorldBakePortalModel& Other);
	//
	uint32 ResourceId;
	//
	EACEWorldBakePortalModelFlags::Type Flags;
	//
	uint8 SurfaceCount;
	//
	TArray<uint32> SurfaceIDs;
	//
	TArray<FACEWorldBakePortalModelVertex> Vertices;
	//
	uint16 NumCollisionFans;
	//
	TArray<FACEWorldBakePortalModelTriangleFan> CollisionFans;
	//
	mutable TACEWorldBakeBSPNodePtr CollisionBSP;
	//
	uint16 NumRenderFans;
	//
	TArray<FACEWorldBakePortalModelTriangleFan> RenderFans;
	//
	mutable TACEWorldBakeBSPNodePtr RenderBSP;
	//
	TArray<TACEWorldBakeBSPNodePtr> PortalNodes;
};

extern void ReadBSP(FACEWorldBakeByteReader& Reader, TACEWorldBakeBSPNodePtr& Node, EACEWorldBakePortalModelBSPTree::Type TreeType);
extern TSet<TACEWorldBakeBSPNodePtr> WalkBSP(TACEWorldBakeBSPNodePtr Node, EACEWorldBakePortalModelBSPNode::Type NodeType);
