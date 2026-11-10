#include "ACESession.h"
#include "Protocol/ACEMaterialTypeNames.inl"
#include "UI/ACERetailAllegianceTitle.h"

namespace
{
	static bool ReadContractTracker(FACEBinaryReader& Reader, FACEContractEntry& Out)
	{
		if (!Reader.CanRead(28)) return false;
		Reader.ReadUInt32(); // version
		Out.ContractId = Reader.ReadInt32();
		Out.Stage = Reader.ReadInt32();
		Out.TimeWhenDone = Reader.ReadDouble();
		Out.TimeWhenRepeats = Reader.ReadDouble();
		return FMath::IsFinite(Out.TimeWhenDone) && FMath::IsFinite(Out.TimeWhenRepeats);
	}

	// ReadString16L returns an empty string on truncation, which is also a valid
	// wire value. Validate the complete string and its padding before consuming it.
	static bool ReadSocialString(FACEBinaryReader& Reader, FString& Out)
	{
		if (!Reader.CanRead(2)) return false;
		const int32 Start = Reader.Tell();
		const int32 Length = Reader.ReadUInt16();
		Reader.Seek(Start);
		if (!Reader.CanRead(Align(Length + 2, 4))) return false;
		Out = Reader.ReadString16L();
		return true;
	}

	static bool ReadBookPage(FACEBinaryReader& Reader, FString& Text, bool& bTextIncluded, FACEBookPageAuthor& Author)
	{
		if (!Reader.CanRead(4)) return false;
		Author.Guid = Reader.ReadInt32();
		FString Ignored;
		if (!ReadSocialString(Reader, Author.Name) || !ReadSocialString(Reader, Ignored) || !Reader.CanRead(4)) return false;
		const uint32 VersionOrTextIncluded = Reader.ReadUInt32();
		if ((VersionOrTextIncluded >> 16) == 0xFFFF)
		{
			if (VersionOrTextIncluded != 0xFFFF0002 || !Reader.CanRead(8)) return false;
			bTextIncluded = Reader.ReadUInt32() != 0;
			Author.bIgnoreAuthor = Reader.ReadUInt32() != 0;
		}
		else bTextIncluded = VersionOrTextIncluded != 0; // legacy PageData has no version/ignore-author fields
		return !bTextIncluded || ReadSocialString(Reader, Text);
	}

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
		if (!ReadSocialString(Reader, Out.Name)) return false;
		Out.VitalsReceivedAt = FPlatformTime::Seconds();
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
	// The UI/plugin subscription can be set before login, and outlives an
	// individual world session. Reapply it on entry, when the server can accept it.
	bFellowshipUpdatesRequested = bPanelOpen;
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
	CursorAllegianceUntil=FPlatformTime::Seconds()+30.;
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
	CursorAllegianceUntil=FPlatformTime::Seconds()+30.;
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
	CursorAllegianceUntil=bPanelOpen ? FPlatformTime::Seconds()+30. : 0.;
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
	if (!Reader.CanRead(4)) return;
	const uint16 Count = Reader.ReadUInt16();
	Reader.ReadUInt16(); // buckets
	// A member is at least 44 fixed bytes plus a padded string. The trailing
	// name, flags and departed-members table are mandatory in retail.
	if (Reader.Remaining() < 28 || Count > (Reader.Remaining() - 28) / 48) return;
	FACEFellowshipInfo Parsed;
	Parsed.Members.Reserve(Count);
	for (uint16 i = 0; i < Count; ++i)
	{
		FACEFellowshipMember M;
		if (!ReadFellowRecord(Reader, M)) return;
		Parsed.Members.Add(M);
	}
	if (!ReadSocialString(Reader, Parsed.Name) || !Reader.CanRead(24)) return;
	Parsed.LeaderGuid = static_cast<int32>(Reader.ReadUInt32());
	Parsed.bShareXP = Reader.ReadUInt32() != 0;
	Parsed.bEvenShare = Reader.ReadUInt32() != 0;
	Parsed.bOpen = Reader.ReadUInt32() != 0;
	Parsed.bLocked = Reader.ReadUInt32() != 0;
	// DepartedMembers packable hashtable
	const uint16 DepCount = Reader.ReadUInt16();
	Reader.ReadUInt16(); // buckets
	if (!Reader.CanRead(DepCount * 8)) return;
	Reader.Skip(DepCount * 8);
	// ACE appends FellowshipLocks; retail's Fellowship::UnPack ends at the
	// departed table. Accept either form, but never a partial extension.
	if (Reader.Remaining() > 0)
	{
		if (!Reader.CanRead(4)) return;
		const uint16 LockCount = Reader.ReadUInt16();
		Reader.ReadUInt16();
		if (LockCount > Reader.Remaining() / 24) return;
		for (uint16 i = 0; i < LockCount; ++i)
		{
			FString LockName;
			if (!ReadSocialString(Reader, LockName) || !Reader.CanRead(20)) return;
			Reader.Skip(20);
		}
	}
	Parsed.bValid = true;
	Fellowship = MoveTemp(Parsed);
	OnFellowshipChanged.Broadcast();
}

