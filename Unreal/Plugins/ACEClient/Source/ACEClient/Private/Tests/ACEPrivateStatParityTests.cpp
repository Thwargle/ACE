#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/GameInstance.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"

namespace
{
// Literal wire opcodes deliberately independent of the dispatch constants.
TArray<uint8> CompactStat(uint32 Opcode, uint8 Stamp, uint32 Id, uint32 Value)
{
	FACEBinaryWriter W;
	W.WriteUInt32(Opcode); W.WriteUInt8(Stamp); W.WriteUInt32(Id); W.WriteUInt32(Value);
	return W.GetData();
}
TArray<uint8> FullAttribute(uint8 Stamp, uint32 Id, uint32 Ranks, uint32 Start, uint32 Xp)
{
	FACEBinaryWriter W;
	W.WriteUInt32(0x2E3); W.WriteUInt8(Stamp); W.WriteUInt32(Id);
	W.WriteUInt32(Ranks); W.WriteUInt32(Start); W.WriteUInt32(Xp);
	return W.GetData();
}
TArray<uint8> FullVital(uint8 Stamp, uint32 Id, uint32 Ranks, uint32 Start, uint32 Xp, uint32 Current)
{
	FACEBinaryWriter W;
	W.WriteUInt32(0x2E7); W.WriteUInt8(Stamp); W.WriteUInt32(Id);
	W.WriteUInt32(Ranks); W.WriteUInt32(Start); W.WriteUInt32(Xp); W.WriteUInt32(Current);
	return W.GetData();
}
TArray<uint8> FullSkill(uint8 Stamp, uint32 Id, uint16 Ranks, uint32 Sac, uint32 Xp, uint32 Init)
{
	FACEBinaryWriter W;
	W.WriteUInt32(0x2DD); W.WriteUInt8(Stamp); W.WriteUInt32(Id);
	W.WriteUInt16(Ranks); W.WriteUInt16(1); W.WriteUInt32(Sac);
	W.WriteUInt32(Xp); W.WriteUInt32(Init); W.WriteUInt32(0); W.WriteDouble(0);
	return W.GetData();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPrivateStatParityTest, "ACE.Network.PrivateStatParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEPrivateStatParityTest::RunTest(const FString&)
{
	FString Directory = TEXT("C:/Turbine/Asheron's Call");
	FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), Directory);
	auto* GI = NewObject<UGameInstance>();
	auto* Dat = NewObject<UACEDatSubsystem>(GI);
	if (!TestTrue(TEXT("Load retail DATs"), Dat->LoadDatDirectory(Directory))
		|| !TestTrue(TEXT("Load retail XP curves"), Dat->EnsureXpTableLoaded())) return false;
	if (!TestTrue(TEXT("Both skill curves contain tested ranks"), Dat->TrainedSkillXpList.Num() > 100
		&& Dat->SpecializedSkillXpList.Num() > 100)) return false;

	FACESession Session;
	Session.PlayerGuid = 0x50001234;
	Session.StatResolver = [Dat](auto& V, const auto& E) { Dat->RecomputePlayerStats(V, E); };
	auto& V = Session.PlayerVitals;
	int32 Notices = 0;
	FACEPlayerVitals Published;
	Session.OnVitalsUpdated.AddLambda([&](const FACEPlayerVitals& Value) { ++Notices; Published = Value; });
	auto Skill = [&](int32 Id) -> FACESkillInfo&
	{
		return *V.Skills.FindByPredicate([Id](const auto& S) { return S.SkillId == Id; });
	};

