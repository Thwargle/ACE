#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEBinaryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEClosedContainerLifetimeTest, "ACE.RetailParity.ClosedContainerLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEClosedContainerLifetimeTest::RunTest(const FString&)
{
    FACESession Session;
    Session.PlayerGuid=100;Session.State=EACESessionState::InWorld;
    auto Item=[](int32 Guid,int32 Container,bool Pack=false)
    {
        FACEWorldObject O;O.Guid=Guid;O.ContainerId=Container;
        O.Name=TEXT("Hunt loot");O.ItemType=Pack?ACEItemType::Container:ACEItemType::Misc;
        O.ItemsCapacity=Pack?24:0;return O;
    };
    auto View=[&](int32 Guid,const TArray<FACEContainerItemRef>& Refs)
    {
        if(Guid!=701) Session.BeginPendingUse(Guid,0); // Requested ground container; 701 is nested metadata.
        FACEBinaryWriter W;W.WriteUInt32(Guid);W.WriteUInt32(Refs.Num());
        for(const auto& Ref:Refs){W.WriteUInt32(Ref.ItemGuid);W.WriteUInt32(Ref.ContainerType);}
        FACEBinaryReader R(W.GetData());Session.HandleViewContents(R);
    };
    auto Close=[&](int32 Guid)
    {
        FACEBinaryWriter W;W.WriteUInt32(Guid);FACEBinaryReader R(W.GetData());Session.HandleCloseGroundContainer(R);
    };
    auto Expire=[&](double Now){Session.MaintainWorldObjectVisibility(Now,[](int32){return true;});};
    auto PickUp=[&](int32 Guid,bool Pack=false)
    {
        FACEBinaryWriter W;W.WriteUInt32(Guid);W.WriteUInt32(100);W.WriteInt32(0);W.WriteInt32(Pack?1:0);
        FACEBinaryReader R(W.GetData());Session.HandleInventoryPutObjInContainer(R);
    };
    auto FillCorpse=[&]
    {
        View(700,{{701,1},{703,0},{704,0}});View(701,{{702,0}});
        Session.UpsertWorldObject(Item(701,700,true));
        Session.UpsertWorldObject(Item(702,701));Session.UpsertWorldObject(Item(703,700));
    };
    FillCorpse();
    Session.SelectedObject.Guid=703;
    const double ClosedAt=FPlatformTime::Seconds();Close(700);
    TestEqual(TEXT("Closing immediately revokes access"),Session.OpenExternalContainerGuid,0);
    TestTrue(TEXT("Closing immediately removes nested display lists"),Session.ContainerContents.IsEmpty());
    Expire(ClosedAt+24);
    TestEqual(TEXT("Retail grace retains descriptors for delayed receipts"),Session.WorldObjects.Num(),3);
    Session.UpsertWorldObject(Item(704,700)); // late SmartboxQueue create after UIQueue close
    Expire(ClosedAt+26);
    TestEqual(TEXT("Grace retires nested and late-arriving leftover loot"),Session.WorldObjects.Num(),0);
    TestEqual(TEXT("Expired loot is no longer selected"),Session.SelectedObject.Guid,0);

    FillCorpse();Close(700);
    PickUp(701,true);PickUp(703);
    Expire(FPlatformTime::Seconds()+26);
    TestTrue(TEXT("Delayed pickup preserves the whole bag and its contents"),
        Session.WorldObjects.Contains(701)&&Session.WorldObjects.Contains(702)&&Session.WorldObjects.Contains(703));
    Session.ClearWorldState();Session.PlayerGuid=100;

    FillCorpse();Close(700);
    View(700,{{701,1}});View(701,{{702,0}}); // item 703 is absent from the refreshed list
    Expire(FPlatformTime::Seconds()+26);
    TestTrue(TEXT("Reopening cancels cleanup of refreshed bag contents"),
        Session.WorldObjects.Contains(701)&&Session.WorldObjects.Contains(702));
    TestFalse(TEXT("Reopening does not retain items absent from the new list"),Session.WorldObjects.Contains(703));
    Session.SendNoLongerViewingContents(700);
    TestTrue(TEXT("Local close retires lists without waiting for the server"),Session.ContainerContents.IsEmpty());
    Expire(FPlatformTime::Seconds()+26);
    TestTrue(TEXT("Local close schedules the same cleanup as server close"),Session.WorldObjects.IsEmpty());

    FillCorpse();
    View(800,{}); // replacing the ground container must retire the previous one
    Expire(FPlatformTime::Seconds()+26);
    TestTrue(TEXT("Opening a different corpse retires the previous contents"),Session.WorldObjects.IsEmpty());
    Close(800);

    FillCorpse();Close(700);
    auto Dropped=Item(703,0);Dropped.bHasPosition=true;
    Dropped.Position.CellId=0x7D64000D;Dropped.Position.Location=FVector(34,104,12);
    Session.UpsertWorldObject(Dropped);
    Expire(FPlatformTime::Seconds()+26);
    TestTrue(TEXT("A fresh world drop survives the old container deadline"),Session.WorldObjects.Contains(703));
    Session.ClearWorldState();Session.PlayerGuid=100;

    // ApproachVendor carries its own PublicWeenieDesc records, not a later
    // CreateObject. Reopening within the old close's 25-second grace must
    // renew those records, or the shop keeps icons that select as "Unknown".
    Session.State=EACESessionState::InWorld;
    auto Vendor=[&](const TArray<int32>& Stock)
    {
        FACEBinaryWriter W;
        W.WriteUInt32(900);W.WriteUInt32(ACEItemType::Misc);
        W.WriteUInt32(0);W.WriteUInt32(100000);W.WriteUInt32(1);
        W.WriteFloat(.9f);W.WriteFloat(1.f);W.WriteUInt32(0);
        W.WriteUInt32(0);W.WriteString16L(TEXT(""));W.WriteUInt32(Stock.Num());
        for(int32 Guid:Stock)
        {
            W.WriteUInt32(0xFFFFFFFFu);W.WriteUInt32(Guid);
            W.WriteUInt32(8); // PublicWeenieDesc Value present
            W.WriteString16L(TEXT("Vendor component"));W.WriteUInt16(273);W.WriteUInt16(0x11CB);
            W.WriteUInt32(ACEItemType::Misc);W.WriteUInt32(0);W.Align();
            W.WriteUInt32(50);W.Align();
        }
        FACEBinaryReader R(W.GetData());Session.HandleApproachVendor(R);
        TestEqual(TEXT("Vendor snapshot fully decoded"),R.Remaining(),0);
    };
    Vendor({901,902});Session.SendNoLongerViewingContents(900);
    Vendor({901,903}); // one returning listing, one new, one no longer sold
    Expire(FPlatformTime::Seconds()+26);
    FACEWorldObject Stock;
    TestTrue(TEXT("Reopened vendor retains returning item details"),Session.GetWorldObject(901,Stock));
    TestEqual(TEXT("Reopened vendor retains purchase value"),Stock.Value,50);
    Session.SelectObject(901);
    TestEqual(TEXT("Returning stock selects by name without reopening again"),Session.SelectedObject.Name,FString(TEXT("Vendor component")));
    TestTrue(TEXT("New vendor stock survives old cleanup"),Session.WorldObjects.Contains(903));
    TestFalse(TEXT("Stock absent from refreshed vendor still expires"),Session.WorldObjects.Contains(902));
    Session.SendNoLongerViewingContents(900);Expire(FPlatformTime::Seconds()+26);
    TestTrue(TEXT("Closing refreshed vendor still releases all stock descriptors"),Session.WorldObjects.IsEmpty());
    Session.ClearWorldState();Session.PlayerGuid=100;

    auto MeasureInventory=[&](const TCHAR* Phase)
    {
        const double Start=FPlatformTime::Seconds();
        for(int I=0;I<200;++I){TArray<int32> Items;Session.GetPackItemGuids(100,Items);}
        AddInfo(FString::Printf(TEXT("%s: %d object records; inventory query %.3f us"),Phase,
            Session.WorldObjects.Num(),(FPlatformTime::Seconds()-Start)*1.e6/200));
    };
    Session.UpsertWorldObject(Item(101,100));
    MeasureInventory(TEXT("Before hunt"));
    int32 Peak=0;
    // Replay 2,160 corpse visits (three hours at one kill every five seconds).
    // Advance the cleanup clock instead of waiting for real-time expiration.
    // ACE closes the list without sending DeleteObject for leftover loot.
    for(int Visit=0;Visit<2160;++Visit)
    {
        const int32 Corpse=1000+Visit*32;
        Session.UpsertWorldObject(Item(Corpse,0,true));
        TArray<FACEContainerItemRef> Refs;
        for(int N=1;N<=20;++N)Refs.Add({Corpse+N,0});
        View(Corpse,Refs);
        for(const auto& Ref:Refs)Session.UpsertWorldObject(Item(Ref.ItemGuid,Corpse));
        Close(Corpse);
        Session.DeleteWorldObject(Corpse);
        Peak=FMath::Max(Peak,Session.WorldObjects.Num());
        Expire(FPlatformTime::Seconds()+26);
    }
    MeasureInventory(TEXT("After hunt and Stop-equivalent idle cleanup"));
    TestEqual(TEXT("Closed corpse loot cannot accumulate over a long hunt"),Session.WorldObjects.Num(),1);
    TestTrue(TEXT("Live records stay bounded by current loot, not total kills"),Peak<=21);
    TestTrue(TEXT("Carried inventory survives cleanup"),Session.WorldObjects.Contains(101));
    TestTrue(TEXT("All deferred cleanup records are released"),Session.ClosedContainerItemDeadlines.IsEmpty());
    // Exercise the production network tick as well as explicit clock advancement:
    // cleanup must not depend on UCM running, a HUD, or a drawing viewport.
    View(700,{{701,0}});Session.UpsertWorldObject(Item(701,700));Close(700);
    Session.ClosedContainerItemDeadlines[701]=FPlatformTime::Seconds()-1;
    Session.NextClosedContainerExpiryAt=0;Session.Tick(0);
    TestFalse(TEXT("Stopped automation still receives session cleanup"),Session.WorldObjects.Contains(701));
    return !HasAnyErrors();
}
#endif
