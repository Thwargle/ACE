#pragma once
#include "ACETypes.h"

namespace ACEEquipmentRules
{
    // Retail's left weapon slot shares Shield. One-handed melee descriptors
    // advertise MeleeWeapon only; ACE explicitly permits this alternate slot.
    inline bool CanWieldInSlot(const FACEWorldObject& Item, int64 Slot)
    {
        return (Item.ValidLocations & Slot) != 0
            || (Slot == ACEEquipMask::Shield && Item.ValidLocations == ACEEquipMask::MeleeWeapon);
    }
}
