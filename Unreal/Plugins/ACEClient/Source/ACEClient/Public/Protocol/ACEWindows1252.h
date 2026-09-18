#pragma once

#include "CoreMinimal.h"

// AC String16L is a byte-counted Windows-1252 string, independent of the host locale.
namespace ACEWindows1252
{
inline constexpr uint16 Extended[32] = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
};

inline TCHAR Decode(uint8 Byte)
{
    return static_cast<TCHAR>(Byte >= 0x80 && Byte < 0xa0 ? Extended[Byte - 0x80] : Byte);
}

inline bool Encode(TCHAR Character, uint8& Byte)
{
    if (Character < 0x80 || (Character >= 0xa0 && Character <= 0xff))
    {
        Byte = static_cast<uint8>(Character);
        return true;
    }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Extended); ++Index)
    {
        if (Character == Extended[Index])
        {
            Byte = 0x80 + Index;
            return true;
        }
    }
    Byte = '?';
    return false;
}

inline bool IsPrintable(TCHAR Character)
{
    uint8 Byte;
    return Character >= 32 && !(Character >= 0x7f && Character < 0xa0) && Encode(Character, Byte);
}
}
