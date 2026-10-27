#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Dat/ACEDatDatabase.h"
#include "Misc/Paths.h"
#include "Engine/GameInstance.h"


namespace
{
FString ACERetailStatsTestsDatDirectory()
{
    FString Directory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    return Directory;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailStatsTest, "ACE.RetailParity.CharacterStats",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailStatsTest::RunTest(const FString& Parameters)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    if (!Dat->LoadDatDirectory(ACERetailStatsTestsDatDirectory())) return false;
    // Verify the actual fixture files, not their filenames or the server's
    // extended Skill enum (ACE deliberately adds retired server-side skills).
    struct FDatVersion { const TCHAR* File; uint32 Iteration; };
    for (const auto& Expected : {FDatVersion{TEXT("client_portal.dat"),2072},
        FDatVersion{TEXT("client_cell_1.dat"),982}, FDatVersion{TEXT("client_local_English.dat"),994},
        FDatVersion{TEXT("client_highres.dat"),497}})
    {
        FACEDatDatabase Database; TArray<uint8> Bytes;
        if (!TestTrue(Expected.File,Database.Open(FPaths::Combine(ACERetailStatsTestsDatDirectory(),Expected.File)))) return false;
        if (!TestTrue(TEXT("DAT has an iteration record"),Database.ReadFile(0xFFFF0001u,Bytes))) return false;
        FACEDatCursor Cursor(Bytes); bool Ok=true; const uint32 Iteration=Cursor.ReadU32(Ok);
        TestTrue(TEXT("Iteration record is readable"),Ok);
        TestEqual(*FString::Printf(TEXT("%s is end of retail"),Expected.File),Iteration,Expected.Iteration);
        AddInfo(FString::Printf(TEXT("%s iteration %u"),Expected.File,Iteration));
    }
    {
        // The reported legacy entries had no icons and ranks of 5,231. Recreate
        // both login and subsequent live updates alongside modern skill IDs.
        const TArray<uint32> Modern={6,7,14,15,16,18,19,20,21,22,23,24,27,28,29,30,
            31,32,33,34,35,36,37,38,39,40,41,43,44,45,46,47,48,49,50,51,52,54};
        const TArray<uint32> Retired={1,2,3,4,5,8,9,10,11,12,13,17,25,26,42,53,999};
        FString Name; uint32 Icon=0;
        for (uint32 Id:Modern) TestTrue(TEXT("Modern skill has a DAT name and icon"),Dat->TryGetSkillInfo(Id,Name,Icon) && !Name.IsEmpty() && Icon!=0);
        for (uint32 Id:Retired) TestFalse(TEXT("Retired/internal skill is absent from the retail table"),Dat->TryGetSkillInfo(Id,Name,Icon));
        TestEqual(TEXT("End-of-retail skill table has 38 definitions"),Dat->SkillInfoCache.Num(),38);
        FACESession Legacy;
        Legacy.StatResolver=[Dat](auto& V,const auto& E){Dat->RecomputePlayerStats(V,E);};
        auto WriteSkill=[](FACEBinaryWriter& W,uint32 Id)
        {
            W.WriteUInt32(Id);W.WriteUInt16(5231);W.WriteUInt16(0);W.WriteUInt32(3);
            W.WriteUInt32(1000);W.WriteUInt32(10);W.WriteUInt32(0);W.WriteDouble(0);
        };
        FACEBinaryWriter Login;
        Login.WriteUInt32(0);Login.WriteUInt32(1);Login.WriteUInt32(2);Login.WriteUInt32(0);
        Login.WriteUInt16(Modern.Num()+Retired.Num());Login.WriteUInt16(64);
        for(uint32 Id:Modern)WriteSkill(Login,Id);
        for(uint32 Id:Retired)WriteSkill(Login,Id);
        FACEBinaryReader LoginReader(Login.GetData());Legacy.HandlePlayerDescription(LoginReader);
        TestEqual(TEXT("Login exposes only DAT-defined player skills to all consumers"),Legacy.GetPlayerVitals().Skills.Num(),Modern.Num());
        for(uint32 Id:Retired)
        {
            FACEBinaryWriter Update;Update.WriteUInt8(1);WriteSkill(Update,Id);
            FACEBinaryReader Reader(Update.GetData());Legacy.HandlePrivateUpdateSkill(Reader);
            TestFalse(TEXT("Live retired skill update cannot resurrect a phantom row"),Legacy.GetPlayerVitals().Skills.ContainsByPredicate([Id](const auto& S){return S.SkillId==Id;}));
        }
        for(uint32 Id:Modern)
            TestTrue(TEXT("Modern skill ranks and specialization remain intact"),Legacy.GetPlayerVitals().Skills.ContainsByPredicate([Id](const auto& S){return S.SkillId==Id && S.Ranks==5231 && S.AdvancementClass==3;}));
        // Simulate definitions/formulas cached from a previous DAT installation.
        // Reloading must replace them, including a previously added old skill.
        auto Old=Dat->SkillInfoCache.FindChecked(45);Old.Name=TEXT("Old Light Weapons");
        Dat->SkillInfoCache.Add(45,Old);Dat->SkillInfoCache.Add(1,Old);
        FACEPlayerVitals Custom;FACESkillInfo CustomSkill;CustomSkill.SkillId=1;CustomSkill.AdvancementClass=3;Custom.Skills.Add(CustomSkill);
        Dat->RecomputePlayerStats(Custom,{});
        TestEqual(TEXT("A custom DAT-defined skill is retained regardless of its historical ID"),Custom.Skills.Num(),1);
        Dat->VitalFormulas[0][3]=1;Dat->bVitalFormulasLoaded=true;
        if (!Dat->LoadDatDirectory(ACERetailStatsTestsDatDirectory())) return false;
        TestFalse(TEXT("DAT replacement invalidates cached vital formulas"),Dat->bVitalFormulasLoaded);
        TestFalse(TEXT("DAT replacement drops obsolete cached skill definitions"),Dat->TryGetSkillInfo(1,Name,Icon));
        TestTrue(TEXT("DAT replacement reloads the current skill name"),Dat->TryGetSkillInfo(45,Name,Icon) && Name==TEXT("Light Weapons"));
    }
    FString Tooltip;
    TestTrue(TEXT("Cooking tooltip resolves from the skill table"), Dat->TryGetSkillTooltip(39, Tooltip));
    const FString CookingFormula = TEXT("( (Coordination + Focus) / 3 )\n");
    TestTrue(TEXT("Retail tooltip begins with its complete two-attribute formula"), Tooltip.StartsWith(CookingFormula));
    TestTrue(TEXT("Retail DAT skill description follows the formula"), Tooltip.Len() > CookingFormula.Len());
    TestTrue(TEXT("Run tooltip resolves"), Dat->TryGetSkillTooltip(24, Tooltip));
    TestTrue(TEXT("Single-attribute formula omits divisor one and inner parentheses"), Tooltip.StartsWith(TEXT("( Quickness )\n")));
    TestFalse(TEXT("Missing skill has no fabricated tooltip"), Dat->TryGetSkillTooltip(MAX_uint32, Tooltip));
    TestTrue(TEXT("Missing skill clears previous hover text"), Tooltip.IsEmpty());
    FACESession Session;
    Session.StatResolver = [Dat](auto& V, const auto& E) { Dat->RecomputePlayerStats(V, E); };
    auto& V = Session.PlayerVitals;
    V.bValid = true; V.Coordination = 120; V.Focus = 180; V.Endurance = 100; V.Self = 100;
    FACESkillInfo Cooking; Cooking.SkillId = 39; Cooking.AdvancementClass = 2; Cooking.InitLevel = 5;
    V.Skills.Add(Cooking);
    Session.NotifyVitalsChanged();
    TestEqual(TEXT("Cooking uses the DAT coordination/focus formula"), V.Skills[0].Base, 105);
    TestEqual(TEXT("Unbuffed current equals full base, not ranks"), V.Skills[0].Current, 105);
    {
        auto Unbuffed=V;Unbuffed.Strength=100;Unbuffed.Quickness=100;Unbuffed.Skills.Reset();
        for(int32 Id : {6,7,15,22,24,31,32,33,34,39,41,43,44,45,46,47,48,49,54})
        {
            auto S=Cooking;S.SkillId=Id;S.Ranks=50;Unbuffed.Skills.Add(S);
        }
        Dat->RecomputePlayerStats(Unbuffed,{});
        for(const auto& S:Unbuffed.Skills)
            TestEqual(*FString::Printf(TEXT("No phantom bonus without spells or augmentations: skill %d"),S.SkillId),S.Current,S.Base);
        Unbuffed.StatQualityInts.Add(326,1); // Retail adds Jack of All Trades to current, not raw.
        Dat->RecomputePlayerStats(Unbuffed,{});
        for(const auto& S:Unbuffed.Skills)
        {
            TestEqual(TEXT("Jack of All Trades adds five after retail enchantment math"),S.Current-S.Base,5);
            TestEqual(TEXT("Jack of All Trades belongs to the unbuffed display baseline"),S.GetDisplayBase(),S.Current);
            TestEqual(TEXT("Jack of All Trades alone has no display modifier"),S.GetDisplayModifier(),0);
        }
        Unbuffed.Skills[0].AdvancementClass=3;Unbuffed.StatQualityInts.Add(344,3);
        Dat->RecomputePlayerStats(Unbuffed,{});
        TestEqual(TEXT("Specialized luminance bonus also affects current without an enchantment"),Unbuffed.Skills[0].Current-Unbuffed.Skills[0].Base,11);
        TestEqual(TEXT("Specialization bonus does not affect trained skills"),Unbuffed.Skills[1].Current-Unbuffed.Skills[1].Base,5);
        for(const auto& S:Unbuffed.Skills)
            TestEqual(TEXT("Combined permanent gains have no display modifier"),S.GetDisplayModifier(),0);
    }

    FACEBinaryWriter Skill;
    Skill.WriteUInt8(1); Skill.WriteUInt32(39); Skill.WriteUInt16(10); Skill.WriteUInt16(0);
    Skill.WriteUInt32(2); Skill.WriteUInt32(1000); Skill.WriteUInt32(5); Skill.WriteUInt32(0); Skill.WriteDouble(0);
    FACEBinaryReader SkillReader(Skill.GetData()); Session.HandlePrivateUpdateSkill(SkillReader);
    TestEqual(TEXT("Live ten-rank packet updates full Cooking level"), V.Skills[0].Current, 115);
    TestEqual(TEXT("Live ten-rank packet updates base"), V.Skills[0].Base, 115);

    FACEActiveEnchantment Focus; Focus.SpellId=1; Focus.SpellCategory=101; Focus.PowerLevel=6;
    Focus.StatModType=0x8001; Focus.StatModKey=5; Focus.StatModValue=60;
    FACEActiveEnchantment Suppressed=Focus; Suppressed.SpellId=2; Suppressed.PowerLevel=3; Suppressed.StatModValue=30;
    FACEActiveEnchantment Cook=Focus; Cook.SpellId=3; Cook.SpellCategory=102;
    Cook.StatModType=0x8010; Cook.StatModKey=39; Cook.StatModValue=40;
    Session.ActiveEnchantments={Focus, Suppressed, Cook}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Only strongest Focus category applies"), V.GetBuffedFocus(), 240);
    TestEqual(TEXT("Skill combines buffed attributes and direct skill enchantment"), V.Skills[0].Current, 175);
    TestEqual(TEXT("Buffs never overwrite raw base"), V.Skills[0].Base, 115);
    V.Focus=210; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Attribute update refreshes all dependent skills"), V.Skills[0].Current, 185);
    Session.ActiveEnchantments.Reset(); Session.NotifyVitalsChanged();
    TestEqual(TEXT("Buff expiry restores current"), V.Skills[0].Current, 125);
    FACEActiveEnchantment End=Focus; End.StatModKey=2; End.StatModValue=60;
    Session.ActiveEnchantments={End}; Session.NotifyVitalsChanged();
    const int32 RaisedMax=V.MaxHealth;
    Session.ActiveEnchantments.Reset(); Session.NotifyVitalsChanged();
    TestEqual(TEXT("Expired Endurance buff lowers maximum health"), V.MaxHealth, RaisedMax-30);
    FACEActiveEnchantment Debuff=Cook; Debuff.StatModValue=-20;
    Session.ActiveEnchantments={Debuff}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Skill debuff lowers current, preserves base"), V.Skills[0].Current, V.Skills[0].Base-20);

    Session.ActiveEnchantments.Reset();
    V.HealthStart=9000; V.HealthRanks=1000;
    V.StaminaStart=12000; V.StaminaRanks=2000;
    V.ManaStart=18000; V.ManaRanks=3000;
    FACEBinaryWriter Gear; Gear.WriteUInt8(1); Gear.WriteUInt32(379); Gear.WriteInt32(750);
    FACEBinaryReader GearReader(Gear.GetData()); Session.HandlePrivateUpdatePropertyInt(GearReader);
    TestEqual(TEXT("Large health uses server starts, ranks and gear quality"),V.MaxHealth,10800);
    TestEqual(TEXT("Gear health is part of the raw maximum, not a phantom spell bonus"),V.BaseMaxHealth,V.MaxHealth);
    TestEqual(TEXT("Large stamina has no retail character cap"),V.MaxStamina,14100);
    TestEqual(TEXT("Large mana has no retail character cap"),V.MaxMana,21100);
    End.StatModValue=60; Session.ActiveEnchantments={End}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Large pool still follows Endurance buff"),V.MaxHealth,10830);
    TestEqual(TEXT("Raw maximum does not inherit the Endurance buff"),V.MaxHealth-V.BaseMaxHealth,30);
    TestEqual(TEXT("Stamina uses the same raw-versus-buffed DAT formula"),V.MaxStamina-V.BaseMaxStamina,60);
    TestEqual(TEXT("Unchanged mana has no bonus"),V.MaxMana,V.BaseMaxMana);
    Session.ActiveEnchantments.Reset(); Session.NotifyVitalsChanged();
    TestEqual(TEXT("Large pool still falls after buff expiry"),V.MaxHealth,10800);
    FACEBinaryWriter RemoveGear; RemoveGear.WriteUInt8(2); RemoveGear.WriteUInt32(379); RemoveGear.WriteInt32(0);
    FACEBinaryReader RemoveGearReader(RemoveGear.GetData()); Session.HandlePrivateUpdatePropertyInt(RemoveGearReader);
    TestEqual(TEXT("Unequipping health rating refreshes immediately"),V.MaxHealth,10050);

    // ACE's Call of Leadership V (5122) sends a secondary-attribute additive,
    // category 0x261, key MaxHealth=1, value +10. Exercise the wire handlers and
    // every subsequent recalculation, not just a pre-populated modifier array.
    V.HealthStart=633; V.HealthRanks=0; V.Endurance=100; V.Health=683;
    auto Horn = [&](uint16 Id, uint32 Power, float Bonus, double Elapsed=0.)
    {
        FACEBinaryWriter Packet;
        Packet.WriteUInt16(Id); Packet.WriteUInt16(1);
        Packet.WriteUInt16(0x261); Packet.WriteUInt16(1);
        Packet.WriteUInt32(Power); Packet.WriteDouble(Elapsed); Packet.WriteDouble(1800.);
        Packet.WriteUInt32(0x50000002); // another fellowship member
        Packet.WriteFloat(0); Packet.WriteFloat(0); Packet.WriteDouble(0);
        Packet.WriteUInt32(0x9002); Packet.WriteUInt32(1); Packet.WriteFloat(Bonus);
        Packet.WriteUInt32(0);
        FACEBinaryReader Reader(Packet.GetData()); Session.HandleMagicUpdateEnchantment(Reader);
    };
    auto Health = [&](int32 Current)
    {
        FACEBinaryWriter Packet; Packet.WriteUInt8(1); Packet.WriteUInt32(2); Packet.WriteUInt32(Current);
        FACEBinaryReader Reader(Packet.GetData()); Session.HandlePrivateUpdateAttribute2ndLevel(Reader);
    };
    auto RemoveHorn = [&](uint16 Id)
    {
        FACEBinaryWriter Packet; Packet.WriteUInt16(Id); Packet.WriteUInt16(1);
        FACEBinaryReader Reader(Packet.GetData()); Session.HandleMagicRemoveEnchantment(Reader);
    };
    Session.NotifyVitalsChanged();
    TestEqual(TEXT("Horn reproduction starts at 683 maximum health"),V.MaxHealth,683);
    Horn(5122,400,10);
    TestEqual(TEXT("Fellowship horn raises max health before healing arrives"),V.MaxHealth,693);
    Health(693);
    TestEqual(TEXT("Full health remains 693/693 after the server vital update"),V.MaxHealth,693);
    Health(540); Session.NotifyVitalsChanged();
    TestEqual(TEXT("Horn max health persists after damage and unrelated stat refreshes"),V.MaxHealth,693);
    Horn(5133,175,2);
    TestEqual(TEXT("Weaker fellowship horns do not stack with the strongest"),V.MaxHealth,693);
    RemoveHorn(5122);
    TestEqual(TEXT("Removing the strongest horn restores the remaining weaker bonus"),V.MaxHealth,685);
    RemoveHorn(5133);
    TestEqual(TEXT("Horn expiry removes the bonus without retaining a high-water mark"),V.MaxHealth,683);
    TestEqual(TEXT("Horn expiry restores neutral maximum comparison"),V.MaxHealth,V.BaseMaxHealth);
    Horn(5122,400,10,-600.);
    TestEqual(TEXT("A restored partially elapsed horn still applies its full bonus"),V.MaxHealth,693);
    Session.ActiveEnchantments.Reset(); Session.NotifyVitalsChanged();

    for (int32 Kind=0; Kind<4; ++Kind)
    {
        auto Cost=[&](int32 Spent, int32 Ranks) {
            int64 Needed=0;
            if (Kind==0) Dat->TryGetAttributeXpToNextRank(Spent,Needed,nullptr,Ranks);
            else if (Kind==1) Dat->TryGetVitalXpToNextRank(Spent,Needed,nullptr,Ranks);
            else Dat->TryGetSkillXpToNextRank(Kind,Spent,Needed,nullptr,Ranks);
            return Needed;
        };
        int64 Ten=Cost(0,10), Sequential=0;
        for (int32 I=0; I<10; ++I) Sequential+=Cost(static_cast<int32>(Sequential),1);
        TestTrue(TEXT("DAT ten-rank cost exceeds ten times first rank"), Ten>Cost(0,1)*10);
        TestEqual(TEXT("Raise ten equals ten successive purchases"),Ten,Sequential);
    }
    for (int32 Class : {2, 3})
    {
        int64 RankStart=0, RankCost=0;
        Dat->TryGetSkillXpToNextRank(Class,0,RankStart,nullptr,50);
        Dat->TryGetSkillXpToNextRank(Class,int32(RankStart),RankCost);
        float Progress=-1.f;
        TestTrue(TEXT("Trained and specialized progress use their own DAT tables"),
            Dat->TryGetSkillRankProgress(Class,50,int32(RankStart),Progress));
        TestEqual(TEXT("A freshly earned skill point resets progress"),Progress,0.f);
        Dat->TryGetSkillRankProgress(Class,50,int32(RankStart+RankCost/2),Progress);
        TestTrue(TEXT("Usage XP halfway to the next point fills half of the meter"),FMath::IsNearlyEqual(Progress,.5f,.001f));
        Dat->TryGetSkillRankProgress(Class,51,int32(RankStart+RankCost),Progress);
        TestEqual(TEXT("The next rank starts a new progress span"),Progress,0.f);
        Dat->TryGetSkillRankProgress(Class,50,int32(RankStart-1),Progress);
        TestEqual(TEXT("Out of order XP/rank snapshots never produce a negative fill"),Progress,0.f);
        int32 Rank=0;int64 Spent=0,Cost=0;
        while(Rank<1000 && Dat->TryGetSkillXpToNextRank(Class,int32(Spent),Cost) && Cost>0){Spent+=Cost;++Rank;}
        TestTrue(TEXT("Retail rank cap is found from DAT"),Rank<1000 && Spent>MAX_int32);
        Dat->TryGetSkillRankProgress(Class,Rank,int32(Spent),Progress);
        TestEqual(TEXT("At the skill cap, the meter is empty as in retail"),Progress,0.f);
    }
    float UntrainedProgress=1.f;
    TestFalse(TEXT("Untrained skills have no usage XP progress"),Dat->TryGetSkillRankProgress(1,0,100,UntrainedProgress));
    TestEqual(TEXT("Untrained progress is empty"),UntrainedProgress,0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailAugmentationStatsTest, "ACE.RetailParity.AugmentationStats",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailAugmentationStatsTest::RunTest(const FString& Parameters)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    if (!Dat->LoadDatDirectory(ACERetailStatsTestsDatDirectory())) return false;
    FACESession Session;
    Session.StatResolver = [Dat](auto& V, const auto& E) { Dat->RecomputePlayerStats(V, E); };
    auto& V = Session.PlayerVitals;
    V.bValid = true;
    auto Quality = [&](uint32 Key, int32 Value)
    {
        FACEBinaryWriter Packet;
        Packet.WriteUInt8(1); Packet.WriteUInt32(Key); Packet.WriteInt32(Value);
        FACEBinaryReader Reader(Packet.GetData()); Session.HandlePrivateUpdatePropertyInt(Reader);
    };
    auto Attribute = [&](uint32 Id, int32 Start)
    {
        FACEBinaryWriter Packet;
        Packet.WriteUInt8(1); Packet.WriteUInt32(Id); Packet.WriteUInt32(125);
        Packet.WriteUInt32(Start); Packet.WriteUInt32(0);
        FACEBinaryReader Reader(Packet.GetData()); Session.HandlePrivateUpdateAttribute(Reader);
    };
    // Quickness/Coordination are reversed between attribute and augmentation enums.
    const uint32 InnateProperties[] = {218, 219, 221, 220, 222, 223};
    for (uint32 Id = 1; Id <= 6; ++Id)
    {
        Attribute(Id, 25);
        Attribute(Id, 30); // ACE AugmentationDevice raises StartingValue by five.
        Quality(InnateProperties[Id - 1], 1);
        TestEqual(TEXT("Innate attribute gain is already in base, counted once"), V.GetAttributeBase(Id), 155);
        TestEqual(TEXT("Innate attribute gain is not a modified-stat bonus"), V.GetAttributeCurrent(Id), 155);
    }
    TestEqual(TEXT("Innate Endurance gain belongs to base health"), V.MaxHealth, V.BaseMaxHealth);
    TestEqual(TEXT("Innate Endurance gain belongs to base stamina"), V.MaxStamina, V.BaseMaxStamina);
    TestEqual(TEXT("Innate Self gain belongs to base mana"), V.MaxMana, V.BaseMaxMana);

    const TSet<int32> Melee = {41, 44, 45, 46, 49};
    const TSet<int32> Magic = {31, 32, 33, 34, 43};
    const int32 SkillIds[] = {6, 7, 15, 18, 22, 24, 28, 29, 30, 31, 32, 33, 34, 37, 38, 39, 40, 41, 43, 44, 45, 46, 47, 48, 49, 54};
    for (int32 Class : {1, 2, 3})
    {
        V.Skills.Reset();
        V.StatQualityInts.Reset();
        for (int32 Id : SkillIds)
        {
            FACESkillInfo Skill;
            Skill.SkillId = Id; Skill.AdvancementClass = Class;
            Skill.InitLevel = Class == 3 ? 10 : Class == 2 ? 5 : 0;
            Skill.Ranks = Class == 1 ? 0 : 50;
            V.Skills.Add(Skill);
        }
        Session.NotifyVitalsChanged();
        const auto Before = V.Skills;
        // Apply each skill family separately: no leakage to other skills.
        for (int32 Property : {300, 301, 302, 365, 326, 344, 390})
        {
            Quality(Property, Property == 365 || Property == 344 ? 3 : 1);
            for (int32 Index = 0; Index < V.Skills.Num(); ++Index)
            {
                const auto& Skill = V.Skills[Index];
                const int32 Gain = Property == 365 ? 3 : Property == 326 ? 5 :
                    Property == 344 ? (Class == 3 ? 6 : 0) : Property == 390 ? (Class >= 2 ? 1 : 0) :
                    ((Property == 300 && Melee.Contains(Skill.SkillId)) ||
                     (Property == 301 && Skill.SkillId == 47) ||
                     (Property == 302 && Magic.Contains(Skill.SkillId))) ? 10 : 0;
                const bool PostEnchantment = Property == 326 || Property == 344;
                const FString Context = FString::Printf(TEXT("Class %d skill %d augmentation %d"), Class, Skill.SkillId, Property);
                TestEqual(*(Context + TEXT(" raw requirement")), Skill.Base, Before[Index].Base + (PostEnchantment ? 0 : Gain));
                TestEqual(*(Context + TEXT(" total")), Skill.Current, Before[Index].Base + Gain);
                TestEqual(*(Context + TEXT(" display baseline")), Skill.GetDisplayBase(), Before[Index].Base + Gain);
                TestEqual(*(Context + TEXT(" is neutral without spells")), Skill.GetDisplayModifier(), 0);
            }
            Quality(Property, 0);
            for (int32 Index = 0; Index < V.Skills.Num(); ++Index)
                TestEqual(TEXT("Removing an augmentation also resets its display baseline"),
                    V.Skills[Index].GetDisplayBase(), Before[Index].Base);
        }
    }

    // Use a known retail Cooking formula to verify combined bonuses and expiry.
    V.Skills.Reset();
    FACESkillInfo Cooking; Cooking.SkillId = 39; Cooking.AdvancementClass = 3;
    Cooking.InitLevel = 10; Cooking.Ranks = 50; V.Skills.Add(Cooking);
    Quality(365, 3);
    TestEqual(TEXT("Cooking base includes rounded augmented attributes and all-skills luminance"), V.Skills[0].Base, 166);
    FACEActiveEnchantment Focus;
    Focus.SpellId = 1; Focus.SpellCategory = 101; Focus.StatModType = 0x8001;
    Focus.StatModKey = 5; Focus.StatModValue = 60;
    Session.ActiveEnchantments = {Focus}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Attribute buff shows only its own increase over augmented base"), V.GetAttributeCurrent(5) - V.GetAttributeBase(5), 60);
    TestEqual(TEXT("Dependent skill shows only the buff contribution"), V.Skills[0].Current - V.Skills[0].Base, 20);
    Session.ActiveEnchantments.Reset(); Session.NotifyVitalsChanged();
    TestEqual(TEXT("Buff expiry restores neutral augmented attributes"), V.GetAttributeCurrent(5), 155);
    TestEqual(TEXT("Buff expiry restores neutral augmented skills"), V.Skills[0].Current, 166);

    // CACQualities::InqSkill's !raw branch deliberately treats these differently.
    Quality(326, 1);
    Quality(344, 3);
    TestEqual(TEXT("Retail Jack/spec luminance do not overwrite raw skill"), V.Skills[0].Base, 166);
    TestEqual(TEXT("Jack and three spec luminance ranks increase the total by eleven"), V.Skills[0].Current, 177);
    TestEqual(TEXT("Permanent gains are part of the displayed base"), V.Skills[0].GetDisplayBase(), 177);
    TestEqual(TEXT("No spell bonus for permanent gains"), V.Skills[0].GetDisplayModifier(), 0);
    FACEActiveEnchantment Cook;
    Cook.SpellId = 2; Cook.SpellCategory = 102; Cook.StatModType = 0x8010;
    Cook.StatModKey = 39; Cook.StatModValue = 40;
    Session.ActiveEnchantments = {Focus, Cook}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Real buffs retain the total including permanent gains"), V.Skills[0].Current, 237);
    TestEqual(TEXT("Only direct and attribute spell gains appear in the display modifier"), V.Skills[0].GetDisplayModifier(), 60);
    Cook.StatModValue = -3;
    Session.ActiveEnchantments = {Cook}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("A small debuff is not hidden by positive permanent gains"), V.Skills[0].GetDisplayModifier(), -3);
    Cook.StatModType = 0x4010; Cook.StatModValue = .5f;
    Session.ActiveEnchantments = {Cook}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Multiplicative spells do not scale Jack or specialized luminance"), V.Skills[0].Current, 94);
    TestEqual(TEXT("Multiplicative penalty uses the complete permanent baseline"), V.Skills[0].GetDisplayModifier(), -83);
    FACEActiveEnchantment Vitae; Vitae.bVitae = true; Vitae.StatModValue = .9f;
    Session.ActiveEnchantments = {Vitae}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Retail applies vitae to innate gains, but not Jack/spec luminance"), V.Skills[0].Current, 160);
    TestEqual(TEXT("Vitae remains visible as a penalty without a phantom positive augmentation"), V.Skills[0].GetDisplayModifier(), -17);
    Session.ActiveEnchantments.Reset(); Session.NotifyVitalsChanged();
    TestEqual(TEXT("Spell and vitae expiry restore the permanent baseline"), V.Skills[0].GetDisplayModifier(), 0);
    Session.NotifyVitalsChanged();
    TestEqual(TEXT("Repeated stat refresh does not accumulate permanent gains"), V.Skills[0].GetDisplayBase(), 177);
    Quality(326, 0); Quality(344, 0);
    TestEqual(TEXT("Removing modified-only augmentations restores neutral comparison immediately"), V.Skills[0].Current, V.Skills[0].Base);
    TestTrue(TEXT("Augmentation updates do not invent active enchantments"), Session.ActiveEnchantments.IsEmpty());
    return true;
}
#endif
