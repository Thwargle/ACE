#pragma once

#include "Dat/ACESetupMeshBuilder.h"

// Retail CPolygon / D3DPolyRender::ConstructMesh drawing semantics, shared by
// environment cells and GfxObj parts. Materials must cull back faces: each
// authored side has its own winding, normal, UVs and surface.
namespace ACEPolygonMeshBuilder
{
    void Append(const TMap<uint16, FACEDatPolygon>& Polygons,
        const TMap<uint16, FACEDatSWVertex>& Vertices, const TArray<uint32>& Surfaces,
        FACEDatTextureResolver* Textures, int32 PartIndex, const FACEObjDesc* Appearance,
        float WorldScale, TArray<FACEBuiltMeshSection>& OutSections);
}
