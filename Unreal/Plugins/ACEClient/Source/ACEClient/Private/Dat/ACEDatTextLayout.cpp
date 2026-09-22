#include "Dat/ACEDatTextLayout.h"

namespace
{
	bool White(uint16 C) { return C == 9 || C == 13 || C == 32; }
	bool EastAsian(uint16 C)
	{
		return (C >= 0x1100 && C <= 0x11FF) || (C >= 0x3000 && C <= 0xD7AF)
			|| (C >= 0xF900 && C <= 0xFAFF) || (C >= 0xFF00 && C <= 0xFFDC);
	}
	bool NonBeginning(uint16 C)
	{
		switch (C)
		{
		case 33: case 41: case 44: case 46: case 63: case 12289: case 12290: case 12540:
		case 0xFF01: case 0xFF09: case 0xFF1F: case 0xFF70: case 0xFF9E: case 0xFF9F: return true;
		default: return false;
		}
	}
	bool CanBreak(uint16 Prev, uint16 Cur)
	{
		return Prev == 10 || ((White(Prev) || EastAsian(Prev) || EastAsian(Cur))
			&& !NonBeginning(Cur) && Prev != 40 && Prev != 0xFF08);
	}
}

const FACEDatFontChar* ACEDatText::FindChar(const FACEDatFont& Font, uint16 Code)
{
	// Layout controls never become the missing-glyph question mark. CRLF is
	// one line break; tabs have a space advance, and invisible marks have none.
	if (Code == 0 || Code == 10 || Code == 13 || Code == 0x200B || Code == 0xFEFF) return nullptr;
	if (Code == 9 || Code == 0x00A0) Code = 32;
	const int32* Index = Font.IndexByUnicode.Find(Code);
	if (!Index) Index = Font.IndexByUnicode.Find(63);
	return Index && Font.Chars.IsValidIndex(*Index) ? &Font.Chars[*Index] : nullptr;
}

int32 ACEDatText::Advance(const FACEDatFont& Font, uint16 Code)
{
	const FACEDatFontChar* Ch = FindChar(Font, Code);
	// Font::GetCharWidthA returns a byte; Glyph::SetData stores that byte.
	return Ch ? static_cast<uint8>(Ch->HorizontalOffsetBefore + Ch->Width + Ch->HorizontalOffsetAfter) : 0;
}

FString ACEDatText::Ellipsize(const FACEDatFont& Font, const FString& Text, int32 Width)
{
	int32 FullWidth=0;
	for (TCHAR C:Text) FullWidth+=Advance(Font,C);
	if (FullWidth<=Width) return Text;
	const int32 Trailer=3*Advance(Font,'.');
	if (Width<Trailer) return {};
	int32 Used=0,End=0;
	while (End<Text.Len() && Used+Advance(Font,Text[End])+Trailer<=Width) Used+=Advance(Font,Text[End++]);
	return Text.Left(End)+TEXT("...");
}

TArray<FACEBitmapTextLine> ACEDatText::Layout(const FACEDatFont& Font,
	const FString& Text, int32 MarginWidth, bool bOneLine)
{
	TArray<FACEBitmapTextLine> Lines;
	if (Text.IsEmpty()) return Lines;
	int32 Start = 0, Width = 0, Accumulated = 0, LastBreak = 0;
	bool bWrappable = false;
	for (int32 I = 0; I < Text.Len(); ++I)
	{
		const uint16 C = Text[I];
		const uint16 Prev = I > 0 ? Text[I - 1] : 0;
		const int32 CharWidth = Advance(Font, C);
		bool bMustWrap = false;
		if (!bOneLine)
		{
			if (I > 0 && CanBreak(Prev, C))
			{
				LastBreak = I;
				Accumulated = 0;
				bWrappable = true;
				bMustWrap = Prev == 10;
			}
			else if (I > Start && !White(C) && C != 10 && Accumulated + CharWidth > MarginWidth)
			{
				LastBreak = I;
				Accumulated = 0;
				bWrappable = true;
			}
		}
		Width += CharWidth;
		Accumulated += CharWidth;
		const int32 VisibleWidth = Width - ((White(C) || C == 10) ? CharWidth : 0);
		if (bMustWrap || (bWrappable && VisibleWidth > MarginWidth))
		{
			int32 LineWidth = Width - Accumulated;
			const uint16 EndChar = Text[LastBreak - 1];
			if (White(EndChar) || EndChar == 10) LineWidth -= Advance(Font, EndChar);
			Lines.Add({Start, LastBreak, LineWidth});
			Start = LastBreak;
			Width = Accumulated;
			bWrappable = false;
		}
	}
	Lines.Add({Start, Text.Len(), Width});
	if (!bOneLine && Text[Text.Len() - 1] == '\n') Lines.Add({Text.Len(), Text.Len(), 0});
	return Lines;
}
