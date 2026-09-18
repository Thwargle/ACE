#include "Dat/ACEDatFontRenderer.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Dat/ACEDatCursor.h"
#include "Dat/ACEDatTextLayout.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

bool FACEDatFontRenderer::LoadFont(uint32 FontId, FACEDatFont& OutFont)
{
	OutFont = FACEDatFont();
	if (!Textures || FontId == 0)
	{
		return false;
	}
	if (const FACEDatFont* Cached = FontCache.Find(FontId))
	{
		OutFont = *Cached;
		return true;
	}

	FACEDatDatabase* Portal = Textures->GetPortalDat();
	if (!Portal)
	{
		return false;
	}
	TArray<uint8> Blob;
	if (!Portal->ReadFile(FontId, Blob))
	{
		return false;
	}
	FACEDatCursor Cur(Blob);
	FACEDatFont Font;
	if (!ACEDatUnpack::UnpackFont(Cur, Font))
	{
		return false;
	}
	FontCache.Add(FontId, Font);
	OutFont = Font;
	return true;
}

int32 FACEDatFontRenderer::MeasureWidth(const FACEDatFont& Font, const FString& Text) const
{
	int32 Width = 0;
	for (const FACEBitmapTextLine& Line : ACEDatText::Layout(Font, Text, MAX_int32, false))
		Width = FMath::Max(Width, Line.Width);
	return Width;
}

UTexture2D* FACEDatFontRenderer::CreateGlyphAtlas(uint32 TextureId, bool& bOutTint)
{
	FACEDatTexture Raw;
	FACEDatDecodedSurface Decoded;
	bOutTint = true;
	if (!Textures || !Textures->LoadTextureForUi(TextureId, Raw)
		|| !Textures->DecodeTextureForUi(Raw, Decoded) || !Decoded.bHasPixels) return nullptr;
	const bool bAlphaOnly = Raw.Format == EACESurfacePixelFormat::A8;
	// TextureBasedFont::RenderText supplies diffuse color for every glyph,
	// including RGBA atlases. Preserve their RGB and modulate it at draw time.
	bOutTint = true;
	if (bAlphaOnly)
		for (FColor& Pixel : Decoded.Pixels) Pixel = FColor(255, 255, 255, Pixel.R);
	UTexture2D* Texture = FACEDatTextureResolver::CreateTransientRgbaUi(Decoded.Width, Decoded.Height, Decoded.Pixels);
	// Native pixel centers still reproduce the atlas exactly. Linear filtering also
	// preserves coverage when the viewport/OS applies a fractional DPI scale.
	if (Texture) Texture->Filter = TF_Bilinear;
	return Texture;
}

const FACEDatDecodedSurface* FACEDatFontRenderer::GetAtlas(uint32 TextureId)
{
	if (!Textures || TextureId == 0)
	{
		return nullptr;
	}
	if (FACEDatDecodedSurface* Found = AtlasCache.Find(TextureId))
	{
		return Found;
	}
	FACEDatTexture Raw;
	FACEDatDecodedSurface Decoded;
	if (!Textures->LoadTextureForUi(TextureId, Raw)
		|| !Textures->DecodeTextureForUi(Raw, Decoded)
		|| !Decoded.bHasPixels)
	{
		return nullptr;
	}
	if (Raw.Format == EACESurfacePixelFormat::A8)
		for (FColor& Pixel : Decoded.Pixels) Pixel = FColor(255, 255, 255, Pixel.R);
	return &AtlasCache.Add(TextureId, MoveTemp(Decoded));
}

