#pragma once

#include "CoreMinimal.h"
#include "ACETypes.h"

class ACECLIENT_API FACEBinaryReader
{
public:
	FACEBinaryReader() = default;
	FACEBinaryReader(const uint8* InData, int32 InLength);
	explicit FACEBinaryReader(const TArray<uint8>& InData);

	void Reset(const uint8* InData, int32 InLength);
	bool IsValid() const { return Data != nullptr; }
	int32 Tell() const { return Position; }
	int32 Remaining() const { return Length - Position; }
	bool CanRead(int32 Bytes) const { return Bytes >= 0 && Position + Bytes <= Length; }
	void Seek(int32 NewPosition);
	void Skip(int32 Bytes);
	void Align();

	uint8 ReadUInt8();
	uint16 ReadUInt16();
	uint32 ReadUInt32();
	uint64 ReadUInt64();
	int32 ReadInt32();
	float ReadFloat();
	double ReadDouble();
	TArray<uint8> ReadBytes(int32 Count);
	FString ReadString16L();
	/** Turbine chat packed UTF-16LE. */
	FString ReadPackedUnicode();
	FACEPosition ReadPosition();

	const uint8* GetData() const { return Data; }
	int32 GetLength() const { return Length; }

private:
	static uint32 PadMultiple(uint32 Len, uint32 Multiple);
	const uint8* Data = nullptr;
	int32 Length = 0;
	int32 Position = 0;
};
