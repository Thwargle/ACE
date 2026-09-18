#include "UI/ACEUIResourceResolver.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Dat/ACEDatFontRenderer.h"
#include "Dat/ACEDatCursor.h"
#include "Engine/Texture2D.h"

void UACEUIResourceResolver::Initialize(UACEDatSubsystem* InDat)
{
	SpellIconCache.Reset();
	Dat = InDat;
	Cache.Reset();
	IconCache.Reset();
	CompositeCache.Reset();
	FontRenderer.Reset();
	FontAtlasCache.Reset();
	FontAtlasTint.Reset();
	DidMaps.Reset();
	EnumStrings.Reset(); BaseEnumMaps.Reset();
	ItemCompositeCache.Reset();
	if (Dat && Dat->GetTextureResolver())
	{
		FontRenderer = MakeUnique<FACEDatFontRenderer>(Dat->GetTextureResolver());
	}
}

void UACEUIResourceResolver::Shutdown()
{
	SpellIconCache.Reset();
	PaperDollClickPixels.Reset();
	PaperDollClickSize = FIntPoint::ZeroValue;
	DidMaps.Reset();
	EnumStrings.Reset(); BaseEnumMaps.Reset();
	ItemCompositeCache.Reset();
	FontAtlasCache.Reset();
	FontAtlasTint.Reset();
	Cache.Reset();
	IconCache.Reset();
	CompositeCache.Reset();
	FontRenderer.Reset();
	Dat = nullptr;
}

FString UACEUIResourceResolver::GetSuggestedAssetName(uint32 ResourceId, FName Kind) const
{
	const FString Id = FString::Printf(TEXT("%08X"), ResourceId);
	if (Kind == TEXT("Cursor"))
	{
		return FString::Printf(TEXT("AC_UI_Cursor_%s"), *Id);
	}
	if (Kind == TEXT("Font"))
	{
		return FString::Printf(TEXT("AC_UI_Font_%s"), *Id);
	}
	if (Kind == TEXT("Icon") || (ResourceId & 0xFF000000u) == 0x06000000u)
	{
		return FString::Printf(TEXT("AC_UI_Icon_%s"), *Id);
	}
	return FString::Printf(TEXT("AC_UI_Texture_%s"), *Id);
}

UTexture2D* UACEUIResourceResolver::ResolveTexture(uint32 ResourceId, uint32 AlphaResourceId)
{
	if (ResourceId == 0)
	{
		return nullptr;
	}
	if (AlphaResourceId != 0)
	{
		const uint64 Key = (static_cast<uint64>(ResourceId) << 32) | static_cast<uint64>(AlphaResourceId)
			| (0x3ull << 56);
		if (TObjectPtr<UTexture2D>* Found = CompositeCache.Find(Key))
		{
			return Found->Get();
		}
		if (!Dat || !Dat->IsDatReady())
		{
			return nullptr;
		}
		FACEDatTextureResolver* Resolver = Dat->GetTextureResolver();
		if (!Resolver)
		{
			return nullptr;
		}
		if (UTexture2D* Tex = Resolver->GetOrCreateUiTextureWithAlpha(ResourceId, AlphaResourceId))
		{
			CompositeCache.Add(Key, Tex);
			return Tex;
		}
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* Found = Cache.Find(ResourceId))
	{
		return Found->Get();
	}
	if (!Dat || !Dat->IsDatReady())
	{
		return nullptr;
	}
	FACEDatTextureResolver* Resolver = Dat->GetTextureResolver();
	if (!Resolver)
	{
		return nullptr;
	}
	if (UTexture2D* Tex = Resolver->GetOrCreateUiTexture(ResourceId))
	{
		Cache.Add(ResourceId, Tex);
		return Tex;
	}
	return nullptr;
}

