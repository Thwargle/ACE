#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Protocol/ACEObjectCreateParser.h"
#include "ACESession.h"

namespace
{
// Login ObjectCreate: equipment ownership is separate from the optional physics
// parent that the server supplies for ammunition actually loaded in a launcher.
TArray<uint8> MakeEquipmentCreate(uint32 Slot, uint16 AmmoType, bool bLoaded, bool bPublicChild = false, bool bGameDataOnly = false)
{
	FACEBinaryWriter Wire;
	Wire.WriteUInt32(0x8000059Au);
	if (!bGameDataOnly)
	{
		Wire.WriteUInt8(0x11); // ObjDesc with no appearance overrides
		Wire.WriteUInt8(0); Wire.WriteUInt8(0); Wire.WriteUInt8(0);
		Wire.WriteUInt32(0x000001u | (bLoaded ? 0x020020u : 0u)); // Setup, optional AnimationFrame + Parent
		Wire.WriteUInt32(0); // PhysicsState
		if (bLoaded) Wire.WriteUInt32(1); // RightHandCombat placement
		Wire.WriteUInt32(0x02000BBAu); // Atlatl Dart setup
		if (bLoaded)
		{
			Wire.WriteUInt32(0x50000001u);
			Wire.WriteUInt32(1); // RightHand
		}
		for (int32 I = 0; I < 9; ++I) Wire.WriteUInt16(1);
		Wire.Align();
	}
	Wire.WriteUInt32(0x00003100u | (AmmoType==2 ? 1u : 0u) | (bPublicChild ? 0u : 0x00028000u)); // Optional plural, AmmoType, stack counts, Wielder + wield slot
	Wire.WriteString16L(TEXT("Login equipment"));
	Wire.WriteUInt16(1); // Packed weenie class ID
	Wire.WriteUInt16(1); // Packed icon ID
	Wire.WriteUInt32(static_cast<uint32>(Slot == ACEEquipMask::Held ? ACEItemType::Caster : ACEItemType::MissileWeapon));
	Wire.WriteUInt32(0); // ObjectDescriptionFlags
	Wire.Align();
	if(AmmoType==2) Wire.WriteString16L(TEXT("Login bolts"));
	Wire.WriteUInt16(AmmoType);
	Wire.WriteUInt16(37); Wire.WriteUInt16(2500); // StackSize / MaxStackSize
	if (!bPublicChild)
	{
		Wire.WriteUInt32(0x50000001u);
		Wire.WriteUInt32(Slot);
	}
	Wire.Align();
	return Wire.GetData();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAmmoAttachmentTest, "ACE.RetailParity.LoginAmmoAttachment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEAmmoAttachmentTest::RunTest(const FString&)
{
	// The first ParentEvent stamp belongs to the wielder, never the arrow.
	// Exercise local and remote wielders, and either order of object creation.
	for (bool bLocal : {false, true}) for (int32 Order : {0, 1, 2})
	{
		FACESession Session; Session.PlayerGuid = bLocal ? 100 : 200;
		FACEWorldObject Parent; Parent.Guid=100; Parent.bHasPhysicsTimestamps=true;
		Parent.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=7;
		FACEWorldObject Arrow; Arrow.Guid=300; Arrow.SetupId=0x02000BBA;
		Arrow.WielderId=100; Arrow.CurrentWieldedLocation=ACEEquipMask::MissileAmmo;
		Arrow.bHasPhysicsTimestamps=true; Arrow.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=2;
		Arrow.PhysicsTimestamps[ACEPhysicsTimeStamp::Position]=4;
		auto SendParent=[&](uint16 Instance,uint16 Position)
		{
			FACEBinaryWriter W; W.WriteUInt32(100);W.WriteUInt32(300);W.WriteUInt32(1);W.WriteUInt32(1);
			W.WriteUInt16(Instance);W.WriteUInt16(Position);
			FACEBinaryReader R(W.GetData());Session.HandleParentEvent(R);
		};
		if(Order!=1)Session.WorldObjects.Add(100,Parent);
		if(Order!=2)Session.WorldObjects.Add(300,Arrow);
		SendParent(7,5);
		if(Order==1){Session.WorldObjects.Add(100,Parent);Session.ReplayObjectPhysicsEvents(100);}
		if(Order==2){Session.WorldObjects.Add(300,Arrow);Session.ReplayObjectPhysicsEvents(300);}
		TestEqual(TEXT("Reload attaches an arrow with a different incarnation from its wielder"),Session.WorldObjects[300].ParentGuid,100);
		TestEqual(TEXT("Reload retains the combat placement"),Session.WorldObjects[300].PlacementId,1);
		TestEqual(TEXT("Deferred attachment drains after both objects exist"),Session.PendingObjectPhysicsBytes,0);
		Session.WorldObjects[300].ParentGuid=0;
		SendParent(6,99);
		TestEqual(TEXT("Previous player login cannot reattach an arrow"),Session.WorldObjects[300].ParentGuid,0);
		SendParent(7,4);
		TestEqual(TEXT("Old child position sequence cannot reattach an arrow"),Session.WorldObjects[300].ParentGuid,0);
		SendParent(7,6);
		TestEqual(TEXT("Next reload reattaches the same equipped stack"),Session.WorldObjects[300].ParentGuid,100);
	}
	for (uint16 AmmoType : {1, 2, 4}) // Arrow, bolt, atlatl dart
	{
		for (bool bGameDataOnly : {false, true})
		{
			const auto Bytes = MakeEquipmentCreate(static_cast<uint32>(ACEEquipMask::MissileAmmo), AmmoType, false, false, bGameDataOnly);
			FACEBinaryReader Reader(Bytes);
			FACEDecodedObject Item;
			if (!TestTrue(TEXT("Login ammunition description parses"), bGameDataOnly
				? FACEObjectCreateParser::ParseGameDataOnly(Reader, Item) : FACEObjectCreateParser::Parse(Reader, Item))) return false;
			TestEqual(TEXT("Equipped ammunition does not create a shoulder/quiver mesh"), Item.ParentLocation, 0);
			TestEqual(TEXT("Unloaded stack has no held placement"), Item.PlacementId, 0);
			TestEqual(TEXT("Ammunition stays owned by the player"), Item.WielderId, 0x50000001);
			TestEqual(TEXT("Ammunition stays in its equipment slot"), Item.CurrentWieldedLocation, static_cast<uint32>(ACEEquipMask::MissileAmmo));
			TestEqual(TEXT("Equipped stack count is preserved"), Item.StackSize, 37);
			TestEqual(TEXT("Optional server-authored plural survives both description formats"),Item.PluralName,
				AmmoType==2 ? FString(TEXT("Login bolts")) : FString());
			TestEqual(TEXT("Ammunition kind is preserved"), Item.AmmoType, static_cast<int32>(AmmoType));
		}
		for (bool bPublicChild : {false, true})
		{
			const auto Bytes = MakeEquipmentCreate(static_cast<uint32>(ACEEquipMask::MissileAmmo), AmmoType, true, bPublicChild);
			FACEBinaryReader Reader(Bytes);
			FACEDecodedObject Item;
			if (!TestTrue(TEXT("Loaded ammunition parses for local and remote players"), FACEObjectCreateParser::Parse(Reader, Item))) return false;
			TestEqual(TEXT("Server-provided launcher owner is retained"), Item.ParentGuid, 0x50000001);
			TestEqual(TEXT("Loaded ammunition keeps its server-provided hand attachment"), Item.ParentLocation, 1);
			TestEqual(TEXT("Loaded ammunition keeps its combat placement"), Item.PlacementId, 1);
		}
	}
	// Fallbacks for actual held equipment must survive the ammunition fix.
	for (const auto SlotAndHand : {TPair<uint32, int32>(static_cast<uint32>(ACEEquipMask::Held), 1),
		TPair<uint32, int32>(static_cast<uint32>(ACEEquipMask::MissileWeapon), 2)})
	{
		const auto Bytes = MakeEquipmentCreate(SlotAndHand.Key, 0, false);
		FACEBinaryReader Reader(Bytes);
		FACEDecodedObject Item;
		if (!TestTrue(TEXT("Held equipment description parses"), FACEObjectCreateParser::Parse(Reader, Item))) return false;
		TestEqual(TEXT("Wand/launcher fallback attachment is retained"), Item.ParentLocation, SlotAndHand.Value);
	}
	return true;
}
#endif
