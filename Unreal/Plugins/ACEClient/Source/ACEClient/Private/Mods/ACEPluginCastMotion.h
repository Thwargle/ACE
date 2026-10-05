#pragma once
#include "CoreMinimal.h"

namespace ACEPluginCastMotion
{
    // VT MySpell.IsInstantCast and gj/u: hold backward for fast non-war,
    // non-void casts. Ordinary item/other buffs retain their casting animation.
    inline bool FastBuff(uint32 School,uint32 Power,uint32 Flags,double Duration)
    {
        if(School<1||School>5||School==1||School==5)return false;
        return Power<50 || ((Flags&8)&&!(Flags&0x2000)&&Duration>=60&&(School==2||School==4));
    }
}
