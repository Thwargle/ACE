#pragma once

#include "CoreMinimal.h"

/**
 * Retail PlayerOption. `Option` is the index sent by GameAction 0x0005
 * (SetSingleCharacterOption); `Flag` is the same option's bit inside CharacterOptions1 or
 * CharacterOptions2 as sent by GameEvent PlayerDescription / GameAction 0x01A1.
 */
struct FACECharacterOptionDesc
{
	const TCHAR* Label;
	int32 Option;
	bool bInOptions2;
	uint32 Flag;
	/** Options panel page this option is listed under. */
	uint8 Page;
};

namespace ACECharacterOptions
{
	enum EPage : uint8
	{
		PageCharacter = 0,
		PageChat = 1,
		PageConfig = 2,
	};

	/** ACE.Entity CharacterOptions1.Default / CharacterOptions2.Default. */
	constexpr uint32 Options1Default = 0x00000002u | 0x00000008u | 0x00000040u | 0x00000100u
		| 0x00000400u | 0x00002000u | 0x00008000u | 0x00040000u | 0x00400000u | 0x00800000u
		| 0x10000000u | 0x40000000u;
	constexpr uint32 Options2Default = 0x00000100u | 0x00000200u | 0x00000400u | 0x00008000u
		| 0x00040000u | 0x00100000u | 0x00800000u;

	ACECLIENT_API TArrayView<const FACECharacterOptionDesc> GetTable();
	/** Options listed on one page, in listed order. */
	ACECLIENT_API void GetPageOptions(uint8 Page, TArray<const FACECharacterOptionDesc*>& Out);
	ACECLIENT_API const FACECharacterOptionDesc* Find(int32 Option);
}
