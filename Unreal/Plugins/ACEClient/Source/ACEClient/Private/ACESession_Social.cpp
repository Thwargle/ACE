#include "ACESession.h"
#include "Protocol/ACEMaterialTypeNames.inl"

namespace
{
	static const TCHAR* SkillNameForSalvage(uint32 SkillId)
	{
		switch (SkillId)
		{
		case 18: return TEXT("Item Tinkering");
		case 28: return TEXT("Weapon Tinkering");
		case 29: return TEXT("Armor Tinkering");
		case 30: return TEXT("Magic Item Tinkering");
		case 40: return TEXT("Salvaging");
		default: return TEXT("tinkering");
		}
	}

	static bool ReadFellowRecord(FACEBinaryReader& Reader, FACEFellowshipMember& Out)
	{
		if (!Reader.CanRead(4 + 8 + 4 + 24 + 4))
		{
			return false;
		}
		Out.Guid = static_cast<int32>(Reader.ReadUInt32());
		Reader.ReadUInt32(); // cpCached
		Reader.ReadUInt32(); // lumCached
		Out.Level = static_cast<int32>(Reader.ReadUInt32());
		Out.HealthMax = static_cast<int32>(Reader.ReadUInt32());
		Out.StaminaMax = static_cast<int32>(Reader.ReadUInt32());
		Out.ManaMax = static_cast<int32>(Reader.ReadUInt32());
		Out.HealthCur = static_cast<int32>(Reader.ReadUInt32());
		Out.StaminaCur = static_cast<int32>(Reader.ReadUInt32());
		Out.ManaCur = static_cast<int32>(Reader.ReadUInt32());
		const uint32 ShareLoot = Reader.ReadUInt32();
		Out.bShareLoot = (ShareLoot != 0);
		Out.Name = Reader.ReadString16L();
		return true;
	}
}

