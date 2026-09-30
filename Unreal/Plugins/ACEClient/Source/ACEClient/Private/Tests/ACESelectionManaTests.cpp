#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESelectionManaTest, "ACE.RetailParity.SelectionMana",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACESelectionManaTest::RunTest(const FString&)
{
    FACESession Session;
    Session.State = EACESessionState::InWorld; Session.PlayerGuid = 1;
    Session.PlayerPosition.CellId = 0x7D640014;
    auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Address = Sockets->CreateInternetAddr(); bool Valid = false;
    Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(0);
    auto* Receiver = Sockets->CreateSocket(NAME_DGram, TEXT("Selection mana receiver"), false);
    if (!TestNotNull(TEXT("Local receiver created"), Receiver)) return false;
    ON_SCOPE_EXIT { Receiver->Close(); Sockets->DestroySocket(Receiver); };
    if (!TestTrue(TEXT("Local receiver bound"), Valid && Receiver->Bind(*Address))) return false;
    Receiver->GetAddress(*Address);
    Session.SocketC2S = Sockets->CreateSocket(NAME_DGram, TEXT("Selection mana sender"), false);
    if (!TestNotNull(TEXT("Local sender created"), Session.SocketC2S)) return false;
    ON_SCOPE_EXIT { Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S = nullptr; };
    Session.ServerC2SAddr = Address; Session.IssacClient = MakeUnique<FACEIsaac>(123u);
    auto Add = [&](int32 Guid, int32 Container)
    {
        FACEWorldObject Item; Item.Guid = Guid; Item.ContainerId = Container;
        Item.Name = TEXT("Custom item"); Item.ItemType = ACEItemType::Jewelry;
        Session.WorldObjects.Add(Guid, Item);
    };
    Add(20, 1); Add(21, 20); Add(22, 21); Add(23, 0);
    Session.WorldObjects[23].WielderId = 1;
    Add(24, 999); Add(25, 26); Add(26, 25);
    auto Sent = [&](uint32 Opcode, uint32 Target)
    {
        int32 Count = 0;
        for (const auto& Packet : Session.CachedC2SPackets)
        {
            FACEBinaryReader R(Packet.Value.Payload); R.Skip(24);
            if (R.CanRead(8) && R.ReadUInt32() == Opcode && R.ReadUInt32() == Target) ++Count;
        }
        return Count;
    };
    auto Reply = [&](int32 Guid, float Fraction, uint32 Success)
    {
        FACEBinaryWriter W; W.WriteUInt32(1); W.WriteUInt32(1); W.WriteUInt32(ACEGameEvent::QueryItemManaResponse);
        W.WriteUInt32(Guid); W.WriteFloat(Fraction); W.WriteUInt32(Success);
        FACEBinaryReader R(W.GetData()); Session.HandleGameEvent(R);
    };
    int32 Notifications = 0;
    Session.OnSelectionChanged.AddLambda([&](const FACESelectedObject&) { ++Notifications; });
    Session.SelectObject(22);
    TestEqual(TEXT("Nested owned item requests actual item mana"), Sent(ACEGameAction::QueryItemMana, 22), 1);
    TestEqual(TEXT("Owned item is not queried as a health target"), Sent(ACEGameAction::QueryHealth, 22), 0);
    TestFalse(TEXT("No fabricated full mana before server reply"), Session.SelectedObject.bShowMana);
    Reply(22, .37f, 1);
    TestTrue(TEXT("Successful reply enables mana vial"), Session.SelectedObject.bShowMana);
    TestEqual(TEXT("Mana vial uses server fraction"), Session.SelectedObject.ManaFraction, .37f);
    TestEqual(TEXT("Response notifies the UI"), Notifications, 2);
    Reply(24, .9f, 1);
    TestEqual(TEXT("Response for another selection is ignored"), Session.SelectedObject.ManaFraction, .37f);
    Session.SelectObject(22);
    TestEqual(TEXT("Reselecting keeps known fraction while refreshing"), Session.SelectedObject.ManaFraction, .37f);
    Reply(22, 0.f, 1);
    TestTrue(TEXT("Zero mana is an empty vial, not a missing vial"), Session.SelectedObject.bShowMana);
    TestEqual(TEXT("Zero mana retained"), Session.SelectedObject.ManaFraction, 0.f);
    Session.CachedC2SPackets.Reset(); Reply(22, 0.f, 0);
    TestFalse(TEXT("Nonmagical failure hides vial"), Session.SelectedObject.bShowMana);
    TestEqual(TEXT("Failure unsubscribes mana queries"), Sent(ACEGameAction::QueryItemMana, 0), 1);
    Reply(22, .8f, 1);
    TestFalse(TEXT("Late response after cancellation cannot show a stale vial"), Session.SelectedObject.bShowMana);
    Session.CachedC2SPackets.Reset(); Session.SelectObject(23); Reply(23, 2.f, 1);
    TestEqual(TEXT("Equipped item requests mana"), Sent(ACEGameAction::QueryItemMana, 23), 1);
    TestEqual(TEXT("Overfull server fraction stays inside the vial"), Session.SelectedObject.ManaFraction, 1.f);
    Reply(23, std::numeric_limits<float>::quiet_NaN(), 1);
    TestFalse(TEXT("Malformed fraction cannot corrupt UI geometry"), Session.SelectedObject.bShowMana);
    Session.SelectObject(22); Reply(22, .5f, 1);
    Session.WorldObjects[22].ContainerId = 999;
    Reply(22, .6f, 1);
    TestFalse(TEXT("An item that left inventory loses its mana subscription"), Session.SelectedObject.bShowMana);
    for (int32 Guid : {24, 25})
    {
        Session.CachedC2SPackets.Reset(); Session.SelectObject(Guid);
        TestEqual(TEXT("Unowned or cyclic ownership cannot request item mana"), Sent(ACEGameAction::QueryItemMana, Guid), 0);
    }
    Session.WorldObjects[22].ContainerId = 1; Session.WorldObjects[22].StackSize = 10;
    Session.CachedC2SPackets.Reset(); Session.SelectObject(22);
    TestEqual(TEXT("Stack quantity control takes priority over mana"), Sent(ACEGameAction::QueryItemMana, 22), 0);
    TestFalse(TEXT("Stack does not retain previous mana vial"), Session.SelectedObject.bShowMana);
    Add(30, 0); auto& Mob = Session.WorldObjects[30]; Mob.ItemType = ACEItemType::Creature;
    Mob.ObjectDescriptionFlags = ACEObjectDescFlag::Attackable; Mob.bHasPosition = true; Mob.Position = Session.PlayerPosition;
    Session.CachedC2SPackets.Reset(); Session.SelectObject(30);
    TestEqual(TEXT("Attackable creature requests health"), Sent(ACEGameAction::QueryHealth, 30), 1);
    TestFalse(TEXT("Health waits for authoritative reply"), Session.SelectedObject.bShowHealth);
    FACEBinaryWriter Health; Health.WriteUInt32(30); Health.WriteFloat(.65f);
    FACEBinaryReader H(Health.GetData()); Session.HandleUpdateHealth(H);
    TestTrue(TEXT("Health reply enables health vial"), Session.SelectedObject.bShowHealth);
    TestFalse(TEXT("Health and mana vials cannot overlap"), Session.SelectedObject.bShowMana);
    Mob.ObjectDescriptionFlags = 0; Mob.PetOwnerId = 999;
    Session.CachedC2SPackets.Reset(); Session.SelectObject(30);
    TestEqual(TEXT("Nonattackable summoned pet still requests health"), Sent(ACEGameAction::QueryHealth, 30), 1);
    Mob.PetOwnerId = 0;
    Session.CachedC2SPackets.Reset(); Session.SelectObject(30);
    TestEqual(TEXT("Nonattackable ordinary NPC has no health subscription"), Sent(ACEGameAction::QueryHealth, 30), 0);
    Mob.ObjectDescriptionFlags = ACEObjectDescFlag::FreePkStatus;
    Session.CachedC2SPackets.Reset(); Session.SelectObject(30);
    TestEqual(TEXT("Free-PK creature has a health subscription without Attackable"), Sent(ACEGameAction::QueryHealth, 30), 1);
    Session.WorldObjects[23].ObjectDescriptionFlags = ACEObjectDescFlag::Attackable;
    Session.CachedC2SPackets.Reset(); Session.SelectObject(23);
    TestEqual(TEXT("Custom noncreature item with Attackable still queries item mana"), Sent(ACEGameAction::QueryItemMana, 23), 1);
    Session.SelectObject(23); Reply(23, .8f, 1);
    Session.CachedC2SPackets.Reset(); Session.SelectObject(0);
    TestEqual(TEXT("Deselect unsubscribes owned-item mana"), Sent(ACEGameAction::QueryItemMana, 0), 1);
    TestFalse(TEXT("Deselect clears the vial"), Session.SelectedObject.bShowMana);
    Reply(23, .9f, 1);
    TestFalse(TEXT("Delayed reply cannot resurrect selection"), Session.SelectedObject.bValid);
    return !HasAnyErrors();
}
#endif
