#include "ACEWorldBakeDatModel.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

bool ValidEnum(EACEWorldBakePortalModelFlags::Type Value)
{
	const uint32 kAllFlags = EACEWorldBakePortalModelFlags::kNone | EACEWorldBakePortalModelFlags::kHasPhysicsBSP | EACEWorldBakePortalModelFlags::kHasDrawingBSP | EACEWorldBakePortalModelFlags::kHasDegrade;
	return 0 == (~kAllFlags & Value);
}

bool ValidEnum(EACEWorldBakePortalModelStippling::Type Value)
{
	switch (Value)
	{
	case EACEWorldBakePortalModelStippling::kNoStippling:
	case EACEWorldBakePortalModelStippling::kPositiveStippling:
	//case EACEWorldBakeStippling::kNegativeStippling:
	case EACEWorldBakePortalModelStippling::kNoPosUVs:
	//case EACEWorldBakeStippling::kNoNegUVs:
		return true;
	}

	return false;
}

bool ValidEnum(EACEWorldBakePortalModelSides::Type Value)
{
	switch (Value)
	{
	case EACEWorldBakePortalModelSides::kSingle:
	case EACEWorldBakePortalModelSides::kDouble:
	case EACEWorldBakePortalModelSides::kBoth:
		return true;
	}

	return false;
}

bool ValidEnum(EACEWorldBakePortalModelBSPNode::Type Value)
{
	switch (Value)
	{
	//case EACEWorldBakeBSPNode::kNone: // our fake null
	case EACEWorldBakePortalModelBSPNode::kLeaf:
	case EACEWorldBakePortalModelBSPNode::kPortal:
	case EACEWorldBakePortalModelBSPNode::kBpnn:
	case EACEWorldBakePortalModelBSPNode::kBPIn:
	case EACEWorldBakePortalModelBSPNode::kBpIN:
	case EACEWorldBakePortalModelBSPNode::kBpnN:
	case EACEWorldBakePortalModelBSPNode::kBPIN:
	case EACEWorldBakePortalModelBSPNode::kBPnN:
	case EACEWorldBakePortalModelBSPNode::kUnkn1:
	case EACEWorldBakePortalModelBSPNode::kUnkn2:
	case EACEWorldBakePortalModelBSPNode::kUnkn3: // appears in EACEWorldBakeResource::Environment
		return true;
	}

	return false;
}

void ReadBSP(FACEWorldBakeByteReader& Reader, TACEWorldBakeBSPNodePtr& Node, EACEWorldBakePortalModelBSPTree::Type TreeType)
{
	EACEWorldBakePortalModelBSPNode::Type NodeType = Reader.ReadValue<EACEWorldBakePortalModelBSPNode::Type>();
	check(ValidEnum(NodeType));

	if (EACEWorldBakePortalModelBSPNode::kLeaf == NodeType)
	{
		Node = MakeShared<FACEWorldBakePortalModelBSPLeaf>(TreeType, Reader);
	}
	else if (EACEWorldBakePortalModelBSPNode::kPortal == NodeType)
	{
		Node = MakeShared<FACEWorldBakePortalModelBSPPortal>(TreeType, Reader);
	}
	else
	{
		Node = MakeShared<FACEWorldBakePortalModelBSPNode>(TreeType, NodeType, Reader);
	}
}

TSet<TACEWorldBakeBSPNodePtr> WalkBSP(TACEWorldBakeBSPNodePtr Node, EACEWorldBakePortalModelBSPNode::Type NodeType)
{
	TSet<TACEWorldBakeBSPNodePtr> NodesOfType;
	if (Node)
	{
		if (Node->BackChild)
		{
			NodesOfType.Append(WalkBSP(Node->BackChild, NodeType));
		}

		if (Node->FrontChild)
		{
			NodesOfType.Append(WalkBSP(Node->FrontChild, NodeType));
		}

		if (Node->Node == NodeType)
		{
			NodesOfType.Add(Node);
		}
	}

	return NodesOfType;
}

FACEWorldBakePortalModelVertex::FACEWorldBakePortalModelVertex()
: Index(INDEX_NONE)
, NumTexCoords(0)
, Position(FVector3f::ZeroVector)
, Normal(FVector3f::ZeroVector)
{
}

FACEWorldBakePortalModelVertex::FACEWorldBakePortalModelVertex(const FACEWorldBakePortalModelVertex& Other)
: Index(Other.Index)
, NumTexCoords(Other.NumTexCoords)
, Position(Other.Position)
, Normal(Other.Normal)
, UVs(Other.UVs)
{
}

