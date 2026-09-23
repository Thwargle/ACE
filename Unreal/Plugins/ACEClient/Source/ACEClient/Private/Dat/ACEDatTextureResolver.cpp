#include "Dat/ACEDatTextureResolver.h"
#include "Dat/ACEDatCursor.h"
#include "Dat/ACEDxtUtil.h"
#include "ACEDatSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture.h"
#include "TextureResource.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarRuntimeTextureMaxSize(TEXT("ace.Texture.MaxWorldSize"), PLATFORM_ANDROID ? 512 : 0,
		TEXT("Maximum runtime DAT object texture dimension (0 = authored size). Landscape, UI and sky textures retain their authored resolution; restart to apply."));

	template<typename KeyType>
	UTexture2D* TakeRetainedTexture(KeyType Key, TMap<KeyType, TWeakObjectPtr<UTexture2D>>& Retained)
	{
		TWeakObjectPtr<UTexture2D> Texture;
		return Retained.RemoveAndCopyValue(Key, Texture) ? Texture.Get() : nullptr;
	}
	int32 CountMipLevels(int32 Width, int32 Height)
	{
		int32 Levels = 1;
		while (Width > 1 || Height > 1)
		{
			Width = FMath::Max(1, Width / 2);
			Height = FMath::Max(1, Height / 2);
			++Levels;
		}
		return Levels;
	}

	void BoxFilterMip(const TArray<FColor>& Src, int32 SrcW, int32 SrcH, TArray<FColor>& Dst, int32 DstW, int32 DstH,
		bool bWrapX = false, bool bWrapY = false, bool bAlphaWeighted = false)
	{
		Dst.SetNumUninitialized(DstW * DstH);
		for (int32 Y = 0; Y < DstH; ++Y)
		{
			const int32 Sy0 = (Y * 2);
			// Wrapped textures (sky dome / scrolling clouds) must average the last row/column
			// with the first, or the downsized mips keep an edge discontinuity that shows as a
			// bright seam once trilinear filtering samples across the TA_Wrap boundary.
			const int32 Sy1 = bWrapY ? ((Sy0 + 1) % SrcH) : FMath::Min(Sy0 + 1, SrcH - 1);
			for (int32 X = 0; X < DstW; ++X)
			{
				const int32 Sx0 = (X * 2);
				const int32 Sx1 = bWrapX ? ((Sx0 + 1) % SrcW) : FMath::Min(Sx0 + 1, SrcW - 1);
				const FColor& A = Src[Sy0 * SrcW + Sx0];
				const FColor& B = Src[Sy0 * SrcW + Sx1];
				const FColor& C = Src[Sy1 * SrcW + Sx0];
				const FColor& D = Src[Sy1 * SrcW + Sx1];
				const int32 SumA = static_cast<int32>(A.A) + B.A + C.A + D.A;
				if (bAlphaWeighted && SumA > 0)
				{
					// Weight RGB by alpha so transparent (black) texels don't darken cutout
					// edges in lower mips — that darkening reads as hard/dirty silhouettes
					// on sun/moon/cloud quads once the dome is at distance.
					Dst[Y * DstW + X] = FColor(
						static_cast<uint8>((A.R * A.A + B.R * B.A + C.R * C.A + D.R * D.A) / SumA),
						static_cast<uint8>((A.G * A.A + B.G * B.A + C.G * C.A + D.G * D.A) / SumA),
						static_cast<uint8>((A.B * A.A + B.B * B.A + C.B * C.A + D.B * D.A) / SumA),
						static_cast<uint8>(SumA >> 2));
				}
				else
				{
					Dst[Y * DstW + X] = FColor(
						static_cast<uint8>((static_cast<int32>(A.R) + B.R + C.R + D.R) >> 2),
						static_cast<uint8>((static_cast<int32>(A.G) + B.G + C.G + D.G) >> 2),
						static_cast<uint8>((static_cast<int32>(A.B) + B.B + C.B + D.B) >> 2),
						static_cast<uint8>(SumA >> 2));
				}
			}
		}
	}

	/**
	 * Flood the RGB of opaque texels into fully-transparent neighbors (alpha stays 0).
	 * Bilinear/trilinear filtering blends RGB across the alpha edge regardless of coverage,
	 * so leaving transparent texels black draws a dark fringe around every cutout.
	 */
	void DilateRgbIntoTransparent(TArray<FColor>& Pixels, int32 W, int32 H, int32 Passes)
	{
		TArray<FColor> Scratch;
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			Scratch = Pixels;
			bool bAny = false;
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					FColor& Out = Scratch[Y * W + X];
					if (Out.A != 0)
					{
						continue;
					}
					int32 SumR = 0, SumG = 0, SumB = 0, Count = 0;
					for (int32 Dy = -1; Dy <= 1; ++Dy)
					{
						const int32 Ny = Y + Dy;
						if (Ny < 0 || Ny >= H)
						{
							continue;
						}
						for (int32 Dx = -1; Dx <= 1; ++Dx)
						{
							const int32 Nx = X + Dx;
							if (Nx < 0 || Nx >= W || (Dx == 0 && Dy == 0))
							{
								continue;
							}
							const FColor& N = Pixels[Ny * W + Nx];
							// Consider both opaque texels and previously-dilated ones.
							if (N.A != 0 || N.R + N.G + N.B != 0)
							{
								SumR += N.R; SumG += N.G; SumB += N.B;
								++Count;
							}
						}
					}
					if (Count > 0)
					{
						Out.R = static_cast<uint8>(SumR / Count);
						Out.G = static_cast<uint8>(SumG / Count);
						Out.B = static_cast<uint8>(SumB / Count);
						bAny = true;
					}
				}
			}
			Pixels = MoveTemp(Scratch);
			if (!bAny)
			{
				break;
			}
		}
	}
}

void FACEDatTextureResolver::BuildOpaqueMip(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<FColor>& Out)
{
	BoxFilterMip(Pixels,Width,Height,Out,FMath::Max(1,Width/2),FMath::Max(1,Height/2));
}

UTexture2D* FACEDatTextureResolver::CreateTransientRgbaWithMips(
	int32 Width, int32 Height, const TArray<FColor>& Pixels, bool bUsesAlpha,
	TextureAddress AddressX, TextureAddress AddressY, bool bPremultiplyAlpha,
	bool bDilateRgbIntoTransparent, int32 MaxMipLevels, TextureFilter Filter, bool bApplyWorldSizeLimit)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
	{
		return nullptr;
	}
	const int32 Limit = CVarRuntimeTextureMaxSize.GetValueOnAnyThread();
	if (bApplyWorldSizeLimit && Limit > 0 && FMath::Max(Width, Height) > FMath::Max(32, Limit))
	{
		const int32 NextW = FMath::Max(1, Width / 2), NextH = FMath::Max(1, Height / 2);
		TArray<FColor> Reduced;
		BoxFilterMip(Pixels, Width, Height, Reduced, NextW, NextH, AddressX == TA_Wrap, AddressY == TA_Wrap, bUsesAlpha);
		return CreateTransientRgbaWithMips(NextW, NextH, Reduced, bUsesAlpha, AddressX, AddressY,
			bPremultiplyAlpha, bDilateRgbIntoTransparent, MaxMipLevels, Filter);
	}

	UTexture2D* Tex = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Tex || !Tex->GetPlatformData())
	{
		return nullptr;
	}

	Tex->CompressionSettings = bUsesAlpha ? TC_EditorIcon : TC_Default;
	Tex->SRGB = true;
	Tex->Filter = Filter;
	Tex->AddressX = AddressX;
	Tex->AddressY = AddressY;
	Tex->NeverStream = true;

	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	const int32 FullMipCount = CountMipLevels(Width, Height);
    // ImgTex::InitHardwareTexture caps retail particle sprites at four levels;
    // terrain and other callers retain their full chains by default.
	const int32 MipCount = MaxMipLevels > 0 ? FMath::Min(FullMipCount, MaxMipLevels) : FullMipCount;
	PlatformData->SizeX = Width;
	PlatformData->SizeY = Height;
	PlatformData->PixelFormat = PF_B8G8R8A8;
	PlatformData->Mips.Empty(MipCount);

	TArray<FColor> Current = Pixels;
	// Ensure exact size (callers may pass larger scratch buffers).
	Current.SetNum(Width * Height, EAllowShrinking::No);
	if (bUsesAlpha && bDilateRgbIntoTransparent)
	{
		// Smooth cutout edges: fill transparent texels with neighboring RGB so bilinear
		// blending across the alpha edge doesn't produce dark fringes / jagged silhouettes.
		DilateRgbIntoTransparent(Current, Width, Height, /*Passes*/ 2);
	}
	if (bPremultiplyAlpha)
	{
		// Unlit additive often ignores the Opacity pin and tex.A. RGB×A makes holes
		// black (invisible on additive) even when the shader only samples color.
		for (FColor& P : Current)
		{
			const float A = static_cast<float>(P.A) * (1.f / 255.f);
			P.R = static_cast<uint8>(FMath::RoundToInt(static_cast<float>(P.R) * A));
			P.G = static_cast<uint8>(FMath::RoundToInt(static_cast<float>(P.G) * A));
			P.B = static_cast<uint8>(FMath::RoundToInt(static_cast<float>(P.B) * A));
		}
	}
	int32 CurW = Width;
	int32 CurH = Height;
	for (int32 Level = 0; Level < MipCount; ++Level)
	{
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		Mip->SizeX = CurW;
		Mip->SizeY = CurH;
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Data = Mip->BulkData.Realloc(CurW * CurH * sizeof(FColor));
		FMemory::Memcpy(Data, Current.GetData(), CurW * CurH * sizeof(FColor));
		Mip->BulkData.Unlock();
		PlatformData->Mips.Add(Mip);

		if (Level + 1 >= MipCount)
		{
			break;
		}
		const int32 NextW = FMath::Max(1, CurW / 2);
		const int32 NextH = FMath::Max(1, CurH / 2);
		TArray<FColor> Next;
		BoxFilterMip(Current, CurW, CurH, Next, NextW, NextH,
			AddressX == TA_Wrap, AddressY == TA_Wrap, bUsesAlpha);
		Current = MoveTemp(Next);
		CurW = NextW;
		CurH = NextH;
	}

	Tex->UpdateResource();
	return Tex;
}

