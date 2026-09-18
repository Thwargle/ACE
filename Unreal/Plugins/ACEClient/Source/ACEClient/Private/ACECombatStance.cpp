#include "ACECombatStance.h"
#include "ACEOpcodes.h"

namespace ACECombatStyle
{
	constexpr int32 Unarmed = 0x00001;
	constexpr int32 OneHanded = 0x00002;
	constexpr int32 OneHandedAndShield = 0x00004;
	constexpr int32 TwoHanded = 0x00008;
	constexpr int32 Bow = 0x00010;
	constexpr int32 Crossbow = 0x00020;
	constexpr int32 Sling = 0x00040;
	constexpr int32 ThrownWeapon = 0x00080;
	constexpr int32 DualWield = 0x00100;
	constexpr int32 Magic = 0x00200;
	constexpr int32 Atlatl = 0x00400;
	constexpr int32 ThrownShield = 0x00800;
}

namespace ACEAmmoTypeFlag
{
	constexpr int32 Arrow = 0x1;
	constexpr int32 Bolt = 0x2;
	constexpr int32 Atlatl = 0x4;
}

namespace ACECombatUseVal
{
	constexpr int32 Shield = 0x04;
	constexpr int32 TwoHanded = 0x05;
}

namespace
{
	constexpr int32 ParentLocationRightHand = 1;

	const FACEWorldObject* FindByWieldMask(const TArray<FACEWorldObject>& Equipped, int64 Mask)
	{
		for (const FACEWorldObject& Obj : Equipped)
		{
			if ((Obj.CurrentWieldedLocation & Mask) != 0)
			{
				return &Obj;
			}
		}
		return nullptr;
	}

	const FACEWorldObject* GetEquippedWand(const TArray<FACEWorldObject>& Equipped)
	{
		return FindByWieldMask(Equipped, ACEEquipMask::Held);
	}

	const FACEWorldObject* GetEquippedMissileWeapon(const TArray<FACEWorldObject>& Equipped)
	{
		return FindByWieldMask(Equipped, ACEEquipMask::MissileWeapon);
	}

	const FACEWorldObject* GetEquippedMeleeWeaponMain(const TArray<FACEWorldObject>& Equipped)
	{
		for (const FACEWorldObject& Obj : Equipped)
		{
			if (Obj.ParentLocation == ParentLocationRightHand
				&& ((Obj.CurrentWieldedLocation & ACEEquipMask::MeleeWeapon) != 0
					|| (Obj.CurrentWieldedLocation & ACEEquipMask::TwoHanded) != 0))
			{
				return &Obj;
			}
		}
		for (const FACEWorldObject& Obj : Equipped)
		{
			if ((Obj.CurrentWieldedLocation & (ACEEquipMask::MeleeWeapon | ACEEquipMask::TwoHanded)) != 0)
			{
				return &Obj;
			}
		}
		return nullptr;
	}

	const FACEWorldObject* GetDualWieldWeapon(const TArray<FACEWorldObject>& Equipped)
	{
		for (const FACEWorldObject& Obj : Equipped)
		{
			if ((Obj.CurrentWieldedLocation & ACEEquipMask::Shield) != 0
				&& Obj.CombatUse != ACECombatUseVal::Shield)
			{
				return &Obj;
			}
		}
		return nullptr;
	}

	const FACEWorldObject* GetEquippedShield(const TArray<FACEWorldObject>& Equipped)
	{
		for (const FACEWorldObject& Obj : Equipped)
		{
			if ((Obj.CurrentWieldedLocation & ACEEquipMask::Shield) != 0
				&& Obj.CombatUse == ACECombatUseVal::Shield)
			{
				return &Obj;
			}
		}
		return nullptr;
	}

	const FACEWorldObject* GetEquippedWeaponMain(const TArray<FACEWorldObject>& Equipped)
	{
		if (const FACEWorldObject* Melee = GetEquippedMeleeWeaponMain(Equipped))
		{
			return Melee;
		}
		return GetEquippedMissileWeapon(Equipped);
	}

	uint32 AddShieldStance(uint32 CombatStance)
	{
		switch (CombatStance)
		{
		case ACEMotion::StanceSwordCombat:
			return ACEMotion::StanceSwordShield;
		case ACEMotion::StanceThrownWeapon:
			return ACEMotion::StanceThrownShield;
		default:
			return CombatStance;
		}
	}
}

uint32 ACECombatStance::ResolveEquippedMode(const TArray<FACEWorldObject>& Equipped)
{
	if (GetEquippedWand(Equipped)) return ACECombatMode::Magic;
	if (const auto* Weapon = GetEquippedWeaponMain(Equipped))
		return (Weapon->CurrentWieldedLocation & ACEEquipMask::MissileWeapon) != 0
			|| (Weapon->ItemType & ACEItemType::MissileWeapon) != 0 ? ACECombatMode::Missile : ACECombatMode::Melee;
	// Retail arrows also have ItemType::MissileWeapon. Their ammunition slot
	// must not switch a sword (or empty hands) into the missile attack path.
	return ACECombatMode::Melee;
}