FACEWorldBakePortalModelVertex::FACEWorldBakePortalModelVertex(FACEWorldBakeByteReader& Reader)
: Index(Reader.ReadUint16())
, NumTexCoords(Reader.ReadUint16())
, Position(Reader.ReadValue<FVector3f>())
, Normal(Reader.ReadValue<FVector3f>())
{
	for (uint16 TexCoordIndex = 0; TexCoordIndex < NumTexCoords; ++TexCoordIndex)
	{
		UVs.Add(Reader.ReadValue<FVector2f>());
	}
}

FACEWorldBakePortalModelVertex& FACEWorldBakePortalModelVertex::operator=(const FACEWorldBakePortalModelVertex& Other)
{
	Index = Other.Index;
	NumTexCoords = Other.NumTexCoords;
	Position = Other.Position;
	Normal = Other.Normal;
	UVs = Other.UVs;
	return *this;
}

FACEWorldBakePortalModelTriangleFan::FACEWorldBakePortalModelTriangleFan()
: FanIndex(INDEX_NONE)
, NumIndices(0)
, Stippling(EACEWorldBakePortalModelStippling::kNoStippling)
, Sides(EACEWorldBakePortalModelSides::kSingle)
, SurfaceIndex(INDEX_NONE)
, SurfaceUnk(0)
{
}

FACEWorldBakePortalModelTriangleFan::FACEWorldBakePortalModelTriangleFan(const FACEWorldBakePortalModelTriangleFan& Other)
: FanIndex(Other.FanIndex)
, NumIndices(Other.NumIndices)
, Stippling(Other.Stippling)
, Sides(Other.Sides)
, SurfaceIndex(Other.SurfaceIndex)
, SurfaceUnk(Other.SurfaceUnk)
, VertexIndices(Other.VertexIndices)
, TexCoordIndices(Other.TexCoordIndices)
{
}

FACEWorldBakePortalModelTriangleFan::FACEWorldBakePortalModelTriangleFan(FACEWorldBakeByteReader& Reader)
: FanIndex(Reader.ReadUint16())
, NumIndices(Reader.ReadUint8())
, Stippling(Reader.ReadValue<EACEWorldBakePortalModelStippling::Type>())
, Sides(Reader.ReadValue<EACEWorldBakePortalModelSides::Type>())
, SurfaceIndex(Reader.ReadUint16())
, SurfaceUnk(Reader.ReadUint16())
{
	check(ValidEnum(Stippling));
	check(ValidEnum(Sides));

	VertexIndices.Reserve(NumIndices);
	for (uint8 Index = 0; Index < NumIndices; ++Index)
	{
		VertexIndices.Add(Reader.ReadUint16());
	}

	if (EACEWorldBakePortalModelStippling::kNoPosUVs != Stippling)
	{
		TexCoordIndices.Reserve(NumIndices);
		for (uint16 Index = 0; Index < NumIndices; ++Index)
		{
			TexCoordIndices.Add(Reader.ReadUint8());
		}
	}

	if (EACEWorldBakePortalModelSides::kBoth == Sides)
	{
		for (uint8 Index = 0; Index < NumIndices; ++Index)
		{
			Reader.ReadUint8();
		}
	}
}

FACEWorldBakePortalModelTriangleFan& FACEWorldBakePortalModelTriangleFan::operator=(const FACEWorldBakePortalModelTriangleFan& Other)
{
	FanIndex = Other.FanIndex;
	NumIndices = Other.NumIndices;
	Stippling = Other.Stippling;
	Sides = Other.Sides;
	SurfaceIndex = Other.SurfaceIndex;
	SurfaceUnk = Other.SurfaceUnk;
	VertexIndices = Other.VertexIndices;
	TexCoordIndices = Other.TexCoordIndices;
	return *this;
}

bool FACEWorldBakePortalModelTriangleFan::operator<(const FACEWorldBakePortalModelTriangleFan& Other) const
{
	return (SurfaceIndex < Other.SurfaceIndex) || ((SurfaceIndex == Other.SurfaceIndex) && (FanIndex < Other.FanIndex));
}

FACEWorldBakePortalModelBSPNode::FACEWorldBakePortalModelBSPNode(EACEWorldBakePortalModelBSPTree::Type TreeType, EACEWorldBakePortalModelBSPNode::Type PlaneType)
: Tree(TreeType)
, Node(PlaneType)
{
}

