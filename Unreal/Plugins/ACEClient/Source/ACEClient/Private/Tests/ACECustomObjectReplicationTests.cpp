#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "UI/ACEAppraisalUsage.h"

namespace
{
struct FObjectPacket
{
	int32 Guid = 0x70010001;
	uint16 Instance = 3;
	uint16 Sequence = 1;
	uint16 Part = 1;
	int32 Setup = 0x02000001;
	int32 PhysicsState = 0;
	int32 DescriptionFlags = 0;
	int32 Container = 0;
	int32 Placement = 0;
	float Scale = 1.f;
	float X = 10.f;
	bool bPosition = true;
	float Workmanship = -1.f;
};

void WriteAppearance(FACEBinaryWriter& W, uint16 Part)
{
	W.WriteUInt8(0x11); W.WriteUInt8(0); W.WriteUInt8(0); W.WriteUInt8(1);
	W.WriteUInt8(0); W.WriteUInt16(Part); W.Align();
}

TArray<uint8> ObjectPacket(const FObjectPacket& P, uint32 Opcode = ACEOpcode::ObjectCreate)
{
	FACEBinaryWriter W;
	W.WriteUInt32(Opcode); W.WriteUInt32(P.Guid);
	WriteAppearance(W, P.Part);
	W.WriteUInt32(0x020081u | (P.bPosition ? 0x8000u : 0u)); // Setup, Scale, Placement, optional Position
	W.WriteUInt32(P.PhysicsState); W.WriteUInt32(P.Placement);
	if (P.bPosition)
	{
		W.WriteUInt32(0x7D640014u);
		W.WriteFloat(P.X); W.WriteFloat(20.f); W.WriteFloat(30.f);
		W.WriteFloat(1.f); W.WriteFloat(0.f); W.WriteFloat(0.f); W.WriteFloat(0.f);
	}
	W.WriteUInt32(P.Setup); W.WriteFloat(P.Scale);
	for (int32 I = 0; I < ACEPhysicsTimeStamp::Count; ++I)
		W.WriteUInt16(I == ACEPhysicsTimeStamp::Instance ? P.Instance : P.Sequence);
	W.Align();
	W.WriteUInt32((P.Container ? 0x4000u : 0u) | (P.Workmanship>=0 ? 0x01000000u : 0u));
	W.WriteString16L(TEXT("Server-authored object"));
	W.WriteUInt16(1); W.WriteUInt16(1); // WCID and icon have packed known type prefixes.
	W.WriteUInt32(ACEItemType::Misc); W.WriteUInt32(P.DescriptionFlags); W.Align();
	if (P.Container) W.WriteUInt32(P.Container);
	if (P.Workmanship>=0) W.WriteFloat(P.Workmanship);
	W.Align();
	return W.GetData();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECustomObjectReplicationTest, "ACE.RetailParity.CustomObjectReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECustomObjectReplicationTest::RunTest(const FString&)
{
	// ACSmartBox queues F74C for absent objects and newer incarnations, then
	// replays after CreateObject. Exercise actual wire parsing and presentation.
	{
		FACESession MotionSession;
		FObjectPacket Object;
		int32 Motions = 0;
		MotionSession.OnMotionUpdate.AddLambda([&](int32 Guid, const FACEObjectMotionState&)
		{
			TestTrue(TEXT("Deferred motion has a created object before presentation"), MotionSession.WorldObjects.Contains(Guid));
			++Motions;
		});
		auto Motion = [&](uint16 Instance, uint16 Sequence, bool Truncated = false)
		{
			FACEBinaryWriter W; W.WriteUInt32(ACEOpcode::UpdateMotion); W.WriteUInt32(Object.Guid);
			W.WriteUInt16(Instance); W.WriteUInt16(Sequence); W.WriteUInt16(1); W.WriteUInt8(0); W.Align();
			W.WriteUInt8(0); W.WriteUInt8(0); W.WriteUInt16(0x3D);
			if (!Truncated) W.WriteUInt32(0);
			MotionSession.HandleGameMessage(W.GetData());
		};
		Motion(3, 2, true);
		TestEqual(TEXT("Incomplete motion cannot enter the deferred queue"), MotionSession.PendingObjectPhysicsBytes, 0);
		Motion(3, 2);
		TestEqual(TEXT("Motion waits for its object create"), Motions, 0);
		MotionSession.HandleGameMessage(ObjectPacket(Object));
		TestEqual(TEXT("Create replays a newer deferred motion"), Motions, 1);
		TestEqual(TEXT("Motion timestamp survives deferred replay"), MotionSession.WorldObjects[Object.Guid].PhysicsTimestamps[ACEPhysicsTimeStamp::Movement], uint16(2));
		Motion(3, 2); Motion(2, 100);
		TestEqual(TEXT("Duplicate and old-incarnation motions stay rejected"), Motions, 1);
		Motion(4, 5);
		TestEqual(TEXT("Future incarnation cannot animate the existing object"), Motions, 1);
		Object.Instance = 4;
		MotionSession.HandleGameMessage(ObjectPacket(Object));
		TestEqual(TEXT("Recreated incarnation receives its queued motion"), Motions, 2);
		TestEqual(TEXT("Replayed motion releases queued bytes"), MotionSession.PendingObjectPhysicsBytes, 0);
		MotionSession.WorldObjects[Object.Guid].bPhysicsDescriptionPending = true;
		Motion(4, 6);
		TestEqual(TEXT("Inventory-only stub cannot consume motion"), Motions, 2);
		MotionSession.HandleGameMessage(ObjectPacket(Object));
		TestEqual(TEXT("Full physics description releases stub motion"), Motions, 3);
	}
	FACESession Session;
	Session.PlayerGuid = 100;
	FACEWorldObject Wielder; Wielder.Guid=100; Wielder.bHasPhysicsTimestamps=true;
	Wielder.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=3;
	Session.WorldObjects.Add(100,Wielder);
	FObjectPacket P;
	int32 Deleted = 0;
	Session.OnObjectDeleted.AddLambda([&](int32 Guid) { if (Guid == P.Guid) ++Deleted; });
	auto Create = [&](uint32 Opcode = ACEOpcode::ObjectCreate) { Session.HandleGameMessage(ObjectPacket(P, Opcode)); };
	auto Appearance = [&](uint16 Instance, uint16 Sequence, uint16 Part)
	{
		FACEBinaryWriter W; W.WriteUInt32(P.Guid); WriteAppearance(W, Part);
		W.WriteUInt16(Instance); W.WriteUInt16(Sequence);
		FACEBinaryReader R(W.GetData()); Session.HandleObjDescEvent(R);
	};
	auto State = [&](uint16 Instance, uint16 Sequence, int32 Value)
	{
		FACEBinaryWriter W; W.WriteUInt32(P.Guid); W.WriteUInt32(Value);
		W.WriteUInt16(Instance); W.WriteUInt16(Sequence);
		FACEBinaryReader R(W.GetData()); Session.HandleSetState(R);
	};
	auto Delete = [&](uint16 Instance)
	{
		FACEBinaryWriter W; W.WriteUInt32(P.Guid); W.WriteUInt16(Instance); W.Align();
		FACEBinaryReader R(W.GetData()); Session.HandleObjectDelete(R);
	};
	auto Pickup = [&](uint16 Instance, uint16 Sequence)
	{
		FACEBinaryWriter W; W.WriteUInt32(P.Guid); W.WriteUInt16(Instance); W.WriteUInt16(Sequence);
		FACEBinaryReader R(W.GetData()); Session.HandlePickupEvent(R);
	};
	auto Parent = [&](uint16 Instance, uint16 Sequence)
	{
		FACEBinaryWriter W; W.WriteUInt32(100); W.WriteUInt32(P.Guid);
		W.WriteUInt32(1); W.WriteUInt32(1); W.WriteUInt16(Instance); W.WriteUInt16(Sequence);
		FACEBinaryReader R(W.GetData()); Session.HandleParentEvent(R);
	};
	auto ContainStub = [&]()
	{
		FACEBinaryWriter W; W.WriteUInt32(P.Guid); W.WriteUInt32(100); W.WriteUInt32(0); W.WriteUInt32(0);
		FACEBinaryReader R(W.GetData()); Session.HandleInventoryPutObjInContainer(R);
	};
	auto Position = [&]()
	{
		FACEBinaryWriter W; W.WriteUInt32(P.Guid); W.WriteUInt32(0x70); W.WriteUInt32(0x7D640014);
		W.WriteFloat(42.f); W.WriteFloat(20.f); W.WriteFloat(30.f); W.WriteFloat(1.f);
		W.WriteUInt16(P.Instance); W.WriteUInt16(8); W.WriteUInt16(1); W.WriteUInt16(0);
		FACEBinaryReader R(W.GetData()); Session.HandleUpdatePosition(R);
	};
	auto Vector = [&]()
	{
		FACEBinaryWriter W; W.WriteUInt32(P.Guid);
		W.WriteFloat(3.f); W.WriteFloat(4.f); W.WriteFloat(5.f);
		W.WriteFloat(0.f); W.WriteFloat(0.f); W.WriteFloat(2.f);
		W.WriteUInt16(P.Instance); W.WriteUInt16(8);
		FACEBinaryReader R(W.GetData()); Session.HandleVectorUpdate(R);
	};
	auto PartId = [&]() -> int32
	{
		const FACEWorldObject* O = Session.WorldObjects.Find(P.Guid);
		return O && O->Appearance.AnimPartChanges.Num() == 1 ? O->Appearance.AnimPartChanges[0].PartId : 0;
	};

	// UIQueue ContainId can precede SmartboxQueue Create, but an explicit world
	// position must supersede old ownership instead of making the object invisible.
	FACEWorldObject Stub; Stub.Guid = P.Guid; Stub.ContainerId = 100;
	Session.WorldObjects.Add(P.Guid, Stub);
	Create();
	if (!TestTrue(TEXT("Custom object wire description parses"), Session.WorldObjects.Contains(P.Guid))) return false;
	TestEqual(TEXT("Positioned object does not inherit an old inventory owner"), Session.WorldObjects[P.Guid].ContainerId, 0);
	TestTrue(TEXT("Server placement is visible after the inventory stub"), Session.WorldObjects[P.Guid].bHasPosition);
	TestEqual(TEXT("Custom server setup is retained"), Session.WorldObjects[P.Guid].SetupId, P.Setup);
	P.Workmanship=7.25f;Create();
	TestEqual(TEXT("Public workmanship preserves fractional salvage without appraisal"),Session.WorldObjects[P.Guid].SalvageWorkmanship,7.25f);
	P.Workmanship=8.5f;Create(ACEOpcode::UpdateObject);
	TestEqual(TEXT("Updated workmanship reaches replicated objects"),Session.WorldObjects[P.Guid].SalvageWorkmanship,8.5f);
	Deleted=0; // The explicit UpdateObject above legitimately replaced its actor.

	Appearance(3, 8, 8); State(3, 8, ACEPhysicsState::Hidden);
	Appearance(2, 99, 9); State(2, 99, 0); Pickup(2, 99); Parent(2, 99); Delete(2);
	TestTrue(TEXT("Old-instance deletion does not remove a replacement"), Session.WorldObjects.Contains(P.Guid));
	TestEqual(TEXT("Old-instance model cannot override newer appearance"), PartId(), 0x01000008);
	TestEqual(TEXT("Old-instance state cannot change visibility"), Session.WorldObjects[P.Guid].PhysicsState, ACEPhysicsState::Hidden);
	TestTrue(TEXT("Old-instance pickup cannot hide a world object"), Session.WorldObjects[P.Guid].bHasPosition);
	TestEqual(TEXT("Old-instance parent cannot attach a world object"), Session.WorldObjects[P.Guid].ParentGuid, 0);
	TestEqual(TEXT("Rejected events never destroy the visual"), Deleted, 0);

	// A repeated description must not reset independent physics event sequences.
	Create();
	TestEqual(TEXT("Repeated create preserves a later model event"), PartId(), 0x01000008);
	TestEqual(TEXT("Repeated create preserves a later visibility event"), Session.WorldObjects[P.Guid].PhysicsState, ACEPhysicsState::Hidden);
	TestEqual(TEXT("Repeated create preserves the model sequence"), Session.WorldObjects[P.Guid].PhysicsTimestamps[ACEPhysicsTimeStamp::ObjDesc], uint16(8));
	Pickup(3, 9);
	Create();
	TestFalse(TEXT("An earlier create cannot resurrect a picked-up world visual"), Session.WorldObjects[P.Guid].bHasPosition);
	TestEqual(TEXT("Pickup advances the position sequence"), Session.WorldObjects[P.Guid].PhysicsTimestamps[ACEPhysicsTimeStamp::Position], uint16(9));
	P.Sequence = 10; P.X = 42.f; Create();
	TestTrue(TEXT("A newer server placement returns the object to the world"), Session.WorldObjects[P.Guid].bHasPosition);
	TestEqual(TEXT("Server placement coordinates survive the packet"), Session.WorldObjects[P.Guid].Position.Location.X, 42.0);

	// Future-epoch events wait for their own object, without poisoning this one's counters.
	Appearance(4, 2, 12); State(4, 2, ACEPhysicsState::NoDraw);
	TestEqual(TEXT("Future appearance leaves current incarnation unchanged"), PartId(), 0x01000001);
	TestEqual(TEXT("Future events do not advance the current instance"), Session.WorldObjects[P.Guid].PhysicsTimestamps[ACEPhysicsTimeStamp::Instance], uint16(3));
	Session.WorldObjects[P.Guid].MaterialType = 65;
	Session.WorldObjects[P.Guid].bDying = true;
	P.Instance = 4; P.Sequence = 1; P.Setup = 0x02000002; P.Scale = 2.5f;
	Create();
	TestEqual(TEXT("New incarnation resets the custom setup"), Session.WorldObjects[P.Guid].SetupId, P.Setup);
	TestEqual(TEXT("New incarnation accepts the server scale"), Session.WorldObjects[P.Guid].Scale, 2.5f);
	TestEqual(TEXT("New incarnation does not inherit omitted item metadata"), Session.WorldObjects[P.Guid].MaterialType, 0);
	TestFalse(TEXT("New incarnation does not inherit a death flag"), Session.WorldObjects[P.Guid].bDying);
	TestEqual(TEXT("Future model event replays after create"), PartId(), 0x0100000C);
	TestEqual(TEXT("Future state event replays after create"), Session.WorldObjects[P.Guid].PhysicsState, ACEPhysicsState::NoDraw);
	TestEqual(TEXT("Replaying deferred packets releases the queued payload bytes"), Session.PendingObjectPhysicsBytes, 0);
	TestEqual(TEXT("Replacing an incarnation releases its old actor"), Deleted, 2); // Pickup plus replacement.
	P.Instance = 3; P.Setup = 0x02000003; Create();
	TestEqual(TEXT("Old create cannot replace the current incarnation"), Session.WorldObjects[P.Guid].SetupId, 0x02000002);

	// F7DB is a force-recreate, even with unchanged timestamp values.
	P.Instance = 4; P.Setup = 0x02000004; P.Part = 4; P.Scale = 0.75f; P.Placement = 7;
	Create(ACEOpcode::UpdateObject);
	TestEqual(TEXT("UpdateObject forces server model replacement"), Session.WorldObjects[P.Guid].SetupId, P.Setup);
	TestEqual(TEXT("UpdateObject replaces the visual description"), PartId(), 0x01000004);
	TestEqual(TEXT("UpdateObject resets old hidden state"), Session.WorldObjects[P.Guid].PhysicsState, 0);
	TestEqual(TEXT("UpdateObject applies the server placement ID"), Session.WorldObjects[P.Guid].PlacementId, 7);
	TestEqual(TEXT("UpdateObject releases old actor state"), Deleted, 3);

	Delete(5);
	TestTrue(TEXT("Future delete waits for its matching incarnation"), Session.WorldObjects.Contains(P.Guid));
	P.Instance = 5; Create();
	TestFalse(TEXT("Queued delete removes its matching incarnation after create"), Session.WorldObjects.Contains(P.Guid));
	Appearance(6, 5, 6);
	TestFalse(TEXT("Appearance before create does not invent an incomplete world object"), Session.WorldObjects.Contains(P.Guid));
	P.Instance = 6; Create();
	TestEqual(TEXT("Appearance received before object creation is retained"), PartId(), 0x01000006);

	// Sequence epochs restart on a fresh world entry, including a reused static GUID.
	Appearance(7, 5, 7);
	Session.ClearWorldState();
	TestTrue(TEXT("Relog drops queued packets from the preceding world"), Session.PendingObjectPhysicsEvents.IsEmpty());
	TestEqual(TEXT("Relog releases deferred packet payload bytes"), Session.PendingObjectPhysicsBytes, 0);
	P.Instance = 1; P.Part = 1; P.Setup = 0x02000005; Create();
	TestEqual(TEXT("Relog accepts reset instance numbers for the same GUID"), Session.WorldObjects[P.Guid].SetupId, P.Setup);
	P.Instance = 65535;
	Session.ClearWorldState(); Create();
	P.Instance = 0; P.Setup = 0x02000006; Create();
	Delete(65535);
	TestTrue(TEXT("Instance wraparound rejects the preceding incarnation's delete"), Session.WorldObjects.Contains(P.Guid));
	TestEqual(TEXT("Instance wraparound accepts the next incarnation"), Session.WorldObjects[P.Guid].SetupId, P.Setup);

	// Preserve the cross-queue salvage/inventory repair for genuinely positionless descriptions.
	Session.ClearWorldState(); Stub.MaterialType = 65; Session.WorldObjects.Add(P.Guid, Stub);
	P.bPosition = false; Create();
	TestEqual(TEXT("Inventory stub ownership still survives a positionless create"), Session.WorldObjects[P.Guid].ContainerId, 100);
	TestEqual(TEXT("Inventory stub material still survives its create"), Session.WorldObjects[P.Guid].MaterialType, 65);
	for (int32 I = 0; I < 70; ++I) Appearance(1, uint16(I + 1), 7);
	TestEqual(TEXT("Deferred event count is bounded for each GUID"), Session.PendingObjectPhysicsEvents[P.Guid].Num(), 64);
	Session.MaintainWorldObjectVisibility(FPlatformTime::Seconds() + 26., [](int32) { return true; });
	TestTrue(TEXT("Visibility maintenance expires abandoned deferred events"), Session.PendingObjectPhysicsEvents.IsEmpty());
	TestEqual(TEXT("Expiry releases all queued payload bytes"), Session.PendingObjectPhysicsBytes, 0);
	for (int32 I = 0; I < 70; ++I)
	{
		FACEBinaryWriter W; W.WriteUInt32(0x71000000u + I); W.WriteUInt32(ACEPhysicsState::Hidden);
		W.WriteUInt16(1); W.WriteUInt16(1); W.Pad(65536 - 12);
		FACEBinaryReader R(W.GetData()); Session.HandleSetState(R);
	}
	TestEqual(TEXT("Deferred payload memory has an aggregate four MiB bound"), Session.PendingObjectPhysicsBytes, 4 * 1024 * 1024);
	TestEqual(TEXT("Exhausted aggregate capacity rejects new GUIDs deterministically"), Session.PendingObjectPhysicsEvents.Num(), 64);
	TestTrue(TEXT("Capacity pressure retains earlier ordered packets"), Session.PendingObjectPhysicsEvents.Contains(0x71000000));
	TestFalse(TEXT("Capacity pressure rejects excess packets"), Session.PendingObjectPhysicsEvents.Contains(0x71000040));
	Session.ClearWorldState();
	TestEqual(TEXT("Teardown frees a capacity-sized queue"), Session.PendingObjectPhysicsBytes, 0);

	// Retail keeps UI-only weenie records separate from complete physics objects.
	// None of the early physics events is a substitute for the first full Create.
	P = FObjectPacket(); Session.PlayerGuid = 100; ContainStub();
	TestTrue(TEXT("ContainId marks a partial inventory record awaiting physics"), Session.WorldObjects[P.Guid].bPhysicsDescriptionPending);
	Wielder.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=P.Instance;
	Session.WorldObjects.Add(100,Wielder);
	Appearance(P.Instance, 8, 8); State(P.Instance, 8, ACEPhysicsState::Hidden); Parent(P.Instance, 8);
	TestEqual(TEXT("Physics events wait while only the inventory record exists"), Session.PendingObjectPhysicsEvents[P.Guid].Num(), 3);
	Create();
	TestFalse(TEXT("Create completes the partial physics description"), Session.WorldObjects[P.Guid].bPhysicsDescriptionPending);
	TestEqual(TEXT("First Create installs setup after early visual/state/parent events"), Session.WorldObjects[P.Guid].SetupId, P.Setup);
	TestEqual(TEXT("Stub appearance survives the complete Create"), PartId(), 0x01000008);
	TestEqual(TEXT("Stub visibility state survives the complete Create"), Session.WorldObjects[P.Guid].PhysicsState, ACEPhysicsState::Hidden);
	TestEqual(TEXT("Stub parent event survives the complete Create"), Session.WorldObjects[P.Guid].ParentGuid, 100);
	TestEqual(TEXT("Replaying stub events releases queued payloads"), Session.PendingObjectPhysicsBytes, 0);
	for (bool bVectorFirst : {false, true})
	{
		Session.ClearWorldState(); Session.PlayerGuid = 100; ContainStub();
		if (bVectorFirst) { Vector(); Position(); }
		else { Position(); Vector(); }
		TestTrue(TEXT("Position/vector updates cannot promote a UI stub into a complete physics object"), Session.WorldObjects[P.Guid].bPhysicsDescriptionPending);
		TestFalse(TEXT("Deferred updates do not poison a stub's instance/category counters"), Session.WorldObjects[P.Guid].bHasPhysicsTimestamps);
		Create();
		TestEqual(TEXT("First Create retains the real setup after position/vector in either order"), Session.WorldObjects[P.Guid].SetupId, P.Setup);
		TestEqual(TEXT("Deferred position is applied after the complete Create"), Session.WorldObjects[P.Guid].Position.Location.X, 42.0);
		TestEqual(TEXT("Deferred vector is applied after the complete Create"), Session.WorldObjects[P.Guid].Velocity, FVector(3., 4., 5.));
		TestEqual(TEXT("Deferred angular velocity is applied after the complete Create"), Session.WorldObjects[P.Guid].Omega, FVector(0., 0., 2.));
		TestEqual(TEXT("Position/vector replay preserves their independent sequence"), Session.WorldObjects[P.Guid].PhysicsTimestamps[ACEPhysicsTimeStamp::Vector], uint16(8));
		TestEqual(TEXT("Position/vector replay releases its queued payloads"), Session.PendingObjectPhysicsBytes, 0);
	}
	Session.ClearWorldState(); Session.PlayerGuid = 100;
	P.Container = 100; P.bPosition = false; Create();
	Delete(P.Instance);
	TestTrue(TEXT("Deleting owned physics retains its inventory snapshot"), Session.WorldObjects.Contains(P.Guid));
	TestTrue(TEXT("Retained inventory snapshot awaits a fresh physics description"), Session.WorldObjects[P.Guid].bPhysicsDescriptionPending);
	TestFalse(TEXT("Deleted physics counters cannot filter a later full create"), Session.WorldObjects[P.Guid].bHasPhysicsTimestamps);
	TestEqual(TEXT("Deleting owned physics retains inventory ownership"), Session.WorldObjects[P.Guid].ContainerId, 100);
	Appearance(P.Instance, 8, 14);
	P.Container = 0; P.bPosition = true; P.Setup = 0x0200000A; P.Scale = 3.f;
	Create();
	TestEqual(TEXT("Retained inventory record does not preserve the deleted physics setup"), Session.WorldObjects[P.Guid].SetupId, P.Setup);
	TestEqual(TEXT("Recreated owned object accepts its full physics scale"), Session.WorldObjects[P.Guid].Scale, 3.f);
	TestEqual(TEXT("Events following owned physics deletion replay after recreation"), PartId(), 0x0100000E);
	TestTrue(TEXT("Recreated owned object enters the server-provided world placement"), Session.WorldObjects[P.Guid].bHasPosition);
	TestEqual(TEXT("Recreated world placement supersedes retained inventory ownership"), Session.WorldObjects[P.Guid].ContainerId, 0);

	// Custom scenery can be physically visible while excluded from the UI.
	Session.ClearWorldState(); P = FObjectPacket();
	P.DescriptionFlags = ACEObjectDescFlag::UiHidden; Create();
	TestTrue(TEXT("Create preserves server UIHidden flag"), Session.WorldObjects[P.Guid].IsUiHidden());
	TestFalse(TEXT("Hidden pedestal is excluded from world hotkey candidates"), Session.WorldObjects[P.Guid].IsSelectableWorldObject());
	int32 QualityNotifications = 0;
	Session.OnObjectCreated.AddLambda([&](const FACEWorldObject& Object)
	{
		if (Object.Guid == P.Guid && Object.bAppearanceOnlyUpdate) ++QualityNotifications;
	});
	auto BoolQuality = [&](uint32 Prop, bool Value, bool Public = true)
	{
		FACEBinaryWriter W;
		W.WriteUInt32(Public ? ACEOpcode::PublicUpdatePropertyBool : ACEOpcode::PrivateUpdatePropertyBool);
		W.WriteUInt8(1); if (Public) W.WriteUInt32(P.Guid);
		W.WriteUInt32(Prop); W.WriteUInt32(Value ? 1 : 0);
		Session.HandleGameMessage(W.GetData());
	};
	BoolQuality(24, false);
	TestTrue(TEXT("Public unhide restores selection without recreating physics"), Session.WorldObjects[P.Guid].IsSelectableWorldObject());
	BoolQuality(24, true);
	TestFalse(TEXT("Public hide removes world selection"), Session.WorldObjects[P.Guid].IsSelectableWorldObject());
	TestEqual(TEXT("UIHidden leaves physics and rendering flags alone"), Session.WorldObjects[P.Guid].PhysicsState, 0);
	TestEqual(TEXT("Live visibility updates notify the presenter"), QualityNotifications, 2);
	BoolQuality(24, true);
	TestEqual(TEXT("Repeated unchanged flags do not rebuild presentation"), QualityNotifications, 2);
	Session.PlayerGuid = P.Guid;
	BoolQuality(24, false, false);
	TestFalse(TEXT("Private boolean update has player-only wire layout"), Session.WorldObjects[P.Guid].IsUiHidden());
	BoolQuality(3, false);
	TestTrue(TEXT("Unlock updates the retail openable flag"), Session.WorldObjects[P.Guid].IsOpenable());
	BoolQuality(3, true);
	TestFalse(TEXT("Lock clears the retail openable flag"), Session.WorldObjects[P.Guid].IsOpenable());
	{
		auto& Object=Session.WorldObjects[P.Guid];
		Object.ObjectDescriptionFlags&=~ACEObjectDescFlag::UiHidden;
		Object.bDying=true;Object.ItemUseable=0;
		BoolQuality(19,false);
		TestTrue(TEXT("An ordinary dead creature is not revived by attackability alone"),Object.bDying);
		Object.ItemUseable=0x20;BoolQuality(19,true);BoolQuality(19,false);
		TestFalse(TEXT("Server transition from defeated creature to usable NPC clears death state"),Object.bDying);
		TestFalse(TEXT("Live attackability flag follows the server"),(Object.ObjectDescriptionFlags&ACEObjectDescFlag::Attackable)!=0);
		TestTrue(TEXT("Reward NPC is selectable without a weenie whitelist"),Object.IsSelectableWorldObject());
	}

	// Unfamiliar weenie IDs still get the same appraisal sections; packet data
	// supplies the values and the public descriptor supplies healer/capacity.
	Session.WorldObjects[P.Guid].ObjectDescriptionFlags |= 0x10000;
	Session.WorldObjects[P.Guid].ItemsCapacity = 72;
	FACEAppraisalInfo Appraisal;
	Session.OnAppraisal.AddLambda([&](const FACEAppraisalInfo& Info) { Appraisal = Info; });
	FACEBinaryWriter App;
	App.WriteUInt32(P.Guid); App.WriteUInt32(1 | 2 | 4); App.WriteUInt32(1);
	App.WriteUInt16(1); App.WriteUInt16(16); App.WriteUInt32(90); App.WriteInt32(55);
	App.WriteUInt16(1); App.WriteUInt16(16); App.WriteUInt32(69); App.WriteUInt32(0);
	App.WriteUInt16(1); App.WriteUInt16(16); App.WriteUInt32(100); App.WriteDouble(1.25);
	FACEBinaryReader AppReader(App.GetData()); Session.HandleIdentifyObjectResponse(AppReader);
	const FString Details = ACEAppraisalFormatting::ItemUsageDetails(Appraisal);
	TestTrue(TEXT("Custom healing kit identifies from server descriptor, not WCID"), Details.Contains(TEXT("Bonus to Healing Skill: 55")));
	TestTrue(TEXT("Server heal-kit modifier is retained"), Details.Contains(TEXT("Restoration Bonus: 125%")));
	TestTrue(TEXT("Public capacity survives an appraisal that omits it"), Details.Contains(TEXT("Can hold up to 72 items.")));
	TestTrue(TEXT("Explicit false sellability is displayed"), Details.Contains(TEXT("This item cannot be sold.")));
	Appraisal = FACEAppraisalInfo(); Appraisal.IntProperties.Add(89, 6); Appraisal.IntProperties.Add(90, -25);
	TestTrue(TEXT("Consumable depletion uses server vital and signed amount"), ACEAppraisalFormatting::ItemUsageDetails(Appraisal).Contains(TEXT("Depletes 25 Mana")));
	Appraisal.BoolProperties.Add(3, true); Appraisal.IntProperties.Add(38, 300); Appraisal.IntProperties.Add(173, 0);
	TestTrue(TEXT("Zero lock success remains meaningful data"), ACEAppraisalFormatting::ItemUsageDetails(Appraisal).Contains(TEXT("impossible to pick (Resistance 300)")));
	Appraisal.BoolProperties.Add(3, false);
	TestTrue(TEXT("Explicit unlocked state is displayed"), ACEAppraisalFormatting::ItemUsageDetails(Appraisal).Contains(TEXT("Unlocked")));
	TestTrue(TEXT("Missing qualities do not invent extra appraisal data"), ACEAppraisalFormatting::ItemUsageDetails(FACEAppraisalInfo()).IsEmpty());
	return true;
}
#endif
