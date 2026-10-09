#pragma once
#include "ACETypes.h"

namespace ACEScrollLearning
{
    // Learning is not casting: retail exempts I/VII, and ACE also exempts VIII.
    // Other scrolls require a trained school and current skill >= power - 50.
    // This predicts automation eligibility; the server still authorizes Use.
    inline bool CanLearn(uint32 Power, uint32 School, const FACEPlayerVitals& Vitals)
    {
        if (Power < 50 || Power >= 300) return true;
        constexpr int32 Schools[] = {0, 34, 33, 32, 31, 43};
        if (School == 0 || School >= UE_ARRAY_COUNT(Schools)) return false;
        for (const auto& Skill : Vitals.Skills)
            if (Skill.SkillId == Schools[School])
                return Skill.AdvancementClass >= 2 && Skill.Current >= int32(Power - 50);
        return false;
    }
}
