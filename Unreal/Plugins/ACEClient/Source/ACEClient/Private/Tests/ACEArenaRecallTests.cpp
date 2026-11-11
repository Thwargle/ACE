#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEChatEntry.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEArenaRecallTest, "ACE.RetailParity.ArenaRecallCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEArenaRecallTest::RunTest(const FString&)
{
    auto* Sockets = ISocketSubsystem::Get();
    auto* Receiver = FUdpSocketBuilder(TEXT("Arena recall receiver")).AsNonBlocking().BoundToPort(0).Build();
    ON_SCOPE_EXIT { if (Receiver) { Receiver->Close(); Sockets->DestroySocket(Receiver); } };
    if (!TestNotNull(TEXT("Loopback receiver"), Receiver)) return false;
    auto* GI = NewObject<UGameInstance>();
    auto* Client = NewObject<UACEClientSubsystem>(GI);
    Client->Session = MakeShared<FACESession>();
    auto& S = *Client->Session;
    S.SocketC2S = FUdpSocketBuilder(TEXT("Arena recall sender")).AsNonBlocking().BoundToPort(0).Build();
    if (!TestNotNull(TEXT("Loopback sender"), S.SocketC2S)) return false;
    S.ServerC2SAddr = Sockets->CreateInternetAddr();
    S.ServerC2SAddr->SetIp(0x7f000001); S.ServerC2SAddr->SetPort(Receiver->GetPortNo());
    S.IssacClient = MakeUnique<FACEIsaac>(123u);
    S.State = EACESessionState::InWorld; S.PlayerGuid = 1;
    ON_SCOPE_EXIT { S.State = EACESessionState::Disconnected; Client->Session.Reset(); };
    auto* Binder = NewObject<UACEUIGameplayBinder>(); Binder->Client = Client;
    auto* Entry = NewObject<UACEChatEntry>(); Binder->ChatEntry = Entry;
    Entry->SetChatText(TEXT("unfinished message"));
    auto CheckPacket = [&](uint32 ExpectedAction)
    {
        if (!TestEqual(TEXT("Exactly one reliable recall action"), S.CachedC2SPackets.Num(), 1)) return;
        FACEBinaryReader R(S.CachedC2SPackets.CreateConstIterator().Value().Payload);
        R.Skip(14);
        TestEqual(TEXT("Retail SendToWeenie queue"), R.ReadUInt16(), ACEQueue::WeenieQueue);
        TestEqual(TEXT("Game action envelope"), R.ReadUInt32(), ACEOpcode::GameAction); R.ReadUInt32();
        TestEqual(TEXT("Retail arena opcode, not Talk"), R.ReadUInt32(), ExpectedAction);
        TestEqual(TEXT("Recall has no arguments or chat payload"), R.Remaining(), 0);
    };
    for (const FString Name : {TEXT("pkarena"), TEXT("pka"), TEXT("pklarena"), TEXT("pla")})
    {
        const bool Lite = Name == TEXT("pklarena") || Name == TEXT("pla");
        const int32 Flag = Lite ? ACEObjectDescFlag::PkLiteStatus : ACEObjectDescFlag::PlayerKiller;
        FACEWorldObject Self; Self.Guid = 1; Self.bIsPlayer = true; Self.bIsSelf = true;
        Self.ObjectDescriptionFlags = Flag; S.WorldObjects.Add(1, Self);
        // Independent wire values from CM_Character::Event_TeleToPK(L)Arena.
        const uint32 Action = Lite ? 0x26u : 0x27u;
        for (const FString Prefix : {TEXT("/"), TEXT("@")})
        {
            S.CachedC2SPackets.Reset();
            TestTrue(TEXT("Typed arena command handled"), Binder->TryDispatchChatCommand(Prefix + Name));
            CheckPacket(Action);
            S.CachedC2SPackets.Reset();
            TestTrue(TEXT("Case and whitespace accepted"), Binder->TryDispatchChatCommand(TEXT("  ") + Prefix + Name.ToUpper() + TEXT("  ")));
            CheckPacket(Action);
            S.CachedC2SPackets.Reset();
            TestTrue(TEXT("Action Deck uses the same arena command"), Binder->DispatchActionDeckCommand(Prefix + Name));
            CheckPacket(Action);
            TestEqual(TEXT("Action Deck preserves unsent chat"), Entry->GetText().ToString(), FString(TEXT("unfinished message")));
            S.CachedC2SPackets.Reset();
            const FString TypedCommand = Prefix + Name;
            TestTrue(TEXT("Submitting a chat line dispatches the arena action"), Binder->TrySendChatFromEntry(&TypedCommand));
            CheckPacket(Action);
            Entry->SetChatText(TEXT("unfinished message"));
            S.CachedC2SPackets.Reset();
            Binder->TryDispatchChatCommand(Prefix + Name + TEXT(" extra"));
            TestTrue(TEXT("Arguments show usage without recalling or sending Talk"), S.CachedC2SPackets.IsEmpty());
            Binder->TryDispatchChatCommand(Prefix + TEXT("help ") + Name);
            TestTrue(TEXT("Arena help stays local"), S.CachedC2SPackets.IsEmpty());
        }
        for (int32 Status : {0, ACEObjectDescFlag::FreePkStatus,
            Lite ? ACEObjectDescFlag::PlayerKiller : ACEObjectDescFlag::PkLiteStatus})
        {
            S.WorldObjects[1].ObjectDescriptionFlags = Status;
            S.CachedC2SPackets.Reset(); Binder->TryDispatchChatCommand(TEXT("/") + Name);
            TestTrue(TEXT("Wrong PvP status cannot request an arena recall"), S.CachedC2SPackets.IsEmpty());
        }
        S.WorldObjects.Remove(1); S.CachedC2SPackets.Reset();
        Binder->TryDispatchChatCommand(TEXT("/") + Name); CheckPacket(Action);
        S.State = EACESessionState::Disconnected; S.CachedC2SPackets.Reset();
        Binder->TryDispatchChatCommand(TEXT("/") + Name);
        TestTrue(TEXT("No recall outside the world"), S.CachedC2SPackets.IsEmpty());
        S.State = EACESessionState::InWorld;
    }
    return !HasAnyErrors();
}
#endif
