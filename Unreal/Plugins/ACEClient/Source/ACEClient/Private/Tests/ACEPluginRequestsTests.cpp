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
#include "Mods/ACEPluginSight.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
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
        TestTrue(TEXT("Completed/rejected cast releases fixed delay after minimum request interval"),P->NextAction<=Sent+.5);
        TestTrue(TEXT("Cast completion clears owner and pending spell"),H->PendingSpellOwner.IsEmpty()&&H->PendingSpell==0);
        P->NextAction=Sent+8;H->ObserveUseDone(0);
        TestEqual(TEXT("Unrelated use completion cannot clear another action throttle"),P->NextAction,Sent+8);
        P->NextAction=0;
    }
    auto Tell=[&](const FString& Text,int32 Sender=0x50000002,int32 Type=ACEChatMessageType::Tell)
    {
        FACEBinaryWriter W;W.WriteString16L(Text);W.WriteString16L(TEXT("Visitor"));W.WriteUInt32(Sender);
        W.WriteUInt32(Session.PlayerGuid);W.WriteUInt32(Type);W.WriteUInt32(0);
        FACEBinaryReader R(W.GetData());Session.HandleTell(R);
    };
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
            H->ExecuteInventory(*P,Intent,TEXT("close_corpse"));
            TestEqual(TEXT("Closing corpse preserves mode"),Session.PlayerVitals.CombatMode,Mode);
        }
        Corpse.ObjectDescriptionFlags=0;Session.WorldObjects.Add(870,Corpse);Session.PlayerVitals.CombatMode=8;
        H->ExecuteInventory(*P,Intent,TEXT("use_world"));
        TestEqual(TEXT("Ordinary world-use stance behavior is unchanged"),Session.PlayerVitals.CombatMode,1);
    }
    // Exercise AC's +Y-forward pose and AC-space turn integration, not a
    // synthetic Unreal X-forward pawn (which masked both steering errors).
    auto* PC=World->SpawnActor<AACEPlayerController>();auto* Pawn=World->SpawnActor<APawn>();
    auto* Root=NewObject<USceneComponent>(Pawn);Pawn->SetRootComponent(Root);Root->RegisterComponent();PC->Possess(Pawn);
    for(float Arrival:{17.5f,70.f})for(float Rate:{30.f,90.f,144.f})for(bool VR:{false,true})for(float CameraYaw:{0.f,90.f,210.f})
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
                float F=0,R=0,T=0;H->ApplyMovement(PC,F,R,T,false,false,VR,Facing);
                const FQuat Turn(FVector::UpVector,-1.5f*1.5f*T/Rate);
                const FQuat Ac=(Turn*Pose.GetAcQuat()).GetNormalized();Pose.RotationW=Ac.W;Pose.RotationXYZ=FVector(Ac.X,Ac.Y,Ac.Z);
                Location+=(Pose.GetAceForwardVector()*F+Pose.GetAceRightVector()*R)*600.f/Rate;
            }
            TestTrue(*FString::Printf(TEXT("%s route reaches each corner within %.1f cm at %.0f FPS, camera %.0f"),VR?TEXT("VR"):TEXT("Desktop"),Arrival,Rate,CameraYaw),FVector::Dist2D(Location,Goal)<Arrival);
        }
    }
    H->MovementOwner=TEXT("ucm");H->MoveExpires=FPlatformTime::Seconds()+20;
    H->LastMovePosition=Session.PlayerPosition.ToUnrealLocation();H->LastProgress=FPlatformTime::Seconds()-6;
    const uint32 BeforeBlocked=H->MovementBlockedSerial;float BlockF=1,BlockR=1,BlockT=1;
    H->ApplyMovement(PC,BlockF,BlockR,BlockT,false,false,false,FVector::ForwardVector);
    TestEqual(TEXT("Blocked movement asks policy to recover"),H->MovementBlockedSerial,BeforeBlocked+1);
    TestTrue(TEXT("Blocked movement clears drive input before recovery"),H->MovementOwner.IsEmpty()&&BlockF==0&&BlockR==0&&BlockT==0);
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