UTexture2D* UACEUIResourceResolver::ResolveFloatingHealthTexture(uint32 ResourceId)
{
	// These are the retail toolbar's empty/full heart-vial layers. Keep the
	// original textures intact for desktop and the central inventory toolbar.
	if (ResourceId!=0x0600193Eu && ResourceId!=0x0600193Fu) return ResolveTexture(ResourceId);
	const FString Key=FString::Printf(TEXT("floating_vial_%08X"),ResourceId);
	if (auto* Found=ItemCompositeCache.Find(Key)) return Found->Get();
	if (!Dat || !Dat->GetTextureResolver()) return nullptr;
	auto* Resolver=Dat->GetTextureResolver();
	FACEDatTexture Raw; FACEDatDecodedSurface Image;
	if (!Resolver->LoadTextureForUi(ResourceId,Raw) || !Resolver->DecodeTextureForUi(Raw,Image)) return nullptr;
	// The bottom four rows are the toolbar separator, not part of the vial.
	// Flood only the near-black exterior so the dark glass/interior outline is
	// preserved instead of chroma-keying every dark pixel in the health fill.
	TArray<int32> Pending; TBitArray<> Seen(false,Image.Pixels.Num());
	auto Visit=[&](int32 I) {
		if (I<0 || I>=Image.Pixels.Num() || Seen[I]) return;
		Seen[I]=true; const FColor P=Image.Pixels[I];
		if (FMath::Max3(P.R,P.G,P.B)<=16 || P.A==0) Pending.Add(I);
	};
	for (int32 Y=0;Y<Image.Height;++Y) { Visit(Y*Image.Width); Visit((Y+1)*Image.Width-1); }
	for (int32 X=0;X<Image.Width;++X) { Visit(X); Visit((Image.Height-4)*Image.Width+X); }
	for (int32 N=0;N<Pending.Num();++N)
	{
		const int32 I=Pending[N]; Image.Pixels[I]=FColor(0,0,0,0);
		if (I%Image.Width) Visit(I-1); if (I%Image.Width<Image.Width-1) Visit(I+1);
		Visit(I-Image.Width); Visit(I+Image.Width);
	}
	for (int32 I=FMath::Max(0,Image.Height-4)*Image.Width;I<Image.Pixels.Num();++I) Image.Pixels[I]=FColor(0,0,0,0);
	auto* Texture=FACEDatTextureResolver::CreateTransientRgbaUi(Image.Width,Image.Height,Image.Pixels);
	ItemCompositeCache.Add(Key,Texture); return Texture;
}

UTexture2D* UACEUIResourceResolver::ResolveIconTexture(uint32 ResourceId)
{
	if (ResourceId == 0)
	{
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* Found = IconCache.Find(ResourceId))
	{
		return Found->Get();
	}
	if (!Dat || !Dat->IsDatReady())
	{
		return nullptr;
	}
	FACEDatTextureResolver* Resolver = Dat->GetTextureResolver();
	if (!Resolver)
	{
		return nullptr;
	}
	if (UTexture2D* Tex = Resolver->GetOrCreateUiIconTexture(ResourceId))
	{
		IconCache.Add(ResourceId, Tex);
		return Tex;
	}
	return nullptr;
}

bool UACEUIResourceResolver::ResolveFont(uint32 FontId, FACEDatFont& OutFont)
{
	if (!FontRenderer && Dat && Dat->GetTextureResolver())
		FontRenderer = MakeUnique<FACEDatFontRenderer>(Dat->GetTextureResolver());
	return FontRenderer && FontRenderer->LoadFont(FontId, OutFont);
}

uint32 UACEUIResourceResolver::ResolveMappedDid(uint32 MapperId, uint32 Key, uint32 FallbackKey)
{
	if (!Dat || !Dat->GetTextureResolver()) return 0;
	if (!DidMaps.Contains(MapperId))
	{
		TArray<uint8> Blob;
		if (!Dat->GetTextureResolver()->GetPortalDat()->ReadFile(MapperId,Blob)) return 0;
		FACEDatCursor Cursor(Blob);
		uint32 Id = 0; uint8 Numbering = 0; bool bOk = true;
		if (!Cursor.Read(Id) || !Cursor.Read(Numbering)) return 0;
		const uint32 Count = Cursor.ReadCompressedUInt32(bOk);
		if (!bOk || Count > static_cast<uint32>(Cursor.Remaining()/8)) return 0;
		TMap<uint32,uint32> Map;
		for (uint32 I=0; I<Count; ++I)
		{
			uint32 Enum = 0, Did = 0;
			if (!Cursor.Read(Enum) || !Cursor.Read(Did)) return 0;
			Map.Add(Enum,Did);
		}
		DidMaps.Add(MapperId,MoveTemp(Map));
	}
	const auto& Map = DidMaps[MapperId];
	if (const uint32* Did = Map.Find(Key); Did && *Did) return *Did;
	return Map.FindRef(FallbackKey);
}