int32 FACEDatTextureResolver::EstimateTextureGpuBytes(const UTexture2D* Tex)
{
	if (!Tex)
	{
		return 0;
	}
	const EPixelFormat Fmt = Tex->GetPixelFormat();
	const bool bDxt1 = Fmt == PF_DXT1;
	const bool bDxt5 = Fmt == PF_DXT5 || Fmt == PF_DXT3;
	int32 W = FMath::Max(1, Tex->GetSizeX());
	int32 H = FMath::Max(1, Tex->GetSizeY());
	const int32 MipCount = FMath::Max(1, Tex->GetNumMips());
	int64 Total = 0;
	for (int32 M = 0; M < MipCount; ++M)
	{
		if (bDxt1 || bDxt5)
		{
			Total += static_cast<int64>(((W + 3) / 4) * ((H + 3) / 4) * (bDxt1 ? 8 : 16));
		}
		else
		{
			Total += static_cast<int64>(W) * H * 4;
		}
		W = FMath::Max(1, W / 2);
		H = FMath::Max(1, H / 2);
	}
	return static_cast<int32>(FMath::Clamp(Total, (int64)0, (int64)MAX_int32));
}

bool FACEDatTextureResolver::IsRuntimeDxtTexture(const UTexture2D* Tex)
{
	if (!Tex)
	{
		return false;
	}
	const EPixelFormat Fmt = Tex->GetPixelFormat();
	return Fmt == PF_DXT1 || Fmt == PF_DXT5 || Fmt == PF_DXT3;
}

void FACEDatTextureResolver::BindRuntimeTexture(UTexture2D* Tex)
{
	if (!Tex)
	{
		return;
	}
	if (GcOwner)
	{
		Tex->Rename(nullptr, GcOwner, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
		if (UACEDatSubsystem* Dat = Cast<UACEDatSubsystem>(GcOwner))
		{
			Dat->TrackRuntimeTexture(Tex);
		}
	}
}

void FACEDatTextureResolver::DiscardSurfacePixels(uint32 SurfaceId)
{
	if (FACEDatDecodedSurface* Cached = SurfaceCache.Find(SurfaceId))
	{
		Cached->Pixels.Empty();
		Cached->bHasPixels = false;
	}
}

void FACEDatTextureResolver::TouchTexture(uint32 SurfaceId)
{
	TextureLastUsed.Add(SurfaceId, FPlatformTime::Seconds());
}

void FACEDatTextureResolver::TouchSkyTexture(uint64 Key)
{
	SkyLastUsed.Add(Key, FPlatformTime::Seconds());
}

void FACEDatTextureResolver::TouchCompositeTexture(uint64 Key)
{
	CompositeLastUsed.Add(Key, FPlatformTime::Seconds());
}

int64 FACEDatTextureResolver::EstimateResidentTextureBytes() const
{
	int64 Total = 0;
	for (const auto& Pair : TextureObjects)
	{
		Total += EstimateTextureGpuBytes(Pair.Value.Get());
	}
	for (const auto& Pair : IconTextureObjects)
	{
		Total += EstimateTextureGpuBytes(Pair.Value.Get());
	}
	for (const auto& Pair : SkyTextureObjects)
	{
		Total += EstimateTextureGpuBytes(Pair.Value.Get());
	}
	for (const auto& Pair : CompositeUiTextures)
	{
		Total += EstimateTextureGpuBytes(Pair.Value.Get());
	}
	return Total;
}

UTexture2D* FACEDatTextureResolver::CreateTransientRgbaUi(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
	{
		return nullptr;
	}

	UTexture2D* Tex = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Tex || !Tex->GetPlatformData())
	{
		return nullptr;
	}

	Tex->CompressionSettings = TC_EditorIcon;
	Tex->SRGB = true;
	// Bilinear so canvas ScaleX/ScaleY (and binder PlaceScaled) does not stair-step
	// DAT fonts / composited button labels the way Nearest did after login UI scale work.
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	Tex->NeverStream = true;
#if WITH_EDITORONLY_DATA
	Tex->MipGenSettings = TMGS_NoMipmaps;
#endif
	Tex->LODGroup = TEXTUREGROUP_UI;

	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	PlatformData->SizeX = Width;
	PlatformData->SizeY = Height;
	PlatformData->PixelFormat = PF_B8G8R8A8;
	PlatformData->Mips.Empty(1);

	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	Mip->SizeX = Width;
	Mip->SizeY = Height;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* Data = Mip->BulkData.Realloc(Width * Height * sizeof(FColor));
	FMemory::Memcpy(Data, Pixels.GetData(), Width * Height * sizeof(FColor));
	Mip->BulkData.Unlock();
	PlatformData->Mips.Add(Mip);

	Tex->UpdateResource();
	return Tex;
}

UTexture2D* FACEDatTextureResolver::CreateTransientRgbaSky(
	int32 Width, int32 Height, const TArray<FColor>& Pixels, bool bUsesAlpha,
	TextureAddress AddressX, TextureAddress AddressY)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
	{
		return nullptr;
	}

	UTexture2D* Tex = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Tex || !Tex->GetPlatformData())
	{
		return nullptr;
	}

	// Sky faces are huge on screen — mips average across Wrap edges into dark/bright fringes.
	Tex->CompressionSettings = bUsesAlpha ? TC_EditorIcon : TC_Default;
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = AddressX;
	Tex->AddressY = AddressY;
	Tex->NeverStream = true;
#if WITH_EDITORONLY_DATA
	Tex->MipGenSettings = TMGS_NoMipmaps;
#endif

	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	PlatformData->SizeX = Width;
	PlatformData->SizeY = Height;
	PlatformData->PixelFormat = PF_B8G8R8A8;
	PlatformData->Mips.Empty(1);

	TArray<FColor> Current = Pixels;
	Current.SetNum(Width * Height, EAllowShrinking::No);
	if (bUsesAlpha)
	{
		DilateRgbIntoTransparent(Current, Width, Height, /*Passes*/ 2);
	}
	else
	{
		// Base1Image day-dome faces have no authored alpha. Leave A=0 (DXT1 key /
		// unused channel) and a translucent fallback samples black.
		for (FColor& C : Current)
		{
			C.A = 255;
		}
	}

	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	Mip->SizeX = Width;
	Mip->SizeY = Height;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* Data = Mip->BulkData.Realloc(Width * Height * sizeof(FColor));
	FMemory::Memcpy(Data, Current.GetData(), Width * Height * sizeof(FColor));
	Mip->BulkData.Unlock();
	PlatformData->Mips.Add(Mip);

	Tex->UpdateResource();
	return Tex;
}