FACEWorldBakePortalModelBSPNode::FACEWorldBakePortalModelBSPNode(const FACEWorldBakePortalModelBSPNode& Other)
: Tree(Other.Tree)
, Node(Other.Node)
, Bounds(Other.Bounds)
, Partition(Other.Partition)
, FrontChild(Other.FrontChild)
, BackChild(Other.BackChild)
, TriangleIndices(Other.TriangleIndices)
{
}

FACEWorldBakePortalModelBSPNode::FACEWorldBakePortalModelBSPNode(EACEWorldBakePortalModelBSPTree::Type TreeType, EACEWorldBakePortalModelBSPNode::Type PlaneType, FACEWorldBakeByteReader& Reader)
: Tree(TreeType)
, Node(PlaneType)
{
	const FVector3f PartitionNormal = Reader.ReadValue<FVector3f>();
	const float ParitionW = Reader.ReadFloat();
	Partition = FPlane4f(PartitionNormal, ParitionW);

	if (EACEWorldBakePortalModelBSPNode::kBpnn == Node || EACEWorldBakePortalModelBSPNode::kBPIn == Node)
	{
		ReadBSP(Reader, FrontChild, Tree);
	}
	else if (EACEWorldBakePortalModelBSPNode::kBpIN == Node || EACEWorldBakePortalModelBSPNode::kBpnN == Node)
	{
		ReadBSP(Reader, BackChild, Tree);
	}
	else if (EACEWorldBakePortalModelBSPNode::kBPIN == Node || EACEWorldBakePortalModelBSPNode::kBPnN == Node)
	{
		ReadBSP(Reader, FrontChild, Tree);
		ReadBSP(Reader, BackChild, Tree);
	}

	if (EACEWorldBakePortalModelBSPTree::kDrawing == Tree || EACEWorldBakePortalModelBSPTree::kPhysics == Tree)
	{
		const FVector3f BoundsCenter = Reader.ReadValue<FVector3f>();
		const float BoundsRadius = Reader.ReadFloat();
		Bounds = FSphere3f(BoundsCenter, BoundsRadius);
	}

	if (EACEWorldBakePortalModelBSPTree::kDrawing == Tree)
	{
		uint32 triCount = Reader.ReadUint32();
		TriangleIndices.Reserve(triCount);

		for (uint32 TriangleIndex = 0; TriangleIndex < triCount; ++TriangleIndex)
		{
			TriangleIndices.Add(Reader.ReadUint16());
		}
	}
}

FACEWorldBakePortalModelBSPNode& FACEWorldBakePortalModelBSPNode::operator=(const FACEWorldBakePortalModelBSPNode& Other)
{
	Tree = Other.Tree;
	Node = Other.Node;
	Bounds = Other.Bounds;
	Partition = Other.Partition;
	FrontChild = Other.FrontChild;
	BackChild = Other.BackChild;
	TriangleIndices = Other.TriangleIndices;
	return *this;
}

FACEWorldBakePortalModelBSPLeaf::FACEWorldBakePortalModelBSPLeaf(EACEWorldBakePortalModelBSPTree::Type TreeType)
: FACEWorldBakePortalModelBSPNode(TreeType, EACEWorldBakePortalModelBSPNode::kLeaf)
, Index(INDEX_NONE)
, Solid(INDEX_NONE)
{
}

FACEWorldBakePortalModelBSPLeaf::FACEWorldBakePortalModelBSPLeaf(const FACEWorldBakePortalModelBSPLeaf& Other)
: FACEWorldBakePortalModelBSPNode(Other)
, Index(Other.Index)
, Solid(Other.Solid)
{
}

FACEWorldBakePortalModelBSPLeaf::FACEWorldBakePortalModelBSPLeaf(EACEWorldBakePortalModelBSPTree::Type TreeType, FACEWorldBakeByteReader& Reader)
: FACEWorldBakePortalModelBSPNode(TreeType, EACEWorldBakePortalModelBSPNode::kLeaf)
, Index(Reader.ReadUint32())
, Solid(0)
{
	if(EACEWorldBakePortalModelBSPTree::kPhysics == Tree)
	{
		Solid = Reader.ReadUint32();

		const FVector3f BoundsCenter = Reader.ReadValue<FVector3f>();
		const float BoundsRadius = Reader.ReadFloat();
		Bounds = FSphere3f(BoundsCenter, BoundsRadius);

		uint32 triCount = Reader.ReadUint32();
		TriangleIndices.Reserve(triCount);

		for (uint32 TriangleIndex = 0; TriangleIndex < triCount; ++TriangleIndex)
		{
			TriangleIndices.Add(Reader.ReadUint16());
		}
	}
}