uint32 UACEUIResourceResolver::ResolveItemBackgroundId(uint32 ItemType)
{
	// IconData::RenderIcons: lowest set ItemType bit + 1, default enum 33.
	return ResolveMappedDid(0x25000008,ItemType ? FMath::CountTrailingZeros(ItemType)+1 : 33,33);
}

uint32 UACEUIResourceResolver::ResolvePaperDollAnimation(int32 Heritage)
{
	// gmPaperDollUI constructor and UpdateForHeritage: enum group 7.
	const uint32 Key = Heritage == 12 ? 0x10000011 : Heritage == 13 ? 0x10000013 : 0x10000005;
	return ResolveMappedDid(0x25000010, Key, 0x10000005);
}

uint32 UACEUIResourceResolver::ResolvePaperDollSelectionMask(FIntPoint Point)
{
	if (PaperDollClickPixels.IsEmpty() && Dat && Dat->GetTextureResolver())
	{
		const uint32 Id = ResolveMappedDid(0x25000010, 0x1000000C, 0x1000000C);
		FACEDatTexture Raw; FACEDatDecodedSurface Image;
		if (Id && Dat->GetTextureResolver()->LoadTextureForUi(Id, Raw)
			&& Dat->GetTextureResolver()->DecodeTextureForUi(Raw, Image))
		{
			PaperDollClickSize = FIntPoint(Image.Width, Image.Height);
			PaperDollClickPixels = MoveTemp(Image.Pixels);
		}
	}
	if (Point.X < 0 || Point.Y < 0 || Point.X >= PaperDollClickSize.X || Point.Y >= PaperDollClickSize.Y) return 0;
	const int32 Index = Point.Y * PaperDollClickSize.X + Point.X;
	if (!PaperDollClickPixels.IsValidIndex(Index)) return 0;
	const FColor C = PaperDollClickPixels[Index];
	// Retail RGBAColor_HitTest_* constants; both clothing and armor layers.
	const uint32 RGB = (uint32(C.R) << 16) | (uint32(C.G) << 8) | C.B;
	switch (RGB)
	{
	case 0x0000FF: return 1;
	case 0x00FF00: return 514;
	case 0xFF0000: return 1028;
	case 0x00FFFF: return 2056;
	case 0xFF00FF: return 4112;
	case 0xFFFF00: return 8256;
	case 0x000080: return 16512;
	case 0x008000: return 32;
	case 0x800000: return 256;
	default: return 0;
	}
}

FString UACEUIResourceResolver::ResolveEnumString(uint32 EnumId, uint32 Value)
{
	uint32 Did = ResolveMappedDid(0x25000001, EnumId, EnumId);
	TSet<uint32> Visited;
	while ((Did >> 24) == 0x22 && !Visited.Contains(Did))
	{
		Visited.Add(Did);
		if (!EnumStrings.Contains(Did))
		{
			TArray<uint8> Blob;
			if (!Dat->GetPortalDat()->ReadFile(Did, Blob)) return {};
			FACEDatCursor Cursor(Blob);
			uint32 Id = 0, Base = 0; uint8 Numbering = 0; bool bOk = true;
			if (!Cursor.Read(Id) || Id != Did || !Cursor.Read(Base) || !Cursor.Read(Numbering)) return {};
			const uint32 Count = Cursor.ReadCompressedUInt32(bOk);
			if (!bOk || Count > static_cast<uint32>(Cursor.Remaining() / 5)) return {};
			TMap<uint32, FString> Strings;
			for (uint32 I = 0; I < Count; ++I)
			{
				uint32 Key = 0;
				if (!Cursor.Read(Key)) return {};
				FString Name = Cursor.ReadPString(bOk, 1);
				if (!bOk) return {};
				Strings.Add(Key, MoveTemp(Name));
			}
			EnumStrings.Add(Did, MoveTemp(Strings));
			BaseEnumMaps.Add(Did, Base);
		}
		if (const FString* Name = EnumStrings[Did].Find(Value)) return *Name;
		Did = BaseEnumMaps.FindRef(Did);
	}
	return {};
}

