#pragma once
#include "CoreMinimal.h"

namespace ACEPluginCastMotion
{
    // Discard a tap blocked by UI/airborne movement instead of replaying it later.
    // This bounds request freshness, not how long a backward key is held.
    constexpr double TapRequestSeconds = 0.12;
    // VT MySpell.IsInstantCast eligibility. Ordinary item/other buffs retain
    // their casting animation.
    inline bool FastBuff(uint32 School,uint32 Power,uint32 Flags,double Duration)
    {
        if(School<1||School>5||School==1||School==5)return false;
        return Power<50 || ((Flags&8)&&!(Flags&0x2000)&&Duration>=60&&(School==2||School==4));
    }
}