FACEWorldBakePortalModelBSPLeaf& FACEWorldBakePortalModelBSPLeaf::operator=(const FACEWorldBakePortalModelBSPLeaf& Other)
{
	FACEWorldBakePortalModelBSPNode::operator=(Other);
	Index = Other.Index;
	Solid = Other.Solid;
	return *this;
}

FACEWorldBakePortalModelBSPPortalPoly::FACEWorldBakePortalModelBSPPortalPoly(uint16 InPortalIndex, uint16 InPolygonIndex)
: PortalIndex(InPortalIndex)
, PolygonIndex(InPolygonIndex)
{
}

FACEWorldBakePortalModelBSPPortalPoly::FACEWorldBakePortalModelBSPPortalPoly(const FACEWorldBakePortalModelBSPPortalPoly& Other)
: PortalIndex(Other.PortalIndex)
, PolygonIndex(Other.PolygonIndex)
{
}

FACEWorldBakePortalModelBSPPortalPoly::FACEWorldBakePortalModelBSPPortalPoly(FACEWorldBakeByteReader& Reader)
: PortalIndex(Reader.ReadUint16())
, PolygonIndex(Reader.ReadUint16())
{
}

FACEWorldBakePortalModelBSPPortalPoly& FACEWorldBakePortalModelBSPPortalPoly::operator=(const FACEWorldBakePortalModelBSPPortalPoly& Other)
{
	PortalIndex = Other.PortalIndex;
	PolygonIndex = Other.PolygonIndex;
	return *this;
}

FACEWorldBakePortalModelBSPPortal::FACEWorldBakePortalModelBSPPortal(EACEWorldBakePortalModelBSPTree::Type TreeType)
: FACEWorldBakePortalModelBSPNode(TreeType, EACEWorldBakePortalModelBSPNode::kPortal)
{
}

FACEWorldBakePortalModelBSPPortal::FACEWorldBakePortalModelBSPPortal(const FACEWorldBakePortalModelBSPPortal& Other)
: FACEWorldBakePortalModelBSPNode(Other)
, PortalPolys(Other.PortalPolys)
{
}

FACEWorldBakePortalModelBSPPortal::FACEWorldBakePortalModelBSPPortal(EACEWorldBakePortalModelBSPTree::Type TreeType, FACEWorldBakeByteReader& Reader)
: FACEWorldBakePortalModelBSPNode(TreeType, EACEWorldBakePortalModelBSPNode::kPortal)
{
	const FVector3f PartitionNormal = Reader.ReadValue<FVector3f>();
	const float ParitionW = Reader.ReadFloat();
	Partition = FPlane4f(PartitionNormal, ParitionW);
	ReadBSP(Reader, FrontChild, Tree);
	ReadBSP(Reader, BackChild, Tree);

	if (EACEWorldBakePortalModelBSPTree::kDrawing == Tree)
	{
		const FVector3f BoundsCenter = Reader.ReadValue<FVector3f>();
		const float BoundsRadius = Reader.ReadFloat();
		Bounds = FSphere3f(BoundsCenter, BoundsRadius);

		uint32 TriCount = Reader.ReadUint32();
		TriangleIndices.Reserve(TriCount);

		uint32 polyCount = Reader.ReadUint32();
		PortalPolys.Reserve(polyCount);

		for (uint32 TriangleIndex = 0; TriangleIndex < TriCount; ++TriangleIndex)
		{
			TriangleIndices.Add(Reader.ReadUint16());
		}

		for (uint32 PolyIndex = 0; PolyIndex < polyCount; ++PolyIndex)
		{
			uint16 PortalIndex = Reader.ReadUint16();
			uint16 PolygonIndex = Reader.ReadUint16();
			PortalPolys.Add(FACEWorldBakePortalModelBSPPortalPoly(PortalIndex, PolygonIndex));
		}
	}
}

FACEWorldBakePortalModelBSPPortal& FACEWorldBakePortalModelBSPPortal::operator=(const FACEWorldBakePortalModelBSPPortal& Other)
{
	FACEWorldBakePortalModelBSPNode::operator=(Other);
	PortalPolys = Other.PortalPolys;
	return *this;
}