FLinearColor FACEDatTextureResolver::ArgbToLinear(uint32 Argb)
{
	const float A = ((Argb >> 24) & 0xFF) / 255.f;
	const float R = ((Argb >> 16) & 0xFF) / 255.f;
	const float G = ((Argb >> 8) & 0xFF) / 255.f;
	const float B = (Argb & 0xFF) / 255.f;
	return FLinearColor(R, G, B, A <= 0.f ? 1.f : A);
}

void FACEDatTextureResolver::ApplyClipMapKeying(TArray<FColor>& Pixels)
{
	// ClipMap RGB keying is for foliage / banner chrome only (green, blue, cyan).
	// Do NOT key magenta or black here — that punched holes in character clothing/armor.
	// Building door/window openings are solid ColorValue + Translucency portal planes
	// (bFullyTransparent), not ClipMap color keys.
	for (FColor& C : Pixels)
	{
		if (C.A < 16)
		{
			C.A = 0;
			C.R = C.G = C.B = 0;
			continue;
		}
		const int32 MaxRB = FMath::Max(static_cast<int32>(C.R), static_cast<int32>(C.B));
		const int32 MaxRG = FMath::Max(static_cast<int32>(C.R), static_cast<int32>(C.G));
		const int32 MaxGB = FMath::Max(static_cast<int32>(C.G), static_cast<int32>(C.B));
		const bool bKeyGreen = C.G >= 140 && C.R <= 110 && C.B <= 110 && (C.G - MaxRB) >= 40;
		const bool bKeyBlue = C.B >= 140 && C.R <= 110 && C.G <= 110 && (C.B - MaxRG) >= 40;
		const bool bKeyCyan = C.G >= 140 && C.B >= 140 && C.R <= 110 && (MaxGB - C.R) >= 40;
		if (bKeyGreen || bKeyBlue || bKeyCyan)
		{
			C.A = 0;
			C.R = C.G = C.B = 0;
		}
	}
}

bool FACEDatTextureResolver::ReadBlob(uint32 Id, TArray<uint8>& Out) const
{
	if (Portal && Portal->ReadFile(Id, Out))
	{
		return true;
	}
	return HighRes && HighRes->ReadFile(Id, Out);
}

bool FACEDatTextureResolver::LoadTexture(uint32 TextureId, FACEDatTexture& Out) const
{
	Out = FACEDatTexture();
	if (TextureId == 0)
	{
		return false;
	}
	auto TryDat = [&](FACEDatDatabase* Dat) -> bool
	{
		if (!Dat)
		{
			return false;
		}
		TArray<uint8> Blob;
		if (!Dat->ReadFile(TextureId, Blob))
		{
			return false;
		}
		FACEDatCursor Cur(Blob);
		FACEDatTexture Tex;
		if (!ACEDatUnpack::UnpackTexture(Cur, Tex))
		{
			return false;
		}
		Out = MoveTemp(Tex);
		return true;
	};

	// Match ACViewer: portal stub often has Length==0; real pixels are in highres.
	if (TryDat(Portal) && Out.SourceData.Num() > 0)
	{
		return true;
	}
	if (TryDat(HighRes) && Out.SourceData.Num() > 0)
	{
		return true;
	}
	return Out.Width > 0 && Out.Height > 0 && Out.SourceData.Num() > 0;
}

bool FACEDatTextureResolver::BuildWorkingPalette(const FACEObjDesc& Appearance, TArray<uint32>& OutColors) const
{
	return BuildWorkingPalette(Appearance, 0, OutColors, true);
}

bool FACEDatTextureResolver::BuildWorkingPalette(const FACEObjDesc& Appearance, uint32 FallbackPaletteId, TArray<uint32>& OutColors, bool bApplyBiologyRanges) const
{
	OutColors.Reset();
	// ACViewer IndexToColor starts from the texture's DefaultPaletteId, then applies dye
	// SubPalettes. Using ObjDesc PaletteBaseId (usually skin) as the base makes helmets/armor
	// inherit skin colors in undyed index ranges.
	uint32 BaseId = FallbackPaletteId;
	if (BaseId == 0)
	{
		BaseId = static_cast<uint32>(Appearance.PaletteBaseId);
	}
	if (BaseId == 0)
	{
		return false;
	}

	TArray<uint8> Blob;
	if (!ReadBlob(BaseId, Blob))
	{
		return false;
	}
	FACEDatCursor Cur(Blob);
	FACEDatPalette Pal;
	if (!ACEDatUnpack::UnpackPalette(Cur, Pal))
	{
		return false;
	}
	OutColors = Pal.Colors;

	// When skipping biology, drop only the creature's first skin/hair/eyes SubPalette DIDs.
	// Clothing dyes often reuse the same offsets with different DIDs — those must still apply.
	TSet<uint32> BiologyPaletteIds;
	if (!bApplyBiologyRanges)
	{
		bool bGotSkin = false, bGotHair = false, bGotEyes = false;
		for (const FACEObjDescSubPalette& Sub : Appearance.SubPalettes)
		{
			const uint32 Did = static_cast<uint32>(Sub.SubPaletteId);
			if (!bGotSkin && Sub.Offset == 0 && Sub.NumColors == 0x18 * 8)
			{
				BiologyPaletteIds.Add(Did);
				bGotSkin = true;
			}
			else if (!bGotHair && Sub.Offset == 0x18 * 8 && Sub.NumColors == 0x8 * 8)
			{
				BiologyPaletteIds.Add(Did);
				bGotHair = true;
			}
			else if (!bGotEyes && Sub.Offset == 0x20 * 8 && Sub.NumColors == 0x8 * 8)
			{
				BiologyPaletteIds.Add(Did);
				bGotEyes = true;
			}
		}
	}

	for (const FACEObjDescSubPalette& Sub : Appearance.SubPalettes)
	{
		if (!bApplyBiologyRanges
			&& BiologyPaletteIds.Contains(static_cast<uint32>(Sub.SubPaletteId)))
		{
			continue;
		}
		TArray<uint8> SubBlob;
		if (!ReadBlob(static_cast<uint32>(Sub.SubPaletteId), SubBlob))
		{
			continue;
		}
		FACEDatCursor SubCur(SubBlob);
		FACEDatPalette SubPal;
		if (!ACEDatUnpack::UnpackPalette(SubCur, SubPal))
		{
			continue;
		}
		// ACViewer: paletteColors[j+offset] = newPalette.Colors[j+offset]
		for (int32 i = 0; i < Sub.NumColors; ++i)
		{
			const int32 Idx = Sub.Offset + i;
			if (!OutColors.IsValidIndex(Idx) || !SubPal.Colors.IsValidIndex(Idx))
			{
				break;
			}
			OutColors[Idx] = SubPal.Colors[Idx];
		}
	}
	return OutColors.Num() > 0;
}

