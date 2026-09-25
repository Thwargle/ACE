#include "Protocol/ACECombatChat.h"
#include "Internationalization/Regex.h"

namespace ACECombatChat
{
	bool ParseOutgoingSpellDamage(const FString& Text, FString& Target, int32& Amount, bool& Critical)
	{
		// Only call for senderless Magic messages. Anchor the complete server
		// sentence so someone else's hit, a tell, or a cast announcement cannot
		// be attributed to this player. Physical hits already have their event.
		static const FRegexPattern Pattern(TEXT("^(?:Critical hit! )?(?:Overpower! )?(?:Sneak Attack! )?You [A-Za-z]+ (.+) for ([0-9]+) points (?:with .+\\.|of (?:periodic )?[A-Za-z]+ damage!)(?: Your critical hit was avoided with their augmentation!)?$"));
		FRegexMatcher Match(Pattern, Text);
		if (!Match.FindNext()) return false;
		const int64 Value = FCString::Atoi64(*Match.GetCaptureGroup(2));
		if (Value <= 0 || Value > MAX_int32) return false;
		Target = Match.GetCaptureGroup(1); Amount = int32(Value);
		Critical = Text.StartsWith(TEXT("Critical hit! "));
		return true;
	}

	namespace
	{
		constexpr uint32 CondCriticalProtection = 0x1;
		constexpr uint32 CondRecklessness = 0x2;
		constexpr uint32 CondSneakAttack = 0x4;
		constexpr uint32 CondOverpower = 0x8;
	}

#include "Protocol/ACEWeenieErrorStrings.inl"

	FString DamageTypeName(uint32 DamageType)
	{
		switch (DamageType)
		{
		case 0x1: return TEXT("slashing");
		case 0x2: return TEXT("piercing");
		case 0x4: return TEXT("bludgeoning");
		case 0x8: return TEXT("cold");
		case 0x10: return TEXT("fire");
		case 0x20: return TEXT("acid");
		case 0x40: return TEXT("electric");
		case 0x80: return TEXT("health");
		case 0x100: return TEXT("stamina");
		case 0x200: return TEXT("mana");
		case 0x400: return TEXT("nether");
		default: return TEXT("untyped");
		}
	}

	FString DamageLocationName(uint32 DamageLocation)
	{
		static const TCHAR* Names[] = {
			TEXT("head"), TEXT("chest"), TEXT("abdomen"), TEXT("upper arm"), TEXT("lower arm"),
			TEXT("hand"), TEXT("upper leg"), TEXT("lower leg"), TEXT("foot")
		};
		return Names[FMath::Clamp(static_cast<int32>(DamageLocation), 0, 8)];
	}

