#pragma once
#include "ACETypes.h"

namespace ACERadarColors
{
    // gmRadarUI::GetBlipColor, also used by VividTargetIndicator. The default
    // is white; yellow is an explicit NPC/vendor color, not a generic item tint.
    inline uint8 Resolve(const FACEWorldObject& Object, const FACEFellowshipInfo* Fellowship = nullptr)
    {
        const int32 Flags = Object.ObjectDescriptionFlags;
        if (Flags & ACEObjectDescFlag::UiHidden) return ACERadarColor::White;
        if (Object.RadarBlipColor != ACERadarColor::Default) return Object.RadarBlipColor;
        if (Flags & ACEObjectDescFlag::Portal) return ACERadarColor::Portal;
        if (Flags & ACEObjectDescFlag::Vendor) return ACERadarColor::Vendor;
        if (!Object.bIsPlayer)
            return (Flags & ACEObjectDescFlag::Attackable) && (Object.ItemType & ACEItemType::Creature)
                ? ACERadarColor::Creature : ACERadarColor::White;

        uint8 Color = ACERadarColor::White;
        if ((Flags & ACEObjectDescFlag::Admin) && !(Flags & ACEObjectDescFlag::HiddenAdmin)) Color = ACERadarColor::Cyan;
        else if (Flags & ACEObjectDescFlag::PlayerKiller) Color = ACERadarColor::PlayerKiller;
        else if (Flags & ACEObjectDescFlag::PkLiteStatus) Color = ACERadarColor::PKLite;
        else if (Flags & ACEObjectDescFlag::FreePkStatus) Color = ACERadarColor::Creature;
        if (Fellowship && Fellowship->bValid && (Fellowship->LeaderGuid == Object.Guid ||
            Fellowship->Members.ContainsByPredicate([&](const FACEFellowshipMember& Member) { return Member.Guid == Object.Guid; })))
            Color = ACERadarColor::BrightGreen;
        return Color;
    }

    inline FLinearColor Tint(uint8 ColorId)
    {
        // Retail display RGB values, converted for Slate/material tinting.
        FColor Color = FColor::White;
        switch (ColorId)
        {
        case ACERadarColor::Blue: Color = FColor(64,168,255); break;
        case ACERadarColor::Gold: Color = FColor(255,171,0); break;
        case ACERadarColor::Purple: Color = FColor(191,99,255); break;
        case ACERadarColor::Red: Color = FColor(255,64,99); break;
        case ACERadarColor::Pink: Color = FColor(255,168,191); break;
        case ACERadarColor::Green: Color = FColor(0,128,64); break;
        case ACERadarColor::Yellow: Color = FColor(255,255,128); break;
        case ACERadarColor::Cyan: Color = FColor(0,255,255); break;
        // Retail's switch accepts decimal 10; ACE's enum also uses 0x10.
        case 10:
        case ACERadarColor::BrightGreen: Color = FColor(0,255,0); break;
        default: break;
        }
        return FLinearColor::FromSRGBColor(Color);
    }
}
