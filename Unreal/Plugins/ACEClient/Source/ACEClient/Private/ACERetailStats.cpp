#include "ACEDatSubsystem.h"

void UACEDatSubsystem::RecomputePlayerStats(FACEPlayerVitals& V, const TArray<FACEActiveEnchantment>& Enchantments)
{
	// CEnchantmentRegistry::CullEnchantmentsFromList / Duel: only the winning
	// enchantment in each category affects a quality. Suppressed item buffs still
	// exist in the registry, so summing every row overstates attributes and skills.
	auto Modifiers = [&](uint32 Type, int32 Key, float& Mul, float& Add)
	{
		Mul = 1.f; Add = 0.f;
		TMap<int32, const FACEActiveEnchantment*> Winners;
		for (const auto& E : Enchantments)
		{
			const uint32 Flags = static_cast<uint32>(E.StatModType);
			if (E.bCooldown || E.bVitae || !(Flags & Type)) { continue; }
			const bool Attack = Key == 41 || (Key >= 43 && Key <= 47) || Key == 49 || Key == 33 || Key == 34;
			const bool Defense = Key == 6 || Key == 7 || Key == 15 || Key == 48;
			if (!(Flags & 0x2000u) && E.StatModKey != Key
				&& !(Type == 0x10u && ((Attack && (Flags & 0x10000u)) || (Defense && (Flags & 0x20000u))))) { continue; }
			const auto* Previous = Winners.FindRef(E.SpellCategory);
			if (!Previous || E.PowerLevel > Previous->PowerLevel
				|| (E.PowerLevel == Previous->PowerLevel && E.StartTime >= Previous->StartTime))
			{
				Winners.Add(E.SpellCategory, &E);
			}
		}
		for (const auto& Pair : Winners)
		{
			const auto& E = *Pair.Value;
			if (E.StatModType & 0x4000) { Mul *= E.StatModValue; }
			else if (E.StatModType & 0x8000) { Add += E.StatModValue; }
		}
	};
	Modifiers(1, 1, V.StrengthEnchantMul, V.StrengthEnchantAdd);
	Modifiers(1, 2, V.EnduranceEnchantMul, V.EnduranceEnchantAdd);
	Modifiers(1, 3, V.QuicknessEnchantMul, V.QuicknessEnchantAdd);
	Modifiers(1, 4, V.CoordinationEnchantMul, V.CoordinationEnchantAdd);
	Modifiers(1, 5, V.FocusEnchantMul, V.FocusEnchantAdd);
	Modifiers(1, 6, V.SelfEnchantMul, V.SelfEnchantAdd);
	float Vitae = 1.f;
	for (const auto& E : Enchantments) { if (E.bVitae) { Vitae = E.StatModValue; } }
	for (auto& Skill : V.Skills)
	{
		FString Name; uint32 Icon = 0;
		TryGetSkillInfo(Skill.SkillId, Name, Icon);
		const auto* Info = SkillInfoCache.Find(Skill.SkillId);
		auto Formula = [&](bool Buffed) -> int32
		{
			if (!Info || !Info->FormulaZ || Skill.AdvancementClass < static_cast<int32>(Info->MinLevel)) { return 0; }
			const int32 A = Buffed ? V.GetAttributeCurrent(Info->FormulaAttr1) : V.GetAttributeBase(Info->FormulaAttr1);
			const int32 B = Buffed ? V.GetAttributeCurrent(Info->FormulaAttr2) : V.GetAttributeBase(Info->FormulaAttr2);
			// SkillFormula::Calculate rounds, it does not truncate integer division.
			return FMath::RoundToInt((static_cast<double>(Info->FormulaW) + A * Info->FormulaX + B * Info->FormulaY) / Info->FormulaZ);
		};
		if (!Name.IsEmpty()) { Skill.Name = Name; }
		int32 Innate = Skill.InitLevel + Skill.Ranks + FMath::Max(0, V.StatQualityInts.FindRef(0x16D)); // Jack of All Trades
		const bool Magic = Skill.SkillId == 31 || Skill.SkillId == 32 || Skill.SkillId == 33 || Skill.SkillId == 34 || Skill.SkillId == 43;
		const bool Melee = Skill.SkillId == 41 || Skill.SkillId == 44 || Skill.SkillId == 45 || Skill.SkillId == 46 || Skill.SkillId == 49;
		if ((Magic && V.StatQualityInts.FindRef(0x12E) > 0) || (Melee && V.StatQualityInts.FindRef(0x12C) > 0)
			|| (Skill.SkillId == 47 && V.StatQualityInts.FindRef(0x12D) > 0)) { Innate += 10; }
		Skill.Base = Innate + Formula(false);
		float Mul, Add; Modifiers(0x10, Skill.SkillId, Mul, Add);
		Skill.Current = FMath::Max(0, FMath::RoundToInt((Innate + Formula(true)) * Vitae * Mul + Add));
		if (V.StatQualityInts.FindRef(0x146) > 0) { Skill.Current += 5; }
		if (Skill.AdvancementClass == 3) { Skill.Current += 2 * FMath::Max(0, V.StatQualityInts.FindRef(0x158)); }
		if (Skill.SkillId == 22) { V.JumpSkillCurrent = Skill.Current; }
		if (Skill.SkillId == 24) { V.RunSkillCurrent = Skill.Current; }
	}
	// Maximums are derived values, never a historical high-water mark. Buff expiry
	// must lower them even when the corresponding Current packet arrives later.
	auto Vital = [&](int32 Key, int32 Base)
	{
		float Mul, Add; Modifiers(2, Key, Mul, Add);
		return FMath::Max(Base < 5 ? 1 : 5, FMath::RoundToInt(Base * Vitae * Mul + Add));
	};
	V.MaxHealth = Vital(1, V.HealthStart + V.HealthRanks + FMath::RoundToInt(V.GetBuffedEndurance() / 2.f));
	V.MaxStamina = Vital(3, V.StaminaStart + V.StaminaRanks + V.GetBuffedEndurance());
	V.MaxMana = Vital(5, V.ManaStart + V.ManaRanks + V.GetBuffedSelf());
}
