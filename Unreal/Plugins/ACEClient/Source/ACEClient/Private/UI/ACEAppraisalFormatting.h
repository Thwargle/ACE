#pragma once
#include "ACETypes.h"
#include "ACEDatSubsystem.h"
#include "ACERetailAllegianceTitle.h"

namespace ACEAppraisalFormatting
{
inline bool UsesCharacterExamination(const FACEAppraisalInfo& Info)
{
    // gmExaminationUI::RecvNotice_AppraisalInfo uses profession/title presence,
    // not the object's player flag (some NPCs also have character appraisals).
    return Info.bIsCreature && (Info.StringProperties.Contains(5) || Info.IntProperties.Contains(261));
}

inline FString ExaminationName(const FACEAppraisalInfo& Info)
{
    if (!UsesCharacterExamination(Info)) return Info.Name;
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
    if (UsesAmmo && FMath::IsFinite(Info.WeaponDamageMod) && Info.WeaponDamageMod > 0.0)
        Text += FString::Printf(TEXT("Damage Modifier: %.3gx\n"), Info.WeaponDamageMod);
    if (Info.WeaponTime < 0) Text += TEXT("Speed: Unknown\n");
    else
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

inline FString ItemDetails(const FACEAppraisalInfo& Info, UACEDatSubsystem* Dat = nullptr)
{
    FString Text = WeaponDetails(Info, Dat);
    auto Int = [&](uint32 Key, const TCHAR* Label)
    {
        if (const auto* Value = Info.IntProperties.Find(Key)) Text += FString::Printf(TEXT("%s%d\n"), Label, *Value);
    };
    if (Info.IntProperties.FindRef(33)) Text += TEXT("Bonded\n");
    if (Info.IntProperties.FindRef(114)) Text += TEXT("Attuned\n");
    Int(28, TEXT("Armor Level: "));
    static const TCHAR* DamageNames[] = {TEXT("Slashing"), TEXT("Piercing"), TEXT("Bludgeoning"), TEXT("Cold"), TEXT("Fire"), TEXT("Acid"), TEXT("Nether"), TEXT("Electric")};
    for (int32 I = 0; I < Info.ArmorResistances.Num() && I < UE_ARRAY_COUNT(DamageNames); ++I)
    {
        const float R = Info.ArmorResistances[I];
        // ItemExamineUI describes the material modifier, independently of AL.
        const TCHAR* Quality = R >= 2.f ? TEXT("Unparalleled") : R >= 1.6f ? TEXT("Excellent")
            : R >= 1.2f ? TEXT("Above Average") : R > .8f ? TEXT("Average")
            : R > .4f ? TEXT("Below Average") : R > 0.f ? TEXT("Poor") : TEXT("None");
        Text += FString::Printf(TEXT("%s: %s (%d)\n"), DamageNames[I], Quality,
            FMath::TruncToInt(Info.IntProperties.FindRef(28) * FMath::Clamp(R, 0.f, 2.f)));
    }
    if (const auto* N = Info.IntProperties.Find(171))
        Text += FString::Printf(TEXT("This item has been tinkered %d time%s.\n"), *N, *N == 1 ? TEXT("") : TEXT("s"));
    for (const auto& E : {TPair<uint32, const TCHAR*>(39,TEXT("Last tinkered by")),{40,TEXT("Imbued by")}})
        if (const auto* V = Info.StringProperties.Find(E.Key); V && !V->IsEmpty())
            Text += FString::Printf(TEXT("%s %s.\n"), E.Value, **V);
    if (const auto* W = Info.IntProperties.Find(105))
    {
        static const TCHAR* Quality[] = {TEXT("Unknown"),TEXT("Poorly crafted"),TEXT("Well-crafted"),TEXT("Finely crafted"),TEXT("Exquisitely crafted"),TEXT("Magnificent"),TEXT("Nearly flawless"),TEXT("Flawless"),TEXT("Utterly flawless"),TEXT("Incomparable"),TEXT("Priceless")};
        Text += FString::Printf(TEXT("Workmanship: %s (%d)\n\n"), Quality[FMath::Clamp(*W,0,10)], *W);
    }
    for (const auto& E : {TPair<uint32,const TCHAR*>(29,TEXT("Melee Defense")),{149,TEXT("Missile Defense")},{150,TEXT("Magic Defense")}})
        if (const auto* V = Info.FloatProperties.Find(E.Key))
            Text += FString::Printf(TEXT("Bonus to %s: %+.1f%%.\n\n"), E.Value, (*V-1.0)*100.0);
    TArray<FString> SpellNames;
    FString SpellDescriptions;
    TSet<int32> SeenSpells;
    for (int32 Id : Info.SpellIds)
    {
        if (SeenSpells.Contains(Id)) continue;
        SeenSpells.Add(Id);
        FString Name, Desc; uint32 Icon=0;
        if (!Dat || !Dat->TryGetSpellInfo(Id,Name,Icon)) Name=FString::Printf(TEXT("Spell %d"),Id);
        SpellNames.Add(Name);
        if (Dat && Dat->TryGetSpellDescription(Id,Desc))
            SpellDescriptions += TEXT("~ ") + Name + TEXT(": ") + Desc + TEXT("\n");
    }
    if (!SpellNames.IsEmpty()) Text += TEXT("Spells: ") + FString::Join(SpellNames,TEXT(", ")) + TEXT("\n\n");
    uint32 Imbued = 0;
    for (uint32 Id : {179u,303u,304u,305u,306u}) Imbued |= Info.IntProperties.FindRef(Id);
    TArray<FString> Properties;
    for (const auto& E : {TPair<uint32,const TCHAR*>(1,TEXT("Critical Strike")),{2,TEXT("Crippling Blow")},{4,TEXT("Armor Rending")},
        {8,TEXT("Slash Rending")},{16,TEXT("Pierce Rending")},{32,TEXT("Bludgeon Rending")},{64,TEXT("Acid Rending")},
        {128,TEXT("Cold Rending")},{256,TEXT("Lightning Rending")},{512,TEXT("Fire Rending")},{16384,TEXT("Nether Rending")},
        {1024,TEXT("+1 Melee Defense")},{2048,TEXT("+1 Missile Defense")},{4096,TEXT("+1 Magic Defense")},{0x80000000u,TEXT("Phantasmal")}})
        if (Imbued & E.Key) Properties.Add(E.Value);
    if (Info.BoolProperties.FindRef(91)) Properties.Add(TEXT("Retained"));
    if (Info.BoolProperties.FindRef(99)) Properties.Add(TEXT("Ivoryable"));
    if (Info.BoolProperties.FindRef(100)) Properties.Add(TEXT("Dyeable"));
    if (!Properties.IsEmpty()) Text += TEXT("Properties: ") + FString::Join(Properties,TEXT(", ")) + TEXT("\n");
    if (Imbued) Text += TEXT("This item cannot be further imbued.\n");
    if (!Properties.IsEmpty() || Imbued) Text += TEXT("\n");
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
    }
    if (const auto* Lore = Info.IntProperties.Find(109); Lore && *Lore > 0)
        Text += FString::Printf(TEXT("Activation requires Arcane Lore: %d\n"), *Lore);
    Int(110, TEXT("Activation requires Allegiance Rank: "));
    Text += TEXT("\n");
    if (const auto* ManaConv = Info.FloatProperties.Find(144))
        Text += FString::Printf(TEXT("Bonus to Mana Conversion: %+.0f%%.\n\n"), *ManaConv*100.0);
    if (const auto* Mod = Info.FloatProperties.Find(152))
    {
        const uint32 Type = Info.IntProperties.FindRef(45);
        FString DamageName;
        for (const auto& E : {TPair<uint32,const TCHAR*>(1,TEXT("Slashing")),{2,TEXT("Piercing")},{4,TEXT("Bludgeoning")},{8,TEXT("Cold")},
            {16,TEXT("Fire")},{32,TEXT("Acid")},{64,TEXT("Electric")},{1024,TEXT("Nether")}})
            if (Type & E.Key) { if (!DamageName.IsEmpty()) DamageName += TEXT("/"); DamageName += E.Value; }
        if (!DamageName.IsEmpty()) Text += FString::Printf(TEXT("Damage bonus for %s spells:\n vs. Monsters: %+.1f%%.\n vs. Players: %+.1f%%.\n"),
            *DamageName, (*Mod-1.0)*100.0, (*Mod-1.0)*50.0);
    }
    Int(106, TEXT("Spellcraft: "));
    if (Info.IntProperties.Contains(108)) Text += FString::Printf(TEXT("Mana: %d / %d.\n"), Info.IntProperties.FindRef(107), Info.IntProperties.FindRef(108));
    if (const auto* Rate = Info.FloatProperties.Find(5); Rate && FMath::IsFinite(*Rate) && FMath::Abs(*Rate) > SMALL_NUMBER)
        Text += FString::Printf(TEXT("Mana Cost: 1 point per %d seconds.\n"), FMath::RoundToInt(FMath::Abs(1.0 / *Rate)));
    else Int(117, TEXT("Mana Cost: "));
    if (!SpellDescriptions.IsEmpty()) Text += TEXT("\nSpell Descriptions:\n") + SpellDescriptions + TEXT("\n");
    if (const auto* Cooldown = Info.FloatProperties.Find(167)) Text += FString::Printf(TEXT("Cooldown: %.1f seconds\n"), *Cooldown);
    if (Info.IntProperties.Contains(92)) Text += FString::Printf(TEXT("Uses remaining: %d/%d\n"), Info.IntProperties.FindRef(92), Info.IntProperties.FindRef(91));
    for (const auto& Entry : {TPair<uint32, const TCHAR*>(25, TEXT("Crafted by: ")), {38, TEXT("Destination: ")}})
        if (const auto* Value = Info.StringProperties.Find(Entry.Key); Value && !Value->IsEmpty()) Text += FString(Entry.Value) + *Value + TEXT("\n");
    return Text;
}
}