	// Compact attribute values replace starting attributes, not purchased ranks.
	for (uint32 Id = 1; Id <= 6; ++Id)
	{
		Session.ReceiveGameMessage(FullAttribute(10, Id, 20, 100, 123456));
		Session.ReceiveGameMessage(CompactStat(0x2E5, 11, Id, 130));
		TestEqual(TEXT("Attribute level retains earned ranks"), V.GetAttributeRanks(Id), 20);
		TestEqual(TEXT("Attribute level retains unsigned XP"), V.GetAttributeXpSpent(Id), 123456);
		TestEqual(TEXT("Attribute base is new start plus existing ranks"), V.GetAttributeBase(Id), 150);
		Session.ReceiveGameMessage(FullAttribute(10, Id, 1, 10, 0));
		TestEqual(TEXT("Older full update cannot undo a compact attribute"), V.GetAttributeBase(Id), 150);
	}
	Session.ReceiveGameMessage(FullSkill(10, 24, 20, 2, 1000, 5));
	TestEqual(TEXT("Run uses updated Quickness"), V.RunSkillCurrent, 175);
	Session.ReceiveGameMessage(CompactStat(0x2E5, 12, 3, 160));
	TestEqual(TEXT("Compact attribute refreshes movement skill"), V.RunSkillCurrent, 205);
	TestEqual(TEXT("Published view has refreshed movement skill"), Published.RunSkillCurrent, 205);
	Session.ReceiveGameMessage(CompactStat(0x2DF, 11, 24, 15));
	TestEqual(TEXT("Compact skill level changes init only"), Skill(24).InitLevel, 15);
	TestEqual(TEXT("Compact skill level preserves ranks"), Skill(24).Ranks, 20);
	TestEqual(TEXT("Compact skill level preserves XP"), Skill(24).XpSpent, 1000);
	TestEqual(TEXT("Compact skill level preserves advancement"), Skill(24).AdvancementClass, 2);
	TestEqual(TEXT("Compact skill level refreshes movement"), V.RunSkillCurrent, 215);
	Session.ReceiveGameMessage(FullSkill(10, 24, 1, 1, 0, 0));
	TestEqual(TEXT("Older full skill cannot overwrite compact value"), V.RunSkillCurrent, 215);
	Session.ReceiveGameMessage(FullSkill(12, 24, 30, 2, 2000, 10));
	Session.ReceiveGameMessage(CompactStat(0x2DF, 11, 24, 1));
	TestEqual(TEXT("Older compact cannot overwrite newer full skill"), Skill(24).InitLevel, 10);
	FACEActiveEnchantment Quickness;
	Quickness.SpellId = 1; Quickness.SpellCategory = 101; Quickness.PowerLevel = 6;
	Quickness.StatModType = 0x8001; Quickness.StatModKey = 3; Quickness.StatModValue = 30;
	Session.ActiveEnchantments.Add(Quickness);
	Session.ReceiveGameMessage(CompactStat(0x2E5, 13, 3, 170));
	TestEqual(TEXT("Compact attribute retains active enchantment math"), V.RunSkillCurrent, 260);
	TestEqual(TEXT("Enchantment does not overwrite compact raw attribute"), V.GetAttributeBase(3), 190);
	Session.ActiveEnchantments.Reset();
	Session.NotifyVitalsChanged();
	TestEqual(TEXT("Buff expiry retains the new compact starting value"), V.RunSkillCurrent, 230);

	// Full/compact max-vital updates share a family; current IDs are separate.
	for (uint32 Id : {1u, 3u, 5u})
	{
		Session.ReceiveGameMessage(FullVital(10, Id, 25, 10, 12345, 40));
		const int32 OldMax = V.GetVitalBase(Id);
		Session.ReceiveGameMessage(CompactStat(0x2E9, 11, Id, 50));
		TestEqual(TEXT("Odd compact vital ID changes maximum starting value"), V.GetVitalBase(Id), OldMax + 40);
		Session.ReceiveGameMessage(FullVital(10, Id, 0, 0, 0, 0));
		TestEqual(TEXT("Stale full vital cannot undo compact maximum"), V.GetVitalBase(Id), OldMax + 40);
		Session.ReceiveGameMessage(CompactStat(0x2E9, 1, Id + 1, 30));
	}
	TestEqual(TEXT("Maximum updates retain health ranks"), V.HealthRanks, 25);
	TestEqual(TEXT("Maximum updates retain stamina ranks"), V.StaminaRanks, 25);
	TestEqual(TEXT("Maximum updates retain mana ranks"), V.ManaRanks, 25);
	TestEqual(TEXT("Maximum updates retain health XP"), V.HealthXpSpent, 12345);
	TestEqual(TEXT("Maximum updates retain stamina XP"), V.StaminaXpSpent, 12345);
	TestEqual(TEXT("Maximum updates retain mana XP"), V.ManaXpSpent, 12345);
	TestEqual(TEXT("Current health ID has its own timestamp"), V.Health, 30);
	TestEqual(TEXT("Current stamina ID has its own timestamp"), V.Stamina, 30);
	TestEqual(TEXT("Current mana ID has its own timestamp"), V.Mana, 30);