bool FACEDatTextureResolver::DecodeTexture(const FACEDatTexture& Tex, const TArray<uint32>* PaletteOverride, FACEDatDecodedSurface& Out, bool bClipMap)
{
	Out.Width = Tex.Width;
	Out.Height = Tex.Height;
	Out.bIsSolid = false;
	Out.bHasPixels = false;

	// Retail UI backdrops (e.g. char select 0x06007576) are CUSTOM_RAW_JPEG with Width/Height=0;
	// dimensions come from the JPEG payload itself.
	if (Tex.Format == EACESurfacePixelFormat::CUSTOM_RAW_JPEG)
	{
		if (Tex.SourceData.Num() == 0)
		{
			return false;
		}
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
		if (!Wrapper.IsValid()
			|| !Wrapper->SetCompressed(Tex.SourceData.GetData(), Tex.SourceData.Num()))
		{
			UE_LOG(LogTemp, Warning, TEXT("ACEDat: JPEG wrapper failed for UI texture"));
			return false;
		}
		TArray<uint8> RawBGRA;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawBGRA))
		{
			UE_LOG(LogTemp, Warning, TEXT("ACEDat: JPEG GetRaw failed"));
			return false;
		}
		Out.Width = Wrapper->GetWidth();
		Out.Height = Wrapper->GetHeight();
		if (Out.Width <= 0 || Out.Height <= 0 || RawBGRA.Num() < Out.Width * Out.Height * 4)
		{
			return false;
		}
		const int32 PixCount = Out.Width * Out.Height;
		Out.Pixels.SetNumUninitialized(PixCount);
		for (int32 i = 0; i < PixCount; ++i)
		{
			const int32 O = i * 4;
			Out.Pixels[i] = FColor(RawBGRA[O + 2], RawBGRA[O + 1], RawBGRA[O], RawBGRA[O + 3]);
		}
		Out.bHasPixels = true;
		Out.bUsesAlpha = false;
		return true;
	}

	if (Tex.Width <= 0 || Tex.Height <= 0 || Tex.SourceData.Num() == 0)
	{
		return false;
	}

	TArray<uint8> RGBA;

	switch (Tex.Format)
	{
	case EACESurfacePixelFormat::DXT1:
		ACEDxtUtil::DecompressDxt1(Tex.SourceData, Tex.Width, Tex.Height, RGBA);
		break;
	case EACESurfacePixelFormat::DXT3:
		ACEDxtUtil::DecompressDxt3(Tex.SourceData, Tex.Width, Tex.Height, RGBA);
		break;
	case EACESurfacePixelFormat::DXT5:
		ACEDxtUtil::DecompressDxt5(Tex.SourceData, Tex.Width, Tex.Height, RGBA);
		break;
	case EACESurfacePixelFormat::INDEX16:
	case EACESurfacePixelFormat::P8:
	{
		TArray<uint32> LocalColors;
		const TArray<uint32>* Colors = PaletteOverride;
		if (!Colors || Colors->Num() == 0)
		{
			if (!Tex.bHasPalette)
			{
				return false;
			}
			TArray<uint8> PalBlob;
			if (!ReadBlob(Tex.DefaultPaletteId, PalBlob))
			{
				return false;
			}
			FACEDatCursor PalCur(PalBlob);
			FACEDatPalette ParsedPal;
			if (!ACEDatUnpack::UnpackPalette(PalCur, ParsedPal))
			{
				return false;
			}
			LocalColors = MoveTemp(ParsedPal.Colors);
			Colors = &LocalColors;
		}

		const int32 PixCount = Tex.Width * Tex.Height;
		Out.Pixels.SetNumUninitialized(PixCount);
		FACEDatCursor Cur(Tex.SourceData);
		bool bOk = true;
		for (int32 i = 0; i < PixCount; ++i)
		{
			int32 Index = 0;
			if (Tex.Format == EACESurfacePixelFormat::INDEX16)
			{
				Index = Cur.ReadI16(bOk);
			}
			else
			{
				Index = Cur.ReadU8(bOk);
			}
			if (!bOk)
			{
				Out.Pixels[i] = FColor(0, 0, 0, 0);
				continue;
			}
			// ACViewer TextureCache.IndexToColor: ClipMap palette indices 0..7 are holes
			// (door/window openings). Those slots are often authored as bright magenta.
			if (bClipMap && Index >= 0 && Index < 8)
			{
				Out.Pixels[i] = FColor(0, 0, 0, 0);
				continue;
			}
			if (!Colors->IsValidIndex(Index))
			{
				Out.Pixels[i] = bClipMap ? FColor(0, 0, 0, 0) : FColor::Magenta;
				continue;
			}
			const uint32 Argb = (*Colors)[Index];
			Out.Pixels[i] = FColor((Argb >> 16) & 0xFF, (Argb >> 8) & 0xFF, Argb & 0xFF, (Argb >> 24) & 0xFF);
		}
		Out.bHasPixels = true;
		return true;
	}
	case EACESurfacePixelFormat::A8R8G8B8:
	{
		FACEDatCursor Cur(Tex.SourceData);
		const int32 PixCount = Tex.Width * Tex.Height;
		Out.Pixels.SetNumUninitialized(PixCount);
		bool bOk = true;
		for (int32 i = 0; i < PixCount; ++i)
		{
			const uint32 Argb = Cur.ReadU32(bOk);
			Out.Pixels[i] = FColor((Argb >> 16) & 0xFF, (Argb >> 8) & 0xFF, Argb & 0xFF, (Argb >> 24) & 0xFF);
		}
		Out.bHasPixels = bOk;
		return bOk;
	}
	case EACESurfacePixelFormat::R8G8B8:
	{
		FACEDatCursor Cur(Tex.SourceData);
		const int32 PixCount = Tex.Width * Tex.Height;
		Out.Pixels.SetNumUninitialized(PixCount);
		bool bOk = true;
		for (int32 i = 0; i < PixCount; ++i)
		{
			const uint8 B = Cur.ReadU8(bOk);
			const uint8 G = Cur.ReadU8(bOk);
			const uint8 R = Cur.ReadU8(bOk);
			Out.Pixels[i] = FColor(R, G, B, 255);
		}
		Out.bHasPixels = bOk;
		return bOk;
	}
	case EACESurfacePixelFormat::CUSTOM_LSCAPE_R8G8B8:
	{
		FACEDatCursor Cur(Tex.SourceData);
		const int32 PixCount = Tex.Width * Tex.Height;
		Out.Pixels.SetNumUninitialized(PixCount);
		bool bOk = true;
		for (int32 i = 0; i < PixCount; ++i)
		{
			const uint8 R = Cur.ReadU8(bOk);
			const uint8 G = Cur.ReadU8(bOk);
			const uint8 B = Cur.ReadU8(bOk);
			Out.Pixels[i] = FColor(R, G, B, 255);
		}
		Out.bHasPixels = bOk;
		return bOk;
	}
	case EACESurfacePixelFormat::A8:
	case EACESurfacePixelFormat::CUSTOM_LSCAPE_ALPHA:
	{
		FACEDatCursor Cur(Tex.SourceData);
		const int32 PixCount = Tex.Width * Tex.Height;
		Out.Pixels.SetNumUninitialized(PixCount);
		bool bOk = true;
		for (int32 i = 0; i < PixCount; ++i)
		{
			const uint8 A = Cur.ReadU8(bOk);
			Out.Pixels[i] = FColor(A, A, A, 255);
		}
		Out.bHasPixels = bOk;
		return bOk;
	}
	case EACESurfacePixelFormat::A4R4G4B4:
	{
		// D3D format 26: little-endian ARGB nibbles, including authored coverage.
		FACEDatCursor Cur(Tex.SourceData);
		const int32 PixCount = Tex.Width * Tex.Height;
		if (Tex.SourceData.Num() < PixCount * 2) return false;
		Out.Pixels.SetNumUninitialized(PixCount);
		bool bOk = true;
		for (int32 i = 0; i < PixCount; ++i)
		{
			const uint16 Argb = Cur.ReadU16(bOk);
			Out.Pixels[i] = FColor(((Argb >> 8) & 15) * 17, ((Argb >> 4) & 15) * 17,
				(Argb & 15) * 17, ((Argb >> 12) & 15) * 17);
		}
		Out.bHasPixels = bOk;
		Out.bUsesAlpha = true;
		return bOk;
	}
	case EACESurfacePixelFormat::R5G6B5:
	{
		FACEDatCursor Cur(Tex.SourceData);
		const int32 PixCount = Tex.Width * Tex.Height;
		Out.Pixels.SetNumUninitialized(PixCount);
		bool bOk = true;
		for (int32 i = 0; i < PixCount; ++i)
		{
			const uint16 Val = Cur.ReadU16(bOk);
			const uint8 R = static_cast<uint8>(((Val & 0xF800) >> 11) << 3);
			const uint8 G = static_cast<uint8>(((Val & 0x07E0) >> 5) << 2);
			const uint8 B = static_cast<uint8>((Val & 0x001F) << 3);
			Out.Pixels[i] = FColor(R, G, B, 255);
		}
		Out.bHasPixels = bOk;
		return bOk;
	}
	default:
		return false;
	}

	if (RGBA.Num() < Tex.Width * Tex.Height * 4)
	{
		return false;
	}
	const int32 PixCount = Tex.Width * Tex.Height;
	Out.Pixels.SetNumUninitialized(PixCount);
	for (int32 i = 0; i < PixCount; ++i)
	{
		const int32 O = i * 4;
		Out.Pixels[i] = FColor(RGBA[O], RGBA[O + 1], RGBA[O + 2], RGBA[O + 3]);
	}
	Out.bHasPixels = true;
	return true;
}

bool FACEDatTextureResolver::ResolveSurface(uint32 SurfaceId, FACEDatDecodedSurface& Out)
{
	return ResolveSurfaceWithAppearance(SurfaceId, INDEX_NONE, nullptr, Out);
}

