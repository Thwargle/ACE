#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEWindows1252.h"

FACEBinaryReader::FACEBinaryReader(const uint8* InData, int32 InLength)
{
	Reset(InData, InLength);
}

FACEBinaryReader::FACEBinaryReader(const TArray<uint8>& InData)
{
	Reset(InData.GetData(), InData.Num());
}

void FACEBinaryReader::Reset(const uint8* InData, int32 InLength)
{
	Data = InData;
	Length = InLength;
	Position = 0;
}

void FACEBinaryReader::Seek(int32 NewPosition)
{
	Position = FMath::Clamp(NewPosition, 0, Length);
}

void FACEBinaryReader::Skip(int32 Bytes)
{
	Seek(Position + Bytes);
}

uint32 FACEBinaryReader::PadMultiple(uint32 Len, uint32 Multiple)
{
	return Multiple * ((Len + Multiple - 1u) / Multiple) - Len;
}

void FACEBinaryReader::Align()
{
	Skip(static_cast<int32>(PadMultiple(static_cast<uint32>(Position), 4u)));
}

uint8 FACEBinaryReader::ReadUInt8()
{
	check(CanRead(1));
	return Data[Position++];
}

uint16 FACEBinaryReader::ReadUInt16()
{
	check(CanRead(2));
	const uint16 Value = static_cast<uint16>(Data[Position]) | (static_cast<uint16>(Data[Position + 1]) << 8);
	Position += 2;
	return Value;
}

uint32 FACEBinaryReader::ReadUInt32()
{
	check(CanRead(4));
	const uint32 Value =
		static_cast<uint32>(Data[Position]) |
		(static_cast<uint32>(Data[Position + 1]) << 8) |
		(static_cast<uint32>(Data[Position + 2]) << 16) |
		(static_cast<uint32>(Data[Position + 3]) << 24);
	Position += 4;
	return Value;
}

uint64 FACEBinaryReader::ReadUInt64()
{
	const uint32 Lo = ReadUInt32();
	const uint32 Hi = ReadUInt32();
	return static_cast<uint64>(Lo) | (static_cast<uint64>(Hi) << 32);
}

int32 FACEBinaryReader::ReadInt32()
{
	return static_cast<int32>(ReadUInt32());
}

float FACEBinaryReader::ReadFloat()
{
	const uint32 Bits = ReadUInt32();
	float Value;
	FMemory::Memcpy(&Value, &Bits, sizeof(Value));
	return Value;
}

double FACEBinaryReader::ReadDouble()
{
	const uint64 Bits = ReadUInt64();
	double Value;
	FMemory::Memcpy(&Value, &Bits, sizeof(Value));
	return Value;
}

TArray<uint8> FACEBinaryReader::ReadBytes(int32 Count)
{
	TArray<uint8> Out;
	if (!CanRead(Count) || Count <= 0)
	{
		return Out;
	}
	Out.Append(Data + Position, Count);
	Position += Count;
	return Out;
}

FString FACEBinaryReader::ReadString16L()
{
	if (!CanRead(2))
	{
		return FString();
	}
	const uint16 StrLen = ReadUInt16();
	FString Result;
	if (StrLen > 0 && CanRead(StrLen))
	{
		Result.Reserve(StrLen);
		for (uint16 Index = 0; Index < StrLen; ++Index)
		{
			Result.AppendChar(ACEWindows1252::Decode(Data[Position++]));
		}
	}
	Skip(static_cast<int32>(PadMultiple(sizeof(uint16) + StrLen, 4u)));
	return Result;
}

FString FACEBinaryReader::ReadPackedUnicode()
{
	if (!CanRead(1))
	{
		return FString();
	}
	int32 Len = ReadUInt8();
	if ((Len & 0x80) != 0)
	{
		if (!CanRead(1))
		{
			return FString();
		}
		const uint8 Low = ReadUInt8();
		Len = ((Len & 0x7F) << 8) | Low;
	}
	if (Len <= 0 || !CanRead(Len * 2))
	{
		return FString();
	}
	FString Result;
	Result.Reserve(Len);
	for (int32 i = 0; i < Len; ++i)
	{
		const uint8 Lo = ReadUInt8();
		const uint8 Hi = ReadUInt8();
		Result.AppendChar(static_cast<TCHAR>(Lo | (static_cast<uint16>(Hi) << 8)));
	}
	return Result;
}

FACEPosition FACEBinaryReader::ReadPosition()
{
	FACEPosition Pos;
	Pos.CellId = static_cast<int32>(ReadUInt32());
	Pos.Location.X = ReadFloat();
	Pos.Location.Y = ReadFloat();
	Pos.Location.Z = ReadFloat();
	Pos.RotationW = ReadFloat();
	Pos.RotationXYZ.X = ReadFloat();
	Pos.RotationXYZ.Y = ReadFloat();
	Pos.RotationXYZ.Z = ReadFloat();
	Pos.NormalizeRotation();
	return Pos;
}
