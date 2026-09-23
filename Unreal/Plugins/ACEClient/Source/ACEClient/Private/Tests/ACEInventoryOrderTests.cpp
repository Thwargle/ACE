#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailInventoryOrderTest, "ACE.RetailParity.InventoryOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailInventoryOrderTest::RunTest(const FString& Parameters)
{
    FACESession Session;
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Inventory persistence fixture"),false);
    auto Address=Sockets->CreateInternetAddr(); bool Valid=false;
    Address->SetIp(TEXT("127.0.0.1"),Valid); Address->SetPort(0);
    if(!Receiver || !Receiver->Bind(*Address))
    {
        if(Receiver) Sockets->DestroySocket(Receiver);
        AddError(TEXT("Could not create isolated loopback inventory fixture")); return false;
    }
    Receiver->GetAddress(*Address);
    Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Inventory persistence sender"),false);
    Session.ServerC2SAddr=Address; Session.IssacClient=MakeUnique<FACEIsaac>(123u);
    Session.PlayerGuid=100; Session.State=EACESessionState::InWorld;
    auto Item=[](int32 Id,int32 Container,int32 Place,bool Pack=false)
    {
        FACEWorldObject O; O.Guid=Id; O.ContainerId=Container; O.PlacementPosition=Place;
        O.ItemType=Pack ? ACEItemType::Container : ACEItemType::Misc;
        O.ItemsCapacity=Pack ? 24 : 0; return O;
    };
    for (const auto& O : {Item(1,100,0),Item(2,100,1),Item(3,100,2),Item(10,100,0,true),Item(20,100,1,true)})
        Session.UpsertWorldObject(O);
    auto Contains=[&](int32 Id,int32 Container,int32 Place,bool Pack=false)
    {
        FACEBinaryWriter Prop; Prop.WriteUInt8(1); Prop.WriteUInt32(Id); Prop.WriteUInt32(2); Prop.WriteUInt32(Container);
        FACEBinaryReader PR(Prop.GetData()); Session.HandlePublicUpdateInstanceId(PR);
        FACEBinaryWriter W; W.WriteUInt32(Id); W.WriteUInt32(Container); W.WriteInt32(Place); W.WriteInt32(Pack ? 1 : 0);
        FACEBinaryReader R(W.GetData()); Session.HandleInventoryPutObjInContainer(R);
    };
    auto Send=[&](int32 Id,int32 Container,int32 Position)
    {
        Session.CachedC2SPackets.Reset();
        Session.SendPutItemInContainer(Id,Container,Position);
        uint32 Sequence=0; for(const auto& Pair:Session.CachedC2SPackets) Sequence=FMath::Max(Sequence,Pair.Key);
        if(!TestTrue(TEXT("Inventory drag sends reliable server action"),Sequence!=0))return;
        FACEBinaryReader Wire(Session.CachedC2SPackets[Sequence].Payload); Wire.Skip(16);
        TestEqual(TEXT("Inventory move GameAction envelope"),Wire.ReadUInt32(),ACEOpcode::GameAction); Wire.ReadUInt32();
        TestEqual(TEXT("Inventory move action"),Wire.ReadUInt32(),ACEGameAction::PutItemInContainer);
        TestEqual(TEXT("Inventory move item"),Wire.ReadUInt32(),uint32(Id));
        TestEqual(TEXT("Inventory move container"),Wire.ReadUInt32(),uint32(Container));
        TestEqual(TEXT("Inventory move position"),Wire.ReadInt32(),Position);
    };
    auto Check=[&](int32 Container,TArray<int32> Expected,bool Packs=false)
    {
        TArray<FACEWorldObject> Actual;
        if (Packs) Session.GetPlayerPacks(Actual); else Session.GetPackItems(Container,Actual);
        TestEqual(TEXT("Server inventory count"),Actual.Num(),Expected.Num());
        for(int32 I=0;I<FMath::Min(Actual.Num(),Expected.Num());++I)
            TestEqual(FString::Printf(TEXT("Server inventory position %d"),I),Actual[I].Guid,Expected[I]);
    };
    // Two drags outstanding at once; ACK order is authoritative, without duplicate shifts.
    Send(3,100,0); Send(2,100,0);
    Check(100,{1,2,3}); Contains(3,100,0); Contains(2,100,0); Check(100,{2,3,1});
    Send(3,20,0); Contains(3,20,0); Check(100,{2,1}); Check(20,{3});
    Send(20,100,0); Contains(20,100,0,true); Check(100,{20,10},true);
    // Failed put must not drop an owned item or undo another acknowledged move.
    Send(1,20,0);
    FACEBinaryWriter Failed; Failed.WriteUInt32(1); FACEBinaryReader FR(Failed.GetData()); Session.HandleInventoryServerSaveFailed(FR);
    Check(100,{2,1}); Check(20,{3});
    // Replay the ACE login footer from the saved inventory, with objects arriving later.
    Session.ClearWorldState(); Session.PlayerGuid=0;
    FACEBinaryWriter Login;
    Login.WriteUInt32(1); Login.WriteUInt32(0); // quality flags and weenie type
    Login.WriteUInt16(1); Login.WriteUInt16(1); Login.WriteUInt32(322); Login.WriteInt32(3);
    for(int I=0;I<6;++I) Login.WriteUInt32(0); // vectors and player module
    Login.WriteUInt32(4);
    for(const auto& Ref : {TPair<int32,int32>(2,0),{20,1},{1,0},{10,1}})
    { Login.WriteUInt32(Ref.Key); Login.WriteUInt32(Ref.Value); }
    Login.WriteUInt32(2); // equipped profile is independent of ObjectCreate order
    Login.WriteUInt32(30); Login.WriteUInt32(uint32(ACEEquipMask::Held)); Login.WriteUInt32(0);
    Login.WriteUInt32(40); Login.WriteUInt32(uint32(ACEEquipMask::Cloak)); Login.WriteUInt32(9);
    auto Caster=Item(30,0,-1); Caster.SpellDID=1;
    Session.UpsertWorldObject(Caster);
    Session.UpsertWorldObject(Item(1,100,0)); // one create precedes the footer
    FACEBinaryReader LR(Login.GetData()); Session.HandlePlayerDescription(LR);
    TestTrue(TEXT("Description before PlayerCreate retains the saved inventory"),Session.ContainerContents.Contains(0));
    TestEqual(TEXT("Login decodes aetheria unlocks from property 322"),Session.PlayerVitals.AetheriaUnlocked,3);
    FACEBinaryWriter Create; Create.WriteUInt32(100);
    Session.bLoginCompleteSent=true;
    FACEBinaryReader PlayerCreate(Create.GetData()); Session.HandlePlayerCreate(PlayerCreate);
    TestFalse(TEXT("PlayerCreate transfers the temporary inventory profile"),Session.ContainerContents.Contains(0));
    TestEqual(TEXT("Earlier caster create inherits equipped owner"),Session.WorldObjects[30].WielderId,100);
    TestEqual(TEXT("Earlier caster retains its innate spell"),Session.WorldObjects[30].SpellDID,1);
    Session.UpsertWorldObject(Item(40,0,-1));
    TestEqual(TEXT("Later cloak create retains equipped owner"),Session.WorldObjects[40].WielderId,100);
    TestEqual(TEXT("Later cloak create retains its location"),Session.WorldObjects[40].CurrentWieldedLocation,int64(ACEEquipMask::Cloak));
    TestEqual(TEXT("Later cloak create retains layer priority"),Session.WorldObjects[40].ClothingPriority,9u);
    TestEqual(TEXT("Missing earlier creates still reserve their saved position"),Session.WorldObjects[1].PlacementPosition,1);
    for(const auto& O : {Item(10,100,-1,true),Item(1,100,-1),Item(20,100,-1,true),Item(2,100,-1)}) Session.UpsertWorldObject(O);
    Check(100,{2,1}); Check(100,{20,10},true);
    FACEBinaryWriter Contents; Contents.WriteUInt32(20); Contents.WriteUInt32(2);
    Contents.WriteUInt32(9); Contents.WriteUInt32(0); Contents.WriteUInt32(3); Contents.WriteUInt32(0);
    FACEBinaryReader CR(Contents.GetData()); Session.HandleViewContents(CR);
    // Reverse ObjectCreate order used to destroy the saved side-bag order.
    Session.UpsertWorldObject(Item(3,20,-1)); Session.UpsertWorldObject(Item(9,20,-1));
    Check(20,{9,3});
    FACEBinaryWriter RootContents; RootContents.WriteUInt32(100); RootContents.WriteUInt32(4);
    for(const auto& Ref : {TPair<int32,int32>(10,1),{1,0},{20,1},{2,0}})
    { RootContents.WriteUInt32(Ref.Key); RootContents.WriteUInt32(Ref.Value); }
    FACEBinaryReader RR(RootContents.GetData()); Session.HandleViewContents(RR);
    Check(100,{1,2}); Check(100,{10,20},true);

    // ACE sends corpse contents (including bags) before CreateObject; GDLE can
    // describe objects first. Both orders must expose the same root and ownership.
    for (bool ObjectsFirst : {false,true})
    {
        Session.ClearWorldState(); Session.PlayerGuid=100;
        auto CreateLoot=[&]()
        {
            Session.UpsertWorldObject(Item(702,0,-1));
            Session.UpsertWorldObject(Item(701,0,-1,true));
            Session.UpsertWorldObject(Item(703,0,-1));
        };
        auto View=[&](int32 Container,TArray<FACEContainerItemRef> Refs)
        {
            FACEBinaryWriter W; W.WriteUInt32(Container); W.WriteUInt32(Refs.Num());
            for (const auto& Ref:Refs) {W.WriteUInt32(Ref.ItemGuid); W.WriteUInt32(Ref.ContainerType);}
            FACEBinaryReader R(W.GetData()); Session.HandleViewContents(R);
        };
        if (ObjectsFirst) CreateLoot();
        View(700,{{701,1},{703,0}});
        View(701,{{702,0}});
        if (!ObjectsFirst) CreateLoot();
        TestEqual(TEXT("Nested corpse bag retains the root as open container"),Session.OpenExternalContainerGuid,700);
        TestEqual(TEXT("Early/late bag create receives corpse membership"),Session.WorldObjects[701].ContainerId,700);
        TestEqual(TEXT("Early/late item create receives nested bag membership"),Session.WorldObjects[702].ContainerId,701);
        Check(701,{702}); Check(700,{703});
        View(800,{});
        FACEBinaryWriter Close; Close.WriteUInt32(700); FACEBinaryReader C(Close.GetData()); Session.HandleCloseGroundContainer(C);
        TestEqual(TEXT("Old corpse close does not revoke the new corpse"),Session.OpenExternalContainerGuid,800);
        TestFalse(TEXT("Closed corpse contents are retired"),Session.ContainerContents.Contains(700));
        TestFalse(TEXT("Closed nested bag contents are retired"),Session.ContainerContents.Contains(701));
        Close=FACEBinaryWriter(); Close.WriteUInt32(800); FACEBinaryReader C2(Close.GetData()); Session.HandleCloseGroundContainer(C2);
        TestEqual(TEXT("Matching server close ends access immediately"),Session.OpenExternalContainerGuid,0);
        View(700,{{701,1},{703,0}});View(701,{{702,0}});
        Session.WorldObjects.Add(704,Item(704,100,0)); // already-picked-up inventory survives recall
        bool ClosedBeforePortal=false;
        const auto CloseHandle=Session.OnCloseGroundContainer.AddLambda([&](int32 Guid){if(Guid==700)ClosedBeforePortal=true;});
        const auto PortalHandle=Session.OnPlayerTeleportStarted.AddLambda([&]()
        {
            TestTrue(TEXT("Loot closes before portal presentation starts"),ClosedBeforePortal);
            TestEqual(TEXT("Recall revokes container access immediately"),Session.OpenExternalContainerGuid,0);
        });
        FACEBinaryWriter Teleport;Teleport.WriteUInt16(1);Teleport.Align();
        FACEBinaryReader T(Teleport.GetData());Session.HandlePlayerTeleport(T);
        TestFalse(TEXT("Recall clears nested loot lists"),Session.ContainerContents.Contains(700)||Session.ContainerContents.Contains(701));
        TestEqual(TEXT("Recall preserves items already picked up"),Session.WorldObjects[704].ContainerId,100);
        Session.OnCloseGroundContainer.Remove(CloseHandle);Session.OnPlayerTeleportStarted.Remove(PortalHandle);
    }
    // Stance changes delay dequipping on both ACE and GDLE. Only ContainID is the
    // completion event; the earlier public property update must not release Wield.
    for (int32 Conflicts : {1,2})
    {
        Session.ClearWorldState(); Session.PlayerGuid=100; Session.State=EACESessionState::InWorld;
        auto New=Item(900,100,0); New.ItemType=ACEItemType::MeleeWeapon;
        Session.UpsertWorldObject(New);
        auto Old=Item(901,0,-1); Old.WielderId=100; Old.CurrentWieldedLocation=ACEEquipMask::MeleeWeapon;
        Session.UpsertWorldObject(Old);
        if (Conflicts==2)
        {
            auto Shield=Old; Shield.Guid=902; Shield.CurrentWieldedLocation=ACEEquipMask::Shield;
            Session.UpsertWorldObject(Shield);
        }
        Session.CachedC2SPackets.Reset();
        auto LastAction=[&](uint32 Action,int32 Guid)
        {
            uint32 Seq=0; for(const auto& P:Session.CachedC2SPackets)Seq=FMath::Max(Seq,P.Key);
            if(!TestTrue(TEXT("Swap emitted a reliable action"),Seq!=0))return;
            FACEBinaryReader R(Session.CachedC2SPackets[Seq].Payload);R.Skip(24);
            TestEqual(TEXT("Swap action order"),R.ReadUInt32(),Action);
            TestEqual(TEXT("Swap action item"),R.ReadUInt32(),uint32(Guid));
        };
        const int64 Location=Conflicts==2?ACEEquipMask::TwoHanded:ACEEquipMask::MeleeWeapon;
        Session.SendGetAndWieldItem(900,Location);
        TestEqual(TEXT("Swap removes all conflicting hands"),Session.PendingEquipmentRemovals.Num(),Conflicts);
        TestTrue(TEXT("Swap keeps interaction busy while waiting for server"),Session.IsUseBusy());
        for(int32 I=0;I<Conflicts;++I)
        {
            const int32 Removing=Session.PendingEquipmentRemovals[0];
            LastAction(ACEGameAction::PutItemInContainer,Removing);
            const int32 Sent=Session.CachedC2SPackets.Num();
            FACEBinaryWriter Prop;Prop.WriteUInt8(1);Prop.WriteUInt32(Removing);Prop.WriteUInt32(2);Prop.WriteUInt32(100);
            FACEBinaryReader PR(Prop.GetData());Session.HandlePublicUpdateInstanceId(PR);
            TestEqual(TEXT("Early container property does not advance swap"),Session.CachedC2SPackets.Num(),Sent);
            Session.SendGetAndWieldItem(900,Location);
            TestEqual(TEXT("Repeated click does not duplicate pending swap"),Session.CachedC2SPackets.Num(),Sent);
            Contains(Removing,100,0);
        }
        LastAction(ACEGameAction::GetAndWieldItem,900);
        TestTrue(TEXT("All dequips complete before final equip"),Session.PendingEquipmentRemovals.IsEmpty());
        FACEBinaryWriter Wield;Wield.WriteUInt32(900);Wield.WriteInt32(int32(Location));
        FACEBinaryReader WR(Wield.GetData());Session.HandleWieldItem(WR);
        TestEqual(TEXT("Wield acknowledgement completes the swap"),Session.PendingEquipmentGuid,0);
        Session.WorldObjects[900].WielderId=0;Session.WorldObjects[900].CurrentWieldedLocation=0;Session.WorldObjects[900].ContainerId=100;
        Old.CurrentWieldedLocation=ACEEquipMask::MeleeWeapon;Session.UpsertWorldObject(Old);
        Session.SendGetAndWieldItem(900,Location);
        const int32 Removing=Session.PendingEquipmentRemovals[0];
        const int32 Sent=Session.CachedC2SPackets.Num();
        FACEBinaryWriter Failure;Failure.WriteUInt32(Removing);FACEBinaryReader R(Failure.GetData());Session.HandleInventoryServerSaveFailed(R);
        Contains(Removing,100,0);
        TestEqual(TEXT("Failed removal cannot later equip the requested item"),Session.CachedC2SPackets.Num(),Sent);
        TestEqual(TEXT("Failure cancels pending swap"),Session.PendingEquipmentGuid,0);
    }
    for(bool CancelCombat:{false,true})
    {
        Session.ClearWorldState();Session.PlayerGuid=100;Session.State=EACESessionState::InWorld;
        Session.PlayerVitals.CombatMode=ACECombatMode::Magic;
        auto Old=Item(910,0,-1);Old.WielderId=100;Old.CurrentWieldedLocation=ACEEquipMask::Held;
        Session.UpsertWorldObject(Old);Session.UpsertWorldObject(Item(911,100,0));
        Session.SendGetAndWieldItem(911,ACEEquipMask::Held);
        Session.ApplyServerCombatMode(ACECombatMode::Melee);
        TestEqual(TEXT("Wand removal accepts the temporary unarmed melee state"),Session.PlayerVitals.CombatMode,int32(ACECombatMode::Melee));
        Contains(910,100,0);
        if(CancelCombat)Session.SendChangeCombatMode(ACECombatMode::NonCombat);
        FACEBinaryWriter W;W.WriteUInt32(911);W.WriteInt32(int32(ACEEquipMask::Held));
        FACEBinaryReader R(W.GetData());Session.HandleWieldItem(R);
        TestEqual(TEXT("Wand swap restores magic unless player explicitly leaves combat"),Session.PlayerVitals.CombatMode,
            int32(CancelCombat?ACECombatMode::NonCombat:ACECombatMode::Magic));
    }
    Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S=nullptr;
    Receiver->Close(); Sockets->DestroySocket(Receiver);
    return true;
}
#endif
