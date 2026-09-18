#pragma once

#include "ACEWorldBakeDatTools.h"

class ACEWORLDBAKEDATTOOLS_API FACEWorldBakeByteReader
{
public:
	FACEWorldBakeByteReader(TByteArrayPtr InData);

	FORCEINLINE void CheckPosition(uint32 Size) { check(Position + Size <= static_cast<uint32>(Data->Num())); }

	FORCEINLINE uint32 ReadUint32() { return ReadValue<uint32>(); }
	FORCEINLINE int32 ReadInt32() { return ReadValue<int32>(); }
	FORCEINLINE uint16 ReadUint16() { return ReadValue<uint16>(); }
	FORCEINLINE int16 ReadInt16() { return ReadValue<int16>(); }
	FORCEINLINE uint8 ReadUint8() { return ReadValue<uint8>(); }
	FORCEINLINE int8 ReadInt8() { return ReadValue<int8>(); }
	FORCEINLINE float ReadFloat() { return ReadValue<float>(); }
	FString ReadString();

	FORCEINLINE uint16 ReadPackedUint16()
	{
		uint16 Value = ReadUint8();

		if (Value & 0x80)
		{
			Value = (Value & 0x7F) << 8 | ReadUint8();
		}

		return Value;
	}

	uint32 ReadPackedUint32()
	{
		uint32 Value = ReadUint16();

		if (Value & 0x8000)
		{
			Value = (Value & 0x7FFF) << 16 | ReadUint16();
		}

		return Value;
	}

	FORCEINLINE uint8* ReadBlob(uint32 Size)
	{
		uint8* const returnValue = reinterpret_cast<uint8*>(&(*Data)[Position]);
		Position += Size;
		return returnValue;
	}

	template<typename T>
	T ReadValue()
	{
		CheckPosition(sizeof(T));
		const T* const returnValue = reinterpret_cast<T*>(&(*Data)[Position]);
		Position += sizeof(T);
		return *returnValue;
	}

	template<typename T>
	T* ReadStructure()
	{
		CheckPosition(sizeof(T));
		return reinterpret_cast<T*>(ReadBlob(sizeof(T)));
	}

	FORCEINLINE void Align() { Position = (Position + 3) & ~3; }

	FORCEINLINE uint32 BytesLeft() { return Data->Num() - Position; }

	FORCEINLINE uint32 TotalSize() { return Data->Num(); }

private:
	// no copies
	FACEWorldBakeByteReader(const FACEWorldBakeByteReader& Other);
	FACEWorldBakeByteReader& operator=(const FACEWorldBakeByteReader& Other);

private:
	TByteArrayPtr Data;
	uint32 Position;
	const uint32 TotalSizeInBytes;
};
