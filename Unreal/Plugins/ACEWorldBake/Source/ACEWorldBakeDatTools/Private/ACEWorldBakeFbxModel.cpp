#include "ACEWorldBakeFbxModel.h"
#include "ACEWorldBakeDatTools.h"

FACEWorldBakeFbxModel::FACEWorldBakeFbxModel(const TArray<FACEWorldBakePortalModelTriangleFan>& InFans, const TArray<FACEWorldBakePortalModelVertex>& InVertices, int32 InSurfaceIndex, bool bInFilterSurfaceFans, bool bInCollisionOnly, bool bInCollisionHasTexture, const FString& SurfaceName)
: FACEWorldBakeFbxSection(TEXT("Model"),  FString::Printf(TEXT("Model::%s\", \"Mesh"), *SurfaceName), 1)
, Fans(InFans)
, Vertices(InVertices)
, SurfaceIndex(InSurfaceIndex)
, bFilterSurfaceFans(bInFilterSurfaceFans)
, bCollisionOnly(bInCollisionOnly)
, bCollisionHasTexture(bInCollisionHasTexture)
{
}

int32 TriangulateAndAppendIndices(const FACEWorldBakePortalModelTriangleFan& ModelFan, TArray<int32>& TriangulatedIndices, TArray<uint8>& TriangulatedUVIndices)
{
	const bool bIsFans = (ModelFan.SurfaceIndex != (uint16)INDEX_NONE) || (0 == (ModelFan.VertexIndices.Num() % 3));

	int32 IndicesAdded = 0;
	for (int32 Indice = 0; Indice < ModelFan.VertexIndices.Num(); ++Indice)
	{
		const int32 VertexIndex = ModelFan.VertexIndices[Indice];
		const int32 VertexUVIndex = ModelFan.TexCoordIndices.IsValidIndex(Indice) ? ModelFan.TexCoordIndices[Indice] : INDEX_NONE;

		if (Indice < 2)
		{
			TriangulatedIndices.Add(VertexIndex);
			if (0 <= VertexUVIndex)
			{
				TriangulatedUVIndices.Add(VertexUVIndex);
			}
			IndicesAdded++;
		}
		else if (2 == Indice)
		{
			TriangulatedIndices.Add(VertexIndex ^ -1);
			if (0 <= VertexUVIndex)
			{
				TriangulatedUVIndices.Add(VertexUVIndex);
			}
			IndicesAdded++;
		}
		else
		{
			check(!bIsFans || TriangulatedIndices[TriangulatedIndices.Num() - 1] < 0);
			const int32 VertexIndexA = ModelFan.VertexIndices[bIsFans ? 0 : (Indice - 2)];
			const int32 VertexIndexB = ModelFan.VertexIndices[Indice - 1];

			if (0 <= VertexUVIndex)
			{
				check(0 < ModelFan.TexCoordIndices.Num());
				check((Indice - 1) < ModelFan.TexCoordIndices.Num());
				TriangulatedUVIndices.Add(ModelFan.TexCoordIndices[0]);
				TriangulatedUVIndices.Add(ModelFan.TexCoordIndices[Indice - 1]);
				TriangulatedUVIndices.Add(VertexUVIndex);
			}

			TriangulatedIndices.Add(VertexIndexA);
			TriangulatedIndices.Add(VertexIndexB);
			TriangulatedIndices.Add(VertexIndex ^ -1);
			IndicesAdded += 3;
		}
	}

	return IndicesAdded;
}

