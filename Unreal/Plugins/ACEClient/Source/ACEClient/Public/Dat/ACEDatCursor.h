#pragma once

#include "CoreMinimal.h"

/** Minimal binary cursor over a contiguous DAT file blob (ACE.DatLoader-compatible). */
struct FACEDatCursor
{
	const uint8* Data = nullptr;
	int32 Size = 0;
	int32 Pos = 0;

	FACEDatCursor() = default;
	FACEDatCursor(const TArray<uint8>& Buffer)
		: Data(Buffer.GetData()), Size(Buffer.Num()), Pos(0)
	{
	}

	FACEDatCursor(const uint8* InData, int32 InSize)
		: Data(InData), Size(InSize), Pos(0)
	{
	}

	bool IsValid() const { return Data != nullptr && Size > 0; }
	bool CanRead(int32 NumBytes) const { return Pos >= 0 && NumBytes >= 0 && (Pos + NumBytes) <= Size; }
	int32 Remaining() const { return FMath::Max(0, Size - Pos); }

	bool Seek(int32 Absolute)
	{
		if (Absolute < 0 || Absolute > Size)
		{
			return false;
		}
		Pos = Absolute;
		return true;
	}

	template <typename T>
	bool Read(T& Out)
	{
		if (!CanRead(static_cast<int32>(sizeof(T))))
		{
			return false;
		}
		FMemory::Memcpy(&Out, Data + Pos, sizeof(T));
		Pos += static_cast<int32>(sizeof(T));
		return true;
	}

	bool ReadBytes(void* Dest, int32 Num)
	{
		if (!CanRead(Num))
		{
			return false;
		}
		FMemory::Memcpy(Dest, Data + Pos, Num);
		Pos += Num;
		return true;
	}

	bool Skip(int32 Num)
	{
		if (!CanRead(Num))
		{
			return false;
		}
		Pos += Num;
		return true;
	}

	uint8 ReadU8(bool& bOk)
	{
		uint8 V = 0;
		bOk = Read(V);
		return V;
	}

	uint16 ReadU16(bool& bOk)
	{
		uint16 V = 0;
		bOk = Read(V);
		return V;
	}

	int16 ReadI16(bool& bOk)
	{
		int16 V = 0;
		bOk = Read(V);
		return V;
	}

	uint32 ReadU32(bool& bOk)
	{
		uint32 V = 0;
		bOk = Read(V);
		return V;
	}

	int32 ReadI32(bool& bOk)
	{
		int32 V = 0;
		bOk = Read(V);
		return V;
	}

	float ReadF32(bool& bOk)
	{
		float V = 0.f;
		bOk = Read(V);
		return V;
	}

	FVector3f ReadVector3(bool& bOk)
	{
		FVector3f V;
		bOk = Read(V.X) && Read(V.Y) && Read(V.Z);
		return V;
	}

	/** ACE Frame: origin + quat as W,X,Y,Z on disk. */
	bool ReadFrame(FVector3f& OutOrigin, FQuat4f& OutQuat)
	{
		bool bOk = true;
		OutOrigin = ReadVector3(bOk);
		if (!bOk)
		{
			return false;
		}
		const float W = ReadF32(bOk);
		const float X = ReadF32(bOk);
		const float Y = ReadF32(bOk);
		const float Z = ReadF32(bOk);
		if (!bOk)
		{
			return false;
		}
		OutQuat = FQuat4f(X, Y, Z, W);
		return true;
	}

	/**
	 * Packed count used by SmartArrays (GfxObj surfaces / polys).
	 * 1 byte if MSB clear; 2 bytes if 0x80 set and 0x40 clear; else 4 bytes.
	 */
	uint32 ReadCompressedUInt32(bool& bOk)
	{
		const uint8 B0 = ReadU8(bOk);
		if (!bOk)
		{
			return 0;
		}
		if ((B0 & 0x80) == 0)
		{
			return B0;
		}
		const uint8 B1 = ReadU8(bOk);
		if (!bOk)
		{
			return 0;
		}
		if ((B0 & 0x40) == 0)
		{
			return static_cast<uint32>(((B0 & 0x7F) << 8) | B1);
		}
		const uint16 S = ReadU16(bOk);
		if (!bOk)
		{
			return 0;
		}
		return static_cast<uint32>(((((B0 & 0x3F) << 8) | B1) << 16) | S);
	}

