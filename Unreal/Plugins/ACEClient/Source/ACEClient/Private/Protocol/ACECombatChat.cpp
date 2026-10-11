#include "Protocol/ACECombatChat.h"
#include "ACEOpcodes.h"
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
	}

	namespace
	{
		struct FWeenieErrorEntry
		{
			uint32 Code;
			const TCHAR* Text;
			int32 ChatType;
		};

#include "Protocol/ACEWeenieErrorStrings.inl"

		const FWeenieErrorEntry* FindWeenieError(uint32 Code)
		{
			static const TMap<uint32, const FWeenieErrorEntry*> Entries = []
			{
				TMap<uint32, const FWeenieErrorEntry*> Result;
				Result.Reserve(UE_ARRAY_COUNT(WeenieErrorEntries));
				for (const FWeenieErrorEntry& Entry : WeenieErrorEntries) Result.Add(Entry.Code, &Entry);
				return Result;
			}();
			const auto* Found = Entries.Find(Code);
			return Found ? *Found : nullptr;
		}

		FString FormatWeenieError(uint32 Code, const FString& Argument)
		{
			if (const FWeenieErrorEntry* Entry = FindWeenieError(Code); Entry && Entry->Text)
			{
				const FString Template(Entry->Text);
				if (!Template.Contains(TEXT("%s"))) return Template;
				// These identifiers carry complete server text, including an empty message.
				if (Template == TEXT("%s")) return Argument;
				if (!Argument.IsEmpty())
				{
					// A single, non-recursive substitution preserves literal '%' / braces in
					// names and also handles templates which use the same name more than once.
					return Template.Replace(TEXT("%s"), *Argument, ESearchCase::CaseSensitive);
				}
				// Retail supplies "item" when the salvage name is empty.
				if (Code == 0x04BF || Code == 0x04C0)
					return Template.Replace(TEXT("%s"), TEXT("item"));
			}
			// Keep diagnostic identifiers out of player-facing chat. Internal failures,
			// malformed/missing arguments and private-server additions have no safe
			// specific explanation; do not guess from the supplied name or item string.
			UE_LOG(LogTemp, Log, TEXT("ACE: Unformatted server failure 0x%04X (%s)"), Code,
				FindWeenieError(Code) ? TEXT("internal code or missing argument") : TEXT("unknown code"));
			return TEXT("The server could not complete that action. Please try again.");
		}
	}

	FString DamageTypeName(uint32 DamageType)
	{
		// AppraisalSystem::DamageTypeToString: only these flags have labels;
		// combinations retain retail ordering and are separated by '/'.
		static const struct { uint32 Mask; const TCHAR* Name; } Types[] = {
			{1,TEXT("slashing")},{2,TEXT("piercing")},{4,TEXT("bludgeoning")},
			{8,TEXT("cold")},{16,TEXT("fire")},{32,TEXT("acid")},
			{64,TEXT("electrical")},{1024,TEXT("nether")},{0x10000000,TEXT("prismatic")}};
		FString Out;
		for (const auto& Type : Types) if (DamageType & Type.Mask)
		{
			if (!Out.IsEmpty()) Out += TEXT("/");
			Out += Type.Name;
		}
		return Out;
	}

	FString DamageLocationName(uint32 DamageLocation)
	{
		static const TCHAR* Names[] = {
			TEXT("head"), TEXT("chest"), TEXT("abdomen"), TEXT("upper arm"), TEXT("lower arm"),
			TEXT("hand"), TEXT("upper leg"), TEXT("lower leg"), TEXT("foot"), TEXT("horn"),
			TEXT("front leg"), TEXT("unknown"), TEXT("front foot"), TEXT("rear leg"), TEXT("unknown"),
			TEXT("rear foot"), TEXT("torso"), TEXT("tail"), TEXT("arm"), TEXT("leg"), TEXT("claw"),
			TEXT("wings"), TEXT("breath"), TEXT("tentacle"), TEXT("upper tentacle"), TEXT("lower tentacle"),
			TEXT("cloak"), TEXT("num")
		};
		if (DamageLocation == MAX_uint32) return TEXT("undefined");
		return DamageLocation < UE_ARRAY_COUNT(Names) ? Names[DamageLocation] : TEXT("unknown");
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

	static FString AttackPrefix(bool bCritical, uint64 Conditions, bool bDefender)
	{
		// ClientCombatSystem::HandleAttacker/DefenderNotificationEvent. The
		// server decides which skills proc; retail only presents these flags.
		FString Out = bCritical ? (bDefender ? TEXT("Critical hit! ") : TEXT("Critical hit!  ")) : TEXT("");
		if (Conditions & CondSneakAttack) Out += TEXT("Sneak Attack! ");
		if (Conditions & CondRecklessness) Out += bDefender ? TEXT("Reckless! ") : TEXT("Recklessness! ");
		// Retail does not print condition 0x8 for physical hits. Magic hit text
		// (including Overpower) arrives already formatted by the server.
		return Out;
	}

	FString FormatAttackerNotification(const FString& DefenderName, uint32 DamageType, float Percent,
		uint32 Damage, bool bCritical, uint64 AttackConditions)
	{
		FString Verb, Plural;
		GetAttackVerb(DamageType, Percent, Verb, Plural);
		FString Type = DamageTypeName(DamageType);
		if (!Type.IsEmpty()) Type += TEXT(" ");
		FString Msg = AttackPrefix(bCritical, AttackConditions, false)
			+ FString::Printf(TEXT("You %s %s for %u point%s of %sdamage!"),
				*Verb, *DefenderName, Damage, Damage == 1 ? TEXT("") : TEXT("s"), *Type);
		if (AttackConditions & CondCriticalProtection)
			Msg += TEXT(" Your target's Critical Protection augmentation allows them to avoid your critical hit!");
		return Msg;
	}

	FString FormatDefenderNotification(const FString& AttackerName, uint32 DamageType, float Percent,
		uint32 Damage, uint32 DamageLocation, bool bCritical, uint64 AttackConditions)
	{
		FString Verb, Plural;
		GetAttackVerb(DamageType, Percent, Verb, Plural);
		FString Type = DamageTypeName(DamageType);
		if (!Type.IsEmpty()) Type += TEXT(" ");
		const FString Loc = DamageLocationName(DamageLocation);
		const FString Body = Loc.IsEmpty() ? TEXT("you") : TEXT("your ") + Loc;
		FString Msg = AttackPrefix(bCritical, AttackConditions, true)
			+ FString::Printf(TEXT("%s %s %s for %u point%s of %sdamage!"),
				*AttackerName, *Plural, *Body, Damage, Damage == 1 ? TEXT("") : TEXT("s"), *Type);
		if (AttackConditions & CondCriticalProtection)
			Msg += TEXT(" Your Critical Protection augmentation allows you to avoid a critical hit!");
		return Msg;
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
		return FormatWeenieError(ErrorCode, FString());
	}

	FString LookupWeenieErrorWithString(uint32 ErrorCode, const FString& Arg)
	{
		return ErrorCode == 0 ? Arg : FormatWeenieError(ErrorCode, Arg);
	}

	int32 WeenieErrorChatType(uint32 ErrorCode)
	{
		const FWeenieErrorEntry* Entry = FindWeenieError(ErrorCode);
		return Entry ? Entry->ChatType : ACEChatMessageType::ChatError;
	}
}
