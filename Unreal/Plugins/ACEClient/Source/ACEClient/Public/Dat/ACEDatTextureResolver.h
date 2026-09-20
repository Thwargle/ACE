#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatFileTypes.h"
#include "ACETypes.h"

/** Decoded RGBA surface ready for UVs / UTexture2D. */
struct FACEDatDecodedSurface
{
	uint32 SurfaceId = 0;
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor> Pixels;
	FLinearColor SolidColor = FLinearColor::White;
	/** True for flat ColorValue surfaces (no texture). */
	bool bIsSolid = false;
	bool bHasPixels = false;
	/** ClipMap / Alpha / translucent — needs a masked/translucent material. */
	bool bUsesAlpha = false;
	/** SurfaceType.Additive — portal/spell FX (BLEND_Additive). */
	bool bAdditive = false;
	/** True only for Base1ClipMap surfaces (foliage / keyed textures — not door portals). */
	bool bClipMap = false;
	/** Retail alpha-test reference for keyed textures (indexed 100, DDS 200). */
	float AlphaTestReference = 0.f;
	/**
	 * Solid ColorValue portal fills use Translucency≈1 so retail draws them invisible
	 * (door/window planes). Skip mesh sections entirely — do not paint vertex-color slabs.
	 */
	bool bFullyTransparent = false;
	/** Surface.Luminosity — high values mark glow FX (portal ground discs / sparks). */
	float Luminosity = 0.f;
	float Diffuse = 1.f;
	/** Surface.Translucency (0..1) — Virindi / soft glass; alpha is premultiplied by (1−T). */
	float Translucency = 0.f;
	/** SurfaceType.Translucent bit — retail soft-blend body (Claude), not shop-sign UsesAlpha. */
	bool bSurfaceTranslucent = false;

	FColor SampleUV(float U, float V) const
	{
		if (!bHasPixels || Width <= 0 || Height <= 0 || Pixels.Num() == 0)
		{
			return SolidColor.ToFColor(true);
		}
		U = U - FMath::FloorToFloat(U);
		V = V - FMath::FloorToFloat(V);
		if (U < 0.f)
		{
			U += 1.f;
		}
		if (V < 0.f)
		{
			V += 1.f;
		}
		const int32 X = FMath::Clamp(static_cast<int32>(U * Width), 0, Width - 1);
		const int32 Y = FMath::Clamp(static_cast<int32>(V * Height), 0, Height - 1);
		return Pixels[Y * Width + X];
	}
};

/**
 * Resolves Surface → Texture with optional ObjDesc texture swaps + dyed palette.
 */
class ACECLIENT_API FACEDatTextureResolver
{
	friend class FACETextureBudgetTest;
public:
	FACEDatTextureResolver(FACEDatDatabase* InPortal, FACEDatDatabase* InHighRes)
		: Portal(InPortal), HighRes(InHighRes)
	{
	}

	bool ResolveSurface(uint32 SurfaceId, FACEDatDecodedSurface& Out);
	/** Per-resolver diagnostic; worker resolvers own their counters. */
	uint64 GetSurfaceResolveCount() const { return SurfaceResolveCount; }
	bool ResolveSurfaceWithAppearance(uint32 SurfaceId, int32 PartIndex, const FACEObjDesc* Appearance, FACEDatDecodedSurface& Out);
	UTexture2D* GetOrCreateUTexture(uint32 SurfaceId, UObject* Outer);
	/** Outer used to Rename runtime textures so GC can collect them without AddToRoot. */
	void SetGcOwner(UObject* Owner) { GcOwner = Owner; }
	UObject* GetGcOwner() const { return GcOwner; }
	void AdoptRuntimeTexture(UTexture2D* Tex) { BindRuntimeTexture(Tex); }
	/** Drop CPU pixel arrays after the matching UTexture exists (flags stay cached). */
	void DiscardSurfacePixels(uint32 SurfaceId);
	void TouchTexture(uint32 SurfaceId);
	void TouchSkyTexture(uint64 Key);
	void TouchCompositeTexture(uint64 Key);
	int64 EstimateResidentTextureBytes() const;
	/**
	 * Sky dome / cloud / star textures: single mip, bilinear, always Wrap.
	 * Retail starfield (0x010015EF) UVs span ~0.4–4.6 — Clamp collapses that to a tiny patch.
	 * No mips: Wrap+mips fringe white/black lines on large on-screen sky faces.
	 * bScrolling is retained for call-site clarity; address mode no longer depends on it.
	 */
	UTexture2D* GetOrCreateSkyUTexture(uint32 SurfaceId, UObject* Outer, bool bScrolling);
	/** Direct 0x06… texture DID → UTexture2D (retail UI art referenced by LayoutDescs). */
	UTexture2D* GetOrCreateUiTexture(uint32 TextureId);
	/** Inventory / spell / paperdoll icons: single mip, Clamp, no RGB dilate (retail cutouts). */
	UTexture2D* GetOrCreateUiIconTexture(uint32 TextureId);
	/**
	 * Color texture + separate alphaFile mask (LayoutDesc drawMode 3 buttons).
	 * Alpha channel is taken from the mask's R (or A if present).
	 */
	UTexture2D* GetOrCreateUiTextureWithAlpha(uint32 ColorTextureId, uint32 AlphaTextureId);
	void ReleaseTextureObjects();
	/** Drop world/UI runtime UTextures (not sky) so DXT transients are rebuilt as RGBA. */
	void DiscardNonSkyRuntimeTextures();
	/** Drop cached Surface decode results (e.g. after FullyTransparent rules change). */
	void InvalidateSurfaceCache() { SurfaceCache.Reset(); }
	/**
	 * LRU-evict world/UI UTextures and decoded SurfaceCache entries. Recently used
	 * entries stay. SkyTextureObjects are never evicted (always on camera).
	 * MaxBytes is a GPU-size estimate after CPU mips are released.
	 */
	void TrimCaches(int32 MaxTextures, int32 MaxDecodedSurfaces, int64 MaxBytes = 384ll << 20);

