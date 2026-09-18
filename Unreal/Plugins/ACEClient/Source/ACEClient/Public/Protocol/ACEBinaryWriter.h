#pragma once

#include "CoreMinimal.h"

/** Little-endian binary writer with ACE String16L / String32L helpers. */
class ACECLIENT_API FACEBinaryWriter
{
public:
	void Reset();
	void WriteUInt8(uint8 Value);
	void WriteUInt16(uint16 Value);
	void WriteUInt32(uint32 Value);
	void WriteUInt64(uint64 Value);
	void WriteInt32(int32 Value);
	void WriteFloat(float Value);
	void WriteDouble(double Value);
	void WriteBytes(const uint8* Data, int32 Length);
	void WriteBytes(const TArray<uint8>& Data);

	/** Windows-1252-ish (ASCII-compatible for account/password); padded to 4-byte boundary including length. */
	void WriteString16L(const FString& Value);

	/** Turbine chat packed UTF-16LE (byte length, then UTF-16 code units). */
	void WritePackedUnicode(const FString& Value);

	void WriteUInt32At(int32 ByteOffset, uint32 Value);

	/** Login password / GLS ticket packing. */
	void WriteString32L(const FString& Value);

	void Align();
	void Pad(uint32 Count);

	const TArray<uint8>& GetData() const { return Buffer; }
	TArray<uint8>& GetDataMutable() { return Buffer; }
	int32 Num() const { return Buffer.Num(); }

private:
	static uint32 PadMultiple(uint32 Length, uint32 Multiple);
	TArray<uint8> Buffer;
};