void FACESession::HandleFellowshipUpdateFellow(FACEBinaryReader& Reader)
{
	FACEFellowshipMember M;
	if (!ReadFellowRecord(Reader, M))
	{
		return;
	}
	if (!Reader.CanRead(4)) return;
	Reader.ReadUInt32(); // FellowUpdateType; all types carry a complete Fellow
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
	Out.Gender = Reader.ReadUInt8();
	Out.HeritageGroup = Reader.ReadUInt8();
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
	return ReadSocialString(Reader, Out.Name);
}

void FACESession::HandleAllegianceLoginNotification(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(8)) return;
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const bool bOnline = Reader.ReadUInt32() != 0;
	bool bChanged = false;
	auto Update = [&](FACEAllegianceMember& Member)
	{
		if (Member.Guid == Guid && Member.bOnline != bOnline)
		{ Member.bOnline = bOnline; bChanged = true; }
	};
	Update(Allegiance.Monarch); Update(Allegiance.Patron); Update(Allegiance.Self);
	for (auto& Vassal : Allegiance.Vassals) Update(Vassal);
	if (bChanged) OnAllegianceChanged.Broadcast();
}

void FACESession::HandleAllegianceUpdate(FACEBinaryReader& Reader)
{
	CursorAllegianceUntil=0.;
	if (!Reader.CanRead(4)) return;
	const int32 Rank = Reader.ReadInt32();
	FACEAllegianceInfo Parsed;
	if (!ReadAllegianceProfile(Reader, PlayerGuid, Parsed)) return;
	if (!Parsed.Self.Guid) Parsed.Rank = Rank;
	Allegiance = MoveTemp(Parsed);
	OnAllegianceChanged.Broadcast();
}

