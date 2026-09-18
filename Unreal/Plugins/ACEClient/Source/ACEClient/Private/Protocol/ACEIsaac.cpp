#include "Protocol/ACEIsaac.h"

FACEIsaac::FACEIsaac(const uint8 Seed[4])
{
	Initialize(Seed);
}

FACEIsaac::FACEIsaac(uint32 Seed)
{
	uint8 Bytes[4];
	Bytes[0] = static_cast<uint8>(Seed & 0xFF);
	Bytes[1] = static_cast<uint8>((Seed >> 8) & 0xFF);
	Bytes[2] = static_cast<uint8>((Seed >> 16) & 0xFF);
	Bytes[3] = static_cast<uint8>((Seed >> 24) & 0xFF);
	Initialize(Bytes);
}

void FACEIsaac::Initialize(const uint8 Seed[4])
{
	Offset = 255;
	for (int32 i = 0; i < 256; ++i)
	{
		Mm[i] = 0;
		RandRsl[i] = 0;
	}

	uint32 Abcdefgh[8];
	for (int32 i = 0; i < 8; ++i)
	{
		Abcdefgh[i] = 0x9E3779B9u;
	}

	for (int32 i = 0; i < 4; ++i)
	{
		Shuffle(Abcdefgh);
	}

	for (int32 Round = 0; Round < 2; ++Round)
	{
		for (int32 j = 0; j < 256; j += 8)
		{
			for (int32 k = 0; k < 8; ++k)
			{
				Abcdefgh[k] += (Round < 1) ? RandRsl[j + k] : Mm[j + k];
			}
			Shuffle(Abcdefgh);
			for (int32 k = 0; k < 8; ++k)
			{
				Mm[j + k] = Abcdefgh[k];
			}
		}
	}

	A = static_cast<uint32>(Seed[0]) |
		(static_cast<uint32>(Seed[1]) << 8) |
		(static_cast<uint32>(Seed[2]) << 16) |
		(static_cast<uint32>(Seed[3]) << 24);
	C = B = A;

	IsaacScramble();
	bInitialized = true;
}

uint32 FACEIsaac::Next()
{
	if (!bInitialized)
	{
		return 0;
	}

	const uint32 Value = RandRsl[Offset];
	if (Offset > 0)
	{
		--Offset;
	}
	else
	{
		IsaacScramble();
		Offset = 255;
	}
	return Value;
}

void FACEIsaac::IsaacScramble()
{
	B += ++C;
	for (int32 i = 0; i < 256; ++i)
	{
		const uint32 X = Mm[i];
		switch (i & 3)
		{
		case 0: A ^= (A << 0x0D); break;
		case 1: A ^= (A >> 0x06); break;
		case 2: A ^= (A << 0x02); break;
		case 3: A ^= (A >> 0x10); break;
		default: break;
		}

		A += Mm[(i + 128) & 0xFF];
		uint32 Y;
		Mm[i] = Y = Mm[(X >> 2) & 0xFF] + A + B;
		RandRsl[i] = B = Mm[(Y >> 10) & 0xFF] + X;
	}
}

void FACEIsaac::Shuffle(uint32 X[8])
{
	X[0] ^= X[1] << 0x0B; X[3] += X[0]; X[1] += X[2];
	X[1] ^= X[2] >> 0x02; X[4] += X[1]; X[2] += X[3];
	X[2] ^= X[3] << 0x08; X[5] += X[2]; X[3] += X[4];
	X[3] ^= X[4] >> 0x10; X[6] += X[3]; X[4] += X[5];
	X[4] ^= X[5] << 0x0A; X[7] += X[4]; X[5] += X[6];
	X[5] ^= X[6] >> 0x04; X[0] += X[5]; X[6] += X[7];
	X[6] ^= X[7] << 0x08; X[1] += X[6]; X[7] += X[0];
	X[7] ^= X[0] >> 0x09; X[2] += X[7]; X[0] += X[1];
}

FACECryptoSystem::FACECryptoSystem(const uint8 Seed[4])
	: FACEIsaac(Seed)
{
	CurrentKey = Next();
}

FACECryptoSystem::FACECryptoSystem(uint32 Seed)
	: FACEIsaac(Seed)
{
	CurrentKey = Next();
}

void FACECryptoSystem::ConsumeKey(uint32 Key)
{
	if (CurrentKey == Key)
	{
		CurrentKey = Next();
	}
	else
	{
		Xors.Remove(Key);
	}
}

bool FACECryptoSystem::Search(uint32 Key)
{
	if (CurrentKey == Key)
	{
		return true;
	}
	if (Xors.Contains(Key))
	{
		return true;
	}

	const int32 Grown = Xors.Num();
	for (int32 i = 0; i < MaximumEffortLevel - Grown; ++i)
	{
		Xors.Add(CurrentKey);
		ConsumeKey(CurrentKey);
		if (CurrentKey == Key)
		{
			return true;
		}
	}
	return false;
}
