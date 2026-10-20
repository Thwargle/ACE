#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAppraisalCommandContextTest, "ACE.Plugins.AppraisalCommandContext",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEAppraisalCommandContextTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI);
    Context.OwningGameInstance = GI; Context.SetCurrentWorld(World); GI->OnWorldChanged(nullptr, World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Client = GI->GetSubsystem<UACEClientSubsystem>();
    auto& Session = *Client->GetSession(); Session.State = EACESessionState::InWorld; Session.PlayerGuid = 1;

    auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Address = Sockets->CreateInternetAddr(); bool Valid = false;
    Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(0);
    auto* Receiver = Sockets->CreateSocket(NAME_DGram, TEXT("Appraisal context receiver"), false);
    if (!TestNotNull(TEXT("Loopback receiver"), Receiver)) return false;
    ON_SCOPE_EXIT { Receiver->Close(); Sockets->DestroySocket(Receiver); };
    if (!TestTrue(TEXT("Loopback bind"), Valid && Receiver->Bind(*Address))) return false;
    Receiver->GetAddress(*Address);
    Session.SocketC2S = Sockets->CreateSocket(NAME_DGram, TEXT("Appraisal context sender"), false);
    if (!TestNotNull(TEXT("Loopback sender"), Session.SocketC2S)) return false;
    ON_SCOPE_EXIT { Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S = nullptr; };
    Session.ServerC2SAddr = Address; Session.IssacClient = MakeUnique<FACEIsaac>(123u);

    constexpr int32 Vendor = 101, Crystal = 102, Mmd = 201, Peas = 202;
    TSet<int32> Present, Failed;
    for (int32 Guid : {Vendor, Crystal, Mmd, Peas})
    {
        FACEWorldObject Object; Object.Guid = Guid; Object.Name = FString::FromInt(Guid);
        Object.ItemType = ACEItemType::Misc; Object.ContainerId = Guid >= Mmd ? 1 : 0;
        Session.WorldObjects.Add(Guid, Object); Present.Add(Guid);
    }

    // Consume the actual encoded outgoing actions using ACE Player::Examine
    // target semantics. No response is delivered until after each command: a
    // slow link must not make a command depend on an appraisal-reply round trip.
    int32 Requested = 0, Current = 0, Health = 0, Mana = 0;
    uint32 PreviousAction = 0;
    struct FCommand { FString Text; int32 Requested, VendorTarget; };
    TArray<FCommand> Commands;
    auto Consume = [&]()
    {
        TArray<uint32> Keys; Session.CachedC2SPackets.GetKeys(Keys); Keys.Sort();
        for (uint32 Key : Keys)
        {
            FACEBinaryReader R(Session.CachedC2SPackets[Key].Payload);
            R.Skip(14);
            TestEqual(TEXT("Appraisals and commands share the reliable ordered queue"), R.ReadUInt16(), ACEQueue::WeenieQueue);
            TestEqual(TEXT("Actual wire message is GameAction"), R.ReadUInt32(), ACEOpcode::GameAction);
            const uint32 Sequence = R.ReadUInt32();
            TestEqual(TEXT("Commands cannot overtake their appraisal restoration"), Sequence, PreviousAction + 1);
            PreviousAction = Sequence;
            const uint32 Action = R.ReadUInt32();
            if (Action == ACEGameAction::IdentifyObject)
            {
                const int32 Target = int32(R.ReadUInt32());
                if (!Target) Requested = Current = 0;
                else if (Present.Contains(Target))
                {
                    Requested = Target;
                    if (!Failed.Contains(Target)) Current = Target;
                }
            }
            else if (Action == ACEGameAction::QueryHealth) Health = int32(R.ReadUInt32());
            else if (Action == ACEGameAction::QueryItemMana) Mana = int32(R.ReadUInt32());
            else if (Action == ACEGameAction::Talk)
            {
                const FString Text = R.ReadString16L();
                if (Text.StartsWith(TEXT("@"))) Commands.Add({Text, Requested, Health ? Health : Mana ? Mana : Current});
            }
        }
        Session.CachedC2SPackets.Reset();
    };
    auto Reply = [&](int32 Guid, bool Success = true)
    {
        FACEBinaryWriter W; W.WriteUInt32(Guid); W.WriteUInt32(0); W.WriteUInt32(Success ? 1 : 0);
        FACEBinaryReader R(W.GetData()); Session.HandleIdentifyObjectResponse(R);
    };
    auto CheckCommand = [&](int32 Expected, const TCHAR* Message)
    {
        Consume();
        if (!TestTrue(TEXT("Server received command"), !Commands.IsEmpty())) return;
        TestEqual(Message, Commands.Last().Requested, Expected);
        TestEqual(TEXT("Successful-appraisal commands use the same intended object"), Commands.Last().VendorTarget, Expected);
    };

    Client->SelectObject(Vendor); Client->SendIdentifyObject(Vendor);
    const uint64 ManualSerial = Client->GetIdentifyRequestSerial();
    for (const TCHAR* Command : {TEXT("@vendordump"), TEXT("@getinfo"), TEXT("@propertydump"), TEXT("@custom_crystal_info extra arguments")})
    {
        Client->RequestBackgroundAppraisal(Mmd); Client->RequestBackgroundAppraisal(Peas);
        Client->SendChatMessage(Command);
        CheckCommand(Vendor, TEXT("Inventory appraisal scans cannot hijack a server command"));
        TestEqual(TEXT("Restoration leaves command arguments intact"), Commands.Last().Text, FString(Command));
    }
    TestEqual(TEXT("Maintenance does not act like another UI inspection"), Client->GetIdentifyRequestSerial(), ManualSerial);
    TestEqual(TEXT("Maintenance retains the inspected object"), Client->GetIdentifyRequestGuid(), Vendor);
    Client->SendChatMessage(TEXT("@getinfo"));
    TestEqual(TEXT("No extra appraisals when no background request intervened"), Session.CachedC2SPackets.Num(), 1);
    CheckCommand(Vendor, TEXT("Repeated commands retain context"));

    Client->RequestBackgroundAppraisal(Peas); Consume();
    Client->SendChatMessage(TEXT("ordinary conversation"));
    TestEqual(TEXT("Ordinary speech adds no appraisal traffic"), Session.CachedC2SPackets.Num(), 1); Consume();
    TestEqual(TEXT("Speech does not restore appraisal context"), Requested, Peas);
    Client->SendIdentifyObject(Crystal); Consume();
    Client->RequestBackgroundAppraisal(Mmd);
    Client->SelectObject(Vendor); // Selection alone is not an appraisal.
    Client->SendChatMessage(TEXT("@getinfo")); CheckCommand(Crystal, TEXT("Commands retain the last explicit appraisal, not the latest selection"));
    TestEqual(TEXT("Restoring context never changes selection"), Client->GetSelectedObject().Guid, Vendor);
    Reply(Peas); Reply(Mmd); Reply(Vendor); Reply(Crystal);
    auto* Host = GI->GetSubsystem<UACEPluginSubsystem>();
    TestTrue(TEXT("Delayed background replies still populate plugin appraisal data"), Host->GetPluginAppraisals().Contains(Mmd) && Host->GetPluginAppraisals().Contains(Peas));
    Client->SendChatMessage(TEXT("@getinfo")); CheckCommand(Crystal, TEXT("Late replies cannot change the manual command target"));

    Client->SendIdentifyObject(Mmd); Client->RequestBackgroundAppraisal(Peas);
    Client->SendChatMessage(TEXT("@getinfo")); CheckCommand(Mmd, TEXT("An intentionally appraised inventory item remains a valid command target"));

    Client->SendIdentifyObject(Crystal); Client->RequestBackgroundAppraisal(Mmd); Consume();
    Failed.Add(Crystal);
    Client->SendChatMessage(TEXT("@getinfo")); Consume();
    TestEqual(TEXT("Failed appraisal keeps requested target for getinfo"), Commands.Last().Requested, Crystal);
    TestEqual(TEXT("Failed restoration cannot leave vendordump pointed at inventory"), Commands.Last().VendorTarget, 0);
    Client->RequestBackgroundAppraisal(Mmd); Client->SendIdentifyObject(Crystal);
    Client->SendChatMessage(TEXT("@vendordump")); Consume();
    TestEqual(TEXT("A failed new manual appraisal also clears successful background context"), Commands.Last().VendorTarget, 0);
    Failed.Reset();

    Client->SendIdentifyObject(Crystal); Client->RequestBackgroundAppraisal(Mmd); Consume();
    Present.Remove(Crystal); Session.WorldObjects.Remove(Crystal);
    Client->SendChatMessage(TEXT("@getinfo")); CheckCommand(0, TEXT("A vanished target cannot fall back to the scanned item"));

    Session.ClearWorldState(); Session.State = EACESessionState::InWorld; Session.PlayerGuid = 1;
    FACEWorldObject Inventory; Inventory.Guid = Mmd; Inventory.ContainerId = 1; Session.WorldObjects.Add(Mmd, Inventory);
    Requested = Current = Health = Mana = 0;
    Client->RequestBackgroundAppraisal(Mmd); Client->SendChatMessage(TEXT("@getinfo"));
    CheckCommand(0, TEXT("Relog never reuses a previous character's manual appraisal"));
    Client->SendChatMessage(TEXT("@vendordump")); CheckCommand(0, TEXT("No manual appraisal means no background inventory fallback"));
    return !HasAnyErrors();
}
#endif
