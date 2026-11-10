#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Engine/GameInstance.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"

namespace
{
struct FQualityWire
{
	uint32 Update, Remove;
};
constexpr FQualityWire Int{0x2CD, 0x1D1}, Quad{0x2CF, 0x2B8}, Bool{0x2D1, 0x1D3},
	Float{0x2D3, 0x1D5}, String{0x2D5, 0x1D7};
TArray<uint8> QualityMessage(FQualityWire Type, bool Remove, bool Public, uint8 Stamp,
	uint32 Guid, uint32 Key, int64 Value = 1)
{
	FACEBinaryWriter W;
	W.WriteUInt32((Remove ? Type.Remove : Type.Update) + (Public ? 1 : 0));
	W.WriteUInt8(Stamp);
	if (Public) W.WriteUInt32(Guid); // Retail order, including string removals.
	W.WriteUInt32(Key);
	if (!Remove)
	{
		if (Type.Update == String.Update) { W.Align(); W.WriteString16L(Value ? TEXT("Value") : TEXT("")); }
		else if (Type.Update == Float.Update) W.WriteDouble(double(Value));
		else if (Type.Update == Quad.Update) W.WriteUInt64(uint64(Value));
		else W.WriteInt32(int32(Value));
	}
	return W.GetData();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEQualityRemovalTest, "ACE.Network.QualityRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEQualityRemovalTest::RunTest(const FString&)
{
	FACESession Session;
	Session.PlayerGuid = 0x50001234;
	int32 Notices = 0;
	FACEPlayerVitals Published;
	Session.OnVitalsUpdated.AddLambda([&](const FACEPlayerVitals& V) { ++Notices; Published = V; });
	auto Present = [&](FQualityWire Type, uint32 Key)
	{
		const auto& V = Session.PlayerVitals;
		if (Type.Update == Int.Update) return V.StatQualityInts.Contains(Key);
		if (Type.Update == Quad.Update) return V.QualityInt64s.Contains(Key);
		if (Type.Update == Bool.Update) return V.QualityBools.Contains(Key);
		if (Type.Update == Float.Update) return V.QualityDoubles.Contains(Key);
		return V.QualityStrings.Contains(Key);
	};
	// Exercise every wire family independently, with all private/public pairings.
	for (FQualityWire Type : {Int, Quad, Bool, Float, String})
	for (bool UpdatePublic : {false, true})
	for (bool RemovePublic : {false, true})
	{
		const uint32 Key = 9000 + (UpdatePublic ? 2 : 0) + (RemovePublic ? 1 : 0);
		auto Send = [&](bool Remove, bool Public, uint8 Stamp, int64 Value = 1)
		{
			Session.ReceiveGameMessage(QualityMessage(Type, Remove, Public, Stamp, Session.PlayerGuid, Key, Value));
		};
		Send(false, UpdatePublic, 10);
		TestTrue(TEXT("Character quality arrives before player ObjectCreate"), Present(Type, Key));
		Send(true, RemovePublic, 11);
		TestFalse(TEXT("Removal erases the quality, rather than storing zero"), Present(Type, Key));
		const int32 AfterRemove = Notices;
		Send(true, RemovePublic, 11);
		TestEqual(TEXT("Duplicate deletion avoids redundant UI recomputation"), Notices, AfterRemove);
		Send(false, !UpdatePublic, 10);
		TestFalse(TEXT("Stale replacement cannot resurrect a removed quality"), Present(Type, Key));
		Send(false, UpdatePublic, 12, 0);
		TestTrue(TEXT("Explicit zero/false/empty remains a present property"), Present(Type, Key));
		Send(true, RemovePublic, 11);
		TestTrue(TEXT("Stale removal cannot delete a newer value"), Present(Type, Key));
		const auto Complete = QualityMessage(Type, true, RemovePublic, 30, Session.PlayerGuid, Key);
		for (int32 Length = 0; Length < Complete.Num(); ++Length)
		{
			auto Partial = Complete; Partial.SetNum(Length); Session.ReceiveGameMessage(Partial);
		}
		Send(true, RemovePublic, 13);
		TestFalse(TEXT("Truncated removal never advances the shared timestamp"), Present(Type, Key));
		Send(false, UpdatePublic, 14);
		Send(true, RemovePublic, 14);
		TestFalse(TEXT("Retail accepts equal stamps across update and removal"), Present(Type, Key));
	}
	for (FQualityWire Type : {Int, Quad, Bool, Float, String})
	{
		Session.ReceiveGameMessage(QualityMessage(Type, false, false, 254, Session.PlayerGuid, 9999));
		Session.ReceiveGameMessage(QualityMessage(Type, true, true, 255, Session.PlayerGuid, 9999));
		Session.ReceiveGameMessage(QualityMessage(Type, false, true, 0, Session.PlayerGuid, 9999));
		Session.ReceiveGameMessage(QualityMessage(Type, true, false, 255, Session.PlayerGuid, 9999));
		TestTrue(TEXT("Timestamp rollover preserves newer replacement"), Present(Type, 9999));
		Session.ReceiveGameMessage(QualityMessage(Type, true, true, 1, Session.PlayerGuid + 1, 9999));
		TestTrue(TEXT("Another object's deletion cannot erase self qualities"), Present(Type, 9999));
	}
	auto Set = [&](uint32 Key, int64 Value, FQualityWire Type = Int, bool Public = true)
	{
		Session.ReceiveGameMessage(QualityMessage(Type, false, Public, 1, Session.PlayerGuid, Key, Value));
	};
	auto Remove = [&](uint32 Key, FQualityWire Type = Int)
	{
		Session.ReceiveGameMessage(QualityMessage(Type, true, true, 2, Session.PlayerGuid, Key));
	};
	Set(25, 275); Set(24, 32); Set(125, 7200); Set(43, 19); Set(181, 1500); Set(192, 42);
	TestEqual(TEXT("Public level update reaches the character panel"), Published.Level, 275);
	TestEqual(TEXT("Public credits update reaches the character panel"), Published.AvailableSkillCredits, 32);
	TestEqual(TEXT("Live age refreshes its cached character field"), Published.AgeSeconds, 7200);
	TestEqual(TEXT("Live deaths refresh their cached character field"), Published.NumDeaths, 19);
	TestEqual(TEXT("Live fishing skill refreshes its cached character field"), Published.FishingSkill, 42);
	Remove(25); Remove(24); Remove(125); Remove(43); Remove(181); Remove(192);
	TestEqual(TEXT("Deleted character qualities cannot leave cached panel values"),
		Published.Level + Published.AvailableSkillCredits + Published.AgeSeconds + Published.NumDeaths + Published.ChessRank + Published.FishingSkill, 0);
	Set(6, 8000000000000LL, Quad);
	TestEqual(TEXT("Live luminance retains its 64-bit balance"), Published.AvailableLuminance, int64(8000000000000LL));
	Remove(6, Quad);
	TestEqual(TEXT("Deleted luminance clears cached balance"), Published.AvailableLuminance, int64(0));
	TestTrue(TEXT("Deleted balance is authoritatively zero, not pending login"), Published.bHasLuminanceBalance);
	Set(5, 1, String); Remove(5, String);
	TestTrue(TEXT("Removed template cannot linger in the character panel"), Published.TemplateName.IsEmpty());
	Set(68, 0, Bool); Remove(68, Bool);
	TestFalse(TEXT("Removing component exemption restores the absent-property default"), Published.QualityBools.Contains(68));

	// Use the real retail stat resolver to prove removal rolls back bonuses,
	// rather than just hiding the raw property from plugin snapshots.
	auto* GI = NewObject<UGameInstance>();
	auto* Dat = NewObject<UACEDatSubsystem>(GI);
	FString Directory = TEXT("C:/Turbine/Asheron's Call");
	FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), Directory);
	if (!TestTrue(TEXT("Load retail DAT for quality-derived stats"), Dat->LoadDatDirectory(Directory))) return false;
	ON_SCOPE_EXIT { Dat->Deinitialize(); };
	Session.StatResolver = [Dat](auto& V, const auto& E) { Dat->RecomputePlayerStats(V, E); };
	Session.PlayerVitals.Endurance = Session.PlayerVitals.Quickness = 100;
	FACESkillInfo Run; Run.SkillId = 24; Run.AdvancementClass = 2; Run.InitLevel = 10; Run.Ranks = 20;
	Session.PlayerVitals.Skills.Add(Run);
	Session.NotifyVitalsChanged();
	const int32 BaseRun = Published.RunSkillCurrent, BaseHealth = Published.MaxHealth;
	Set(365, 12); Set(379, 40); Set(230, 3); Set(322, 7);
	TestEqual(TEXT("Augmented running uses the raw quality"), Published.RunSkillCurrent, BaseRun + 12);
	TestEqual(TEXT("Equipment health uses the raw quality"), Published.MaxHealth, BaseHealth + 40);
	Remove(365); Remove(379); Remove(230); Remove(322);
	TestEqual(TEXT("Removed augmentation immediately restores run skill"), Published.RunSkillCurrent, BaseRun);
	TestEqual(TEXT("Removed gear health immediately restores maximum"), Published.MaxHealth, BaseHealth);
	TestEqual(TEXT("Removed capacity augmentation clears its mirror"), Published.CarryingCapacityAugs, 0);
	TestEqual(TEXT("Removed aetheria unlock clears its mirror"), Published.AetheriaUnlocked, 0);