	bool BuildWorkingPalette(const FACEObjDesc& Appearance, TArray<uint32>& OutColors) const;
	bool BuildWorkingPalette(const FACEObjDesc& Appearance, uint32 FallbackPaletteId, TArray<uint32>& OutColors, bool bApplyBiologyRanges = true) const;

	/** Decode a SurfaceTexture (0x05…) / embedded Texture for land TexMerge. */
	bool DecodeSurfaceTexture(uint32 SurfaceTextureId, FACEDatDecodedSurface& Out);

	/**
	 * Runtime UTexture2D with a full mip chain (CreateTransient alone only writes mip 0,
	 * which causes distant aliasing / shimmer on landblock and setup meshes).
	 * MaxMipLevels=0 keeps the full chain; particle callers can match retail's cap/filter.
	 */
	static void BuildOpaqueMip(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<FColor>& Out);
	static UTexture2D* CreateTransientRgbaWithMips(
		int32 Width, int32 Height, const TArray<FColor>& Pixels, bool bUsesAlpha,
		TextureAddress AddressX = TA_Wrap, TextureAddress AddressY = TA_Wrap,
		bool bPremultiplyAlpha = false, bool bDilateRgbIntoTransparent = true,
        int32 MaxMipLevels = 0, TextureFilter Filter = TF_Trilinear, bool bApplyWorldSizeLimit = true);
	/** Single-mip UI texture (bitmap fonts) — no dilate / mip soften. */
	static UTexture2D* CreateTransientRgbaUi(int32 Width, int32 Height, const TArray<FColor>& Pixels);
	/** Single-mip sky face texture (no mip fringe at cube edges). */
	static UTexture2D* CreateTransientRgbaSky(
		int32 Width, int32 Height, const TArray<FColor>& Pixels, bool bUsesAlpha,
		TextureAddress AddressX, TextureAddress AddressY);

	FACEDatDatabase* GetPortalDat() const { return Portal; }
	bool LoadTextureForUi(uint32 TextureId, FACEDatTexture& Out) const { return LoadTexture(TextureId, Out); }
	bool DecodeTextureForUi(const FACEDatTexture& Tex, FACEDatDecodedSurface& Out)
	{
		return DecodeTexture(Tex, nullptr, Out, false);
	}

private:
	uint64 SurfaceResolveCount = 0;
	bool ReadBlob(uint32 Id, TArray<uint8>& Out) const;
	/**
	 * ACViewer TextureCache.LoadTexture: read portal first; if SourceData is empty,
	 * reload the same id from client_highres.dat (high-res pixel payloads live there).
	 */
	bool LoadTexture(uint32 TextureId, FACEDatTexture& Out) const;
	bool DecodeTexture(const FACEDatTexture& Tex, const TArray<uint32>* PaletteOverride, FACEDatDecodedSurface& Out, bool bClipMap = false);
	static FLinearColor ArgbToLinear(uint32 Argb);
	/**
	 * RGB fallback for DXT/non-indexed ClipMaps: foliage green / banner cyan only.
	 * Door openings use solid ColorValue+Translucency (bFullyTransparent), not color keys.
	 */
	static void ApplyClipMapKeying(TArray<FColor>& Pixels);
	void BindRuntimeTexture(UTexture2D* Tex);
	static int32 EstimateTextureGpuBytes(const UTexture2D* Tex);
	static bool IsRuntimeDxtTexture(const UTexture2D* Tex);

	FACEDatDatabase* Portal = nullptr;
	FACEDatDatabase* HighRes = nullptr;
	UObject* GcOwner = nullptr;
	TMap<uint32, FACEDatDecodedSurface> SurfaceCache;
	TMap<uint32, TObjectPtr<UTexture2D>> TextureObjects;
	TMap<uint32, TObjectPtr<UTexture2D>> IconTextureObjects;
	TMap<uint32, double> IconLastUsed;
	/** Cache key: (SurfaceId << 1) | (bScrolling ? 1 : 0). */
	TMap<uint64, TObjectPtr<UTexture2D>> SkyTextureObjects;
	TMap<uint64, TObjectPtr<UTexture2D>> CompositeUiTextures;
	TMap<uint32, double> TextureLastUsed;
	TMap<uint64, double> SkyLastUsed;
	TMap<uint64, double> CompositeLastUsed;
	// These do not keep textures alive: scene/material owners determine residency.
	TMap<uint32, TWeakObjectPtr<UTexture2D>> RetainedWorldTextures;
	TMap<uint32, TWeakObjectPtr<UTexture2D>> RetainedIconTextures;
	TMap<uint64, TWeakObjectPtr<UTexture2D>> RetainedCompositeTextures;
};