namespace
{
	bool DecodeIcon(FACEDatTextureResolver* Resolver,uint32 Id,TArray<FColor>& Pixels)
	{
		FACEDatTexture Raw; FACEDatDecodedSurface Image;
		if (!Id || !Resolver->LoadTextureForUi(Id,Raw) || !Resolver->DecodeTextureForUi(Raw,Image)
			|| Image.Width != 32 || Image.Height != 32) return false;
		Pixels = MoveTemp(Image.Pixels); return Pixels.Num()==1024;
	}
	void OverIcon(TArray<FColor>& Base,const TArray<FColor>& Over)
	{
		for (int32 I=0; I<1024; ++I)
		{
			const uint32 A=Over[I].A, B=Base[I].A;
			const uint32 OutA=A+(B*(255-A)+127)/255;
			if (!OutA) { Base[I]=FColor(0,0,0,0); continue; }
			auto Blend=[&](uint32 D,uint32 S) -> uint8
			{ return (S*A+(D*B*(255-A)+127)/255+OutA/2)/OutA; };
			Base[I]=FColor(Blend(Base[I].R,Over[I].R),Blend(Base[I].G,Over[I].G),
				Blend(Base[I].B,Over[I].B),OutA);
		}
	}
}

UTexture2D* UACEUIResourceResolver::ResolveItemBackground(uint32 ItemType,uint32 UnderlayId)
{
	const uint32 BackgroundId=ResolveItemBackgroundId(ItemType);
	if (!UnderlayId) return ResolveIconTexture(BackgroundId);
	const FString Key=FString::Printf(TEXT("background_%08X_%08X"),BackgroundId,UnderlayId);
	if (auto* Found=ItemCompositeCache.Find(Key)) return Found->Get();
	if (!Dat || !Dat->GetTextureResolver()) return nullptr;
	TArray<FColor> Pixels, Underlay;
	auto* Resolver=Dat->GetTextureResolver();
	if (!DecodeIcon(Resolver,BackgroundId,Pixels)) return nullptr;
	if (DecodeIcon(Resolver,UnderlayId,Underlay)) OverIcon(Pixels,Underlay);
	auto* Texture=FACEDatTextureResolver::CreateTransientRgbaUi(32,32,Pixels);
	ItemCompositeCache.Add(Key,Texture); return Texture;
}

UTexture2D* UACEUIResourceResolver::ResolveItemForeground(uint32 IconId,uint32 OverlayId,uint32 Effects,bool bForSale)
{
	if (!IconId || !Dat || !Dat->GetTextureResolver()) return nullptr;
	const uint32 EffectId=ResolveMappedDid(0x25000009,Effects ? FMath::CountTrailingZeros(Effects)+1 : 33,33);
	const FString Key=FString::Printf(TEXT("icon_%08X_%08X_%08X_%d"),IconId,OverlayId,EffectId,bForSale);
	if (auto* Found=ItemCompositeCache.Find(Key)) return Found->Get();
	TArray<FColor> Pixels, Overlay, Effect;
	auto* Resolver=Dat->GetTextureResolver();
	if (!DecodeIcon(Resolver,IconId,Pixels)) return ResolveIconTexture(IconId);
	if (DecodeIcon(Resolver,OverlayId,Overlay)) OverIcon(Pixels,Overlay);
	// Retail replaces exact white icon-mask pixels with the authored effect image.
	// It does not tint the item-type background or the whole item.
	if (DecodeIcon(Resolver,EffectId,Effect))
		for (int32 I=0; I<1024; ++I) if (Pixels[I]==FColor::White) Pixels[I]=Effect[I];
	if (bForSale && DecodeIcon(Resolver,0x060012D9,Overlay)) OverIcon(Pixels,Overlay);
	auto* Texture=FACEDatTextureResolver::CreateTransientRgbaUi(32,32,Pixels);
	ItemCompositeCache.Add(Key,Texture); return Texture;
}

