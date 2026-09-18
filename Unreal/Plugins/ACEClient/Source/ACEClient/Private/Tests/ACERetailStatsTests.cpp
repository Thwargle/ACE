#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailStatsTest, "ACE.RetailParity.CharacterStats",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailStatsTest::RunTest(const FString& Parameters)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
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
    return true;
}
#endif
