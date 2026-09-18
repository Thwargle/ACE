#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
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
    Text+=FString::Printf(TEXT("Chess Rank: %s\nFishing Skill: %d\n\n"),*FText::AsNumber(V.ChessRank>0?V.ChessRank:1400).ToString(),V.FishingSkill);
    const TCHAR* Masteries[]={TEXT("Unknown"),TEXT("Unarmed Weapons"),TEXT("Swords"),TEXT("Axes"),TEXT("Maces"),TEXT("Spears"),TEXT("Daggers"),TEXT("Staves"),TEXT("Bows"),TEXT("Crossbows"),TEXT("Thrown Weapons"),TEXT("Two Handed Weapons"),TEXT("Magical Spells")};
    for (int32 P : {354,355}) if (const int32 M=Int(P); M>0)
        Text+=FString::Printf(TEXT("Your %s mastery is %s.\n"),P==354?TEXT("melee"):TEXT("ranged"),Masteries[M<UE_ARRAY_COUNT(Masteries)?M:0]);
    if (const int32 M=Int(362); M>0)
        Text+=FString::Printf(TEXT("\nYour summoning mastery is %s.\n"),M==1?TEXT("Naturalist"):M==2?TEXT("Necromancer"):M==3?TEXT("Primalist"):TEXT("Unknown"));
    Text+=TEXT("\nLuminance Augmentations:\n\n");
    struct FInfo { int32 Property; const TCHAR* Description; };
    const FInfo Lum[]={
        {333,TEXT("Damage rating")},{334,TEXT("Damage reduction rating")},{335,TEXT("Critical damage rating")},
        {336,TEXT("Critical damage reduction rating")},{337,TEXT("Surge effect rating")},{338,TEXT("Surge chance rating")},
        {339,TEXT("Item mana usage augmentation")},{340,TEXT("Item mana gain augmentation")},{341,TEXT("Vitality augmentation")},
        {342,TEXT("Healing rating")},{343,TEXT("Crafting skill augmentation")},{344,TEXT("Specialized skill augmentation")}
    };
    for (const auto& A:Lum) if (const int32 N=Int(A.Property);N>0) Text+=FString::Printf(TEXT("%s: %d\n\n"),A.Description,N);
    if (Int(229)>0) Text+=TEXT("You have augmented your pack-carrying capacity.\n\n");
    if (Int(230)>0) Text+=FString::Printf(TEXT("You have augmented your burden-bearing ability %d times.\n\n"),Int(230));
    const FInfo Infused[]={ {294,TEXT("Creature Magic")},{295,TEXT("Item Magic")},{296,TEXT("Life Magic")},{297,TEXT("War Magic")},{328,TEXT("Void Magic")} };
    for (const auto& A:Infused) if (Int(A.Property)>0)
        Text+=FString::Printf(TEXT("You earned the Infused %s augmentation. You no longer need foci to cast %s spells.\n\n"),A.Description,A.Description);
    if (Int(326)>0) Text+=TEXT("You earned the Jack of All Trades augmentation. All your effective skills are increased by 5.\n\n");
    const FInfo Other[]={
        {218,TEXT("Innate Strength")},{219,TEXT("Innate Endurance")},{220,TEXT("Innate Coordination")},
        {221,TEXT("Innate Quickness")},{222,TEXT("Innate Focus")},{223,TEXT("Innate Self")},
        {224,TEXT("Specialized Salvaging")},{225,TEXT("Specialized Item Tinkering")},{226,TEXT("Specialized Armor Tinkering")},
        {227,TEXT("Specialized Magic Item Tinkering")},{228,TEXT("Specialized Weapon Tinkering")},
        {231,TEXT("Fewer items lost on death")},{232,TEXT("Spells retained after death")},{233,TEXT("Critical defense")},
        {234,TEXT("Bonus experience")},{235,TEXT("Bonus salvage")},{236,TEXT("Bonus imbue chance")},
        {237,TEXT("Faster regeneration")},{238,TEXT("Increased spell duration")},
        {240,TEXT("Slashing resistance")},{241,TEXT("Piercing resistance")},{242,TEXT("Bludgeoning resistance")},
        {243,TEXT("Acid resistance")},{244,TEXT("Fire resistance")},{245,TEXT("Cold resistance")},{246,TEXT("Lightning resistance")},
        {293,TEXT("Specialized Gearcraft")},{298,TEXT("Critical expertise")},{299,TEXT("Critical power")},
        {300,TEXT("Skilled melee")},{301,TEXT("Skilled missile")},{302,TEXT("Skilled magic")},
        {309,TEXT("Damage bonus")},{310,TEXT("Damage reduction")},{327,TEXT("Nether resistance")}
    };
    for (const auto& A:Other) if (const int32 N=Int(A.Property);N>0)
        Text+=FString::Printf(TEXT("%s augmentation: %d\n\n"),A.Description,N);
    int32 Burden=0;
    if (Client) Client->TryGetPlayerEncumbrance(Burden);
    const int32 Capacity=FMath::Max(1,(150+FMath::Clamp(30*V.CarryingCapacityAugs,0,150))*V.GetBuffedStrength());
    if (Burden<=Capacity) Text+=TEXT("You are not overburdened at this time.\n");
    else Text+=FString::Printf(TEXT("You are overburdened by %s burden units.\n"),*FText::AsNumber(Burden-Capacity).ToString());
    if (V.CarryingCapacityAugs>0) Text+=FString::Printf(TEXT("You have been augmented %d times with the Might of the Seventh Mule. This has increased your carrying capacity by %d%%."),V.CarryingCapacityAugs,20*V.CarryingCapacityAugs);
    return Text;
}