	/** Known-type DataID encoding used by AnimationPartChange. */
	uint32 ReadAsDataIdOfKnownType(uint32 KnownType, bool& bOk)
	{
		uint16 Value = ReadU16(bOk);
		if (!bOk)
		{
			return 0;
		}
		if ((Value & 0x8000) != 0)
		{
			const uint16 Lower = ReadU16(bOk);
			if (!bOk)
			{
				return 0;
			}
			const uint32 Higher = static_cast<uint32>(Value & 0x3FFF) << 16;
			return KnownType + (Higher | Lower);
		}
		return KnownType + Value;
	}

	/** Four ASCII bytes as type tag, reversed like ACE.DatLoader string.Reverse(). */
	bool PeekFourCCReversed(ANSICHAR Out[5])
	{
		if (!CanRead(4))
		{
			return false;
		}
		Out[0] = static_cast<ANSICHAR>(Data[Pos + 3]);
		Out[1] = static_cast<ANSICHAR>(Data[Pos + 2]);
		Out[2] = static_cast<ANSICHAR>(Data[Pos + 1]);
		Out[3] = static_cast<ANSICHAR>(Data[Pos + 0]);
		Out[4] = 0;
		return true;
	}

	FString ReadFourCCReversed(bool& bOk)
	{
		uint8 Bytes[4];
		bOk = ReadBytes(Bytes, 4);
		if (!bOk)
		{
			return FString();
		}
		TCHAR Chars[5];
		Chars[0] = static_cast<TCHAR>(Bytes[3]);
		Chars[1] = static_cast<TCHAR>(Bytes[2]);
		Chars[2] = static_cast<TCHAR>(Bytes[1]);
		Chars[3] = static_cast<TCHAR>(Bytes[0]);
		Chars[4] = 0;
		return FString(Chars);
	}

	/** Align read position to the next 4-byte boundary (ACE AlignBoundary). */
	void AlignBoundary()
	{
		const int32 AlignDelta = Pos % 4;
		if (AlignDelta != 0)
		{
			Pos += (4 - AlignDelta);
			if (Pos > Size)
			{
				Pos = Size;
			}
		}
	}

	/** ACE obfuscated string: UInt16 length, nibble-swapped bytes (code page 1252). */
	FString ReadObfuscatedString(bool& bOk)
	{
		const int32 Len = static_cast<int32>(ReadU16(bOk));
		if (!bOk || Len < 0 || !CanRead(Len))
		{
			bOk = false;
			return FString();
		}
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Len);
		if (!ReadBytes(Bytes.GetData(), Len))
		{
			bOk = false;
			return FString();
		}
		for (int32 i = 0; i < Len; ++i)
		{
			Bytes[i] = static_cast<uint8>((Bytes[i] >> 4) | (Bytes[i] << 4));
		}
		Bytes.Add(0);
		// Windows-1252: high bytes map to Latin-1 for the AC spell name set we care about.
		FString Out;
		Out.Reserve(Len);
		for (int32 i = 0; i < Len; ++i)
		{
			Out.AppendChar(static_cast<TCHAR>(Bytes[i]));
		}
		return Out;
	}

	/** ACE PString: UInt16 length then bytes (Default sizeOfLength=2). */
	FString ReadPString(bool& bOk, uint32 SizeOfLength = 2)
	{
		int32 Len = 0;
		if (SizeOfLength == 1)
		{
			Len = ReadU8(bOk);
		}
		else
		{
			Len = ReadU16(bOk);
		}
		if (!bOk || Len < 0 || !CanRead(Len))
		{
			bOk = false;
			return FString();
		}
		TArray<ANSICHAR> Bytes;
		Bytes.SetNumUninitialized(Len + 1);
		ReadBytes(Bytes.GetData(), Len);
		Bytes[Len] = 0;
		return FString(ANSI_TO_TCHAR(Bytes.GetData()));
	}
};
