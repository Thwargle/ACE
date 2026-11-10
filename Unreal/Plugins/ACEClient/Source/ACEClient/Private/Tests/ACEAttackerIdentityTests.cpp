#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ACESession.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEInputBindings.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACERadarVisuals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "Components/CapsuleComponent.h"

namespace
{
TArray<uint8> AttackerIid(bool Public, uint8 Stamp, int32 Owner, uint32 Property, int32 Value)
{
	FACEBinaryWriter W;
	W.WriteUInt32(Public ? 0x2DA : 0x2D9); W.WriteUInt8(Stamp);
	if (Public) W.WriteInt32(Owner);
	W.WriteUInt32(Property); W.WriteInt32(Value);
	return W.GetData();
}
TArray<uint8> CombatNotice(uint32 Event, const FString& Name)
{
	FACEBinaryWriter W;
	W.WriteUInt32(ACEOpcode::GameEvent); W.WriteUInt32(0); W.WriteUInt32(0); W.WriteUInt32(Event);
	W.WriteString16L(Name);
	if (Event == ACEGameEvent::DefenderNotification || Event == ACEGameEvent::AttackerNotification)
	{
		W.WriteUInt32(4); W.WriteDouble(.1); W.WriteUInt32(10);
		if (Event == ACEGameEvent::DefenderNotification) W.WriteUInt32(1);
		W.WriteUInt32(0); W.WriteUInt64(0);
	}
	return W.GetData();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAttackerIdentityTest, "ACE.Network.AttackerIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FACEAttackerIdentityTest::RunTest(const FString&)
{
	constexpr int32 Player = 0x50000001;
	constexpr int32 First = static_cast<int32>(0x80001001u), Second = static_cast<int32>(0x80001002u);
	FACESession S; S.PlayerGuid = Player; S.State = EACESessionState::InWorld;
	FACEWorldObject Mob; Mob.Guid = First; Mob.Name = TEXT("Drudge");
	S.WorldObjects.Add(First, Mob); Mob.Guid = Second; S.WorldObjects.Add(Second, Mob);
	S.ReceiveGameMessage(AttackerIid(false, 10, 0, 11, Second));
	TestEqual(TEXT("Private CurrentAttacker retains exact unsigned GUID bits"), S.GetLastAttackerGuid(), Second);
	TestEqual(TEXT("Identity update does not invent a recent hit"), S.GetLastAttackerTimeSeconds(), 0.0);
	S.ReceiveGameMessage(CombatNotice(ACEGameEvent::DefenderNotification, TEXT("Drudge")));
	TestEqual(TEXT("Identical names cannot replace authoritative attacker identity"), S.GetLastAttackerGuid(), Second);
	TestTrue(TEXT("Incoming damage refreshes retaliation timer"), S.GetLastAttackerTimeSeconds() > 0.0);
	S.LastAttackerTimeSeconds = -100.0;
	S.ReceiveGameMessage(CombatNotice(ACEGameEvent::EvasionDefenderNotification, TEXT("Drudge")));
	TestTrue(TEXT("Evading the same attacker refreshes timer without another IID update"), S.GetLastAttackerTimeSeconds() > 0.0);
	TestEqual(TEXT("Evade keeps authoritative identity"), S.GetLastAttackerGuid(), Second);
	const double HitTime = S.GetLastAttackerTimeSeconds();
	S.ReceiveGameMessage(CombatNotice(ACEGameEvent::AttackerNotification, TEXT("Another Drudge")));
	S.ReceiveGameMessage(CombatNotice(ACEGameEvent::EvasionAttackerNotification, TEXT("Another Drudge")));
	TestEqual(TEXT("Outgoing damage/misses do not refresh incoming attack timer"), S.GetLastAttackerTimeSeconds(), HitTime);
	S.ReceiveGameMessage(CombatNotice(ACEGameEvent::DefenderNotification, TEXT("Different name")));
	TestEqual(TEXT("Text from another queue never guesses a replacement GUID"), S.GetLastAttackerGuid(), Second);
	S.ReceiveGameMessage(AttackerIid(false, 11, 0, 11, First));
	TestEqual(TEXT("Attacker can change without a damage message, as on a resist"), S.GetLastAttackerGuid(), First);
	S.ReceiveGameMessage(AttackerIid(true, 10, Player, 11, Second));
	TestEqual(TEXT("Older public update shares private ordering"), S.GetLastAttackerGuid(), First);
	S.ReceiveGameMessage(AttackerIid(true, 12, Player, 11, Second));
	S.ReceiveGameMessage(AttackerIid(false, 11, 0, 11, First));
	TestEqual(TEXT("Older private update cannot undo newer public identity"), S.GetLastAttackerGuid(), Second);
	S.ReceiveGameMessage(AttackerIid(true, 20, First, 11, First));
	TestEqual(TEXT("Other creatures' CurrentAttacker cannot replace the player's"), S.GetLastAttackerGuid(), Second);
	S.ReceiveGameMessage(AttackerIid(false, 13, 0, 11, 0));
	TestEqual(TEXT("Server can explicitly clear current attacker"), S.GetLastAttackerGuid(), 0);
	S.ReceiveGameMessage(CombatNotice(ACEGameEvent::DefenderNotification, TEXT("Drudge")));
	TestEqual(TEXT("Damage text alone cannot recreate a cleared identity"), S.GetLastAttackerGuid(), 0);
	S.ResetReceivedMessages();
	S.ReceiveGameMessage(AttackerIid(false, 255, 0, 11, First));
	S.ReceiveGameMessage(AttackerIid(true, 0, Player, 11, Second));
	S.ReceiveGameMessage(AttackerIid(false, 255, 0, 11, First));
	TestEqual(TEXT("IID timestamps wrap without accepting stale private packets"), S.GetLastAttackerGuid(), Second);
	S.ReceiveGameMessage(AttackerIid(false, 0, 0, 11, First));
	TestEqual(TEXT("Equal IID timestamps retain retail acceptance"), S.GetLastAttackerGuid(), First);

	for (bool Public : {false, true})
	{
		const auto Packet = AttackerIid(Public, 90, Player, 11, Second);
		for (int32 Size = 0; Size < Packet.Num(); ++Size)
		{
			S.ResetReceivedMessages();
			S.LastAttackerGuid = First;
			S.ReceiveGameMessage(TArray<uint8>(Packet.GetData(), Size));
			TestEqual(TEXT("Truncated IID retains previous identity"), S.GetLastAttackerGuid(), First);
			TestTrue(TEXT("Truncated IID consumes no timestamp"), S.PropertyStamps.IsEmpty());
		}
		S.ReceiveGameMessage(AttackerIid(Public, 1, Player, 11, Second));
		TestEqual(TEXT("Valid IID works after malformed high-stamp message"), S.GetLastAttackerGuid(), Second);
	}
	for (uint32 Event : {ACEGameEvent::DefenderNotification, ACEGameEvent::EvasionDefenderNotification})
	{
		const auto Packet = CombatNotice(Event, TEXT("Drudge"));
		for (int32 Size = 0; Size < Packet.Num(); ++Size)
		{
			S.LastAttackerTimeSeconds = -100.0;
			S.ReceiveGameMessage(TArray<uint8>(Packet.GetData(), Size));
			TestEqual(TEXT("Truncated hit/evade cannot refresh retaliation"), S.GetLastAttackerTimeSeconds(), -100.0);
		}
	}
	// Private/public use one application path for the existing descriptor IIDs.
	Mob.Guid = Player; S.WorldObjects.Add(Player, Mob);
	S.ReceiveGameMessage(AttackerIid(false, 1, 0, 26, First));
	S.ReceiveGameMessage(AttackerIid(false, 1, 0, 32, Second));
	S.ReceiveGameMessage(AttackerIid(false, 1, 0, 44, First));
	TestEqual(TEXT("Private monarch updates reach player descriptor"), S.WorldObjects[Player].MonarchGuid, First);
	TestEqual(TEXT("Private house-owner updates reach player descriptor"), S.WorldObjects[Player].HouseOwnerId, Second);
	TestEqual(TEXT("Private pet-owner updates reach player descriptor"), S.WorldObjects[Player].PetOwnerId, First);
	// Login's IID table is the initial source of identity; it is not recent combat.
	FACEBinaryWriter Description;
	Description.WriteUInt32(0x40); Description.WriteUInt32(1);
	Description.WriteUInt16(1); Description.WriteUInt16(8); Description.WriteUInt32(11); Description.WriteInt32(Second);
	Description.WriteUInt32(0); Description.WriteUInt32(0);
	FACEBinaryReader DescReader(Description.GetData()); S.HandlePlayerDescription(DescReader);
	TestEqual(TEXT("PlayerDescription initializes current attacker"), S.GetLastAttackerGuid(), Second);
	TestEqual(TEXT("Login identity does not start retaliation timer"), S.GetLastAttackerTimeSeconds(), 0.0);
	S.LastAttackerTimeSeconds = 123.0; S.ClearWorldState();
	TestEqual(TEXT("World exit clears attacker identity"), S.GetLastAttackerGuid(), 0);
	TestEqual(TEXT("World exit clears retaliation timer"), S.GetLastAttackerTimeSeconds(), 0.0);
	S.LastAttackerGuid = Second; S.LastAttackerTimeSeconds = 123.0; S.Disconnect();
	TestEqual(TEXT("Disconnect clears attacker identity"), S.GetLastAttackerGuid(), 0);
	TestEqual(TEXT("Disconnect clears retaliation timer"), S.GetLastAttackerTimeSeconds(), 0.0);

	// Exercise actual keyboard polling and combat entry using the network identity.
	const FString OriginalSettings = GGameUserSettingsIni;
	GGameUserSettingsIni = FPaths::ProjectSavedDir()/TEXT("Automation/AttackerIdentity.ini");
	FConfigFile Config; Config.NoSave = false; Config.bCanSaveAllSections = true;
	GConfig->SetFile(GGameUserSettingsIni, &Config); ACEInputBindings::Reload();
	ON_SCOPE_EXIT { GGameUserSettingsIni = OriginalSettings; ACEInputBindings::Reload(); };
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI);
	Context.OwningGameInstance = GI; Context.SetCurrentWorld(World); GI->Init();
	ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	auto* Client = GI->GetSubsystem<UACEClientSubsystem>(); const auto Session = Client->GetSession();
	auto* PC = World->SpawnActor<AACEPlayerController>(); PC->Client = Client;
	PC->SetPlayer(NewObject<ULocalPlayer>(GEngine)); World->AddController(PC);
	auto* Pawn = World->SpawnActor<APawn>(); auto* Capsule = NewObject<UCapsuleComponent>(Pawn);
	Capsule->InitCapsuleSize(22, 88); Pawn->SetRootComponent(Capsule); Capsule->RegisterComponent();
	Pawn->SetActorEnableCollision(false); PC->Possess(Pawn); PC->PlayerInput = NewObject<UPlayerInput>(PC);
	auto* Binder = NewObject<UACEUIGameplayBinder>(); Binder->Client = Client; Binder->PlayerController = PC;
	PC->DatGameplayBinder = Binder;
	Session->PlayerGuid = Player; Session->State = EACESessionState::InWorld;
	Session->CharacterOptions1 |= 0x2000u; Binder->CombatMode = 2;
	FACEPosition Start; Start.CellId = 0x7D640019; Start.Location = FVector(50, 100, 50);
	Session->PlayerPosition = PC->PredictedPose = Start; PC->bHavePredictedPose = true;
	Pawn->SetActorLocationAndRotation(Start.ToUnrealLocation(PC->WorldScale) + FVector(0, 0, 88), Start.ToUnrealQuat());
	Mob = FACEWorldObject(); Mob.Guid = First; Mob.Name = TEXT("Drudge"); Mob.ItemType = ACEItemType::Creature;
	Mob.ObjectDescriptionFlags = ACEObjectDescFlag::Attackable; Mob.bHasPosition = true;
	Mob.Position = Start; Mob.Position.Location.X += 2; Session->WorldObjects.Add(First, Mob);
	Mob.Guid = Second; Mob.Position.Location.X += 10; Session->WorldObjects.Add(Second, Mob);
	Session->ReceiveGameMessage(AttackerIid(false, 1, 0, 11, Second));
	Session->ReceiveGameMessage(CombatNotice(ACEGameEvent::EvasionDefenderNotification, TEXT("Drudge")));
	Client->SelectObject(0); Binder->TryAutoTargetOnCombatEnter();
	TestEqual(TEXT("Combat entry prefers the exact recent attacker, not nearest namesake"), Client->GetSelectedObject().Guid, Second);
	Session->LastAttackerTimeSeconds = FPlatformTime::Seconds() - 20.0;
	Client->SelectObject(0); Binder->TryAutoTargetOnCombatEnter();
	TestEqual(TEXT("Expired retaliation falls back to nearest target"), Client->GetSelectedObject().Guid, First);
	ACEInputBindings::BeginEdit(); ACEInputBindings::Defaults(); ACEInputBindings::Commit();
	auto Press = [&](FKey Key)
	{
		PC->PlayerInput->FlushPressedKeys();
		PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		// Super::PlayerTick processes the input stack. Processing it here as well
		// would consume the just-pressed edge before gameplay polls the binding.
		PC->PlayerTick(.016f);
		const bool Matched = ACEInputBindings::Pressed(PC, EKeys::Home);
		PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
		PC->PlayerInput->ProcessInputStack({}, .016f, false);
		return Matched;
	};
	TestTrue(TEXT("Home reaches Last Attacker binding"), Press(EKeys::Home));
	TestEqual(TEXT("Home selects current attacker even after retaliation expires"), Client->GetSelectedObject().Guid, Second);
	ACEInputBindings::BeginEdit(); ACEInputBindings::Set(EKeys::Home, 0, FInputChord(EKeys::F11)); ACEInputBindings::Commit();
	Client->SelectObject(First);
	TestFalse(TEXT("Removed physical binding cannot activate Last Attacker"), Press(EKeys::Home));
	TestEqual(TEXT("Rebound Last Attacker no longer uses Home"), Client->GetSelectedObject().Guid, First);
	TestTrue(TEXT("Custom physical key reaches Last Attacker binding"), Press(EKeys::F11));
	TestEqual(TEXT("Rebound key selects server attacker"), Client->GetSelectedObject().Guid, Second);
	for (uint32 Cell : {0x7D640019u, 0x0110028Au})
	{
		Start.CellId = static_cast<int32>(Cell); Session->PlayerPosition = PC->PredictedPose = Start;
		auto& Target = Session->WorldObjects[Second]; Target.Position = Start;
		const float Range = ACERadarVisuals::RangeAc(Cell);
		Client->SelectObject(First); Target.Position.Location.X += Range;
		PC->SelectLastAttacker();
		TestEqual(TEXT("Exact radar boundary is excluded like retail"), Client->GetSelectedObject().Guid, First);
		Target.Position.Location.X -= .1;
		Target.Position.Location.Z += 100;
		PC->SelectLastAttacker();
		TestEqual(TEXT("Last attacker uses planar indoor/outdoor radar range"), Client->GetSelectedObject().Guid, Second);
	}
	Client->SelectObject(First); Session->WorldObjects.Remove(Second); PC->SelectLastAttacker();
	TestEqual(TEXT("Removed attacker cannot create an Unknown selection"), Client->GetSelectedObject().Guid, First);
	Session->ReceiveGameMessage(AttackerIid(false, 2, 0, 11, 0)); PC->SelectLastAttacker();
	TestEqual(TEXT("Cleared attacker leaves current selection intact"), Client->GetSelectedObject().Guid, First);
	Session->State = EACESessionState::Disconnected;
	return true;
}
#endif
