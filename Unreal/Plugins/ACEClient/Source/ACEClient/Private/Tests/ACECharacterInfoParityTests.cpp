#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/ACEUIGameplayBinder.h"
#include "ACESession.h"

static FORCENOINLINE void CheckCharacterAugmentationText(FAutomationTestBase& Test, const FString& Info)
{
        Test.TestTrue(TEXT("Zero chess rank is not replaced by a fabricated rating"),Info.Contains(TEXT("Chess Rank: 0")));
        for (const TCHAR* Part : {
            TEXT("You have augmented your ability to avoid critical hits."),
            TEXT("25% of critical hits from creatures and 5% of critical hits from players"),
            TEXT("You have augmented the duration of the spells you cast 1 time."),
            TEXT("You earned the Frenzy of the Slayer augmentation."),
            TEXT("You earned the Iron Skin of the Invincible augmentation."),
            TEXT("You earned the Hand of the Remorseless augmentation.")})
            Test.TestTrue(Part,Info.Contains(Part));
        Test.TestTrue(TEXT("Augmentations retain retail paragraph breaks"),Info.Contains(TEXT("normal damage.\n\n")));
        Test.TestFalse(TEXT("Raw augmentation property labels are not shown"),Info.Contains(TEXT("Critical defense augmentation: 1")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECharacterInfoParityTest, "ACE.RetailParity.CharacterInformation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECharacterInfoParityTest::RunTest(const FString& Parameters)
{
    auto* Binder=NewObject<UACEUIGameplayBinder>();
    TestTrue(TEXT("Retail character strings load"),Binder->SocialStrings.LoadStrings(TEXT("C:/Turbine/Asheron's Call"),0x23000001));
    Binder->bLoadedSocialStrings=true;
    FACESession Session;
    for (int32 Property : {233,238,309,310,299}) Session.PlayerVitals.StatQualityInts.Add(Property,1);
    Binder->LastVitals=Session.PlayerVitals;
    const FString CharacterText=Binder->BuildCharacterInformation();
    CheckCharacterAugmentationText(*this,CharacterText);
    Binder->LastVitals.StatQualityInts.Add(238,2);
    TestTrue(TEXT("Retail augmentation descriptions pluralize repeated purchases"),
        Binder->BuildCharacterInformation().Contains(TEXT("spells you cast 2 times.")));
    for (int32 Mastery : {1,2,3,99})
    {
        FACEBinaryWriter Update;Update.WriteUInt8(static_cast<uint8>(Mastery));Update.WriteUInt32(362);Update.WriteInt32(Mastery);
        FACEBinaryReader Reader(Update.GetData());Session.HandlePrivateUpdatePropertyInt(Reader);
        Binder->LastVitals=Session.PlayerVitals;
        const FString Updated=Binder->BuildCharacterInformation();
        const TCHAR* Expected=Mastery==1?TEXT("Primalist"):Mastery==2?TEXT("Necromancer"):Mastery==3?TEXT("Naturalist"):TEXT("Unknown");
        TestTrue(TEXT("Summoning mastery follows the retail wire enum"),Updated.Contains(FString::Printf(TEXT("Your summoning mastery is %s."),Expected)));
    }
    return true;
}
#endif
