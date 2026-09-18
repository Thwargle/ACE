#include "Protocol/ACEBinaryWriter.h"
#include "Protocol/ACEWindows1252.h"

void FACEBinaryWriter::Reset()
{
	Buffer.Reset();
}

void FACEBinaryWriter::WriteUInt8(uint8 Value)
{
	Buffer.Add(Value);
}

void FACEBinaryWriter::WriteUInt16(uint16 Value)
{
	Buffer.Add(static_cast<uint8>(Value & 0xFF));
	Buffer.Add(static_cast<uint8>((Value >> 8) & 0xFF));
}

void FACEBinaryWriter::WriteUInt32(uint32 Value)
{
	Buffer.Add(static_cast<uint8>(Value & 0xFF));
	Buffer.Add(static_cast<uint8>((Value >> 8) & 0xFF));
	Buffer.Add(static_cast<uint8>((Value >> 16) & 0xFF));
	Buffer.Add(static_cast<uint8>((Value >> 24) & 0xFF));
}

void FACEBinaryWriter::WriteUInt64(uint64 Value)
{
	WriteUInt32(static_cast<uint32>(Value & 0xFFFFFFFFu));
	WriteUInt32(static_cast<uint32>((Value >> 32) & 0xFFFFFFFFu));
}

void FACEBinaryWriter::WriteInt32(int32 Value)
{
	WriteUInt32(static_cast<uint32>(Value));
}

void FACEBinaryWriter::WriteFloat(float Value)
{
	uint32 Bits;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	WriteUInt32(Bits);
}

void FACEBinaryWriter::WriteDouble(double Value)
{
	uint64 Bits;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	WriteUInt64(Bits);
}

void FACEBinaryWriter::WriteBytes(const uint8* Data, int32 Length)
{
	if (Data && Length > 0)
	{
		Buffer.Append(Data, Length);
	}
}

void FACEBinaryWriter::WriteBytes(const TArray<uint8>& Data)
{
	Buffer.Append(Data);
}

uint32 FACEBinaryWriter::PadMultiple(uint32 Length, uint32 Multiple)
{
	return Multiple * ((Length + Multiple - 1u) / Multiple) - Length;
}

void FACEBinaryWriter::Pad(uint32 Count)
{
	for (uint32 i = 0; i < Count; ++i)
	{
		Buffer.Add(0);
	}
}

void FACEBinaryWriter::Align()
{
	Pad(PadMultiple(static_cast<uint32>(Buffer.Num()), 4u));
}

void FACEBinaryWriter::WriteString16L(const FString& Value)
{
	const int32 Len = FMath::Min(Value.Len(), int32(MAX_uint16));

	WriteUInt16(static_cast<uint16>(Len));
	for (int32 i = 0; i < Len; ++i)
	{
		uint8 Byte;
		ACEWindows1252::Encode(Value[i], Byte);
		Buffer.Add(Byte);
	}
	Pad(PadMultiple(sizeof(uint16) + static_cast<uint32>(Len), 4u));
}

void FACEBinaryWriter::WritePackedUnicode(const FString& Value)
{
	const int32 Len = Value.Len();
	if (Len < 128)
	{
		WriteUInt8(static_cast<uint8>(Len));
	}
	else
	{
		WriteUInt8(static_cast<uint8>(0x80 | ((Len >> 8) & 0x7F)));
		WriteUInt8(static_cast<uint8>(Len & 0xFF));
	}
	for (int32 i = 0; i < Len; ++i)
	{
		const uint16 C = static_cast<uint16>(Value[i]);
		WriteUInt8(static_cast<uint8>(C & 0xFF));
		WriteUInt8(static_cast<uint8>(C >> 8));
	}
}

void FACEBinaryWriter::WriteUInt32At(int32 ByteOffset, uint32 Value)
{
	if (ByteOffset < 0 || ByteOffset + 4 > Buffer.Num())
	{
		return;
	}
	Buffer[ByteOffset] = static_cast<uint8>(Value);
	Buffer[ByteOffset + 1] = static_cast<uint8>(Value >> 8);
	Buffer[ByteOffset + 2] = static_cast<uint8>(Value >> 16);
	Buffer[ByteOffset + 3] = static_cast<uint8>(Value >> 24);
}

void FACEBinaryWriter::WriteString32L(const FString& Value)
{
	FTCHARToUTF8 Converter(*Value);
	const char* Chars = Converter.Get();
	const int32 Len = Converter.Length();

	if (Len == 0)
	{
		WriteUInt32(0);
		return;
	}

	if (Len <= 255)
	{
		WriteUInt32(static_cast<uint32>(Len + 1));
		WriteUInt8(static_cast<uint8>(Len));
	}
	else
	{
		WriteUInt32(static_cast<uint32>(Len + 2));
		WriteUInt8(0xFF);
		WriteUInt8(static_cast<uint8>(Len & 0xFF));
	}

	for (int32 i = 0; i < Len; ++i)
	{
		Buffer.Add(static_cast<uint8>(Chars[i]));
	}

	// Padding unused at end of login packet, but keep stream tidy.
	Pad(PadMultiple(sizeof(uint32) + static_cast<uint32>(Len), 4u));
}
