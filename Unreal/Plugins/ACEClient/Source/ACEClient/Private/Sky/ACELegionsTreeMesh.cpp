#include "Sky/ACELegionsTreeFit.h"
#include "Sky/ACELegionsEnvironment.h"
#include "Dat/ACESetupMeshBuilder.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

// AC2 positions, normals and UVs share a native index. A corner only needs a separate
// render vertex when the source already split it (for example at a texture seam).
static void BuildLegionsTreeLevel(FMeshDescription& Mesh, const TArray<ACELegionsSky::FTreeSection>& Sections,
	const TArray<FVector3f>& Positions, const TArray<FVector3f>& Normals, const TArray<FVector2D>& UVs)
{
	FStaticMeshAttributes Attributes(Mesh);
	Attributes.Register();
	auto VertexPositions = Attributes.GetVertexPositions();
	auto VertexNormals = Attributes.GetVertexInstanceNormals();
	auto VertexUVs = Attributes.GetVertexInstanceUVs();
	auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	VertexUVs.SetNumChannels(1);

	TArray<FVertexInstanceID> Instances;
	Instances.Init(FVertexInstanceID(INDEX_NONE), Positions.Num());
	for (int32 Slot = 0; Slot < Sections.Num(); ++Slot)
	{
		const FPolygonGroupID Group = Mesh.CreatePolygonGroup();
		SlotNames[Group] = FName(*FString::Printf(TEXT("ACE_%d"), Slot));
		const auto& Indices = Sections[Slot].Indices;
		Mesh.ReserveNewTriangles(Indices.Num() / 3);
		for (int32 I = 0; I < Indices.Num(); I += 3)
		{
			FVertexInstanceID Corners[3];
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 Index = Indices[I + K];
				FVertexInstanceID& Instance = Instances[Index];
				if (Instance.GetValue() == INDEX_NONE)
				{
					const FVertexID Vertex = Mesh.CreateVertex();
					VertexPositions[Vertex] = Positions[Index];
					Instance = Mesh.CreateVertexInstance(Vertex);
					VertexNormals[Instance] = Normals[Index];
					VertexUVs.Set(Instance, 0, FVector2f(UVs[Index]));
				}
				Corners[K] = Instance;
			}
			Mesh.CreateTriangle(Group, Corners);
		}
	}
}

