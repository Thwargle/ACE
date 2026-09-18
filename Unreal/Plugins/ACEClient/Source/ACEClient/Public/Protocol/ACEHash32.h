#pragma once

#include "CoreMinimal.h"

/** ACE Hash32 checksum (length<<16 + LE dword sum + residual). */
struct ACECLIENT_API FACEHash32
{
	static uint32 Calculate(const uint8* Data, int32 Length);
	static uint32 Calculate(const TArray<uint8>& Data);
	static uint32 Calculate(const TArrayView<const uint8> Data);
};
