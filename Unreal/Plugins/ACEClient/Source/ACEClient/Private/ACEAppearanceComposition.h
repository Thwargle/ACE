#pragma once

#include "ACETypes.h"

// Retail ObjDesc::UnPack and ClothingTable::BuildObjDesc use the same insertion
// rules. Keep the server's layer order; equipment slots are not a draw order.
namespace ACEAppearanceComposition
{
inline void AddSubPalette(FACEObjDesc& Out, const FACEObjDescSubPalette& Change)
{
    // Subpalette::supercedes (005AD870): a complete replacement blocks later
    // partial dyes. Another complete replacement can still replace it.
    const bool bFullPalette = Change.Offset == 0 && Change.NumColors == 2048;
    if (!bFullPalette && Out.SubPalettes.ContainsByPredicate([](const auto& Existing)
        { return Existing.Offset == 0 && Existing.NumColors == 2048; })) return;

    // ObjDesc::AddSubpalette (005AB970), Subpalette::replaces (005AD830).
    // Different overlapping ranges remain ordered; only identical ranges or
    // a full-palette override remove earlier changes.
    Out.SubPalettes.RemoveAll([&](const auto& Existing)
        { return bFullPalette || (Existing.Offset == Change.Offset && Existing.NumColors == Change.NumColors); });
    if (Out.SubPalettes.Num() < 255) Out.SubPalettes.Add(Change);
}

inline void AddTextureChange(FACEObjDesc& Out, const FACEObjDescTextureChange& Change)
{
    // TextureMapChange::replaces (005AD980) keys on part + ORIGINAL texture.
    Out.TextureChanges.RemoveAll([&](const auto& Existing)
        { return Existing.PartIndex == Change.PartIndex && Existing.OldTexture == Change.OldTexture; });
    if (Out.TextureChanges.Num() < 255) Out.TextureChanges.Add(Change);
}

inline void AddAnimPartChange(FACEObjDesc& Out, const FACEObjDescAnimPartChange& Change)
{
    // ObjDesc::RemoveDuplicateAnimPartChange (005AB720): last part wins.
    Out.AnimPartChanges.RemoveAll([&](const auto& Existing) { return Existing.PartIndex == Change.PartIndex; });
    if (Out.AnimPartChanges.Num() < 255) Out.AnimPartChanges.Add(Change);
}
}
