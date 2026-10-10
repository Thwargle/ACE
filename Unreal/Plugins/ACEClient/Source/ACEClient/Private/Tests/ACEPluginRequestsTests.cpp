#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Interfaces/IPluginManager.h"
#include "Mods/ACEPluginSight.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEInventoryRules.h"
#include "GameFramework/Pawn.h"
#include "ACESession.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Misc/App.h"
#include "ShaderCompiler.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginRequestsTest,"ACE.Plugins.RouteAndTellRequests",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginRequestsTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
    Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->OnWorldChanged(nullptr,World);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* C=GI->GetSubsystem<UACEClientSubsystem>();auto* H=GI->GetSubsystem<UACEPluginSubsystem>();
    C->Session=MakeShared<FACESession>();auto& Session=*C->Session;
    Session.State=EACESessionState::InWorld;Session.PlayerGuid=0x50000001;
    {
        FACEBinaryWriter Login;Login.WriteUInt32(0x97);Login.WriteUInt32(1);
        auto Header=[&](){Login.WriteUInt16(1);Login.WriteUInt16(1);};
        Header();Login.WriteUInt32(25);Login.WriteInt32(275);
        Header();Login.WriteUInt32(2);Login.WriteUInt64(8000000000000ULL);
        Header();Login.WriteUInt32(9001);Login.WriteUInt32(1);
        Header();Login.WriteUInt32(9002);Login.WriteDouble(0.25);
        Header();Login.WriteUInt32(9003);Login.WriteString16L(TEXT("Custom"));
        for(int I=0;I<8;++I)Login.WriteUInt32(0); // empty vectors, player module and inventory
        FACEBinaryReader LR(Login.GetData());Session.HandlePlayerDescription(LR);
        auto Snapshot=MakeShared<FJsonObject>();H->ExtendSnapshot(Snapshot);
        TestEqual(TEXT("Login quad reaches plugin snapshot without int32 truncation"),Snapshot->GetObjectField(TEXT("char_quads"))->GetNumberField(TEXT("2")),8000000000000.);
        TestEqual(TEXT("Login decimal reaches plugin snapshot"),Snapshot->GetObjectField(TEXT("char_doubles"))->GetNumberField(TEXT("9002")),0.25);
        TestEqual(TEXT("Login custom bool reaches plugin snapshot"),Snapshot->GetObjectField(TEXT("char_bools"))->GetNumberField(TEXT("9001")),1.);
        TestEqual(TEXT("Login custom string reaches plugin snapshot"),Snapshot->GetObjectField(TEXT("char_strings"))->GetStringField(TEXT("9003")),FString(TEXT("Custom")));
        for(bool Public:{false,true})
        {
            FACEBinaryWriter Q;Q.WriteUInt32(Public?ACEOpcode::PublicUpdatePropertyInt64:ACEOpcode::PrivateUpdatePropertyInt64);Q.WriteUInt8(1);
            if(Public)Q.WriteUInt32(Session.PlayerGuid);Q.WriteUInt32(2);Q.WriteUInt64(Public?9000000000001ULL:9000000000000ULL);Session.HandleGameMessage(Q.GetData());
            FACEBinaryWriter D;D.WriteUInt32(Public?ACEOpcode::PublicUpdatePropertyFloat:ACEOpcode::PrivateUpdatePropertyFloat);D.WriteUInt8(1);
            if(Public)D.WriteUInt32(Session.PlayerGuid);D.WriteUInt32(9002);D.WriteDouble(Public?0.75:0.5);Session.HandleGameMessage(D.GetData());
            FACEBinaryWriter B;B.WriteUInt32(Public?ACEOpcode::PublicUpdatePropertyBool:ACEOpcode::PrivateUpdatePropertyBool);B.WriteUInt8(1);
            if(Public)B.WriteUInt32(Session.PlayerGuid);B.WriteUInt32(9001);B.WriteUInt32(Public?1:0);Session.HandleGameMessage(B.GetData());
            FACEBinaryWriter S;S.WriteUInt32(Public?ACEOpcode::PublicUpdatePropertyString:ACEOpcode::PrivateUpdatePropertyString);S.WriteUInt8(1);S.WriteUInt32(9003);
            if(Public)S.WriteUInt32(Session.PlayerGuid);S.Align();S.WriteString16L(Public?TEXT("Public"):TEXT("Private"));Session.HandleGameMessage(S.GetData());
            H->ExtendSnapshot(Snapshot);
            TestEqual(TEXT("Live quad uses the server value"),Snapshot->GetObjectField(TEXT("char_quads"))->GetNumberField(TEXT("2")),Public?9000000000001.:9000000000000.);
            TestEqual(TEXT("Live decimal uses the server value"),Snapshot->GetObjectField(TEXT("char_doubles"))->GetNumberField(TEXT("9002")),Public?0.75:0.5);
            TestEqual(TEXT("Live bool updates before player ObjectCreate"),Snapshot->GetObjectField(TEXT("char_bools"))->GetNumberField(TEXT("9001")),Public?1.:0.);
            TestEqual(TEXT("String packet handles property/GUID order and alignment"),Snapshot->GetObjectField(TEXT("char_strings"))->GetStringField(TEXT("9003")),FString(Public?TEXT("Public"):TEXT("Private")));
        }
        TestTrue(TEXT("Absent exemption still requires components"),Snapshot->GetBoolField(TEXT("components_required")));
        for(bool Required:{false,true})
        {
            FACEBinaryWriter B;B.WriteUInt32(Required?ACEOpcode::PublicUpdatePropertyBool:ACEOpcode::PrivateUpdatePropertyBool);B.WriteUInt8(1);
            if(Required)B.WriteUInt32(Session.PlayerGuid);B.WriteUInt32(68);B.WriteUInt32(Required?1:0);Session.HandleGameMessage(B.GetData());
            H->ExtendSnapshot(Snapshot);TestEqual(TEXT("Server component policy updates live"),Snapshot->GetBoolField(TEXT("components_required")),Required);
        }
        FACEBinaryWriter Other;Other.WriteUInt32(ACEOpcode::PublicUpdatePropertyFloat);Other.WriteUInt8(1);Other.WriteUInt32(Session.PlayerGuid+1);Other.WriteUInt32(9002);Other.WriteDouble(99);Session.HandleGameMessage(Other.GetData());
        TestEqual(TEXT("Another player's update cannot contaminate self qualities"),Session.PlayerVitals.QualityDoubles.FindRef(9002),0.75);
        FACEBinaryWriter Short;Short.WriteUInt32(ACEOpcode::PrivateUpdatePropertyString);Short.WriteUInt8(1);Short.WriteUInt32(9003);Short.Align();Short.WriteUInt16(50);Session.HandleGameMessage(Short.GetData());
        TestEqual(TEXT("Truncated string preserves last valid value"),Session.PlayerVitals.QualityStrings.FindRef(9003),FString(TEXT("Public")));
        FACEBinaryWriter Relog;Relog.WriteUInt32(0);Relog.WriteUInt32(1);for(int I=0;I<8;++I)Relog.WriteUInt32(0);
        FACEBinaryReader RR(Relog.GetData());Session.HandlePlayerDescription(RR);
        TestTrue(TEXT("Relog cannot retain previous character qualities"),Session.PlayerVitals.QualityStrings.IsEmpty()&&Session.PlayerVitals.QualityDoubles.IsEmpty()&&Session.PlayerVitals.QualityBools.IsEmpty()&&Session.PlayerVitals.QualityInt64s.IsEmpty());
    }
    auto P=H->Find(TEXT("ucm"));if(!TestTrue(TEXT("UCM discovered"),P.IsValid()))return false;
    P->Enabled=true;P->Running=true;P->Profile=MakeShared<FJsonObject>();
    P->Profile->SetBoolField(TEXT("buffing"),true);P->Profile->SetBoolField(TEXT("buff_others"),true);
    {
        FACEWorldObject Vendor;Vendor.Guid=500;Vendor.Name=TEXT("Arcanist");Vendor.WeenieClassId=200;
        auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        FSocket* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("UCM vendor test receiver"),false);
        const auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
        if(!TestTrue(TEXT("Isolated vendor test socket binds"),Receiver&&Receiver->Bind(*Address)))return false;
        Receiver->GetAddress(*Address);Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("UCM vendor test sender"),false);
        Session.ServerC2SAddr=Address;Session.IssacClient=MakeUnique<FACEIsaac>(123u);
        ON_SCOPE_EXIT {Session.SocketC2S->Close();Sockets->DestroySocket(Session.SocketC2S);Session.SocketC2S=nullptr;Session.ServerC2SAddr.Reset();Session.IssacClient.Reset();Receiver->Close();Sockets->DestroySocket(Receiver);Session.CachedC2SPackets.Reset();};
        FACEWorldObject Stock;Stock.Guid=501;Stock.Name=TEXT("Scarab");Stock.WeenieClassId=300;Stock.MaxStackSize=100;Stock.VendorQuantityAvailable=-1;
        Session.OpenVendorGuid=Vendor.Guid;Session.WorldObjects.Add(Vendor.Guid,Vendor);Session.VendorMerchandise={Stock};
        auto Snapshot=MakeShared<FJsonObject>();H->ExtendSnapshot(Snapshot);
        TestEqual(TEXT("Vendor snapshot carries stable identity"),Snapshot->GetNumberField(TEXT("vendor_wcid")),200.);
        TestEqual(TEXT("Vendor stock includes retail quantity limit"),Snapshot->GetArrayField(TEXT("vendor_stock"))[0]->AsObject()->GetNumberField(TEXT("limit")),100.);
        auto Buy=MakeShared<FJsonObject>();Buy->SetNumberField(TEXT("vendor"),500);Buy->SetNumberField(TEXT("item"),501);Buy->SetNumberField(TEXT("count"),3);
        Session.CachedC2SPackets.Reset();H->ExecuteInventory(*P,Buy,TEXT("buy"));
        TestEqual(TEXT("Restock sends one retail action"),Session.CachedC2SPackets.Num(),1);
        for(const auto& Packet:Session.CachedC2SPackets)
        {
            FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);
            TestEqual(TEXT("Restock uses retail Buy opcode"),R.ReadUInt32(),ACEGameAction::Buy);
            TestEqual(TEXT("Purchase identifies open vendor"),R.ReadUInt32(),500u);TestEqual(TEXT("Purchase has one stock row"),R.ReadUInt32(),1u);
            TestEqual(TEXT("Purchase transmits missing units"),R.ReadInt32(),3);TestEqual(TEXT("Purchase transmits stock GUID"),R.ReadUInt32(),501u);
        }
        auto HasPurchase=[&](){for(const auto& Packet:Session.CachedC2SPackets){FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);if(R.ReadUInt32()==ACEGameAction::Buy)return true;}return false;};
        {
            const auto SavedObjects=Session.WorldObjects;
            ON_SCOPE_EXIT {Session.WorldObjects=SavedObjects;Session.VendorCurrencyName.Empty();Session.VendorCurrencyWeenie=0;Session.TradeSelfItems.Reset();Session.CachedC2SPackets.Reset();P->Running=true;};
            FACEWorldObject Self;Self.Guid=Session.PlayerGuid;Self.ItemsCapacity=102;Session.WorldObjects.Add(Self.Guid,Self);
            FACEWorldObject Note;Note.Guid=600;Note.WeenieClassId=400;Note.Name=TEXT("Trade Note");Note.ItemType=ACEItemType::PromissoryNote;
            Note.ContainerId=Session.PlayerGuid;Note.StackSize=10;Note.MaxStackSize=100;Note.Value=10000;Session.WorldObjects.Add(Note.Guid,Note);
            FACEWorldObject Coins;Coins.Guid=601;Coins.WeenieClassId=273;Coins.ItemType=ACEItemType::Money;Coins.ContainerId=Self.Guid;Coins.StackSize=100;Session.WorldObjects.Add(Coins.Guid,Coins);
            auto Refresh=[&](){auto Out=MakeShared<FJsonObject>();H->ExtendSnapshot(Out);return Out;};
            auto Money=Refresh();TestEqual(TEXT("Live restock purse counts carried pyreals"),Money->GetNumberField(TEXT("pyreals")),100.);
            TestEqual(TEXT("Trade note redemption uses face value per unit"),Money->GetArrayField(TEXT("vendor_trade_notes"))[0]->AsObject()->GetNumberField(TEXT("unit_value")),1000.);
            auto CountSales=[&](){int32 Count=0;for(const auto& Packet:Session.CachedC2SPackets){FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);if(R.CanRead(4)&&R.ReadUInt32()==ACEGameAction::Sell)++Count;}return Count;};
            auto Redeem=MakeShared<FJsonObject>();Redeem->SetNumberField(TEXT("vendor"),500);Redeem->SetNumberField(TEXT("item"),600);Redeem->SetNumberField(TEXT("count"),4);
            Session.CachedC2SPackets.Reset();H->ExecuteInventory(*P,Redeem,TEXT("split_note"));
            TestEqual(TEXT("Partial redemption sends exactly one split"),Session.CachedC2SPackets.Num(),1);
            for(const auto& Packet:Session.CachedC2SPackets)
            {
                FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);TestEqual(TEXT("Trade note split uses retail opcode"),R.ReadUInt32(),ACEGameAction::StackableSplitToContainer);
                TestEqual(TEXT("Split identifies original stack"),R.ReadUInt32(),600u);TestEqual(TEXT("Split stays in owned pack"),R.ReadUInt32(),uint32(Self.Guid));
                R.ReadInt32();TestEqual(TEXT("Split transmits needed quantity only"),R.ReadInt32(),4);
            }
            Session.CachedC2SPackets.Reset();H->ExecuteInventory(*P,Redeem,TEXT("sell_note"));
            TestEqual(TEXT("Cannot sell an unsplit partial stack"),CountSales(),0);
            Session.CachedC2SPackets.Reset();P->Running=true;Note.Guid=602;Note.StackSize=4;Note.Value=4000;Session.WorldObjects.Add(Note.Guid,Note);Redeem->SetNumberField(TEXT("item"),602);
            H->ExecuteInventory(*P,Redeem,TEXT("sell_note"));TestEqual(TEXT("Confirmed note object can be redeemed"),Session.CachedC2SPackets.Num(),1);
            for(const auto& Packet:Session.CachedC2SPackets)
            {
                FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);TestEqual(TEXT("Redemption uses retail Sell opcode"),R.ReadUInt32(),ACEGameAction::Sell);
                TestEqual(TEXT("Redemption identifies current vendor"),R.ReadUInt32(),500u);TestEqual(TEXT("One note stack is sold"),R.ReadUInt32(),1u);
                TestEqual(TEXT("Exact split quantity is sold"),R.ReadInt32(),4);TestEqual(TEXT("Only the new stack is sold"),R.ReadUInt32(),602u);
            }
            Session.WorldObjects[600].ObjectDescriptionFlags|=ACEObjectDescFlag::Retained;
            Session.WorldObjects[602].ObjectDescriptionFlags|=ACEObjectDescFlag::Retained;
            TestEqual(TEXT("Retained notes excluded immediately without appraisal refresh"),Refresh()->GetArrayField(TEXT("vendor_trade_notes")).Num(),0);
            Session.CachedC2SPackets.Reset();H->ExecuteInventory(*P,Redeem,TEXT("sell_note"));TestEqual(TEXT("Retained note cannot be redeemed even with stale intent"),CountSales(),0);
            Session.WorldObjects[602].ObjectDescriptionFlags=0;Session.VendorCurrencyName=TEXT("Tokens");P->Running=true;
            H->ExecuteInventory(*P,Redeem,TEXT("sell_note"));TestEqual(TEXT("Custom currency vendor cannot redeem for restocking"),CountSales(),0);
            Session.VendorCurrencyName.Empty();Session.VendorCurrencyWeenie=999;P->Running=true;
            TestFalse(TEXT("Unnamed alternate currency is not pyreals"),Refresh()->GetBoolField(TEXT("vendor_uses_pyreals")));
            H->ExecuteInventory(*P,Redeem,TEXT("sell_note"));TestEqual(TEXT("Unnamed alternate currency cannot sell notes"),CountSales(),0);
            Session.VendorCurrencyWeenie=0;Session.TradeSelfItems.Add(602);P->Running=true;
            TestEqual(TEXT("Notes offered in trade excluded from redemption"),Refresh()->GetArrayField(TEXT("vendor_trade_notes")).Num(),0);
            H->ExecuteInventory(*P,Redeem,TEXT("sell_note"));TestEqual(TEXT("Stale trade-offered intent cannot sell notes"),CountSales(),0);
        }
        Session.CachedC2SPackets.Reset();Session.VendorMerchandise[0].VendorQuantityAvailable=0;H->ExecuteInventory(*P,Buy,TEXT("buy"));
        TestTrue(TEXT("Sold-out stock cannot send a purchase"),!HasPurchase());
        P->Running=true;Session.OpenVendorGuid=0;H->ExecuteInventory(*P,Buy,TEXT("buy"));
        TestTrue(TEXT("Closed vendor cannot send a purchase"),!HasPurchase());
        P->Running=true;Session.WorldObjects.Remove(Vendor.Guid);Session.VendorMerchandise.Empty();
        {
            const int32 OldMode=Session.PlayerVitals.CombatMode;
            FACEWorldObject Essence;Essence.Guid=9200;Essence.ContainerId=Session.PlayerGuid;
            Essence.Name=TEXT("Custom Summoning Essence");Essence.ItemType=ACEItemType::Misc;Essence.ItemUseable=8;
            Session.WorldObjects.Add(Essence.Guid,Essence);
            FACEAppraisalInfo Appraisal;Appraisal.bSuccess=true;Appraisal.IntProperties.Add(280,213);H->Appraisals.Add(Essence.Guid,Appraisal);
            auto Use=MakeShared<FJsonObject>();Use->SetNumberField(TEXT("item"),Essence.Guid);
            for(int32 Mode:{1,2,4,8})
            {
                Session.PlayerVitals.CombatMode=Mode;Session.CachedC2SPackets.Reset();
                H->ExecuteInventory(*P,Use,TEXT("use_item"));
                TestEqual(TEXT("Summoning preserves current combat mode"),Session.PlayerVitals.CombatMode,Mode);
                TestEqual(TEXT("Summon sends only Use, with no peace-mode packet"),Session.CachedC2SPackets.Num(),1);
                for(const auto& Packet:Session.CachedC2SPackets)
                {
                    FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);
                    TestEqual(TEXT("Summon uses standard retail Use opcode"),R.ReadUInt32(),ACEGameAction::Use);
                    TestEqual(TEXT("Summon targets selected essence"),R.ReadUInt32(),uint32(Essence.Guid));
                }
            }
            Session.WorldObjects.Remove(Essence.Guid);H->Appraisals.Remove(Essence.Guid);
            Session.PlayerVitals.CombatMode=OldMode;P->WaitAction.Empty();P->NextAction=0;
        }
        {
            const int32 OldMode=Session.PlayerVitals.CombatMode;Session.PlayerVitals.CombatMode=1;
            FACEWorldObject Stone;Stone.Guid=9100;Stone.ContainerId=Session.PlayerGuid;Stone.Name=TEXT("Mana Stone");Stone.ItemType=ACEItemType::ManaStone;Stone.ItemUseable=0x00100008;
            FACEWorldObject Donor;Donor.Guid=9101;Donor.ContainerId=Session.PlayerGuid;Donor.Name=TEXT("Donor");Donor.ItemType=ACEItemType::Jewelry;
            Session.WorldObjects.Add(Stone.Guid,Stone);Session.WorldObjects.Add(Donor.Guid,Donor);
            FACEAppraisalInfo StoneID;StoneID.bSuccess=true;H->Appraisals.Add(Stone.Guid,StoneID);
            FACEAppraisalInfo DonorID;DonorID.bSuccess=true;DonorID.IntProperties.Add(107,5000);DonorID.IntProperties.Add(108,5000);H->Appraisals.Add(Donor.Guid,DonorID);
            auto Use=MakeShared<FJsonObject>();Use->SetNumberField(TEXT("item"),Stone.Guid);Use->SetNumberField(TEXT("target"),Donor.Guid);Use->SetStringField(TEXT("activity"),TEXT("item_mana"));
            Session.CachedC2SPackets.Reset();H->ExecuteInventory(*P,Use,TEXT("apply_item"));
            TestEqual(TEXT("Stone filling sends one normal network action"),Session.CachedC2SPackets.Num(),1);
            for(const auto& Packet:Session.CachedC2SPackets)
            {
                FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);
                TestEqual(TEXT("Stone filling uses retail UseWithTarget opcode"),R.ReadUInt32(),ACEGameAction::UseWithTarget);
                TestEqual(TEXT("Packet carries stone as source"),R.ReadUInt32(),uint32(Stone.Guid));
                TestEqual(TEXT("Packet carries donor as target"),R.ReadUInt32(),uint32(Donor.Guid));
            }
            TestTrue(TEXT("Source stone queued for fresh appraisal"),H->PendingResourceRefresh.Contains(Stone.Guid));
            H->ObserveUseDone(0);TestFalse(TEXT("Old stone contents discarded on completion"),H->Appraisals.Contains(Stone.Guid));
            TestFalse(TEXT("Old donor appraisal discarded on completion"),H->Appraisals.Contains(Donor.Guid));
            H->Appraisals.Add(Stone.Guid,StoneID);H->Appraisals.Add(Donor.Guid,DonorID);
            Session.WorldObjects[Donor.Guid].ObjectDescriptionFlags|=ACEObjectDescFlag::Retained;
            Session.CachedC2SPackets.Reset();H->ExecuteInventory(*P,Use,TEXT("apply_item"));
            TestEqual(TEXT("Stale intent cannot consume an item now retained"),Session.CachedC2SPackets.Num(),0);
            TestTrue(TEXT("Rejected refill leaves UCM running"),P->Running);
            Session.WorldObjects.Remove(Stone.Guid);Session.WorldObjects.Remove(Donor.Guid);H->Appraisals.Remove(Stone.Guid);H->Appraisals.Remove(Donor.Guid);
            P->Profile->RemoveField(TEXT("ucm_activity_failure"));Session.PlayerVitals.CombatMode=OldMode;
        }
    }
    H->RefreshAutomationData();
    {
        TestTrue(TEXT("Native vitals view uses session storage"),&C->GetPlayerVitalsView()==&Session.GetPlayerVitals());
        H->DrawRoute();TestNull(TEXT("No route does not allocate a world actor"),H->RouteActor.Get());
        // A fully appraised inventory previously rescanned at render frequency.
        // Force the deadline only for the comparison; both paths execute the
        // real maintenance code over identical world data.
        for(int32 I=0;I<2000;++I)
        {FACEWorldObject O;O.Guid=0x60000000+I;O.ContainerId=Session.PlayerGuid;O.ItemType=ACEItemType::Money;Session.WorldObjects.Add(O.Guid,O);}
        H->LastAppraisalRequest=0;H->NextAppraisalScan=0;H->RefreshAutomationData();
        const double Deadline=H->NextAppraisalScan;
        double Begin=FPlatformTime::Seconds();for(int32 I=0;I<200;++I)H->RefreshAutomationData();
        const double Cached=(FPlatformTime::Seconds()-Begin)*1e6/200;
        TestEqual(TEXT("Empty maintenance scan keeps its deadline across frames"),H->NextAppraisalScan,Deadline);
        Begin=FPlatformTime::Seconds();for(int32 I=0;I<200;++I){H->NextAppraisalScan=0;H->RefreshAutomationData();}
        const double Repeated=(FPlatformTime::Seconds()-Begin)*1e6/200;
        AddInfo(FString::Printf(TEXT("Appraisal maintenance, 2000 objects: forced scan %.2f us/call, scheduled %.2f us/call (CPU microbenchmark, not FPS)"),Repeated,Cached));
        for(int32 I=0;I<2000;++I)Session.WorldObjects.Remove(0x60000000+I);
        H->NextAppraisalScan=0;
    }
    {
        const auto OriginalProfile=P->Profile;const auto Vitals=Session.PlayerVitals;
        Session.PlayerVitals.bValid=true;Session.PlayerVitals.Health=100;
        P->Profile=MakeShared<FJsonObject>();P->Profile->SetArrayField(TEXT("vt_meta"),{});
        TestTrue(TEXT("Meta host starts"),H->Start(TEXT("ucm")));auto First=P->VM;
        H->Stop(TEXT("ucm"),TEXT("Stopped"),true);
        TestTrue(TEXT("Manual stop retains only resumable meta VM"),!P->Running&&P->CanResumeMeta&&P->VM==First);
        TestTrue(TEXT("Same character/profile resumes"),H->Start(TEXT("ucm")));
        TestTrue(TEXT("Resume uses retained VM and arms one resume callback"),P->VM==First&&P->ResumeMetaPending);
        H->Stop(TEXT("ucm"),TEXT("Paused before Force Buff"),true);
        TestTrue(TEXT("Force Buff can run from a paused meta"),H->RequestForceBuff());
        TestTrue(TEXT("Manual buff keeps the paused meta VM"),P->VM==First&&H->bForceBuffOnly);
        H->CancelForceBuff();
        TestTrue(TEXT("Cancelling manual buff preserves resumable meta"),!P->Running&&P->CanResumeMeta&&P->VM==First);
        H->RequestForceBuff();auto Done=MakeShared<FJsonObject>();Done->SetStringField(TEXT("action"),TEXT("force_buff_done"));
        Done->SetNumberField(TEXT("request"),H->ForceBuffRequest);H->Execute(*P,Done);
        TestTrue(TEXT("Completing manual buff preserves resumable meta"),!P->Running&&P->CanResumeMeta&&P->VM==First);
        H->Start(TEXT("ucm"));
        H->Stop(TEXT("ucm"),TEXT("Stopped"),true);P->Profile->SetNumberField(TEXT("radius"),77);
        H->Start(TEXT("ucm"));TestTrue(TEXT("Changed settings cannot resume stale meta state"),P->VM!=First&&!P->ResumeMetaPending);
        First=P->VM;H->Stop(TEXT("ucm"),TEXT("Stopped"),true);++Session.PlayerGuid;
        H->Start(TEXT("ucm"));TestTrue(TEXT("Another character cannot inherit a paused meta"),P->VM!=First&&!P->ResumeMetaPending);--Session.PlayerGuid;
        H->Stop(TEXT("ucm"),TEXT("Script error fixture"));TestTrue(TEXT("Errors discard the VM rather than repeatedly resuming broken state"),!P->VM&&!P->CanResumeMeta);
        P->Profile=OriginalProfile;P->Running=true;Session.PlayerVitals=Vitals;H->RefreshAutomationData();
    }
    {
        FACEWorldObject Monster;Monster.Guid=900;Monster.Name=TEXT("Wasp");Session.WorldObjects.Add(900,Monster);
        Session.SelectedObject.Guid=900;H->PhysicalAttackTarget=900;H->PhysicalAttackOwner=TEXT("ucm");H->PhysicalAttackUntil=FPlatformTime::Seconds()+30;
        Session.OnChatMessage.Broadcast(TEXT("Your missile attack hit the environment."),TEXT("Visitor"),0);
        Session.OnChatMessage.Broadcast(TEXT("Your missile attack hit the environment."),TEXT(""),ACEChatMessageType::Tell);
        TestEqual(TEXT("Player chat cannot fabricate obstruction failures"),H->CombatOutcomes.Num(),0);
        Session.OnChatMessage.Broadcast(TEXT("Your missile attack hit the environment."),TEXT(""),0);
        TestEqual(TEXT("Server environment miss records selected physical target"),H->CombatOutcomes.Num(),1);
        if(H->CombatOutcomes.Num())TestEqual(TEXT("Failure carries target GUID"),H->CombatOutcomes[0]->AsObject()->GetNumberField(TEXT("target")),900.);
        Session.OnCombatFeedback.Broadcast(TEXT("Wasp"),0,false,false);Session.OnCombatFeedback.Broadcast(TEXT("Wasp"),20,true,false);Session.OnCombatFeedback.Broadcast(TEXT("Other"),20,false,false);
        TestEqual(TEXT("Evades, incoming damage and other targets are not hits"),H->CombatOutcomes.Num(),1);
        Session.OnCombatFeedback.Broadcast(TEXT("Wasp"),20,false,false);TestEqual(TEXT("Confirmed damage resets target failures"),H->CombatOutcomes.Num(),2);
        Session.SelectedObject.Guid=901;Session.OnChatMessage.Broadcast(TEXT("Your missile attack hit the environment."),TEXT(""),0);
        TestEqual(TEXT("Changed selection rejects ambiguous late outcome"),H->CombatOutcomes.Num(),2);
        Session.SelectedObject.Guid=900;H->PhysicalAttackUntil=0;Session.OnChatMessage.Broadcast(TEXT("Your missile attack hit the environment."),TEXT(""),0);
        TestEqual(TEXT("Expired physical attack ignores late feedback"),H->CombatOutcomes.Num(),2);
        H->PhysicalAttackTarget=0;H->PhysicalAttackOwner.Empty();H->CombatOutcomes.Empty();Session.SelectedObject.Guid=0;Session.WorldObjects.Remove(900);
    }
    {
        FACEWorldObject Monster;Monster.Guid=900;Monster.Name=TEXT("Wasp");Session.WorldObjects.Add(900,Monster);
        FACEWorldObject Player;Player.Guid=Session.PlayerGuid;Player.Name=TEXT("Caster");Session.WorldObjects.Add(Player.Guid,Player);
        Session.SelectedObject.Guid=900;H->PendingSpell=123;
        auto Cast=[&](bool Started=true)
        {
            UACEPluginSubsystem::FOffensiveCast E;E.Spell=123;E.Target=900;E.Owner=TEXT("ucm");E.SpellName=TEXT("Flame Bolt VII");E.TargetName=TEXT("Wasp");
            E.Sent=FPlatformTime::Seconds();E.Started=Started?E.Sent:0;H->OffensiveCasts.Add(E);
            H->PendingSpell=E.Spell;H->PendingSpellTarget=E.Target;return E.Sent;
        };
        const double Unstarted=Cast(false);H->ExpireOffensiveCasts(Unstarted+6);
        TestTrue(TEXT("No spell words means no target failure on turn/equip timeout"),H->OffensiveCasts.IsEmpty()&&H->CombatOutcomes.IsEmpty());
        Cast(false);Session.OnChatMessage.Broadcast(TEXT("words"),TEXT("Other"),ACEChatMessageType::Spellcasting);
        TestEqual(TEXT("Another caster cannot arm result timeout"),H->OffensiveCasts[0].Started,0.);
        Session.OnChatMessage.Broadcast(TEXT("words"),TEXT("Caster"),ACEChatMessageType::Spellcasting);
        TestTrue(TEXT("Own spell words arm result deadline"),H->OffensiveCasts[0].Started>0);
        H->ExpireOffensiveCasts(H->OffensiveCasts[0].Started+3.629);
        TestEqual(TEXT("Started spell timeout counts once"),H->CombatOutcomes.Num(),1);
        H->ExpireOffensiveCasts(FPlatformTime::Seconds()+50);TestEqual(TEXT("Expired result is not counted again"),H->CombatOutcomes.Num(),1);
        Cast();Session.OnChatMessage.Broadcast(TEXT("Wasp resists your spell"),TEXT(""),ACEChatMessageType::Magic);
        Cast();Session.OnChatMessage.Broadcast(TEXT("Your spell fizzled."),TEXT(""),ACEChatMessageType::Magic);
        TestTrue(TEXT("Resists and fizzles discard pending result without blacklisting"),H->OffensiveCasts.IsEmpty()&&H->CombatOutcomes.Num()==1);
        Cast();H->ObserveUseDone(1);TestTrue(TEXT("UseDone error suppresses a later false timeout"),H->OffensiveCasts.IsEmpty());
        Cast();Session.OnChatMessage.Broadcast(TEXT("Target is out of range"),TEXT("Visitor"),ACEChatMessageType::Magic);
        TestEqual(TEXT("Player text cannot blacklist target"),H->CombatOutcomes.Num(),1);
        Session.OnChatMessage.Broadcast(TEXT("Target is out of range"),TEXT(""),ACEChatMessageType::Magic);
        TestTrue(TEXT("Permanent server failure marks target immediately unhittable"),H->CombatOutcomes.Last()->AsObject()->GetBoolField(TEXT("unhittable")));
        Cast();Session.OnChatMessage.Broadcast(TEXT("Critical hit! You burn Wasp for 100 points with Flame Bolt VII."),TEXT(""),ACEChatMessageType::Magic);
        TestTrue(TEXT("Magic damage success resets miss count after UseDone"),H->OffensiveCasts.IsEmpty()&&H->CombatOutcomes.Last()->AsObject()->GetBoolField(TEXT("hit")));
        Cast();Session.WorldObjects.Remove(900);H->ExpireOffensiveCasts(FPlatformTime::Seconds()+10);
        TestTrue(TEXT("Released target cannot acquire a timeout for a reused GUID"),H->OffensiveCasts.IsEmpty());
        H->CombatOutcomes.Empty();H->PendingSpell=H->PendingSpellTarget=0;Session.SelectedObject.Guid=0;Session.WorldObjects.Remove(Player.Guid);
    }
    for(uint32 CastError:{0u,1u})
    {
        const double Sent=FPlatformTime::Seconds()-1.;
        H->PendingSpell=123;H->PendingSpellTarget=0;H->PendingSpellAt=Sent;H->PendingSpellOwner=P->Id;
        P->NextAction=Sent+3.5;H->ObserveUseDone(CastError);
        TestTrue(TEXT("Completed cast releases immediately; rejected cast retains minimum request interval"),P->NextAction<=(CastError?Sent+.5:FPlatformTime::Seconds()));
        TestTrue(TEXT("Cast completion clears owner and pending spell"),H->PendingSpellOwner.IsEmpty()&&H->PendingSpell==0);
        P->NextAction=Sent+8;H->ObserveUseDone(0);
        TestEqual(TEXT("Unrelated use completion cannot clear another action throttle"),P->NextAction,Sent+8);
        P->NextAction=0;
    }
    {
        const auto SavedPlugin=*P;
        const auto SavedVitals=Session.PlayerVitals;
        const bool SavedUseBusy=Session.bUseBusy;
        const int32 SavedEquipment=Session.PendingEquipmentGuid;
        const double SavedMaintenance=H->NextMaintenanceAt;
        ON_SCOPE_EXIT
        {
            FTSTicker::GetCoreTicker().RemoveTicker(H->DecisionWakeTicker);
            H->DecisionWakeTicker.Reset();H->DecisionWakeAt=0;
            H->NextMaintenanceAt=SavedMaintenance;*P=SavedPlugin;Session.PlayerVitals=SavedVitals;
            Session.bUseBusy=SavedUseBusy;Session.PendingEquipmentGuid=SavedEquipment;
            H->PendingSpell=0;H->PendingSpellOwner.Empty();
        };
        FTSTicker::GetCoreTicker().RemoveTicker(H->DecisionWakeTicker);H->DecisionWakeTicker.Reset();
        P->VM=MakeShared<FACEPluginVM>();FString Error;
        TestTrue(TEXT("Load cast scheduling probe"),P->VM->Load(TEXT("local n=0; return function(s,p) n=n+1; return {status='Decision '..n} end"),Error));
        P->Profile=MakeShared<FJsonObject>();P->Running=true;P->Player=Session.PlayerGuid;P->Server=C->GetServerName();
        Session.PlayerVitals.bValid=true;Session.PlayerVitals.Health=100;
        Session.bUseBusy=false;Session.PendingEquipmentGuid=0;
        P->WaitAction.Empty();P->NextDecision=0;P->NextAction=0;P->Status=TEXT("Waiting");
        H->NextMaintenanceAt=FPlatformTime::Seconds()+60;
        const double Maintenance=H->NextMaintenanceAt;
        auto CompleteCast=[&](uint32 Result)
        {
            FACEBinaryWriter Packet;Packet.WriteUInt32(Result);FACEBinaryReader Reader(Packet.GetData());
            Session.HandleUseDone(Reader);
        };
        auto DeliverWake=[&]()
        {
            // Invoke the registered callback without recursively ticking the global ticker.
            FTSTicker::GetCoreTicker().RemoveTicker(H->DecisionWakeTicker);
            H->RunDecisionWake(.016f);
        };
        for(int32 Cast=1;Cast<=20;++Cast)
        {
            H->PendingSpell=123;H->PendingSpellAt=FPlatformTime::Seconds();H->PendingSpellOwner=P->Id;
            H->PendingSpellConfirmed=false;H->PendingSpellFizzled=false;
            P->NextAction=H->PendingSpellAt+3.5;P->NextDecision=0;
            const FString Before=P->Status;H->Tick(.016f);
            TestEqual(TEXT("Pending spell prevents another decision"),P->Status,Before);
            CompleteCast(0);
            TestTrue(TEXT("Server completion schedules immediate deferred decision"),H->DecisionWakeTicker.IsValid()&&H->DecisionWakeAt<=FPlatformTime::Seconds());
            TestEqual(TEXT("Network callback does not execute policy reentrantly"),P->Status,Before);
            TestFalse(TEXT("Projectile still in flight is not falsely confirmed"),H->LastSpellConfirmed);
            const double WakeAt=H->DecisionWakeAt;H->ScheduleDecisionWake(WakeAt+1);
            TestEqual(TEXT("Later wake cannot delay an already ready cast"),H->DecisionWakeAt,WakeAt);
            DeliverWake();
            TestEqual(TEXT("Each acknowledged cast gets the next decision"),P->Status,FString::Printf(TEXT("Decision %d"),Cast));
            H->Tick(.016f);
            TestEqual(TEXT("Periodic poll cannot duplicate the same decision"),P->Status,FString::Printf(TEXT("Decision %d"),Cast));
            TestEqual(TEXT("Rapid cast completions do not repeat background scans"),H->NextMaintenanceAt,Maintenance);
        }
        H->PendingSpell=123;H->PendingSpellOwner=P->Id;H->PendingSpellAt=FPlatformTime::Seconds();
        P->NextAction=H->PendingSpellAt+3.5;CompleteCast(1);
        TestTrue(TEXT("Immediate rejection retains half-second retry floor"),H->DecisionWakeAt>=H->PendingSpellAt+.5);
        DeliverWake();TestEqual(TEXT("Early retry cannot execute another decision"),P->Status,FString(TEXT("Decision 20")));
        H->PendingSpell=123;H->PendingSpellOwner=P->Id;CompleteCast(0);
        P->Running=false;DeliverWake();
        TestEqual(TEXT("Stopping before a queued wake prevents another decision"),P->Status,FString(TEXT("Decision 20")));
    }
    auto Tell=[&](const FString& Text,int32 Sender=0x50000002,int32 Type=ACEChatMessageType::Tell)
    {
        FACEBinaryWriter W;W.WriteString16L(Text);W.WriteString16L(TEXT("Visitor"));W.WriteUInt32(Sender);
        W.WriteUInt32(Session.PlayerGuid);W.WriteUInt32(Type);W.WriteUInt32(0);
        FACEBinaryReader R(W.GetData());Session.HandleTell(R);
    };
    {
        auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        FSocket* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("UCM queue receiver"),false);
        auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
        if(!TestTrue(TEXT("Queue test binds only to loopback"),Receiver&&Receiver->Bind(*Address)))return false;
        Receiver->GetAddress(*Address);Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("UCM queue sender"),false);
        Session.ServerC2SAddr=Address;Session.IssacClient=MakeUnique<FACEIsaac>(123u);
        ON_SCOPE_EXIT{H->ClearBuffRequests();Session.SocketC2S->Close();Sockets->DestroySocket(Session.SocketC2S);Session.SocketC2S=nullptr;Session.ServerC2SAddr.Reset();Session.IssacClient.Reset();Receiver->Close();Sockets->DestroySocket(Receiver);Session.CachedC2SPackets.Reset();};
        auto HasTell=[&](const FString& Name,const FString& Part){for(const auto& Packet:Session.CachedC2SPackets)
        {FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);if(R.ReadUInt32()!=ACEGameAction::Tell)continue;const auto Message=R.ReadString16L(),Recipient=R.ReadString16L();if(Recipient==Name&&Message.Contains(Part))return true;}return false;};
        H->ObservePlayerTell(TEXT("heavy"),TEXT("Alice"),0x50000020);
        H->ObservePlayerTell(TEXT("mage"),TEXT("Bob"),0x50000021);
        TestTrue(TEXT("Second requester receives direct queue position"),HasTell(TEXT("Bob"),TEXT("#2 (1 ahead")));
        TestTrue(TEXT("Host can see queued names and order"),H->BuffQueueSummary().Contains(TEXT("2. Bob")));
        auto Start=MakeShared<FJsonObject>();Start->SetStringField(TEXT("action"),TEXT("buff_request_start"));Start->SetNumberField(TEXT("request"),H->BuffRequests[0]->AsObject()->GetNumberField(TEXT("id")));
        Session.CachedC2SPackets.Reset();H->Execute(*P,Start);TestTrue(TEXT("First requester gets turn notification"),HasTell(TEXT("Alice"),TEXT("your turn")));
        Session.CachedC2SPackets.Reset();H->Execute(*P,Start);TestTrue(TEXT("Repeated start cannot spam turn notifications"),Session.CachedC2SPackets.IsEmpty());
        auto Done=MakeShared<FJsonObject>(*Start);Done->SetStringField(TEXT("action"),TEXT("buff_request_done"));Done->SetStringField(TEXT("status"),TEXT("Buffs complete"));H->Execute(*P,Done);
        TestTrue(TEXT("Finished player gets a completion tell"),HasTell(TEXT("Alice"),TEXT("Buffs complete")));
        TestTrue(TEXT("Remaining player gets updated position"),HasTell(TEXT("Bob"),TEXT("#1")));
        Start->SetNumberField(TEXT("request"),H->BuffRequests[0]->AsObject()->GetNumberField(TEXT("id")));Session.CachedC2SPackets.Reset();H->Execute(*P,Start);
        TestTrue(TEXT("Next player is notified when their buff task starts"),HasTell(TEXT("Bob"),TEXT("your turn")));
        H->ClearBuffRequests();TestTrue(TEXT("Clear sends cancellation to the remaining requester"),HasTell(TEXT("Bob"),TEXT("cancelled")));
        FACEWorldObject Blade;Blade.Guid=9300;Blade.Name=TEXT("Tethered blade");Blade.ContainerId=Session.PlayerGuid;
        Blade.ItemType=ACEItemType::MeleeWeapon;Blade.ValidLocations=ACEEquipMask::MeleeWeapon;Blade.CombatUse=1;
        Session.WorldObjects.Add(Blade.Guid,Blade);FACEAppraisalInfo BladeInfo;BladeInfo.bSuccess=true;H->Appraisals.Add(Blade.Guid,BladeInfo);
        auto Equip=MakeShared<FJsonObject>();Equip->SetNumberField(TEXT("item"),Blade.Guid);
        for(bool Left:{true,false})
        {
            const uint64 Revision=Session.GetInventoryDataRevision();
            FACEBinaryWriter Update;Update.WriteUInt32(ACEOpcode::PublicUpdatePropertyBool);Update.WriteUInt8(Left?1:2);Update.WriteUInt32(Blade.Guid);Update.WriteUInt32(130);Update.WriteUInt32(Left?1:0);
            Session.HandleGameMessage(Update.GetData());
            TestTrue(TEXT("Tether property updates invalidate cached inventory"),Session.GetInventoryDataRevision()>Revision);
            TestTrue(TEXT("Ordinary inventory use honors live automatic hand flag"),ACEInventoryRules::DetermineOwnedUse(Session.WorldObjects[Blade.Guid],Session.PlayerGuid)==(Left?EACEOwnedItemUse::WieldLeft:EACEOwnedItemUse::WieldRight));
            auto Snapshot=MakeShared<FJsonObject>();H->ExtendSnapshot(Snapshot);
            bool Found=false;for(const auto& V:Snapshot->GetArrayField(TEXT("inventory")))if(V->AsObject()->GetNumberField(TEXT("id"))==Blade.Guid)
            {Found=true;TestEqual(TEXT("UCM sees tether changes without relog or appraisal"),V->AsObject()->GetBoolField(TEXT("auto_wield_left")),Left);}
            TestTrue(TEXT("Tethered weapon is in inventory snapshot"),Found);
            Session.PlayerVitals.CombatMode=1;Session.CachedC2SPackets.Reset();H->ExecuteInventory(*P,Equip,TEXT("equip"));
            bool Sent=false;for(const auto& Packet:Session.CachedC2SPackets)
            {FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);if(R.ReadUInt32()!=ACEGameAction::GetAndWieldItem)continue;Sent=true;TestEqual(TEXT("Equip packet identifies blade"),R.ReadUInt32(),uint32(Blade.Guid));TestEqual(TEXT("Equip packet uses retail right/left location"),R.ReadUInt32(),uint32(Left?ACEEquipMask::Shield:ACEEquipMask::MeleeWeapon));}
            TestTrue(TEXT("UCM sends standard equipment request"),Sent);Session.CancelEquipmentSwap();
        }
        FACEWorldObject Ust;Ust.Guid=9301;Ust.ContainerId=Session.PlayerGuid;Ust.ItemType=0x20000000;Session.WorldObjects.Add(Ust.Guid,Ust);
        auto Salvage=MakeShared<FJsonObject>();Salvage->SetNumberField(TEXT("item"),Blade.Guid);Salvage->SetNumberField(TEXT("tool"),Ust.Guid);
        for(int Protection=0;Protection<4;++Protection)
        {
            auto& Live=Session.WorldObjects[Blade.Guid];Live=Blade;Live.MaterialType=59;Live.Structure=200;
            H->Appraisals[Blade.Guid]=BladeInfo;
            if(Protection==1)H->Appraisals[Blade.Guid].IntProperties.Add(171,1);
            if(Protection==2)H->Appraisals[Blade.Guid].StringProperties.Add(8,TEXT("Owner"));
            if(Protection==3)Live.ObjectDescriptionFlags|=ACEObjectDescFlag::Retained;
            Session.CachedC2SPackets.Reset();H->ExecuteInventory(*P,Salvage,TEXT("salvage"));
            bool Sent=false;for(const auto& Packet:Session.CachedC2SPackets)
            {FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);if(R.ReadUInt32()==ACEGameAction::CreateTinkeringTool)Sent=true;}
            TestEqual(TEXT("Host validates protected salvage and permits ordinary items with over 100 uses"),Sent,Protection==0);
        }
        Session.WorldObjects.Remove(Ust.Guid);P->ActivityFailure.Reset();
        Session.WorldObjects.Remove(Blade.Guid);H->Appraisals.Remove(Blade.Guid);P->WaitAction.Empty();P->NextAction=0;
        H->LastBuffReply.Empty();
    }
    Session.OnChatMessage.Broadcast(TEXT("mage"),TEXT("Visitor"),ACEChatMessageType::Tell);
    TestEqual(TEXT("Ordinary chat display cannot fabricate a buff request"),H->QueuedBuffRequests(),0);
    Tell(TEXT("mage"),0x70000001);Tell(TEXT("mage"),Session.PlayerGuid);Tell(TEXT("please mage"));
    TestEqual(TEXT("NPC, own tells and non-command text ignored"),H->QueuedBuffRequests(),0);
    Tell(TEXT("  MAGE  "));TestEqual(TEXT("Authenticated player tell queues exact case-insensitive keyword"),H->QueuedBuffRequests(),1);
    Tell(TEXT("mage"));TestEqual(TEXT("Duplicate request does not reset a buff cycle"),H->QueuedBuffRequests(),1);
    auto Done=MakeShared<FJsonObject>();Done->SetStringField(TEXT("action"),TEXT("buff_request_done"));
    Done->SetNumberField(TEXT("request"),H->BuffRequests[0]->AsObject()->GetNumberField(TEXT("id")));Done->SetStringField(TEXT("status"),TEXT("Complete"));
    H->Execute(*P,Done);Tell(TEXT("mage"));TestEqual(TEXT("Recent completed request is rate limited"),H->QueuedBuffRequests(),0);
    H->ClearBuffRequests();P->Profile->SetBoolField(TEXT("buffing"),false);Tell(TEXT("mage"));
    TestEqual(TEXT("Buff off does not accept requests"),H->QueuedBuffRequests(),0);
    P->Profile->SetBoolField(TEXT("buffing"),true);
    auto Command=MakeShared<FJsonObject>();Command->SetStringField(TEXT("keyword"),TEXT("warrior"));Command->SetStringField(TEXT("role"),TEXT("heavy"));
    P->Profile->SetArrayField(TEXT("buff_commands"),{MakeShared<FJsonValueObject>(Command)});
    Tell(TEXT("heavy"));TestEqual(TEXT("Removed default keyword no longer responds"),H->QueuedBuffRequests(),0);
    Tell(TEXT("warrior"));TestEqual(TEXT("Custom keyword maps to role"),H->BuffRequests[0]->AsObject()->GetStringField(TEXT("role")),FString(TEXT("heavy")));
    H->ClearBuffRequests();for(int32 I=0;I<10;++I)Tell(TEXT("warrior"),0x50000010+I);
    TestEqual(TEXT("Queue is bounded"),H->QueuedBuffRequests(),8);
    H->Stop(TEXT("ucm"));TestEqual(TEXT("Stopping clears request queue"),H->QueuedBuffRequests(),0);

    // A migrated profile omits route_preview. The UI defaults it to on, so the
    // real renderer must also draw, with ground projection and active markers.
    Session.PlayerPosition.CellId=0x01010001;Session.PlayerPosition.Location=FVector(0,0,0);
    const FVector Origin=Session.PlayerPosition.ToUnrealLocation();
    auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);Box->SetBoxExtent(FVector(2000,2000,50));
    Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Floor->SetActorLocation(Origin-FVector(0,0,50));
    {
        auto* Wall=World->SpawnActor<AActor>();auto* Obstacle=NewObject<UBoxComponent>(Wall);
        Wall->SetRootComponent(Obstacle);Obstacle->SetBoxExtent(FVector(10,300,300));
        Obstacle->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Obstacle->SetCollisionResponseToAllChannels(ECR_Block);
        Obstacle->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);Obstacle->RegisterComponent();Wall->SetActorLocation(Origin+FVector(-500,0,200));
        FACEPluginSightQuery Sight(*World,nullptr);
        for(double Height:{25.,120.,250.})
            TestFalse(TEXT("Wall / closed door blocks low, middle and high monster sight despite ignoring Camera"),Sight.Clear(Origin+FVector(0,0,120),Origin+FVector(-1000,0,Height)));
        Obstacle->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
        TestTrue(TEXT("Open door immediately restores sight"),Sight.Clear(Origin+FVector(0,0,120),Origin+FVector(-1000,0,120)));
        Wall->Destroy();
        auto* Room=World->SpawnActor<AACEEnvCellActor>();
        Room->SetActorLocation(Origin+FVector(-500,0,0));
        Room->CellMesh->CreateMeshSection(0,{FVector(0,-300,0),FVector(0,300,0),FVector(0,0,400)},
            {0,1,2},TArray<FVector>(),TArray<FVector2D>(),TArray<FColor>(),TArray<FProcMeshTangent>(),false);
        Room->SetActorHiddenInGame(true);Room->CellMesh->SetVisibility(false);
        Room->CellMesh->SetMeshSectionVisible(0,false);
        FACEPluginSightQuery HiddenSight(*World,nullptr);
        TestFalse(TEXT("Culled room wall without cooked collision still blocks combat"),HiddenSight.Clear(Origin+FVector(0,0,120),Origin+FVector(-1000,0,120)));
        Room->Destroy();
    }
    auto Point=[&](double X,const FString& Kind=TEXT("walk"))
    {auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("cell"),0x01010001);O->SetNumberField(TEXT("x"),X);O->SetNumberField(TEXT("y"),0);O->SetNumberField(TEXT("z"),.2);O->SetStringField(TEXT("kind"),Kind);return MakeShared<FJsonValueObject>(O);};
    P->Profile->SetArrayField(TEXT("route"),{Point(0),Point(10)});H->RoutePoint=2;H->NextRouteCheck=0;H->DrawRoute();
    if(!TestNotNull(TEXT("Migrated profile creates route geometry"),H->RouteMesh.Get()))return false;
    int32 Path=0,Ring=0;bool Grounded=true;
    for(const auto& L:H->RouteSegments)
    {
        if(L.Thickness==10){++Path;Grounded&=FMath::IsNearlyEqual(L.Start.Z,10.,.1)&&FMath::IsNearlyEqual(L.End.Z,10.,.1);}
        if(L.Thickness==8)++Ring;
    }
    TestTrue(TEXT("Ground path connects waypoints in multiple segments"),Path>1);
    TestTrue(TEXT("Path is projected onto floor, not floating at the recorded height"),Grounded);
    TestEqual(TEXT("Current waypoint has a full circle"),Ring,32);
    TestTrue(TEXT("Other waypoint uses sphere geometry"),H->RouteSegments.Num()>Path+Ring);
    TestTrue(TEXT("Route culling bounds contain geometry far from world origin"),H->RouteMesh->Bounds.GetBox().IsInsideOrOn(Origin+FVector(-500,0,10)));
    if(FApp::CanEverRender())
    {
        auto* View=World->SpawnActor<AActor>();auto* Capture=NewObject<USceneCaptureComponent2D>(View);
        View->SetRootComponent(Capture);Capture->RegisterComponent();
        auto* Target=NewObject<UTextureRenderTarget2D>();Target->InitAutoFormat(256,256);Target->UpdateResourceImmediate(true);
        Capture->TextureTarget=Target;Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
        Capture->FOVAngle=65;
        Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
        Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetBloom(false);
        Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetAntiAliasing(false);
        Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;Capture->ShowOnlyComponent(H->RouteMesh);
        View->SetActorLocationAndRotation(Origin+FVector(-500,0,1500),FRotator(-90,0,0));
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();
        TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        int32 Colored=0;for(const auto& Pixel:Pixels)Colored+=(Pixel.G>Pixel.R+30&&Pixel.G>80);
        AddInfo(FString::Printf(TEXT("Route capture: %d colored pixels, %d segments, visible=%d render-state=%d"),Colored,H->RouteSegments.Num(),H->RouteMesh->IsVisible(),H->RouteMesh->IsRenderStateCreated()));
        TestTrue(TEXT("Route lines and target circle actually render, not merely exist in a CPU list"),Colored>100);
        TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(256,256,Pixels,Png);
        FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("Automation/UCM-Route-Overlay.png")));
        Target->ReleaseResource();View->Destroy();
    }
    const int32 PreviousLines=H->RouteSegments.Num();H->NextRouteCheck=0;H->DrawRoute();
    TestEqual(TEXT("Unchanged preview reuses geometry"),H->RouteSegments.Num(),PreviousLines);
    const auto PreviousCell=Session.PlayerPosition.CellId;const double BuiltAt=H->RouteRebuiltAt;
    Session.PlayerPosition.CellId=(PreviousCell&int32(0xffff0000))|((uint32(PreviousCell)&0xffff)<0x100?2:0x102);
    H->NextRouteCheck=0;H->DrawRoute();
    TestEqual(TEXT("Cell index alone does not retrace or rebuild the route"),H->RouteRebuiltAt,BuiltAt);
    Session.PlayerPosition.CellId=PreviousCell;
    P->Profile->SetBoolField(TEXT("loop_route"),true);H->NextRouteCheck=0;H->DrawRoute();
    int32 LoopPath=0;for(const auto& L:H->RouteSegments)LoopPath+=L.Thickness==10;
    TestEqual(TEXT("Circular route draws the return leg to its first point"),LoopPath,Path*2);
    P->Profile->SetBoolField(TEXT("reverse_route"),true);H->NextRouteCheck=0;H->DrawRoute();
    int32 LinearPath=0;for(const auto& L:H->RouteSegments)LinearPath+=L.Thickness==10;
    TestEqual(TEXT("Reversing route reuses its path without a false closing leg"),LinearPath,Path);
    P->Profile->SetBoolField(TEXT("loop_route"),false);P->Profile->SetBoolField(TEXT("reverse_route"),false);
    // NAV coordinates can normalize into the neighboring LB even though the
    // player remains inside a dungeon with negative local coordinates.
    const auto OutdoorPosition=Session.PlayerPosition;
    Session.PlayerPosition.CellId=0x01010100;Session.PlayerPosition.Location=FVector(0,-10,0);
    auto DungeonPoint=Point(0)->AsObject();DungeonPoint->SetNumberField(TEXT("cell"),0x01000008);
    DungeonPoint->SetNumberField(TEXT("y"),182);DungeonPoint->SetBoolField(TEXT("legacy"),true);
    P->Profile->SetArrayField(TEXT("route"),{MakeShared<FJsonValueObject>(DungeonPoint)});H->RoutePoint=1;H->NextRouteCheck=0;H->DrawRoute();
    TestEqual(TEXT("Imported dungeon waypoint beyond LB boundary still draws its target circle"),H->RouteSegments.Num(),32);
    TestTrue(TEXT("Replacement route refreshes bounds to the new location"),H->RouteMesh->Bounds.GetBox().IsInsideOrOn(Session.PlayerPosition.ToUnrealLocation()+FVector(0,0,10)));
    Session.PlayerPosition=OutdoorPosition;
    P->Profile->SetArrayField(TEXT("route"),{Point(0,TEXT("portal")),Point(10)});H->NextRouteCheck=0;H->DrawRoute();
    Path=0;for(const auto& L:H->RouteSegments)Path+=L.Thickness==10;
    TestEqual(TEXT("Editing portal point immediately removes false ground connection"),Path,0);
    P->Profile->SetBoolField(TEXT("route_preview"),false);H->NextRouteCheck=0;H->DrawRoute();
    TestEqual(TEXT("Preview off clears markers and path"),H->RouteSegments.Num(),0);
    {
        auto* LootPC=World->SpawnActor<AACEPlayerController>();
        auto* Local=NewObject<ULocalPlayer>(GEngine);GI->AddLocalPlayer(Local,FPlatformUserId::CreateFromInternalId(0));LootPC->SetPlayer(Local);
        const auto SavedVitals=Session.PlayerVitals;const int32 SavedContainer=Session.OpenExternalContainerGuid;
        ON_SCOPE_EXIT {Session.PlayerVitals=SavedVitals;Session.OpenExternalContainerGuid=SavedContainer;H->UseApproachOwner.Empty();Session.WorldObjects.Remove(870);Session.WorldObjects.Remove(871);GI->RemoveLocalPlayer(Local);LootPC->Destroy();};
        TestTrue(TEXT("Loot regression uses the real local-controller dispatch"),GI->GetFirstLocalPlayerController()==LootPC);
        FACEWorldObject Corpse;Corpse.Guid=870;Corpse.Name=TEXT("Corpse of Red Rat");Corpse.bHasPosition=true;Corpse.Position=Session.PlayerPosition;Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;Session.WorldObjects.Add(870,Corpse);
        FACEWorldObject Drop;Drop.Guid=871;Drop.Name=TEXT("Loot");Drop.ContainerId=870;Drop.StackSize=1;Session.WorldObjects.Add(871,Drop);
        auto Intent=MakeShared<FJsonObject>();Intent->SetNumberField(TEXT("item"),870);
        for(int32 Mode:{1,2,4,8})for(const TCHAR* Action:{TEXT("open_corpse"),TEXT("use_world")})
        {
            Session.PlayerVitals.CombatMode=Mode;
            TestTrue(TEXT("Corpse dispatch is handled"),H->ExecuteInventory(*P,Intent,Action));
            TestTrue(TEXT("Corpse dispatch starts normal approach"),LootPC->IsUseApproachActive());
            TestEqual(TEXT("Opening corpse preserves peace/melee/missile/magic mode"),Session.PlayerVitals.CombatMode,Mode);
            Session.OpenExternalContainerGuid=870;
            auto Pickup=MakeShared<FJsonObject>();Pickup->SetNumberField(TEXT("item"),871);
            H->ExecuteInventory(*P,Pickup,TEXT("loot"));
            TestEqual(TEXT("Taking corpse loot preserves mode"),Session.PlayerVitals.CombatMode,Mode);
            const double Deadline=P->NextAction;H->RefreshActionWait(*P);
            TestEqual(TEXT("Unconfirmed pickup retains throttle"),P->NextAction,Deadline);
            FACEAppraisalInfo OtherID;OtherID.ObjectGuid=123456;H->ObserveAppraisal(OtherID);
            TestEqual(TEXT("Unrelated appraisal cannot release a pickup"),P->NextAction,Deadline);
            Session.WorldObjects[871].ContainerId=Session.PlayerGuid;H->RefreshActionWait(*P);
            TestTrue(TEXT("Ownership confirmation removes fixed one-second pickup delay"),P->NextAction<=P->ActionSentAt+.25);
            TestEqual(TEXT("Confirmation wakes next policy decision"),P->NextDecision,0.);
            Session.WorldObjects[871].ContainerId=870;
            H->ExecuteInventory(*P,Intent,TEXT("close_corpse"));
            TestEqual(TEXT("Closing corpse preserves mode"),Session.PlayerVitals.CombatMode,Mode);
        }
        {
            const bool Busy=Session.bUseBusy;const int32 Equipment=Session.PendingEquipmentGuid;
            const int32 SourceGuid=Session.UseSourceGuid,TargetGuid=Session.UseTargetGuid;
            ON_SCOPE_EXIT {Session.bUseBusy=Busy;Session.PendingEquipmentGuid=Equipment;Session.UseSourceGuid=SourceGuid;Session.UseTargetGuid=TargetGuid;H->PendingSpell=0;};
            Session.PendingEquipmentGuid=0;Session.OpenExternalContainerGuid=0;H->PendingSpell=0;P->Running=true;
            LootPC->BeginUseApproach(870,.6f);H->UseApproachOwner=P->Id;
            H->TrackActionWait(*P,TEXT("open_corpse"),870);P->NextAction=FPlatformTime::Seconds()-1;
            Session.BeginPendingUse(870,0);H->RefreshActionWait(*P);
            TestEqual(TEXT("Long corpse approach retains ownership after initial throttle"),P->WaitAction,FString(TEXT("open_corpse")));
            TestTrue(TEXT("Only the owned corpse approach is available for interruption"),H->CanPauseLootApproach(*P));
            TestTrue(TEXT("Busy snapshot exposes the safe corpse interruption"),H->Snapshot()->GetBoolField(TEXT("loot_approaching")));
            H->PendingSpell=123;TestFalse(TEXT("A pending cast cannot be released as corpse approach"),H->CanPauseLootApproach(*P));H->PendingSpell=0;
            Session.PendingEquipmentGuid=871;TestFalse(TEXT("Pending equipment cannot be released as corpse approach"),H->CanPauseLootApproach(*P));Session.PendingEquipmentGuid=0;
            Session.UseSourceGuid=871;TestFalse(TEXT("An unrelated use cannot be released as corpse approach"),H->CanPauseLootApproach(*P));Session.UseSourceGuid=870;
            H->TrackActionWait(*P,TEXT("loot"),871);TestFalse(TEXT("An item transfer is not a cancellable approach"),H->CanPauseLootApproach(*P));
            H->TrackActionWait(*P,TEXT("open_corpse"),870);
            auto Pause=MakeShared<FJsonObject>();Pause->SetStringField(TEXT("action"),TEXT("pause_loot_approach"));
            H->Execute(*P,Pause);
            TestTrue(TEXT("Combat interruption releases only approach and its use wait"),!LootPC->IsUseApproachActive()&&!Session.IsUseBusy()&&P->WaitAction.IsEmpty()&&P->NextAction==0);
            TestTrue(TEXT("Interrupting looting keeps UCM running"),P->Running);
        }
        Corpse.ObjectDescriptionFlags=0;Session.WorldObjects.Add(870,Corpse);Session.PlayerVitals.CombatMode=8;
        H->ExecuteInventory(*P,Intent,TEXT("use_world"));
        TestEqual(TEXT("Ordinary world-use stance behavior is unchanged"),Session.PlayerVitals.CombatMode,1);
        H->TrackActionWait(*P,TEXT("equip"),871,0x100000);P->NextAction=P->ActionSentAt+2;
        H->RefreshActionWait(*P);TestFalse(TEXT("Equipment request does not complete before wield update"),P->WaitAction.IsEmpty());
        Session.WorldObjects[871].WielderId=Session.PlayerGuid;Session.WorldObjects[871].CurrentWieldedLocation=0x100000;
        H->RefreshActionWait(*P);TestTrue(TEXT("Wield confirmation releases equipment delay"),P->WaitAction.IsEmpty()&&P->NextAction<=P->ActionSentAt+.25);
        H->TrackActionWait(*P,TEXT("identify"),871);P->NextAction=P->ActionSentAt+.5;
        FACEAppraisalInfo Reply;Reply.ObjectGuid=870;H->ObserveAppraisal(Reply);
        TestFalse(TEXT("Different appraisal cannot wake item decision"),P->WaitAction.IsEmpty());
        Reply.ObjectGuid=871;H->ObserveAppraisal(Reply);
        TestTrue(TEXT("Requested appraisal wakes decision, including a failed appraisal"),P->WaitAction.IsEmpty()&&P->NextAction<=P->ActionSentAt+.25);
        H->Appraisals.Remove(870);H->Appraisals.Remove(871);H->Appraisals.Remove(123456);
        H->TrackActionWait(*P,TEXT("use"));P->NextAction=P->ActionSentAt+3;
        H->RefreshActionWait(*P);TestFalse(TEXT("Consumable waits for UseDone"),P->WaitAction.IsEmpty());
        H->ObserveUseDone(0);H->RefreshActionWait(*P);
        TestTrue(TEXT("Consumable completion releases three-second fallback"),P->WaitAction.IsEmpty()&&P->NextAction<=P->ActionSentAt+.25);
        P->NextAction=0;
    }
    // Exercise AC's +Y-forward pose and AC-space turn integration, not a
    // synthetic Unreal X-forward pawn (which masked both steering errors).
    auto* PC=World->SpawnActor<AACEPlayerController>();auto* Pawn=World->SpawnActor<APawn>();
    auto* Root=NewObject<USceneComponent>(Pawn);Pawn->SetRootComponent(Root);Root->RegisterComponent();PC->Possess(Pawn);
    {
        H->MovementOwner.Empty();
        FACEWorldObject Player;Player.Guid=Session.PlayerGuid;Player.Name=TEXT("Caster");Session.WorldObjects.Add(Player.Guid,Player);
        auto Begin=[&]()
        {
            H->PendingSpell=123;H->PendingSpellAt=FPlatformTime::Seconds();H->PendingSpellOwner=TEXT("ucm");
            H->PendingSpellConfirmation=TEXT("You cast Focus Self I on yourself");
            H->PendingSpellConfirmed=false;H->PendingSpellFizzled=false;H->FastCastOwner=TEXT("ucm");
            H->FastCastStarted=false;H->FastCastMovementApplied=false;
            H->ObserveMetaChat(TEXT("words"),TEXT("Other"),ACEChatMessageType::Spellcasting);
            TestFalse(TEXT("Another caster cannot start backward input"),H->FastCastStarted);
            H->ObserveMetaChat(TEXT("words"),TEXT("Caster"),ACEChatMessageType::Spellcasting);
        };
        auto Drive=[&]() {float F=0,R=0,T=0;H->ApplyMovement(PC,F,R,T,false,false,false,FVector::ForwardVector);return F;};
        Begin();TestEqual(TEXT("Local words start normal backward input"),Drive(),-1.f);
        H->ObserveMetaChat(TEXT("You cast Focus Self I on yourself"),TEXT("Other"),ACEChatMessageType::Magic);
        H->ObserveMetaChat(TEXT("You cast Focus Self II on yourself"),TEXT(""),ACEChatMessageType::Magic);
        TestFalse(TEXT("Wrong sender or spell cannot complete our buff"),H->PendingSpellConfirmed);
        H->ObserveMetaChat(TEXT("You cast Focus Self I on yourself, refreshing Focus Self I"),TEXT(""),ACEChatMessageType::Magic);
        TestEqual(TEXT("Spell result releases backward input before delayed UseDone"),Drive(),0.f);
        TestEqual(TEXT("Result retains the request until its server acknowledgment"),H->PendingSpell,123);
        P->NextAction=FPlatformTime::Seconds()+3.5;P->NextDecision=FPlatformTime::Seconds()+.25;
        H->ObserveUseDone(0);TestTrue(TEXT("Acknowledged matching result confirms the buff"),H->LastSpellConfirmed);
        TestTrue(TEXT("Completed fast buff is ready immediately, without a half-second floor"),P->NextAction<=FPlatformTime::Seconds()&&P->NextDecision==0);
        Begin();H->ObserveMetaChat(TEXT("You cast Focus Self I on yourself"),TEXT(""),ACEChatMessageType::Magic);
        TestEqual(TEXT("Coalesced words and result still produce one recoil-cancel edge"),Drive(),-1.f);
        TestEqual(TEXT("Coalesced result never leaves backward held"),Drive(),0.f);H->ObserveUseDone(0);
        Begin();H->ObserveMetaChat(TEXT("Your spell fizzled."),TEXT(""),ACEChatMessageType::Magic);
        TestEqual(TEXT("Fizzle releases movement immediately"),Drive(),0.f);H->ObserveUseDone(0);
        TestFalse(TEXT("Success UseDone after a fizzle cannot confirm a forced buff"),H->LastSpellConfirmed);
        Begin();TestEqual(TEXT("Unconfirmed cast begins with a bump"),Drive(),-1.f);
        H->FastCastStartedAt=FPlatformTime::Seconds()-.13;
        TestEqual(TEXT("Missing result releases backward within 120ms"),Drive(),0.f);
        TestEqual(TEXT("Movement timeout does not discard the pending spell"),H->PendingSpell,123);
        H->ObserveMetaChat(TEXT("words"),TEXT("Caster"),ACEChatMessageType::Spellcasting);
        TestEqual(TEXT("Repeated spell words cannot restart an expired bump"),Drive(),0.f);
        H->ObserveUseDone(0);TestFalse(TEXT("Missing spell result cannot confirm a forced buff"),H->LastSpellConfirmed);
        Begin();H->FastCastStartedAt=FPlatformTime::Seconds()-.13;
        TestEqual(TEXT("A blocked or delayed frame never starts a stale bump"),Drive(),0.f);
        H->ObserveUseDone(0);
    }
    {
        auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
        if(!TestTrue(TEXT("Retail DAT loads for redirected bane confirmations"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
        {
            const auto SavedSkills=Session.PlayerVitals.Skills;
            FACEWorldObject Scroll;Scroll.Guid=0x71009f00;Scroll.ItemType=ACEItemType::Writable;Scroll.ContainerId=Session.PlayerGuid;
            ON_SCOPE_EXIT {Session.PlayerVitals.Skills=SavedSkills;Session.WorldObjects.Remove(Scroll.Guid);H->InventoryRevision=MAX_uint64;};
            for(uint32 ExpectedPower:{1u,250u,300u,400u})
            {
                Scroll.SpellDID=0;
                for(uint32 Id=1;Id<7000;++Id)
                {
                    uint32 School=0,Power=0,Category=0,Flags=0;double Duration=0;
                    if(Dat->TryGetPluginSpellInfo(Id,School,Power,Category,Flags,Duration)&&School==1&&Power==ExpectedPower){Scroll.SpellDID=Id;break;}
                }
                if(!TestTrue(*FString::Printf(TEXT("Retail spell found for scroll power %u"),ExpectedPower),Scroll.SpellDID!=0))return false;
                Session.WorldObjects.Add(Scroll.Guid,Scroll);H->InventoryRevision=MAX_uint64;
                FACESkillInfo Skill;Skill.SkillId=34;Skill.AdvancementClass=1;Skill.Current=500;
                Session.PlayerVitals.Skills={Skill};
                for(int Case=0;Case<3;++Case)
                {
                    if(Case>0){Session.PlayerVitals.Skills[0].AdvancementClass=2;Session.PlayerVitals.Skills[0].Current=Case==1?199:200;}
                    auto Snapshot=MakeShared<FJsonObject>();Snapshot->SetObjectField(TEXT("position"),MakeShared<FJsonObject>());H->ExtendSnapshot(Snapshot);
                    bool Found=false;
                    for(const auto& Record:Snapshot->GetArrayField(TEXT("inventory")))
                    {
                        const auto Item=Record->AsObject();if(Item->GetNumberField(TEXT("id"))!=double(uint32(Scroll.Guid)))continue;
                        Found=true;TestEqual(TEXT("UCM learning eligibility follows retail tiers and live skill updates"),Item->GetBoolField(TEXT("scroll_can_learn")),ExpectedPower!=250||Case==2);
                    }
                    TestTrue(TEXT("Owned scroll reaches automation snapshot"),Found);
                }
            }
        }
        FACEWorldObject Armor;Armor.Guid=0x71009000;Armor.Name=TEXT("Copper Helm");Armor.ItemType=ACEItemType::Armor;Armor.WielderId=Session.PlayerGuid;
        FACEWorldObject Shirt=Armor;Shirt.Guid++;Shirt.Name=TEXT("Linen Shirt");Shirt.ItemType=ACEItemType::Clothing;
        FACEWorldObject Carried=Armor;Carried.Guid+=2;Carried.Name=TEXT("Carried Helm");Carried.WielderId=0;Carried.ContainerId=Session.PlayerGuid;
        FACEWorldObject Other=Armor;Other.Guid+=3;Other.Name=TEXT("Other Helm");Other.WielderId=Session.PlayerGuid+1;
        for(const auto& Item:{Armor,Shirt,Carried,Other})Session.WorldObjects.Add(Item.Guid,Item);
        ON_SCOPE_EXIT{for(const auto& Item:{Armor,Shirt,Carried,Other})Session.WorldObjects.Remove(Item.Guid);H->ItemBuffs.Empty();};
        FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
        for(const FString Name:{TEXT("Blade Bane I"),TEXT("Impenetrability I")})
        {
            uint32 Spell=0,School=0,Power=0,Category=0,Flags=0;double Duration=0;
            for(uint32 Id=1;Id<7000;++Id){FString Candidate;uint32 Icon=0;if(Dat->TryGetSpellInfo(Id,Candidate,Icon)&&Candidate==Name){Spell=Id;break;}}
            if(!TestTrue(TEXT("Retail bane/impen spell found"),Spell&&Dat->TryGetPluginSpellInfo(Spell,School,Power,Category,Flags,Duration)))return false;
            auto Begin=[&](int32 Target)
            {
                H->ItemBuffs.Empty();H->PendingSpell=Spell;H->PendingSpellTarget=Target;
                H->PendingSpellAt=FPlatformTime::Seconds();H->PendingSpellConfirmed=false;H->PendingSpellFizzled=false;
                H->PrepareSpellConfirmation(Name);
            };
            auto Chat=[&](const FString& Target,const FString& Suffix=TEXT(""),const FString& Sender=TEXT(""))
            {H->ObserveMetaChat(TEXT("You cast ")+Name+TEXT(" on ")+Target+Suffix,Sender,ACEChatMessageType::Magic);};
            for(const FString& Suffix:TArray<FString>{TEXT(""),TEXT("."),TEXT(", refreshing ")+Name,TEXT(", surpassing ")+Name,TEXT(", but it is surpassed by ")+Name})
            {
                Begin(Session.PlayerGuid);
                Chat(Carried.Name);Chat(Other.Name);Chat(Armor.Name+TEXT(" Replica"));Chat(Armor.Name,TEXT(""),TEXT("Other caster"));
                H->ObserveMetaChat(TEXT("You cast Wrong Spell on ")+Armor.Name,TEXT(""),ACEChatMessageType::Magic);
                TestFalse(TEXT("Unrelated item/spell/sender cannot confirm a self bane"),H->PendingSpellConfirmed);
                Chat(Armor.Name,Suffix);Chat(Shirt.Name,Suffix);
                TestTrue(TEXT("Worn item result confirms the redirected spell"),H->PendingSpellConfirmed);
                H->ObserveUseDone(0);
                TestTrue(TEXT("Acknowledged bane succeeds"),H->LastSpellConfirmed);
                TestEqual(TEXT("Multiple armor results retain one player-targeted family timer"),H->ItemBuffs.Num(),1);
                if(H->ItemBuffs.Num())TestEqual(TEXT("Timer belongs to the requested player"),H->ItemBuffs[0]->AsObject()->GetNumberField(TEXT("target")),double(uint32(Session.PlayerGuid)));
            }
            // Feed the real host timer into UCM, including a manual forced cycle.
            for(bool Forced:{false,true})
            {
                FACEPluginVM VM;if(!TestTrue(*Error,VM.Load(Source,Error)))return false;
                TSharedPtr<FJsonObject> State=MakeShared<FJsonObject>();
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"combat_mode":8,"components_required":false,"inventory":[{"id":99,"type":32768,"equipped":true}],"spells":[]})")),State);
                State->SetNumberField(TEXT("player"),uint32(Session.PlayerGuid));State->SetNumberField(TEXT("time"),FPlatformTime::Seconds());
                State->SetNumberField(TEXT("action_serial"),H->ActionSerial);
                auto Settings=MakeShared<FJsonObject>();Settings->SetBoolField(TEXT("buffing"),true);Settings->SetBoolField(TEXT("recovery"),false);Settings->SetStringField(TEXT("combat"),TEXT("off"));
                auto Info=MakeShared<FJsonObject>();Info->SetNumberField(TEXT("id"),Spell);Info->SetNumberField(TEXT("category"),Category);Info->SetNumberField(TEXT("school"),School);Info->SetNumberField(TEXT("power"),Power);Info->SetNumberField(TEXT("duration"),Duration);Info->SetNumberField(TEXT("skill"),600);Info->SetNumberField(TEXT("target_type"),6);Info->SetBoolField(TEXT("beneficial"),true);
                State->SetArrayField(TEXT("spells"),{MakeShared<FJsonValueObject>(Info)});
                if(Forced){State->SetNumberField(TEXT("force_buff_request"),1);State->SetArrayField(TEXT("item_buffs"),H->ItemBuffs);}
                TSharedPtr<FJsonObject> Intent;TestTrue(*Error,VM.Step(State,Settings,Intent,Error));
                if(!TestTrue(TEXT("Buff cycle issues initial bane"),Intent&&Intent->HasField(TEXT("spell"))))return false;
                Begin(Session.PlayerGuid);Chat(Armor.Name);H->ObserveUseDone(0);
                State->SetNumberField(TEXT("action_serial"),H->ActionSerial);State->SetNumberField(TEXT("last_spell"),Spell);State->SetBoolField(TEXT("last_spell_confirmed"),H->LastSpellConfirmed);State->SetNumberField(TEXT("action_error"),0);State->SetArrayField(TEXT("item_buffs"),H->ItemBuffs);
                TestTrue(*Error,VM.Step(State,Settings,Intent,Error));
                TestFalse(TEXT("Confirmed bane advances instead of repeating"),Intent->HasField(TEXT("spell")));
                if(Forced)TestEqual(TEXT("Force Buff completes after item result"),Intent->GetStringField(TEXT("action")),FString(TEXT("force_buff_done")));
                State->SetNumberField(TEXT("force_buff_request"),0);State->SetNumberField(TEXT("time"),FPlatformTime::Seconds()+Duration+1);
                TestTrue(*Error,VM.Step(State,Settings,Intent,Error));TestTrue(TEXT("Bane becomes eligible again when its timer expires"),Intent->HasField(TEXT("spell")));
            }
            Begin(Session.PlayerGuid);H->ObserveUseDone(0);TestTrue(TEXT("UseDone alone cannot manufacture a bane timer"),H->ItemBuffs.IsEmpty());
            Begin(Session.PlayerGuid);Chat(Armor.Name);H->ObserveMetaChat(TEXT("Your spell fizzled."),TEXT(""),ACEChatMessageType::Magic);H->ObserveUseDone(0);
            TestTrue(TEXT("Fizzle does not retain a bane timer"),H->ItemBuffs.IsEmpty());
            Begin(Session.PlayerGuid);Chat(Armor.Name);H->ObserveUseDone(1);TestTrue(TEXT("Failed UseDone cannot retain a bane timer"),H->ItemBuffs.IsEmpty());
            Begin(Armor.Guid);Chat(Shirt.Name);TestFalse(TEXT("An explicit item buff cannot confirm against a different worn item"),H->PendingSpellConfirmed);
            Chat(Armor.Name);H->ObserveUseDone(0);TestTrue(TEXT("Explicit item result still confirms"),H->LastSpellConfirmed);
            const int32 OldDurationAug=Session.PlayerVitals.StatQualityInts.FindRef(238);
            for(int32 Augments:{0,1,5})
            {
                Session.PlayerVitals.StatQualityInts.Add(238,Augments);
                Begin(Session.PlayerGuid);Chat(Armor.Name);H->ObserveUseDone(0);
                TestEqual(TEXT("Confirmed item buff timer includes retail spell-duration augmentations"),
                    H->ItemBuffs[0]->AsObject()->GetNumberField(TEXT("duration")),Duration*(1.+.2*Augments));
            }
            Session.PlayerVitals.StatQualityInts.Add(238,OldDurationAug);
        }
    }
    {
        auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
        for(const FString Name:{TEXT("Heal Self I"),TEXT("Revitalize Self I"),TEXT("Mana Boost Self I"),TEXT("Heal Other I")})
        {
            uint32 Spell=0;for(uint32 Id=1;Id<7000;++Id){FString Candidate;uint32 Icon=0;if(Dat->TryGetSpellInfo(Id,Candidate,Icon)&&Candidate==Name){Spell=Id;break;}}
            if(!TestTrue(TEXT("Retail recovery spell exists"),Spell!=0))return false;
            const bool Self=!Name.Contains(TEXT("Other"));
            FACEWorldObject Friend;Friend.Guid=0x71009010;Friend.Name=TEXT("Friend");Session.WorldObjects.Add(Friend.Guid,Friend);
            const FString Vital=Name.StartsWith(TEXT("Heal"))?TEXT("health"):Name.StartsWith(TEXT("Revitalize"))?TEXT("stamina"):TEXT("mana");
            const FString Message=Self?TEXT("You cast ")+Name+TEXT(" and restore 42 points of your ")+Vital+TEXT(".")
                :TEXT("With ")+Name+TEXT(" you restore 42 points of ")+Vital+TEXT(" to Friend.");
            auto Begin=[&]()
            {
                H->PendingSpell=Spell;H->PendingSpellTarget=Self?Session.PlayerGuid:Friend.Guid;H->PendingSpellAt=FPlatformTime::Seconds();
                H->PendingSpellOwner=P->Id;H->PendingSpellConfirmed=false;H->PendingSpellFizzled=false;H->PrepareSpellConfirmation(Name);
            };
            Begin();H->ObserveMetaChat(Message,TEXT("Other caster"),ACEChatMessageType::Magic);
            H->ObserveMetaChat(Message.Replace(*Name,TEXT("Unrelated Spell")),TEXT(""),ACEChatMessageType::Magic);
            H->ObserveMetaChat(Message.Replace(TEXT("42"),TEXT("invalid")),TEXT(""),ACEChatMessageType::Magic);
            if(!Self)H->ObserveMetaChat(Message.Replace(TEXT("Friend."),TEXT("Someone Else.")),TEXT(""),ACEChatMessageType::Magic);
            TestFalse(TEXT("Recovery confirmation rejects unrelated or malformed messages"),H->PendingSpellConfirmed);
            H->ObserveMetaChat(Message,TEXT(""),ACEChatMessageType::Magic);H->ObserveUseDone(0);
            TestTrue(TEXT("VT recovery message confirms the cast"),H->LastSpellConfirmed);
            TestTrue(TEXT("Recovery result releases the host cast delay immediately"),P->NextAction<=FPlatformTime::Seconds());
            Begin();H->ObserveMetaChat(Message,TEXT(""),ACEChatMessageType::Magic);H->ObserveUseDone(1);
            TestFalse(TEXT("Recovery error still overrides chat success"),H->LastSpellConfirmed);
            Session.WorldObjects.Remove(Friend.Guid);
        }
    }
    {
        // Real DAT spells and loopback packets exercise result-driven buff
        // dispatch, including servers which reject casts during recoil.
        const auto SavedPlugin=*P;const auto SavedVitals=Session.PlayerVitals;const auto SavedSpells=Session.KnownSpells;
        auto* BuffPC=World->SpawnActor<AACEPlayerController>();auto* BuffPawn=World->SpawnActor<APawn>();
        auto* BuffRoot=NewObject<USceneComponent>(BuffPawn);BuffPawn->SetRootComponent(BuffRoot);BuffRoot->RegisterComponent();BuffPC->Possess(BuffPawn);
        auto* Local=NewObject<ULocalPlayer>(GEngine);GI->AddLocalPlayer(Local,FPlatformUserId::CreateFromInternalId(0));BuffPC->SetPlayer(Local);
        auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Fast buff receiver"),false);
        auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
        if(!TestTrue(TEXT("Fast buff test binds loopback"),Receiver&&Receiver->Bind(*Address)))return false;
        Receiver->GetAddress(*Address);Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Fast buff sender"),false);
        Session.ServerC2SAddr=Address;Session.IssacClient=MakeUnique<FACEIsaac>(123u);
        ON_SCOPE_EXIT
        {
            *P=SavedPlugin;Session.PlayerVitals=SavedVitals;Session.KnownSpells=SavedSpells;
            H->PendingSpell=0;H->PendingSpellOwner.Empty();H->PendingBuffResultDriven=false;H->BuffRetryAt=0;
            H->ConfirmedBuffAckAt=0;H->ConfirmedBuffOwner.Empty();H->DeferredBuffIntent.Reset();H->DeferredBuffOwner.Empty();H->FastCastOwner.Empty();H->ItemBuffs.Empty();
            FTSTicker::GetCoreTicker().RemoveTicker(H->DecisionWakeTicker);H->DecisionWakeTicker.Reset();
            Session.SocketC2S->Close();Sockets->DestroySocket(Session.SocketC2S);Session.SocketC2S=nullptr;
            Session.ServerC2SAddr.Reset();Session.IssacClient.Reset();Receiver->Close();Sockets->DestroySocket(Receiver);Session.CachedC2SPackets.Reset();
            GI->RemoveLocalPlayer(Local);BuffPC->Destroy();BuffPawn->Destroy();
        };
        auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();int32 Focus=0,Self=0;
        for(uint32 Id=1;Id<7000&&(!Focus||!Self);++Id)
        {FString Name;uint32 Icon;if(Dat->TryGetSpellInfo(Id,Name,Icon)){if(Name==TEXT("Focus Self I"))Focus=Id;if(Name==TEXT("Willpower Self I"))Self=Id;}}
        if(!TestTrue(TEXT("Retail focus and self buffs found"),Focus&&Self))return false;
        Session.KnownSpells={Focus,Self};Session.PlayerVitals.CombatMode=8;Session.PlayerVitals.Health=100;
        Session.bUseBusy=false;Session.PendingEquipmentGuid=0;P->Running=true;P->Player=Session.PlayerGuid;P->Server=C->GetServerName();
        P->Permissions.Add(TEXT("cast"));P->Permissions.Add(TEXT("navigation"));P->WaitAction.Empty();P->NextAction=0;
        auto Intent=[&](int32 Spell)
        {
            auto I=MakeShared<FJsonObject>();I->SetStringField(TEXT("action"),TEXT("cast"));I->SetStringField(TEXT("activity"),TEXT("buffs"));
            I->SetNumberField(TEXT("spell"),Spell);I->SetNumberField(TEXT("target"),uint32(Session.PlayerGuid));I->SetBoolField(TEXT("fast_cast"),true);return I;
        };
        auto Result=[&](int32 Spell)
        {FString Name;uint32 Icon;Dat->TryGetSpellInfo(Spell,Name,Icon);H->ObserveMetaChat(TEXT("You cast ")+Name+TEXT(" on yourself."),TEXT(""),ACEChatMessageType::Magic);};
        auto Ack=[&](uint32 Error){FACEBinaryWriter Packet;Packet.WriteUInt32(Error);FACEBinaryReader R(Packet.GetData());Session.HandleUseDone(R);};
        auto Drive=[&](){float F=0,R=0,T=0;H->ApplyMovement(BuffPC,F,R,T,false,false,false,FVector::ForwardVector);return F;};
        auto CastCount=[&](){int32 Count=0;for(const auto& Packet:Session.CachedC2SPackets){FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);if(R.CanRead(4)&&R.ReadUInt32()==ACEGameAction::CastTargetedSpell)++Count;}return Count;};
        H->Execute(*P,Intent(Focus));TestTrue(TEXT("Buff dispatch enables result-driven pipeline"),H->PendingBuffResultDriven&&H->PendingSpell==Focus);
        H->ObserveMetaChat(TEXT("words"),TEXT("Caster"),ACEChatMessageType::Spellcasting);Result(Focus);
        TestEqual(TEXT("Coalesced buff result waits for the cancellation movement edge"),H->PendingSpell,Focus);
        TestEqual(TEXT("Result-driven cast preserves one backward edge"),Drive(),-1.f);
        TestTrue(TEXT("Confirmed buff advances before late UseDone"),!H->PendingSpell&&H->ConfirmedBuffAckAt&&H->LastSpellConfirmed&&P->NextAction<=FPlatformTime::Seconds());
        TestEqual(TEXT("Backward movement releases on the next frame"),Drive(),0.f);
        H->Execute(*P,Intent(Self));TestEqual(TEXT("Next buff sends without waiting through recoil"),CastCount(),2);
        const auto Serial=H->ActionSerial;
        Ack(0x001D);TestTrue(TEXT("Busy during recoil preserves second buff and schedules retry"),H->PendingSpell==Self&&H->BuffRetryAt&&H->ActionSerial==Serial&&H->ConfirmedBuffAckAt);
        Ack(0);TestTrue(TEXT("Old success only drains the first buff acknowledgment"),!H->ConfirmedBuffAckAt&&H->PendingSpell==Self&&H->ActionSerial==Serial);
        H->UpdateBuffDispatch();TestEqual(TEXT("Draining recoil promptly retries the rejected buff"),CastCount(),3);
        H->UpdateBuffDispatch();TestEqual(TEXT("Accepted retry is not resent without a rejection"),CastCount(),3);
        H->ObserveMetaChat(TEXT("words"),TEXT("Caster"),ACEChatMessageType::Spellcasting);Drive();Result(Self);
        TestTrue(TEXT("Retry result confirms once and clears movement"),H->ActionSerial==Serial+1&&H->LastCompletedSpell==Self&&H->LastSpellConfirmed&&Drive()==0);
        auto FinishNotice=MakeShared<FJsonObject>();FinishNotice->SetStringField(TEXT("action"),TEXT("notice"));FinishNotice->SetStringField(TEXT("text"),TEXT("Buff pipeline complete"));
        H->Execute(*P,FinishNotice);TestTrue(TEXT("Non-buff intent waits for outstanding untagged acknowledgment"),H->DeferredBuffIntent.IsValid());
        Ack(0x001D);TestTrue(TEXT("Unrelated rejected manual use cannot drain a successful buff acknowledgment"),H->ConfirmedBuffAckAt&&H->DeferredBuffIntent.IsValid());
        Ack(0);H->NextMaintenanceAt=FPlatformTime::Seconds()+60;H->Tick(.016f);
        TestTrue(TEXT("Deferred intent runs exactly after acknowledgment"),!H->DeferredBuffIntent&&H->Notice==TEXT("Buff pipeline complete"));
        // When queue support accepts a second buff, even an early second result
        // cannot create two acknowledgment debts or complete an unrelated action.
        P->NextAction=0;H->Execute(*P,Intent(Focus));Result(Focus);H->Execute(*P,Intent(Self));
        Result(Self);TestTrue(TEXT("Only one confirmed cast may be awaiting its acknowledgment"),H->ConfirmedBuffAckAt&&H->PendingSpell==Self);
        Ack(0);TestEqual(TEXT("First queued acknowledgment cannot finish the second buff"),H->PendingSpell,Self);
        Ack(0);TestTrue(TEXT("Second queued acknowledgment finishes the second buff"),!H->PendingSpell&&!H->ConfirmedBuffAckAt&&H->LastSpellConfirmed);
        // Standard ack-only completion and fizzle remain conservative.
        P->NextAction=0;H->Execute(*P,Intent(Focus));Ack(0);
        TestFalse(TEXT("UseDone alone never confirms a buff"),H->LastSpellConfirmed);
        P->NextAction=0;H->Execute(*P,Intent(Focus));H->ObserveMetaChat(TEXT("Your spell fizzled."),TEXT(""),ACEChatMessageType::Magic);Ack(0);
        TestTrue(TEXT("Fizzle creates no acknowledgment debt"),!H->ConfirmedBuffAckAt&&!H->LastSpellConfirmed&&H->FastCastOwner.IsEmpty());
        P->NextAction=0;H->Execute(*P,Intent(Focus));H->PendingSpellAt=FPlatformTime::Seconds()-6;Ack(0x001D);
        TestTrue(TEXT("Busy retry window is bounded"),!H->PendingSpell&&!H->BuffRetryAt&&H->LastActionError==0x001D);
        P->NextAction=0;H->Execute(*P,Intent(Focus));Result(Focus);H->Execute(*P,FinishNotice);H->Stop(P->Id);
        TestTrue(TEXT("Stop discards deferred actions but retains late acknowledgment guard"),!H->DeferredBuffIntent&&H->ConfirmedBuffAckAt&&!H->BuffRetryAt);
        Ack(0);TestFalse(TEXT("Late acknowledgment after Stop is drained"),H->ConfirmedBuffAckAt!=0);
        // Death wakes bypass only the old physical attack retry interval, and
        // use the same packet path for melee and missile on the next opponent.
        P->Running=true;P->Permissions.Add(TEXT("combat"));P->WaitAction.Empty();
        FACEWorldObject Enemy;Enemy.Guid=970;Enemy.Name=TEXT("Target one");Enemy.ItemType=ACEItemType::Creature;
        Enemy.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;Enemy.bHasPosition=true;Enemy.Position=Session.PlayerPosition;Enemy.Position.Location.X+=2;
        FACEWorldObject NextEnemy=Enemy;NextEnemy.Guid=971;NextEnemy.Name=TEXT("Target two");NextEnemy.Position.Location.Y+=2;
        ON_SCOPE_EXIT{Session.WorldObjects.Remove(970);Session.WorldObjects.Remove(971);H->CombatTarget=0;H->DefeatedTargets.Empty();H->PhysicalAttackTarget=0;H->PhysicalAttackOwner.Empty();Session.SelectedObject.Guid=0;};
        auto AttackIntent=[&](int32 Target,int32 Mode)
        {auto I=MakeShared<FJsonObject>();I->SetStringField(TEXT("action"),TEXT("attack"));I->SetStringField(TEXT("activity"),TEXT("combat"));I->SetNumberField(TEXT("target"),Target);I->SetNumberField(TEXT("mode"),Mode);return I;};
        auto AttackCount=[&](){int32 Count=0;for(const auto& Packet:Session.CachedC2SPackets){FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);if(R.CanRead(4)){const uint32 Op=R.ReadUInt32();if(Op==ACEGameAction::TargetedMeleeAttack||Op==ACEGameAction::TargetedMissileAttack)++Count;}}return Count;};
        for(int32 Mode:{2,4})
        {
            Session.WorldObjects.Add(970,Enemy);Session.WorldObjects.Add(971,NextEnemy);H->DefeatedTargets.Empty();H->PhysicalAttackOwner.Empty();
            Session.PlayerVitals.CombatMode=Mode;P->NextAction=0;H->Execute(*P,AttackIntent(970,Mode));
            const int32 Before=AttackCount();TestTrue(TEXT("Physical attack establishes a tracked target"),H->CombatTarget==970&&H->PhysicalAttackTarget==970);
            auto Changed=Session.PlayerVitals;Changed.Health=40;H->LastCombatVitals=FIntVector(-1,-1,-1);
            Session.OnVitalsUpdated.Broadcast(Changed);
            TestTrue(TEXT("Vital changes release physical throttle for immediate recovery decision"),P->NextAction==0&&P->NextDecision==0&&H->DecisionWakeAt<=FPlatformTime::Seconds());
            H->Execute(*P,AttackIntent(970,Mode));TestEqual(TEXT("Checking recovery without needing it does not restart server attack"),AttackCount(),Before);
            const double Throttle=P->NextAction;Session.OnVitalsUpdated.Broadcast(Changed);
            TestEqual(TEXT("Identical vital updates do not continually wake policy"),P->NextAction,Throttle);
            FACEBinaryWriter Health;Health.WriteUInt32(970);Health.WriteFloat(0);FACEBinaryReader HR(Health.GetData());Session.HandleUpdateHealth(HR);
            TestTrue(TEXT("Zero-health network event bypasses remaining physical attack wait"),P->NextAction==0&&H->CombatTarget==0&&H->DefeatedTargets.Contains(970));
            H->Execute(*P,AttackIntent(971,Mode));TestEqual(TEXT("Next opponent attack is sent immediately after death"),AttackCount(),Before+1);
            auto State=H->Snapshot();bool DeadInSnapshot=false;for(const auto& V:State->GetArrayField(TEXT("targets")))DeadInSnapshot|=V->AsObject()->GetNumberField(TEXT("id"))==970;
            TestFalse(TEXT("Dead opponent stays excluded after selection moves to next opponent"),DeadInSnapshot);
            Session.WorldObjects.Remove(971);Session.OnObjectDeleted.Broadcast(971);
            TestTrue(TEXT("Removal without health update also releases target throttle"),H->CombatTarget==0&&P->NextAction==0);
        }
        Session.WorldObjects.Add(970,Enemy);Session.OnObjectCreated.Broadcast(Enemy);
        TestFalse(TEXT("Newly created object with reused GUID does not inherit dead filter"),H->DefeatedTargets.Contains(970));
        H->CombatTarget=970;H->PendingSpell=Focus;H->PendingSpellOwner=P->Id;H->PendingSpellAt=FPlatformTime::Seconds();P->NextAction=H->PendingSpellAt+3.5;
        const double CastingGate=P->NextAction;Session.WorldObjects[970].bDying=true;FACEObjectMotionState Death;Death.ForwardCommand=ACEMotion::Dead;
        Session.OnMotionUpdate.Broadcast(970,Death);
        TestTrue(TEXT("Death motion wakes targeting but preserves outstanding spell acknowledgment"),H->CombatTarget==0&&H->PendingSpell==Focus&&P->NextAction==CastingGate);
        Ack(0);P->WaitAction=TEXT("equip");P->NextAction=FPlatformTime::Seconds()+2;H->PhysicalAttackOwner=P->Id;H->CombatTarget=970;
        const double EquipmentGate=P->NextAction;H->LastCombatVitals=FIntVector(-1,-1,-1);Session.OnVitalsUpdated.Broadcast(Session.PlayerVitals);
        TestEqual(TEXT("Recovery wake cannot bypass pending equipment"),P->NextAction,EquipmentGate);
        P->WaitAction.Empty();P->NextAction=0;
    }
    for(float Speed:{600.f,2000.f})for(float Arrival:{17.5f,70.f})for(float Rate:{15.f,30.f,90.f,144.f})for(bool VR:{false,true})for(float CameraYaw:{0.f,90.f,210.f})
    {
        H->MoveArrivalRadius=Arrival;
        FVector Location=Origin;FACEPosition Pose=OutdoorPosition;Pose.RotationW=1;Pose.RotationXYZ=FVector::ZeroVector;
        for(const FVector Offset:{FVector(1000,0,0),FVector(1000,1000,0),FVector(0,1000,0),FVector(0,0,0)})
        {
            const FVector Goal=Origin+Offset;
            H->MoveTarget=OutdoorPosition;H->MoveTarget.Location=FACEPosition::AceVectorToUnreal(Offset,.01f);
            H->MovementOwner=TEXT("ucm");H->MoveExpires=FPlatformTime::Seconds()+100;H->LastProgress=FPlatformTime::Seconds();H->LastMovePosition=Location;
            for(int Frame=0;Frame<int(Rate*20)&&FVector::Dist2D(Location,Goal)>=Arrival;++Frame)
            {
                Pose.Location=FACEPosition::AceVectorToUnreal(Location-Origin,.01f);
                Session.PlayerPosition=Pose;Pawn->SetActorLocationAndRotation(Location,Pose.ToUnrealQuat());
                // VR uses body facing; a desktop camera direction must be ignored.
                const FVector Facing=VR?Pose.GetAceForwardVector():FRotator(0,CameraYaw,0).Vector();
                float F=0,R=0,T=0;H->ApplyMovement(PC,F,R,T,false,false,VR,Facing,Speed/Rate);
                const FQuat Turn(FVector::UpVector,-1.5f*1.5f*T/Rate);
                const FQuat Ac=(Turn*Pose.GetAcQuat()).GetNormalized();Pose.RotationW=Ac.W;Pose.RotationXYZ=FVector(Ac.X,Ac.Y,Ac.Z);
                Location+=(Pose.GetAceForwardVector()*F+Pose.GetAceRightVector()*R)*Speed/Rate;
            }
            TestTrue(*FString::Printf(TEXT("%s route reaches each corner within %.1f cm at %.0f FPS, camera %.0f"),VR?TEXT("VR"):TEXT("Desktop"),Arrival,Rate,CameraYaw),FVector::Dist2D(Location,Goal)<Arrival);
        }
    }
    {
        auto Move=MakeShared<FJsonObject>();Move->SetStringField(TEXT("action"),TEXT("move"));
        Move->SetNumberField(TEXT("cell"),uint32(Session.PlayerPosition.CellId));
        Move->SetNumberField(TEXT("x"),Session.PlayerPosition.Location.X);
        Move->SetNumberField(TEXT("y"),Session.PlayerPosition.Location.Y);
        Move->SetNumberField(TEXT("z"),Session.PlayerPosition.Location.Z);
        P->NextAction=0;Session.bUseBusy=false;H->Execute(*P,Move);
        TestTrue(TEXT("Steering leaves no action delay that can block arrival decisions"),P->NextAction<=FPlatformTime::Seconds());
        TestEqual(TEXT("Steering retains movement ownership"),H->MovementOwner,P->Id);
        const FACEPosition Before=H->MoveTarget;Move->SetNumberField(TEXT("x"),Before.Location.X+1);
        P->NextAction=FPlatformTime::Seconds()+2;H->Execute(*P,Move);
        TestEqual(TEXT("Steering still respects an outstanding action throttle"),H->MoveTarget.Location.X,Before.Location.X);
        P->NextAction=0;
    }
    H->MovementOwner=TEXT("ucm");H->MoveTarget=Session.PlayerPosition;H->MoveExpires=FPlatformTime::Seconds()+20;
    H->LastProgress=FPlatformTime::Seconds();H->bMoveArrived=false;P->NextDecision=FPlatformTime::Seconds()+.25;
    float ArriveF=1,ArriveR=1,ArriveT=1;
    H->ApplyMovement(PC,ArriveF,ArriveR,ArriveT,false,false,false,FVector::ForwardVector);
    TestTrue(TEXT("Arrival stops all axes and wakes policy without a breadcrumb pause"),ArriveF==0&&ArriveR==0&&ArriveT==0&&P->NextDecision==0);
    P->NextDecision=FPlatformTime::Seconds()+.25;const double NextArrivalDecision=P->NextDecision;
    H->ApplyMovement(PC,ArriveF,ArriveR,ArriveT,false,false,false,FVector::ForwardVector);
    TestEqual(TEXT("Idle arrival does not force repeated expensive policy snapshots"),P->NextDecision,NextArrivalDecision);
    H->MovementOwner=TEXT("ucm");H->MoveExpires=FPlatformTime::Seconds()+20;
    H->LastMovePosition=Session.PlayerPosition.ToUnrealLocation();H->LastProgress=FPlatformTime::Seconds()-6;
    const uint32 BeforeBlocked=H->MovementBlockedSerial;float BlockF=1,BlockR=1,BlockT=1;
    H->ApplyMovement(PC,BlockF,BlockR,BlockT,false,false,false,FVector::ForwardVector);
    TestEqual(TEXT("Blocked movement asks policy to recover"),H->MovementBlockedSerial,BeforeBlocked+1);
    TestTrue(TEXT("Blocked movement clears drive input before recovery"),H->MovementOwner.IsEmpty()&&BlockF==0&&BlockR==0&&BlockT==0);
    {
        const auto OriginalProfile=P->Profile;
        P->Profile=MakeShared<FJsonObject>(*OriginalProfile);P->Running=true;
        P->Profile->SetBoolField(TEXT("navigation"),true);P->Profile->SetBoolField(TEXT("recovery"),true);P->Profile->SetStringField(TEXT("combat"),TEXT("auto"));
        const auto OriginalVM=P->VM;
        H->MovementOwner=TEXT("ucm");H->UseApproachOwner=TEXT("ucm");H->MoveExpires=FPlatformTime::Seconds()+20;
        auto Pause=MakeShared<FJsonObject>();Pause->SetStringField(TEXT("action"),TEXT("pause_navigation"));Pause->SetStringField(TEXT("status"),TEXT("Navigation paused: blocked door"));
        H->Execute(*P,Pause);
        TestTrue(TEXT("Route failure retains running UCM and its VM"),P->Running&&P->VM==OriginalVM);
        TestFalse(TEXT("Route failure disables shared navigation toggle"),P->Profile->GetBoolField(TEXT("navigation")));
        TestTrue(TEXT("Route failure preserves enabled recovery"),P->Profile->GetBoolField(TEXT("recovery")));
        TestEqual(TEXT("Route failure preserves combat"),P->Profile->GetStringField(TEXT("combat")),FString(TEXT("auto")));
        TestTrue(TEXT("Route failure clears drive and approach ownership"),H->MovementOwner.IsEmpty()&&H->UseApproachOwner.IsEmpty()&&H->MoveExpires==0);
        P->Profile=OriginalProfile;
    }
    for(const TCHAR* Action:{TEXT("salvage"),TEXT("sell"),TEXT("buy"),TEXT("split_note"),TEXT("combine_salvage"),TEXT("store_item")})
    {
        P->Running=true;P->ActivityFailure.Reset();const auto OriginalVM=P->VM;
        Session.CachedC2SPackets.Reset();
        auto Invalid=MakeShared<FJsonObject>();Invalid->SetStringField(TEXT("action"),Action);Invalid->SetNumberField(TEXT("item"),1234567);
        TestTrue(TEXT("Invalid inventory intent is handled"),H->ExecuteInventory(*P,Invalid,Action));
        TestTrue(*FString::Printf(TEXT("Rejected %s retains UCM and VM"),Action),P->Running&&P->VM==OriginalVM);
        TestTrue(TEXT("Host rejection is returned to activity policy"),P->ActivityFailure.IsValid());
        TestTrue(TEXT("Rejected inventory action sends neither destructive request nor attack cancellation"),Session.CachedC2SPackets.IsEmpty());
    }
    P->ActivityFailure.Reset();
    for(const TCHAR* Activity:{TEXT("buffs"),TEXT("combat"),TEXT("recovery2")})
    {
        P->Running=true;const auto OriginalVM=P->VM;
        H->PendingSpell=123;H->PendingSpellOwner=P->Id;H->PendingSpellActivity=Activity;H->PendingSpellAt=FPlatformTime::Seconds()-31;
        H->CheckPendingSpellTimeout();
        TestTrue(TEXT("Cast timeout retains running UCM and its policy"),P->Running&&P->VM==OriginalVM);
        TestEqual(TEXT("Timed-out cast no longer blocks decisions"),H->PendingSpell,0);
        TestTrue(TEXT("Cast timeout reports its specific activity"),P->ActivityFailure&&P->ActivityFailure->GetStringField(TEXT("activity"))==Activity);
        P->ActivityFailure.Reset();
    }
    H->MovementOwner.Empty();Session.PlayerPosition=OutdoorPosition;PC->UnPossess();Pawn->Destroy();PC->Destroy();
    if(FParse::Param(FCommandLine::Get(),TEXT("WaypointRender")))
    {
        auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
        if(TestTrue(TEXT("Waypoint uses retail DAT geometry"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))
        {
            auto Waypoint=H->Find(TEXT("waypoint"));Waypoint->Enabled=true;Waypoint->Profile=MakeShared<FJsonObject>();
            Waypoint->Profile->SetNumberField(TEXT("east"),-99.);Waypoint->Profile->SetNumberField(TEXT("north"),-48.5);
            for(int Scene=0;Scene<5;++Scene)
            {
                const bool Dungeon=Scene==2;Waypoint->Profile->SetBoolField(TEXT("dungeon"),Dungeon);
                Waypoint->Profile->SetBoolField(TEXT("heading_up"),Scene==3);
                Session.PlayerPosition.CellId= Dungeon?0x01430171:0x7D640014;
                Session.PlayerPosition.Location= Dungeon?FVector(49,-75,0):FVector(60,78,12);
                Session.PlayerPosition.RotationW=FMath::Sqrt(.5);Session.PlayerPosition.RotationXYZ=FVector(0,0,-FMath::Sqrt(.5));
                const FVector2D Size=Scene==0?FVector2D(290,190):FVector2D(780,760);
                auto Widget=H->MakeWaypointPanel(Scene!=0,Scene==0);FWidgetRenderer Renderer(false,true);
                auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
                const double Begin=FPlatformTime::Seconds();
                for(int Pass=0;Pass<(Scene==3?4800:Scene==4?2400:Dungeon?600:5);++Pass)
                {
                    Renderer.DrawWidget(Target,Widget,Size,.016f);FlushRenderingCommands();
                    if(Scene>=3&&Pass==2)
                    {
                        TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>)> FindMap=[&](TSharedRef<SWidget> W)->TSharedPtr<SWidget>
                        {if(W->GetTypeAsString()==TEXT("SWaypointMap"))return W;auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto Found=FindMap(Children->GetChildAt(I)))return Found;return nullptr;};
                        if(auto Map=FindMap(Widget))
                        {
                            const auto& G=Map->GetCachedGeometry();const FVector2D At=G.LocalToAbsolute(G.GetLocalSize()*.5f);
                            const float Wheel=FMath::Loge(Scene==3?16.f:48.f)/FMath::Loge(1.25f);
                            TestTrue(TEXT("Map accepts zoom while rotated"),Map->OnMouseWheel(G,FPointerEvent(0,At,At,{},EKeys::Invalid,Wheel,FModifierKeysState())).IsEventHandled());
                        }
                        else AddError(TEXT("Map widget not found for zoom fixture"));
                    }
                    if(Scene>=3)FPlatformProcess::Sleep(.005f);
                }
                AddInfo(FString::Printf(TEXT("Waypoint %s render/load fixture: %.2f ms"),Dungeon?TEXT("dungeon"):TEXT("world/arrow"),(FPlatformTime::Seconds()-Begin)*1000));
                TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
                TestEqual(TEXT("DAT navigation view renders"),Pixels.Num(),int(Size.X*Size.Y));
                TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(int(Size.X),int(Size.Y),Pixels,Png);
                FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("Automation")/FString::Printf(TEXT("Waypoint-Live-%d.png"),Scene)));Target->ReleaseResource();
            }
        }
    }
    return true;
}
#endif
