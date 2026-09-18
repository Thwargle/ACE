#pragma once

#include "CoreMinimal.h"

/** Client-side formatters for F7B0 combat GameEvents (retail chat strings). */
namespace ACECombatChat
{
	FString DamageTypeName(uint32 DamageType);
	FString DamageLocationName(uint32 DamageLocation);
	void GetAttackVerb(uint32 DamageType, float Percent, FString& OutSingular, FString& OutPlural);

	FString FormatAttackerNotification(const FString& DefenderName, uint32 DamageType, float Percent,
		uint32 Damage, bool bCritical, uint64 AttackConditions);
	FString FormatDefenderNotification(const FString& AttackerName, uint32 DamageType, float Percent,
		uint32 Damage, uint32 DamageLocation, bool bCritical, uint64 AttackConditions);
	FString FormatEvasionAttacker(const FString& DefenderName);
	FString FormatEvasionDefender(const FString& AttackerName);
	FString AppendAttackConditions(const FString& Base, uint64 AttackConditions);

	FString LookupWeenieError(uint32 ErrorCode);
	FString LookupWeenieErrorWithString(uint32 ErrorCode, const FString& Arg);
}
