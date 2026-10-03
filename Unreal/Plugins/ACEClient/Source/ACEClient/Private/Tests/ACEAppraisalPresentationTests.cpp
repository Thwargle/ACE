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
    TestTrue(TEXT("Crafting and combat stats form separate paragraphs"),ArmorBody.Contains(TEXT("Workmanship: Flawless (7)\n\nArmor Level: 100")));
    TestTrue(TEXT("Related defense bonuses remain single spaced"),ArmorBody.Contains(TEXT("Melee Defense: +10.0%.\nBonus to Missile Defense: +20.0%.\nSlashing:")));
    TestFalse(TEXT("Sparse armor sections have no triple line breaks"),ArmorBody.Contains(TEXT("\n\n\n")));
    return !HasAnyErrors();
}
#endif
