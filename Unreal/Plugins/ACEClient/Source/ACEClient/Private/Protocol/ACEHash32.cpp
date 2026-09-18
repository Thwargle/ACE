#include "Protocol/ACEHash32.h"

uint32 FACEHash32::Calculate(const uint8* Data, int32 Length)
{
	if (Data == nullptr || Length <= 0)
	{
		return 0;
	}

	uint32 Checksum = static_cast<uint32>(Length) << 16;

	int32 i = 0;
	for (; i + 4 <= Length; i += 4)
	{
		const uint32 Word =
			static_cast<uint32>(Data[i]) |
			(static_cast<uint32>(Data[i + 1]) << 8) |
			(static_cast<uint32>(Data[i + 2]) << 16) |
			(static_cast<uint32>(Data[i + 3]) << 24);
		Checksum += Word;
	}

	int32 Shift = 3;
	while (i < Length)
	{
		Checksum += static_cast<uint32>(Data[i++]) << (8 * Shift--);
	}

	return Checksum;
}

uint32 FACEHash32::Calculate(const TArray<uint8>& Data)
{
	return Calculate(Data.GetData(), Data.Num());
}

uint32 FACEHash32::Calculate(const TArrayView<const uint8> Data)
{
	return Calculate(Data.GetData(), Data.Num());
}
