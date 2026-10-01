#pragma once
#include "ACETypes.h"

namespace ACEAppraisalFormatting
{
/** ItemExamineUI's usage/kit/capacity/lock sections; all values are server data. */
inline FString ItemUsageDetails(const FACEAppraisalInfo& Info)
{
    FString Text;
    const bool Healer = (Info.ObjectDescriptionFlags & 0x10000) != 0;
    if (Healer)
    {
        if (const auto* Bonus = Info.IntProperties.Find(90))
            Text += FString::Printf(TEXT("Bonus to Healing Skill: %d\n"), *Bonus);
        if (const auto* Bonus = Info.FloatProperties.Find(100); Bonus && FMath::IsFinite(*Bonus))
            Text += FString::Printf(TEXT("Restoration Bonus: %.0f%%\n"), FMath::TruncToDouble(*Bonus * 100.));
    }
    else if (const auto* Boost = Info.IntProperties.Find(90))
    {
        const int32 Vital = Info.IntProperties.FindRef(89);
        const TCHAR* Name = Vital == 2 ? TEXT("Health") : Vital == 4 ? TEXT("Stamina") : Vital == 6 ? TEXT("Mana") : nullptr;
        if (Name) Text += FString::Printf(TEXT("%s %lld %s when %s.\n"), *Boost < 0 ? TEXT("Depletes") : TEXT("Restores"),
            FMath::Abs(int64(*Boost)), Name, Vital == 4 ? TEXT("consumed") : TEXT("used"));
    }
    const int32 Items = Info.IntProperties.FindRef(6), Packs = Info.IntProperties.FindRef(7);
    if (Items > 0 && Packs > 0) Text += FString::Printf(TEXT("Can hold up to %d items and %d containers.\n"), Items, Packs);
    else if (Items > 0) Text += FString::Printf(TEXT("Can hold up to %d items.\n"), Items);
    else if (Packs > 0) Text += FString::Printf(TEXT("Can hold up to %d containers.\n"), Packs);
    if (const auto* Max = Info.IntProperties.Find(175); Max && Info.IntProperties.Contains(174))
        Text += FString::Printf(TEXT("%d of %d pages full.\n"), Info.IntProperties.FindRef(174), *Max);
    if (const auto* Locked = Info.BoolProperties.Find(3))
    {
        Text += *Locked ? TEXT("Locked\n") : TEXT("Unlocked\n");
        if (*Locked)
        {
            if (const auto* Resistance = Info.IntProperties.Find(38))
            {
                if (const auto* Chance = Info.IntProperties.Find(173); Chance && *Chance >= 0)
                {
                    const int32 P = *Chance;
                    const TCHAR* Difficulty = P == 0 ? TEXT("impossible") : P < 5 ? TEXT("ridiculously difficult")
                        : P < 15 ? TEXT("extremely difficult") : P < 35 ? TEXT("quite difficult") : P < 50 ? TEXT("difficult")
                        : P < 70 ? TEXT("challenging") : P < 85 ? TEXT("mildly challenging") : P < 95 ? TEXT("easy") : TEXT("trivial");
                    Text += FString::Printf(TEXT("The lock looks %s to pick (Resistance %d).\n"), Difficulty, *Resistance);
                }
            }
            else Text += TEXT("You can't tell how hard the lock is to pick.\n");
        }
    }
    else if (const auto* Bonus = Info.IntProperties.Find(38); Bonus && *Bonus)
        Text += FString::Printf(TEXT("Bonus to Lockpick Skill: %+d\n"), *Bonus);
    if (const auto* Sellable = Info.BoolProperties.Find(69); Sellable && !*Sellable)
        Text += TEXT("This item cannot be sold.\n");
    return Text;
}
}
