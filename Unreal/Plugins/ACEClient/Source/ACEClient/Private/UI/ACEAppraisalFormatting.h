#pragma once
#include "ACETypes.h"
#include "ACEDatSubsystem.h"
#include "ACERetailAllegianceTitle.h"
#include "ACERetailObjectNames.h"
#include "ACEAppraisalUsage.h"

class UACEUIResourceResolver;

namespace ACEAppraisalFormatting
{
FString EquipmentSetName(int32 Id);
FString ItemLevelDetails(const FACEAppraisalInfo& Info);
FString RequirementEnumName(UACEDatSubsystem* Dat, uint32 Enum, int32 Value);
inline FString ManaStoneDetails(const FACEAppraisalInfo& Info)
{
    // ItemExamineUI::Appraisal_ShowManaStoneInfo: property presence (including
    // zero), and no spell book. Charges use the same fields as reusable stones.
    if (!Info.SpellIds.IsEmpty()) return FString();
    FString Text;
    if (const auto* Mana = Info.IntProperties.Find(0x6B))
        Text += FString::Printf(TEXT("Stored Mana: %d\n"), *Mana);
    for (const auto& Entry : {TPair<uint32,const TCHAR*>(0x57,TEXT("Efficiency")), {0x89,TEXT("Chance of Destruction")}})
        if (const auto* Value = Info.FloatProperties.Find(Entry.Key); Value && FMath::IsFinite(*Value))
            Text += FString::Printf(TEXT("%s: %d%%\n"), Entry.Value, FMath::TruncToInt(*Value * 100.0));
    return Text;
}

inline bool UsesCharacterExamination(const FACEAppraisalInfo& Info)
{
    // gmExaminationUI::RecvNotice_AppraisalInfo uses profession/title presence,
    // not the object's player flag (some NPCs also have character appraisals).
    return Info.bIsCreature && (Info.StringProperties.Contains(5) || Info.IntProperties.Contains(261));
}

inline FString ExaminationName(const FACEAppraisalInfo& Info)
{
    if (!UsesCharacterExamination(Info))
    {
        FACEWorldObject Object; Object.Name = Info.Name; Object.MaterialType = Info.IntProperties.FindRef(131);
        return ACERetailObjectNames::Name(Object);
    }
    const FString Rank = ACERetailAllegiance::Title(Info.IntProperties.FindRef(30),
        Info.IntProperties.FindRef(188), Info.IntProperties.FindRef(113));
    return Rank.IsEmpty() ? Info.Name : Rank + TEXT(" ") + Info.Name;
}

struct FCreatureDetailLine
{
    FString Label;
    FString Value;
    FLinearColor Color = FLinearColor::White;
};

struct FCreatureHeadings
{
    FString Type, Heritage, Profession, PlayerKiller, Allegiance;
};
FCreatureHeadings CreatureHeadings(const FACEAppraisalInfo& Info, UACEUIResourceResolver* Resources,
    const FACEWorldObject* Subject = nullptr);

inline TArray<FCreatureDetailLine> CreatureStatLines(const FACEAppraisalInfo& Info)
{
    // BasicCreatureExamineUI's retail order differs from the wire attribute bits.
    static const TCHAR* Names[] = {TEXT("Strength"), TEXT("Endurance"), TEXT("Coordination"),
        TEXT("Quickness"), TEXT("Focus"), TEXT("Self"), TEXT("Health"), TEXT("Stamina"), TEXT("Mana")};
    const int32 Values[] = {Info.Strength, Info.Endurance, Info.Coordination, Info.Quickness,
        Info.Focus, Info.Self, Info.Health, Info.Stamina, Info.Mana};
    const int32 Maxima[] = {Info.MaxHealth, Info.MaxStamina, Info.MaxMana};
    const int32 Masks[] = {1, 2, 8, 4, 16, 32, 64, 128, 256};
    TArray<FCreatureDetailLine> Lines;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
    {
        FString Value = Values[I] > 0 ? FString::FromInt(Values[I]) : TEXT("???");
        if (I >= 6)
        {
            const int32 Max = Maxima[I - 6];
            Value = TEXT("???");
            if (Max > 0)
            {
                const int32 Percent = FMath::RoundToInt(100.0 * Values[I] / Max);
                if (Info.bSuccess)
                    Value = I == 6 ? FString::Printf(TEXT("%d/%d (%d %%)"), Values[I], Max, Percent)
                        : FString::Printf(TEXT("%d/%d"), Values[I], Max);
                else if (I == 6) Value = FString::Printf(TEXT("%d %%"), Percent);
            }
        }
        FLinearColor Color = FLinearColor::White;
        if (!Info.bSuccess) Color = FLinearColor::Yellow;
        else if (Info.AttributeHighlights & Masks[I])
            Color = (Info.AttributeColors & Masks[I]) ? FLinearColor::Green : FLinearColor::Red;
        Lines.Add({Names[I], MoveTemp(Value), Color});
    }
    return Lines;
}

inline TArray<FCreatureDetailLine> CreatureDetailLines(const FACEAppraisalInfo& Info, int32 ViewerFaction = 0)
{
    TArray<FCreatureDetailLine> Lines;
    auto Add = [&](FString Label, FString Value = FString()) { Lines.Add({MoveTemp(Label), MoveTemp(Value)}); };
    auto String = [&](uint32 Key, const TCHAR* Label)
    {
        if (const auto* Value = Info.StringProperties.Find(Key); Value && !Value->IsEmpty())
            Add(Label, *Value);
    };
    const bool bCharacter = UsesCharacterExamination(Info);
    if (bCharacter)
    {
        const int32 Faction = Info.IntProperties.FindRef(281);
        for (int32 I = 0; I < 3; ++I)
        {
            if (!(Faction & (1 << I))) continue;
            static const TCHAR* Societies[] = {TEXT("Celestial Hand"), TEXT("Eldrytch Web"), TEXT("Radiant Blood")};
            const int32 Rank = Info.IntProperties.FindRef(287 + I);
            const TCHAR* RankName = Rank >= 1 && Rank <= 100 ? TEXT(" ~ Initiate") : Rank <= 300 && Rank > 0 ? TEXT(" ~ Adept")
                : Rank <= 600 && Rank > 0 ? TEXT(" ~ Knight") : Rank <= 1000 && Rank > 0 ? TEXT(" ~ Lord")
                : Rank <= 1500 && Rank > 0 ? TEXT(" ~ Master") : TEXT("");
            Add(TEXT("Society:"), FString(Societies[I]) + RankName);
            if (ViewerFaction) Lines.Last().Color = (ViewerFaction & (1 << I)) ? FLinearColor::Green : FLinearColor::Red;
            break;
        }
        if (Info.IntProperties.FindRef(30) > 0)
        {
            // Appraisal MonarchName (21) and PatronName (35) are different from
            // the character-description strings used by the allegiance panel.
            if (const auto* Monarch = Info.StringProperties.Find(21))
            {
                const auto* Patron = Info.StringProperties.Find(35);
                if (Patron && *Patron == *Monarch) Add(TEXT("Monarch/Patron:"), *Monarch);
                else
                {
                    Add(TEXT("Monarch:"), *Monarch);
                    if (Patron) Add(TEXT("Patron:"), *Patron);
                }
            }
            else
            {
                const int32 Followers = FMath::Max(0, Info.IntProperties.FindRef(35));
                Add(TEXT("Alleg. Monarch:"), FString::Printf(TEXT("%d Follower%s"), Followers, Followers == 1 ? TEXT("") : TEXT("s")));
            }
        }
        if (Info.ArmorLevels.Num() == 9 && Info.ArmorLevels.ContainsByPredicate([](int32 V) { return V > 0; }))
        {
            Add(TEXT(""));
            static const TCHAR* Parts[] = {TEXT("Head/Chest/Groin"), TEXT("Bicep/Wrist/Hand"), TEXT("Thigh/Shin/Foot")};
            for (int32 I = 0; I < 3; ++I)
            {
                TArray<FString> Armor;
                for (int32 J = 0; J < 3; ++J)
                {
                    const int32 V = Info.ArmorLevels[I * 3 + J];
                    Armor.Add(V >= 9999 ? TEXT("*") + FString::FromInt(V - 9999) : FString::FromInt(V));
                }
                Add(Parts[I], TEXT("AL: ") + FString::Join(Armor, TEXT("/")));
            }
        }
    }
    const bool bDamage = Info.DamageRating > 0 || Info.CritRating > 0 || Info.CritDamageRating > 0;
    const bool bResist = Info.DamageResistRating > 0 || Info.CritResistRating > 0 || Info.CritDamageResistRating > 0;
    const int32 Dot = Info.IntProperties.FindRef(350), Life = Info.IntProperties.FindRef(351);
    if (bDamage || bResist || Dot > 0 || Life > 0) Add(TEXT(""));
    if (bDamage) Add(TEXT("Dmg/CritDmg"), FString::Printf(TEXT("%%Rating: %d/%d"), Info.DamageRating, Info.CritDamageRating));
    if (bResist) Add(TEXT("Dmg/CritDmg"), FString::Printf(TEXT("%%Resist: %d/%d"), Info.DamageResistRating, Info.CritDamageResistRating));
    if (Dot > 0 || Life > 0) Add(TEXT("DoT/Life:"), FString::Printf(TEXT("%%Resist: %d/%d"), Dot, Life));
    if (bDamage || bResist || Dot > 0 || Life > 0) Add(TEXT(""));
    if (!bCharacter) return Lines;
    String(10, TEXT("Fellowship:"));
    String(43, TEXT("Arrived in Dereth:"));
    if (const auto* Age = Info.IntProperties.Find(125))
    {
        int32 S = FMath::Max(0, *Age);
        FString Time;
        for (const auto& Unit : {TPair<int32, const TCHAR*>(2592000, TEXT("mo")), {86400, TEXT("d")}, {3600, TEXT("h")}, {60, TEXT("m")}})
        {
            const int32 N = S / Unit.Key; S %= Unit.Key;
            if (N) Time += FString::Printf(TEXT("%d%s "), N, Unit.Value);
        }
        Add(TEXT("Time in Dereth:"), Time + FString::Printf(TEXT("%ds"), S));
    }
    if (const auto* Rank = Info.IntProperties.Find(181)) Add(TEXT("Chess Rank:"), FString::FromInt(*Rank));
    if (const auto* Skill = Info.IntProperties.Find(192)) Add(TEXT("Fishing Skill:"), FString::FromInt(*Skill));
    if (const auto* Deaths = Info.IntProperties.Find(43)) Add(TEXT("Deaths:"), *Deaths > 0 ? FString::FromInt(*Deaths) : TEXT("Has never died"));
    if (const auto* Titles = Info.IntProperties.Find(262)) Add(TEXT("Titles Earned:"), FString::FromInt(*Titles));
    // CharExamineUI always includes the armor legend, even with no marked parts.
    Add(TEXT("* = Unenchantable"));
    return Lines;
}

inline TArray<FLinearColor> ItemTextColors(const FACEAppraisalInfo& Info, const FString& Text)
{
    TArray<FLinearColor> Colors; Colors.Init(FLinearColor::White, Text.Len());
    // AppraisalProfile::InqInt/FloatEnchantmentMod: low word = changed,
    // high word = beneficial color. The server already reverses weapon time.
    auto Stat = [&](const TCHAR* Label, uint32 Flags, uint32 Bit)
    {
        if (!(Flags & Bit)) return;
        const bool Higher = (Flags & (Bit << 16)) != 0;
        const FLinearColor Color = Higher ? FLinearColor(.45f,.85f,.05f) : FLinearColor(1.f,.2f,.15f);
        int32 Start = 0;
        while ((Start = Text.Find(Label, ESearchCase::CaseSensitive, ESearchDir::FromStart, Start)) != INDEX_NONE)
        {
            if (Start == 0 || Text[Start-1] == '\n')
                for (int32 I = Start; I < Text.Len() && Text[I] != '\n'; ++I) Colors[I] = Color;
            Start += FCString::Strlen(Label);
        }
    };
    Stat(TEXT("Armor Level:"), Info.ArmorEnchantments, 1);
    Stat(TEXT("Base Shield Level:"), Info.ArmorEnchantments, 1);
    const TCHAR* Armor[] = {TEXT("Slashing:"),TEXT("Piercing:"),TEXT("Bludgeoning:"),TEXT("Cold:"),TEXT("Fire:"),TEXT("Acid:"),TEXT("Electric:"),TEXT("Nether:")};
    for (int32 I=0; I<UE_ARRAY_COUNT(Armor); ++I) Stat(Armor[I], Info.ArmorEnchantments, 2u<<I);
    Stat(TEXT("Damage:"), Info.WeaponEnchantments, 8);
    Stat(TEXT("Damage Bonus:"), Info.WeaponEnchantments, 8);
    Stat(TEXT("Damage Modifier:"), Info.WeaponEnchantments, 32);
    Stat(TEXT("Speed:"), Info.WeaponEnchantments, 4);
    Stat(TEXT("Bonus to Attack Skill:"), Info.WeaponEnchantments, 1);
    Stat(TEXT("Bonus to Melee Defense:"), Info.WeaponEnchantments, 2);
    Stat(TEXT("Bonus to Mana Conversion:"), Info.ResistanceEnchantments, 0x1000);
    Stat(TEXT("Elemental Damage Bonus:"), Info.ResistanceEnchantments, 0x2000);
    return Colors;
}

inline FString WeaponDetails(const FACEAppraisalInfo& Info, UACEDatSubsystem* Dat)
{
    if (!Info.bHasWeaponProfile)
        return !Info.bSuccess && (Info.ItemType & (ACEItemType::MeleeWeapon | ACEItemType::MissileWeapon))
            ? FString(TEXT("Damage: Unknown\nSpeed: Unknown\n\n")) : FString();
    // The wire profile already includes enchantments. Retail displays variance
    // as a fraction removed from maximum damage, and launcher multipliers apart.
    FString Text, Skill; uint32 Icon = 0;
    if (Dat) Dat->TryGetSkillInfo(Info.WeaponSkill, Skill, Icon);
    if (!Skill.IsEmpty()) Text += TEXT("Skill: ") + Skill;
    static const TCHAR* Types[] = {TEXT(""),TEXT("Unarmed Weapon"),TEXT("Sword"),TEXT("Axe"),TEXT("Mace"),TEXT("Spear"),TEXT("Dagger"),TEXT("Staff"),TEXT("Bow"),TEXT("Crossbow"),TEXT("Thrown")};
    const int32 Type = Info.IntProperties.FindRef(353);
    if (Type > 0 && Type < UE_ARRAY_COUNT(Types))
        Text += Skill.IsEmpty() ? FString(TEXT("Weapon: ")) + Types[Type] : FString::Printf(TEXT(" (%s)"), Types[Type]);
    if (!Text.IsEmpty()) Text += TEXT("\n");
    FString DamageType;
    for (const auto& E : {TPair<int32,const TCHAR*>(1,TEXT("Slashing")),{2,TEXT("Piercing")},{4,TEXT("Bludgeoning")},
        {8,TEXT("Cold")},{16,TEXT("Fire")},{32,TEXT("Acid")},{64,TEXT("Electric")},{128,TEXT("Health")},
        {256,TEXT("Stamina")},{512,TEXT("Mana")},{1024,TEXT("Nether")}})
        if (Info.DamageType & E.Key) { if (!DamageType.IsEmpty()) DamageType += TEXT("/"); DamageType += E.Value; }
    const bool Launcher = (Info.ItemType & ACEItemType::MissileWeapon) || (Info.IntProperties.FindRef(9) & ACEEquipMask::MissileWeapon);
    const bool UsesAmmo = Launcher && Info.IntProperties.FindRef(50) != 0;
    Text += UsesAmmo ? TEXT("Damage Bonus: ") : TEXT("Damage: ");
    if (Info.Damage < 0 || !FMath::IsFinite(Info.DamageVariance)) Text += TEXT("Unknown\n");
    else
    {
        const double Low = Info.Damage * (1.0 - FMath::Clamp(double(Info.DamageVariance), 0.0, 1.0));
        if (Info.Damage - Low > .0002)
            Text += Low >= 10.0 ? FString::Printf(TEXT("%.4g - %d"), Low, Info.Damage)
                : FString::Printf(TEXT("%.3g - %d"), Low, Info.Damage);
        else Text += FString::FromInt(Info.Damage);
        if (!UsesAmmo) Text += TEXT(", ") + (DamageType.IsEmpty() ? TEXT("unknown type") : DamageType);
        Text += TEXT("\n");
    }
    if (const int32 Bonus = Info.IntProperties.FindRef(204); Bonus > 0)
        Text += FString::Printf(TEXT("Elemental Damage Bonus: %d, %s\n"), Bonus, *DamageType);
    if (UsesAmmo)
        Text += Info.bSuccess && FMath::IsFinite(Info.WeaponDamageMod) && Info.WeaponDamageMod > 0.0
            ? FString::Printf(TEXT("Damage Modifier: %s%d%%.\n"), Info.WeaponDamageMod < 1.0 ? TEXT("-") : TEXT("+"),
                FMath::RoundToInt(FMath::Abs(Info.WeaponDamageMod - 1.0) * 100.0))
            : TEXT("Damage Modifier: Unknown\n");
    const bool ShowSpeed = (Info.IntProperties.FindRef(9) & 0x2500000)
        || (Info.ItemType & (ACEItemType::MeleeWeapon | ACEItemType::MissileWeapon));
    if (ShowSpeed && Info.WeaponTime < 0) Text += TEXT("Speed: Unknown\n");
    else if (ShowSpeed)
    {
        const TCHAR* Speed = Info.WeaponTime < 11 ? TEXT("Very Fast") : Info.WeaponTime < 31 ? TEXT("Fast")
            : Info.WeaponTime < 50 ? TEXT("Average") : Info.WeaponTime < 80 ? TEXT("Slow") : TEXT("Very Slow");
        Text += FString::Printf(TEXT("Speed: %s (%d)\n"), Speed, Info.WeaponTime);
    }
    if (Launcher && FMath::IsFinite(Info.WeaponMaxVelocity) && Info.WeaponMaxVelocity > 0.0)
    {
        const double Range = FMath::Min(85.0, FMath::Square(Info.WeaponMaxVelocity) * 1.094 / 9.8);
        const int32 Yards = Range >= 10.0 ? FMath::FloorToInt(Range / 5.0) * 5 : FMath::CeilToInt(Range);
        Text += FString::Printf(TEXT("Range: %d yds.%s\n"), Yards, Info.bWeaponMaxVelocityEstimated ? TEXT(" (based on STRENGTH 100)") : TEXT(""));
    }
    if (!UsesAmmo && FMath::IsFinite(Info.WeaponOffense) && !FMath::IsNearlyEqual(Info.WeaponOffense,1.f))
        Text += FString::Printf(TEXT("Bonus to Attack Skill: %+.0f%%.\n"), (Info.WeaponOffense - 1.0) * 100.0);
    if (const int32 Cleave = Info.IntProperties.FindRef(292); Cleave > 1)
        Text += FString::Printf(TEXT("Cleave: %d enemies in front arc.\n"), Cleave);
    return Text + TEXT("\n");
}

inline FString ItemDetails(const FACEAppraisalInfo& Info, UACEDatSubsystem* Dat = nullptr, const FACEPlayerVitals* Viewer = nullptr)
{
    FString Text;
    auto Int = [&](uint32 Key, const TCHAR* Label)
    {
        if (const auto* Value = Info.IntProperties.Find(Key)) Text += FString::Printf(TEXT("%s%d\n"), Label, *Value);
    };
    if (const auto* N = Info.IntProperties.Find(171))
        Text += FString::Printf(TEXT("This item has been tinkered %d time%s.\n"), *N, *N == 1 ? TEXT("") : TEXT("s"));
    for (const auto& E : {TPair<uint32, const TCHAR*>(39,TEXT("Last tinkered by")),{40,TEXT("Imbued by")}})
        if (const auto* V = Info.StringProperties.Find(E.Key); V && !V->IsEmpty())
            Text += FString::Printf(TEXT("%s %s.\n"), E.Value, **V);
    if (const auto* W = Info.IntProperties.Find(105))
    {
        static const TCHAR* Quality[] = {TEXT("Unknown"),TEXT("Poorly crafted"),TEXT("Well-crafted"),TEXT("Finely crafted"),TEXT("Exquisitely crafted"),TEXT("Magnificent"),TEXT("Nearly flawless"),TEXT("Flawless"),TEXT("Utterly flawless"),TEXT("Incomparable"),TEXT("Priceless")};
        const int32 Count=Info.IntProperties.FindRef(170);
        if (Count>0)
        {
            const double Average=double(*W)/Count;
            Text+=FString::Printf(TEXT("Workmanship: %s (%.2f)\n\nSalvaged from %d items.\n\n"),
                Quality[FMath::Clamp(FMath::RoundToInt(Average),0,10)],Average,Count);
        }
        else Text += FString::Printf(TEXT("Workmanship: %s (%d)\n\n"), Quality[FMath::Clamp(*W,0,10)], *W);
    }
    if (!Text.IsEmpty() && !Text.EndsWith(TEXT("\n\n"))) Text += TEXT("\n");
    // ItemExamineUI prints crafting, set and ratings before equipment stats.
    const FString SetName=EquipmentSetName(Info.IntProperties.FindRef(265));
    if(!SetName.IsEmpty())Text+=TEXT("Set: ")+SetName+TEXT("\n");
    TArray<FString> Ratings;
    const uint32 RatingIds[] = {370,371,372,374,373,375,376,377,378};
    const TCHAR* RatingNames[] = {TEXT("Dam"),TEXT("Dam Resist"),TEXT("Crit"),TEXT("Crit Dam"),
        TEXT("Crit Resist"),TEXT("Crit Dam Resist"),TEXT("Heal Boost"),TEXT("Nether Resist"),TEXT("Life Resist")};
    for (int32 I=0; I<UE_ARRAY_COUNT(RatingIds); ++I)
        if (const int32 Value=Info.IntProperties.FindRef(RatingIds[I]); Value>0)
            Ratings.Add(FString::Printf(TEXT("%s %d"),RatingNames[I],Value));
    if (!Ratings.IsEmpty()) AppendItemText(Text,TEXT("Ratings: ")+FString::Join(Ratings,TEXT(", ")));
    const int32 Vitality=Info.IntProperties.FindRef(379);
    if (Vitality>0) AppendItemText(Text,FString::Printf(TEXT("This item adds %d Vitality."),Vitality));
    if (!SetName.IsEmpty() || !Ratings.IsEmpty() || Vitality>0)
    { Text.TrimEndInline(); Text+=TEXT("\n\n"); }
    if ((Info.IntProperties.FindRef(9)&0x8007FFF) || (Info.ItemType&(ACEItemType::Armor|ACEItemType::Clothing)))
    {
        TArray<FString> Covers;
        const uint32 Coverage=Info.IntProperties.FindRef(4);
        for(const auto& Part:{TPair<uint32,const TCHAR*>(0x4000,TEXT("Head")),{0x408,TEXT("Chest")},{0x810,TEXT("Abdomen")},
            {0x1020,TEXT("Upper Arms")},{0x2040,TEXT("Lower Arms")},{0x8000,TEXT("Hands")},
            {0x102,TEXT("Upper Legs")},{0x204,TEXT("Lower Legs")},{0x10000,TEXT("Feet")}})
            if(Coverage&Part.Key)Covers.Add(Part.Value);
        if(!Covers.IsEmpty())Text+=TEXT("Covers ")+FString::Join(Covers,TEXT(", "))+TEXT("\n");
    }
    const bool Shield = (Info.IntProperties.FindRef(9) & ACEEquipMask::Shield) != 0;
    if (Shield)
    {
        if (const int32* Level = Info.IntProperties.Find(28))
        {
            Int(28, TEXT("Base Shield Level: "));
            if (Viewer)
            {
                int32 Limit = 0;
                for (const auto& Skill : Viewer->Skills) if (Skill.SkillId == 48)
                    Limit = Skill.AdvancementClass >= 3 ? Skill.Current : Skill.Current / 2;
                Text += FString::Printf(TEXT("Effective Shield Level : %d (with Shield skill)\n"), FMath::Min(*Level, Limit));
            }
        }
        else Text += TEXT("Shield Level: Unknown\n");
    }
    FString Stats = WeaponDetails(Info, Dat).TrimEnd();
    if (!Stats.IsEmpty()) Text += Stats + TEXT("\n");
    const bool Launcher = (Info.IntProperties.FindRef(9) & ACEEquipMask::MissileWeapon)
        || (Info.ItemType & ACEItemType::MissileWeapon);
    const bool Ammunition = (Info.IntProperties.FindRef(9) & ACEEquipMask::MissileAmmo) != 0;
    if (Launcher || Ammunition)
    {
        const int32 Ammo = Info.IntProperties.FindRef(50);
        const TCHAR* Kind = Ammo==1 ? (Launcher?TEXT("arrows"):TEXT("bows"))
            : Ammo==2 ? (Launcher?TEXT("quarrels"):TEXT("crossbows"))
            : Ammo==4 ? (Launcher?TEXT("atlatl darts"):TEXT("atlatls")) : nullptr;
        if (Kind) Text += Launcher ? FString::Printf(TEXT("Uses %s as ammunition.\n"),Kind)
            : FString::Printf(TEXT("Used as ammunition by %s.\n"),Kind);
    }
    for (const auto& E : {TPair<uint32,const TCHAR*>(29,TEXT("Melee Defense")),{149,TEXT("Missile Defense")},{150,TEXT("Magic Defense")}})
        if (const auto* V = Info.FloatProperties.Find(E.Key))
            Text += FString::Printf(TEXT("Bonus to %s: %+.1f%%.\n"), E.Value, (*V-1.0)*100.0);
    const bool HasArmor = !Info.ArmorResistances.IsEmpty() && Info.IntProperties.FindRef(28) > 0;
    if (HasArmor || (!Shield && Info.IntProperties.Contains(28)))
    {
        // Appraisal_ShowArmorMods starts with an explicit newline.
        AppendItemText(Text,FString::Printf(TEXT("Armor Level: %d"),Info.IntProperties.FindRef(28)),true);
        Text+=TEXT("\n");
    }
    static const TCHAR* DamageNames[] = {TEXT("Slashing"), TEXT("Piercing"), TEXT("Bludgeoning"), TEXT("Cold"), TEXT("Fire"), TEXT("Acid"), TEXT("Nether"), TEXT("Electric")};
    // Wire storage order differs from ItemExamineUI::Appraisal_ShowArmorMods.
    for (int32 I : {0,1,2,4,3,5,7,6})
    {
        if (!HasArmor || !Info.ArmorResistances.IsValidIndex(I)) continue;
        const float R = Info.ArmorResistances[I];
        const TCHAR* Quality = R >= 2.f ? TEXT("Unparalleled") : R >= 1.6f ? TEXT("Excellent")
            : R >= 1.2f ? TEXT("Above Average") : R > .8f ? TEXT("Average")
            : R > .4f ? TEXT("Below Average") : R > 0.f ? TEXT("Poor") : TEXT("None");
        Text += FString::Printf(TEXT("%s: %s (%d)\n"), DamageNames[I], Quality,
            FMath::TruncToInt(Info.IntProperties.FindRef(28) * FMath::Clamp(R, 0.f, 2.f)));
    }
    TArray<FString> SpellNames;
    FString SpellDescriptions;
    FString EnchantmentDescriptions;
    TSet<int32> SeenSpells;
    for (int32 WireId : Info.SpellBookEntries.IsEmpty()?Info.SpellIds:Info.SpellBookEntries)
    {
        if (!Info.bSuccess || SeenSpells.Contains(WireId)) continue;
        SeenSpells.Add(WireId);
        // Retail marks active enchantments with the high bit. They are not
        // inherent item spells, but have their own description section.
        const bool Enchantment=(uint32(WireId)&0x80000000u)!=0;
        const int32 Id=uint32(WireId)&0x7fffffffu;
        FString Name, Desc; uint32 Icon=0;
        if (!Dat || !Dat->TryGetSpellInfo(Id,Name,Icon)) Name=FString::Printf(TEXT("Spell %d"),Id);
        if (!Enchantment) SpellNames.Add(Name);
        if (Dat && Dat->TryGetSpellDescription(Id,Desc))
            (Enchantment?EnchantmentDescriptions:SpellDescriptions) += TEXT("~ ") + Name + TEXT(": ") + Desc + TEXT("\n");
    }
    if (!SpellNames.IsEmpty())
        AppendItemText(Text, TEXT("Spells: ") + FString::Join(SpellNames,TEXT(", ")), true);
    else if (!Info.bSuccess && !Info.SpellIds.IsEmpty())
        AppendItemText(Text,TEXT("Spells: unknown."),true);
    uint32 Imbued = 0;
    for (uint32 Id : {179u,303u,304u,305u,306u}) Imbued |= Info.IntProperties.FindRef(Id);
    TArray<FString> Properties;
    if (Info.IntProperties.FindRef(33)) Properties.Add(TEXT("Bonded"));
    if (Info.IntProperties.FindRef(114)) Properties.Add(TEXT("Attuned"));
    for (const auto& E : {TPair<uint32,const TCHAR*>(1,TEXT("Critical Strike")),{2,TEXT("Crippling Blow")},{4,TEXT("Armor Rending")},
        {8,TEXT("Slash Rending")},{16,TEXT("Pierce Rending")},{32,TEXT("Bludgeon Rending")},{64,TEXT("Acid Rending")},
        {128,TEXT("Cold Rending")},{256,TEXT("Lightning Rending")},{512,TEXT("Fire Rending")},{16384,TEXT("Nether Rending")},
        {1024,TEXT("+1 Melee Defense")},{2048,TEXT("+1 Missile Defense")},{4096,TEXT("+1 Magic Defense")},{0x80000000u,TEXT("Phantasmal")}})
        if (Imbued & E.Key) Properties.Add(E.Value);
    if (Info.BoolProperties.FindRef(91)) Properties.Add(TEXT("Retained"));
    for (const auto& Property : {TPair<uint32,const TCHAR*>(136,TEXT("Crushing Blow")),
        {147,TEXT("Biting Strike")},{155,TEXT("Armor Cleaving")}})
        if (Info.FloatProperties.Contains(Property.Key)) Properties.Add(Property.Value);
    // Retail ItemExamineUI::Appraisal_ShowSpecialProperties checks presence of
    // ResistanceModifier (float 157) and ResistanceModifierType (int 263).
    // This is independent of imbued/rending flags and the weapon's damage type.
    if (Info.FloatProperties.Contains(157))
    {
        if (const int32* ResistanceType = Info.IntProperties.Find(263))
        {
            TArray<FString> Names;
            for (const auto& E : {TPair<uint32,const TCHAR*>(1,TEXT("Slashing")),{2,TEXT("Piercing")},{4,TEXT("Bludgeoning")},
                {8,TEXT("Cold")},{16,TEXT("Fire")},{32,TEXT("Acid")},{64,TEXT("Electrical")},{1024,TEXT("Nether")},{0x10000000u,TEXT("Prismatic")}})
                if (uint32(*ResistanceType) & E.Key) Names.Add(E.Value);
            Properties.Add(TEXT("Resistance Cleaving: ") + FString::Join(Names,TEXT("/")));
        }
    }
    if (Info.BoolProperties.FindRef(99)) Properties.Add(TEXT("Ivoryable"));
    if (Info.BoolProperties.FindRef(100)) Properties.Add(TEXT("Dyeable"));
    if (!Properties.IsEmpty()) AppendItemText(Text, TEXT("Properties: ") + FString::Join(Properties,TEXT(", ")), true);
    if (Imbued) AppendItemText(Text, TEXT("This item cannot be further imbued."));
    const bool Tethered=Info.BoolProperties.FindRef(130);
    if (Tethered) AppendItemText(Text,TEXT("This item is tethered to the left side."));
    // Usage is a separate section before requirements, not a description at the end.
    if (const auto* Usage = Info.StringProperties.Find(14); Usage && !Usage->IsEmpty())
        AppendItemText(Text, *Usage, true);
    if (!Text.IsEmpty())
    {
        Text.TrimEndInline();
        // Appraisal_ShowSpecialProperties reserves a blank line even when
        // there are no special properties between the spells and requirements.
        const bool HasUsage=!Info.StringProperties.FindRef(14).IsEmpty();
        Text += (!Properties.IsEmpty() || Imbued || Tethered || !Info.SpellIds.IsEmpty()) && !HasUsage ? TEXT("\n\n") : TEXT("\n");
    }
    const int32 MinLevel=Info.IntProperties.FindRef(86), MaxLevel=Info.IntProperties.FindRef(87);
    FString LevelLimit;
    if (MinLevel>0 && MaxLevel>0)
        LevelLimit=MinLevel==MaxLevel?FString::Printf(TEXT("Restricted to characters of Level %d."),MinLevel)
            :FString::Printf(TEXT("Restricted to characters of Levels %d to %d."),MinLevel,MaxLevel);
    else if (MinLevel>0) LevelLimit=FString::Printf(TEXT("Restricted to characters of Level %d or greater."),MinLevel);
    else if (MaxLevel>0) LevelLimit=FString::Printf(TEXT("Restricted to characters of Level %d or below."),MaxLevel);
    if (!LevelLimit.IsEmpty()) { AppendItemText(Text,LevelLimit,true); Text+=TEXT("\n"); }
    if (const auto* Destination=Info.StringProperties.Find(38); Destination && !Destination->IsEmpty())
    { AppendItemText(Text,TEXT("Destination: ")+*Destination,true); Text+=TEXT("\n"); }
    if (Info.BoolProperties.FindRef(85))
        Text+=TEXT("Wield requires ")+(Info.StringProperties.FindRef(25).IsEmpty()?FString(TEXT("the original owner")):Info.StringProperties[25])+TEXT("\n");
    if (Info.IntProperties.FindRef(26)==1) Text+=TEXT("Use requires Throne of Destiny.\n");
    if (const int32 Heritage = Info.IntProperties.FindRef(324); Heritage)
        Text += TEXT("Wield requires ") + RequirementEnumName(Dat, 0x10000002, Heritage) + TEXT("\n");
    for (uint32 Base : {158u, 270u, 273u, 276u})
    {
        const int32 Type = Info.IntProperties.FindRef(Base), Stat = Info.IntProperties.FindRef(Base+1), Value = Info.IntProperties.FindRef(Base+2);
        if (!Type) continue;
        FString Name;
        uint32 Icon = 0;
        if ((Type == 1 || Type == 2 || Type == 8) && Dat) Dat->TryGetSkillInfo(Stat, Name, Icon);
        if (Type == 7) Text += FString::Printf(TEXT("Wield requires Level %d\n"), Value);
        else if (Type == 8 && !Name.IsEmpty()) Text += FString::Printf(TEXT("Wield requires %s %s\n"), Value >= 3 ? TEXT("Specialized") : TEXT("Trained"), *Name);
        else if (Type == 1 || Type == 2)
            Text += FString::Printf(TEXT("Wield requires %s%s %d\n"), Type == 2 ? TEXT("base ") : TEXT(""), Name.IsEmpty() ? TEXT("skill") : *Name, Value);
        else if (Type >= 3 && Type <= 6)
        {
            static const TCHAR* Attributes[] = {TEXT(""),TEXT("Strength"),TEXT("Endurance"),TEXT("Quickness"),TEXT("Coordination"),TEXT("Focus"),TEXT("Self")};
            static const TCHAR* Vitals[] = {TEXT(""),TEXT("Health"),TEXT("Health"),TEXT("Stamina"),TEXT("Stamina"),TEXT("Mana"),TEXT("Mana")};
            if (Stat > 0 && Stat < 7) Text += FString::Printf(TEXT("Wield requires %s%s %d\n"), (Type == 4 || Type == 6) ? TEXT("base ") : TEXT(""), Type < 5 ? Attributes[Stat] : Vitals[Stat], Value);
        }
        else if (Type == 9 || Type == 10)
        {
            const TCHAR* Standing = Stat == 287 ? TEXT("Standing with the Celestial Hand")
                : Stat == 288 ? TEXT("Standing with the Eldrytch Web") : Stat == 289 ? TEXT("Standing with the Radiant Blood") : TEXT("unknown quality");
            Text += FString::Printf(TEXT("Wield requires %s %d\n"), Standing, Value);
        }
        else if (Type == 11 || Type == 12)
            Text += FString::Printf(TEXT("Wield requires %s %s\n"), *RequirementEnumName(Dat, Type == 11 ? 0x10000005 : 0x10000002, Value), Type == 11 ? TEXT("type") : TEXT("race"));
    }
    FString UseLimits;
    if (const int32 Level=Info.IntProperties.FindRef(369); Level>0)
        AppendItemText(UseLimits,FString::Printf(TEXT("Use requires level %d."),Level));
    for (uint32 Key : {366u,368u})
    {
        const int32 Skill=Info.IntProperties.FindRef(Key), Minimum=Info.IntProperties.FindRef(367);
        if (!Skill || (Key==366 && !Minimum)) continue;
        FString Name; uint32 Icon=0;
        if (!Dat || !Dat->TryGetSkillInfo(Skill,Name,Icon)) Name=TEXT("Unknown Skill");
        AppendItemText(UseLimits,Key==368?FString::Printf(TEXT("Use requires specialized %s."),*Name)
            :FString::Printf(TEXT("Use requires %s of at least %d."),*Name,Minimum));
    }
    if (!UseLimits.IsEmpty()) { AppendItemText(Text,UseLimits,true); Text+=TEXT("\n"); }
    if (const FString Levels=ItemLevelDetails(Info); !Levels.IsEmpty())
    { AppendItemText(Text,Levels); Text+=TEXT("\n\n"); }
    if (const auto* Lore = Info.IntProperties.Find(109); Lore && *Lore > 0)
        Text += FString::Printf(TEXT("Activation requires Arcane Lore: %d\n"), *Lore);
    Int(110, TEXT("Activation requires Allegiance Rank: "));
    if (const int32 Heritage = Info.IntProperties.FindRef(188); Heritage)
        Text += TEXT("Activation requires ") + RequirementEnumName(Dat, 0x10000002, Heritage) + TEXT("\n");
    // Retail Appraisal_ShowActivationRequirements also displays the independent
    // skill, attribute and vital limits; these are not additional wield slots.
    if (const int32 Limit = Info.IntProperties.FindRef(115); Limit > 0)
    {
        FString Name; uint32 Icon = 0;
        const int32 Skill = Info.IntProperties.FindRef(176);
        if (Dat) Dat->TryGetSkillInfo(Skill, Name, Icon);
        if (Name.IsEmpty()) Name = FString::Printf(TEXT("Skill %d"), Skill);
        Text += FString::Printf(TEXT("Activation requires %s: %d\n"), *Name, Limit);
    }
    for (uint32 Base : {257u, 259u})
    {
        const int32 Stat = Info.IntProperties.FindRef(Base), Limit = Info.IntProperties.FindRef(Base+1);
        static const TCHAR* Attributes[] = {TEXT(""),TEXT("Strength"),TEXT("Endurance"),TEXT("Quickness"),TEXT("Coordination"),TEXT("Focus"),TEXT("Self")};
        static const TCHAR* Vitals[] = {TEXT(""),TEXT("Health"),TEXT("Health"),TEXT("Stamina"),TEXT("Stamina"),TEXT("Mana"),TEXT("Mana")};
        if (Limit > 0 && Stat > 0 && Stat < 7)
            Text += FString::Printf(TEXT("Activation requires %s: %d\n"), Base == 257 ? Attributes[Stat] : Vitals[Stat], Limit);
    }
    if (Info.BoolProperties.FindRef(94))
    {
        const FString Owner = Info.StringProperties.FindRef(25);
        Text += TEXT("This item can only be activated by ") + (Owner.IsEmpty() ? TEXT("the original owner") : Owner) + TEXT(".\n");
    }
    if (const auto* ManaConv = Info.FloatProperties.Find(144))
    {
        AppendItemText(Text, FString::Printf(TEXT("Bonus to Mana Conversion: %+.0f%%."), *ManaConv*100.0), true);
        Text += TEXT("\n");
    }
    if (const auto* Mod = Info.FloatProperties.Find(152))
    {
        const uint32 Type = Info.IntProperties.FindRef(45);
        FString DamageName;
        for (const auto& E : {TPair<uint32,const TCHAR*>(1,TEXT("Slashing")),{2,TEXT("Piercing")},{4,TEXT("Bludgeoning")},{8,TEXT("Cold")},
            {16,TEXT("Fire")},{32,TEXT("Acid")},{64,TEXT("Electric")},{1024,TEXT("Nether")}})
            if (Type & E.Key) { if (!DamageName.IsEmpty()) DamageName += TEXT("/"); DamageName += E.Value; }
        if (!DamageName.IsEmpty())
        {
            AppendItemText(Text, FString::Printf(TEXT("Damage bonus for %s spells:\n vs. Monsters: %+.1f%%.\n vs. Players: %+.1f%%."),
                *DamageName, (*Mod-1.0)*100.0, (*Mod-1.0)*50.0), true);
            Text += TEXT("\n");
        }
    }
    AppendItemText(Text, ItemUsageDetails(Info), true);
    AppendItemText(Text, ManaStoneDetails(Info));
    if (!Text.IsEmpty()) { Text.TrimEndInline(); Text += TEXT("\n"); }
    if (const auto* Cooldown = Info.FloatProperties.Find(167)) Text += FString::Printf(TEXT("Cooldown: %.1f seconds\n"), *Cooldown);
    if (Info.IntProperties.Contains(92)) Text += FString::Printf(TEXT("Uses remaining: %d/%d\n"), Info.IntProperties.FindRef(92), Info.IntProperties.FindRef(91));
    if (const auto* Craftsman=Info.StringProperties.Find(25); Craftsman && !Craftsman->IsEmpty()) Text+=TEXT("Crafted by: ")+*Craftsman+TEXT("\n");
    if (!SpellNames.IsEmpty())
    {
        if (const auto* Craft=Info.IntProperties.Find(106)) Text+=FString::Printf(TEXT("Spellcraft: %d.\n"),*Craft);
        if (Info.IntProperties.Contains(107) && Info.IntProperties.Contains(108)) Text+=FString::Printf(TEXT("Mana: %d / %d.\n"),Info.IntProperties[107],Info.IntProperties[108]);
        if (const auto* Rate=Info.FloatProperties.Find(5); Rate && FMath::IsFinite(*Rate) && FMath::Abs(*Rate)>SMALL_NUMBER)
            Text+=FString::Printf(TEXT("Mana Cost: 1 point per %d seconds.\n"),FMath::RoundToInt(FMath::Abs(1.0 / *Rate)));
        else if (const auto* Cost=Info.IntProperties.Find(117))
            AppendItemText(Text,FString::Printf(TEXT("Mana Cost: %d."),*Cost)+(*Cost>0?TEXT("\n(Can be reduced by the Mana Conversion skill)"):TEXT("")),true);
    }
    if (!SpellDescriptions.IsEmpty()) AppendItemText(Text, TEXT("Spell Descriptions:\n") + SpellDescriptions, true);
    if (!EnchantmentDescriptions.IsEmpty()) AppendItemText(Text,TEXT("Enchantments:\n\n")+EnchantmentDescriptions,true);
    return Text;
}
inline FString ItemExaminationText(const FACEAppraisalInfo& Info,UACEDatSubsystem* Dat,bool IncludeValue=true,bool IncludeBurden=true,const FACEPlayerVitals* Viewer=nullptr)
{
    FString Body;
    if(IncludeValue)AppendItemText(Body,Info.bHasValue?TEXT("Value: ")+FText::AsNumber(Info.Value).ToString():TEXT("Value: ???"));
    if(IncludeBurden)AppendItemText(Body,Info.bHasBurden?TEXT("Burden: ")+FText::AsNumber(Info.Burden).ToString():TEXT("Burden: Unknown"));
    const bool HasTinkering=Info.IntProperties.Contains(171) || Info.IntProperties.Contains(105)
        || Info.StringProperties.Contains(39) || Info.StringProperties.Contains(40);
    AppendItemText(Body,ItemDetails(Info,Dat,Viewer),!HasTinkering);
    // Appraisal_ShowDescription prefers LongDesc, falling back to ShortDesc.
    // The protocol Summary concatenates all strings in wire order, so it cannot
    // supply retail paragraph boundaries (and can repeat both descriptions).
    FString RemainingSummary=Info.Summary.IsEmpty() && !Info.bSuccess
        ? TEXT("You fail to appraise the item.") : Info.Summary;
    for(uint32 Id:{14u,15u,16u})
        if(const auto* Value=Info.StringProperties.Find(Id);Value && !Value->IsEmpty())
            RemainingSummary.ReplaceInline(**Value,TEXT(""),ESearchCase::CaseSensitive);
    {
        TArray<FString> Lines;RemainingSummary.ParseIntoArrayLines(Lines,false);
        Lines.RemoveAll([](const FString& Line){return Line.StartsWith(TEXT("Spells (")) || Line.StartsWith(TEXT("  Spell "))
            || (Line.StartsWith(TEXT("Damage ")) && Line.Contains(TEXT("  Speed ")) && Line.Contains(TEXT("  Offense ")));});
        AppendItemText(Body,FString::Join(Lines,TEXT("\n")),true);
    }
    const FString* Description=Info.StringProperties.Find(16);
    if(!Description || Description->IsEmpty())Description=Info.StringProperties.Find(15);
    if(Description)
    {
        FString Decorated = *Description;
        if (Description == Info.StringProperties.Find(16))
        {
            if (const auto* Plating = Info.StringProperties.Find(52)) Decorated = *Plating;
            if (const int32* Decoration = Info.IntProperties.Find(172))
            {
                FString Prefix;
                if ((*Decoration & 1) && Info.IntProperties.Contains(105))
                {
                    static const TCHAR* Quality[] = {TEXT("Unknown"),TEXT("Poorly crafted"),TEXT("Well-crafted"),TEXT("Finely crafted"),TEXT("Exquisitely crafted"),TEXT("Magnificent"),TEXT("Nearly flawless"),TEXT("Flawless"),TEXT("Utterly flawless"),TEXT("Incomparable"),TEXT("Priceless")};
                    Prefix = FString(Quality[FMath::Clamp(Info.IntProperties[105],0,10)]) + TEXT(" ");
                }
                const int32 Material = Info.IntProperties.FindRef(131);
                if (Material > 0 && Material <= 77)
                {
                    const FString Name = ACERetailObjectNames::GetMaterialTypeName(Material);
                    Decorated.ReplaceInline(*Name,TEXT(""),ESearchCase::CaseSensitive);
                    Decorated.TrimStartAndEndInline(); Prefix += Name + TEXT(" ");
                }
                Decorated = Prefix + Decorated;
                const int32 Gems = Info.IntProperties.FindRef(177), GemType = Info.IntProperties.FindRef(178);
                if ((*Decoration & 4) && Gems > 0 && GemType > 0 && GemType <= 77)
                {
                    FString GemName = ACERetailObjectNames::GetMaterialTypeName(GemType);
                    if (Gems != 1)
                    {
                        // AppraisalSystem::InqPluralizedGemName uses material-specific grammar.
                        switch (GemType)
                        {
                        case 38: GemName=TEXT("Rubies"); break;
                        case 11: case 24: case 27: case 29: case 32: case 36:
                        case 37: case 40: case 45: case 46: GemName=TEXT("pieces of ")+GemName; break;
                        case 26: case 49: GemName+=TEXT("es"); break;
                        case 28: break;
                        default: GemName+=TEXT("s"); break;
                        }
                    }
                    Decorated += FString::Printf(TEXT(", set with %d %s"),Gems,*GemName);
                }
            }
        }
        AppendItemText(Body,Decorated,true);
    }
    return Body;
}
}
