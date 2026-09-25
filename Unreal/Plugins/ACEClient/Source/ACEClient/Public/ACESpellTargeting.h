#pragma once
#include "ACETypes.h"

namespace ACESpellTargeting
{
    // ClientMagicSystem::ObjectCompatibleWithSpellTargetType. Item enchantments
    // may target a player to enchant their equipment; creature "Other" spells
    // cannot target the caster, irrespective of school or beneficial flags.
    inline bool IsCompatible(uint32 TargetType, int32 PlayerGuid, int32 TargetGuid, const FACEWorldObject* Target)
    {
        if (!TargetType) return TargetGuid == 0;
        if (!TargetGuid || !Target) return false;
        constexpr uint32 EquipmentTargets = 0x8107;
        if (TargetGuid == PlayerGuid && !(TargetType & EquipmentTargets)) return false;
        if (Target->StackSize > 1 || Target->PetOwnerId != 0) return false;
        if (!(Target->ItemType & TargetType) && !(TargetType & EquipmentTargets)) return false;
        return Target->bIsPlayer || (Target->ObjectDescriptionFlags & ACEObjectDescFlag::Attackable) != 0;
    }
}