bool FACEDatTextureResolver::ResolveSurfaceWithAppearance(uint32 SurfaceId, int32 PartIndex, const FACEObjDesc* Appearance, FACEDatDecodedSurface& Out)
{
	++SurfaceResolveCount;
	const bool bUseCache = Appearance == nullptr || !Appearance->HasVisualOverrides();
	if (bUseCache)
	{
		if (const FACEDatDecodedSurface* Cached = SurfaceCache.Find(SurfaceId))
		{
			Out = *Cached;
			return Out.bHasPixels || Out.bIsSolid;
		}
	}

	Out = FACEDatDecodedSurface();
	Out.SurfaceId = SurfaceId;

	TArray<uint8> Blob;
	if (!ReadBlob(SurfaceId, Blob))
	{
		return false;
	}
	FACEDatCursor Cur(Blob);
	FACEDatSurface Surf;
	if (!ACEDatUnpack::UnpackSurface(Cur, Surf))
	{
		return false;
	}

	const bool bImage = EnumHasAnyFlags(Surf.Type, EACESurfaceType::Base1Image)
		|| EnumHasAnyFlags(Surf.Type, EACESurfaceType::Base1ClipMap);
	const bool bClipMap = EnumHasAnyFlags(Surf.Type, EACESurfaceType::Base1ClipMap);
	// Match ACViewer AlphaSurfaceTypes: ClipMap | Translucent | Alpha | Additive.
	const bool bSurfaceAlpha = EnumHasAnyFlags(Surf.Type, EACESurfaceType::Alpha)
		|| EnumHasAnyFlags(Surf.Type, EACESurfaceType::Translucent)
		|| EnumHasAnyFlags(Surf.Type, EACESurfaceType::Additive)
		|| EnumHasAnyFlags(Surf.Type, EACESurfaceType::InvAlpha)
		|| Surf.Translucency > 0.01f;
	if (!bImage)
	{
		Out.bIsSolid = true;
		Out.SolidColor = ArgbToLinear(Surf.ColorValue);
		// Retail D3DPolyRender::SetSurface: curr_alpha = (1 - translucency) * Color.A;
		// TRANSLUCENT → SRCALPHA blend, z-write off. Mag-nus 06-rendering §10.
		// Do not invent hue heuristics — portal depth cutters are a separate path.
		const float Trans = FMath::Clamp(Surf.Translucency, 0.f, 1.f);
		const float ColorA = FMath::Clamp(Out.SolidColor.A, 0.f, 1.f);
		Out.SolidColor.A = ColorA * (1.f - Trans);
		const bool bTranslucentType = EnumHasAnyFlags(Surf.Type, EACESurfaceType::Translucent)
			|| EnumHasAnyFlags(Surf.Type, EACESurfaceType::Alpha)
			|| EnumHasAnyFlags(Surf.Type, EACESurfaceType::Additive);
		Out.bAdditive = EnumHasAnyFlags(Surf.Type, EACESurfaceType::Additive);
		// Only zero opacity is invisible. High translucency is still visible glass;
		// a color or translucency threshold must not remove authored geometry.
		Out.bFullyTransparent = Out.SolidColor.A <= 0.f;
		Out.bUsesAlpha = !Out.bFullyTransparent && (Out.SolidColor.A < 0.99f || bTranslucentType || bSurfaceAlpha);
		Out.Luminosity = Surf.Luminosity;
		Out.Diffuse = Surf.Diffuse;
		Out.Translucency = Surf.Translucency;
		Out.bSurfaceTranslucent = EnumHasAnyFlags(Surf.Type, EACESurfaceType::Translucent)
			|| Surf.Translucency > 0.01f;
		if (bUseCache)
		{
			SurfaceCache.Add(SurfaceId, Out);
		}
		return true;
	}

	uint32 SurfaceTexId = Surf.OrigTextureId;

	if (Appearance && PartIndex >= 0)
	{
		// Last matching change wins — underwear then armor both map the same OldTexture.
		for (int32 Ci = Appearance->TextureChanges.Num() - 1; Ci >= 0; --Ci)
		{
			const FACEObjDescTextureChange& Change = Appearance->TextureChanges[Ci];
			if (static_cast<int32>(Change.PartIndex) != PartIndex
				|| static_cast<uint32>(Change.OldTexture) != SurfaceTexId)
			{
				continue;
			}

			SurfaceTexId = static_cast<uint32>(Change.NewTexture);
			break;
		}
	}

	TArray<uint8> StBlob;
	if (!ReadBlob(SurfaceTexId, StBlob))
	{
		return false;
	}
	FACEDatCursor StCur(StBlob);
	FACEDatSurfaceTexture St;
	if (!ACEDatUnpack::UnpackSurfaceTexture(StCur, St) || St.Textures.Num() == 0)
	{
		return false;
	}

	// Textures[0] is often a 0×0 / tiny portal stub; pick the largest valid (usually highres).
	FACEDatTexture Tex;
	bool bGotTex = false;
	int32 BestArea = 0;
	for (const uint32 TexId : St.Textures)
	{
		FACEDatTexture Candidate;
		if (!LoadTexture(TexId, Candidate) || Candidate.SourceData.Num() == 0
			|| Candidate.Width <= 0 || Candidate.Height <= 0)
		{
			continue;
		}
		const int32 Area = Candidate.Width * Candidate.Height;
		if (!bGotTex || Area > BestArea)
		{
			Tex = MoveTemp(Candidate);
			BestArea = Area;
			bGotTex = true;
		}
	}
	if (!bGotTex)
	{
		return false;
	}

	TArray<uint32> WorkingPalette;
	const TArray<uint32>* PalettePtr = nullptr;
	if (Appearance && Appearance->HasVisualOverrides())
	{
        // CPartArray::SetPalette / CPhysicsPart::UsePalette select by original
        // palette DID. Clothing count is unrelated: remote naked heads can have
        // dozens of clothing overrides and still require hair/eye subpalettes.
        uint32 OriginalPalette = Surf.OrigPaletteId;
        // CPhysicsPart chooses its palette before ObjDesc changes the texture.
        // The Focusing Stone replaces a BF8 texture with a BEF texture but
        // still applies its BF8 -> BF1 yellow palette to those new indices.
        if (!OriginalPalette && SurfaceTexId != Surf.OrigTextureId)
        {
            TArray<uint8> OriginalBlob;
            if (ReadBlob(Surf.OrigTextureId, OriginalBlob))
            {
                FACEDatCursor OriginalCursor(OriginalBlob); FACEDatSurfaceTexture Original;
                if (ACEDatUnpack::UnpackSurfaceTexture(OriginalCursor, Original))
                    for (uint32 Id : Original.Textures)
                    {
                        FACEDatTexture Source;
                        if (LoadTexture(Id, Source) && Source.DefaultPaletteId)
                        { OriginalPalette = Source.DefaultPaletteId; break; }
                    }
            }
        }
        if (!OriginalPalette) OriginalPalette = Tex.DefaultPaletteId;
        if (OriginalPalette == static_cast<uint32>(Appearance->PaletteBaseId)
            && BuildWorkingPalette(*Appearance, OriginalPalette, WorkingPalette, true))
            PalettePtr = &WorkingPalette;
	}

	if (!DecodeTexture(Tex, PalettePtr, Out, bClipMap))
	{
		// An unreadable image is not an authored solid surface. In particular,
		// luminous particle cards must never turn into opaque fallback rectangles.
		return false;
	}

	Out.SurfaceId = SurfaceId;
	if (Out.bHasPixels && bClipMap)
	{
		// Retail ClipMap transparency comes from the indexed palette holes decoded above.
		// RGB color-key heuristics incorrectly erase legitimate blue/green object pixels.
		Out.bUsesAlpha = true;
		Out.bClipMap = true;
		Out.AlphaTestReference = (Tex.Format == EACESurfacePixelFormat::INDEX16 ? 100.f : 200.f) / 255.f;
	}
	else
	{
		Out.bClipMap = false;
	}

	Out.bAdditive = EnumHasAnyFlags(Surf.Type, EACESurfaceType::Additive);
	// ACViewer PremultiplyAlpha on A only: a *= (1 - Translucency). Soft glass (lifestones)
	// needs this; ClipMap holes already have A=0 and stay holes.
	// Additive rain curtains (0x080000C5 Translucency=0.5) must NOT be halved — Unreal
	// additive already uses tex.A, and crushing A made the streaks nearly invisible.
	const float AlphaScale = 1.f - FMath::Clamp(Surf.Translucency, 0.f, 1.f);
	if (Out.bHasPixels && AlphaScale < 0.999f && !Out.bAdditive)
	{
		for (FColor& C : Out.Pixels)
		{
			C.A = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(C.A) * AlphaScale), 0, 255));
		}
	}
	// Surface flags and authored translucency apply equally to world objects and ObjDesc
	// appearances. PhysicsDesc object translucency is multiplied later per instance.
	Out.bUsesAlpha = Out.bHasPixels && (bClipMap || bSurfaceAlpha || (!Out.bAdditive && AlphaScale < 0.999f));
	Out.Luminosity = Surf.Luminosity;
	Out.Diffuse = Surf.Diffuse;
	Out.Translucency = Surf.Translucency;
	Out.bSurfaceTranslucent = EnumHasAnyFlags(Surf.Type, EACESurfaceType::Translucent)
		|| Surf.Translucency > 0.01f;
	// Luminosity changes lighting, not the blend mode. The destroyed portal's
	// A4R4G4B4 ground card is a black alpha shadow, not additive white glow.

	// Portal FX anchors (e.g. 0x08000157 / DXT1 fully transparent ClipMap) must not fall
	// through to opaque vertex-color slabs.
	if (Out.bHasPixels && Out.bClipMap)
	{
		bool bAnyOpaque = false;
		for (const FColor& Pixel : Out.Pixels)
		{
			if (Pixel.A > 8)
			{
				bAnyOpaque = true;
				break;
			}
		}
		if (!bAnyOpaque)
		{
			Out.bFullyTransparent = true;
		}
	}

	if (bUseCache)
	{
		SurfaceCache.Add(SurfaceId, Out);
	}
	return true;
}

