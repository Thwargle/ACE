#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/ACEAppraisalFormatting.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAppraisalPresentationTest, "ACE.RetailParity.AppraisalPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEAppraisalPresentationTest::RunTest(const FString&)
{
    FACEAppraisalInfo Info;
    Info.bIsCreature=Info.bSuccess=true;Info.Strength=200;Info.Coordination=180;Info.Quickness=190;
    Info.Health=0;Info.MaxHealth=200;Info.Stamina=75;Info.MaxStamina=100;Info.Mana=0;Info.MaxMana=0;
    Info.AttributeHighlights=1|8;Info.AttributeColors=1;
    auto Rows=ACEAppraisalFormatting::CreatureStatLines(Info);
    TestEqual(TEXT("Both interfaces retain the nine retail stat rows"),Rows.Num(),9);
    TestTrue(TEXT("Beneficial and harmful colors use the wire masks in retail display order"),Rows[0].Color==FLinearColor::Green && Rows[2].Color==FLinearColor::Red && Rows[3].Color==FLinearColor::White);
    TestEqual(TEXT("Depleted health is a known zero, including retail percentage"),Rows[6].Value,FString(TEXT("0/200 (0 %)")));
    TestEqual(TEXT("Missing maximum remains unknown"),Rows[8].Value,FString(TEXT("???")));
    Info.bSuccess=false;Info.Health=120;
    Rows=ACEAppraisalFormatting::CreatureStatLines(Info);
    TestEqual(TEXT("Failed appraisal exposes only the health percentage"),Rows[6].Value,FString(TEXT("60 %")));
    TestEqual(TEXT("Failed appraisal keeps stamina unknown"),Rows[7].Value,FString(TEXT("???")));
    TestTrue(TEXT("Failed appraisal marks every value with the retail unknown color"),Rows.ContainsByPredicate([](const auto& Row){return Row.Color==FLinearColor::Yellow;})
        && !Rows.ContainsByPredicate([](const auto& Row){return Row.Color!=FLinearColor::Yellow;}));
    Info.StringProperties.Add(5,TEXT("War Mage"));Info.IntProperties.Add(261,1);Info.IntProperties.Add(134,ACEPlayerKillerStatus::PK);
    auto Headings=ACEAppraisalFormatting::CreatureHeadings(Info,nullptr);
    TestEqual(TEXT("Title replaces the profession in both interfaces"),Headings.Profession,FString(TEXT("Adventurer")));
    TestEqual(TEXT("Appraisal PK fallback is shared"),Headings.PlayerKiller,FString(TEXT("Player Killer")));
    FACEWorldObject Object;Object.ObjectDescriptionFlags=ACEObjectDescFlag::PkLiteStatus;
    Headings=ACEAppraisalFormatting::CreatureHeadings(Info,nullptr,&Object);
    TestEqual(TEXT("Current object PK status supersedes stale appraisal data"),Headings.PlayerKiller,FString(TEXT("Player Killer Lite")));
    FACEAppraisalInfo Item;
    FACEAppraisalInfo Gear; Gear.bSuccess=true;
    Gear.IntProperties={{370,5},{371,6},{372,7},{373,8},{374,9},{375,10},{376,11},{377,12},{378,13},{379,25}};
    const FString GearText=ACEAppraisalFormatting::ItemExaminationText(Gear,nullptr);
    TestTrue(TEXT("Equipment ratings come from server Gear qualities in retail order"),GearText.Contains(
        TEXT("Ratings: Dam 5, Dam Resist 6, Crit 7, Crit Dam 9, Crit Resist 8, Crit Dam Resist 10, Heal Boost 11, Nether Resist 12, Life Resist 13")));
    TestTrue(TEXT("Equipment vitality has its separate retail description"),GearText.Contains(TEXT("This item adds 25 Vitality.")));
    Item.bSuccess=Item.bHasValue=Item.bHasBurden=true;Item.Value=9000;Item.Burden=50;
    Item.StringProperties.Add(14,TEXT("Use on a magic item to give the stone's stored Mana to that item."));
    Item.StringProperties.Add(15,TEXT("Short fallback."));
    Item.StringProperties.Add(16,TEXT("First paragraph.\r\n\r\nSecond paragraph.\r\nNext line."));
    Item.IntProperties.Add(107,5000);Item.FloatProperties.Add(87,1.);Item.FloatProperties.Add(137,1.);
    Item.Summary=Item.StringProperties[15]+TEXT("\n")+Item.StringProperties[14]+TEXT("\n")+Item.StringProperties[16];
    const FString Body=ACEAppraisalFormatting::ItemExaminationText(Item,nullptr);
    TestTrue(TEXT("Value/burden are consecutive, usage begins a new paragraph"),Body.StartsWith(TEXT("Value: 9,000\nBurden: 50\n\nUse on a magic item")));
    TestTrue(TEXT("Mana info continues directly after usage and separates the description"),Body.Contains(TEXT("that item.\nStored Mana: 5000\nEfficiency: 100%\nChance of Destruction: 100%\n\nFirst paragraph.")));
    TestTrue(TEXT("Authored description paragraphs and line breaks survive in both interfaces"),Body.EndsWith(TEXT("First paragraph.\n\nSecond paragraph.\nNext line.")));
    TestFalse(TEXT("Long description replaces short description"),Body.Contains(TEXT("Short fallback")));
    TestEqual(TEXT("Usage appears only once regardless of wire order"),Body.Find(TEXT("Use on a magic item")),Body.Find(TEXT("Use on a magic item"),ESearchCase::CaseSensitive,ESearchDir::FromEnd));
    const FString VRBody=ACEAppraisalFormatting::ItemExaminationText(Item,nullptr,false,false);
    TestFalse(TEXT("Separate VR value/burden chrome does not leave a leading blank line"),VRBody.StartsWith(TEXT("\n")));
    TestFalse(TEXT("Section joins do not invent triple line breaks"),Body.Contains(TEXT("\n\n\n")));
    Item.StringProperties[16].Empty();Item.Summary.Empty();
    TestTrue(TEXT("Empty long description falls back to the short description"),
        ACEAppraisalFormatting::ItemExaminationText(Item,nullptr).EndsWith(TEXT("Short fallback.")));
    FACEAppraisalInfo FailedItem;
    TestTrue(TEXT("Failed item appraisal keeps its message after the unknown value/burden prefix"),
        ACEAppraisalFormatting::ItemExaminationText(FailedItem,nullptr).Contains(TEXT("You fail to appraise the item.")));
    TestEqual(TEXT("Failed item appraisal keeps its message when value/burden have separate chrome"),
        ACEAppraisalFormatting::ItemExaminationText(FailedItem,nullptr,false,false),FString(TEXT("You fail to appraise the item.")));
    FACEAppraisalInfo Armor;
    Armor.bSuccess=Armor.bHasValue=Armor.bHasBurden=true;
    Armor.IntProperties={{171,2},{105,7},{28,100}};
    Armor.FloatProperties={{29,1.1},{149,1.2}};
    Armor.ArmorResistances={1.f,1.f};
    Armor.StringProperties.Add(16,TEXT("Armor description."));
    const FString ArmorBody=ACEAppraisalFormatting::ItemExaminationText(Armor,nullptr);
    TestTrue(TEXT("Crafting follows burden without an extra empty line"),ArmorBody.Contains(TEXT("Burden: 0\nThis item has been tinkered 2 times.")));
    TestTrue(TEXT("Crafting and combat stats form separate paragraphs"),ArmorBody.Contains(TEXT("Workmanship: Flawless (7)\n\nBonus to Melee Defense:")));
    TestTrue(TEXT("Defense bonuses precede a separate armor paragraph in retail order"),ArmorBody.Contains(TEXT("Melee Defense: +10.0%.\nBonus to Missile Defense: +20.0%.\n\nArmor Level: 100\nSlashing:")));
    TestFalse(TEXT("Sparse armor sections have no triple line breaks"),ArmorBody.Contains(TEXT("\n\n\n")));
    FACEAppraisalInfo Cloak; Cloak.bSuccess=true; Cloak.IntProperties={{28,0}};
    Cloak.ArmorResistances={1.f,1.f};
    TestFalse(TEXT("Zero armor on cloaks does not fabricate an armor paragraph"),ACEAppraisalFormatting::ItemDetails(Cloak).Contains(TEXT("Armor Level:")));
    Cloak.IntProperties[28]=100;Cloak.ArmorResistances.Reset();
    TestFalse(TEXT("Armor modifiers require the retail appraisal armor profile"),ACEAppraisalFormatting::ItemDetails(Cloak).Contains(TEXT("Armor Level:")));
    FACEAppraisalInfo Bow;Bow.bSuccess=Bow.bHasWeaponProfile=true;Bow.ItemType=ACEItemType::MissileWeapon;
    Bow.IntProperties={{9,static_cast<int32>(ACEEquipMask::MissileWeapon)},{50,1}};
    Bow.WeaponDamageMod=2.5f;Bow.Damage=5;Bow.WeaponTime=20;Bow.WeaponMaxVelocity=40;
    for(int32 Ammo:{1,2,4})
    {
        Bow.IntProperties[50]=Ammo;
        const FString Detail=ACEAppraisalFormatting::ItemDetails(Bow);
        TestTrue(TEXT("Bow, crossbow and atlatl display percentage damage modifier"),Detail.Contains(TEXT("Damage Modifier: +150%")));
        TestTrue(TEXT("Launchers identify ammunition damage as a bonus"),Detail.Contains(TEXT("Damage Bonus: 5")));
    }
    Bow.IntProperties[50]=0;
    TestFalse(TEXT("Thrown weapon has no launcher multiplier"),ACEAppraisalFormatting::ItemDetails(Bow).Contains(TEXT("Damage Modifier:")));
    Bow.IntProperties[50]=1;Bow.bSuccess=false;
    TestTrue(TEXT("Failed launcher appraisal does not disclose the multiplier"),ACEAppraisalFormatting::ItemDetails(Bow).Contains(TEXT("Damage Modifier: Unknown")));
    FACEAppraisalInfo Shield;Shield.bSuccess=true;Shield.IntProperties={{9,static_cast<int32>(ACEEquipMask::Shield)},{28,400}};
    Shield.ArmorResistances={1.f,1.1f,1.2f,1.3f,1.4f,1.5f,1.6f,1.7f};
    FACEPlayerVitals Viewer;FACESkillInfo Skill;Skill.SkillId=48;Skill.Current=300;Skill.AdvancementClass=2;Viewer.Skills.Add(Skill);
    auto ShieldText=ACEAppraisalFormatting::ItemDetails(Shield,nullptr,&Viewer);
    TestTrue(TEXT("Shield shows base and trained skill-limited effective level"),ShieldText.Contains(TEXT("Base Shield Level: 400\nEffective Shield Level : 150 (with Shield skill)")));
    TestTrue(TEXT("Shield resistance section has retail's blank before Armor Level"),ShieldText.Contains(TEXT("(with Shield skill)\n\nArmor Level: 400")));
    Viewer.Skills[0].AdvancementClass=3;Viewer.Skills[0].Current=500;
    TestTrue(TEXT("Specialized Shield skill caps at the base level"),ACEAppraisalFormatting::ItemDetails(Shield,nullptr,&Viewer).Contains(TEXT("Effective Shield Level : 400")));
    int32 Previous=-1;
    for(const TCHAR* Name:{TEXT("Slashing:"),TEXT("Piercing:"),TEXT("Bludgeoning:"),TEXT("Fire:"),TEXT("Cold:"),TEXT("Acid:"),TEXT("Electric:"),TEXT("Nether:")})
    {const int32 At=ShieldText.Find(Name);TestTrue(TEXT("Resistances use retail display order without reordering the wire values"),At>Previous);Previous=At;}
    Shield.IntProperties.Remove(28);
    TestTrue(TEXT("Unknown shield level is explicit"),ACEAppraisalFormatting::ItemDetails(Shield).Contains(TEXT("Shield Level: Unknown")));
    Armor.IntProperties.Add(4,0xB1E);Armor.IntProperties.Add(265,14);Armor.ItemType=ACEItemType::Armor;
    const auto CoverageText=ACEAppraisalFormatting::ItemDetails(Armor);
    TestTrue(TEXT("Armor set and coverage follow server qualities and retail body-part order"),CoverageText.Contains(
        TEXT("Set: Adept's\n\nCovers Chest, Abdomen, Upper Legs, Lower Legs")));
    FACEAppraisalInfo Ammo;Ammo.IntProperties={{9,static_cast<int32>(ACEEquipMask::MissileAmmo)},{50,2}};
    TestTrue(TEXT("Ammunition describes its launcher without needing a weapon profile"),ACEAppraisalFormatting::ItemDetails(Ammo).Contains(TEXT("Used as ammunition by crossbows.")));
    TestFalse(TEXT("Ammunition does not invent weapon speed"),ACEAppraisalFormatting::ItemDetails(Ammo).Contains(TEXT("Speed:")));
    Shield.IntProperties.Add(28,400);Shield.ArmorEnchantments=1u|(1u<<16)|16u;
    ShieldText=ACEAppraisalFormatting::ItemDetails(Shield);
    const auto ShieldColors=ACEAppraisalFormatting::ItemTextColors(Shield,ShieldText);
    TestTrue(TEXT("Base shield level uses the armor enchantment color"),ShieldColors[ShieldText.Find(TEXT("Base Shield Level:"))].G>.8f);
    TestTrue(TEXT("Reordered Cold resistance keeps the correct harmful wire color"),ShieldColors[ShieldText.Find(TEXT("Cold:"))].R>.9f);
    FACEAppraisalInfo Cleaving;Cleaving.bSuccess=true;Cleaving.ItemType=ACEItemType::MeleeWeapon;
    Cleaving.FloatProperties.Add(157,1.2);Cleaving.IntProperties.Add(263,4);
    Cleaving.IntProperties.Add(45,1); // Actual weapon damage may differ from its resistance cleave.
    for(bool IncludeChrome:{false,true})
    {
        const FString Details=ACEAppraisalFormatting::ItemExaminationText(Cleaving,nullptr,IncludeChrome,IncludeChrome);
        TestTrue(TEXT("Desktop and VR expose server-provided Bludgeoning resistance cleave"),Details.Contains(TEXT("Properties: Resistance Cleaving: Bludgeoning")));
        TestFalse(TEXT("Resistance cleave is not misreported as an imbue"),Details.Contains(TEXT("cannot be further imbued")));
    }
    for(const auto& Type:{TPair<int32,const TCHAR*>(1,TEXT("Slashing")),{2,TEXT("Piercing")},{4,TEXT("Bludgeoning")},{8,TEXT("Cold")},
        {16,TEXT("Fire")},{32,TEXT("Acid")},{64,TEXT("Electrical")},{1024,TEXT("Nether")},{0x10000000,TEXT("Prismatic")},{5,TEXT("Slashing/Bludgeoning")}})
    {
        Cleaving.IntProperties[263]=Type.Key;
        TestTrue(TEXT("Cleave damage labels and combined flags match retail"),ACEAppraisalFormatting::ItemDetails(Cleaving).Contains(FString(TEXT("Resistance Cleaving: "))+Type.Value));
    }
    Cleaving.FloatProperties[157]=1.;
    TestTrue(TEXT("Retail checks modifier presence, not its magnitude"),ACEAppraisalFormatting::ItemDetails(Cleaving).Contains(TEXT("Resistance Cleaving:")));
    Cleaving.IntProperties.Remove(263);
    TestFalse(TEXT("Modifier alone does not invent a resistance type"),ACEAppraisalFormatting::ItemDetails(Cleaving).Contains(TEXT("Resistance Cleaving:")));
    Cleaving.IntProperties.Add(263,4);Cleaving.FloatProperties.Remove(157);
    TestFalse(TEXT("Resistance type alone does not imply cleaving"),ACEAppraisalFormatting::ItemDetails(Cleaving).Contains(TEXT("Resistance Cleaving:")));
    Cleaving.FloatProperties.Add(157,1.2);Cleaving.bSuccess=false;
    // Retail's special-properties section displays fields the server supplied,
    // even when other parts of an appraisal failed. It does not gate on success.
    TestTrue(TEXT("Received partial-appraisal properties remain visible like retail"),ACEAppraisalFormatting::ItemExaminationText(Cleaving,nullptr).Contains(TEXT("Resistance Cleaving: Bludgeoning")));
    Cleaving.IntProperties.Reset();Cleaving.FloatProperties.Reset();
    TestFalse(TEXT("Failed appraisal without properties does not invent cleaving"),ACEAppraisalFormatting::ItemExaminationText(Cleaving,nullptr).Contains(TEXT("Resistance Cleaving:")));
    FACEAppraisalInfo Requirements; Requirements.bSuccess=true;
    Requirements.IntProperties={{158,7},{159,0},{160,180},{270,4},{271,1},{272,200},
        {273,9},{274,287},{275,500},{276,7},{277,0},{278,200},
        {109,150},{115,100},{176,34},{257,5},{258,250},{259,5},{260,300}};
    Requirements.StringProperties.Add(16,TEXT("Ancient craftsmanship.\n\nThe forgotten king's final gift."));
    const FString AllRequirements=ACEAppraisalFormatting::ItemExaminationText(Requirements,nullptr);
    for (const TCHAR* Line : {TEXT("Wield requires Level 180"),TEXT("Wield requires base Strength 200"),
        TEXT("Standing with the Celestial Hand 500"),TEXT("Wield requires Level 200"),
        TEXT("Activation requires Arcane Lore: 150"),TEXT("Activation requires Skill 34: 100"),
        TEXT("Activation requires Focus: 250"),TEXT("Activation requires Mana: 300"),
        TEXT("Ancient craftsmanship.\n\nThe forgotten king's final gift.")})
        TestTrue(FString(TEXT("Shared inspection retains "))+Line,AllRequirements.Contains(Line));
    Requirements.IntProperties.Add(172,4);Requirements.IntProperties.Add(177,2);Requirements.IntProperties.Add(178,21);
    TestTrue(TEXT("Retail description decoration retains gem details at the bottom"),
        ACEAppraisalFormatting::ItemExaminationText(Requirements,nullptr).Contains(TEXT(", set with 2 ")));
    Requirements.IntProperties.Add(178,38);
    TestTrue(TEXT("Retail gem description pluralizes rubies"),
        ACEAppraisalFormatting::ItemExaminationText(Requirements,nullptr).Contains(TEXT(", set with 2 Rubies")));
    Requirements.IntProperties.Add(178,24);
    TestTrue(TEXT("Retail gem description uses pieces for uncountable materials"),
        ACEAppraisalFormatting::ItemExaminationText(Requirements,nullptr).Contains(TEXT(", set with 2 pieces of ")));
    FACEAppraisalInfo Magic; Magic.bSuccess=true; Magic.SpellIds={1};
    Magic.IntProperties={{158,7},{160,50},{109,100}};
    for (bool Chrome : {false,true})
    {
        const auto Text=ACEAppraisalFormatting::ItemExaminationText(Magic,nullptr,Chrome,Chrome);
        TestTrue(TEXT("Spell list and requirements have a blank line on desktop and VR"),Text.Contains(TEXT("Spells: Spell 1\n\nWield requires Level 50\nActivation requires Arcane Lore: 100")));
        TestFalse(TEXT("Shared inspection does not introduce triple breaks"),Text.Contains(TEXT("\n\n\n")));
    }
    Gear.IntProperties.Add(265,14); Gear.IntProperties.Add(28,100); Gear.ArmorResistances={1.f};
    const auto RatedArmor=ACEAppraisalFormatting::ItemDetails(Gear);
    TestTrue(TEXT("Gear ratings follow the set and precede armor stats"),RatedArmor.Contains(TEXT("Set: Adept's\nRatings:"))
        && RatedArmor.Find(TEXT("Ratings:"))<RatedArmor.Find(TEXT("Armor Level:")));
    TestTrue(TEXT("Vitality immediately follows the gear rating row"),RatedArmor.Contains(TEXT("Life Resist 13\nThis item adds 25 Vitality.")));
    FACEAppraisalInfo Portal; Portal.bSuccess=true;
    Portal.IntProperties={{86,20},{87,40}}; Portal.StringProperties={{14,TEXT("Use this portal.")},{38,TEXT("A custom destination")},{16,TEXT("An ancient passage.")}};
    TestTrue(TEXT("Portal limits and destination precede its description"),ACEAppraisalFormatting::ItemExaminationText(Portal,nullptr,false,false).Contains(
        TEXT("Use this portal.\n\nRestricted to characters of Levels 20 to 40.\n\nDestination: A custom destination\n\nAn ancient passage.")));
    for (const auto& Limit : {TPair<int32,const TCHAR*>(20,TEXT("Level 20.")),{0,TEXT("Level 20 or greater.")}})
    {
        Portal.IntProperties[87]=Limit.Key;
        TestTrue(TEXT("Equal and unbounded portal level limits match retail"),ACEAppraisalFormatting::ItemDetails(Portal).Contains(Limit.Value));
    }
    FACEAppraisalInfo Door; Door.bSuccess=true; Door.BoolProperties={{3,true}}; Door.IntProperties={{38,200},{173,70}};
    Door.StringProperties.Add(16,TEXT("A sturdy door."));
    TestEqual(TEXT("Door inspection separates lock difficulty and authored description"),ACEAppraisalFormatting::ItemExaminationText(Door,nullptr,false,false),
        FString(TEXT("Locked\n\nThe lock looks mildly challenging to pick (Resistance 200).\n\nA sturdy door.")));
    Door.BoolProperties[3]=false;
    TestEqual(TEXT("Unlocked doors omit lock difficulty"),ACEAppraisalFormatting::ItemExaminationText(Door,nullptr,false,false),FString(TEXT("Unlocked\n\nA sturdy door.")));
    FACEAppraisalInfo Sign; Sign.bSuccess=true; Sign.StringProperties={{16,TEXT("Welcome.\r\n\r\nKeep out of the mine.")}};
    TestEqual(TEXT("Signs retain authored paragraphs without creature or gear rows"),ACEAppraisalFormatting::ItemExaminationText(Sign,nullptr,false,false),FString(TEXT("Welcome.\n\nKeep out of the mine.")));
    FACEAppraisalInfo Use; Use.bSuccess=true; Use.BoolProperties={{85,true}}; Use.StringProperties={{25,TEXT("Thwargle")}};
    Use.IntProperties={{369,100},{366,54},{367,570},{368,54}};
    const auto UseText=ACEAppraisalFormatting::ItemDetails(Use);
    TestTrue(TEXT("Owner restriction is separate from activation and use restrictions"),UseText.Contains(TEXT("Wield requires Thwargle\n\nUse requires level 100.\nUse requires Unknown Skill of at least 570.\nUse requires specialized Unknown Skill.")));
    FACEAppraisalInfo Salvage; Salvage.bSuccess=true; Salvage.IntProperties={{105,29},{170,4}};
    FACEAppraisalInfo Special; Special.bSuccess=true; Special.FloatProperties={{136,0.2},{147,0.1},{155,0.5}};
    Special.BoolProperties={{130,true}}; Special.IntProperties={{158,7},{160,100}};
    TestTrue(TEXT("Weapon special properties and left-hand tether precede requirements"),ACEAppraisalFormatting::ItemDetails(Special).Contains(
        TEXT("Properties: Crushing Blow, Biting Strike, Armor Cleaving\nThis item is tethered to the left side.\n\nWield requires Level 100")));
    TestTrue(TEXT("Salvage displays average workmanship and contributing items"),ACEAppraisalFormatting::ItemDetails(Salvage).Contains(TEXT("Workmanship: Flawless (7.25)\n\nSalvaged from 4 items.")));
    FACEAppraisalInfo Leveled; Leveled.Int64Properties={{5,1000},{4,3500}}; Leveled.IntProperties={{319,5},{320,1}};
    for (int32 Scheme : {1,2,3})
    {
        Leveled.IntProperties[320]=Scheme;
        const auto Text=ACEAppraisalFormatting::ItemLevelDetails(Leveled);
        const TCHAR* Expected=Scheme==1?TEXT("Item Level: 3 / 5\nItem XP: 3,500 / 4,000")
            :Scheme==2?TEXT("Item Level: 2 / 5\nItem XP: 3,500 / 7,000"):TEXT("Item Level: 2 / 5\nItem XP: 3,500 / 6,000");
        TestEqual(TEXT("Item XP follows retail's progression schemes"),Text,FString(Expected));
    }
    Leveled.IntProperties[320]=1;Leveled.Int64Properties[4]=5000;
    TestTrue(TEXT("Capped item uses the maximum-level XP threshold"),ACEAppraisalFormatting::ItemLevelDetails(Leveled).Contains(TEXT("Item Level: 5 / 5\nItem XP: 5,000 / 5,000")));
    // Character/NPC and monster appraisals deliberately have different sections.
    FACEAppraisalInfo Creature; Creature.bIsCreature=Creature.bSuccess=true; Creature.Name=TEXT("Wasp");
    TestFalse(TEXT("A monster does not use character examination"),ACEAppraisalFormatting::UsesCharacterExamination(Creature));
    TestTrue(TEXT("An ordinary monster does not invent player history or armor legend"),ACEAppraisalFormatting::CreatureDetailLines(Creature).IsEmpty());
    Creature.StringProperties={{5,TEXT("War Mage")},{21,TEXT("Monarch")},{35,TEXT("Monarch")},{10,TEXT("Friends")}};
    Creature.IntProperties={{30,2},{43,0}}; Creature.ArmorLevels={10000,10,20,30,40,50,60,70,80};
    TestTrue(TEXT("Profession-bearing NPCs use the same character panel as players"),ACEAppraisalFormatting::UsesCharacterExamination(Creature));
    const auto Details=ACEAppraisalFormatting::CreatureDetailLines(Creature);
    for (const TCHAR* Value : {TEXT("Monarch"),TEXT("Friends"),TEXT("Has never died"),TEXT("AL: *1/10/20")})
        TestTrue(FString(TEXT("Character inspection retains "))+Value,Details.ContainsByPredicate([&](const auto& Row){return Row.Value==Value;}));
    return !HasAnyErrors();
}
#endif