	void GetAttackVerb(uint32 DamageType, float Percent, FString& OutSingular, FString& OutPlural)
	{
		// Mirrors ACE.Server Entity.Strings.GetAttackVerb (retail severity bands).
		auto Set = [&](const TCHAR* S, const TCHAR* P)
		{
			OutSingular = S;
			OutPlural = P;
		};

		switch (DamageType)
		{
		case 0x1: // Slash
			if (Percent > 0.5f) Set(TEXT("mangle"), TEXT("mangles"));
			else if (Percent > 0.25f) Set(TEXT("slash"), TEXT("slashes"));
			else if (Percent > 0.1f) Set(TEXT("cut"), TEXT("cuts"));
			else Set(TEXT("scratch"), TEXT("scratches"));
			break;
		case 0x2: // Pierce
			if (Percent > 0.5f) Set(TEXT("gore"), TEXT("gores"));
			else if (Percent > 0.25f) Set(TEXT("impale"), TEXT("impales"));
			else if (Percent > 0.1f) Set(TEXT("stab"), TEXT("stabs"));
			else Set(TEXT("nick"), TEXT("nicks"));
			break;
		case 0x4: // Bludgeon
			if (Percent > 0.5f) Set(TEXT("crush"), TEXT("crushes"));
			else if (Percent > 0.25f) Set(TEXT("smash"), TEXT("smashes"));
			else if (Percent > 0.1f) Set(TEXT("bash"), TEXT("bashes"));
			else Set(TEXT("graze"), TEXT("grazes"));
			break;
		case 0x10: // Fire
			if (Percent > 0.5f) Set(TEXT("incinerate"), TEXT("incinerates"));
			else if (Percent > 0.25f) Set(TEXT("burn"), TEXT("burns"));
			else if (Percent > 0.1f) Set(TEXT("scorch"), TEXT("scorches"));
			else Set(TEXT("singe"), TEXT("singes"));
			break;
		case 0x8: // Cold
			if (Percent > 0.5f) Set(TEXT("freeze"), TEXT("freezes"));
			else if (Percent > 0.25f) Set(TEXT("frost"), TEXT("frosts"));
			else if (Percent > 0.1f) Set(TEXT("chill"), TEXT("chills"));
			else Set(TEXT("numb"), TEXT("numbs"));
			break;
		case 0x20: // Acid
			if (Percent > 0.5f) Set(TEXT("dissolve"), TEXT("dissolves"));
			else if (Percent > 0.25f) Set(TEXT("corrode"), TEXT("corrodes"));
			else if (Percent > 0.1f) Set(TEXT("sear"), TEXT("sears"));
			else Set(TEXT("blister"), TEXT("blisters"));
			break;
		case 0x40: // Electric
			if (Percent > 0.5f) Set(TEXT("blast"), TEXT("blasts"));
			else if (Percent > 0.25f) Set(TEXT("jolt"), TEXT("jolts"));
			else if (Percent > 0.1f) Set(TEXT("shock"), TEXT("shocks"));
			else Set(TEXT("spark"), TEXT("sparks"));
			break;
		case 0x400: // Nether
			if (Percent > 0.5f) Set(TEXT("eradicate"), TEXT("eradicates"));
			else if (Percent > 0.25f) Set(TEXT("wither"), TEXT("withers"));
			else if (Percent > 0.1f) Set(TEXT("twist"), TEXT("twists"));
			else Set(TEXT("scar"), TEXT("scars"));
			break;
		case 0x80: // Health
			if (Percent > 0.5f) Set(TEXT("deplete"), TEXT("depletes"));
			else if (Percent > 0.25f) Set(TEXT("siphon"), TEXT("siphons"));
			else if (Percent > 0.1f) Set(TEXT("exhaust"), TEXT("exhausts"));
			else Set(TEXT("drain"), TEXT("drains"));
			break;
		default:
			Set(TEXT("hit"), TEXT("hits"));
			break;
		}
	}

	FString AppendAttackConditions(const FString& Base, uint64 AttackConditions)
	{
		FString Out = Base;
		if ((AttackConditions & CondCriticalProtection) != 0)
		{
			Out += TEXT(" Your augmented Critical Protection absorbs the severity of the critical strike!");
		}
		if ((AttackConditions & CondRecklessness) != 0)
		{
			Out += TEXT(" Recklessness!");
		}
		if ((AttackConditions & CondSneakAttack) != 0)
		{
			Out += TEXT(" Sneak Attack!");
		}
		if ((AttackConditions & CondOverpower) != 0)
		{
			Out += TEXT(" You overpowered your opponent!");
		}
		return Out;
	}

	FString FormatAttackerNotification(const FString& DefenderName, uint32 DamageType, float Percent,
		uint32 Damage, bool bCritical, uint64 AttackConditions)
	{
		FString Verb, Plural;
		GetAttackVerb(DamageType, Percent, Verb, Plural);
		const FString Type = DamageTypeName(DamageType);
		FString Msg = FString::Printf(TEXT("You %s %s for %u points of %s damage!"),
			*Verb, *DefenderName, Damage, *Type);
		if (bCritical)
		{
			Msg = TEXT("Critical hit! ") + Msg;
		}
		return AppendAttackConditions(Msg, AttackConditions);
	}

	FString FormatDefenderNotification(const FString& AttackerName, uint32 DamageType, float Percent,
		uint32 Damage, uint32 DamageLocation, bool bCritical, uint64 AttackConditions)
	{
		FString Verb, Plural;
		GetAttackVerb(DamageType, Percent, Verb, Plural);
		const FString Type = DamageTypeName(DamageType);
		const FString Loc = DamageLocationName(DamageLocation);
		FString Msg = FString::Printf(TEXT("%s %s you in your %s for %u points of %s damage!"),
			*AttackerName, *Plural, *Loc, Damage, *Type);
		if (bCritical)
		{
			Msg = TEXT("Critical hit! ") + Msg;
		}
		return AppendAttackConditions(Msg, AttackConditions);
	}

	FString FormatEvasionAttacker(const FString& DefenderName)
	{
		return FString::Printf(TEXT("%s evaded your attack!"), *DefenderName);
	}

