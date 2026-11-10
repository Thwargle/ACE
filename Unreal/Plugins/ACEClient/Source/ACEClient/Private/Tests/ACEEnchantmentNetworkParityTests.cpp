#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEnchantmentNetworkParityTest, "ACE.Network.EnchantmentParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEEnchantmentNetworkParityTest::RunTest(const FString&)
{
    FACESession Session;
    auto Event=[&](uint32 Opcode,const FACEBinaryWriter& Payload,int32 Length=INDEX_NONE)
    {
        FACEBinaryWriter W; W.WriteUInt32(100); W.WriteUInt32(1); W.WriteUInt32(Opcode);
        W.WriteBytes(Payload.GetData().GetData(),Length==INDEX_NONE?Payload.Num():Length);
        FACEBinaryReader R(W.GetData()); Session.HandleGameEvent(R);
    };
    // Independent retail Enchantment wire layout, including the optional set ID.
    auto Record=[](uint16 Spell,uint16 Layer,uint32 Caster,double Duration,uint32 Type=0x02008001,
        double Start=0.0,bool Set=false,float Value=20.f)
    {
        FACEBinaryWriter W;
        W.WriteUInt16(Spell); W.WriteUInt16(Layer); W.WriteUInt16(7); W.WriteUInt16(Set?1:0);
        W.WriteUInt32(100); W.WriteDouble(Start); W.WriteDouble(Duration); W.WriteUInt32(Caster);
        W.WriteFloat(0); W.WriteFloat(0); W.WriteDouble(0);
        W.WriteUInt32(Type); W.WriteUInt32(1); W.WriteFloat(Value);
        if(Set) W.WriteUInt32(25);
        return W;
    };
    auto Remove=[](uint16 Spell,uint16 Layer)
    { FACEBinaryWriter W; W.WriteUInt16(Spell); W.WriteUInt16(Layer); return W; };
    int32 Notices=0; Session.OnEnchantmentsChanged.AddLambda([&](){++Notices;});
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(1,1,101,600));
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(1,1,102,1200));
    TestTrue(TEXT("A new caster refreshes the same spell/layer instead of duplicating it"),
        Session.ActiveEnchantments.Num()==1 && Session.ActiveEnchantments[0].CasterGuid==102
        && Session.ActiveEnchantments[0].Duration==1200.f);
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(1,2,103,1800));
    TestTrue(TEXT("Timed recast replaces the prior timed layer"),Session.ActiveEnchantments.Num()==1
        && Session.ActiveEnchantments[0].Layer==2 && Session.ActiveEnchantments[0].CasterGuid==103);
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(1,3,500,-1));
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(1,4,501,-1));
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(1,3,502,-1));
    TestEqual(TEXT("Permanent equipment layers coexist with each other and a timed cast"),Session.ActiveEnchantments.Num(),3);
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(1,2,104,2100,0x02004001));
    TestEqual(TEXT("Multiplicative and additive registries do not overwrite each other"),Session.ActiveEnchantments.Num(),4);
    Event(ACEGameEvent::MagicRemoveEnchantment,Remove(1,3));
    TestTrue(TEXT("Unequip removes only the specified permanent layer"),Session.ActiveEnchantments.Num()==3
        && !Session.ActiveEnchantments.ContainsByPredicate([](const auto& E){return E.Layer==3;})
        && Session.ActiveEnchantments.ContainsByPredicate([](const auto& E){return E.Layer==4 && E.Duration==-1.f;}));
    Session.ActiveEnchantments.Reset(); Notices=0;
    const auto Old=Record(20,1,100,600);
    Event(ACEGameEvent::MagicUpdateEnchantment,Old);
    const double Receipt=Session.ActiveEnchantments[0].ReceivedAt;
    const auto SetSpell=Record(21,1,100,1200,0x02008001,-60.,true);
    for(int32 Size=0;Size<SetSpell.Num();++Size)
    {
        Event(ACEGameEvent::MagicUpdateEnchantment,SetSpell,Size);
        if(!TestTrue(TEXT("Truncated base or set-ID record changes no enchantments"),Notices==1
            && Session.ActiveEnchantments.Num()==1 && Session.ActiveEnchantments[0].ReceivedAt==Receipt))return false;
    }
    FACEBinaryWriter Batch; Batch.WriteUInt32(2); Batch.WriteBytes(Record(20,1,105,2400).GetData()); Batch.WriteBytes(SetSpell.GetData());
    for(int32 Size=0;Size<Batch.Num();++Size)
    {
        Event(ACEGameEvent::MagicUpdateMultipleEnchantments,Batch,Size);
        if(!TestTrue(TEXT("Truncated batch cannot partially refresh timers or notify observers"),Notices==1
            && Session.ActiveEnchantments.Num()==1 && Session.ActiveEnchantments[0].Duration==600.f
            && Session.ActiveEnchantments[0].ReceivedAt==Receipt))return false;
    }
    for(double Bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
    {
        Event(ACEGameEvent::MagicUpdateEnchantment,Record(20,1,105,Bad));
        Event(ACEGameEvent::MagicUpdateEnchantment,Record(20,1,105,1200,0x02008001,Bad));
        Event(ACEGameEvent::MagicUpdateEnchantment,Record(20,1,105,1200,0x02008001,0.,false,float(Bad)));
    }
    TestTrue(TEXT("Nonfinite timers and stat values never enter active effects"),Notices==1 && Session.ActiveEnchantments[0].Duration==600.f);
    FACEBinaryWriter Huge; Huge.WriteUInt32(MAX_uint32);
    Event(ACEGameEvent::MagicUpdateMultipleEnchantments,Huge);
    Event(ACEGameEvent::MagicRemoveMultipleEnchantments,Huge);
    TestEqual(TEXT("Impossible list count leaves observers unchanged"),Notices,1);
    Event(ACEGameEvent::MagicUpdateMultipleEnchantments,Batch);
    TestTrue(TEXT("Complete mixed-format batch commits and notifies once"),Notices==2 && Session.ActiveEnchantments.Num()==2
        && Session.ActiveEnchantments[0].Duration==2400.f && Session.ActiveEnchantments[1].StartTime==-60.f);
    FACEBinaryWriter Deletes; Deletes.WriteUInt32(2); Deletes.WriteBytes(Remove(20,1).GetData()); Deletes.WriteBytes(Remove(21,1).GetData());
    for(uint32 Opcode:{ACEGameEvent::MagicRemoveMultipleEnchantments,ACEGameEvent::MagicDispelMultipleEnchantments})
    {
        for(int32 Size=0;Size<Deletes.Num();++Size)
        {
            Event(Opcode,Deletes,Size);
            if(!TestTrue(TEXT("Truncated remove/dispel list cannot remove the first effect alone"),Notices==2 && Session.ActiveEnchantments.Num()==2))return false;
        }
    }
    Event(ACEGameEvent::MagicDispelMultipleEnchantments,Deletes);
    TestTrue(TEXT("Complete dispel removes both effects and notifies once"),Session.ActiveEnchantments.IsEmpty() && Notices==3);

    Event(ACEGameEvent::MagicUpdateEnchantment,Record(666,0,100,-1,0x00804000,0.,false,.8f));
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(666,1,200,-1,0x00804000,0.,false,.95f));
    float Vitae=0.f;
    TestTrue(TEXT("Vitae is one replaceable registry entry regardless of caster or layer"),Session.ActiveEnchantments.Num()==1
        && Session.TryGetVitaeMultiplier(Vitae) && Vitae==.95f);
    Event(ACEGameEvent::MagicUpdateEnchantment,Record(0x8000+213,1,100,120,0x01000000,-60.));
    TArray<FACEActiveEnchantment> Visible,All; Session.GetActiveEnchantments(Visible); Session.GetActiveEnchantments(All,true);
    TestTrue(TEXT("Cooldowns are opt-in and remain separate from buff-panel effects"),Visible.Num()==1 && All.Num()==2 && All[1].bCooldown);
    FACEWorldObject Item; Item.CooldownId=213; Item.CooldownDuration=120;
    TestTrue(TEXT("Restored server elapsed time drives the shared cooldown"),Session.GetItemCooldownPhase(Item,All[1].ReceivedAt)>0
        && Session.GetItemCooldownPhase(Item,All[1].ReceivedAt+61)==0);

    // Registry order in PlayerDescription: multiplicative, additive, cooldown, vitae.
    FACEBinaryWriter Login; Login.WriteUInt32(0); Login.WriteUInt32(0); Login.WriteUInt32(0x200); Login.WriteUInt32(0);
    Login.WriteUInt32(15);
    Login.WriteUInt32(1); Login.WriteBytes(Record(30,1,300,-1,0x02004001).GetData());
    Login.WriteUInt32(2); Login.WriteBytes(Record(31,1,301,600).GetData()); Login.WriteBytes(Record(31,2,302,900).GetData());
    Login.WriteUInt32(1); Login.WriteBytes(Record(0x8000+213,1,100,120,0,-45.).GetData()); // list marks cooldown even without the flag
    Login.WriteBytes(Record(666,0,100,-1,0x00804000,0.,true,.9f).GetData());
    const int32 RegistryEnd=Login.Num();
    Login.WriteUInt32(0); Login.WriteUInt32(123); Login.WriteUInt32(1); Login.WriteUInt32(27); // player module + one spellbar
    Login.WriteUInt32(1); Login.WriteUInt32(999); Login.WriteUInt32(0); Login.WriteUInt32(0); // inventory + equipment
    const auto BeforeLogin=Session.ActiveEnchantments; const int32 BeforeNotices=Notices;
    for(int32 Size=16;Size<RegistryEnd;++Size)
    {
        Event(ACEGameEvent::PlayerDescription,Login,Size);
        if(!TestTrue(TEXT("Incomplete login registry preserves all existing lists and cannot parse options from leftover records"),
            Notices==BeforeNotices && Session.ActiveEnchantments.Num()==BeforeLogin.Num()
            && Session.ActiveEnchantments[0].StatModValue==.95f))return false;
    }
    Session.PlayerGuid=100;
    Event(ACEGameEvent::PlayerDescription,Login);
    if(!TestEqual(TEXT("Full registry preserves all received list entries"),Session.ActiveEnchantments.Num(),5))return false;
    TestTrue(TEXT("Full registry retains snapshot layers rather than applying incremental recast rules"),
        Session.ActiveEnchantments[1].SpellId==31 && Session.ActiveEnchantments[2].SpellId==31);
    TestTrue(TEXT("Cooldown and vitae lists retain their roles at login"),Session.ActiveEnchantments[3].bCooldown && Session.ActiveEnchantments[4].bVitae);
    TestTrue(TEXT("Optional enchantment set ID leaves the following module and inventory aligned"),Session.GetSpellBar(0).Contains(27)
        && Session.GetCharacterOptions1()==123 && Session.ContainerContents.FindRef(100).Num()==1
        && Session.ContainerContents.FindRef(100)[0].ItemGuid==999);
    return true;
}
#endif