bool FACESession::ReadAllegianceProfile(FACEBinaryReader& Reader, int32 TargetGuid, FACEAllegianceInfo& Out)
{
	if (!Reader.CanRead(12)) return false;
	FACEAllegianceInfo Parsed;
	Parsed.TotalMembers = Reader.ReadInt32();
	Parsed.TotalVassals = Reader.ReadInt32();
	const uint16 RecordCount = Reader.ReadUInt16();
	const uint16 Version = Reader.ReadUInt16();

	// AllegianceHierarchy::UnPack gates each header field by its wire version.
	if (Version >= 6)
	{
		if (!Reader.CanRead(4)) return false;
		const uint16 OffCount = Reader.ReadUInt16();
		Reader.ReadUInt16();
		if (!Reader.CanRead(OffCount * 8)) return false;
		Reader.Skip(OffCount * 8);
	}
	else if (Version >= 1)
	{
		if (!Reader.CanRead(4)) return false;
		Reader.Skip(4); // obsolete officer field
	}
	FString Ignored;
	if (Version >= 9)
	{
		if (!Reader.CanRead(4)) return false;
		const uint32 TitleCount = Reader.ReadUInt32();
		if (TitleCount > uint32(Reader.Remaining() / 4)) return false;
		for (uint32 i = 0; i < TitleCount; ++i)
			if (!ReadSocialString(Reader, Ignored)) return false;
	}
	if (Version >= 2)
	{
		if (!Reader.CanRead(16)) return false;
		Reader.Skip(16); // broadcast counters
	}
	if (Version >= 3 && (!ReadSocialString(Reader, Ignored) || !ReadSocialString(Reader, Ignored))) return false;
	if (Version >= 4)
	{
		if (!Reader.CanRead(4)) return false;
		Reader.Skip(4); // chatRoomID
	}
	if (Version >= 7)
	{
		if (!Reader.CanRead(32)) return false;
		Reader.Skip(32); // bindpoint: cell, position, quaternion
	}
	if (Version >= 8)
	{
		if (!ReadSocialString(Reader, Parsed.AllegianceName) || !Reader.CanRead(4)) return false;
		Reader.Skip(4); // nameLastSetTime
	}
	if (Version >= 10)
	{
		if (!Reader.CanRead(4)) return false;
		Reader.Skip(4); // isLocked
	}
	if (Version >= 11)
	{
		if (!Reader.CanRead(4)) return false;
		Reader.Skip(4); // approvedVassal
	}

	if (RecordCount > Reader.Remaining() / 36) return false;
	if (RecordCount >= 1)
	{
		FACEAllegianceMember Monarch;
		if (!ReadAllegianceData(Reader, Monarch)) return false;
		Monarch.Role = 0;
		Parsed.MonarchGuid = Monarch.Guid;
		Parsed.MonarchName = Monarch.Name;
		Parsed.Monarch = Monarch;
		if (Monarch.Guid == TargetGuid) Parsed.Self = Monarch;
	}
	// Remaining records: treeParent + AllegianceData  (RecordCount - 1). Parent links are only
	// meaningful once every record is read, so collect first and resolve roles afterwards.
	TArray<TPair<int32, FACEAllegianceMember>> Records;
	Records.Reserve(FMath::Max(0, static_cast<int32>(RecordCount) - 1));
	for (uint16 i = 1; i < RecordCount; ++i)
	{
		if (!Reader.CanRead(4)) return false;
		const int32 TreeParent = static_cast<int32>(Reader.ReadUInt32());
		FACEAllegianceMember Member;
		if (!ReadAllegianceData(Reader, Member)) return false;
		Records.Emplace(TreeParent, Member);
	}
	for (const TPair<int32, FACEAllegianceMember>& Rec : Records)
	{
		if (Rec.Value.Guid == TargetGuid)
		{
			Parsed.PatronGuid = Rec.Key;
			Parsed.Self = Rec.Value;
			break;
		}
	}
	for (const TPair<int32, FACEAllegianceMember>& Rec : Records)
	{
		FACEAllegianceMember Member = Rec.Value;
		if (Member.Guid == TargetGuid)
		{
			continue;
		}
		if (Member.Guid == Parsed.PatronGuid)
		{
			Member.Role = 1;
			Parsed.PatronName = Member.Name;
			Parsed.Patron = Member;
		}
		else if (Rec.Key == TargetGuid)
		{
			Member.Role = 3;
			Parsed.Vassals.Add(Member);
		}
	}
	if (Parsed.PatronGuid == Parsed.MonarchGuid && Parsed.PatronName.IsEmpty())
	{
		Parsed.PatronName = Parsed.MonarchName;
		Parsed.Patron = Parsed.Monarch;
	}
	Parsed.SelfCPCached = Parsed.Self.CPCached;
	Parsed.SelfCPTithed = Parsed.Self.CPTithed;
	Parsed.Rank = Parsed.Self.Rank;
	Parsed.bValid = !Parsed.AllegianceName.IsEmpty() || Parsed.MonarchGuid != 0;
	Out = MoveTemp(Parsed);
	return true;
}