bool FACEDatTextureResolver::DecodeSurfaceTexture(uint32 SurfaceTextureId, FACEDatDecodedSurface& Out)
{
	Out = FACEDatDecodedSurface();
	Out.SurfaceId = SurfaceTextureId;
	if (SurfaceTextureId == 0)
	{
		return false;
	}

	TArray<uint8> StBlob;
	if (!ReadBlob(SurfaceTextureId, StBlob))
	{
		return false;
	}
	FACEDatCursor StCur(StBlob);
	FACEDatSurfaceTexture St;
	if (!ACEDatUnpack::UnpackSurfaceTexture(StCur, St) || St.Textures.Num() == 0)
	{
		return false;
	}

	FACEDatTexture Tex;
	bool bGotTex = false;
	for (const uint32 TexId : St.Textures)
	{
		FACEDatTexture Candidate;
		if (LoadTexture(TexId, Candidate) && Candidate.SourceData.Num() > 0
			&& Candidate.Width > 0 && Candidate.Height > 0)
		{
			Tex = MoveTemp(Candidate);
			bGotTex = true;
			break;
		}
	}
	if (!bGotTex || !DecodeTexture(Tex, nullptr, Out))
	{
		return false;
	}
	Out.SurfaceId = SurfaceTextureId;
	return Out.bHasPixels;
}

