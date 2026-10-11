#pragma once

#include "ACETypes.h"

namespace ACEPetDevice
{
    inline bool IsSummoningDevice(const FACEAppraisalInfo* Appraisal)
    {
        if (!Appraisal || !Appraisal->bSuccess) return false;
        const auto& P=Appraisal->IntProperties;
        // Retail essences share cooldown 213. Custom essences can expose a
        // different cooldown, but still advertise Summoning use requirements.
        return P.FindRef(280)==213 || P.FindRef(366)==54 || P.FindRef(368)==54
            || P.FindRef(362)>0 || P.FindRef(266)>0;
    }
}