void FACESession::HandleAllegianceInfoResponse(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4)) return;
	const int32 TargetGuid = Reader.ReadInt32();
	FACEAllegianceInfo Info;
	if (!ReadAllegianceProfile(Reader, TargetGuid, Info) || !TargetGuid || Info.Self.Guid != TargetGuid) return;
	// This is a query about another member, not a replacement of our own UI data.
	auto FullName = [](const FACEAllegianceMember& Member)
	{
		const FString Title = ACERetailAllegiance::Title(Member.Rank, Member.HeritageGroup, Member.Gender);
		return (Title.IsEmpty() ? Member.Name : Title + TEXT(" ") + Member.Name) + (Member.bOnline ? TEXT(" *") : TEXT(""));
	};
	auto Say = [&](const FString& Text) { EmitChatMessage(Text, FString(), ACEChatMessageType::System); };
	Say(TEXT("Note: An asterisk (*) indicates that the character is currently online."));
	Say(FString::Printf(TEXT("Allegiance information for %s:"), *FullName(Info.Self)));
	if (Info.Patron.Guid) Say(TEXT("   Patron: ") + FullName(Info.Patron));
	if (!Info.Vassals.IsEmpty())
	{
		Say(TEXT("   Vassals:"));
		for (const auto& Vassal : Info.Vassals) Say(TEXT("      ") + FullName(Vassal));
	}
}

void FACESession::HandleFriendsListUpdate(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint32 Count = Reader.ReadUInt32();
	// FriendData is at least 12 fixed bytes, a padded string, two list
	// counts, followed by the update type for the complete message.
	if (Reader.Remaining() < 4 || Count > uint32((Reader.Remaining() - 4) / 24)) return;
	TArray<FACEFriendInfo> Parsed;
	Parsed.Reserve(static_cast<int32>(Count));
	for (uint32 i = 0; i < Count; ++i)
	{
		if (!Reader.CanRead(12))
		{
			return;
		}
		FACEFriendInfo F;
		F.Guid = static_cast<int32>(Reader.ReadUInt32());
		F.bOnline = Reader.ReadUInt32() != 0;
		Reader.ReadUInt32(); // appear offline
		if (!ReadSocialString(Reader, F.Name)) return;
		for (int32 ListIndex = 0; ListIndex < 2; ++ListIndex)
		{
			if (!Reader.CanRead(4)) return;
			const uint32 ListCount = Reader.ReadUInt32();
			if (ListCount > uint32(Reader.Remaining() / 4)) return;
			Reader.Skip(static_cast<int32>(ListCount * 4));
		}
		Parsed.Add(F);
	}
	if (!Reader.CanRead(4)) return;
	const uint32 UpdateType = Reader.ReadUInt32();
	if (UpdateType > 4 || (UpdateType != 0 && Parsed.IsEmpty())) return;
	if (UpdateType == 0) // FullList
	{
		Friends = MoveTemp(Parsed);
	}
	else if (Parsed.Num() > 0 && UpdateType<=4)
	{
		const FACEFriendInfo& One = Parsed[0];
		if (UpdateType == 2 || UpdateType == 3) // Removed / removed silently (retail)
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
		EmitChatMessage(TEXT("Salvaging Failed!"), TEXT(""), ACEChatMessageType::System);
		return;
	}
	for (const FString& Line : ResultLines)
	{
		EmitChatMessage(Line, TEXT(""), ACEChatMessageType::System);
	}
	if (AugPct > 0)
	{
		EmitChatMessage(
			FString::Printf(TEXT("Your augmentation increases the amount of salvage obtained by %u%%!"), AugPct),
			TEXT(""), ACEChatMessageType::System);
	}
	for (const FString& Name : BadNames)
	{
		EmitChatMessage(
			FString::Printf(TEXT("You were unable to salvage %s."), *Name),
			TEXT(""), ACEChatMessageType::System);
	}
}

