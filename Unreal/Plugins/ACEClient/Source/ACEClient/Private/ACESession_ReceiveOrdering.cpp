#include "ACESession.h"

// Retail UIQueueManager / WOrderHdr / TSRecv order F7B0 blobs per owning
// weenie. Packet sequence numbers only recover transport loss: a fragmented
// earlier event can finish after a later complete event even on ordered UDP.
void FACESession::ReceiveGameMessage(const TArray<uint8>& Message)
{
	if (Message.Num() < 4) return;
	FACEBinaryReader Reader(Message);
	if (Reader.ReadUInt32() != ACEOpcode::GameEvent)
	{
		if (AcceptPropertyStamp(Message)) HandleGameMessage(Message);
		return;
	}
	if (!Reader.CanRead(12)) return;
	const int32 Owner = Reader.ReadInt32();
	const uint32 Stamp = Reader.ReadUInt32();
	if (!Owner) { HandleGameMessage(Message); return; } // WOrderHdr iid=0 is unordered.
	auto& Stream = OrderedGameEvents.FindOrAdd(Owner);
	if ((Stream.bReceivedFirst && Stamp <= Stream.LastStamp) || Stream.Pending.Contains(Stamp)) return;
	// Preserve reliable events until recovery. Do not silently skip inventory
	// transactions if recovery fails or a peer exhausts the receive budget.
	if (PendingGameEventCount >= 8192 || PendingGameEventBytes + Message.Num() > MaxPartialMessageBytes)
	{
		ConnectionError = TEXT("Too many incomplete ordered game events. Please reconnect to the server.");
		Log(ConnectionError); SetState(EACESessionState::Failed); return;
	}
	Stream.Pending.Add(Stamp, {Message, FPlatformTime::Seconds()});
	++PendingGameEventCount; PendingGameEventBytes += Message.Num();
	QueueReadyGameEvents(Owner);
}

void FACESession::QueueReadyGameEvents(int32 Owner)
{
	// UIQueueManager keeps ordered blobs until their owning weenie exists.
	if (!WorldObjects.Contains(Owner)) return;
	auto* Stream = OrderedGameEvents.Find(Owner);
	if (!Stream || Stream->bReadyQueued) return;
	if ((!Stream->bReceivedFirst && Stream->Pending.Contains(0)) || Stream->Pending.Contains(Stream->LastStamp + 1))
	{
		Stream->bReadyQueued = true;
		ReadyGameEventOwners.Enqueue(Owner);
	}
}

void FACESession::DispatchReadyGameEvent()
{
	int32 Owner;
	if (!ReadyGameEventOwners.Dequeue(Owner)) return;
	auto* Stream = OrderedGameEvents.Find(Owner);
	if (!Stream) return;
	Stream->bReadyQueued = false;
	const uint32 Next = !Stream->bReceivedFirst && Stream->Pending.Contains(0) ? 0 : Stream->LastStamp + 1;
	FOrderedGameEvents::FEvent Event;
	if (!Stream->Pending.RemoveAndCopyValue(Next, Event)) return;
	Stream->LastStamp = Next; Stream->bReceivedFirst = true;
	--PendingGameEventCount; PendingGameEventBytes -= Event.Message.Num();
	QueueReadyGameEvents(Owner);
	// The callback may clear world/session state. Never retain Stream across it.
	HandleGameMessage(Event.Message);
}

