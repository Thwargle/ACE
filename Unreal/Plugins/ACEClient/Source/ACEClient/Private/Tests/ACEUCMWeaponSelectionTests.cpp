#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEPluginCombat.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEUCMProfiles.h"
#include "Mods/ACEVTProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMWeaponSelectionTest,"ACE.Plugins.VTankWeaponSelection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMWeaponSelectionTest::RunTest(const FString&)
{
    auto Json=[](const TCHAR* T){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(T),O);return O;};
    FString Script,Error;
    FFileHelper::LoadFileToString(Script,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto State=[&](){return Json(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"combat_mode":8,"components_required":false,"skills":{"34":{"training":3,"current":400},"44":{"training":3,"current":400},"47":{"training":3,"current":400}},"spells":[],"targets":[{"id":50,"name":"Snow Tusker","identified":true,"distance":1,"creature_type":1,"line_of_sight":true}],"inventory":[]})"));};
    auto Profile=[&](){return Json(TEXT(R"({"buffing":false,"recovery":false,"looting":false,"combat":"auto","attack_spell":0})"));};
    auto SetItems=[&](auto S,const TCHAR* T){S->SetArrayField(TEXT("inventory"),Json(T)->GetArrayField(TEXT("items")));};
    auto Step=[&](auto S,auto P)
    {
        FACEPluginVM VM;TestTrue(TEXT("Shipped UCM compiles"),VM.Load(Script,Error));TSharedPtr<FJsonObject> I;
        TestTrue(*FString::Printf(TEXT("Shipped UCM evaluates: %s"),*Error),VM.Step(S,P,I,Error));
        if(!Error.IsEmpty())AddError(Error);return I?I:MakeShared<FJsonObject>();
    };
    auto Item=[&](auto I,int32 Expected,const TCHAR* Why){TestEqual(Why,I->GetNumberField(TEXT("item")),double(Expected));};
    // VT eDamageElement.Cold=5/Fire=6 are not the protocol's Cold=8/Fire=16.
    // Use the same appraisal bits the server sends, never UI-derived fixtures.
    auto S=State(),P=Profile();auto Target=S->GetArrayField(TEXT("targets"))[0]->AsObject();
    Target->SetArrayField(TEXT("preferred_elements"),ACEPluginCombat::PreferredElements(TEXT("Snow Tusker")));
    TestEqual(TEXT("VT Snow Tusker weakness maps to retail fire bit"),Target->GetArrayField(TEXT("preferred_elements"))[0]->AsNumber(),16.);
    SetItems(S,TEXT(R"({"items":[{"id":90,"name":"Cold sword","type":1,"damage_type":8,"damage":100,"identified":true,"can_wield":true,"equipped":true},{"id":91,"name":"Fire sword","type":1,"damage_type":16,"damage":20,"identified":true,"can_wield":true}]})"));
    Item(Step(S,P),91,TEXT("Fire weakness beats higher displayed cold damage"));
    auto Items=S->GetArrayField(TEXT("inventory"));auto Cold=Items[0]->AsObject(),Fire=Items[1]->AsObject();
    Cold->SetBoolField(TEXT("equipped"),false);Cold->SetNumberField(TEXT("slayer"),1);Cold->SetNumberField(TEXT("damage"),1);
    Item(Step(S,P),90,TEXT("Matching species slayer has VT's first priority"));
    Cold->RemoveField(TEXT("slayer"));Target->SetArrayField(TEXT("preferred_elements"),{MakeShared<FJsonValueNumber>(16),MakeShared<FJsonValueNumber>(8)});
    Cold->SetObjectField(TEXT("int_properties"),Json(TEXT(R"({"179":128})")));
    Item(Step(S,P),90,TEXT("Listed elemental rend outranks an unimbued preferred element"));
    Fire->SetObjectField(TEXT("int_properties"),Json(TEXT(R"({"263":16})")));
    Item(Step(S,P),91,TEXT("Fire cleave and cold rend follow ordered monster elements"));
    Fire->SetObjectField(TEXT("int_properties"),Json(TEXT(R"({"179":4})")));
    Item(Step(S,P),90,TEXT("Elemental rend precedes armor rend before vulnerability"));
    S->SetArrayField(TEXT("debuffs"),{MakeShared<FJsonValueObject>(Json(TEXT(R"({"target":50,"category":110,"power":100,"expires":101})")))});
    Item(Step(S,P),91,TEXT("Confirmed fire vulnerability promotes matching armor-rend weapon even inside refresh window"));
    S->SetArrayField(TEXT("debuffs"),{});P->SetNumberField(TEXT("damage_type"),16);
    Item(Step(S,P),91,TEXT("Explicit fire rule cannot equip a cold rend"));
    P->RemoveField(TEXT("damage_type"));Cold->RemoveField(TEXT("int_properties"));Fire->RemoveField(TEXT("int_properties"));
    Target->SetObjectField(TEXT("resists"),Json(TEXT(R"({"8":2,"16":0.5})")));
    Item(Step(S,P),90,TEXT("Custom server resistance data overrides stock monster database"));

    S=State();P=Profile();P->SetStringField(TEXT("combat"),TEXT("melee"));P->SetNumberField(TEXT("melee_weapon"),90);P->SetNumberField(TEXT("melee_secondary_equip"),2);
    Target=S->GetArrayField(TEXT("targets"))[0]->AsObject();Target->SetArrayField(TEXT("preferred_elements"),{MakeShared<FJsonValueNumber>(16),MakeShared<FJsonValueNumber>(8)});
    SetItems(S,TEXT(R"({"items":[{"id":90,"name":"Main hand","type":1,"damage_type":1,"identified":true,"can_wield":true,"equipped":true,"equipped_slot":1048576},{"id":91,"name":"Cold-rending offhand","type":1,"damage_type":8,"damage":10,"identified":true,"can_wield":true,"int_properties":{"179":128}},{"id":92,"name":"Fire offhand","type":1,"damage_type":16,"damage":100,"identified":true,"can_wield":true}]})"));
    auto Offhand=Step(S,P);Item(Offhand,91,TEXT("Automatic offhand applies the same rend priority"));TestTrue(TEXT("Secondary weapon goes in left hand"),Offhand->GetBoolField(TEXT("offhand")));
    S->GetArrayField(TEXT("inventory"))[2]->AsObject()->SetObjectField(TEXT("int_properties"),Json(TEXT(R"({"263":16})")));
    Item(Step(S,P),92,TEXT("Offhand cleave follows ordered elements alongside rends"));

    // All seven elemental casters, through the native appraisal snapshot and
    // actual Lua policy. Wand elements live in quality 45, with no weapon profile.
    auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone();
    ON_SCOPE_EXIT{GI->Shutdown();};
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    const int32 Bits[]={32,4,8,64,16,2,1};
    for(int32 Index=0;Index<7;++Index)
    {
        S=State();P=Profile();P->SetStringField(TEXT("combat"),TEXT("magic"));
        Target=S->GetArrayField(TEXT("targets"))[0]->AsObject();Target->SetArrayField(TEXT("preferred_elements"),{MakeShared<FJsonValueNumber>(Bits[Index])});
        TArray<TSharedPtr<FJsonValue>> Wands,Spells;
        for(int32 N=0;N<7;++N)
        {
            FACEWorldObject W;W.Guid=90+N;W.Name=TEXT("Elemental wand");W.ItemType=ACEItemType::Caster;
            FACEAppraisalInfo A;A.bSuccess=true;A.IntProperties.Add(45,Bits[N]);A.FloatProperties.Add(152,1.2);
            Host->Appraisals.Add(W.Guid,A);auto J=Host->PluginItemSnapshot(W)->AsObject();
            TestEqual(TEXT("Native wand snapshot reads integer quality 45"),J->GetNumberField(TEXT("damage_type")),double(Bits[N]));
            J->SetBoolField(TEXT("can_wield"),true);J->SetBoolField(TEXT("equipped"),N==Index);Wands.Add(MakeShared<FJsonValueObject>(J));
            auto Spell=Json(TEXT(R"({"school":1,"power":100,"skill":400})"));Spell->SetNumberField(TEXT("id"),1000+N);Spell->SetNumberField(TEXT("category"),117+N);
            Spells.Add(MakeShared<FJsonValueObject>(Spell));
        }
        S->SetArrayField(TEXT("inventory"),Wands);S->SetArrayField(TEXT("spells"),Spells);
        auto I=Step(S,P);TestEqual(TEXT("Selected wand casts corresponding retail element"),I->GetNumberField(TEXT("spell")),double(1000+Index));
        Wands[Index]->AsObject()->SetBoolField(TEXT("equipped"),false);
        Item(Step(S,P),90+Index,TEXT("Monster element selects the matching elemental wand"));
    }
    // Neutral rend wand chooses rend's spell, not its absent damage profile.
    S=State();P=Profile();P->SetStringField(TEXT("combat"),TEXT("magic"));
    Target=S->GetArrayField(TEXT("targets"))[0]->AsObject();Target->SetArrayField(TEXT("preferred_elements"),{MakeShared<FJsonValueNumber>(16)});
    SetItems(S,TEXT(R"({"items":[{"id":90,"name":"Fire-rending wand","type":32768,"damage_type":0,"identified":true,"can_wield":true,"equipped":true,"int_properties":{"179":512}}]})"));
    S->SetArrayField(TEXT("spells"),Json(TEXT(R"({"items":[{"id":1,"category":119,"school":1,"power":300,"skill":400},{"id":2,"category":121,"school":1,"power":100,"skill":400}]})"))->GetArrayField(TEXT("items")));
    TestEqual(TEXT("Fire-rending wand uses fire despite higher-tier cold bolt"),Step(S,P)->GetNumberField(TEXT("spell")),2.);
    P->SetBoolField(TEXT("debuff_vuln"),true);
    auto Spells=S->GetArrayField(TEXT("spells"));Spells.Add(MakeShared<FJsonValueObject>(Json(TEXT(R"({"id":3,"category":110,"school":2,"power":100,"skill":400,"duration":300})"))));S->SetArrayField(TEXT("spells"),Spells);
    TestEqual(TEXT("Wand plan selects the matching fire vulnerability"),Step(S,P)->GetNumberField(TEXT("spell")),3.);

    // Bows, crossbows and atlatls: select a complete loadout, then keep its ammo.
    for(int32 Kind:{1,2,4})
    {
        S=State();P=Profile();P->SetStringField(TEXT("combat"),TEXT("missile"));
        Target=S->GetArrayField(TEXT("targets"))[0]->AsObject();Target->SetArrayField(TEXT("preferred_elements"),{MakeShared<FJsonValueNumber>(16),MakeShared<FJsonValueNumber>(8)});
        SetItems(S,TEXT(R"({"items":[{"id":90,"name":"Cold launcher","type":256,"damage_type":8,"damage_mod":5,"identified":true,"can_wield":true},{"id":91,"name":"Fire launcher","type":256,"damage_type":16,"damage_mod":2,"identified":true,"can_wield":true},{"id":92,"name":"Prismatic ammo","type":256,"slots":8388608,"damage_type":268435456,"damage":20,"identified":true,"can_wield":true}]})"));
        auto Inv=S->GetArrayField(TEXT("inventory"));for(auto V:Inv)V->AsObject()->SetNumberField(TEXT("ammo_type"),Kind);
        Item(Step(S,P),91,TEXT("Prismatic ammo inherits the selected fire launcher"));
        Inv[1]->AsObject()->SetBoolField(TEXT("equipped"),true);
        Item(Step(S,P),92,TEXT("Selected loadout equips its prismatic ammunition"));
        Inv[2]->AsObject()->SetBoolField(TEXT("equipped"),true);
        TestEqual(TEXT("Equipped matching loadout attacks"),Step(S,P)->GetStringField(TEXT("action")),FString(TEXT("attack")));
        P->SetNumberField(TEXT("damage_type"),16);Inv[1]->AsObject()->SetNumberField(TEXT("damage_type"),2);
        TestEqual(TEXT("Neutral bow cannot pretend its prismatic ammo is fire"),Step(S,P)->GetStringField(TEXT("action")),FString(TEXT("activity_failed")));
    }
    // Imported VT settings use enum indices, native settings use protocol bits.
    TArray<FString> Issues;
    auto Converted=ACEVTProfile::Convert(Json(TEXT(R"({"format":"usd","tables":[{"name":"MyMonsters","columns":["MonsterName","DamageType","SecondaryVuln"],"rows":[["name == Cold",5,6],["name == Fire",6,5]]}]})")),Issues);
    TestTrue(TEXT("VT element rules import without warnings"),Converted.IsValid()&&Issues.IsEmpty());
    if(Converted)
    {
        const auto& Rules=Converted->GetArrayField(TEXT("monsters"));
        if(TestEqual(TEXT("Both VT monster rules import"),Rules.Num(),2))for(int32 N=0;N<2;++N)
        {
            TestEqual(TEXT("VT cold/fire enum converts to retail damage"),Rules[N]->AsObject()->GetNumberField(TEXT("damage_type")),N?16.:8.);
            TestEqual(TEXT("VT secondary vulnerability uses the same mapping"),Rules[N]->AsObject()->GetNumberField(TEXT("secondary_vuln")),N?8.:16.);
        }
        ACEUCMProfiles::NormalizeDamageTypes(Converted);
        TestEqual(TEXT("New imports carry a current damage format"),Converted->GetNumberField(TEXT("monster_damage_version")),2.);
    }
    using namespace ACEUCMProfiles;
    auto Legacy=Json(TEXT(R"({"damage_type":8,"monsters":[{"name":"Rat","damage_type":16,"secondary_vuln":8,"pet_damage_type":6}],"loot_rules":[{"conditions":[{"key":45,"value":8}]}]})"));
    NormalizeDamageTypes(Legacy);
    TestEqual(TEXT("Old Fire choice migrates to retail Fire"),Legacy->GetNumberField(TEXT("damage_type")),16.);
    auto Rule=Legacy->GetArrayField(TEXT("monsters"))[0]->AsObject();
    TestEqual(TEXT("Old Cold monster choice migrates"),Rule->GetNumberField(TEXT("damage_type")),8.);
    TestEqual(TEXT("Secondary Fire vulnerability migrates"),Rule->GetNumberField(TEXT("secondary_vuln")),16.);
    TestEqual(TEXT("Pet VT enum is not a protocol mask"),Rule->GetNumberField(TEXT("pet_damage_type")),6.);
    TestEqual(TEXT("Loot property comparisons remain raw server values"),Legacy->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->GetArrayField(TEXT("conditions"))[0]->AsObject()->GetNumberField(TEXT("value")),8.);
    NormalizeDamageTypes(Legacy);TestEqual(TEXT("Repeated migration cannot flip Fire back"),Legacy->GetNumberField(TEXT("damage_type")),16.);
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/WeaponSelection")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    ON_SCOPE_EXIT{IFileManager::Get().DeleteDirectory(*Root,false,true);};
    TestTrue(TEXT("Shared defaults created"),EnsureDefaults(Root,Legacy,Error));
    TSharedPtr<FJsonObject> Refs=MakeShared<FJsonObject>();for(const auto& Section:Sections())TestTrue(TEXT("Shared sections selected"),Select(Root,Section,TEXT("Default"),Legacy,Refs,Error));
    TestTrue(TEXT("Migrated selection saved"),SaveSelection(Root,Legacy,Refs,Error));
    TSharedPtr<FJsonObject> Reloaded,Links;TestTrue(TEXT("Migrated selection reloads"),Load(Root,ActiveName(),Reloaded,Links,Error));
    if(Reloaded)TestEqual(TEXT("Fire choice survives save/reload without another swap"),Reloaded->GetNumberField(TEXT("damage_type")),16.);
    FFileHelper::SaveStringToFile(TEXT(R"({"monsters":[{"name":"Legacy","damage_type":8,"secondary_vuln":16}]})"),*(Root/TEXT("Profiles/Monsters/Old.json")));
    TestTrue(TEXT("Switching to an old independent Monsters file migrates it"),Select(Root,TEXT("Monsters"),TEXT("Old"),Reloaded,Links,Error));
    if(Reloaded)
    {
        auto Migrated=Reloaded->GetArrayField(TEXT("monsters"))[0]->AsObject();
        TestEqual(TEXT("Old shared Fire rule maps correctly"),Migrated->GetNumberField(TEXT("damage_type")),16.);
        TestEqual(TEXT("Old shared Cold secondary maps correctly"),Migrated->GetNumberField(TEXT("secondary_vuln")),8.);
        TestEqual(TEXT("Selecting legacy Monsters does not re-migrate Character"),Reloaded->GetNumberField(TEXT("damage_type")),16.);
        TestTrue(TEXT("Migrated shared file saves"),Save(Root,ActiveName(),Reloaded,Links,nullptr,Error));
        TestTrue(TEXT("Saved shared file reloads"),Load(Root,ActiveName(),Reloaded,Links,Error));
        TestEqual(TEXT("Shared version survives reload without flipping again"),Reloaded->GetArrayField(TEXT("monsters"))[0]->AsObject()->GetNumberField(TEXT("damage_type")),16.);
    }
    return !HasAnyErrors();
}
#endif