void FACESession::SendFellowshipCreate(const FString& Name, bool bShareXP)
{
	if (State != EACESessionState::InWorld || Name.IsEmpty())
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(Name);
	W.WriteUInt32(bShareXP ? 1u : 0u);
	SendGameAction(ACEGameAction::FellowshipCreate, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendFellowshipQuit(bool bDisband)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(bDisband ? 1u : 0u);
	SendGameAction(ACEGameAction::FellowshipQuit, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendFellowshipDismiss(int32 MemberGuid)
{
	if (State != EACESessionState::InWorld || MemberGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(MemberGuid));
	SendGameAction(ACEGameAction::FellowshipDismiss, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendFellowshipRecruit(int32 TargetPlayerGuid)
{
	if (State != EACESessionState::InWorld || TargetPlayerGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(TargetPlayerGuid));
	SendGameAction(ACEGameAction::FellowshipRecruit, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendFellowshipUpdateRequest(bool bPanelOpen)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteInt32(bPanelOpen ? 1 : 0);
	SendGameAction(ACEGameAction::FellowshipUpdateRequest, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendFellowshipAssignNewLeader(int32 MemberGuid)
{
	if (State != EACESessionState::InWorld || MemberGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(MemberGuid));
	SendGameAction(ACEGameAction::FellowshipAssignNewLeader, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendFellowshipChangeOpenness(bool bOpen)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(bOpen ? 1u : 0u);
	SendGameAction(ACEGameAction::FellowshipChangeOpenness, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendSwearAllegiance(int32 TargetGuid)
{
	if (State != EACESessionState::InWorld || TargetGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(TargetGuid));
	SendGameAction(ACEGameAction::SwearAllegiance, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendBreakAllegiance(int32 TargetGuid)
{
	if (State != EACESessionState::InWorld || TargetGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(TargetGuid));
	SendGameAction(ACEGameAction::BreakAllegiance, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendAllegianceUpdateRequest(bool bPanelOpen)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(bPanelOpen ? 1u : 0u);
	SendGameAction(ACEGameAction::AllegianceUpdateRequest, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendAddFriend(const FString& Name)
{
	if (State != EACESessionState::InWorld || Name.IsEmpty())
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(Name);
	SendGameAction(ACEGameAction::AddFriend, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendRemoveFriend(int32 FriendGuid)
{
	if (State != EACESessionState::InWorld || FriendGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(FriendGuid));
	SendGameAction(ACEGameAction::RemoveFriend, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendCreateTinkeringTool(int32 ToolGuid, const TArray<int32>& ItemGuids)
{
	if (State != EACESessionState::InWorld || ToolGuid == 0 || ItemGuids.Num() == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ToolGuid));
	W.WriteUInt32(static_cast<uint32>(ItemGuids.Num()));
	for (const int32 Guid : ItemGuids)
	{
		W.WriteUInt32(static_cast<uint32>(Guid));
	}
	SendGameAction(ACEGameAction::CreateTinkeringTool, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::HandleFellowshipFullUpdate(FACEBinaryReader& Reader)
{
	Fellowship = FACEFellowshipInfo();
	if (!Reader.CanRead(4))
	{
		OnFellowshipChanged.Broadcast();
		return;
	}
	const uint16 Count = Reader.ReadUInt16();
	const uint16 NumBuckets = Reader.ReadUInt16();
	(void)NumBuckets;
	Fellowship.Members.Reserve(Count);
	for (uint16 i = 0; i < Count; ++i)
	{
		FACEFellowshipMember M;
		if (!ReadFellowRecord(Reader, M))
		{
			break;
		}
		Fellowship.Members.Add(M);
	}
	Fellowship.Name = Reader.ReadString16L();
	if (Reader.CanRead(20))
	{
		Fellowship.LeaderGuid = static_cast<int32>(Reader.ReadUInt32());
		Fellowship.bShareXP = Reader.ReadUInt32() != 0;
		Fellowship.bEvenShare = Reader.ReadUInt32() != 0;
		Fellowship.bOpen = Reader.ReadUInt32() != 0;
		Fellowship.bLocked = Reader.ReadUInt32() != 0;
	}
	// DepartedMembers packable hashtable
	if (Reader.CanRead(4))
	{
		const uint16 DepCount = Reader.ReadUInt16();
		Reader.ReadUInt16(); // buckets
		for (uint16 i = 0; i < DepCount && Reader.CanRead(8); ++i)
		{
			Reader.ReadUInt32();
			Reader.ReadInt32();
		}
	}
	// FellowshipLocks: ushort count, ushort buckets, then (string16L + 5×uint32)
	if (Reader.CanRead(4))
	{
		const uint16 LockCount = Reader.ReadUInt16();
		Reader.ReadUInt16();
		for (uint16 i = 0; i < LockCount; ++i)
		{
			Reader.ReadString16L();
			if (Reader.CanRead(20))
			{
				Reader.Skip(20);
			}
		}
	}
	Fellowship.bValid = true;
	OnFellowshipChanged.Broadcast();
}

void FACESession::HandleFellowshipUpdateFellow(FACEBinaryReader& Reader)
{
	FACEFellowshipMember M;
	if (!ReadFellowRecord(Reader, M))
	{
		return;
	}
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // FellowUpdateType
	}
	bool bFound = false;
	for (FACEFellowshipMember& Existing : Fellowship.Members)
	{
		if (Existing.Guid == M.Guid)
		{
			Existing = M;
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		Fellowship.Members.Add(M);
	}
	Fellowship.bValid = true;
	OnFellowshipChanged.Broadcast();
}

void FACESession::HandleFellowshipQuitEvent(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	if (Guid == PlayerGuid)
	{
		Fellowship = FACEFellowshipInfo();
	}
	else
	{
		Fellowship.Members.RemoveAll([Guid](const FACEFellowshipMember& M) { return M.Guid == Guid; });
	}
	OnFellowshipChanged.Broadcast();
}

void FACESession::HandleFellowshipDismissEvent(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	if (Guid == PlayerGuid)
	{
		Fellowship = FACEFellowshipInfo();
	}
	else
	{
		Fellowship.Members.RemoveAll([Guid](const FACEFellowshipMember& M) { return M.Guid == Guid; });
	}
	OnFellowshipChanged.Broadcast();
}

void FACESession::HandleFellowshipDisband(FACEBinaryReader& Reader)
{
	(void)Reader;
	Fellowship = FACEFellowshipInfo();
	OnFellowshipChanged.Broadcast();
}

bool FACESession::ReadAllegianceData(FACEBinaryReader& Reader, FACEAllegianceMember& Out)
{
	if (!Reader.CanRead(16))
	{
		return false;
	}
	Out.Guid = static_cast<int32>(Reader.ReadUInt32());
	Out.CPCached = static_cast<int32>(Reader.ReadUInt32());
	Out.CPTithed = static_cast<int32>(Reader.ReadUInt32());
	const uint32 Bitfield = Reader.ReadUInt32();
	if (!Reader.CanRead(4))
	{
		return false;
	}
	Reader.ReadUInt8(); // gender
	Reader.ReadUInt8(); // heritage
	Out.Rank = Reader.ReadUInt16();
	constexpr uint32 HasPackedLevel = 0x8;
	constexpr uint32 HasAllegianceAge = 0x4;
	constexpr uint32 LoggedIn = 0x1;
	Out.bOnline = (Bitfield & LoggedIn) != 0;
	if (Bitfield & HasPackedLevel)
	{
		if (!Reader.CanRead(4))
		{
			return false;
		}
		Out.Level = static_cast<int32>(Reader.ReadUInt32());
	}
	if (!Reader.CanRead(4))
	{
		return false;
	}
	Out.Loyalty = Reader.ReadUInt16();
	Out.Leadership = Reader.ReadUInt16();
	if (Bitfield & HasAllegianceAge)
	{
		if (!Reader.CanRead(8))
		{
			return false;
		}
		Reader.ReadUInt32();
		Reader.ReadUInt32();
	}
	else
	{
		if (!Reader.CanRead(8))
		{
			return false;
		}
		Reader.ReadUInt64();
	}
	Out.Name = Reader.ReadString16L();
	return true;
}

void FACESession::HandleAllegianceUpdate(FACEBinaryReader& Reader)
{
	Allegiance = FACEAllegianceInfo();
	if (!Reader.CanRead(12))
	{
		OnAllegianceChanged.Broadcast();
		return;
	}
	Allegiance.Rank = static_cast<int32>(Reader.ReadUInt32());
	Allegiance.TotalMembers = static_cast<int32>(Reader.ReadUInt32());
	Allegiance.TotalVassals = static_cast<int32>(Reader.ReadUInt32());

	if (!Reader.CanRead(4))
	{
		OnAllegianceChanged.Broadcast();
		return;
	}
	const uint16 RecordCount = Reader.ReadUInt16();
	const uint16 OldVersion = Reader.ReadUInt16();
	(void)OldVersion;

	// officers packable hashtable
	if (Reader.CanRead(4))
	{
		const uint16 OffCount = Reader.ReadUInt16();
		Reader.ReadUInt16();
		for (uint16 i = 0; i < OffCount && Reader.CanRead(8); ++i)
		{
			Reader.ReadUInt32();
			Reader.ReadUInt32();
		}
	}
	// officer titles
	if (Reader.CanRead(4))
	{
		const int32 TitleCount = Reader.ReadInt32();
		for (int32 i = 0; i < TitleCount; ++i)
		{
			Reader.ReadString16L();
		}
	}
	if (Reader.CanRead(16))
	{
		Reader.Skip(16); // broadcast counters
	}
	Reader.ReadString16L(); // motd
	Reader.ReadString16L(); // motdSetBy
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // chatRoomID
	}
	// Position bindpoint: cell + pos(3f) + rot(4f) = 4+12+16 = 32
	if (Reader.CanRead(32))
	{
		Reader.Skip(32);
	}
	Allegiance.AllegianceName = Reader.ReadString16L();
	if (Reader.CanRead(12))
	{
		Reader.ReadUInt32(); // nameLastSetTime
		Reader.ReadUInt32(); // isLocked
		Reader.ReadInt32();  // approvedVassal
	}

	if (RecordCount >= 1)
	{
		FACEAllegianceMember Monarch;
		if (ReadAllegianceData(Reader, Monarch))
		{
			Monarch.Role = 0;
			Allegiance.MonarchGuid = Monarch.Guid;
			Allegiance.MonarchName = Monarch.Name;
		}
	}
	// Remaining records: treeParent + AllegianceData  (RecordCount - 1). Parent links are only
	// meaningful once every record is read, so collect first and resolve roles afterwards.
	TArray<TPair<int32, FACEAllegianceMember>> Records;
	Records.Reserve(FMath::Max(0, static_cast<int32>(RecordCount) - 1));
	for (uint16 i = 1; i < RecordCount; ++i)
	{
		if (!Reader.CanRead(4))
		{
			break;
		}
		const int32 TreeParent = static_cast<int32>(Reader.ReadUInt32());
		FACEAllegianceMember Member;
		if (!ReadAllegianceData(Reader, Member))
		{
			break;
		}
		Records.Emplace(TreeParent, Member);
	}
	for (const TPair<int32, FACEAllegianceMember>& Rec : Records)
	{
		if (Rec.Value.Guid == PlayerGuid)
		{
			Allegiance.PatronGuid = Rec.Key;
			Allegiance.SelfCPCached = Rec.Value.CPCached;
			Allegiance.SelfCPTithed = Rec.Value.CPTithed;
			Allegiance.Rank = Rec.Value.Rank;
			break;
		}
	}
	for (const TPair<int32, FACEAllegianceMember>& Rec : Records)
	{
		FACEAllegianceMember Member = Rec.Value;
		if (Member.Guid == PlayerGuid)
		{
			continue;
		}
		if (Member.Guid == Allegiance.PatronGuid)
		{
			Member.Role = 1;
			Allegiance.PatronName = Member.Name;
		}
		else if (Rec.Key == PlayerGuid)
		{
			Member.Role = 3;
			Allegiance.Vassals.Add(Member);
		}
	}
	if (Allegiance.PatronGuid == Allegiance.MonarchGuid && Allegiance.PatronName.IsEmpty())
	{
		Allegiance.PatronName = Allegiance.MonarchName;
	}

	Allegiance.bValid = !Allegiance.AllegianceName.IsEmpty() || Allegiance.MonarchGuid != 0;
	OnAllegianceChanged.Broadcast();
}

void FACESession::HandleFriendsListUpdate(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint32 Count = Reader.ReadUInt32();
	TArray<FACEFriendInfo> Parsed;
	Parsed.Reserve(static_cast<int32>(Count));
	for (uint32 i = 0; i < Count; ++i)
	{
		if (!Reader.CanRead(12))
		{
			break;
		}
		FACEFriendInfo F;
		F.Guid = static_cast<int32>(Reader.ReadUInt32());
		F.bOnline = Reader.ReadUInt32() != 0;
		Reader.ReadUInt32(); // appear offline
		F.Name = Reader.ReadString16L();
		if (Reader.CanRead(4))
		{
			const uint32 FriendFriends = Reader.ReadUInt32();
			for (uint32 j = 0; j < FriendFriends && Reader.CanRead(4); ++j)
			{
				Reader.ReadUInt32();
			}
		}
		if (Reader.CanRead(4))
		{
			const uint32 Inverse = Reader.ReadUInt32();
			for (uint32 j = 0; j < Inverse && Reader.CanRead(4); ++j)
			{
				Reader.ReadUInt32();
			}
		}
		Parsed.Add(F);
	}
	uint32 UpdateType = 0;
	if (Reader.CanRead(4))
	{
		UpdateType = Reader.ReadUInt32();
	}
	if (UpdateType == 0) // FullList
	{
		Friends = MoveTemp(Parsed);
	}
	else if (Parsed.Num() > 0)
	{
		const FACEFriendInfo& One = Parsed[0];
		if (UpdateType == 2) // Removed
		{
			Friends.RemoveAll([&](const FACEFriendInfo& F) { return F.Guid == One.Guid; });
		}
		else
		{
			bool bFound = false;
			for (FACEFriendInfo& F : Friends)
			{
				if (F.Guid == One.Guid)
				{
					F = One;
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				Friends.Add(One);
			}
		}
	}
	OnFriendsChanged.Broadcast();
}

void FACESession::HandleSalvageOperationsResult(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(12))
	{
		return;
	}
	const uint32 Skill = Reader.ReadUInt32();
	const uint32 NotSalvageableCount = Reader.ReadUInt32();
	TArray<FString> BadNames;
	for (uint32 i = 0; i < NotSalvageableCount && Reader.CanRead(4); ++i)
	{
		const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
		FACEWorldObject Obj;
		if (GetWorldObject(Guid, Obj) && !Obj.Name.IsEmpty())
		{
			BadNames.Add(Obj.Name);
		}
		else
		{
			BadNames.Add(FString::Printf(TEXT("0x%08X"), Guid));
		}
	}
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint32 ResultCount = Reader.ReadUInt32();
	TArray<FString> ResultLines;
	for (uint32 i = 0; i < ResultCount && Reader.CanRead(16); ++i)
	{
		const uint32 Material = Reader.ReadUInt32();
		const double Workmanship = Reader.ReadDouble();
		const uint32 Units = Reader.ReadUInt32();
		ResultLines.Add(FString::Printf(
			TEXT("You obtain %u %s (ws %.2f) using your knowledge of %s."),
			Units, GetMaterialTypeName(Material), Workmanship, SkillNameForSalvage(Skill)));
	}
	uint32 AugPct = 0;
	if (Reader.CanRead(4))
	{
		AugPct = Reader.ReadUInt32();
	}
	if (ResultLines.Num() == 0 && BadNames.Num() == 0)
	{
		OnChatMessage.Broadcast(TEXT("Salvaging Failed!"), TEXT(""), ACEChatMessageType::System);
		return;
	}
	for (const FString& Line : ResultLines)
	{
		OnChatMessage.Broadcast(Line, TEXT(""), ACEChatMessageType::System);
	}
	if (AugPct > 0)
	{
		OnChatMessage.Broadcast(
			FString::Printf(TEXT("Your augmentation increases the amount of salvage obtained by %u%%!"), AugPct),
			TEXT(""), ACEChatMessageType::System);
	}
	for (const FString& Name : BadNames)
	{
		OnChatMessage.Broadcast(
			FString::Printf(TEXT("You were unable to salvage %s."), *Name),
			TEXT(""), ACEChatMessageType::System);
	}
}

void FACESession::HandleSetSquelchDB(FACEBinaryReader& Reader)
{
	// SquelchDB: PackableHashTable<string, uint> accounts (always empty in retail),
	// PackableHashTable<uint, SquelchInfo> characters, then the global SquelchInfo.
	Squelches.Reset();
	GlobalSquelchMask = 0;
	auto ReadSquelchInfo = [&Reader](int32& OutMask, FString& OutName, bool& bOutAccount) -> bool
	{
		if (!Reader.CanRead(4))
		{
			return false;
		}
		const int32 FilterCount = Reader.ReadInt32();
		int32 Mask = 0;
		for (int32 i = 0; i < FilterCount && Reader.CanRead(4); ++i)
		{
			// Retail repeats the same mask 4x; OR them so any variant is honoured.
			Mask |= static_cast<int32>(Reader.ReadUInt32());
		}
		OutName = Reader.ReadString16L();
		bOutAccount = Reader.CanRead(4) ? (Reader.ReadUInt32() != 0) : false;
		OutMask = Mask;
		return true;
	};
	if (!Reader.CanRead(4))
	{
		OnSquelchChanged.Broadcast();
		return;
	}
	const uint16 AccountCount = Reader.ReadUInt16();
	Reader.ReadUInt16(); // buckets
	for (uint16 i = 0; i < AccountCount; ++i)
	{
		Reader.ReadString16L(); // account name
		if (Reader.CanRead(4)) { Reader.ReadUInt32(); }
	}
	if (!Reader.CanRead(4))
	{
		OnSquelchChanged.Broadcast();
		return;
	}
	const uint16 CharCount = Reader.ReadUInt16();
	Reader.ReadUInt16(); // buckets
	for (uint16 i = 0; i < CharCount; ++i)
	{
		if (!Reader.CanRead(4))
		{
			break;
		}
		FACESquelchEntry Entry;
		Entry.Guid = static_cast<int32>(Reader.ReadUInt32());
		if (!ReadSquelchInfo(Entry.Mask, Entry.Name, Entry.bAccount))
		{
			break;
		}
		Squelches.Add(Entry);
	}
	int32 GlobalMask = 0;
	FString GlobalName;
	bool bGlobalAccount = false;
	if (ReadSquelchInfo(GlobalMask, GlobalName, bGlobalAccount))
	{
		GlobalSquelchMask = GlobalMask;
	}
	OnSquelchChanged.Broadcast();
}

void FACESession::SendModifyCharacterSquelch(bool bSquelch, int32 TargetGuid,
	const FString& PlayerName, uint32 MessageType)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(bSquelch ? 1u : 0u);
	W.WriteUInt32(static_cast<uint32>(TargetGuid));
	W.WriteString16L(PlayerName);
	W.WriteUInt32(MessageType);
	SendGameAction(ACEGameAction::ModifyCharacterSquelch, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendModifyAccountSquelch(bool bSquelch, const FString& PlayerName)
{
	if (State != EACESessionState::InWorld || PlayerName.IsEmpty())
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(bSquelch ? 1u : 0u);
	W.WriteString16L(PlayerName);
	SendGameAction(ACEGameAction::ModifyAccountSquelch, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendModifyGlobalSquelch(bool bSquelch, uint32 MessageType)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(bSquelch ? 1u : 0u);
	W.WriteUInt32(MessageType);
	SendGameAction(ACEGameAction::ModifyGlobalSquelch, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::HandleContractTrackerTable(FACEBinaryReader& Reader)
{
	// PackableHashTable<uint, ContractTracker>: version, contractId, stage, timeDone, timeRepeat.
	Contracts.Reset();
	if (!Reader.CanRead(4))
	{
		OnContractsChanged.Broadcast();
		return;
	}
	const uint16 Count = Reader.ReadUInt16();
	Reader.ReadUInt16(); // buckets
	for (uint16 i = 0; i < Count; ++i)
	{
		if (!Reader.CanRead(4 + 4 + 4 + 4 + 8 + 8))
		{
			break;
		}
		const uint32 KeyId = Reader.ReadUInt32();
		Reader.ReadUInt32(); // Version
		const uint32 ContractId = Reader.ReadUInt32();
		FACEContractEntry Entry;
		Entry.ContractId = static_cast<int32>(ContractId != 0 ? ContractId : KeyId);
		Entry.Stage = static_cast<int32>(Reader.ReadUInt32());
		Entry.TimeWhenDone = static_cast<float>(Reader.ReadDouble());
		Entry.TimeWhenRepeats = static_cast<float>(Reader.ReadDouble());
		Entry.ReceivedAt = FPlatformTime::Seconds();
		Contracts.Add(Entry);
	}
	OnContractsChanged.Broadcast();
}

void FACESession::SendAbandonContract(int32 ContractId)
{
	if (State != EACESessionState::InWorld || ContractId == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ContractId));
	SendGameAction(ACEGameAction::AbandonContract, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendHouseQuery()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::HouseQuery, {}, ACEQueue::WeenieQueue);
}

void FACESession::ClearBook()
{
	Book = FACEBookInfo();
	OnBookChanged.Broadcast();
}

void FACESession::SendAbuseLogRequest(const FString& CharacterName, uint32 StatusMask,
	const FString& Complaint)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(CharacterName);
	W.WriteUInt32(StatusMask);
	W.WriteString16L(Complaint);
	SendGameAction(ACEGameAction::AbuseLogRequest, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendSetInscription(int32 ObjectGuid, const FString& Text)
{
	if (State != EACESessionState::InWorld || ObjectGuid == 0) return;
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ObjectGuid));
	W.WriteString16L(Text);
	SendGameAction(ACEGameAction::SetInscription, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendBookData(int32 BookGuid)
{
	if (State != EACESessionState::InWorld || BookGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(BookGuid));
	SendGameAction(ACEGameAction::BookData, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendBookPageData(int32 BookGuid, int32 PageIndex)
{
	if (State != EACESessionState::InWorld || BookGuid == 0 || PageIndex < 0)
	{
		return;
	}
	if (Book.bOpen && Book.BookGuid == BookGuid)
	{
		Book.CurrentPage = PageIndex;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(BookGuid));
	W.WriteInt32(PageIndex);
	SendGameAction(ACEGameAction::BookPageData, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendChessQuit()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::ChessQuit, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendChessMovePass()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::ChessMovePass, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendChessStalemate(bool bStalemate)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteInt32(bStalemate ? 1 : 0);
	SendGameAction(ACEGameAction::ChessStalemate, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendChessJoin(int32 BoardGuid)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	// Retail CM_Game::Event_Join(idGame, 0xFFFFFFFF) — -1 = join either color.
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(BoardGuid));
	W.WriteInt32(-1);
	SendGameAction(ACEGameAction::ChessJoin, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendChessMove(int32 FromX, int32 FromY, int32 ToX, int32 ToY)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	// GameActionChessMove: ChessPieceCoord from (x,y), to (x,y).
	FACEBinaryWriter W;
	W.WriteInt32(FromX);
	W.WriteInt32(FromY);
	W.WriteInt32(ToX);
	W.WriteInt32(ToY);
	SendGameAction(ACEGameAction::ChessMove, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::HandleChessGameEvent(uint32 EventType, FACEBinaryReader& Reader)
{
	FACEChessEvent Event;
	Event.EventType = static_cast<int32>(EventType);
	if (Reader.CanRead(4))
	{
		Event.BoardGuid = static_cast<int32>(Reader.ReadUInt32());
	}
	switch (EventType)
	{
	case ACEGameEvent::ChessJoinGameResponse: // boardGuid, ChessColor (-1 = failure)
	case ACEGameEvent::ChessStartGame:        // boardGuid, ChessColor to move first
	case ACEGameEvent::ChessMoveResponse:     // boardGuid, ChessMoveResult
	case ACEGameEvent::ChessGameOver:         // boardGuid, winning team
		if (Reader.CanRead(4))
		{
			Event.Value = Reader.ReadInt32();
		}
		break;
	case ACEGameEvent::ChessOpponentStalemate: // boardGuid, color, offer/retract
		if (Reader.CanRead(8))
		{
			Reader.ReadInt32(); // color
			Event.Value = Reader.ReadInt32();
		}
		break;
	case ACEGameEvent::ChessOpponentTurn:
		// boardGuid, color, ChessMoveData { type, playerGuid, per-type payload }.
		if (Reader.CanRead(12))
		{
			Event.Value = Reader.ReadInt32(); // mover color
			Event.MoveType = Reader.ReadInt32();
			Reader.ReadUInt32(); // player guid
			if (Event.MoveType == 4 && Reader.CanRead(8)) // Grid: to only
			{
				Event.ToX = Reader.ReadInt32();
				Event.ToY = Reader.ReadInt32();
			}
			else if (Event.MoveType == 5 && Reader.CanRead(16)) // FromTo
			{
				Event.FromX = Reader.ReadInt32();
				Event.FromY = Reader.ReadInt32();
				Event.ToX = Reader.ReadInt32();
				Event.ToY = Reader.ReadInt32();
			}
			else if (Event.MoveType == 6 && Reader.CanRead(4)) // SelectedPiece
			{
				Reader.ReadUInt32(); // piece guid
			}
		}
		break;
	default:
		break;
	}
	OnChessEvent.Broadcast(Event);
}

void FACESession::HandleHouseData(FACEBinaryReader& Reader)
{
	// HouseData: buyTime, rentTime, type, maintenanceFree, buy[], rent[], position.
	if (!Reader.CanRead(4 * 4 + 4))
	{
		return;
	}
	FACEHouseInfo Info;
	Info.bQueried = true;
	Info.bOwned = true;
	Info.BuyTime = static_cast<int64>(Reader.ReadUInt32());
	Info.RentTime = static_cast<int64>(Reader.ReadUInt32());
	Info.HouseType = static_cast<int32>(Reader.ReadUInt32());
	Info.bMaintenanceFree = Reader.ReadUInt32() != 0;

	auto ReadPayments = [&Reader](TArray<FACEHousePayment>& Out) -> bool
	{
		if (!Reader.CanRead(4))
		{
			return false;
		}
		const int32 Count = static_cast<int32>(Reader.ReadUInt32());
		if (Count < 0 || Count > 256)
		{
			return false;
		}
		for (int32 i = 0; i < Count; ++i)
		{
			if (!Reader.CanRead(12))
			{
				return false;
			}
			FACEHousePayment Payment;
			Payment.Required = static_cast<int32>(Reader.ReadUInt32());
			Payment.Paid = static_cast<int32>(Reader.ReadUInt32());
			Payment.Wcid = static_cast<int32>(Reader.ReadUInt32());
			Payment.Name = Reader.ReadString16L();
			Payment.PluralName = Reader.ReadString16L();
			Out.Add(Payment);
		}
		return true;
	};
	if (!ReadPayments(Info.Buy) || !ReadPayments(Info.Rent))
	{
		return;
	}
	if (Reader.CanRead(32))
	{
		Info.Position = Reader.ReadPosition();
	}
	House = Info;
	OnHouseChanged.Broadcast();
}

void FACESession::HandleHouseStatus(FACEBinaryReader& Reader)
{
	House = FACEHouseInfo();
	House.bQueried = true;
	if (Reader.CanRead(4))
	{
		House.StatusError = static_cast<int32>(Reader.ReadUInt32());
	}
	OnHouseChanged.Broadcast();
}

void FACESession::HandleBookDataResponse(FACEBinaryReader& Reader)
{
	// bookID, maxPages, numPages, maxChars, pageCount, pages[], inscription, authorId, authorName
	if (!Reader.CanRead(4 * 5))
	{
		return;
	}
	FACEBookInfo Info;
	Info.bOpen = true;
	Info.BookGuid = static_cast<int32>(Reader.ReadUInt32());
	Info.MaxPages = Reader.ReadInt32();
	Info.NumPages = Reader.ReadInt32();
	Info.MaxCharsPerPage = Reader.ReadInt32();
	const int32 PageCount = Reader.ReadInt32();
	if (PageCount < 0 || PageCount > 512)
	{
		return;
	}
	Info.Pages.SetNum(PageCount);
	for (int32 i = 0; i < PageCount; ++i)
	{
		if (!Reader.CanRead(4))
		{
			return;
		}
		Reader.ReadUInt32(); // AuthorId
		Reader.ReadString16L(); // AuthorName
		Reader.ReadString16L(); // AuthorAccount
		if (!Reader.CanRead(12))
		{
			return;
		}
		Reader.ReadUInt32(); // flags (0xFFFF0002)
		const uint32 TextIncluded = Reader.ReadUInt32();
		Reader.ReadUInt32(); // IgnoreAuthor
		if (TextIncluded != 0)
		{
			Info.Pages[i] = Reader.ReadString16L();
		}
	}
	Info.Inscription = Reader.ReadString16L();
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // AuthorId
	}
	Info.AuthorName = Reader.ReadString16L();
	Info.CurrentPage = 0;
	Book = Info;
	OnBookChanged.Broadcast();
	// Retail always follows TOC with a page fetch — page text is null in BookDataResponse.
	if (Book.BookGuid != 0 && (Book.NumPages > 0 || Book.MaxPages > 0 || Book.Pages.Num() > 0))
	{
		SendBookPageData(Book.BookGuid, 0);
	}
}

void FACESession::HandleBookPageDataResponse(FACEBinaryReader& Reader)
{
	// bookID, pageIndex, authorId, authorName, authorAccount, flags, textIncluded, ignoreAuthor, text
	if (!Reader.CanRead(8))
	{
		return;
	}
	const int32 BookGuid = static_cast<int32>(Reader.ReadUInt32());
	const int32 PageIndex = Reader.ReadInt32();
	if (!Book.bOpen || Book.BookGuid != BookGuid || PageIndex < 0)
	{
		return;
	}
	Reader.ReadUInt32(); // AuthorId
	Reader.ReadString16L(); // AuthorName
	Reader.ReadString16L(); // AuthorAccount
	if (!Reader.CanRead(12))
	{
		return;
	}
	Reader.ReadUInt32(); // flags
	const uint32 TextIncluded = Reader.ReadUInt32();
	Reader.ReadUInt32(); // IgnoreAuthor
	FString Text;
	if (TextIncluded != 0)
	{
		Text = Reader.ReadString16L();
	}
	if (PageIndex >= Book.Pages.Num())
	{
		Book.Pages.SetNum(PageIndex + 1);
	}
	Book.Pages[PageIndex] = Text;
	Book.CurrentPage = PageIndex;
	OnBookChanged.Broadcast();
}

void FACESession::HandleContractTracker(FACEBinaryReader& Reader)
{
	// ContractTracker + DeleteContract + SetAsDisplayContract
	if (!Reader.CanRead(4 + 4 + 4 + 8 + 8 + 8))
	{
		return;
	}
	Reader.ReadUInt32(); // Version
	FACEContractEntry Entry;
	Entry.ContractId = static_cast<int32>(Reader.ReadUInt32());
	Entry.Stage = static_cast<int32>(Reader.ReadUInt32());
	Entry.TimeWhenDone = static_cast<float>(Reader.ReadDouble());
	Entry.TimeWhenRepeats = static_cast<float>(Reader.ReadDouble());
	Entry.ReceivedAt = FPlatformTime::Seconds();
	const uint32 bDelete = Reader.ReadUInt32();
	Reader.ReadUInt32(); // SetAsDisplay
	const int32 Existing = Contracts.IndexOfByPredicate([&Entry](const FACEContractEntry& C)
	{
		return C.ContractId == Entry.ContractId;
	});
	if (bDelete != 0)
	{
		if (Existing != INDEX_NONE)
		{
			Contracts.RemoveAt(Existing);
		}
	}
	else if (Existing != INDEX_NONE)
	{
		Contracts[Existing] = Entry;
	}
	else
	{
		Contracts.Add(Entry);
	}
	OnContractsChanged.Broadcast();
}