	FString FormatEvasionDefender(const FString& AttackerName)
	{
		return FString::Printf(TEXT("You evaded %s!"), *AttackerName);
	}

	FString LookupWeenieError(uint32 ErrorCode)
	{
		if (ErrorCode == 0)
		{
			return FString();
		}
		if (const FString* Found = GetWeenieErrorStrings().Find(ErrorCode))
		{
			return *Found;
		}
		return FString::Printf(TEXT("Error 0x%04X"), ErrorCode);
	}

	FString LookupWeenieErrorWithString(uint32 ErrorCode, const FString& Arg)
	{
		// 028B carries an error identifier and a substitution argument, not a complete
		// message. In particular pet-device requirement errors carry just "Summoning".
		// These templates are the last-retail-client messages in WeenieErrorWithString.
		const TCHAR* Template = nullptr;
		switch (ErrorCode)
		{
		case 0x0051: Template = TEXT("You fail to affect %s because you are not a player killer!"); break;
		case 0x0052: Template = TEXT("You fail to affect %s because they are not a player killer!"); break;
		case 0x0053: Template = TEXT("You fail to affect %s because you have different player killer types!"); break;
		case 0x04F6: Template = TEXT("%s fails to affect you because they are not a player killer!"); break;
		case 0x04F7: Template = TEXT("%s fails to affect you because you are not a player killer!"); break;
		case 0x04F8: Template = TEXT("%s fails to affect you because you have different player killer types!"); break;
		case 0x001E: case 0x04CE: Template = TEXT("%s is too busy to accept gifts right now."); break;
		case 0x002B: Template = TEXT("%s cannot carry anymore."); break;
		case 0x03EF: Template = TEXT("%s is not accepting gifts right now."); break;
		case 0x04C6: Template = TEXT("You must be %s to use that item's magic."); break;
		case 0x04C9: Template = TEXT("Your %s is too low to use that item's magic."); break;
		case 0x04CA: Template = TEXT("Only %s may use that item's magic."); break;
		case 0x04CB: Template = TEXT("You must have %s specialized to use that item's magic."); break;
		case 0x04CF: Template = TEXT("%s cannot accept stacked objects. Try giving one at a time."); break;
		case 0x04D1: Template = TEXT("Your %s skill must be trained, not untrained or specialized, in order to be altered in this way!"); break;
		case 0x04D2: Template = TEXT("You do not have enough skill credits to specialize your %s skill."); break;
		case 0x04D4: Template = TEXT("Your %s skill is already untrained!"); break;
		case 0x04D5: Template = TEXT("You are currently wielding items which require a certain level of %s.  Your %s skill cannot be lowered while you are wielding these items.  Please remove these items and try again."); break;
		case 0x04D6: Template = TEXT("You have succeeded in specializing your %s skill!"); break;
		case 0x04D7: Template = TEXT("You have succeeded in lowering your %s skill from specialized to trained!"); break;
		case 0x04D8: Template = TEXT("You have succeeded in untraining your %s skill!"); break;
		case 0x04D9: Template = TEXT("Although you cannot untrain your %s skill, you have succeeded in recovering all the experience you had invested in it."); break;
		case 0x051B: Template = TEXT("You have entered the %s channel."); break;
		case 0x051C: Template = TEXT("You have left the %s channel."); break;
		}
		if (Template) { return FString(Template).Replace(TEXT("%s"), *Arg); }
		// Only the documented free-text identifiers accept the argument verbatim.
		if (ErrorCode == 0 || ErrorCode == 0x04DE || ErrorCode == 0x04DF || ErrorCode == 0x055A || ErrorCode == 0x055E)
		{
			return Arg;
		}
		const FString Base = LookupWeenieError(ErrorCode);
		if (Arg.IsEmpty())
		{
			return Base;
		}
		if (Base.Contains(TEXT("%s")) || Base.Contains(TEXT("{0}")))
		{
			return Base.Replace(TEXT("%s"), *Arg).Replace(TEXT("{0}"), *Arg);
		}
		// Retail string tables use a bare '_' as the name placeholder, e.g.
		// "_ doesn't know what to do with that." for TradeAiDoesntWant.
		if (Base.StartsWith(TEXT("_")))
		{
			return Arg + Base.Mid(1);
		}
		if (Base.Contains(TEXT(" _ ")))
		{
			return Base.Replace(TEXT(" _ "), *FString::Printf(TEXT(" %s "), *Arg));
		}
		return FString::Printf(TEXT("%s (%s)"), *Base, *Arg);
	}
}