bool FACESession::IsSenderSquelched(int32 Guid, const FString& Name, uint32 MessageType) const
{
	// SquelchDB::IsSquelched exempts spellcasting text from personal squelches.
	if (MessageType==17 || Guid==PlayerGuid) return false;
	for (const auto& Entry:Squelches)
		if ((Guid!=0 ? Entry.Guid==Guid : !Name.IsEmpty() && Entry.Name.Equals(Name,ESearchCase::IgnoreCase))
			&& Entry.Blocks(MessageType)) return true;
	return false;
}

void FACESession::HandleSetSquelchDB(FACEBinaryReader& Reader)
{
	// SquelchDB: PackableHashTable<string, uint> accounts (always empty in retail),
	// PackableHashTable<uint, SquelchInfo> characters, then the global SquelchInfo.
	Squelches.Reset();
	GlobalSquelchMask = 0;
	auto ReadSquelchInfo = [&Reader](FACESquelchEntry& Entry) -> bool
	{
		if (!Reader.CanRead(4))
		{
			return false;
		}
		const int32 FilterCount = Reader.ReadInt32();
		if (FilterCount<0 || FilterCount>4 || !Reader.CanRead(FilterCount*4)) return false;
		for (int32 i = 0; i < FilterCount; ++i)
		{
			Entry.FilterWords.Add(Reader.ReadUInt32());
		}
		Entry.Name = Reader.ReadString16L();
		if (!Reader.CanRead(4)) return false;
		Entry.bAccount = Reader.ReadUInt32() != 0;
		Entry.Mask = Entry.FilterWords.IsEmpty() ? 0 : int32(Entry.FilterWords[0]);
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
		if (!ReadSquelchInfo(Entry))
		{
			break;
		}
		Squelches.Add(Entry);
	}
	FACESquelchEntry Global;
	if (ReadSquelchInfo(Global))
	{
		GlobalSquelchMask = Global.Mask;
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
	// The hash key precedes each 28-byte tracker. Install only complete snapshots;
	// a truncated table must not erase contracts or reset their countdowns.
	if (!Reader.CanRead(4)) return;
	const uint16 Count = Reader.ReadUInt16();
	Reader.ReadUInt16(); // buckets
	if (Count > Reader.Remaining() / 32) return;
	TArray<FACEContractEntry> Updated;
	Updated.Reserve(Count);
	TMap<int32, int32> IndexById;
	const double ReceivedAt = FPlatformTime::Seconds();
	for (uint16 i = 0; i < Count; ++i)
	{
		const uint32 KeyId = Reader.ReadUInt32();
		FACEContractEntry Entry;
		if (!ReadContractTracker(Reader, Entry)) return;
		if (Entry.ContractId == 0) Entry.ContractId = static_cast<int32>(KeyId);
		Entry.ReceivedAt = ReceivedAt;
		if (const int32* Existing = IndexById.Find(Entry.ContractId)) Updated[*Existing] = Entry;
		else IndexById.Add(Entry.ContractId, Updated.Add(Entry));
	}
	Contracts = MoveTemp(Updated);
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
	SaveBookPage(INDEX_NONE); // Retail saves (or deletes one's blank page) on close.
	ResetBookEditing();
	Book = FACEBookInfo();
	PendingBookPage = INDEX_NONE;
	BookPageRequestedAt = 0.0;
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
	if (State != EACESessionState::InWorld || !Book.bOpen || Book.BookGuid != BookGuid || !BookGuid
		|| !Book.Pages.IsValidIndex(PageIndex))
	{
		return;
	}
	// Retail SetCurPage waits for the pending page and reuses text already read.
	// A bounded wait also lets the user retry if a custom host never replies.
	const double Now = FPlatformTime::Seconds();
	if (PendingBookPage != INDEX_NONE && Now - BookPageRequestedAt < 5.0) return;
	PendingBookPage = INDEX_NONE;
	const bool bChanged = Book.CurrentPage != PageIndex;
	if (bChanged) { bBookDraft = false; BookDraft.Reset(); }
	Book.CurrentPage = PageIndex;
	if (Book.HasPageText(PageIndex))
	{
		if (bChanged) OnBookChanged.Broadcast();
		return;
	}
	PendingBookPage = PageIndex;
	BookPageRequestedAt = Now;
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(BookGuid));
	W.WriteInt32(PageIndex);
	SendGameAction(ACEGameAction::BookPageData, W.GetData(), ACEQueue::WeenieQueue);
	if (bChanged) OnBookChanged.Broadcast();
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
	if (State != EACESessionState::InWorld || !BoardGuid || ChessBoardGuid
		|| FPlatformTime::Seconds() < ChessJoinPendingUntil)
	{
		return;
	}
	ChessJoinPendingUntil = FPlatformTime::Seconds() + 15.0;
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
	// Reject partial notices rather than treating absent fields as a white win/move.
	if (!Reader.CanRead(8)) return;
	FACEChessEvent Event;
	Event.EventType = static_cast<int32>(EventType);
	Event.BoardGuid = static_cast<int32>(Reader.ReadUInt32());
	switch (EventType)
	{
	case ACEGameEvent::ChessJoinGameResponse:
	case ACEGameEvent::ChessStartGame:
	case ACEGameEvent::ChessMoveResponse:
	case ACEGameEvent::ChessGameOver:
		Event.Value = Reader.ReadInt32();
		break;
	case ACEGameEvent::ChessOpponentStalemate:
		if (!Reader.CanRead(8)) return;
		Reader.ReadInt32(); // color
		Event.Value = Reader.ReadInt32();
		break;
	case ACEGameEvent::ChessOpponentTurn:
		if (!Reader.CanRead(12)) return;
		Event.Value = Reader.ReadInt32();
		Event.MoveType = Reader.ReadInt32();
		Reader.ReadUInt32(); // mover guid
		switch (Event.MoveType)
		{
		case 1: break; // pass (other minigames)
		case 4:
			if (!Reader.CanRead(8)) return;
			Event.ToX = Reader.ReadInt32(); Event.ToY = Reader.ReadInt32();
			break;
		case 5:
			if (!Reader.CanRead(16)) return;
			Event.FromX = Reader.ReadInt32(); Event.FromY = Reader.ReadInt32();
			Event.ToX = Reader.ReadInt32(); Event.ToY = Reader.ReadInt32();
			if (Event.FromX < 0 || Event.FromX > 7 || Event.FromY < 0 || Event.FromY > 7
				|| Event.ToX < 0 || Event.ToX > 7 || Event.ToY < 0 || Event.ToY > 7) return;
			break;
		case 6:
			if (!Reader.CanRead(4)) return;
			Reader.ReadUInt32();
			break;
		default: return;
		}
		break;
	default: return;
	}
	if (EventType == ACEGameEvent::ChessJoinGameResponse)
	{
		ChessJoinPendingUntil = 0.0;
		if (!ChessBoardGuid && Event.BoardGuid && (Event.Value == 0 || Event.Value == 1)) ChessBoardGuid = Event.BoardGuid;
	}
	else if (EventType == ACEGameEvent::ChessGameOver && Event.BoardGuid == ChessBoardGuid)
	{
		ChessBoardGuid = 0;
	}
	OnChessEvent.Broadcast(Event);
}