	// Retail SetSkillAdvancementClass recalculates ranks against the new XP curve.
	// Check exact thresholds and adjacent values using the real DAT, not a fake formula.
	for (uint32 Sac : {2u, 3u})
	{
		const auto& Curve = Sac == 3 ? Dat->SpecializedSkillXpList : Dat->TrainedSkillXpList;
		for (int32 Rank : {1, 75, Curve.Num() - 1})
		{
			for (int32 Offset : {-1, 0})
			{
				Session.ResetReceivedMessages();
				const uint32 Xp = Curve[Rank] + Offset;
				Session.ReceiveGameMessage(FullSkill(1, 50, 7, Sac == 3 ? 2 : 3, Xp, 10));
				Session.ReceiveGameMessage(CompactStat(0x2E1, 2, 50, Sac));
				TestEqual(TEXT("Advancement uses the new DAT curve at its threshold"), Skill(50).Ranks, Rank + Offset);
				TestEqual(TEXT("Advancement preserves starting bonus"), Skill(50).InitLevel, 10);
				TestEqual(TEXT("Advancement preserves unsigned spent XP"), uint32(Skill(50).XpSpent), Xp);
				TestFalse(TEXT("DAT calculation completes before publication"), Published.Skills.FindByPredicate(
					[](const auto& S) { return S.SkillId == 50; })->bRecomputeRanksFromXp);
			}
		}
	}
	for (uint32 Sac : {0u, 1u})
	{
		Session.ReceiveGameMessage(CompactStat(0x2E1, 3, 50, Sac));
		TestEqual(TEXT("Inactive/untrained skills have no purchased ranks"), Skill(50).Ranks, 0);
		TestEqual(TEXT("Inactive/untrained transition preserves init"), Skill(50).InitLevel, 10);
	}
	Session.ResetReceivedMessages();
	Session.ReceiveGameMessage(FullSkill(1, 50, 7, 2, MAX_uint32, 10));
	Session.ReceiveGameMessage(CompactStat(0x2E1, 2, 50, 3));
	TestEqual(TEXT("XP above signed 32-bit range caps at the last DAT rank"), Skill(50).Ranks,
		Dat->SpecializedSkillXpList.Num() - 1);
	Session.ReceiveGameMessage(CompactStat(0x2E1, 4, 50, 2));
	Session.ReceiveGameMessage(FullSkill(5, 50, 5231, 3, 123, 42));
	Session.NotifyVitalsChanged();
	TestEqual(TEXT("Explicit custom server ranks are not normalized on refresh"), Skill(50).Ranks, 5231);
	Session.ReceiveGameMessage(CompactStat(0x2DF, 4, 50, 99));
	Session.ReceiveGameMessage(CompactStat(0x2E1, 4, 50, 1));
	TestEqual(TEXT("Skill level and AC share the full update timestamp"), Skill(50).InitLevel, 42);
	TestEqual(TEXT("Stale advancement cannot untrain a skill"), Skill(50).AdvancementClass, 3);

