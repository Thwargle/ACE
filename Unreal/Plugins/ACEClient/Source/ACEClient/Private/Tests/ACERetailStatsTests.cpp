#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailStatsTest, "ACE.RetailParity.CharacterStats",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailStatsTest::RunTest(const FString& Parameters)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
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
    TestEqual(TEXT("Large stamina has no retail character cap"),V.MaxStamina,14100);
    TestEqual(TEXT("Large mana has no retail character cap"),V.MaxMana,21100);
    End.StatModValue=60; Session.ActiveEnchantments={End}; Session.NotifyVitalsChanged();
    TestEqual(TEXT("Large pool still follows Endurance buff"),V.MaxHealth,10830);
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
#endif
