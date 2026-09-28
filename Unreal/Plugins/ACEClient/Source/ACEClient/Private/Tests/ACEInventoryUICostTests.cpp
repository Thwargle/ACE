#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACECombatStance.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEInventoryUICostTest, "ACE.Performance.InventoryUI",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEInventoryUICostTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>(); GI->Init();
    auto* Client = GI->GetSubsystem<UACEClientSubsystem>();
    auto Session = Client->GetSession();
    auto* Binder = NewObject<UACEUIGameplayBinder>(); Binder->Client = Client;
    Session->PlayerGuid = 123;
    Binder->SelectedPackGuid = 123; Binder->OpenVendorGuid = 900;
    auto MakeItem = [](int32 Guid, int32 Container, int32 Position)
    {
        FACEWorldObject O; O.Guid = Guid; O.ContainerId = Container; O.PlacementPosition = Position;
        O.Name = TEXT("Inventory UI benchmark item with appearance data");
        O.IconId = 0x060011CB; O.ItemType = ACEItemType::Misc; O.StackSize = 20; O.Value = 50;
        for (int32 P = 0; P < 16; ++P)
        { auto& Part = O.Appearance.AnimPartChanges.AddDefaulted_GetRef(); Part.PartIndex = P; Part.PartId = 0x01000001; }
        return O;
    };
    auto AddRef = [&](int32 Container, int32 Guid, int32 Type = 0)
    { auto& R = Session->ContainerContents.FindOrAdd(Container).AddDefaulted_GetRef(); R.ItemGuid = Guid; R.ContainerType = Type; };
    // Twenty side packs, alternating ViewContents and pre-ViewContents arrivals.
    for (int32 P = 0; P <= 20; ++P)
    {
        const int32 Container = P == 0 ? 123 : 200 + P;
        if (P != 0)
        {
            auto Pack = MakeItem(Container, 123, P - 1); Pack.ItemsCapacity = 24; Pack.ItemType = ACEItemType::Container;
            Session->WorldObjects.Add(Container, Pack); AddRef(123, Container, 1);
        }
        for (int32 I = 0; I < 24; ++I)
        {
            const auto O = MakeItem(1000 + P * 24 + I, Container, I);
            Session->WorldObjects.Add(O.Guid, O);
            if (P % 2 == 0) AddRef(Container, O.Guid);
        }
    }
    for (int32 I = 0; I < 24; ++I)
    {
        auto O = MakeItem(3000 + I, 0, I); O.WielderId = 123;
        O.CurrentWieldedLocation = I == 0 ? ACEEquipMask::Held : ACEEquipMask::ChestArmor;
        Session->WorldObjects.Add(O.Guid, O);
    }
    for (int32 I = 0; I < 400; ++I)
    {
        auto O = MakeItem(4000 + I, 900, I); Session->VendorMerchandise.Add(O);
        O.ContainerId = 0; Session->WorldObjects.Add(O.Guid, O);
    }
    for (int32 Phase = 0; Phase < 3; ++Phase)
    {
        auto Measure = [&](const TCHAR* Name, auto&& Work)
        {
            const uint64 Expected = Work(); uint64 Sum = 0;
            const double Start = FPlatformTime::Seconds();
            for (int32 I = 0; I < 2000; ++I) Sum += Work();
            AddInfo(FString::Printf(TEXT("InventoryUI %s phase %d: %.3f us/call; result=%llu"), Name, Phase,
                (FPlatformTime::Seconds() - Start) * 1.e6 / 2000, static_cast<unsigned long long>(Expected)));
            TestEqual(TEXT("Unchanged state produces stable UI results"), Sum, Expected * 2000);
        };
        Measure(TEXT("inventory hash"), [&] { return Binder->HashInventoryOverlayState(); });
        Measure(TEXT("vendor hash"), [&] { return Binder->HashVendorOverlayState(); });
        Measure(TEXT("vendor filters"), [&] { Binder->RebuildVendorVisibleFilters(); return uint64(Binder->VendorVisibleFilterIndices.Num()); });
        Measure(TEXT("equipment mode"), [&] { return uint64(Binder->ResolveEquippedCombatMode()); });
    }
    TMap<int32, int32> Counts;
    Counts.Add(123, -1); Counts.Add(999, -1);
    for (int32 P = 1; P <= 20; ++P) Counts.Add(200 + P, -1);
    auto CheckCounts = [&]
    {
        Session->GetPackItemCounts(Counts);
        for (const auto& Count : Counts)
        {
            TArray<FACEWorldObject> Items; Session->GetPackItems(Count.Key, Items);
            TestEqual(FString::Printf(TEXT("Count matches visible slots for container %d"), Count.Key), Count.Value, Items.Num());
        }
    };
    CheckCounts();
    TestEqual(TEXT("Main pack excludes side packs and equipment"), Counts[123], 24);
    // Main-pack late arrivals, authoritative empty side pack, unresolved references,
    // pack/foci slots, nested fallback packs, duplicate references, and deleted items.
    Session->WorldObjects.Add(6000, MakeItem(6000, 123, 24));
    Session->ContainerContents.FindOrAdd(201).Reset();
    AddRef(123, 6001); AddRef(202, 6001); AddRef(202, 6002, 2);
    AddRef(202, 1048); AddRef(123, 1000);
    Session->WorldObjects.Remove(1049);
    auto NestedPack = MakeItem(6003, 203, 25); NestedPack.ItemsCapacity = 24;
    Session->WorldObjects.Add(6003, NestedPack);
    AddRef(202, 6003); AddRef(123, 6003);
    CheckCounts();
    TestEqual(TEXT("An authoritative empty pack stays empty"), Counts[201], 0);
    TestEqual(TEXT("Fallback pack retains its existing nested-container slots"), Counts[203], 25);
    Session->WorldObjects.Add(6001, MakeItem(6001, 202, 26)); CheckCounts();
    Session->WorldObjects[6001].ObjectDescriptionFlags = ACEObjectDescFlag::RequiresPackSlot; CheckCounts();
    Session->WorldObjects[6000].CurrentWieldedLocation = ACEEquipMask::Held;
    Session->WorldObjects[6000].WielderId = 123; CheckCounts();
    Session->ContainerContents.Remove(201); CheckCounts();
    auto Before = Binder->HashInventoryOverlayState();
    Session->WorldObjects[1000].StackSize = 19;
    TestTrue(TEXT("Stack changes invalidate the very next inventory query"), Before != Binder->HashInventoryOverlayState());
    Before = Binder->HashVendorOverlayState(); Session->VendorMerchandise[0].Value++;
    TestTrue(TEXT("Vendor price changes remain immediate"), Before != Binder->HashVendorOverlayState());
    Before = Binder->HashVendorOverlayState(); Session->VendorMerchandise[0].StackSize--;
    TestTrue(TEXT("Vendor quantity changes remain immediate"), Before != Binder->HashVendorOverlayState());
    Session->VendorMerchandise[0].ItemType = ACEItemType::MeleeWeapon;
    Binder->RebuildVendorVisibleFilters();
    TestTrue(TEXT("New merchandise category is visible immediately"), Binder->VendorVisibleFilterIndices == TArray<int32>({0,1,5}));
    Session->VendorMerchandise.Reset(); Session->WorldObjects[4000].ContainerId = 900;
    Session->WorldObjects[4000].ItemType = ACEItemType::Armor;
    Binder->RebuildVendorVisibleFilters();
    TestTrue(TEXT("Vendor without merchandise profiles uses container fallback"), Binder->VendorVisibleFilterIndices == TArray<int32>({0,2}));
    Binder->OpenVendorGuid = 0; Binder->RebuildVendorVisibleFilters();
    TestTrue(TEXT("Closed vendor retains only All"), Binder->VendorVisibleFilterIndices == TArray<int32>({0}));

    Session->WorldObjects.Reset(); Session->ContainerContents.Reset(); CheckCounts();
    const int64 Slots[] = {0, ACEEquipMask::Held, ACEEquipMask::MeleeWeapon, ACEEquipMask::MissileWeapon,
        ACEEquipMask::TwoHanded, ACEEquipMask::MissileAmmo, ACEEquipMask::Shield};
    // Compare against the existing array resolver across ownership, right-hand
    // preference, and overlapping equipment transitions during a weapon swap.
    for (int64 SlotA : Slots) for (int64 SlotB : Slots) for (int32 RightHand : {0,1,2})
    {
        auto A = MakeItem(7000, 0, 0); A.CurrentWieldedLocation = SlotA; A.WielderId = 123;
        A.ParentLocation = RightHand == 1 ? 1 : 2; A.ItemType = ACEItemType::MeleeWeapon;
        auto B = MakeItem(7001, 0, 0); B.CurrentWieldedLocation = SlotB; B.ParentGuid = 123;
        B.ParentLocation = RightHand == 2 ? 1 : 2; B.ItemType = ACEItemType::MissileWeapon;
        Session->WorldObjects.Add(A.Guid, A); Session->WorldObjects.Add(B.Guid, B);
        auto Foreign = A; Foreign.Guid = 7002; Foreign.WielderId = 456; Foreign.CurrentWieldedLocation = ACEEquipMask::Held;
        Session->WorldObjects.Add(Foreign.Guid, Foreign);
        TArray<FACEWorldObject> Equipped; Session->GetEquippedItems(Equipped);
        TestEqual(TEXT("Live mode agrees with array mode during equipment transitions"),
            uint32(Binder->ResolveEquippedCombatMode()), ACECombatStance::ResolveEquippedMode(Equipped));
        TestEqual(TEXT("Live caster lookup matches owned equipment"), Binder->HasEquippedCaster(),
            Equipped.ContainsByPredicate([](const auto& O) { return (O.CurrentWieldedLocation & ACEEquipMask::Held) != 0; }));
        TestEqual(TEXT("Live missile lookup excludes ammo and foreign equipment"), Binder->HasEquippedMissileWeapon(),
            Equipped.ContainsByPredicate([](const auto& O) { return (O.CurrentWieldedLocation & ACEEquipMask::MissileWeapon)
                || ((O.CurrentWieldedLocation & (ACEEquipMask::MeleeWeapon | ACEEquipMask::TwoHanded)) && (O.ItemType & ACEItemType::MissileWeapon)); }));
    }
    Session->PlayerGuid = 0;
    TestEqual(TEXT("Logged out player has no active weapon mode"), uint32(Binder->ResolveEquippedCombatMode()), ACECombatMode::Melee);
    // ViewContents before ObjectCreate must retain unresolved slots and their
    // legacy placeholder, then immediately pick up the arriving object's order/data.
    Session->WorldObjects.Reset(); Session->ContainerContents.Reset(); Session->PlayerGuid = 123;
    auto Arrived = MakeItem(8002, 200, 3); Session->WorldObjects.Add(8002, Arrived);
    AddRef(200, 8001); AddRef(200, 8002); AddRef(200, 8003, 1);
    TArray<int32> Ids; Session->GetPackItemGuids(200, Ids);
    TestTrue(TEXT("Pending item follows ordered creates and pending pack is excluded"), Ids == TArray<int32>({8002,8001}));
    TArray<FACEWorldObject> Copied; Session->GetPackItems(200, Copied);
    if (TestEqual(TEXT("Both resolved and pending inventory slots survive"), Copied.Num(), 2))
    {
        TestEqual(TEXT("Pending item retains placeholder name"), Copied[1].Name, FString::Printf(TEXT("Item 0x%08X"), 8001));
        TestEqual(TEXT("Pending item's container survives"), Copied[1].ContainerId, 200);
        TestEqual(TEXT("Full snapshot still includes appearance data"), Copied[0].Appearance.AnimPartChanges.Num(), 16);
    }
    Session->WorldObjects.Add(8001, MakeItem(8001, 200, 1));
    Session->GetPackItemGuids(200, Ids);
    TestTrue(TEXT("Late create immediately restores server placement order"), Ids == TArray<int32>({8001,8002}));
    Binder->SelectedPackGuid = 200;
    Before = Binder->HashInventoryOverlayState(); Session->WorldObjects[8001].IconOverlayId++;
    TestTrue(TEXT("Item overlay changes invalidate the live hash"), Before != Binder->HashInventoryOverlayState());
    Before = Binder->HashInventoryOverlayState(); Session->WorldObjects[8001].PlacementPosition = 5;
    TestTrue(TEXT("Item reorder invalidates the live hash"), Before != Binder->HashInventoryOverlayState());
    Session->ContainerContents[200].Reset(); Session->GetPackItemGuids(200, Ids);
    TestTrue(TEXT("Authoritative empty contents override existing creates"), Ids.IsEmpty());
    GI->Shutdown();
    return true;
}
#endif
