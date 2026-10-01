#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailSpellLevelTest,"ACE.RetailParity.SpellFormulaLevels",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACERetailSpellLevelTest::RunTest(const FString&)
{
    auto* Dat=NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    Dat->bSpellTableLoaded=true;
    // All power-component results from retail MagicSystem, including the two
    // extra component variants mapped into levels six and seven. Difficulty
    // must not determine membership: special spells can have unrelated power.
    const uint32 ExpectedLevels[]={0,1,2,3,4,5,6,6,7,7,8};
    for(uint32 PowerLevel=0;PowerLevel<UE_ARRAY_COUNT(ExpectedLevels);++PowerLevel)
    {
        auto& Entry=Dat->SpellInfoCache.Add(PowerLevel+1);
        Entry.School=1+PowerLevel%5;Entry.IconPowerLevel=PowerLevel;
        for(uint32 Difficulty:{0u,50u,150u,400u,1000u})
        {
            Entry.Power=Difficulty;
            uint32 School=99,Level=99;
            TestTrue(TEXT("Cached spell exposes filter metadata"),Dat->TryGetSpellSchoolAndLevel(PowerLevel+1,School,Level));
            TestEqual(TEXT("Retail school identifiers are preserved"),School,Entry.School);
            TestEqual(TEXT("Book level follows the formula even when difficulty disagrees"),Level,ExpectedLevels[PowerLevel]);
        }
    }
    for(uint32 Missing:{0u,999u})
    {
        uint32 School=99,Level=99;
        TestFalse(TEXT("Missing spell cannot masquerade as level one"),Dat->TryGetSpellSchoolAndLevel(Missing,School,Level));
        TestTrue(TEXT("Missing metadata clears both filter values"),School==0 && Level==0);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESpellDataRevisionTest,"ACE.Network.SpellDataRevision",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACESpellDataRevisionTest::RunTest(const FString&)
{
    FACESession Session;Session.State=EACESessionState::InWorld;
    auto Deliver=[&](int32 Id,bool Remove=false)
    {
        FACEBinaryWriter W;W.WriteUInt16(Id);W.WriteUInt16(0);
        FACEBinaryReader R(W.GetData());
        if(Remove)Session.HandleMagicRemoveSpell(R);else Session.HandleMagicUpdateSpell(R);
    };
    uint64 Before=Session.GetSpellDataRevision();Deliver(27);
    TestTrue(TEXT("Learning a spell invalidates an open book"),Session.GetSpellDataRevision()>Before && Session.GetKnownSpells().Contains(27));
    Before=Session.GetSpellDataRevision();Deliver(27);Deliver(0);Deliver(999,true);
    TestEqual(TEXT("Duplicate and empty updates avoid redundant rebuilds"),Session.GetSpellDataRevision(),Before);
    Deliver(27,true);
    TestTrue(TEXT("Forgetting a spell invalidates an open book"),Session.GetSpellDataRevision()>Before && Session.GetKnownSpells().IsEmpty());
    Before=Session.GetSpellDataRevision();Session.SendAddSpellToBar(27,0,0);
    TestTrue(TEXT("Hotbar edits from another surface invalidate native book"),Session.GetSpellDataRevision()>Before && Session.GetSpellBar(0).Contains(27));
    Before=Session.GetSpellDataRevision();Session.SendRemoveSpellFromBar(27,0);
    TestTrue(TEXT("Hotbar removal invalidates native book"),Session.GetSpellDataRevision()>Before && !Session.GetSpellBar(0).Contains(27));
    Before=Session.GetSpellDataRevision();Session.SetActiveSpellBar(1);
    TestTrue(TEXT("Changing active bank invalidates native book"),Session.GetSpellDataRevision()>Before);
    Before=Session.GetSpellDataRevision();Session.SetActiveSpellBar(1);
    TestEqual(TEXT("Unchanged bank does not invalidate native book"),Session.GetSpellDataRevision(),Before);
    Session.ClearWorldState();
    TestTrue(TEXT("World reset invalidates all spell views"),Session.GetSpellDataRevision()>Before && Session.GetSpellBar(0).IsEmpty());
    return !HasAnyErrors();
}
#endif
