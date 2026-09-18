#pragma once

#include "CoreMinimal.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACEDatTextureResolver.h"

class UTexture2D;

/**
 * Retail portal DAT bitmap fonts (0x40…).
 * Glyphs are A8 coverage atlases tinted at draw time (optional BG outline pass).
 */
class ACECLIENT_API FACEDatFontRenderer
{
public:
	explicit FACEDatFontRenderer(FACEDatTextureResolver* InTextures)
		: Textures(InTextures)
	{
	}

	bool LoadFont(uint32 FontId, FACEDatFont& OutFont);
	int32 MeasureWidth(const FACEDatFont& Font, const FString& Text) const;
	/** Shared glyph atlas: A8 coverage becomes straight-alpha white for Slate tinting.
	 * Colored retail atlases retain their authored pixels. Caller keeps the texture alive. */
	UTexture2D* CreateGlyphAtlas(uint32 TextureId, bool& bOutTint);
	/**
	 * Renders Text into a new transient UI texture (caller owns via returned UTexture2D*).
	 * bOutline draws BackgroundSurface first in OutlineColor when Font has a BG DID.
	 * If BoxW/BoxH > glyph bounds, pads the texture and centers the line in the box
	 * so overlays can share the same rect as button chrome under canvas scale.
	 */
	UTexture2D* RenderText(uint32 FontId, const FString& Text, FColor Color,
		bool bOutline = false, FColor OutlineColor = FColor(0, 0, 0, 255),
		int32* OutWidth = nullptr, int32* OutHeight = nullptr,
		int32 BoxW = 0, int32 BoxH = 0);

private:
	FACEDatTextureResolver* Textures = nullptr;
	TMap<uint32, FACEDatFont> FontCache;
	TMap<uint32, FACEDatDecodedSurface> AtlasCache;

	const FACEDatDecodedSurface* GetAtlas(uint32 TextureId);
	void BlitGlyph(TArray<FColor>& Dest, int32 DestW, int32 DestH,
		const FACEDatDecodedSurface& Atlas, const FACEDatFontChar& Ch,
		int32 DestX, int32 DestY, FColor Tint) const;
};
