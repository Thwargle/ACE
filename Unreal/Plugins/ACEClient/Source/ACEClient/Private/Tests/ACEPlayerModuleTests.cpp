#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include "ACESpellbookFilters.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPlayerModuleParityTest, "ACE.RetailParity.PlayerModuleOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEPlayerModuleParityTest::RunTest(const FString&)
{
	FACESession Session; Session.PlayerGuid = 100; Session.State = EACESessionState::InWorld;
	// Independent wire fixtures follow retail PlayerModule::Pack/UnPack,
	// including the optional fields before the inventory footer.
	for (uint32 BarFlag : {0u, 4u, 0x10u, 0x400u}) for (bool FiltersPresent : {false, true})
	{
		FACEBinaryWriter W;
		for (int I=0; I<4; ++I) W.WriteUInt32(0); // qualities, type, vectors, health flag
		W.WriteUInt32(BarFlag | 0x1C0u | (FiltersPresent ? 0x20u : 0)); W.WriteUInt32(123);
		const int Bars = BarFlag==4 ? 5 : BarFlag==0x10 ? 7 : BarFlag==0x400 ? 8 : 1;
		for (int Bar=0; Bar<Bars; ++Bar) { W.WriteUInt32(1); W.WriteUInt32(27+Bar); }
		if (FiltersPresent) W.WriteUInt32(0x2024u); // Life, Void, level II
		W.WriteUInt32(456); W.WriteString16L(TEXT("HH:mm"));
		W.WriteUInt32(15); // GenericQualitiesData: int, bool, double, string
		for (uint32 Flag : {1u,2u,4u,8u})
		{
			W.WriteUInt16(1); W.WriteUInt16(16); W.WriteUInt32(1);
			if (Flag==4) W.WriteDouble(.5); else if (Flag==8) W.WriteString16L(TEXT("HH:mm:ss")); else W.WriteUInt32(1);
		}
		W.Align(); W.WriteUInt32(1); W.WriteUInt32(999); W.WriteUInt32(0); W.WriteUInt32(0);
		FACEBinaryReader R(W.GetData()); Session.HandlePlayerDescription(R);
		TestEqual(TEXT("Flagged options do not displace the inventory footer"), Session.ContainerContents.FindRef(100).Num(),1);
		if (Session.ContainerContents.FindRef(100).Num()==1)
			TestEqual(TEXT("Inventory footer retains the server item"),Session.ContainerContents.FindRef(100)[0].ItemGuid,999);
		TestEqual(TEXT("The complete player description is consumed"),R.Remaining(),0);
		TestEqual(TEXT("Options2 follows the flagged filter field"),Session.GetCharacterOptions2(),456u);
		TestEqual(TEXT("Absent filters use retail default"),Session.GetSpellbookFilters(),FiltersPresent ? 0x2024u : 0x3FFFu);
		for (int Bar=0; Bar<8; ++Bar)
			TestEqual(TEXT("All transmitted bars and no stale bars survive login"),Session.GetSpellBar(Bar).Contains(27+Bar),Bar<Bars);
	}
	TestEqual(TEXT("Retail school bit order maps to DAT school identifiers"),ACESpellbookFilters::Schools(0x2024),0x12u);
	TestEqual(TEXT("Retail levels start at bit four"),ACESpellbookFilters::Levels(0x2024),2u);
	for (uint32 Bit=0; Bit<32; ++Bit)
	{
		const uint32 Wire=1u<<Bit;
		TestEqual(TEXT("Editing visible controls preserves other server flags"),
			ACESpellbookFilters::ToWire(ACESpellbookFilters::Schools(Wire),ACESpellbookFilters::Levels(Wire),Wire),Wire);
	}
	auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	FSocket* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Filter receiver"),false);
	auto Address=Sockets->CreateInternetAddr(); bool Valid=false; Address->SetIp(TEXT("127.0.0.1"),Valid); Address->SetPort(0);
	if (!TestTrue(TEXT("Filter loopback receiver binds"),Receiver && Receiver->Bind(*Address))) return false;
	Receiver->GetAddress(*Address);
	Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Filter sender"),false);
	Session.ServerC2SAddr=Address; Session.IssacClient=MakeUnique<FACEIsaac>(123);
	Session.SpellbookFilters=0x3FFFu;
	Session.SendSpellbookFilters(0x2024);
	const auto* Packet=Session.CachedC2SPackets.Find(Session.NextPacketSequence-1);
	if (!TestNotNull(TEXT("Filter edit produces a cached wire packet"),Packet))
	{ Sockets->DestroySocket(Receiver); return false; }
	FACEBinaryReader R(Packet->Payload); R.Skip(16);
	TestEqual(TEXT("Filter edit is an ordinary retail game action"),R.ReadUInt32(),ACEOpcode::GameAction); R.ReadUInt32();
	TestEqual(TEXT("Spellbook uses retail action 0x286"),R.ReadUInt32(),0x286u);
	TestEqual(TEXT("School and level mask sent unchanged"),R.ReadUInt32(),0x2024u);
	const uint32 Before=Session.NextPacketSequence; Session.SendSpellbookFilters(0x2024);
	TestEqual(TEXT("Unchanged filters send no duplicate packet"),Session.NextPacketSequence,Before);
	Session.SendCharacterOptions(123,456);
	const auto* OptionsPacket=Session.CachedC2SPackets.Find(Session.NextPacketSequence-1);
	if (!TestNotNull(TEXT("Options save produces a cached wire packet"),OptionsPacket))
	{ Sockets->DestroySocket(Receiver); return false; }
	FACEBinaryReader Options(OptionsPacket->Payload); Options.Skip(28);
	TestEqual(TEXT("Other option saves explicitly preserve filters"),Options.ReadUInt32(),0x60u);
	Options.ReadUInt32(); Options.ReadUInt32(); TestEqual(TEXT("Saved filter mask is retained"),Options.ReadUInt32(),0x2024u);
	Sockets->DestroySocket(Receiver);
	return !HasAnyErrors();
}
#endif
