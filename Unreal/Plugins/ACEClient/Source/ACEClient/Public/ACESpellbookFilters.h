#pragma once
#include "CoreMinimal.h"

// PlayerModule / gmSpellbookUI use Creature, Item, Life, War in bits 0..3,
// levels in 4..12 and Void in bit 13. UI school IDs run in the opposite order.
namespace ACESpellbookFilters
{
	inline uint32 Schools(uint32 Wire)
	{
		return ((Wire & 8u) >> 3) | ((Wire & 4u) >> 1) | ((Wire & 2u) << 1)
			| ((Wire & 1u) << 3) | ((Wire & 0x2000u) >> 9);
	}
	inline uint32 Levels(uint32 Wire) { return (Wire >> 4) & 0xFFu; }
	inline uint32 ToWire(uint32 Schools, uint32 Levels, uint32 Previous)
	{
		// Keep the reserved ninth-level bit and future flags when editing a
		// visible checkbox; retail does not offer those as individual controls.
		return (Previous & ~0x2FFFu) | ((Schools & 1u) << 3) | ((Schools & 2u) << 1)
			| ((Schools & 4u) >> 1) | ((Schools & 8u) >> 3) | ((Schools & 16u) << 9)
			| ((Levels & 0xFFu) << 4);
	}
}