void FACEWorldBakeFbxModel::AppendBody(FString& OutContent) const
{
	// accumulate triangulated indices for all render fans of this surface
	TArray<int32> IndexInts;
	TArray<uint8> UVIndexInts;
	if (0 < Fans.Num())
	{
		TArray<FACEWorldBakePortalModelTriangleFan> SortedModelFans = Fans;
		SortedModelFans.Sort();

		IndexInts.Reserve(Fans.Num() * 4); // average is 3 indices per fan; some fans include 4-5 index fans

		for (const FACEWorldBakePortalModelTriangleFan& ModelFan : SortedModelFans)
		{
			// ignore other surfaces and portal fans
			const bool bFilteredSurface = bFilterSurfaceFans && (ModelFan.SurfaceIndex != SurfaceIndex);
			const bool bIgnoredCollision = !bCollisionOnly && (EACEWorldBakePortalModelStippling::kNoPosUVs == ModelFan.Stippling);
			if (bFilteredSurface || bIgnoredCollision)
			{
				continue;
			}

			int32 IndicesAdded = TriangulateAndAppendIndices(ModelFan, IndexInts, UVIndexInts);
		}
	}

	TArray<float> VertexFloats;
	TArray<float> VertexNormalFloats;
	TArray<float> VertexUVFloats;
	TArray<uint16> VertexUVIndexInts;
	int32 NextVertexIndex = 0;
	TArray<int32> LinearIndexInts;

	// isolate the verts for this surface (fbx surfaces only contain referenced verts), which makes them linear by nature
	{
		for (int32 Iter = 0; Iter < IndexInts.Num(); ++Iter)
		{
			check(bCollisionOnly || UVIndexInts.IsValidIndex(Iter)); // must be synchronized

			int32 VertexIndex = IndexInts[Iter];
			if (VertexIndex < 0)
			{
				VertexIndex ^= -1;
			}

			const FACEWorldBakePortalModelVertex& ModelVertex = Vertices[VertexIndex];
			const FVector3f RotatedPosition = ModelVertex.Position.RotateAngleAxis(-90.0f, FVector3f::ForwardVector) * 100.0f;
			const FVector3f RotatedNormal = ModelVertex.Normal.RotateAngleAxis(-90.0f, FVector3f::ForwardVector);

			VertexFloats.Add(RotatedPosition.X);
			VertexFloats.Add(RotatedPosition.Y);
			VertexFloats.Add(RotatedPosition.Z);

			VertexNormalFloats.Add(RotatedNormal.X);
			VertexNormalFloats.Add(RotatedNormal.Y);
			VertexNormalFloats.Add(RotatedNormal.Z);

			if (!bCollisionOnly && (UVIndexInts[Iter] < ModelVertex.UVs.Num()))
			{
				VertexUVFloats.Add(ModelVertex.UVs[UVIndexInts[Iter]].X);
				VertexUVFloats.Add(-ModelVertex.UVs[UVIndexInts[Iter]].Y);
				VertexUVIndexInts.Add(NextVertexIndex);
			}

			const int32 LinearVertexIndex = (0 == (NextVertexIndex + 1) % 3) ? (NextVertexIndex ^ -1) : NextVertexIndex;
			LinearIndexInts.Push(LinearVertexIndex);

			NextVertexIndex++;
		}
	}

	// portal-only geometry, generate a unit triangle to create a valid fbx file
	// todo: detect these during export/import pipelines and omit them
	if (0 == LinearIndexInts.Num())
	{
		VertexFloats.Empty();
		VertexFloats.Append({0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f});
		LinearIndexInts.Append({0, 1, -3});
	}

	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Version"), 232, NextDepth)));

	FString VerticesStr;
	ArrayToString(VertexFloats, VerticesStr);
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Vertices"), VerticesStr, NextDepth)));

	FString IndicesStr;
	ArrayToString(LinearIndexInts, IndicesStr);
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("PolygonVertexIndex"), IndicesStr, NextDepth)));

	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Edges"), TEXT(""), NextDepth)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("GeometryVersion"), TEXT("124"), NextDepth)));

	FString VertexNormals;
	ArrayToString(VertexNormalFloats, VertexNormals);

	FACEWorldBakeFbxSection LayerElementNormalSection(TEXT("LayerElementNormal"), 0, NextDepth);
	OutContent.Append(FString(LayerElementNormalSection.GetContent()));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Version"), 101, NextDepth + 1)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Name"), TEXT("\"\""), NextDepth + 1)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("MappingInformationType"), TEXT("\"ByVertice\""),NextDepth + 1)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("ReferenceInformationType"), TEXT("\"Direct\""), NextDepth + 1)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Normals"), VertexNormals, NextDepth + 1)));
	LayerElementNormalSection.AppendFooter(OutContent);

	if (!bCollisionOnly)
	{
		FString VertexUVs;
		ArrayToString(VertexUVFloats, VertexUVs);

		FString VertexUVIndices;
		ArrayToString(VertexUVIndexInts, VertexUVIndices);

		FACEWorldBakeFbxSection LayerElementUVSection(TEXT("LayerElementUV"), 0, NextDepth);
		OutContent.Append(FString(LayerElementUVSection.GetContent()));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Version"), 101, NextDepth + 1)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Name"), TEXT("\"UVChannel_0\""), NextDepth + 1)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("MappingInformationType"), TEXT("\"ByVertice\""), NextDepth + 1)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("ReferenceInformationType"), TEXT("\"IndexToDirect\""), NextDepth + 1)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("UV"), VertexUVs, NextDepth + 1)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("UVIndex"), VertexUVIndices, NextDepth + 1)));
		LayerElementUVSection.AppendFooter(OutContent);
	}

	FACEWorldBakeFbxSection LayerSection(TEXT("Layer"), 0, NextDepth);
	OutContent.Append(FString(LayerSection.GetContent()));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Version"), 100, NextDepth + 1)));

	FACEWorldBakeFbxSection LayerElementSectionNormal(TEXT("LayerElement"), NextDepth + 1);
	OutContent.Append(FString(LayerElementSectionNormal.GetContent()));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Type"), TEXT("\"LayerElementNormal\""), NextDepth + 2)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("TypedIndex"), 0, NextDepth + 2)));
	LayerElementSectionNormal.AppendFooter(OutContent);

	if (!bCollisionOnly || bCollisionHasTexture)
	{
		FACEWorldBakeFbxSection LayerElementSectionTexture(TEXT("LayerElement"), NextDepth + 1);
		OutContent.Append(FString(LayerElementSectionTexture.GetContent()));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Type"), TEXT("\"LayerElementTexture\""), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("TypedIndex"), 0, NextDepth + 2)));
		LayerElementSectionTexture.AppendFooter(OutContent);
	}

	if (!bCollisionOnly)
	{
		FACEWorldBakeFbxSection LayerElementSectionUV(TEXT("LayerElement"), NextDepth + 1);
		OutContent.Append(FString(LayerElementSectionUV.GetContent()));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Type"), TEXT("\"LayerElementUV\""), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("TypedIndex"), 0, NextDepth + 2)));
		LayerElementSectionUV.AppendFooter(OutContent);
	}

	LayerSection.AppendFooter(OutContent);
}
