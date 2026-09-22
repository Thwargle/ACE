#pragma once

#include "CoreMinimal.h"
#include "Dat/ACEDatFileTypes.h"

/** Plain, single-font GlyphList layout in native DAT pixels. */
struct FACEBitmapTextLine
{
	int32 Begin = 0;
	int32 End = 0;
	int32 Width = 0;
};

namespace ACEDatText
{
	/** Font::GetCharDesc falls back to '?' when a code point is absent. */
	ACECLIENT_API const FACEDatFontChar* FindChar(const FACEDatFont& Font, uint16 Code);
	ACECLIENT_API int32 Advance(const FACEDatFont& Font, uint16 Code);
	ACECLIENT_API FString Ellipsize(const FACEDatFont& Font, const FString& Text, int32 Width);
	/** GlyphList::Recalculate / LinebreakUtils, with a uniform font height. */
	ACECLIENT_API TArray<FACEBitmapTextLine> Layout(const FACEDatFont& Font,
		const FString& Text, int32 MarginWidth, bool bOneLine);
}