UTexture2D* FACEDatTextureResolver::GetOrCreateUTexture(uint32 SurfaceId, UObject* Outer, bool bWrapTexture)
{
	const uint32 CacheKey = WorldTextureKey(SurfaceId, bWrapTexture);
	if (auto* Retained = TakeRetainedTexture(CacheKey, RetainedWorldTextures))
	{ BindRuntimeTexture(Retained); TextureObjects.Add(CacheKey, Retained); }
	if (TObjectPtr<UTexture2D>* Found = TextureObjects.Find(CacheKey))
	{
		UTexture2D* Existing = Found->Get();
		if (!IsRuntimeDxtTexture(Existing))
		{
			TouchTexture(CacheKey);
			return Existing;
		}
		if (UACEDatSubsystem* Dat = Cast<UACEDatSubsystem>(GcOwner))
		{
			Dat->UntrackRuntimeTexture(Existing);
		}
		TextureObjects.Remove(CacheKey);
		TextureLastUsed.Remove(CacheKey);
	}

	FACEDatDecodedSurface Decoded;
	if (!ResolveSurface(SurfaceId, Decoded) || Decoded.Width <= 0)
	{
		return nullptr;
	}
	if (!Decoded.bHasPixels)
	{
		SurfaceCache.Remove(SurfaceId);
		if (!ResolveSurface(SurfaceId, Decoded) || !Decoded.bHasPixels || Decoded.Width <= 0)
		{
			return nullptr;
		}
	}

	UTexture2D* Tex = CreateTransientRgbaWithMips(
		Decoded.Width, Decoded.Height, Decoded.Pixels, Decoded.bUsesAlpha,
		bWrapTexture ? TA_Wrap : TA_Clamp, bWrapTexture ? TA_Wrap : TA_Clamp);
	if (!Tex)
	{
		return nullptr;
	}
	if (Outer)
	{
		Tex->Rename(nullptr, Outer, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
	}
	BindRuntimeTexture(Tex);

	TextureObjects.Add(CacheKey, Tex);
	TouchTexture(CacheKey);
	UE_LOG(LogTemp, Verbose, TEXT("ACEDat: UTexture2D %ux%u for Surface 0x%08X"), Decoded.Width, Decoded.Height, SurfaceId);
	return Tex;
}

UTexture2D* FACEDatTextureResolver::GetOrCreateSkyUTexture(uint32 SurfaceId, UObject* Outer, bool bScrolling)
{
	// bit0: Wrap vs Clamp. bits1-7: decode rev (v3 = opaque day-dome A=255).
	constexpr uint64 SkyTexDecodeRev = 3ull;
	const uint64 CacheKey = (static_cast<uint64>(SurfaceId) << 8)
		| (SkyTexDecodeRev << 1)
		| (bScrolling ? 1ull : 0ull);
	if (TObjectPtr<UTexture2D>* Found = SkyTextureObjects.Find(CacheKey))
	{
		if (UTexture2D* Existing = Found->Get())
		{
			TouchSkyTexture(CacheKey);
			return Existing;
		}
		SkyTextureObjects.Remove(CacheKey);
		SkyLastUsed.Remove(CacheKey);
	}

	FACEDatDecodedSurface Decoded;
	if (!ResolveSurface(SurfaceId, Decoded) || !Decoded.bHasPixels || Decoded.Width <= 0)
	{
		return nullptr;
	}

	const TextureAddress Addr = bScrolling ? TA_Wrap : TA_Clamp;
	UTexture2D* Tex = CreateTransientRgbaSky(
		Decoded.Width, Decoded.Height, Decoded.Pixels, Decoded.bUsesAlpha, Addr, Addr);
	if (!Tex)
	{
		return nullptr;
	}
	if (Outer)
	{
		Tex->Rename(nullptr, Outer, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
	}
	BindRuntimeTexture(Tex);

	SkyTextureObjects.Add(CacheKey, Tex);
	TouchSkyTexture(CacheKey);
	UE_LOG(LogTemp, Verbose, TEXT("ACEDat: sky UTexture2D %ux%u for Surface 0x%08X (%s, scroll=%d)"),
		Decoded.Width, Decoded.Height, SurfaceId, bScrolling ? TEXT("wrap") : TEXT("clamp"),
		bScrolling ? 1 : 0);
	return Tex;
}

UTexture2D* FACEDatTextureResolver::GetOrCreateUiTexture(uint32 TextureId)
{
	// UI chrome uses one mip with bilinear filtering. Keep it separate from
	// the same DAT image used on a world surface, which still needs its mips.
	const uint32 CacheKey = TextureId | 0x80000000u;
	if (auto* Retained = TakeRetainedTexture(CacheKey, RetainedWorldTextures))
	{ BindRuntimeTexture(Retained); TextureObjects.Add(CacheKey, Retained); }
	if (TextureId == 0)
	{
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* Found = TextureObjects.Find(CacheKey))
	{
		UTexture2D* Existing = Found->Get();
		if (!IsRuntimeDxtTexture(Existing))
		{
			TouchTexture(CacheKey);
			return Existing;
		}
		if (UACEDatSubsystem* Dat = Cast<UACEDatSubsystem>(GcOwner))
		{
			Dat->UntrackRuntimeTexture(Existing);
		}
		TextureObjects.Remove(CacheKey);
		TextureLastUsed.Remove(CacheKey);
	}

	FACEDatTexture Raw;
	FACEDatDecodedSurface Decoded;
	if (!LoadTexture(TextureId, Raw) || !DecodeTexture(Raw, nullptr, Decoded)
		|| !Decoded.bHasPixels || Decoded.Width <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: failed to decode UI texture 0x%08X"), TextureId);
		return nullptr;
	}

	// Most retail UI chrome is A8R8G8B8; DecodeTexture doesn't set bUsesAlpha for raw formats.
	bool bAlpha = (Raw.Format == EACESurfacePixelFormat::A8R8G8B8);
	if (!bAlpha)
	{
		for (const FColor& C : Decoded.Pixels)
		{
			if (C.A < 255)
			{
				bAlpha = true;
				break;
			}
		}
	}

	// Wrap so Slate tiled brushes (status bar strip, panel smoke) repeat cleanly.
	UTexture2D* Tex = CreateTransientRgbaWithMips(
		Decoded.Width, Decoded.Height, Decoded.Pixels, bAlpha,
		TA_Wrap, TA_Wrap, false, true, 1, TF_Bilinear, false);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->LODGroup = TEXTUREGROUP_UI;
	Tex->Filter = TF_Bilinear;
	Tex->CompressionSettings = TC_EditorIcon;
	Tex->UpdateResource();
	BindRuntimeTexture(Tex);

	TextureObjects.Add(CacheKey, Tex);
	TouchTexture(CacheKey);
	UE_LOG(LogTemp, Log, TEXT("ACEDat: UI UTexture2D %ux%u for Texture 0x%08X (alpha=%d)"),
		Decoded.Width, Decoded.Height, TextureId, bAlpha ? 1 : 0);
	return Tex;
}

UTexture2D* FACEDatTextureResolver::GetOrCreateUiIconTexture(uint32 TextureId)
{
	if (auto* Retained = TakeRetainedTexture(TextureId, RetainedIconTextures))
	{ BindRuntimeTexture(Retained); IconTextureObjects.Add(TextureId, Retained); }
	if (TextureId == 0)
	{
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* Found = IconTextureObjects.Find(TextureId))
	{
		UTexture2D* Existing = Found->Get();
		if (!IsRuntimeDxtTexture(Existing))
		{
			IconLastUsed.Add(TextureId, FPlatformTime::Seconds());
			return Existing;
		}
		if (UACEDatSubsystem* Dat = Cast<UACEDatSubsystem>(GcOwner))
		{
			Dat->UntrackRuntimeTexture(Existing);
		}
		IconTextureObjects.Remove(TextureId);
		IconLastUsed.Remove(TextureId);
	}

	FACEDatTexture Raw;
	FACEDatDecodedSurface Decoded;
	if (!LoadTexture(TextureId, Raw) || !DecodeTexture(Raw, nullptr, Decoded)
		|| !Decoded.bHasPixels || Decoded.Width <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: failed to decode UI icon 0x%08X"), TextureId);
		return nullptr;
	}

	TArray<FColor> IconPixels = Decoded.Pixels;
	IconPixels.SetNum(Decoded.Width * Decoded.Height, EAllowShrinking::No);
	// Transparent texels keep white RGB in DAT. Knock that out, then flood opaque RGB into
	// the holes so bilinear stretch cannot pick a white 1px frame around the cutout.
	for (FColor& C : IconPixels)
	{
		if (C.A == 0)
		{
			C.R = 0;
			C.G = 0;
			C.B = 0;
		}
	}
	DilateRgbIntoTransparent(IconPixels, Decoded.Width, Decoded.Height, /*Passes*/ 2);

	UTexture2D* Tex = CreateTransientRgbaUi(Decoded.Width, Decoded.Height, IconPixels);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->Filter = TF_Bilinear;
	Tex->UpdateResource();
	BindRuntimeTexture(Tex);
	IconTextureObjects.Add(TextureId, Tex);
	IconLastUsed.Add(TextureId, FPlatformTime::Seconds());
	return Tex;
}

UTexture2D* FACEDatTextureResolver::GetOrCreateUiTextureWithAlpha(uint32 ColorTextureId, uint32 AlphaTextureId)
{
	if (ColorTextureId == 0)
	{
		return nullptr;
	}
	if (AlphaTextureId == 0)
	{
		return GetOrCreateUiTexture(ColorTextureId);
	}

	const uint64 Key = (static_cast<uint64>(ColorTextureId) << 32) | static_cast<uint64>(AlphaTextureId)
		| (0x3ull << 56); // v3: multiply color.A * mask.A (preserve button fill translucency)
	if (auto* Retained = TakeRetainedTexture(Key, RetainedCompositeTextures))
	{ BindRuntimeTexture(Retained); CompositeUiTextures.Add(Key, Retained); }
	if (TObjectPtr<UTexture2D>* Found = CompositeUiTextures.Find(Key))
	{
		UTexture2D* Existing = Found->Get();
		if (!IsRuntimeDxtTexture(Existing))
		{
			TouchCompositeTexture(Key);
			return Existing;
		}
		if (UACEDatSubsystem* Dat = Cast<UACEDatSubsystem>(GcOwner))
		{
			Dat->UntrackRuntimeTexture(Existing);
		}
		CompositeUiTextures.Remove(Key);
		CompositeLastUsed.Remove(Key);
	}

	FACEDatTexture ColorRaw;
	FACEDatDecodedSurface ColorDecoded;
	if (!LoadTexture(ColorTextureId, ColorRaw) || !DecodeTexture(ColorRaw, nullptr, ColorDecoded)
		|| !ColorDecoded.bHasPixels || ColorDecoded.Width <= 0)
	{
		return GetOrCreateUiTexture(ColorTextureId);
	}

	FACEDatTexture AlphaRaw;
	FACEDatDecodedSurface AlphaDecoded;
	if (!LoadTexture(AlphaTextureId, AlphaRaw) || !DecodeTexture(AlphaRaw, nullptr, AlphaDecoded)
		|| !AlphaDecoded.bHasPixels || AlphaDecoded.Width <= 0)
	{
		return GetOrCreateUiTexture(ColorTextureId);
	}

	TArray<FColor> OutPixels;
	OutPixels.SetNumUninitialized(ColorDecoded.Pixels.Num());
	const int32 W = ColorDecoded.Width;
	const int32 H = ColorDecoded.Height;
	const int32 AW = AlphaDecoded.Width;
	const int32 AH = AlphaDecoded.Height;
	for (int32 Y = 0; Y < H; ++Y)
	{
		const int32 AY = (AH == H) ? Y : (Y * AH) / H;
		for (int32 X = 0; X < W; ++X)
		{
			const int32 AX = (AW == W) ? X : (X * AW) / W;
			FColor C = ColorDecoded.Pixels[Y * W + X];
			const FColor A = AlphaDecoded.Pixels[AY * AW + AX];
			// A8 atlases store coverage in RGB (DecodeTexture forces A=255).
			// A8R8G8B8 UI masks store opacity in A (RGB is often 0 even when opaque).
			uint8 MaskA = A.A;
			if (AlphaRaw.Format == EACESurfacePixelFormat::A8
				|| AlphaRaw.Format == EACESurfacePixelFormat::CUSTOM_LSCAPE_ALPHA)
			{
				MaskA = A.R;
			}
			// Multiply: mask punches the silhouette; color keeps its own translucency
			// (Enter Game idle center is semi-transparent over the BG art).
			C.A = static_cast<uint8>((static_cast<int32>(C.A) * static_cast<int32>(MaskA)) / 255);
			OutPixels[Y * W + X] = C;
		}
	}

	UTexture2D* Tex = CreateTransientRgbaUi(W, H, OutPixels);
	if (!Tex)
	{
		return GetOrCreateUiTexture(ColorTextureId);
	}
	BindRuntimeTexture(Tex);
	CompositeUiTextures.Add(Key, Tex);
	TouchCompositeTexture(Key);
	UE_LOG(LogTemp, Log, TEXT("ACEDat: UI composite 0x%08X + alpha 0x%08X (%dx%d)"),
		ColorTextureId, AlphaTextureId, W, H);
	return Tex;
}

void FACEDatTextureResolver::ReleaseTextureObjects()
{
	RetainedWorldTextures.Reset(); RetainedIconTextures.Reset(); RetainedCompositeTextures.Reset();
	auto Unbind = [this](UTexture2D* Tex)
	{
		if (!Tex)
		{
			return;
		}
		Tex->RemoveFromRoot();
		if (UACEDatSubsystem* Dat = Cast<UACEDatSubsystem>(GcOwner))
		{
			Dat->UntrackRuntimeTexture(Tex);
		}
	};
	for (auto& Pair : TextureObjects)
	{
		Unbind(Pair.Value.Get());
	}
	TextureObjects.Reset();
	for (auto& Pair : IconTextureObjects)
	{
		Unbind(Pair.Value.Get());
	}
	IconTextureObjects.Reset();
	IconLastUsed.Reset();
	for (auto& Pair : SkyTextureObjects)
	{
		Unbind(Pair.Value.Get());
	}
	SkyTextureObjects.Reset();
	for (auto& Pair : CompositeUiTextures)
	{
		Unbind(Pair.Value.Get());
	}
	CompositeUiTextures.Reset();
	SurfaceCache.Reset();
	TextureLastUsed.Reset();
	SkyLastUsed.Reset();
	CompositeLastUsed.Reset();
}

void FACEDatTextureResolver::DiscardNonSkyRuntimeTextures()
{
	RetainedWorldTextures.Reset(); RetainedIconTextures.Reset(); RetainedCompositeTextures.Reset();
	auto Unbind = [this](UTexture2D* Tex)
	{
		if (!Tex)
		{
			return;
		}
		Tex->RemoveFromRoot();
		if (UACEDatSubsystem* Dat = Cast<UACEDatSubsystem>(GcOwner))
		{
			Dat->UntrackRuntimeTexture(Tex);
		}
	};
	for (auto& Pair : TextureObjects)
	{
		Unbind(Pair.Value.Get());
	}
	TextureObjects.Reset();
	for (auto& Pair : IconTextureObjects)
	{
		Unbind(Pair.Value.Get());
	}
	IconTextureObjects.Reset();
	IconLastUsed.Reset();
	TextureLastUsed.Reset();
	for (auto& Pair : CompositeUiTextures)
	{
		Unbind(Pair.Value.Get());
	}
	CompositeUiTextures.Reset();
	CompositeLastUsed.Reset();
}

void FACEDatTextureResolver::TrimCaches(int32 MaxTextures, int32 MaxDecodedSurfaces, int64 MaxBytes)
{
	auto PruneExpired = [](auto& Map)
	{
		for (auto It = Map.CreateIterator(); It; ++It) if (!It.Value().IsValid()) It.RemoveCurrent();
	};
	PruneExpired(RetainedWorldTextures); PruneExpired(RetainedIconTextures); PruneExpired(RetainedCompositeTextures);
	MaxTextures = FMath::Max(64, MaxTextures);
	MaxDecodedSurfaces = FMath::Max(64, MaxDecodedSurfaces);
	MaxBytes = FMath::Max(64ll << 20, MaxBytes);
	const double Now = FPlatformTime::Seconds();
	const double PinUntil = Now - 8.0;
	UACEDatSubsystem* Dat = Cast<UACEDatSubsystem>(GcOwner);

	auto Unbind = [Dat](UTexture2D* Tex)
	{
		if (!Tex)
		{
			return;
		}
		Tex->RemoveFromRoot();
		if (Dat)
		{
			Dat->UntrackRuntimeTexture(Tex);
		}
	};

	struct FCandU32 { uint32 Key; double Last; int32 Bytes; };
	struct FCandU64 { uint64 Key; double Last; int32 Bytes; };

	auto CollectU32 = [&](TMap<uint32, TObjectPtr<UTexture2D>>& Map, const TMap<uint32, double>& Used, TArray<FCandU32>& Out)
	{
		for (const auto& Pair : Map)
		{
			const double* T = Used.Find(Pair.Key);
			const double Last = T ? *T : 0.0;
			if (Last >= PinUntil)
			{
				continue;
			}
			FCandU32 C;
			C.Key = Pair.Key;
			C.Last = Last;
			C.Bytes = EstimateTextureGpuBytes(Pair.Value.Get());
			Out.Add(C);
		}
	};
	auto CollectU64 = [&](TMap<uint64, TObjectPtr<UTexture2D>>& Map, const TMap<uint64, double>& Used, TArray<FCandU64>& Out)
	{
		for (const auto& Pair : Map)
		{
			const double* T = Used.Find(Pair.Key);
			const double Last = T ? *T : 0.0;
			if (Last >= PinUntil)
			{
				continue;
			}
			FCandU64 C;
			C.Key = Pair.Key;
			C.Last = Last;
			C.Bytes = EstimateTextureGpuBytes(Pair.Value.Get());
			Out.Add(C);
		}
	};

	auto EvictU32 = [&](TMap<uint32, TObjectPtr<UTexture2D>>& Map, TMap<uint32, TWeakObjectPtr<UTexture2D>>& Retained, TMap<uint32, double>& Used, TArray<FCandU32>& Cands, int32& LiveCount, int64& LiveBytes)
	{
		Cands.Sort([](const FCandU32& A, const FCandU32& B) { return A.Last < B.Last; });
		for (const FCandU32& C : Cands)
		{
			if (LiveCount <= MaxTextures && LiveBytes <= MaxBytes)
			{
				break;
			}
			if (UTexture2D* Tex = Map.FindRef(C.Key).Get())
			{
				// A mesh/material may still own this texture after the cache releases
				// it. Reuse that live object instead of allocating another GPU copy.
				Retained.Add(C.Key, Tex);
				Unbind(Tex);
			}
			Map.Remove(C.Key);
			Used.Remove(C.Key);
			SurfaceCache.Remove(C.Key);
			--LiveCount;
			LiveBytes -= C.Bytes;
		}
	};
	auto EvictU64 = [&](TMap<uint64, TObjectPtr<UTexture2D>>& Map, TMap<uint64, double>& Used, TArray<FCandU64>& Cands, int32& LiveCount, int64& LiveBytes)
	{
		Cands.Sort([](const FCandU64& A, const FCandU64& B) { return A.Last < B.Last; });
		for (const FCandU64& C : Cands)
		{
			if (LiveCount <= MaxTextures && LiveBytes <= MaxBytes)
			{
				break;
			}
			if (UTexture2D* Tex = Map.FindRef(C.Key).Get())
			{
				RetainedCompositeTextures.Add(C.Key, Tex);
				Unbind(Tex);
			}
			Map.Remove(C.Key);
			Used.Remove(C.Key);
			--LiveCount;
			LiveBytes -= C.Bytes;
		}
	};

	int32 LiveCount = TextureObjects.Num() + IconTextureObjects.Num()
		+ SkyTextureObjects.Num() + CompositeUiTextures.Num();
	int64 LiveBytes = EstimateResidentTextureBytes();
	TArray<FCandU32> WorldCands;
	TArray<FCandU32> IconCands;
	TArray<FCandU64> CompCands;
	CollectU32(TextureObjects, TextureLastUsed, WorldCands);
	CollectU32(IconTextureObjects, IconLastUsed, IconCands);
	CollectU64(CompositeUiTextures, CompositeLastUsed, CompCands);
	EvictU32(TextureObjects, RetainedWorldTextures, TextureLastUsed, WorldCands, LiveCount, LiveBytes);
	EvictU32(IconTextureObjects, RetainedIconTextures, IconLastUsed, IconCands, LiveCount, LiveBytes);
	// Day-dome / cloud / star textures are created once at GameSky build and never
	// touched again. LRU treated them as the oldest candidates and evicted the
	// cube faces first (black sky, clouds still showing). Always keep them.
	EvictU64(CompositeUiTextures, CompositeLastUsed, CompCands, LiveCount, LiveBytes);

	const int32 SurfOver = SurfaceCache.Num() - MaxDecodedSurfaces;
	if (SurfOver > 0)
	{
		TArray<uint32> Keys;
		SurfaceCache.GetKeys(Keys);
		auto LastSurfaceUse = [&](uint32 Id)
		{
			return FMath::Max(TextureLastUsed.FindRef(WorldTextureKey(Id, false)),
				TextureLastUsed.FindRef(WorldTextureKey(Id, true)));
		};
		Keys.Sort([&](uint32 A, uint32 B)
		{
			return LastSurfaceUse(A) < LastSurfaceUse(B);
		});
		const int32 RemoveCount = FMath::Min(SurfOver, Keys.Num());
		for (int32 i = 0; i < RemoveCount; ++i)
		{
			SurfaceCache.Remove(Keys[i]);
		}
	}
}