FACEWorldBakePortalModel::FACEWorldBakePortalModel()
: ResourceId(INDEX_NONE)
, Flags(EACEWorldBakePortalModelFlags::kNone)
, SurfaceCount(0)
, NumCollisionFans(0)
, NumRenderFans(0)
{
}

FACEWorldBakePortalModel::FACEWorldBakePortalModel(const FACEWorldBakePortalModel& Other)
: ResourceId(Other.ResourceId)
, Flags(Other.Flags)
, SurfaceCount(Other.SurfaceCount)
, SurfaceIDs(Other.SurfaceIDs)
, Vertices(Other.Vertices)
, NumCollisionFans(Other.NumCollisionFans)
, CollisionFans(Other.CollisionFans)
, CollisionBSP(Other.CollisionBSP)
, NumRenderFans(Other.NumRenderFans)
, RenderFans(Other.RenderFans)
, RenderBSP(Other.RenderBSP)
, PortalNodes(Other.PortalNodes)
{
}

FACEWorldBakePortalModel::FACEWorldBakePortalModel(FACEWorldBakeByteReader& Reader)
: ResourceId(Reader.ReadUint32())
, Flags(Reader.ReadValue<EACEWorldBakePortalModelFlags::Type>())
, SurfaceCount(Reader.ReadUint8())
, NumCollisionFans(0)
, NumRenderFans(0)
{
	check(ValidEnum(Flags));

	for (uint8 SurfaceIndex = 0; SurfaceIndex < SurfaceCount; ++SurfaceIndex)
	{
		SurfaceIDs.Add(Reader.ReadUint32());
	}

	uint32 AlwaysOne = Reader.ReadUint32();
	check(1 == AlwaysOne);

	uint16 NumVertices = Reader.ReadUint16();
	uint16 flags2 = Reader.ReadUint16();
	check(0 == flags2 || 0x8000 == flags2);

	for (uint16 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		Vertices.Add(FACEWorldBakePortalModelVertex(Reader));
	}

	if (EACEWorldBakePortalModelFlags::kHasPhysicsBSP & Flags)
	{
		NumCollisionFans = Reader.ReadPackedUint16();
		CollisionFans.Reserve(NumCollisionFans);
		for (uint16 CollisionFan = 0; CollisionFan < NumCollisionFans; ++CollisionFan)
		{
			CollisionFans.Add(FACEWorldBakePortalModelTriangleFan(Reader));
		}

		ReadBSP(Reader, CollisionBSP, EACEWorldBakePortalModelBSPTree::kPhysics);
	}

	FVector3f SortCenter = Reader.ReadValue<FVector3f>();

	if (EACEWorldBakePortalModelFlags::kHasDrawingBSP & Flags)
	{
		NumRenderFans = Reader.ReadPackedUint16();
		RenderFans.Reserve(NumRenderFans);
		for (uint16 RenderFan = 0; RenderFan < NumRenderFans; ++RenderFan)
		{
			RenderFans.Add(FACEWorldBakePortalModelTriangleFan(Reader));
		}

		ReadBSP(Reader, RenderBSP, EACEWorldBakePortalModelBSPTree::kDrawing);

		TSet<TACEWorldBakeBSPNodePtr> RenderPortalNodes = WalkBSP(RenderBSP, EACEWorldBakePortalModelBSPNode::kPortal);
		if (0 < RenderPortalNodes.Num())
		{
			PortalNodes.Append(RenderPortalNodes.Array());
		}
	}

	if (EACEWorldBakePortalModelFlags::kHasDegrade & Flags)
	{
		uint32 HasDegrade = Reader.ReadUint32();
	}

	check(0 == Reader.BytesLeft());
}

FACEWorldBakePortalModel& FACEWorldBakePortalModel::operator=(const FACEWorldBakePortalModel& Other)
{
	ResourceId = Other.ResourceId;
	Flags = Other.Flags;
	SurfaceCount = Other.SurfaceCount;
	SurfaceIDs = Other.SurfaceIDs;
	Vertices = Other.Vertices;
	NumCollisionFans = Other.NumCollisionFans;
	CollisionFans = Other.CollisionFans;
	CollisionBSP = Other.CollisionBSP;
	NumRenderFans = Other.NumRenderFans;
	RenderFans = Other.RenderFans;
	RenderBSP = Other.RenderBSP;
	PortalNodes = Other.PortalNodes;
	return *this;
}
