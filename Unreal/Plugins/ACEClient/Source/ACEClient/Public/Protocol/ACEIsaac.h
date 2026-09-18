#pragma once

#include "CoreMinimal.h"

/**
 * ISAAC stream cipher matching ACE.Common.Cryptography.ISAAC.
 * Seed is 4 bytes (uint32 LE); used only to XOR packet checksums.
 */
class ACECLIENT_API FACEIsaac
{
public:
	FACEIsaac() = default;
	explicit FACEIsaac(const uint8 Seed[4]);
	explicit FACEIsaac(uint32 Seed);

	void Initialize(const uint8 Seed[4]);
	uint32 Next();

private:
	void IsaacScramble();
	static void Shuffle(uint32 X[8]);

	uint32 Offset = 255;
	uint32 A = 0, B = 0, C = 0;
	uint32 Mm[256];
	uint32 RandRsl[256];
	bool bInitialized = false;
};

/** Client-side ISAAC window used to verify S2C encrypted checksums. */
class ACECLIENT_API FACECryptoSystem : public FACEIsaac
{
public:
	FACECryptoSystem() = default;
	explicit FACECryptoSystem(const uint8 Seed[4]);
	explicit FACECryptoSystem(uint32 Seed);

	bool Search(uint32 Key);
	void ConsumeKey(uint32 Key);

	uint32 CurrentKey = 0;

private:
	static constexpr int32 MaximumEffortLevel = 256;
	TSet<uint32> Xors;
};
