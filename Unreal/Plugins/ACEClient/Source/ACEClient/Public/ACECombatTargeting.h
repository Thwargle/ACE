#pragma once
#include "ACETypes.h"

namespace ACECombatTargeting
{
    // ClientCombatSystem::ObjectIsAttackable (0056A600). Free-PK is deliberately
    // independent of heritage: the server can assign it to any character.
    inline bool CanAttack(const FACEWorldObject& Target, const FACEWorldObject* Player)
    {
        if (!(Target.ItemType & ACEItemType::Creature) || Target.bDying || Target.IsCorpse()) return false;
        if (Target.ObjectDescriptionFlags & ACEObjectDescFlag::FreePkStatus) return true;
        // Before the self descriptor arrives, only ordinary monsters and an
        // explicitly Free-PK target are known to be hostile.
        if (!Player) return !Target.bIsPlayer && Target.IsAttackable() && !Target.PetOwnerId;
        if (Player->ObjectDescriptionFlags & ACEObjectDescFlag::FreePkStatus) return true;
        if (Target.bIsPlayer)
            return (Target.ObjectDescriptionFlags & Player->ObjectDescriptionFlags
                & (ACEObjectDescFlag::PlayerKiller | ACEObjectDescFlag::PkLiteStatus)) != 0;
        return Target.IsAttackable() && !Target.PetOwnerId;
    }
}