void FACESession::HandleBookDataResponse(FACEBinaryReader& Reader)
{
	// bookID, maxPages, PageDataList (capacity, maxChars, count, pages), inscription, scribe
	if (!Reader.CanRead(4 * 5))
	{
		return;
	}
	FACEBookInfo Info;
	Info.bOpen = true;
	Info.BookGuid = static_cast<int32>(Reader.ReadUInt32());
	Info.MaxPages = Reader.ReadInt32();
	const int32 ListCapacity = Reader.ReadInt32();
	Info.MaxCharsPerPage = Reader.ReadInt32();
	const int32 PageCount = Reader.ReadInt32();
	// Legacy PageData needs at least 16 bytes; the scribe trailer needs 12.
	if (!Info.BookGuid || Info.MaxPages < 0 || ListCapacity < 0 || Info.MaxCharsPerPage < 0
		|| PageCount < 0 || Reader.Remaining() < 12 || PageCount > (Reader.Remaining() - 12) / 16)
	{
		return;
	}
	Info.NumPages = PageCount;
	Info.Pages.SetNum(PageCount);
	Info.PageTextIncluded.SetNumZeroed(PageCount);
	Info.PageAuthors.SetNum(PageCount);
	for (int32 i = 0; i < PageCount; ++i)
	{
		if (!ReadBookPage(Reader, Info.Pages[i], Info.PageTextIncluded[i], Info.PageAuthors[i])) return;
	}
	if (!ReadSocialString(Reader, Info.Inscription) || !Reader.CanRead(4)) return;
	Reader.ReadUInt32(); // scribe ID
	if (!ReadSocialString(Reader, Info.AuthorName)) return;
	// A duplicate open response must not replace text being edited or a write
	// awaiting acknowledgement. Explicit recovery reads after a timeout/add win.
	if (Book.bOpen && Book.BookGuid == Info.BookGuid && !bBookRefreshRequired
		&& (bBookDraft || BookWriteAction)) return;
	// Retail OpenBook keeps the current page when refreshing the same book,
	// clamping it if the new list became shorter.
	Info.CurrentPage = Book.bOpen && Book.BookGuid == Info.BookGuid
		? FMath::Clamp(Book.CurrentPage, 0, FMath::Max(0, PageCount - 1)) : 0;
	if (Book.BookGuid != Info.BookGuid) SaveBookPage(INDEX_NONE);
	ResetBookEditing();
	Book = MoveTemp(Info);
	PendingBookPage = INDEX_NONE;
	BookPageRequestedAt = 0.0;
	OnBookChanged.Broadcast();
	// Retail requests only missing text. Capacity is not the number of readable pages.
	if (!Book.Pages.IsEmpty())
	{
		SendBookPageData(Book.BookGuid, Book.CurrentPage);
	}
	else if (Book.MaxPages > 0) OpenBookPage(0); // Retail opens a blank book by adding its first page.
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
	if (!Book.bOpen || Book.BookGuid != BookGuid || !Book.Pages.IsValidIndex(PageIndex))
	{
		return;
	}
	FString Text;
	bool bTextIncluded = false;
	FACEBookPageAuthor Author;
	if (!ReadBookPage(Reader, Text, bTextIncluded, Author) || !bTextIncluded) return;
	Book.Pages[PageIndex] = MoveTemp(Text);
	Book.PageTextIncluded.SetNumZeroed(Book.Pages.Num());
	Book.PageTextIncluded[PageIndex] = true;
	Book.PageAuthors.SetNum(Book.Pages.Num());
	Book.PageAuthors[PageIndex] = MoveTemp(Author);
	if (PendingBookPage == PageIndex) PendingBookPage = INDEX_NONE;
	// An old response can fill the cache but must not change the page being read.
	if (Book.CurrentPage == PageIndex) OnBookChanged.Broadcast();
}

void FACESession::HandleContractTracker(FACEBinaryReader& Reader)
{
	// ContractTracker + DeleteContract + SetAsDisplayContract
	if (!Reader.CanRead(36))
	{
		return;
	}
	FACEContractEntry Entry;
	if (!ReadContractTracker(Reader, Entry)) return;
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
