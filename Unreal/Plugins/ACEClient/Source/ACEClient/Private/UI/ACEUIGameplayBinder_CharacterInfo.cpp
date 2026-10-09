#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIResourceResolver.h"
#include "Misc/DateTime.h"
#include <ctime>

FString UACEUIGameplayBinder::BuildCharacterInformation()
{
    if (Client) LastVitals=Client->GetPlayerVitals();
    const auto& V=LastVitals;
    const auto Int=[&V](int32 Property) { return V.StatQualityInts.FindRef(Property); };
    FString Text;
    if (const int32 Birth=Int(98); Birth>0)
    {
        const time_t Timestamp=Birth;
        tm Local{};
#if PLATFORM_WINDOWS
        const bool Valid=localtime_s(&Local,&Timestamp)==0;
#else
        const bool Valid=localtime_r(&Timestamp,&Local)!=nullptr;
#endif
        if (Valid) Text+=FString::Printf(TEXT("You were born on %d/%d/%d %d:%02d:%02d %s.\n"),
            Local.tm_mon+1,Local.tm_mday,Local.tm_year+1900,Local.tm_hour%12 ? Local.tm_hour%12 : 12,
            Local.tm_min,Local.tm_sec,Local.tm_hour>=12 ? TEXT("PM") : TEXT("AM"));
    }
    const int32 Age=FMath::Max(0,V.AgeSeconds);
    auto Unit=[](int32 Count,const TCHAR* Name) { return FString::Printf(TEXT("%d %s%s"),Count,Name,Count==1?TEXT(""):TEXT("s")); };
    Text+=FString::Printf(TEXT("You have played for %s %s %s %s.\nYou have died %s.\n\n"),
        *Unit(Age/86400,TEXT("day")),*Unit(Age/3600%24,TEXT("hour")),*Unit(Age/60%60,TEXT("minute")),*Unit(Age%60,TEXT("second")),*Unit(V.NumDeaths,TEXT("time")));
    const TCHAR* Descriptions[]={TEXT("None"),TEXT("Poor"),TEXT("Mediocre"),TEXT("Hardy"),TEXT("Resilient"),TEXT("Indomitable")};
    const int32 Sum=V.Strength+V.Endurance, Regen=V.Strength+2*V.Endurance;
    const int32 R=Sum<=200?0:Sum<=260?1:Sum<=320?2:Sum<=380?3:Sum<=440?4:5;
    const int32 G=Regen<=200?0:Regen<=346?1:Regen<=470?2:Regen<=580?3:Regen<=690?4:5;
    Text+=FString::Printf(TEXT("Natural Resistances: %s\nDrain Resistances: %s\nRegeneration Bonus: %s\n\n"),Descriptions[R],Descriptions[R],Descriptions[G]);
    Text+=FString::Printf(TEXT("Innate Strength: %d\nInnate Endurance: %d\nInnate Coordination: %d\nInnate Quickness: %d\nInnate Focus: %d\nInnate Self: %d\n\n"),
        V.Strength-V.StrengthRanks,V.Endurance-V.EnduranceRanks,V.Coordination-V.CoordinationRanks,
        V.Quickness-V.QuicknessRanks,V.Focus-V.FocusRanks,V.Self-V.SelfRanks);
    Text+=FString::Printf(TEXT("Chess Rank: %s\nFishing Skill: %d\n\n"),*FText::AsNumber(V.ChessRank).ToString(),V.FishingSkill);
    const TCHAR* Masteries[]={TEXT("Unknown"),TEXT("Unarmed Weapons"),TEXT("Swords"),TEXT("Axes"),TEXT("Maces"),TEXT("Spears"),TEXT("Daggers"),TEXT("Staves"),TEXT("Bows"),TEXT("Crossbows"),TEXT("Thrown Weapons"),TEXT("Two Handed Weapons"),TEXT("Magical Spells")};
    for (int32 P : {354,355}) if (const int32 M=Int(P); M>0)
        Text+=FString::Printf(TEXT("Your %s mastery is %s.\n"),P==354?TEXT("melee"):TEXT("ranged"),Masteries[M<UE_ARRAY_COUNT(Masteries)?M:0]);
    if (const int32 M=Int(362); M>0)
        Text+=FString::Printf(TEXT("\nYour summoning mastery is %s.\n"),M==1?TEXT("Primalist"):M==2?TEXT("Necromancer"):M==3?TEXT("Naturalist"):TEXT("Unknown"));
    // gmCharacterInfoUI::UpdateAugmentations uses the localized descriptions,
    // including their paragraph breaks, rather than displaying raw quality names.
    if (!bLoadedSocialStrings && Canvas && Canvas->GetResourceResolver())
        if (const auto* Dat = Canvas->GetResourceResolver()->GetDatSubsystem())
            bLoadedSocialStrings = SocialStrings.LoadStrings(Dat->GetDatDirectory(), 0x23000001);
    auto AppendAugmentation = [&](const TCHAR* Key, int32 Count)
    {
        FString Description = SocialStrings.FormatText(Key, {{TEXT("NUM_AUGMENTATIONS"), FString::FromInt(Count)}});
        // Retail StringInfo chooses count-dependent text such as {time[1]|times}.
        int32 Start = 0;
        while ((Start = Description.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start)) != INDEX_NONE)
        {
            const int32 End = Description.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
            if (End == INDEX_NONE) break;
            TArray<FString> Choices;
            Description.Mid(Start + 1, End - Start - 1).ParseIntoArray(Choices, TEXT("|"), false);
            FString Selected;
            for (const FString& Choice : Choices)
            {
                int32 Condition = INDEX_NONE;
                if (Choice.EndsWith(TEXT("]")) && Choice.FindLastChar(TEXT('['), Condition))
                {
                    const FString Number = Choice.Mid(Condition + 1, Choice.Len() - Condition - 2);
                    if (Number.IsNumeric() && FCString::Atoi(*Number) == Count) { Selected = Choice.Left(Condition); break; }
                }
                else Selected = Choice;
            }
            Description = Description.Left(Start) + Selected + Description.Mid(End + 1);
            Start += Selected.Len();
        }
        Text += Description;
    };
    AppendAugmentation(TEXT("ID_CharacterInfo_Luminance_Header"), 0);
    struct FLuminanceInfo { int32 Property; const TCHAR* Base; const TCHAR* Specialized; int32 Multiplier; };
    const FLuminanceInfo Luminance[] = {
        {333, TEXT("Damage"), TEXT("Damage"), 1},
        {334, TEXT("Reduction"), TEXT("Reduction"), 1},
        {335, TEXT("Crit_Damage"), TEXT("Crit_Damage"), 1},
        {336, TEXT("Crit_Reduction"), TEXT("Crit_Reduction"), 1},
        {338, TEXT("Surge_Chance"), nullptr, 1},
        {339, TEXT("Mana_Use"), nullptr, 5},
        {340, TEXT("Mana_Gain"), nullptr, 5},
        {342, TEXT("Healing"), nullptr, 1},
        {343, TEXT("Skilled_Craft"), nullptr, 1},
        {344, nullptr, TEXT("Skilled_Spec"), 2},
        {365, TEXT("All_Skills"), nullptr, 1}
    };
    for (const auto& A : Luminance)
    {
        const int32 Count = Int(A.Property);
        if (Count <= 0) continue;
        if (A.Base)
            AppendAugmentation(*FString::Printf(TEXT("ID_CharacterInfo_Luminance_Base_%s"), A.Base),
                (A.Specialized ? FMath::Min(Count, 5) : Count) * A.Multiplier);
        if (A.Specialized && (!A.Base || Count > 5))
            AppendAugmentation(*FString::Printf(TEXT("ID_CharacterInfo_Luminance_Spec_%s"), A.Specialized),
                (A.Base ? Count - 5 : Count) * A.Multiplier);
    }
    struct FAugmentationInfo { int32 Property; const TCHAR* StringKey; };
    const FAugmentationInfo Augmentations[] = {
        {218, TEXT("ID_CharacterInfo_Augmentation_Attribute_Strength")},
        {219, TEXT("ID_CharacterInfo_Augmentation_Attribute_Endurance")},
        {220, TEXT("ID_CharacterInfo_Augmentation_Attribute_Coordination")},
        {221, TEXT("ID_CharacterInfo_Augmentation_Attribute_Quickness")},
        {222, TEXT("ID_CharacterInfo_Augmentation_Attribute_Focus")},
        {223, TEXT("ID_CharacterInfo_Augmentation_Attribute_Self")},
        {240, TEXT("ID_CharacterInfo_Augmentation_Resist_Slash")},
        {241, TEXT("ID_CharacterInfo_Augmentation_Resist_Pierce")},
        {242, TEXT("ID_CharacterInfo_Augmentation_Resist_Blunt")},
        {243, TEXT("ID_CharacterInfo_Augmentation_Resist_Acid")},
        {327, TEXT("ID_CharacterInfo_Augmentation_Resist_Nether")},
        {244, TEXT("ID_CharacterInfo_Augmentation_Resist_Fire")},
        {245, TEXT("ID_CharacterInfo_Augmentation_Resist_Frost")},
        {246, TEXT("ID_CharacterInfo_Augmentation_Resist_Lightning")},
        {224, TEXT("ID_CharacterInfo_Augmentation_Spec_Salvaging")},
        {225, TEXT("ID_CharacterInfo_Augmentation_Spec_ItemTinkering")},
        {226, TEXT("ID_CharacterInfo_Augmentation_Spec_ArmorTinkering")},
        {227, TEXT("ID_CharacterInfo_Augmentation_Spec_MagicItemTinkering")},
        {228, TEXT("ID_CharacterInfo_Augmentation_Spec_WeaponTinkering")},
        {293, TEXT("ID_CharacterInfo_Augmentation_Spec_Gearcraft")},
        {229, TEXT("ID_CharacterInfo_Augmentation_ExtraPackSlot")},
        {230, TEXT("ID_CharacterInfo_Augmentation_IncreasedCarryingCapacity")},
        {231, TEXT("ID_CharacterInfo_Augmentation_LessDeathItemLoss")},
        {232, TEXT("ID_CharacterInfo_Augmentation_SpellsRemainPastDeath")},
        {233, TEXT("ID_CharacterInfo_Augmentation_CriticalDefense")},
        {234, TEXT("ID_CharacterInfo_Augmentation_BonusXP")},
        {235, TEXT("ID_CharacterInfo_Augmentation_BonusSalvage")},
        {236, TEXT("ID_CharacterInfo_Augmentation_BonusImbueChance")},
        {237, TEXT("ID_CharacterInfo_Augmentation_FasterRegen")},
        {238, TEXT("ID_CharacterInfo_Augmentation_IncreasedSpellDuration")},
        {294, TEXT("ID_CharacterInfo_Augmentation_Infused_CreatureMagic")},
        {295, TEXT("ID_CharacterInfo_Augmentation_Infused_ItemMagic")},
        {296, TEXT("ID_CharacterInfo_Augmentation_Infused_LifeMagic")},
        {297, TEXT("ID_CharacterInfo_Augmentation_Infused_WarMagic")},
        {328, TEXT("ID_CharacterInfo_Augmentation_Infused_VoidMagic")},
        {300, TEXT("ID_CharacterInfo_Augmentation_SkilledMelee")},
        {301, TEXT("ID_CharacterInfo_Augmentation_SkilledMissile")},
        {302, TEXT("ID_CharacterInfo_Augmentation_SkilledMagic")},
        {309, TEXT("ID_CharacterInfo_Augmentation_DamageBonus")},
        {310, TEXT("ID_CharacterInfo_Augmentation_DamageResist")},
        {298, TEXT("ID_CharacterInfo_Augmentation_CriticalExpertise")},
        {299, TEXT("ID_CharacterInfo_Augmentation_CriticalPower")},
        {326, TEXT("ID_CharacterInfo_Augmentation_JackOfAllTrades")},
    };
    for (const auto& A : Augmentations)
        if (const int32 Count = Int(A.Property); Count > 0) AppendAugmentation(A.StringKey, Count);
    Text += TEXT("\n");
    int32 Burden=0;
    if (Client) Client->TryGetPlayerEncumbrance(Burden);
    const int32 Capacity=FMath::Max(1,(150+FMath::Clamp(30*V.CarryingCapacityAugs,0,150))*V.GetBuffedStrength());
    if (Burden<=Capacity) Text+=TEXT("You are not overburdened at this time.\n");
    else Text+=FString::Printf(TEXT("You are overburdened by %s burden units.\n"),*FText::AsNumber(Burden-Capacity).ToString());
    if (V.CarryingCapacityAugs>0) Text+=FString::Printf(TEXT("You have been augmented %d times with the Might of the Seventh Mule. This has increased your carrying capacity by %d%%."),V.CarryingCapacityAugs,20*V.CarryingCapacityAugs);
    return Text;
}
