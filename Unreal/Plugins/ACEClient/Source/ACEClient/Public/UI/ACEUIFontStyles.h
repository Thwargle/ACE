#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "UI/ACEUIElement.h"

/**
 * Retail UI fonts live in layout 0x2100003F. Text fields point at a style there via
 * BaseLayout=0x2100003F + BaseElement=<style ElementId>. The JSON dump of that layout
 * only carries the style name (basefont16, sansfont20, …) — the portal.dat Font DID is
 * looked up from the name for DAT-rendered text, and a Slate size is derived for UTextBlock
 * overlays that still use the engine typeface.
 */
namespace ACEUIFontStyles
{
	constexpr uint32 FontCatalogLayout = 0x2100003Fu;

	struct FACEFontStyle
	{
		/** Portal.dat Font DID (0x4000000x); 0 when unknown. */
		uint32 FontId = 0;
		/** Approximate Slate point size that matches the bitmap glyph height at 800×600. */
		int32 SlateSize = 9;
		/** "Regular" or "Bold". */
		FName Typeface = FName(TEXT("Regular"));
		bool bValid = false;
	};

	/** Parse a trailing integer from names like "basefont16" / "sansfont20" / "fancyfont40". */
	inline int32 ParseTrailingSize(const FString& Name)
	{
		int32 Digits = 0;
		int32 Multiplier = 1;
		for (int32 i = Name.Len() - 1; i >= 0; --i)
		{
			const TCHAR C = Name[i];
			if (C < TEXT('0') || C > TEXT('9'))
			{
				break;
			}
			Digits += (C - TEXT('0')) * Multiplier;
			Multiplier *= 10;
		}
		return Digits;
	}

	/**
	 * Map a DAT bitmap height onto the Slate size that currently looks closest in our
	 * overlays (basefont16 ≈ Slate 9). Clamped to a readable range.
	 */
	inline int32 BitmapSizeToSlate(int32 BitmapPx)
	{
		if (BitmapPx <= 0)
		{
			return 9;
		}
		return FMath::Clamp(FMath::RoundToInt(static_cast<float>(BitmapPx) * 9.f / 16.f), 7, 22);
	}

	/** Font DIDs from the retail font catalog (0x2100003F). */
	inline uint32 FontIdForStyleName(const FString& NameLower)
	{
		if (NameLower == TEXT("tabfont") || NameLower == TEXT("fieldvaluemedium")) return 0x40000000u;
		if (NameLower == TEXT("fieldvaluelarge")) return 0x40000001u;
		if (NameLower == TEXT("fieldvaluesmall")) return 0x40000002u;
		const int32 Px = ParseTrailingSize(NameLower);
		if (NameLower.StartsWith(TEXT("fancyfont")))
		{
			const int32 Sizes[] = {24,30,36,38,40,46,48};
			for (int32 I=0; I<UE_ARRAY_COUNT(Sizes); ++I) if (Px==Sizes[I]) return 0x4000000Eu+I;
		}
		if (NameLower.StartsWith(TEXT("sansfont")) && Px>=12 && Px<=24 && Px%2==0)
			return 0x40000007u+(Px-12)/2;
		if (NameLower.StartsWith(TEXT("basefont")))
		{
			switch(Px) {
			case 12: return 0x40000025u; case 14: return 0x40000002u;
			case 16: return 0x40000000u; case 18: return 0x40000001u;
			case 20: return 0x40000004u; case 22: return 0x40000005u; case 24: return 0x40000006u;
			}
		}
		return 0;
	}

	/** Resolve a style from the catalog ElementId used as BaseElement on text fields. */
	inline FACEFontStyle FromBaseElementId(uint32 BaseElement)
	{
		FACEFontStyle Out;
		struct FKnown
		{
			uint32 Id;
			const TCHAR* Name;
			int32 BitmapPx;
		};
		// Mirrors Docs/UI/Resolved/0x2100003F.json roots.
		static const FKnown Known[] = {
			{ 0x10000370u, TEXT("tabfont"), 16 },
			{ 0x10000371u, TEXT("fieldvaluelarge"), 18 },
			{ 0x10000372u, TEXT("fieldvaluemedium"), 16 },
			{ 0x10000373u, TEXT("fieldvaluesmall"), 14 },
			{ 0x10000374u, TEXT("basefont12"), 11 },
			{ 0x10000375u, TEXT("basefont14"), 14 },
			{ 0x10000376u, TEXT("basefont16"), 16 },
			{ 0x10000377u, TEXT("basefont18"), 18 },
			{ 0x10000378u, TEXT("basefont20"), 20 },
			{ 0x10000379u, TEXT("basefont22"), 22 },
			{ 0x1000037Au, TEXT("basefont24"), 24 },
			{ 0x1000037Bu, TEXT("sansfont12"), 12 },
			{ 0x1000037Cu, TEXT("sansfont14"), 14 },
			{ 0x1000037Du, TEXT("sansfont16"), 16 },
			{ 0x1000037Eu, TEXT("sansfont18"), 18 },
			{ 0x1000037Fu, TEXT("sansfont20"), 20 },
			{ 0x10000380u, TEXT("sansfont22"), 22 },
			{ 0x10000381u, TEXT("sansfont24"), 24 },
			{ 0x10000382u, TEXT("fancyfont24"), 24 },
			{ 0x10000383u, TEXT("fancyfont30"), 30 },
			{ 0x10000384u, TEXT("fancyfont36"), 36 },
			{ 0x10000385u, TEXT("fancyfont38"), 38 },
			{ 0x10000386u, TEXT("fancyfont40"), 40 },
			{ 0x10000387u, TEXT("fancyfont46"), 46 },
			{ 0x10000388u, TEXT("fancyfont48"), 48 },
		};
		for (const FKnown& K : Known)
		{
			if (K.Id != BaseElement)
			{
				continue;
			}
			Out.bValid = true;
			Out.SlateSize = BitmapSizeToSlate(K.BitmapPx);
			Out.FontId = FontIdForStyleName(FString(K.Name).ToLower());
			Out.Typeface = FName(TEXT("Regular"));
			return Out;
		}
		return Out;
	}

	/** Prefer the element's BaseElement style; fall back to the caller's Slate size. */
	inline FACEFontStyle ResolveForElement(const TSharedPtr<FACEUIElement>& Element, int32 FallbackSlateSize)
	{
		FACEFontStyle Out;
		if (Element.IsValid() && Element->BaseLayout == FontCatalogLayout && Element->BaseElement != 0)
		{
			Out = FromBaseElementId(Element->BaseElement);
		}
		if (Element.IsValid() && Element->FontId != 0)
		{
			Out.FontId = Element->FontId;
			Out.SlateSize = Element->FontHeight > 0 ? BitmapSizeToSlate(Element->FontHeight) : FallbackSlateSize;
			Out.bValid = true;
		}
		if (!Out.bValid)
		{
			Out.bValid = true;
			Out.SlateSize = FallbackSlateSize > 0 ? FallbackSlateSize : 9;
			Out.Typeface = FName(TEXT("Regular"));
		}
		return Out;
	}

	inline FSlateFontInfo MakeSlateFont(const FACEFontStyle& Style)
	{
		return FCoreStyle::GetDefaultFontStyle(Style.Typeface, Style.SlateSize);
	}
}