bool ACELegionsSky::BuildTreeRenderLevels(const FACEBuiltSetupMesh& Setup, const FTree& Tree,
	TIndirectArray<FMeshDescription>& TreeMeshes)
{
	// Only the AC1 silhouette is needed for fitting. Its textures, normals and UVs
	// can stay unloaded unless the replacement fails and the native mesh is drawn.
	FBox Original(ForceInit);
	TArray<FVector> Points;
	TArray<FIntVector> Faces;
	for (const auto& Part : Setup.Parts)
	{
		for (const auto& Section : Part.Sections)
		{
			if (Section.bCollisionOnly || Section.bFullyTransparent || Section.IsEmpty()) continue;
			const int32 Start = Points.Num();
			Points.Reserve(Start + Section.Vertices.Num());
			for (const FVector& V : Section.Vertices)
			{
				const FVector P = Part.BindTransform.TransformPosition(V);
				Points.Add(P);
				Original += P;
			}
			for (int32 I = 0; I + 2 < Section.Triangles.Num(); I += 3)
			{
				const int32 A = Section.Triangles[I], B = Section.Triangles[I+1], C = Section.Triangles[I+2];
				if (Section.Vertices.IsValidIndex(A) && Section.Vertices.IsValidIndex(B) && Section.Vertices.IsValidIndex(C))
					Faces.Emplace(Start+A, Start+B, Start+C);
			}
		}
	}
	if (Faces.IsEmpty() || Original.GetSize().GetMin() <= UE_SMALL_NUMBER) return false;
	auto OriginalTriangles = [&](auto Visit)
	{
		for (const FIntVector& Face : Faces) Visit(Points[Face.X], Points[Face.Y], Points[Face.Z]);
	};
	auto ImportedTriangles = [&](auto Visit)
	{
		for (const auto& S : Tree.Sections)
			for (int32 I=0; I<S.Indices.Num(); I+=3)
				Visit(Tree.Vertices[S.Indices[I]],Tree.Vertices[S.Indices[I+1]],Tree.Vertices[S.Indices[I+2]]);
	};
	const ACELegionsSky::FTreeSilhouette TargetShape(Original,OriginalTriangles), SourceShape(Tree.Bounds,ImportedTriangles);
	const FQuat Rotation = TargetShape.RotationFrom(SourceShape);
	// Fit the planted tree inside its AC1 height and crown footprint using one
	// scale. Independently fitting X/Y/Z turns palms into flat fans or spears.
	FBox Turned(ForceInit);
	for (const auto& S : Tree.Sections)
		for (int32 Index : S.Indices) Turned += Rotation.RotateVector(Tree.Vertices[Index]);
	const FVector Fit = Original.GetSize() / Turned.GetSize();
	const bool Planted = Tree.Bounds.Min.Z < 0 && Original.Min.Z < 0 &&
		Tree.Bounds.Max.Z > UE_SMALL_NUMBER && Original.Max.Z > 0;
	const double Scale = FMath::Min3(Fit.X, Fit.Y, Planted ? Original.Max.Z / Tree.Bounds.Max.Z : Fit.Z);
	const FVector Base = Rotation.RotateVector(FVector(SourceShape.Trunk,0)) * Scale;
	const FVector Offset(TargetShape.Trunk.X-Base.X,TargetShape.Trunk.Y-Base.Y,
		Planted ? 0. : Original.Min.Z - Scale * Tree.Bounds.Min.Z);
	// The planting point can be on a roof. Some AC2 trunks have a long buried
	// spike that would otherwise pass through the ceiling of the room below.
	const double RootBottom = Turned.Min.Z * Scale + Offset.Z;
	const bool bShortenRoot = Planted && RootBottom < Original.Min.Z - UE_SMALL_NUMBER;
	double RootHinge = 0.;
	if (bShortenRoot)
	{
		// Keep the lowest ring above the AC1 base fixed, along with everything above it.
		for (const auto& S : Tree.Sections)
			for (int32 Index : S.Indices)
			{
				const double Z = Tree.Vertices[Index].Z * Scale;
				if (Z > Original.Min.Z && Z < RootHinge) RootHinge = Z;
			}
	}
	const double RootScale = bShortenRoot ? (RootHinge - Original.Min.Z) / (RootHinge - RootBottom) : 1.;
	TArray<FVector3f> Positions, Normals;
	Positions.Reserve(Tree.Vertices.Num());
	Normals.Reserve(Tree.Vertices.Num());
	for (int32 I = 0; I < Tree.Vertices.Num(); ++I)
	{
		FVector P = Rotation.RotateVector(Tree.Vertices[I]) * Scale + Offset;
		FVector N = Rotation.RotateVector(Tree.Normals[I]);
		if (bShortenRoot && P.Z < RootHinge)
		{
			P.Z = FMath::Max(Original.Min.Z, Original.Min.Z + (P.Z - RootBottom) * RootScale);
			N.Z /= RootScale;
			N.Normalize();
		}
		Positions.Emplace(P);
		Normals.Emplace(N);
	}
	auto AddLevel = [&](const TArray<ACELegionsSky::FTreeSection>& Sections)
	{
		TreeMeshes.Add(new FMeshDescription());
		BuildLegionsTreeLevel(TreeMeshes.Last(), Sections, Positions, Normals, Tree.UVs);
	};
	AddLevel(Tree.Sections);
	for (const auto& Level : Tree.DistanceLevels) AddLevel(Level);
	return true;
}
