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
    Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S=nullptr;
    Receiver->Close(); Sockets->DestroySocket(Receiver);
    return true;
}
#endif
