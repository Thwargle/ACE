#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEquippedLookupTest, "ACE.VR.EquippedLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEEquippedLookupTest::RunTest(const FString&)
{
    FACESession Session; Session.PlayerGuid=123;
    for(int32 I=0;I<400;++I)
    {
        FACEWorldObject Item; Item.Guid=1000+I; Item.Name=TEXT("Equipment lookup performance fixture");
        Item.WielderId=I<20?123:456; Item.CurrentWieldedLocation=I<20?ACEEquipMask::Held:ACEEquipMask::MissileWeapon;
        Item.ItemType=ACEItemType::Caster;
        for(int32 P=0;P<16;++P)
        { auto& Part=Item.Appearance.AnimPartChanges.AddDefaulted_GetRef(); Part.PartIndex=P; Part.PartId=0x01000001; }
        Session.WorldObjects.Add(Item.Guid,Item);
    }
    auto& Launcher=Session.WorldObjects.FindChecked(1019); Launcher.CurrentWieldedLocation=ACEEquipMask::MissileWeapon;
    auto& Ammo=Session.WorldObjects.FindChecked(1018); Ammo.CurrentWieldedLocation=ACEEquipMask::MissileAmmo;
    Ammo.AmmoType=2; Ammo.StackSize=30; Ammo.WielderId=0; Ammo.ParentGuid=123;
    TestTrue(TEXT("Find own launcher rather than another player's"),Session.FindEquippedItem(ACEEquipMask::MissileWeapon)==&Launcher);
    TestTrue(TEXT("Parent-only ammo attachment resolves"),Session.FindEquippedItem(ACEEquipMask::MissileAmmo,0,2)==&Ammo);
    TestNull(TEXT("Arrows do not satisfy bolt-only ammo"),Session.FindEquippedItem(ACEEquipMask::MissileAmmo,0,1));
    TestNotNull(TEXT("Held caster query resolves"),Session.FindEquippedItem(ACEEquipMask::Held,ACEItemType::Caster));
    TestNull(TEXT("Item-type filter excludes held non-weapons"),Session.FindEquippedItem(ACEEquipMask::Held,ACEItemType::MeleeWeapon));
    for(int32 Direct : {0,1,0,1})
    {
        const double Start=FPlatformTime::Seconds(); int64 Sum=0;
        for(int32 I=0;I<4000;++I)
        {
            if(Direct) { if(auto* Item=Session.FindEquippedItem(ACEEquipMask::MissileWeapon)) Sum+=Item->Guid; }
            else
            {
                TArray<FACEWorldObject> Items; Session.GetEquippedItems(Items);
                for(const auto& Item:Items) if(Item.CurrentWieldedLocation & ACEEquipMask::MissileWeapon) { Sum+=Item.Guid;break; }
            }
        }
        TestEqual(TEXT("Both query paths identify the same launcher"),Sum,int64(1019)*4000);
        AddInfo(FString::Printf(TEXT("Equipped lookup direct=%d: %.3f us/query"),Direct,(FPlatformTime::Seconds()-Start)*1000000/4000));
    }
    Ammo.StackSize=29;
    TestEqual(TEXT("Next query sees ammunition consumption immediately"),Session.FindEquippedItem(ACEEquipMask::MissileAmmo)->StackSize,29);
    Launcher.CurrentWieldedLocation=0;
    TestNull(TEXT("Unequip immediately removes the launcher"),Session.FindEquippedItem(ACEEquipMask::MissileWeapon));
    Session.WorldObjects.Remove(1018);
    TestNull(TEXT("Deleted ammunition leaves no stale reference"),Session.FindEquippedItem(ACEEquipMask::MissileAmmo));
    Session.PlayerGuid=0;
    TestNull(TEXT("Logged-out player has no equipped caster"),Session.FindEquippedItem(ACEEquipMask::Held));
    return true;
}
#endif