	// New entries and late availability of DAT definitions.
	Session.ReceiveGameMessage(CompactStat(0x2DF, 1, 54, 10));
	TestEqual(TEXT("Compact update creates missing skill with DAT name"), Skill(54).Name, FString(TEXT("Summoning")));
	Session.ReceiveGameMessage(CompactStat(0x2E1, 2, 54, 3));
	TestEqual(TEXT("Newly specialized skill starts with zero XP ranks"), Skill(54).Ranks, 0);
	FACESession Deferred;
	Deferred.PlayerGuid = 0x50005678;
	Deferred.ReceiveGameMessage(FullSkill(1, 39, 1, 2, Dat->SpecializedSkillXpList[75], 5));
	Deferred.ReceiveGameMessage(CompactStat(0x2E1, 2, 39, 3));
	TestTrue(TEXT("SAC calculation waits for DAT resolver"), Deferred.PlayerVitals.Skills[0].bRecomputeRanksFromXp);
	Deferred.StatResolver = Session.StatResolver;
	Deferred.NotifyVitalsChanged();
	TestEqual(TEXT("Deferred SAC calculation uses retained XP"), Deferred.PlayerVitals.Skills[0].Ranks, 75);
	Deferred.StatResolver = nullptr;
	Deferred.ReceiveGameMessage(CompactStat(0x2E1, 3, 39, 2));
	Deferred.ReceiveGameMessage(FullSkill(4, 39, 5231, 3, 123, 5));
	Deferred.StatResolver = Session.StatResolver;
	Deferred.NotifyVitalsChanged();
	TestEqual(TEXT("Full update supersedes a deferred compact recalculation"), Deferred.PlayerVitals.Skills[0].Ranks, 5231);

	// Every compact packet must be complete before modifying values or timestamps.
	for (uint32 Opcode : {0x2DFu, 0x2E1u, 0x2E5u, 0x2E9u})
	{
		const uint32 Id = Opcode < 0x2E5 ? 39 : 1;
		const auto Packet = CompactStat(Opcode, 90, Id, 3);
		for (int32 Size = 0; Size < Packet.Num(); ++Size)
		{
			Session.ResetReceivedMessages();
			const int32 Before = Notices;
			TArray<uint8> Truncated(Packet.GetData(), Size);
			Session.ReceiveGameMessage(Truncated);
			TestEqual(TEXT("Truncated stat publishes no partial update"), Notices, Before);
			TestTrue(TEXT("Truncated stat consumes no timestamp"), Session.PropertyStamps.IsEmpty());
		}
		Session.ReceiveGameMessage(CompactStat(Opcode, 90, Id, MAX_uint32));
		TestTrue(TEXT("Invalid stat value consumes no timestamp"), Session.PropertyStamps.IsEmpty());
		Session.ReceiveGameMessage(CompactStat(Opcode, 90, 0, 3));
		TestTrue(TEXT("Invalid stat ID consumes no timestamp"), Session.PropertyStamps.IsEmpty());
		const int32 Before = Notices;
		Session.ReceiveGameMessage(CompactStat(Opcode, 1, Id, 3));
		TestEqual(TEXT("Lower-stamped valid update still works after invalid input"), Notices, Before + 1);
	}
	Session.ResetReceivedMessages();
	Session.ReceiveGameMessage(FullSkill(255, 39, 20, 2, 1000, 5));
	Session.ReceiveGameMessage(CompactStat(0x2DF, 0, 39, 10));
	Session.ReceiveGameMessage(FullSkill(255, 39, 1, 1, 0, 0));
	TestEqual(TEXT("Compact update wraps byte timestamps"), Skill(39).InitLevel, 10);
	Session.ReceiveGameMessage(CompactStat(0x2DF, 0, 39, 15));
	TestEqual(TEXT("Equal stamps retain retail acceptance behavior"), Skill(39).InitLevel, 15);
	Session.ResetReceivedMessages();
	Session.ReceiveGameMessage(CompactStat(0x2DF, 255, 39, 20));
	TestEqual(TEXT("New session does not inherit old stat ordering"), Skill(39).InitLevel, 20);
	return true;
}
#endif
