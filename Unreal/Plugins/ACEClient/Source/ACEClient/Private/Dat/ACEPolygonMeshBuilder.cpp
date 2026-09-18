#include "Dat/ACEPolygonMeshBuilder.h"
#include "ACETypes.h"

void ACEPolygonMeshBuilder::Append(const TMap<uint16, FACEDatPolygon>& Polygons,
    const TMap<uint16, FACEDatSWVertex>& Vertices, const TArray<uint32>& Surfaces,
    FACEDatTextureResolver* Textures, int32 PartIndex, const FACEObjDesc* Appearance,
    float WorldScale, TArray<FACEBuiltMeshSection>& OutSections)
{
    TMap<uint32, int32> SurfaceSections;
    // ConstructMesh shares CSurface instances across polygon subsets in retail.
    // ObjDesc surfaces are appearance-dependent and intentionally bypass the global
    // resolver cache. Keep their pixels here for this part, rather than decoding
    // the same texture/palette for every polygon and every side.
    TMap<uint32, FACEDatDecodedSurface> PartSurfaces;
    TSet<uint32> FailedSurfaces;
    for (const auto& Pair : Polygons)
    {
        const FACEDatPolygon& Poly = Pair.Value;
        const int32 Count = Poly.VertexIds.Num();
        if (Count < 3) continue;
        bool bValid = true;
        for (int16 Id : Poly.VertexIds) bValid &= Vertices.Contains(static_cast<uint16>(Id));
        if (!bValid) continue;

        const bool bSharedBack = Poly.SidesType == EACECullMode::None;
        const bool bSeparateBack = Poly.SidesType == EACECullMode::Clockwise;
        const int32 Sides = bSharedBack || bSeparateBack ? 2 : 1;
        for (int32 Side = 0; Side < Sides; ++Side)
        {
            const bool bBack = Side != 0;
            const int32 SurfaceIndex = bBack && bSeparateBack ? Poly.NegSurface : Poly.PosSurface;
            if (!Surfaces.IsValidIndex(SurfaceIndex)) continue;
            const uint32 SurfaceId = Surfaces[SurfaceIndex];
            const TArray<uint8>& UVIndices = bBack && bSeparateBack ? Poly.NegUVIndices : Poly.PosUVIndices;
            const FACEDatDecodedSurface* Decoded = PartSurfaces.Find(SurfaceId);
            if (!Decoded && Textures && !FailedSurfaces.Contains(SurfaceId))
            {
                FACEDatDecodedSurface Surface;
                if (Textures->ResolveSurfaceWithAppearance(SurfaceId, PartIndex, Appearance, Surface))
                    Decoded = &PartSurfaces.Add(SurfaceId, MoveTemp(Surface));
                else FailedSurfaces.Add(SurfaceId);
            }
            if (Decoded && Decoded->bFullyTransparent) continue;

            int32* SectionIndex = SurfaceSections.Find(SurfaceId);
            if (!SectionIndex)
            {
                FACEBuiltMeshSection NewSection;
                NewSection.SurfaceId = SurfaceId;
                NewSection.bClipMap = Decoded && Decoded->bClipMap;
                SurfaceSections.Add(SurfaceId, OutSections.Add(MoveTemp(NewSection)));
                SectionIndex = SurfaceSections.Find(SurfaceId);
            }
            FACEBuiltMeshSection& Section = OutSections[*SectionIndex];
            const int32 Base = Section.Vertices.Num();
            for (int32 I = 0; I < Count; ++I)
            {
                const FACEDatSWVertex& Vertex = Vertices[static_cast<uint16>(Poly.VertexIds[I])];
                // NoPos / NoNeg suppress packed UV index arrays, not geometry.
                // D3DPolyRender uses UV slot zero when an array is absent.
                const int32 UVIndex = UVIndices.IsValidIndex(I) ? UVIndices[I] : 0;
                const FVector2D UV = Vertex.UVs.IsValidIndex(UVIndex) ? FVector2D(Vertex.UVs[UVIndex]) : FVector2D::ZeroVector;
                FLinearColor Color(0.75f, 0.75f, 0.8f, 1.f);
                if (Decoded)
                {
                    if (Decoded->bHasPixels) Color = FLinearColor(Decoded->SampleUV(UV.X, UV.Y));
                    else if (Decoded->bIsSolid) Color = Decoded->SolidColor;
                }
                Section.Vertices.Add(FACEPosition::AceVectorToUnreal(FVector(Vertex.Origin), WorldScale));
                Section.Normals.Add(FACEPosition::AceVectorToUnreal(FVector(Vertex.Normal), bBack ? -1.f : 1.f).GetSafeNormal());
                Section.UVs.Add(UV);
                Section.VertexColors.Add(Color);
            }
            for (int32 I = 1; I + 1 < Count; ++I)
            {
                Section.Triangles.Add(Base);
                Section.Triangles.Add(Base + (bBack ? I + 1 : I));
                Section.Triangles.Add(Base + (bBack ? I : I + 1));
            }
        }
    }
}
