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
    return !HasAnyErrors();
}
#endif