uint32 ACECombatStance::StanceFromCombatStyle(int32 Style)
{
	switch (Style)
	{
	case ACECombatStyle::Atlatl:
		return ACEMotion::StanceAtlatl;
	case ACECombatStyle::Bow:
		return ACEMotion::StanceBowCombat;
	case ACECombatStyle::Crossbow:
		return ACEMotion::StanceCrossbow;
	case ACECombatStyle::DualWield:
		return ACEMotion::StanceDualWield;
	case ACECombatStyle::Magic:
		return ACEMotion::StanceMagic;
	case ACECombatStyle::OneHanded:
	case ACECombatStyle::OneHandedAndShield:
		return ACEMotion::StanceSwordCombat;
	case ACECombatStyle::Sling:
		return ACEMotion::StanceSling;
	case ACECombatStyle::ThrownShield:
		return ACEMotion::StanceThrownShield;
	case ACECombatStyle::ThrownWeapon:
		return ACEMotion::StanceThrownWeapon;
	case ACECombatStyle::TwoHanded:
		return ACEMotion::StanceTwoHandedSword;
	case ACECombatStyle::Unarmed:
		return ACEMotion::StanceHandCombat;
	default:
		return ACEMotion::StanceHandCombat;
	}
}

int32 ACECombatStance::InferCombatStyle(const FACEWorldObject& Obj)
{
	if (Obj.DefaultCombatStyle != 0)
	{
		return Obj.DefaultCombatStyle;
	}
	if ((Obj.ItemType & ACEItemType::Caster) != 0
		|| (Obj.CurrentWieldedLocation & ACEEquipMask::Held) != 0)
	{
		return ACECombatStyle::Magic;
	}
	const bool bMissileWield = (Obj.ItemType & ACEItemType::MissileWeapon) != 0
		|| (Obj.CurrentWieldedLocation & ACEEquipMask::MissileWeapon) != 0;
	if (bMissileWield)
	{
		const bool bMainHandMissile = (Obj.CurrentWieldedLocation & ACEEquipMask::MissileWeapon) == 0
			&& (Obj.CurrentWieldedLocation & (ACEEquipMask::MeleeWeapon | ACEEquipMask::TwoHanded)) != 0;
		if (bMainHandMissile)
		{
			return ACECombatStyle::ThrownWeapon;
		}
		if ((Obj.AmmoType & ACEAmmoTypeFlag::Atlatl) != 0)
		{
			return ACECombatStyle::Atlatl;
		}
		if ((Obj.AmmoType & ACEAmmoTypeFlag::Bolt) != 0)
		{
			return ACECombatStyle::Crossbow;
		}
		if ((Obj.AmmoType & ACEAmmoTypeFlag::Arrow) != 0)
		{
			return ACECombatStyle::Bow;
		}
		return ACECombatStyle::ThrownWeapon;
	}
	if (Obj.CombatUse == ACECombatUseVal::TwoHanded
		|| (Obj.CurrentWieldedLocation & ACEEquipMask::TwoHanded) != 0
		|| (Obj.ValidLocations & ACEEquipMask::TwoHanded) != 0)
	{
		return ACECombatStyle::TwoHanded;
	}
	if ((Obj.ItemType & ACEItemType::MeleeWeapon) != 0
		|| (Obj.CurrentWieldedLocation & (ACEEquipMask::MeleeWeapon | ACEEquipMask::TwoHanded)) != 0)
	{
		return ACECombatStyle::OneHanded;
	}
	return ACECombatStyle::Unarmed;
}

uint32 ACECombatStance::ResolveFromEquipped(const TArray<FACEWorldObject>& Equipped, bool bForceHandCombat)
{
	if (bForceHandCombat)
	{
		return ACEMotion::StanceHandCombat;
	}
	if (GetEquippedWand(Equipped) != nullptr)
	{
		return ACEMotion::StanceMagic;
	}
	uint32 CombatStance = ACEMotion::StanceHandCombat;
	if (const FACEWorldObject* Weapon = GetEquippedWeaponMain(Equipped))
	{
		CombatStance = StanceFromCombatStyle(InferCombatStyle(*Weapon));
	}
	if (GetDualWieldWeapon(Equipped) != nullptr)
	{
		CombatStance = ACEMotion::StanceDualWield;
	}
	if (GetEquippedShield(Equipped) != nullptr)
	{
		CombatStance = AddShieldStance(CombatStance);
	}
	return CombatStance;
}

uint32 ACECombatStance::ResolveForCombatMode(const TArray<FACEWorldObject>& Equipped, uint32 CombatMode,
	bool bForceHandCombat)
{
	if (CombatMode == ACECombatMode::NonCombat)
	{
		return ACEMotion::StanceNonCombat;
	}
	if (CombatMode == ACECombatMode::Magic)
	{
		return ACEMotion::StanceMagic;
	}
	return ResolveFromEquipped(Equipped, bForceHandCombat);
}