UTexture2D* UACEUIResourceResolver::ResolveSpellIcon(uint32 SpellId)
{
	if (auto* Found = SpellIconCache.Find(SpellId)) return Found->Get();
	if (!Dat || !Dat->GetTextureResolver()) return nullptr;
	FString Name; uint32 IconId = 0, Flags = 0, Target = 0;
	if (!Dat->TryGetSpellInfo(SpellId, Name, IconId)) return nullptr;
	Dat->TryGetSpellTargeting(SpellId, Flags, Target);
	const uint32 PowerMap = ResolveMappedDid(0x25000000, 0x10000006, 0x10000006);
	const uint32 EffectMap = ResolveMappedDid(0x25000000, 0x10000007, 0x10000007);
	const uint32 Background = ResolveMappedDid(PowerMap, Dat->GetSpellIconPowerLevel(SpellId), 33);
	const uint32 EffectId = ResolveMappedDid(EffectMap, (Flags & 0x10) ? 1 : 2, 33);
	TArray<FColor> Pixels, Icon, Effect, Overlay;
	auto* Resolver = Dat->GetTextureResolver();
	if (!DecodeIcon(Resolver, IconId, Icon)) return ResolveIconTexture(IconId);
	if (!DecodeIcon(Resolver, Background, Pixels)) Pixels.Init(FColor::Transparent, 1024);
	OverIcon(Pixels, Icon);
	// ClientMagicSystem::CompositeSpellIcon replaces white mask pixels only,
	// preserving actual spell colors and the power-level background.
	if (DecodeIcon(Resolver, EffectId, Effect))
		for (int32 I = 0; I < 1024; ++I) if (Pixels[I] == FColor::White) Pixels[I] = Effect[I];
	const uint32 TargetOverlay = (Flags & 0x2000) ? 4 : (Flags & 8) ? 3 : 0;
	if (TargetOverlay && DecodeIcon(Resolver, ResolveMappedDid(EffectMap, TargetOverlay, 33), Overlay)) OverIcon(Pixels, Overlay);
	auto* Texture = FACEDatTextureResolver::CreateTransientRgbaUi(32, 32, Pixels);
	SpellIconCache.Add(SpellId, Texture); return Texture;
}

UTexture2D* UACEUIResourceResolver::ResolveFontAtlas(uint32 TextureId, bool& bOutTint)
{
	if (const TObjectPtr<UTexture2D>* Cached = FontAtlasCache.Find(TextureId))
	{
		bOutTint = FontAtlasTint.FindRef(TextureId);
		return Cached->Get();
	}
	if (!FontRenderer || TextureId == 0) return nullptr;
	UTexture2D* Texture = FontRenderer->CreateGlyphAtlas(TextureId, bOutTint);
	if (Texture)
	{
		FontAtlasCache.Add(TextureId, Texture);
		FontAtlasTint.Add(TextureId, bOutTint);
	}
	return Texture;
}

UTexture2D* UACEUIResourceResolver::RenderDatText(uint32 FontId, const FString& Text, FColor Color,
	bool bOutline, FColor OutlineColor, int32* OutWidth, int32* OutHeight, int32 BoxW, int32 BoxH)
{
	if (!FontRenderer && Dat && Dat->GetTextureResolver())
	{
		FontRenderer = MakeUnique<FACEDatFontRenderer>(Dat->GetTextureResolver());
	}
	if (!FontRenderer || Text.IsEmpty())
	{
		return nullptr;
	}
	return FontRenderer->RenderText(FontId, Text, Color, bOutline, OutlineColor, OutWidth, OutHeight, BoxW, BoxH);
}

bool UACEUIResourceResolver::TryGetCursor(uint32 CursorImageId, FCursorDesc& Out) const
{
	Out.ImageId = CursorImageId;
	Out.HotX = 0;
	Out.HotY = 0;
	// Hotspots are in UIAssetManifest.json cursor entries — load when cursor table is imported.
	return CursorImageId != 0;
}
