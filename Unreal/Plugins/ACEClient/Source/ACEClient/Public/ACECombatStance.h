#pragma once

#include "CoreMinimal.h"
#include "ACETypes.h"

/** Mirrors ACE.Server Creature_Combat.GetCombatStance / GetWeaponStance for client animation. */
namespace ACECombatStance
{
	/** Choose the active weapon's mode; equipped ammunition and offhand items do not choose it. */
	uint32 ResolveEquippedMode(const TArray<FACEWorldObject>& Equipped);

	/** PropertyInt.DefaultCombatStyle / ACE.Entity.Enum.CombatStyle → MotionStance. */
	uint32 StanceFromCombatStyle(int32 CombatStyle);

	/** When DefaultCombatStyle was not on the wire, infer from item type / ammo / wield. */
	int32 InferCombatStyle(const FACEWorldObject& Obj);

	/** Equipment-only combat stance (ACE GetCombatStance). */
	uint32 ResolveFromEquipped(const TArray<FACEWorldObject>& Equipped, bool bForceHandCombat = false);

	/** Peace vs combat mode — NonCombat uses NonCombat stance; combat modes use ResolveFromEquipped. */
	uint32 ResolveForCombatMode(const TArray<FACEWorldObject>& Equipped, uint32 CombatMode,
		bool bForceHandCombat = false);
}
