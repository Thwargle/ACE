#pragma once
#include "ACEDatSubsystem.h"

namespace ACEPluginCombat
{
inline int32 AttackHeight(UACEDatSubsystem& Dat,const FACEWorldObject& Target,const FACEPosition& Player)
{
    float Step=0,Height=1.8f,Radius=0,SelectionRadius=0;uint32 Anim=0;
    FVector3f SelectionOrigin=FVector3f::ZeroVector;
    Dat.TryGetSetupPhysics(Target.SetupId,Step,Height,Radius,Anim,nullptr,&SelectionOrigin,&SelectionRadius);
    // Retail's setup selection origin includes low bodies and elevated flyers.
    // Apply the server's scale and relative elevation, rather than a species list.
    const float Center=SelectionRadius>0?SelectionOrigin.Z:Height*.6f;
    const double AimHeight=Center*Target.GetValidObjectScale()
        +(Target.Position.ToUnrealLocation().Z-Player.ToUnrealLocation().Z)/100.;
    return AimHeight<.65?1:AimHeight>1.45?3:2;
}
}