bool FACESession::AcceptPropertyStamp(const TArray<uint8>& Message)
{
	FACEBinaryReader Reader(Message);
	const uint32 Opcode = Reader.ReadUInt32();
	uint32 Family = Opcode;
	bool Public = false, String = false;
	int32 ValueBytes = 4;
	switch (Opcode)
	{
	case ACEOpcode::PublicRemovePropertyInt: Public = true; [[fallthrough]];
	case ACEOpcode::PrivateRemovePropertyInt: Family = ACEOpcode::PrivateUpdatePropertyInt; ValueBytes = 0; break;
	case ACEOpcode::PublicRemovePropertyInt64: Public = true; [[fallthrough]];
	case ACEOpcode::PrivateRemovePropertyInt64: Family = ACEOpcode::PrivateUpdatePropertyInt64; ValueBytes = 0; break;
	case ACEOpcode::PublicRemovePropertyBool: Public = true; [[fallthrough]];
	case ACEOpcode::PrivateRemovePropertyBool: Family = ACEOpcode::PrivateUpdatePropertyBool; ValueBytes = 0; break;
	case ACEOpcode::PublicRemovePropertyFloat: Public = true; [[fallthrough]];
	case ACEOpcode::PrivateRemovePropertyFloat: Family = ACEOpcode::PrivateUpdatePropertyFloat; ValueBytes = 0; break;
	case ACEOpcode::PublicRemovePropertyString: Public = true; [[fallthrough]];
	case ACEOpcode::PrivateRemovePropertyString: Family = ACEOpcode::PrivateUpdatePropertyString; ValueBytes = 0; break;
	case ACEOpcode::SetStackSize: Public = true; Family = ACEOpcode::PrivateUpdatePropertyInt; break;
	case ACEOpcode::PublicUpdatePropertyInt: Public = true; Family = ACEOpcode::PrivateUpdatePropertyInt; break;
	case ACEOpcode::PublicUpdatePropertyBool: Public = true; Family = ACEOpcode::PrivateUpdatePropertyBool; break;
	case ACEOpcode::PublicUpdatePropertyDataID: Public = true; Family = ACEOpcode::PrivateUpdatePropertyDataID; break;
	case ACEOpcode::PublicUpdateInstanceId: Public = true; Family = ACEOpcode::PrivateUpdateInstanceId; break;
	case ACEOpcode::PrivateUpdateInstanceId: break;
	case ACEOpcode::PublicUpdatePropertyInt64: Public = true; Family = ACEOpcode::PrivateUpdatePropertyInt64; ValueBytes = 8; break;
	case ACEOpcode::PublicUpdatePropertyFloat: Public = true; Family = ACEOpcode::PrivateUpdatePropertyFloat; ValueBytes = 8; break;
	case ACEOpcode::PublicUpdatePropertyString: Public = String = true; Family = ACEOpcode::PrivateUpdatePropertyString; ValueBytes = 0; break;
	case ACEOpcode::PrivateUpdatePropertyInt:
	case ACEOpcode::PrivateUpdatePropertyBool:
	case ACEOpcode::PrivateUpdatePropertyDataID: break;
	case ACEOpcode::PrivateUpdatePropertyInt64:
	case ACEOpcode::PrivateUpdatePropertyFloat: ValueBytes = 8; break;
	case ACEOpcode::PrivateUpdatePropertyString: String = true; ValueBytes = 0; break;
	case ACEOpcode::PrivateUpdateAttribute: ValueBytes = 12; break;
	case ACEOpcode::PrivateUpdateAttributeLevel: Family = ACEOpcode::PrivateUpdateAttribute; break;
	case ACEOpcode::PrivateUpdateSkill: ValueBytes = 28; break;
	case ACEOpcode::PrivateUpdateSkillLevel:
	case ACEOpcode::PrivateUpdateSkillAC: Family = ACEOpcode::PrivateUpdateSkill; break;
	case ACEOpcode::PrivateUpdateVital: ValueBytes = 16; break;
	case ACEOpcode::PrivateUpdateAttribute2ndLevel: Family = ACEOpcode::PrivateUpdateVital; break;
	default: return true;
	}
	if (!Reader.CanRead(5 + (Public ? 4 : 0) + ValueBytes)) return false;
	const uint8 Stamp = Reader.ReadUInt8();
	int32 Owner = PlayerGuid;
	uint32 Property;
	if (String)
	{
		if (!ReadStringPropertyHeader(Reader, Public, Owner, Property)) return false;
	}
	else
	{
		if (Public) Owner = Reader.ReadInt32();
		Property = Opcode == ACEOpcode::SetStackSize ? 12u : Reader.ReadUInt32();
	}
	// Reject malformed compact stats before consuming their shared timestamp.
	// Full/level/advancement messages for a stat use one retail WTimeStamper key.
	if (Opcode == ACEOpcode::PrivateUpdateAttributeLevel || Opcode == ACEOpcode::PrivateUpdateAttribute2ndLevel)
	{
		if (Property < 1 || Property > 6 || Reader.ReadUInt32() > MAX_int32) return false;
	}
	else if (Opcode == ACEOpcode::PrivateUpdateSkillLevel || Opcode == ACEOpcode::PrivateUpdateSkillAC)
	{
		if (!Property || Property > MAX_int32
			|| Reader.ReadUInt32() > (Opcode == ACEOpcode::PrivateUpdateSkillAC ? 3u : uint32(MAX_int32))) return false;
	}
	// Inventory InstanceID updates can establish a weenie before its physics
	// CreateObject arrives. Keep their stamps too; the handlers build placeholders.
	if (!Owner) return false;
	if (!PropertyStamps.Contains(Owner) && PropertyStamps.Num() >= 32768)
	{
		ConnectionError = TEXT("Too many pending object property updates. Please reconnect to the server.");
		Log(ConnectionError); SetState(EACESessionState::Failed); return false;
	}
	auto& Stamps = PropertyStamps.FindOrAdd(Owner);
	const uint64 Key = (uint64(Family) << 32) | Property;
	if (const uint8* Previous = Stamps.Find(Key))
	{
		// WTimeStamper::UpdateTS accepts equal stamps and wraps at 256. The
		// half-range tie follows retail's explicit absolute-difference rule.
		const bool Older = FMath::Abs(int32(Stamp) - int32(*Previous)) > 127
			? *Previous < Stamp : Stamp < *Previous;
		if (Older) return false;
	}
	Stamps.Add(Key, Stamp);
	return true;
}