	// Quality deletion is not an object update or a request to change stance.
	FACEWorldObject Player; Player.Guid = Session.PlayerGuid;
	Player.Burden = 100; Player.IconId = 0x06001234;
	Session.WorldObjects.Add(Player.Guid, Player);
	Set(19, 1, Bool); // Attackable public descriptor flag.
	const int32 Flags = Session.WorldObjects[Player.Guid].ObjectDescriptionFlags;
	Remove(19, Bool);
	TestEqual(TEXT("Bool removal preserves independently replicated descriptor flags"), Session.WorldObjects[Player.Guid].ObjectDescriptionFlags, Flags);
	Set(40, 8); // Magic.
	const uint32 ModeRevision = Session.CombatModeRevision;
	Remove(40);
	TestEqual(TEXT("Missing combat quality does not force peace"), Session.PlayerVitals.CombatMode, 8);
	TestEqual(TEXT("Missing combat quality does not arbitrate a new stance"), Session.CombatModeRevision, ModeRevision);
	TestEqual(TEXT("Removal preserves public icon"), Session.WorldObjects[Player.Guid].IconId, 0x06001234);
	TestTrue(TEXT("Removal does not manufacture outbound actions"), Session.CachedC2SPackets.IsEmpty());
	{
		FACESession Early; Early.PlayerGuid = Session.PlayerGuid;
		Early.ReceiveGameMessage(QualityMessage(Quad, true, false, 1, Early.PlayerGuid, 6));
		TestTrue(TEXT("Early luminance removal establishes an authoritative zero without a prior value"), Early.PlayerVitals.bHasLuminanceBalance);
	}
	Session.Disconnect();
	Session.PlayerGuid = 0x50001235;
	Session.ReceiveGameMessage(QualityMessage(Int, false, false, 1, Session.PlayerGuid, 9999));
	TestTrue(TEXT("A previous character's removal stamp cannot suppress login values"), Session.PlayerVitals.StatQualityInts.Contains(9999));
	return true;
}
#endif