void FACEDatFontRenderer::BlitGlyph(TArray<FColor>& Dest, int32 DestW, int32 DestH,
	const FACEDatDecodedSurface& Atlas, const FACEDatFontChar& Ch,
	int32 DestX, int32 DestY, FColor Tint) const
{
	if (!Atlas.bHasPixels || Atlas.Width <= 0 || Atlas.Height <= 0 || Ch.Width == 0 || Ch.Height == 0)
	{
		return;
	}
	for (int32 Gy = 0; Gy < Ch.Height; ++Gy)
	{
		const int32 Sy = static_cast<int32>(Ch.OffsetY) + Gy;
		const int32 Dy = DestY + Gy;
		if (Sy < 0 || Sy >= Atlas.Height || Dy < 0 || Dy >= DestH)
		{
			continue;
		}
		for (int32 Gx = 0; Gx < Ch.Width; ++Gx)
		{
			const int32 Sx = static_cast<int32>(Ch.OffsetX) + Gx;
			const int32 Dx = DestX + Gx;
			if (Sx < 0 || Sx >= Atlas.Width || Dx < 0 || Dx >= DestW)
			{
				continue;
			}
			const FColor Src = Atlas.Pixels[Sy * Atlas.Width + Sx];
			// Both atlas formats now use straight alpha, like the live glyph renderer.
			const uint8 Cover = (uint32(Src.A) * Tint.A) / 255;
			if (Cover == 0)
			{
				continue;
			}
			FColor& D = Dest[Dy * DestW + Dx];
			const int32 CoverI = static_cast<int32>(Cover);
			const int32 Inv = 255 - CoverI;
			const uint8 OutA = static_cast<uint8>(FMath::Min(255,
				static_cast<int32>(D.A) + ((255 - D.A) * CoverI) / 255));
			D.R = static_cast<uint8>((D.R * Inv + ((uint32(Src.R) * Tint.R) / 255) * CoverI) / 255);
			D.G = static_cast<uint8>((D.G * Inv + ((uint32(Src.G) * Tint.G) / 255) * CoverI) / 255);
			D.B = static_cast<uint8>((D.B * Inv + ((uint32(Src.B) * Tint.B) / 255) * CoverI) / 255);
			D.A = OutA;
		}
	}
}

UTexture2D* FACEDatFontRenderer::RenderText(uint32 FontId, const FString& Text, FColor Color,
	bool bOutline, FColor OutlineColor, int32* OutWidth, int32* OutHeight, int32 BoxW, int32 BoxH)
{
	FACEDatFont Font;
	if (!LoadFont(FontId, Font) || Text.IsEmpty())
	{
		return nullptr;
	}

	const int32 TextW = MeasureWidth(Font, Text);
	const int32 TextH = FMath::Max(1, static_cast<int32>(Font.MaxCharHeight));
	const int32 Width = FMath::Max(TextW, FMath::Max(1, BoxW));
	const int32 Height = FMath::Max(TextH, FMath::Max(1, BoxH));
	const int32 OriginX = (Width - TextW) / 2;
	const int32 OriginY = (Height - TextH) / 2;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(0, 0, 0, 0), Width * Height);

	auto DrawPass = [&](uint32 AtlasId, FColor Tint)
	{
		const FACEDatDecodedSurface* Atlas = GetAtlas(AtlasId);
		if (!Atlas)
		{
			return;
		}
		int32 PenX = OriginX;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const FACEDatFontChar* Found = ACEDatText::FindChar(Font, static_cast<uint16>(Text[i]));
			if (!Found) continue;
			const FACEDatFontChar& Ch = *Found;
			PenX += static_cast<int8>(Ch.HorizontalOffsetBefore);
			// VerticalOffsetBefore is relative to the top of the MaxCharHeight line box.
			const int32 DestY = OriginY + static_cast<int8>(Ch.VerticalOffsetBefore);
			BlitGlyph(Pixels, Width, Height, *Atlas, Ch, PenX, DestY, Tint);
			PenX += static_cast<int32>(Ch.Width) + static_cast<int8>(Ch.HorizontalOffsetAfter);
		}
	};

	if (bOutline && Font.BackgroundSurfaceDataID != 0)
	{
		DrawPass(Font.BackgroundSurfaceDataID, OutlineColor);
	}
	if (Font.ForegroundSurfaceDataID != 0)
	{
		DrawPass(Font.ForegroundSurfaceDataID, Color);
	}

	// Blits composite premultiplied RGB. Slate expects straight alpha and applies
	// coverage when drawing; storing premultiplied pixels squares glyph coverage.
	for (FColor& Pixel : Pixels)
	{
		if (Pixel.A == 0) continue;
		Pixel.R = FMath::Min(255u, (uint32(Pixel.R) * 255 + Pixel.A / 2) / Pixel.A);
		Pixel.G = FMath::Min(255u, (uint32(Pixel.G) * 255 + Pixel.A / 2) / Pixel.A);
		Pixel.B = FMath::Min(255u, (uint32(Pixel.B) * 255 + Pixel.A / 2) / Pixel.A);
	}
	UTexture2D* Tex = FACEDatTextureResolver::CreateTransientRgbaUi(Width, Height, Pixels);
	if (!Tex)
	{
		return nullptr;
	}
	Textures->AdoptRuntimeTexture(Tex);

	if (OutWidth)
	{
		*OutWidth = Width;
	}
	if (OutHeight)
	{
		*OutHeight = Height;
	}
	return Tex;
}
