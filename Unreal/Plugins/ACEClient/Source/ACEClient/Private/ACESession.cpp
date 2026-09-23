#include "ACESession.h"
#include "ACEInventoryRules.h"
#include "ACEProfiling.h"
#include "ACERetailChat.h"
#include "ACECharacterOptions.h"
#include "ACECombatStance.h"
#include "Protocol/ACEHash32.h"
#include "Protocol/ACEObjectCreateParser.h"
#include "Protocol/ACECombatChat.h"
#include "Protocol/ACELoginErrors.h"
#include "ACEOpcodes.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "AddressInfoTypes.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarNetworkPumpMs(TEXT("ace.Network.PumpBudgetMs"),.5f,
	TEXT("Dispatch budget per socket per frame in milliseconds. A packet finishes atomically; 0 disables the time bound."));
static TAutoConsoleVariable<int32> CVarNetworkPumpPackets(TEXT("ace.Network.PumpMaxPackets"),128,
	TEXT("Maximum datagrams dispatched per socket per frame."));

FACESession::FACESession() = default;

FACESession::~FACESession()
{
	Disconnect();
}

void FACESession::SetState(EACESessionState NewState)
{
	if (State == NewState)
	{
		return;
	}
	if (NewState == EACESessionState::Disconnected || NewState == EACESessionState::Failed)
	{
		bCharacterCreationPending = false;
		bHasServerTime = false;
		PortalYearTicksAtConnect = PortalYearTicksRealtime = 0.0;
	}
	State = NewState;
	OnStateChanged.Broadcast(State);
}

void FACESession::Log(const FString& Msg)
{
	UE_LOG(LogTemp, Log, TEXT("[ACE] %s"), *Msg);
	OnLog.Broadcast(Msg);
}

bool FACESession::Connect(const FACELoginCredentials& Credentials)
{
	Disconnect();

	Creds = Credentials;
	AccountName = Credentials.Account;
	SetState(EACESessionState::Connecting);

	if (!CreateSockets())
	{
		ConnectionError = TEXT("Could not open the network connection.");
		SetState(EACESessionState::Failed);
		Log(TEXT("Failed to create UDP sockets"));
		return false;
	}

	PacketTimeOrigin = FPlatformTime::Seconds();
	SendLoginRequest();
	LoginRequestAt = FPlatformTime::Seconds();
	SetState(EACESessionState::AwaitConnectRequest);
	Log(FString::Printf(TEXT("LoginRequest sent to %s:%d as '%s'"), *Creds.Host, Creds.Port, *Creds.Account));
	return true;
}

void FACESession::Disconnect()
{
	Confirmations.Reset();
	LastServerPacketAt = 0.0;
	LoginRequestAt = 0.0;
	bRecoverLostConnection = false;
	PreHandshakeDatagrams.Reset();
	ConnectionError.Reset();
	VRCapabilities = VRSequence = VRPoseSequence = 0; VRPoses.Reset(); VRSpellProfiles.Reset();
	VRRecoverySequence = VRRecoveryTeleport = 0; VRRecoveryReadyAt = 0; VRRecoveryDuration = 0;
	VRCastSequence=VRCastEpoch=VRCastPhase=VRAimCastSequence=0; VRAimUntil=VRCastReadyAt=VRNextAim=0; VRCastDuration=0;
	VRMissileWeapon = 0; VRMissileSpeed = 0.f;
	// Tell the server to drop us immediately so the account is not left online.
	// Must happen before CloseSockets / Isaac reset while we can still encrypt.
	if (SocketC2S && IssacClient && State != EACESessionState::Disconnected && State != EACESessionState::Failed)
	{
		SendDisconnectPacket();
	}

	bHasServerTime = false;
	PortalYearTicksAtConnect = PortalYearTicksRealtime = 0.0;
	bLogOffPending = false;
	LogOffTimeout = 0.f;
	LogOffRetransmitTimer = 0.f;
	CloseSockets();
	PacketTimeOrigin = 0.0;
	IssacClient.Reset();
	IssacServer.Reset();
	Characters.Reset();
	PartialFragments.Reset();
	WorldObjects.Reset();
	ContainerContents.Reset();
	LoginEquipment.Reset();
	OpenExternalContainerGuid = 0;
	OpenVendorGuid = 0;
	VendorMerchandise.Reset();
	VendorBuyRate = 1.f;
	VendorItemTypes = MAX_uint32;
	VendorMinValue = VendorMaxValue = -1;
	VendorSellRate = 1.f;
	TradePartnerGuid = 0;
	TradeInitiatorGuid = 0;
	TradeSelfItems.Reset();
	TradePartnerItems.Reset();
	bUseBusy = false;
	CombatEventRevision = 0;
	bServerAttackInProgress = false;
	LastAttackError = 0;
	DisplayTitleId = 0;
	CharacterTitleIds.Reset();
	Fellowship = FACEFellowshipInfo();
	Allegiance = FACEAllegianceInfo();
	Friends.Reset();
	Contracts.Reset();
	House = FACEHouseInfo();
	LastTellSenderGuid = 0;
	LastTellSenderName.Reset();
	LastPatronTellSenderName.Reset(); LastMonarchTellSenderName.Reset();
	KnownSpells.Reset();
	SpellBars.Reset();
	ActiveSpellBar = 0;
	ShortcutObjects.Reset();
	bHasPlayerEncumbrance = false;
	PlayerEncumbranceVal = 0;
	ActiveEnchantments.Reset();
	VitaeCpPool = 0;
	DeathLevel = 0;
	LinkStatus = FACELinkStatus();
	PingRequestSentAt = 0.0;
	EchoTimeOrigin = 0.0;
	PendingEchoTimes.Reset();
	for (auto& Bucket : LinkTraffic) Bucket = FLinkTrafficBucket();
	PlayerGuid = 0;
	bLocalPlayerIsAdmin = false;
	CurrentStance = ACEMotion::StanceNonCombat;
	bEnteredWorldSent = false;
	PendingEnterCharacterId = 0;
	bLoginCompleteSent = false;
	bMoving = false;
	bForcePositionReporting = false;
	NextPacketSequence = 2;
	NextFragmentSequence = 1;
	NextGameActionSequence = 1;
	LastReceivedPacketSequence = 1;
	CachedC2SPackets.Reset();
	OutOfOrderS2CPackets.Reset();
	LastRequestForRetransmitTime = 0.0;
	ConnectResponseRetryTimer = 0.f;
	CharacterListWaitTimer = 0.f;
	PendingCharacterMutation = 0;
	CharacterManagementError.Reset();
	ConnectResponseRetriesSent = 0;
	SetState(EACESessionState::Disconnected);
}

void FACESession::SendCharacterLogOff()
{
	SendGameMessage(ACEOpcode::CharacterLogOff, {}, ACEQueue::UIQueue, true);
	Log(TEXT("CharacterLogOff (0xF653) sent"));
}

void FACESession::SendDisconnectPacket()
{
	// Empty encrypted Disconnect header — NetworkSession.ProcessPacket → Terminate → DropSession.
	SendRawPacket(EACEPacketHeaderFlags::Disconnect | EACEPacketHeaderFlags::EncryptedChecksum,
		{}, {}, false, true, ClientId);
	Log(TEXT("Disconnect packet sent"));
}

void FACESession::RequestLogOff()
{
	if (State == EACESessionState::Disconnected || State == EACESessionState::Failed)
	{
		return;
	}

	if (State == EACESessionState::InWorld || State == EACESessionState::EnteringWorld)
	{
		if (bLogOffPending)
		{
			return;
		}
		// Leave combat before logout so the server action queue is not blocked on stance.
		if (State == EACESessionState::InWorld && PlayerVitals.CombatMode != static_cast<int32>(ACECombatMode::NonCombat))
		{
			SendChangeCombatMode(ACECombatMode::NonCombat);
		}
		SendStopMovement();
		FlushAutonomousPosition(true);
		bLogOffPending = true;
		LogOffTimeout = 45.f; // logout anim + landblock remove + 6s SendFinalLogOffMessages
		LogOffRetransmitTimer = 0.f;
		SendCharacterLogOff();
		return;
	}

	// Character select / auth — close the account session entirely.
	Disconnect();
}

void FACESession::ClearWorldState()
{
	VRCapabilities = VRSequence = VRPoseSequence = 0; VRPoses.Reset(); VRSpellProfiles.Reset();
	VRRecoverySequence = VRRecoveryTeleport = 0; VRRecoveryReadyAt = 0; VRRecoveryDuration = 0;
	VRCastSequence=VRCastEpoch=VRCastPhase=VRAimCastSequence=0; VRAimUntil=VRCastReadyAt=VRNextAim=0; VRCastDuration=0;
	VRMissileWeapon = 0; VRMissileSpeed = 0.f;
	TArray<int32> Guids;
	WorldObjects.GetKeys(Guids);
	for (const int32 Guid : Guids)
	{
		OnObjectDeleted.Broadcast(Guid);
	}
	WorldObjects.Reset();
	ContainerContents.Reset();
	LoginEquipment.Reset();
	OpenExternalContainerGuid = 0;
	OpenVendorGuid = 0;
	VendorMerchandise.Reset();
	VendorBuyRate = 1.f;
	VendorItemTypes = MAX_uint32;
	VendorMinValue = VendorMaxValue = -1;
	VendorSellRate = 1.f;
	TradePartnerGuid = 0;
	TradeInitiatorGuid = 0;
	TradeSelfItems.Reset();
	TradePartnerItems.Reset();
	bUseBusy = false;
	CombatEventRevision = 0;
	bServerAttackInProgress = false;
	LastAttackError = 0;
	DisplayTitleId = 0;
	CharacterTitleIds.Reset();
	Fellowship = FACEFellowshipInfo();
	Allegiance = FACEAllegianceInfo();
	Friends.Reset();
	Contracts.Reset();
	House = FACEHouseInfo();
	LastTellSenderGuid = 0;
	LastTellSenderName.Reset();
	LastPatronTellSenderName.Reset(); LastMonarchTellSenderName.Reset();
	KnownSpells.Reset();
	SpellBars.Reset();
	ActiveSpellBar = 0;
	ShortcutObjects.Reset();
	bHasPlayerEncumbrance = false;
	PlayerEncumbranceVal = 0;
	ActiveEnchantments.Reset();
	VitaeCpPool = 0;
	DeathLevel = 0;
	LinkStatus = FACELinkStatus();
	PingRequestSentAt = 0.0;
	EchoTimeOrigin = 0.0;
	PendingEchoTimes.Reset();
	for (auto& Bucket : LinkTraffic) Bucket = FLinkTrafficBucket();
	PlayerGuid = 0;
	bLocalPlayerIsAdmin = false;
	PlayerPosition = FACEPosition();
	PlayerVitals = FACEPlayerVitals();
	SelectedObject = FACESelectedObject();
	CurrentStance = ACEMotion::StanceNonCombat;
	bEnteredWorldSent = false;
	PendingEnterCharacterId = 0;
	bLoginCompleteSent = false;
	bMoving = false;
	bForcePositionReporting = false;
	PartialFragments.Reset();
}

bool FACESession::CreateSockets()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		return false;
	}

	ServerC2SAddr = Sockets->CreateInternetAddr();
	bool bValid = false;
	ServerC2SAddr->SetIp(*Creds.Host, bValid);
	if (!bValid)
	{
		FIPv4Address Ip;
		if (FIPv4Address::Parse(Creds.Host, Ip))
		{
			ServerC2SAddr->SetIp(Ip.Value);
			bValid = true;
		}
		else
		{
			const FAddressInfoResult Info = Sockets->GetAddressInfo(
				*Creds.Host,
				nullptr,
				EAddressInfoFlags::Default,
				NAME_None,
				SOCKTYPE_Datagram);
			if (Info.Results.Num() > 0)
			{
				ServerC2SAddr = Info.Results[0].Address;
				bValid = true;
			}
		}
	}
	if (!bValid)
	{
		Log(TEXT("Invalid host address"));
		return false;
	}
	ServerC2SAddr->SetPort(Creds.Port);

	ServerS2CAddr = ServerC2SAddr->Clone();
	ServerS2CAddr->SetPort(Creds.Port + 1);

	SocketC2S = FUdpSocketBuilder(TEXT("ACE_C2S"))
		.AsNonBlocking()
		.WithReceiveBufferSize(1024 * 1024)
		.AsReusable()
		.BoundToPort(0)
		.Build();

	SocketS2C = FUdpSocketBuilder(TEXT("ACE_S2C"))
		.AsNonBlocking()
		.WithReceiveBufferSize(1024 * 1024)
		.AsReusable()
		.BoundToPort(0)
		.Build();

	return SocketC2S != nullptr && SocketS2C != nullptr;
}

void FACESession::CloseSockets()
{
	if (SocketC2S)
	{
		SocketC2S->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SocketC2S);
		SocketC2S = nullptr;
	}
	if (SocketS2C)
	{
		SocketS2C->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SocketS2C);
		SocketS2C = nullptr;
	}
}

bool FACESession::HasConnectionTimedOut(double Now) const
{
	return State >= EACESessionState::CharacterSelect && State <= EACESessionState::InWorld
		&& LastServerPacketAt > 0.0 && Now - LastServerPacketAt > 20.0;
}

void FACESession::Tick(float DeltaSeconds)
{
	ACE_PROFILE_SCOPE(Network);
	if (PendingCharacterMutation && FPlatformTime::Seconds() - CharacterMutationSentAt > 20.0)
	{
		// Refresh the authoritative roster after a lost reply. Never retry a
		// destructive slot-based action against a potentially changed list.
		const FACELoginCredentials RetryCredentials = Creds;
		Connect(RetryCredentials);
		return;
	}
	// Check wall time before draining queued packets: after headset suspension,
	// old packets must not make a dead session appear alive again. Reauthenticate
	// once and stop at character selection; never automatically enter a character.
	if (bRecoverLostConnection || HasConnectionTimedOut(FPlatformTime::Seconds()))
	{
		const FACELoginCredentials RetryCredentials = Creds;
		Log(TEXT("Connection lost. Reconnecting to character selection."));
		Connect(RetryCredentials);
		return;
	}
	if (State == EACESessionState::Disconnected || State == EACESessionState::Failed)
	{
		return;
	}

	// Grace expired: drop pending sync. Never apply deferred NonCombat here — that was
	// SwitchCombatStyles noise; keeping optimistic CombatMode until PropertyInt confirms
	// the requested mode (or the player explicitly requests peace).
	if (PendingCombatMode != 0 && FPlatformTime::Seconds() >= PendingCombatModeUntil)
	{
		PendingCombatMode = 0;
		DeferredServerCombatMode = INDEX_NONE;
	}

	if (bLogOffPending)
	{
		LogOffTimeout -= DeltaSeconds;
		LogOffRetransmitTimer += DeltaSeconds;
		// UDP: retransmit CharacterLogOff every 2s while waiting for CharacterList.
		if (LogOffRetransmitTimer >= 2.f)
		{
			SendCharacterLogOff();
			LogOffRetransmitTimer = 0.f;
		}
		if (LogOffTimeout <= 0.f)
		{
			Log(TEXT("CharacterLogOff timed out waiting for CharacterList — sending Disconnect"));
			Disconnect();
			return;
		}
	}

	PollSockets();
	if (State == EACESessionState::AwaitConnectRequest && LoginRequestAt > 0.0 && FPlatformTime::Seconds() - LoginRequestAt > 20.0)
	{
		ConnectionError = TEXT("The server did not respond. Check the server address and try connecting again.");
		Log(ConnectionError);
		SetState(EACESessionState::Failed);
		return;
	}
	RequestMissingS2CPackets(FPlatformTime::Seconds());

	if (State == EACESessionState::AwaitCharacterList)
	{
		CharacterListWaitTimer += DeltaSeconds;
		ConnectResponseRetryTimer += DeltaSeconds;

		// Retransmit ConnectResponse: ACE NetworkManager silently drops an early reply if
		// State is not yet AuthConnectResponse, and UDP to :port+1 can be lost.
		static constexpr float RetryIntervals[] = { 0.15f, 0.4f, 0.8f, 1.5f };
		if (ConnectResponseRetriesSent < UE_ARRAY_COUNT(RetryIntervals) &&
			ConnectResponseRetryTimer >= RetryIntervals[ConnectResponseRetriesSent])
		{
			SendConnectResponse();
			++ConnectResponseRetriesSent;
			Log(FString::Printf(TEXT("ConnectResponse retransmit #%d"), ConnectResponseRetriesSent));
		}

		if (CharacterListWaitTimer >= 20.f)
		{
			ConnectionError = FString::Printf(TEXT("The server did not return the character list. Check UDP ports %d/%d for %s."), Creds.Port, Creds.Port + 1, *Creds.Host);
			Log(TEXT("Timed out waiting for CharacterList — server may have missed ConnectResponse (UDP port+1 / auth race), or never sent the list"));
			SetState(EACESessionState::Failed);
			return;
		}
	}

	AckTimer += DeltaSeconds;
	// Retail-ish ACK cadence (~0.5s). Only contiguous LastReceived — never ACK past holes.
	if (bNeedAck && AckTimer >= 0.5f)
	{
		SendAckIfNeeded();
		AckTimer = 0.f;
	}

	EchoTimer += DeltaSeconds;
	if (State >= EACESessionState::CharacterSelect && EchoTimer >= 2.f)
	{
		// Periodic EchoRequest helps keep session alive / RTT
		FACEBinaryWriter Body;
		Body.WriteFloat(RecordEchoRequest(FPlatformTime::Seconds()));
		SendRawPacket(EACEPacketHeaderFlags::EchoRequest | EACEPacketHeaderFlags::EncryptedChecksum,
			Body.GetData(), {}, false, true, ClientId);
		EchoTimer = 0.f;
	}

	if (State == EACESessionState::InWorld && !bLogOffPending && (bMoving || bForcePositionReporting))
	{
		AutoPosTimer += DeltaSeconds;
		// Retail acclient sends AutonomousPosition ~1 Hz while moving (ACE handler comment).
		// ACE then applies the pose each world tick; observer F748 broadcasts are separately
		// capped at MoveToState_UpdatePosition_Threshold (1 s) unless broadcast is forced.
		const float ReportInterval=(VRCapabilities & 32768u) && FPlatformTime::Seconds()-LastVRPoseSent<.5
			? FMath::Min(AutonomousPositionInterval,.05f) : AutonomousPositionInterval;
		if (AutoPosTimer >= ReportInterval)
		{
			SendAutonomousPosition(bAutoPosContact);
			AutoPosTimer = 0.f;
		}
	}
}

void FACESession::PollSockets()
{
	uint8 Buffer[2048];
	int32 BytesRead = 0;
	TSharedRef<FInternetAddr> FromAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

		auto Drain = [&](FSocket* Sock, bool bS2C)
	{
		if (!Sock)
		{
			return;
		}
		const double Start = FPlatformTime::Seconds();
		const double Budget = FMath::Max(0.f, CVarNetworkPumpMs.GetValueOnGameThread()) * .001;
		const int32 MaxPackets = FMath::Max(1, CVarNetworkPumpPackets.GetValueOnGameThread());
		int32 Packets = 0;
		while (Sock->RecvFrom(Buffer, sizeof(Buffer), BytesRead, *FromAddr) && BytesRead > 0)
		{
			// In particular, a cleartext login rejection must come from the endpoint
			// we contacted, not an unrelated sender on this ephemeral UDP socket.
			if ((ServerC2SAddr.IsValid() && *FromAddr == *ServerC2SAddr)
				|| (ServerS2CAddr.IsValid() && *FromAddr == *ServerS2CAddr))
				HandleDatagram(Buffer, BytesRead, bS2C);
			// Leave the remainder queued in the socket in its original order.
			// Continuous traffic must not monopolize an entire game frame.
			if (++Packets >= MaxPackets || (Budget > 0 && FPlatformTime::Seconds() - Start >= Budget)) break;
		}
	};

	Drain(SocketC2S, false);
	Drain(SocketS2C, true);
}

void FACESession::HandleDatagram(const uint8* Data, int32 Size, bool /*bFromS2CSocket*/)
{
	if (Size < PacketHeaderSize)
	{
		return;
	}

	FACEBinaryReader HeaderReader(Data, PacketHeaderSize);
	const uint32 Sequence = HeaderReader.ReadUInt32();
	const EACEPacketHeaderFlags Flags = static_cast<EACEPacketHeaderFlags>(HeaderReader.ReadUInt32());
	const uint32 Checksum = HeaderReader.ReadUInt32();
	const uint16 Id = HeaderReader.ReadUInt16();
	const uint16 Time = HeaderReader.ReadUInt16();
	const uint16 PayloadSize = HeaderReader.ReadUInt16();
	const uint16 Iteration = HeaderReader.ReadUInt16();

	if (PayloadSize > Size - PacketHeaderSize)
	{
		return;
	}

	const bool bEncrypted = EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::EncryptedChecksum);
	if (bEncrypted && !IssacServer && State == EACESessionState::AwaitConnectRequest)
	{
		// UDP can deliver an encrypted auth rejection before the cleartext seed
		// packet. Keep a bounded queue and verify its CRC after installing the
		// seeds; never dispatch unauthenticated content or treat it as corrupt yet.
		if (PreHandshakeDatagrams.Num() < 32 && Size <= 2048)
		{
			TArray<uint8>& Pending = PreHandshakeDatagrams.AddDefaulted_GetRef();
			Pending.Append(Data, Size);
		}
		return;
	}
	const uint32 HHash = HeaderHash32(Sequence, Flags, Id, Time, PayloadSize, Iteration);

	FACEBinaryReader Payload(Data + PacketHeaderSize, PayloadSize);
	TArray<uint8> OptionalBytes;
	float EchoClientTime = -1.f;
	float EchoResponseClientTime = -1.f;
	TArray<uint32> RetransmitSequences;
	uint32 ServerAckSequence = 0;
	bool bHasServerAck = false;
	uint32 NetErrorStringId = 0, NetErrorTableId = 0;
	const bool bHasNetError = EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::NetError | EACEPacketHeaderFlags::NetErrorDisconnect);
	double ServerTicks = 0.0;
	const double ReceivedAt = FPlatformTime::Seconds();

	auto Capture = [&](int32 Bytes)
	{
		if (!Payload.CanRead(Bytes))
		{
			return false;
		}
		const int32 Start = Payload.Tell();
		OptionalBytes.Append(Payload.GetData() + Start, Bytes);
		Payload.Skip(Bytes);
		return true;
	};

	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::RequestRetransmit))
	{
		if (!Payload.CanRead(4)) return;
		const int32 Start = Payload.Tell();
		const uint32 Count = Payload.ReadUInt32();
		if (Count > static_cast<uint32>(Payload.Remaining() / 4)) return;
		RetransmitSequences.Reserve(static_cast<int32>(Count));
		for (uint32 i = 0; i < Count; ++i)
		{
			RetransmitSequences.Add(Payload.ReadUInt32());
		}
		OptionalBytes.Append(Payload.GetData() + Start, 4 + static_cast<int32>(Count * 4));
	}
	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::RejectRetransmit))
	{
		if (!Payload.CanRead(4)) return;
		const int32 Start = Payload.Tell();
		const uint32 Count = Payload.ReadUInt32();
		if (Count > static_cast<uint32>(Payload.Remaining() / 4)) return;
		Payload.Skip(static_cast<int32>(Count * 4));
		OptionalBytes.Append(Payload.GetData() + Start, 4 + static_cast<int32>(Count * 4));
	}
	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::AckSequence))
	{
		if (!Payload.CanRead(4)) return;
		const int32 Start = Payload.Tell();
		ServerAckSequence = Payload.ReadUInt32();
		bHasServerAck = true;
		OptionalBytes.Append(Payload.GetData() + Start, 4);
	}
	// Retail NetError is a StringInfo pair, not a game-event error number.
	// Both optional headers occupy eight bytes and precede TimeSync on the wire.
	for (const auto ErrorFlag : {EACEPacketHeaderFlags::NetError, EACEPacketHeaderFlags::NetErrorDisconnect})
	{
		if (!EnumHasAnyFlags(Flags, ErrorFlag)) continue;
		if (!Payload.CanRead(8)) return;
		const int32 Start = Payload.Tell();
		NetErrorStringId = Payload.ReadUInt32();
		NetErrorTableId = Payload.ReadUInt32();
		OptionalBytes.Append(Payload.GetData() + Start, 8);
	}
	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::TimeSync))
	{
		if (!Payload.CanRead(8)) return;
		const int32 Start = Payload.Tell();
		ServerTicks = Payload.ReadDouble();
		OptionalBytes.Append(Payload.GetData() + Start, 8);

	}
	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::EchoRequest))
	{
		if (!Payload.CanRead(4)) return;
		const int32 Start = Payload.Tell();
		EchoClientTime = Payload.ReadFloat();
		OptionalBytes.Append(Payload.GetData() + Start, 4);
	}
	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::EchoResponse))
	{
		if (!Payload.CanRead(8)) return;
		const int32 Start = Payload.Tell();
		EchoResponseClientTime = Payload.ReadFloat();
		Payload.ReadFloat(); // server local delta (unused)
		OptionalBytes.Append(Payload.GetData() + Start, 8);
	}
	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::Flow))
	{
		if (!Capture(6)) return;
	}

	uint32 PayloadHash = FACEHash32::Calculate(OptionalBytes);
	TArray<FReceivedFragment> Fragments;

	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::BlobFragments))
	{
		while (Payload.Remaining() > 0)
		{
			if (!Payload.CanRead(FragmentHeaderSize))
			{
				return;
			}
			const int32 FragHeaderStart = Payload.Tell();
			const uint32 FragSeq = Payload.ReadUInt32();
			Payload.ReadUInt32(); // Id
			const uint16 FragCount = Payload.ReadUInt16();
			const uint16 FragSize = Payload.ReadUInt16();
			const uint16 FragIndex = Payload.ReadUInt16();
			const uint16 FragQueue = Payload.ReadUInt16();

			const int32 DataLen = static_cast<int32>(FragSize) - FragmentHeaderSize;
			if (FragCount == 0 || FragIndex >= FragCount || DataLen <= 0
				|| DataLen > MaxFragmentDataSize || !Payload.CanRead(DataLen))
			{
				return;
			}

			const uint8* FragHeaderPtr = Payload.GetData() + FragHeaderStart;
			const uint8* FragDataPtr = Payload.GetData() + Payload.Tell();
			PayloadHash += FACEHash32::Calculate(FragHeaderPtr, FragmentHeaderSize);
			PayloadHash += FACEHash32::Calculate(FragDataPtr, DataLen);

			// Keep validated wire fragments local until CRC and packet sequencing pass.
			FReceivedFragment& Fragment = Fragments.AddDefaulted_GetRef();
			Fragment.Sequence = FragSeq;
			Fragment.Count = FragCount;
			Fragment.Index = FragIndex;
			Fragment.Queue = FragQueue;
			Fragment.Data = Payload.ReadBytes(DataLen);
		}
	}
	else if (Payload.Remaining() > 0)
	{
		const int32 BodyLen = Payload.Remaining();
		PayloadHash += FACEHash32::Calculate(Payload.GetData() + Payload.Tell(), BodyLen);

		if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::ConnectRequest))
		{
			// A repeated handshake must not reset active keys, replay a rejection,
			// or move a failed/authenticated session back to character-list wait.
			if (State != EACESessionState::AwaitConnectRequest || BodyLen < 28) return;
			FACEBinaryReader BodyReader(Payload.GetData() + Payload.Tell(), BodyLen);
			// Verify CRC before handling
			bool bCrcOk = false;
			if (bEncrypted)
			{
				if (IssacServer)
				{
					const uint32 Key = (Checksum - HHash) ^ PayloadHash;
					if (IssacServer->Search(Key))
					{
						IssacServer->ConsumeKey(Key);
						bCrcOk = true;
					}
				}
			}
			else
			{
				bCrcOk = (HHash + PayloadHash) == Checksum;
			}

			if (bCrcOk)
			{
				LastServerPacketAt = ReceivedAt;
				HandleConnectRequest(BodyReader);
				if (Sequence != 0)
				{
					LastReceivedPacketSequence = Sequence;
					bNeedAck = true;
				}
				TArray<TArray<uint8>> Pending = MoveTemp(PreHandshakeDatagrams);
				PreHandshakeDatagrams.Reset();
				for (const TArray<uint8>& Datagram : Pending)
				{
					if (State == EACESessionState::Failed || State == EACESessionState::Disconnected) break;
					HandleDatagram(Datagram.GetData(), Datagram.Num(), true);
				}
			}
			return;
		}
	}

	bool bCrcOk = false;
	if (bEncrypted)
	{
		if (IssacServer)
		{
			const uint32 Key = (Checksum - HHash) ^ PayloadHash;
			if (IssacServer->Search(Key))
			{
				IssacServer->ConsumeKey(Key);
				bCrcOk = true;
			}
		}
	}
	else
	{
		bCrcOk = (HHash + PayloadHash) == Checksum;
	}

	if (!bCrcOk)
	{
		Log(TEXT("Packet CRC failed — dropped"));
		return;
	}
	if (bEncrypted) LastServerPacketAt = ReceivedAt;
	if (!RetransmitSequences.IsEmpty()) RecordLinkTraffic(ReceivedAt, 0, RetransmitSequences.Num());
	if (bHasNetError && (NetErrorStringId || NetErrorTableId))
	{
		if (State == EACESessionState::Failed || State == EACESessionState::Disconnected) return;
		// Initial GDLE rejections have no ISAAC keys yet. Once authenticated,
		// ignore delayed cleartext rejections from an earlier login attempt.
		if (!bEncrypted && State != EACESessionState::AwaitConnectRequest) return;
		const FString Reason = ACELoginErrors::Network(NetErrorStringId, NetErrorTableId);
		ConnectionError = FString::Printf(TEXT("%s (%s:%d; 0x%08X, table %u)"),
			*Reason, *Creds.Host, Creds.Port, NetErrorStringId, NetErrorTableId);
		Log(ConnectionError);
		bRecoverLostConnection = State == EACESessionState::InWorld;
		PreHandshakeDatagrams.Reset();
		SetState(EACESessionState::Failed);
		return;
	}

	// Cleartext NAK: retransmit cached C2S — do not advance S2C LastReceived (ACE early-return).
	const bool bCleartextNak = EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::RequestRetransmit)
		&& !EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::EncryptedChecksum);
	if (bCleartextNak)
	{
		HandleServerRequestRetransmit(RetransmitSequences);
		return;
	}

	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::Disconnect))
	{
		Log(TEXT("Server sent Disconnect"));
		bRecoverLostConnection = State == EACESessionState::InWorld;
		ConnectionError = TEXT("The connection to the server was lost.");
		SetState(EACESessionState::Failed);
		return;
	}

	// Server AckSequence optional: prune C2S send cache (order-insensitive, like ACE).
	if (bHasServerAck)
	{
		TArray<uint32> ToRemove;
		for (const auto& Pair : CachedC2SPackets)
		{
			if (Pair.Key < ServerAckSequence)
			{
				ToRemove.Add(Pair.Key);
			}
		}
		for (uint32 Seq : ToRemove)
		{
			CachedC2SPackets.Remove(Seq);
		}
	}

	const bool bAckOnly = Flags == EACEPacketHeaderFlags::AckSequence;
	if (Sequence == 0 || bAckOnly)
	{
		ProcessOrderedS2CPacket(Sequence, Flags, EchoClientTime, EchoResponseClientTime, Fragments, ServerTicks, ReceivedAt);
		return;
	}

	// Contiguous S2C reorder (mirror ACE.Server NetworkSession).
	if (Sequence <= LastReceivedPacketSequence)
	{
		return; // duplicate / already handled
	}

	const uint32 Desired = LastReceivedPacketSequence + 1;
	if (Sequence > Desired)
	{
		FPendingS2CPacket& Pending = OutOfOrderS2CPackets.FindOrAdd(Sequence);
		Pending.Flags = Flags;
		Pending.EchoClientTime = EchoClientTime;
		Pending.EchoResponseClientTime = EchoResponseClientTime;
		Pending.Fragments = MoveTemp(Fragments);
		Pending.ServerTicks = ServerTicks;
		Pending.ReceivedAt = ReceivedAt;

		RequestMissingS2CPackets(FPlatformTime::Seconds());
		return;
	}

	ProcessOrderedS2CPacket(Sequence, Flags, EchoClientTime, EchoResponseClientTime, Fragments, ServerTicks, ReceivedAt);
	DrainOutOfOrderS2C();
}

void FACESession::ProcessOrderedS2CPacket(uint32 Sequence, EACEPacketHeaderFlags Flags,
	float EchoClientTime, float EchoResponseClientTime, const TArray<FReceivedFragment>& Fragments, double ServerTicks, double ReceivedAt)
{
	if (EchoClientTime >= 0.f)
	{
		SendEchoResponse(EchoClientTime);
	}
	if (EchoResponseClientTime >= 0.f)
	{
		UpdateLinkStatusFromEcho(EchoResponseClientTime, ReceivedAt);
	}

	if (Sequence != 0 && Flags != EACEPacketHeaderFlags::AckSequence)
	{
		LastReceivedPacketSequence = Sequence;
		bNeedAck = true;
	}

	// TimeSync is also authenticated and applied in packet order.
	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::TimeSync))
	{
		ApplyServerTime(ServerTicks, ReceivedAt, TEXT("TimeSync"));
	}
	for (const FReceivedFragment& Fragment : Fragments) ProcessReceivedFragment(Fragment);
}

void FACESession::RequestMissingS2CPackets(double Now)
{
	if (OutOfOrderS2CPackets.IsEmpty() || Now - LastRequestForRetransmitTime < 1.0) return;
	uint32 Highest = LastReceivedPacketSequence;
	double Oldest = Now;
	for (const auto& Pair : OutOfOrderS2CPackets)
	{
		Highest = FMath::Max(Highest, Pair.Key);
		Oldest = FMath::Min(Oldest, Pair.Value.ReceivedAt);
	}
	// Allow normal UDP reordering, then retry even a single hole when no more
	// packets arrive. Waiting for three later packets could stall the world forever.
	if (Now - Oldest < .15) return;
	TArray<uint32> Missing;
	for (uint32 Seq = LastReceivedPacketSequence + 1; Seq < Highest && Missing.Num() < 115; ++Seq)
		if (!OutOfOrderS2CPackets.Contains(Seq)) Missing.Add(Seq);
	if (Missing.IsEmpty()) return;
	SendRequestRetransmit(Missing);
	LastRequestForRetransmitTime = Now;
}

void FACESession::DrainOutOfOrderS2C()
{
	while (const FPendingS2CPacket* Pending = OutOfOrderS2CPackets.Find(LastReceivedPacketSequence + 1))
	{
		const uint32 Seq = LastReceivedPacketSequence + 1;
		FPendingS2CPacket Copy = *Pending;
		OutOfOrderS2CPackets.Remove(Seq);
		ProcessOrderedS2CPacket(Seq, Copy.Flags, Copy.EchoClientTime, Copy.EchoResponseClientTime, Copy.Fragments, Copy.ServerTicks, Copy.ReceivedAt);
	}
}

void FACESession::ProcessReceivedFragment(const FReceivedFragment& Fragment)
{
	if (Fragment.Count == 1)
	{
		HandleGameMessage(Fragment.Data);
		return;
	}
	FPartialMessage& Partial = PartialFragments.FindOrAdd(Fragment.Sequence);
	if (Partial.Count != 0 && (Partial.Count != Fragment.Count || Partial.Queue != Fragment.Queue))
	{
		Log(TEXT("Inconsistent fragment metadata — dropped"));
		return;
	}
	Partial.Count = Fragment.Count;
	Partial.Queue = Fragment.Queue;
	// Retransmission never replaces previously authenticated fragment bytes.
	if (Partial.Parts.Contains(Fragment.Index)) return;
	Partial.Parts.Add(Fragment.Index, Fragment.Data);
	if (Partial.Parts.Num() != Partial.Count) return;
	TArray<uint8> Message;
	for (uint16 Index=0; Index<Partial.Count; ++Index)
	{
		const auto* Part = Partial.Parts.Find(Index);
		if (!Part) return;
		Message.Append(*Part);
	}
	PartialFragments.Remove(Fragment.Sequence);
	HandleGameMessage(Message);
}

void FACESession::SendRequestRetransmit(const TArray<uint32>& MissingSequences)
{
	if (MissingSequences.Num() == 0)
	{
		return;
	}
	FACEBinaryWriter Body;
	Body.WriteUInt32(static_cast<uint32>(MissingSequences.Num()));
	for (uint32 Seq : MissingSequences)
	{
		Body.WriteUInt32(Seq);
	}
	// Cleartext NAK — sequence does not advance (ACE FlushPackets isNak path).
	SendRawPacket(EACEPacketHeaderFlags::RequestRetransmit, Body.GetData(), {}, false, false, ClientId);
}

void FACESession::HandleServerRequestRetransmit(const TArray<uint32>& Sequences)
{
	TArray<uint32> Missing;
	for (uint32 Seq : Sequences)
	{
		if (const FCachedC2SPacket* Cached = CachedC2SPackets.Find(Seq))
		{
			// Rebuild wire packet with Retransmission flag; reuse original Isaac XOR.
			EACEPacketHeaderFlags Flags = Cached->Flags | EACEPacketHeaderFlags::Retransmission;
			TArray<uint8> Packet;
			Packet.SetNumZeroed(PacketHeaderSize);
			Packet.Append(Cached->Payload);
			const uint16 PayloadSize = static_cast<uint16>(Cached->Payload.Num());
			const uint16 Time = PacketIntervalAt(FPlatformTime::Seconds());
			constexpr uint16 Iteration = 1;
			const uint32 HHash = HeaderHash32(Seq, Flags, ClientId, Time, PayloadSize, Iteration);
			const uint32 PayloadHash = FACEHash32::Calculate(Cached->Payload);
			const uint32 FinalChecksum = HHash + (PayloadHash ^ Cached->IsaacXor);
			auto W32 = [&](int32 Off, uint32 V)
			{
				Packet[Off] = static_cast<uint8>(V & 0xFF);
				Packet[Off + 1] = static_cast<uint8>((V >> 8) & 0xFF);
				Packet[Off + 2] = static_cast<uint8>((V >> 16) & 0xFF);
				Packet[Off + 3] = static_cast<uint8>((V >> 24) & 0xFF);
			};
			auto W16 = [&](int32 Off, uint16 V)
			{
				Packet[Off] = static_cast<uint8>(V & 0xFF);
				Packet[Off + 1] = static_cast<uint8>((V >> 8) & 0xFF);
			};
			W32(0, Seq);
			W32(4, static_cast<uint32>(Flags));
			W32(8, FinalChecksum);
			W16(12, ClientId);
			W16(14, Time);
			W16(16, PayloadSize);
			W16(18, Iteration);
			if (SocketC2S && ServerC2SAddr.IsValid())
			{
				int32 Sent = 0;
				SocketC2S->SendTo(Packet.GetData(), Packet.Num(), Sent, *ServerC2SAddr);
			}
		}
		else
		{
			Missing.Add(Seq);
		}
	}
	if (Missing.Num() > 0)
	{
		FACEBinaryWriter Body;
		Body.WriteUInt32(static_cast<uint32>(Missing.Num()));
		for (uint32 Seq : Missing)
		{
			Body.WriteUInt32(Seq);
		}
		SendRawPacket(EACEPacketHeaderFlags::RejectRetransmit, Body.GetData(), {}, false, false, ClientId);
	}
}

void FACESession::CacheOutboundPacket(uint32 Sequence, EACEPacketHeaderFlags Flags, uint32 IsaacXor, const TArray<uint8>& PayloadAfterHeader)
{
	if (Sequence < 2)
	{
		return;
	}
	FCachedC2SPacket& Entry = CachedC2SPackets.FindOrAdd(Sequence);
	Entry.Flags = Flags;
	Entry.IsaacXor = IsaacXor;
	Entry.Payload = PayloadAfterHeader;
	// Bound cache — prune oldest if huge (server ACK normally clears).
	constexpr int32 MaxCached = 512;
	if (CachedC2SPackets.Num() > MaxCached)
	{
		uint32 MinSeq = MAX_uint32;
		for (const auto& Pair : CachedC2SPackets)
		{
			MinSeq = FMath::Min(MinSeq, Pair.Key);
		}
		CachedC2SPackets.Remove(MinSeq);
	}
}

uint32 FACESession::HeaderHash32(uint32 Sequence, EACEPacketHeaderFlags Flags, uint16 Id, uint16 Time, uint16 Size, uint16 Iteration)
{
	uint8 Buf[20];
	auto Write32 = [&](int32 Off, uint32 V)
	{
		Buf[Off] = V & 0xFF;
		Buf[Off + 1] = (V >> 8) & 0xFF;
		Buf[Off + 2] = (V >> 16) & 0xFF;
		Buf[Off + 3] = (V >> 24) & 0xFF;
	};
	auto Write16 = [&](int32 Off, uint16 V)
	{
		Buf[Off] = V & 0xFF;
		Buf[Off + 1] = (V >> 8) & 0xFF;
	};
	Write32(0, Sequence);
	Write32(4, static_cast<uint32>(Flags));
	Write32(8, HeaderChecksumMagic);
	Write16(12, Id);
	Write16(14, Time);
	Write16(16, Size);
	Write16(18, Iteration);
	return FACEHash32::Calculate(Buf, 20);
}

void FACESession::HandleConnectRequest(FACEBinaryReader& Body)
{
	const double ServerTime = Body.ReadDouble();
	// ACE sends Timers.PortalYearTicks (Dereth game seconds) — drives the client sky day cycle.
	ApplyServerTime(ServerTime, FPlatformTime::Seconds(), TEXT("ConnectRequest"));
	ConnectionCookie = Body.ReadUInt64();
	ClientId = static_cast<uint16>(Body.ReadUInt32());
	for (int32 i = 0; i < 4; ++i) ServerSeed[i] = Body.ReadUInt8();
	for (int32 i = 0; i < 4; ++i) ClientSeed[i] = Body.ReadUInt8();

	IssacServer = MakeUnique<FACECryptoSystem>(ServerSeed);
	IssacClient = MakeUnique<FACEIsaac>(ClientSeed);

	Log(FString::Printf(TEXT("ConnectRequest: ClientId=%u Cookie=%llu ServerTime=%f"), ClientId, ConnectionCookie, ServerTime));

	ConnectResponseRetryTimer = 0.f;
	CharacterListWaitTimer = 0.f;
	ConnectResponseRetriesSent = 0;
	SendConnectResponse();
	SetState(EACESessionState::AwaitCharacterList);
}

TArray<uint8> FACESession::BuildLoginRequestBody(const FACELoginCredentials& Creds)
{
	FACEBinaryWriter Body;
	Body.WriteString16L(TEXT("1802"));

	FACEBinaryWriter Auth;
	Auth.WriteUInt32(Creds.bGDLE ? 1 : 2); // NetAuthType::Account / AccountPassword
	Auth.WriteUInt32(0); // AuthFlags
	Auth.WriteUInt32(0); // Timestamp
	if (Creds.bGDLE)
	{
		// ThwargLauncher passes GDLE credentials via retail's -a argument.
		// Client::EvaluateCommandLineArg lowercases that entire argument (including
		// the password) before NetAuthenticator packs it. Match its ASCII casing
		// on the wire only; never alter saved credentials or ACE's case-sensitive ticket.
		FString RetailAccount = Creds.Account + TEXT(":") + Creds.Password;
		for (TCHAR& C : RetailAccount) if (C >= TEXT('A') && C <= TEXT('Z')) C += TEXT('a') - TEXT('A');
		Auth.WriteString16L(RetailAccount);
		Auth.WriteUInt32(0); // NetAuthenticator crypto-data length
		Auth.WriteUInt32(0); // NetAuthenticator extra-data length
	}
	else
	{
		Auth.WriteString16L(Creds.Account);
		Auth.WriteString16L(TEXT("")); // account override
		Auth.WriteString32L(Creds.Password);
	}

	Body.WriteUInt32(static_cast<uint32>(Auth.Num()));
	Body.WriteBytes(Auth.GetData());

	return Body.GetData();
}

void FACESession::SendLoginRequest()
{
	SendRawPacket(EACEPacketHeaderFlags::LoginRequest, BuildLoginRequestBody(Creds), {}, false, false, 0);
}

void FACESession::SendConnectResponse()
{
	FACEBinaryWriter Body;
	Body.WriteUInt64(ConnectionCookie);
	// GDLE routes the acknowledgement by its assigned recipient ID. ACE instead
	// resolves the cookie on port+1 and expects the existing zero-ID response.
	SendRawPacket(EACEPacketHeaderFlags::ConnectResponse, Body.GetData(), {}, true, false, Creds.bGDLE ? ClientId : 0);
	Log(TEXT("ConnectResponse sent on port+1"));
}

uint16 FACESession::PacketIntervalAt(double Now) const
{
	// Retail FlowQueue advances interval_ once every 0.5 seconds. GDLE's
	// speed-hack check compares this counter with elapsed wall time. CPU cycles
	// are unrelated and can make ordinary bursts of item actions look accelerated.
	if (PacketTimeOrigin <= 0.0) return 0;
	const uint64 Intervals = static_cast<uint64>(FMath::Max(0.0, Now - PacketTimeOrigin) * 2.0);
	return static_cast<uint16>(Intervals & 0xFFFFu);
}

void FACESession::SendRawPacket(
	EACEPacketHeaderFlags Flags,
	const TArray<uint8>& Body,
	const TArray<TArray<uint8>>& Fragments,
	bool bToS2CPort,
	bool bEncrypted,
	uint16 HeaderId)
{
	FSocket* Sock = bToS2CPort ? SocketS2C : SocketC2S;
	TSharedPtr<FInternetAddr> Dest = bToS2CPort ? ServerS2CAddr : ServerC2SAddr;
	if (!Sock || !Dest.IsValid())
	{
		return;
	}

	if (Fragments.Num() > 0)
	{
		Flags |= EACEPacketHeaderFlags::BlobFragments;
	}
	if (bEncrypted)
	{
		Flags |= EACEPacketHeaderFlags::EncryptedChecksum;
	}

	uint32 IsaacXor = 0;
	if (bEncrypted)
	{
		if (!IssacClient)
		{
			return;
		}
		IsaacXor = IssacClient->Next();
	}

	uint32 Sequence = 0;
	const bool bIsNak = EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::RequestRetransmit);
	// Mirror ACE.Server FlushPackets: only exact AckSequence (no other flags) or NAK
	// reuse CurrentValue without consuming a sequence slot.
	if (EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::LoginRequest) ||
		EnumHasAnyFlags(Flags, EACEPacketHeaderFlags::ConnectResponse))
	{
		Sequence = 0;
	}
	else if (Flags == EACEPacketHeaderFlags::AckSequence || bIsNak)
	{
		Sequence = NextPacketSequence;
	}
	else
	{
		Sequence = NextPacketSequence++;
	}

	const uint16 Time = PacketIntervalAt(FPlatformTime::Seconds());
	constexpr uint16 Iteration = 1;

	TArray<uint8> Packet;
	Packet.SetNumZeroed(PacketHeaderSize);

	uint32 PayloadHash = 0;
	if (Body.Num() > 0)
	{
		Packet.Append(Body);
		PayloadHash += FACEHash32::Calculate(Body);
	}
	for (const TArray<uint8>& FullFrag : Fragments)
	{
		Packet.Append(FullFrag);
		PayloadHash += FACEHash32::Calculate(FullFrag);
	}

	const uint16 PayloadSize = static_cast<uint16>(Packet.Num() - PacketHeaderSize);
	const uint32 HHash = HeaderHash32(Sequence, Flags, HeaderId, Time, PayloadSize, Iteration);
	const uint32 FinalChecksum = HHash + (PayloadHash ^ IsaacXor);

	auto W32 = [&](int32 Off, uint32 V)
	{
		Packet[Off] = static_cast<uint8>(V & 0xFF);
		Packet[Off + 1] = static_cast<uint8>((V >> 8) & 0xFF);
		Packet[Off + 2] = static_cast<uint8>((V >> 16) & 0xFF);
		Packet[Off + 3] = static_cast<uint8>((V >> 24) & 0xFF);
	};
	auto W16 = [&](int32 Off, uint16 V)
	{
		Packet[Off] = static_cast<uint8>(V & 0xFF);
		Packet[Off + 1] = static_cast<uint8>((V >> 8) & 0xFF);
	};
	W32(0, Sequence);
	W32(4, static_cast<uint32>(Flags));
	W32(8, FinalChecksum);
	W16(12, HeaderId);
	W16(14, Time);
	W16(16, PayloadSize);
	W16(18, Iteration);

	// Cache for server NAK retransmit (seq >= 2, not NAK itself).
	if (Sequence >= 2 && !bIsNak)
	{
		TArray<uint8> PayloadAfterHeader;
		PayloadAfterHeader.Append(Packet.GetData() + PacketHeaderSize, PayloadSize);
		CacheOutboundPacket(Sequence, Flags, IsaacXor, PayloadAfterHeader);
	}

	int32 Sent = 0;
	Sock->SendTo(Packet.GetData(), Packet.Num(), Sent, *Dest);
	if (Sent == Packet.Num()) RecordLinkTraffic(FPlatformTime::Seconds(), 1, 0);
}

void FACESession::SendGameMessage(uint32 Opcode, const TArray<uint8>& PayloadAfterOpcode, uint16 Queue, bool bEncrypted)
{
	FACEBinaryWriter Msg;
	Msg.WriteUInt32(Opcode);
	Msg.WriteBytes(PayloadAfterOpcode);

	const TArray<uint8>& Bytes = Msg.GetData();
	const int32 TotalLen = Bytes.Num();
	const uint16 Count = static_cast<uint16>(FMath::Max(1, (TotalLen + MaxFragmentDataSize - 1) / MaxFragmentDataSize));
	const uint32 FragSeq = NextFragmentSequence++;

	for (uint16 Index = 0; Index < Count; ++Index)
	{
		const int32 Offset = Index * MaxFragmentDataSize;
		const int32 DataLen = FMath::Min(MaxFragmentDataSize, TotalLen - Offset);

		FACEBinaryWriter Frag;
		Frag.WriteUInt32(FragSeq);
		Frag.WriteUInt32(0x80000000u);
		Frag.WriteUInt16(Count);
		Frag.WriteUInt16(static_cast<uint16>(FragmentHeaderSize + DataLen));
		Frag.WriteUInt16(Index);
		Frag.WriteUInt16(Queue);
		Frag.WriteBytes(Bytes.GetData() + Offset, DataLen);
		// FlowQueue::EnqueueBlob limits the payload to 0x1D0 (464) bytes.
		// Each full 448-byte fragment therefore needs its own UDP packet.
		SendRawPacket(EACEPacketHeaderFlags::None, {}, {Frag.GetData()}, false, bEncrypted, ClientId);
	}

}

void FACESession::SendGameAction(uint32 ActionType, const TArray<uint8>& ActionPayload, uint16 Queue)
{
	FACEBinaryWriter Inner;
	Inner.WriteUInt32(NextGameActionSequence++);
	Inner.WriteUInt32(ActionType);
	Inner.WriteBytes(ActionPayload);
	SendGameMessage(ACEOpcode::GameAction, Inner.GetData(), Queue, true);
}

void FACESession::SendAckIfNeeded()
{
	if (!bNeedAck || !IssacClient)
	{
		return;
	}
	FACEBinaryWriter Body;
	Body.WriteUInt32(LastReceivedPacketSequence);
	SendRawPacket(EACEPacketHeaderFlags::AckSequence | EACEPacketHeaderFlags::EncryptedChecksum,
		Body.GetData(), {}, false, true, ClientId);
	bNeedAck = false;
}

void FACESession::SendEchoResponse(float ClientTime)
{
	FACEBinaryWriter Body;
	Body.WriteFloat(ClientTime);
	Body.WriteFloat(0.f);
	SendRawPacket(EACEPacketHeaderFlags::EchoResponse | EACEPacketHeaderFlags::EncryptedChecksum,
		Body.GetData(), {}, false, true, ClientId);
}

void FACESession::HandleGameMessage(const TArray<uint8>& MessageBytes)
{
	if (MessageBytes.Num() < 4)
	{
		return;
	}
	FACEBinaryReader Reader(MessageBytes);
	const uint32 Opcode = Reader.ReadUInt32();

	switch (Opcode)
	{
	case 0xF643:
		if (PendingCharacterMutation && bRestoringCharacter) HandleCharacterRestored(Reader);
		else HandleCharacterCreated(Reader);
		break;
	case 0xF655:
		// Delete acknowledgement has no roster. Wait for CharacterList.
		break;
	case ACEOpcode::CharacterList:
		HandleCharacterList(Reader);
		break;
	case ACEOpcode::CharacterLogOff:
		// Server ack after FinalizeLogout — CharacterList follows shortly.
		Log(TEXT("CharacterLogOff (server)"));
		break;
	case ACEOpcode::CharacterError:
	{
		if (Reader.Remaining() < 4) break;
		const uint32 ErrorCode = Reader.ReadUInt32();
		if (State == EACESessionState::CharacterSelect && PendingCharacterMutation)
		{
			CharacterManagementError = FString::Printf(TEXT("The server could not %s this character (error %u)."),
				bRestoringCharacter ? TEXT("restore") : TEXT("delete"), ErrorCode);
			PendingCharacterMutation = 0;
			break;
		}
		ConnectionError = ACELoginErrors::Character(ErrorCode);
		Log(FString::Printf(TEXT("CharacterError code=%u - login rejected by server"), ErrorCode));
		if (State == EACESessionState::EnteringWorld && PlayerGuid == 0)
		{
			PendingEnterCharacterId = 0;
			bEnteredWorldSent = false;
			SetState(EACESessionState::CharacterSelect);
			OnCharacterList.Broadcast(Characters, ServerName);
		}
		else
		{
			SetState(EACESessionState::Failed);
		}
		break;
	}
	case ACEOpcode::AccountBoot:
	{
		FString Reason = TEXT("(no reason)");
		if (Reader.Remaining() >= 2)
		{
			Reason = Reader.ReadString16L();
		}
		ConnectionError = FString::Printf(TEXT("Login rejected: %s"), *Reason.TrimStartAndEnd());
		Log(ConnectionError);
		SetState(EACESessionState::Failed);
		break;
	}
	case ACEOpcode::ServerName:
	{
		Reader.ReadUInt32(); // current connections
		Reader.ReadUInt32(); // max connections
		ServerName = Reader.ReadString16L();
		Log(FString::Printf(TEXT("ServerName: %s"), *ServerName));
		break;
	}
	case ACEOpcode::CharacterEnterWorldServerReady:
		// CPlayerSystem::LogOnCharacter waits for fReadyToEnterGame before F657.
		if (State == EACESessionState::EnteringWorld && PendingEnterCharacterId != 0 && !bEnteredWorldSent)
		{
			FACEBinaryWriter Payload;
			Payload.WriteUInt32(static_cast<uint32>(PendingEnterCharacterId));
			Payload.WriteString16L(AccountName);
			bEnteredWorldSent = true;
			SendGameMessage(ACEOpcode::CharacterEnterWorld, Payload.GetData(), ACEQueue::UIQueue, true);
			PendingEnterCharacterId = 0;
		}
		Log(TEXT("CharacterEnterWorldServerReady"));
		break;
	case ACEOpcode::ObjectCreate:
		HandleObjectCreate(Reader);
		break;
	case ACEOpcode::UpdateObject:
		// Same payload as ObjectCreate (SerializeUpdateObject). Required for house hooks
		// morphing into hooked items (Font of Jojii Setup/DefaultScript swap).
		HandleObjectCreate(Reader);
		break;
	case ACEOpcode::ObjDescEvent:
		HandleObjDescEvent(Reader);
		break;
	case ACEOpcode::ObjectDelete:
		HandleObjectDelete(Reader);
		break;
	case ACEOpcode::PlayerCreate:
		HandlePlayerCreate(Reader);
		break;
	case ACEOpcode::UpdatePosition:
		HandleUpdatePosition(Reader);
		break;
	case ACEOpcode::PickupEvent:
		HandlePickupEvent(Reader);
		break;
	case ACEOpcode::ParentEvent:
		HandleParentEvent(Reader);
		break;
	case ACEOpcode::UpdateMotion:
		HandleUpdateMotion(Reader);
		break;
	case ACEOpcode::VectorUpdate:
		HandleVectorUpdate(Reader);
		break;
	case ACEOpcode::SetState:
		HandleSetState(Reader);
		break;
	case ACEOpcode::Sound:
		HandleSound(Reader);
		break;
	case ACEOpcode::PlayerTeleport:
		HandlePlayerTeleport(Reader);
		break;
	case ACEOpcode::PlayScriptId:
		HandlePlayScriptId(Reader);
		break;
	case ACEOpcode::PlayEffect:
		HandlePlayEffect(Reader);
		break;
	case ACEOpcode::ServerMessage:
		HandleServerMessage(Reader);
		break;
	case ACEOpcode::HearSpeech:
		HandleHearSpeech(Reader);
		break;
	case ACEOpcode::HearRangedSpeech:
		HandleHearRangedSpeech(Reader);
		break;
	case ACEOpcode::SoulEmote:
		HandleSoulEmote(Reader);
		break;
	case ACEOpcode::TurbineChat:
		HandleTurbineChat(Reader);
		break;
	case ACEOpcode::GameEvent:
		HandleGameEvent(Reader);
		break;
	case ACEOpcode::PrivateUpdateAttribute:
		HandlePrivateUpdateAttribute(Reader);
		break;
	case ACEOpcode::PrivateUpdateVital:
		HandlePrivateUpdateVital(Reader);
		break;
	case ACEOpcode::PrivateUpdateAttribute2ndLevel:
		HandlePrivateUpdateAttribute2ndLevel(Reader);
		break;
	case ACEOpcode::PrivateUpdateSkill:
		HandlePrivateUpdateSkill(Reader);
		break;
	case ACEOpcode::PrivateUpdatePropertyInt:
		HandlePrivateUpdatePropertyInt(Reader);
		break;
	case ACEOpcode::PrivateUpdatePropertyInt64:
		HandlePrivateUpdatePropertyInt64(Reader);
		break;
	case ACEOpcode::PublicUpdatePropertyInt:
		HandlePublicUpdatePropertyInt(Reader);
		break;
	case ACEOpcode::PrivateUpdatePropertyDataID:
		HandlePrivateUpdatePropertyDataID(Reader);
		break;
	case ACEOpcode::PublicUpdatePropertyDataID:
		HandlePublicUpdatePropertyDataID(Reader);
		break;
	case ACEOpcode::PublicUpdateInstanceId:
		HandlePublicUpdateInstanceId(Reader);
		break;
	case ACEOpcode::InventoryRemoveObject:
		HandleInventoryRemoveObject(Reader);
		break;
	case ACEOpcode::SetStackSize:
		HandleSetStackSize(Reader);
		break;
	case ACEOpcode::DDD_Interrogation:
		Log(TEXT("DDD_Interrogation received (ignored — disable DAT patching on server for UE clients)"));
		break;
	case ACEOpcode::DDD_EndDDD:
		Log(TEXT("DDD_EndDDD"));
		break;
	default:
		break;
	}
}

void FACESession::HandleCharacterList(FACEBinaryReader& Reader)
{
	const bool bReturningFromWorld =
		bLogOffPending
		|| State == EACESessionState::InWorld
		|| State == EACESessionState::EnteringWorld;

	Reader.ReadUInt32(); // 0
	const uint32 Count = Reader.ReadUInt32();
	Characters.Reset();
	for (uint32 i = 0; i < Count; ++i)
	{
		FACECharacterInfo Info;
		Info.CharacterId = static_cast<int32>(Reader.ReadUInt32());
		Info.Name = Reader.ReadString16L();
		Info.DeleteSeconds = static_cast<int32>(Reader.ReadUInt32());
		Characters.Add(Info);
	}
	Reader.ReadUInt32(); // 0
	CharacterSlotCount = FMath::Clamp(static_cast<int32>(Reader.ReadUInt32()), 1, 100);
	PendingCharacterMutation = 0;
	CharacterManagementError.Reset();
	bCharacterCreationPending = false;
	AccountName = Reader.ReadString16L();

	if (bReturningFromWorld)
	{
		ClearWorldState();
		bLogOffPending = false;
		LogOffTimeout = 0.f;
		LogOffRetransmitTimer = 0.f;
		Log(TEXT("Logged off — returned to character select"));
	}

	SetState(EACESessionState::CharacterSelect);
	Log(FString::Printf(TEXT("Character list: %d character(s)"), Characters.Num()));
	OnCharacterList.Broadcast(Characters, ServerName);
}

void FACESession::HandlePlayerCreate(FACEBinaryReader& Reader)
{
	PlayerGuid = static_cast<int32>(Reader.ReadUInt32());
	Log(FString::Printf(TEXT("PlayerCreate guid=0x%08X"), PlayerGuid));
	ApplyPlayerInventoryProfile();
	if (FACEWorldObject* Existing = WorldObjects.Find(PlayerGuid))
	{
		Existing->bIsSelf = true;
		Existing->bIsPlayer = true;
	}
	if (!bLoginCompleteSent)
	{
		SendLoginComplete();
	}
	MaybeEnterWorldComplete();
}

void FACESession::UpsertWorldObject(const FACEWorldObject& Object)
{
	FACEWorldObject Merged = Object;
	if (const FACEWorldObject* Existing = WorldObjects.Find(Object.Guid))
	{
		// CreateObject (SmartboxQueue) and ContainId (UIQueue) can reorder. If ContainId
		// homed an item first, a later CreateObject that omits Container must not wipe it
		// — that dropped newly created salvage bags out of GetPackItems.
		if (Merged.ContainerId == 0 && Existing->ContainerId != 0
			&& Merged.ParentGuid == 0 && Merged.WielderId == 0
			&& Merged.CurrentWieldedLocation == 0)
		{
			Merged.ContainerId = Existing->ContainerId;
		}
		// A repeated description of the same dead creature is not a respawn.
		const bool bSameInstance = !Merged.bHasPhysicsTimestamps || !Existing->bHasPhysicsTimestamps
			|| Merged.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance] == Existing->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance];
		if (Existing->bDying && !Merged.bIsPlayer && bSameInstance)
		{
			Merged.bDying = true;
			Merged.InitialMotionCommand = ACEMotion::Dead;
			Merged.InitialMotionStyle = ACEMotion::StanceNonCombat;
		}
		if (Merged.PlacementPosition < 0 && Existing->PlacementPosition >= 0)
		{
			Merged.PlacementPosition = Existing->PlacementPosition;
		}
        // An equipment profile/wield notification can precede the mesh description.
        if (!Merged.bHasPosition && !Merged.ContainerId && !Merged.CurrentWieldedLocation
            && Existing->WielderId && Existing->CurrentWieldedLocation
            && (!Merged.WielderId || Merged.WielderId == Existing->WielderId)
            && (!Merged.ParentGuid || Merged.ParentGuid == Existing->WielderId))
        {
            Merged.WielderId = Existing->WielderId;
            Merged.CurrentWieldedLocation = Existing->CurrentWieldedLocation;
            Merged.ClothingPriority = Existing->ClothingPriority;
        }
		if (Merged.MaterialType == 0 && Existing->MaterialType != 0)
		{
			Merged.MaterialType = Existing->MaterialType;
		}
		if (Merged.IconId == 0 && Existing->IconId != 0)
		{
			Merged.IconId = Existing->IconId;
		}
		if (Merged.IconUnderlayId == 0 && Existing->IconUnderlayId != 0)
		{
			Merged.IconUnderlayId = Existing->IconUnderlayId;
		}
		if (Merged.SpellDID == 0 && Existing->SpellDID != 0)
		{
			Merged.SpellDID = Existing->SpellDID;
		}
		if (Merged.IconOverlayId == 0 && Existing->IconOverlayId != 0)
		{
			Merged.IconOverlayId = Existing->IconOverlayId;
		}
		if (Merged.UiEffects == 0 && Existing->UiEffects != 0)
		{
			Merged.UiEffects = Existing->UiEffects;
		}
		if (Merged.Name.IsEmpty() && !Existing->Name.IsEmpty())
		{
			Merged.Name = Existing->Name;
		}
	}
	// CreateObject has no PropertyInt.PlacementPosition. Retail fills the pack from
	// PlayerDescription ContentProfile order (ACE OrderBy PlacementPosition, densified
	// 0..n-1 on load). Stamp that index when ObjectCreate arrives after the footer.
	if (Merged.PlacementPosition < 0 && PlayerGuid != 0)
	{
		if (const TArray<FACEContainerItemRef>* Contents = ContainerContents.Find(Merged.ContainerId))
		{
			int32 MainI = 0;
			int32 PackI = 0;
			for (const FACEContainerItemRef& Ref : *Contents)
			{
				if (Ref.ItemGuid == Merged.Guid)
				{
					Merged.PlacementPosition = (Ref.ContainerType == 0) ? MainI : PackI;
					break;
				}
				if (Ref.ContainerType == 0)
				{
					++MainI;
				}
				else
				{
					++PackI;
				}
			}
		}
	}
	WorldObjects.Add(Merged.Guid, Merged);
	OnObjectCreated.Broadcast(Merged);
}

void FACESession::MaybeEnterWorldComplete()
{
	if (State != EACESessionState::EnteringWorld || PlayerGuid == 0 || !PlayerPosition.IsValid())
	{
		return;
	}
	SetState(EACESessionState::InWorld);

	{
		constexpr float WorldScale = 100.f;
		const FVector Ue = PlayerPosition.ToUnrealLocation(WorldScale);
		Log(FString::Printf(
			TEXT("EnterWorld place cell=0x%08X local=(%.2f,%.2f,%.2f) unreal=(%.0f,%.0f,%.0f) heading=%.1f°"),
			PlayerPosition.CellId,
			PlayerPosition.Location.X, PlayerPosition.Location.Y, PlayerPosition.Location.Z,
			Ue.X, Ue.Y, Ue.Z,
			PlayerPosition.GetAceHeadingDegrees()));
	}

	OnEnteredWorld.Broadcast(PlayerGuid, PlayerPosition);
}

void FACESession::HandleObjectCreate(FACEBinaryReader& Reader)
{
	FACEDecodedObject Decoded;
	if (!FACEObjectCreateParser::Parse(Reader, Decoded) || !Decoded.bParseOk)
	{
		Log(FString::Printf(TEXT("ObjectCreate parse failed guid=0x%08X isSelf=%d"),
			Decoded.Guid, (PlayerGuid != 0 && Decoded.Guid == PlayerGuid) ? 1 : 0));
		return;
	}

	FACEWorldObject Obj;
	Obj.Guid = Decoded.Guid;
	Obj.Name = Decoded.Name;
	Obj.WeenieClassId = Decoded.WeenieClassId;
	Obj.IconId = Decoded.IconId;
	Obj.IconOverlayId = Decoded.IconOverlayId;
	Obj.IconUnderlayId = Decoded.IconUnderlayId;
	Obj.UiEffects = static_cast<int32>(Decoded.UiEffects);
	Obj.ContainerId = Decoded.ContainerId;
	Obj.WielderId = Decoded.WielderId;
	Obj.MonarchGuid = Decoded.MonarchGuid;
	Obj.Value = Decoded.Value;
	Obj.Burden = Decoded.Burden;
	Obj.ContainersCapacity = Decoded.ContainersCapacity;
	Obj.ItemsCapacity = Decoded.ItemsCapacity;
	if (PlayerGuid != 0 && Decoded.Guid == PlayerGuid && Decoded.Burden > 0)
	{
		// ObjectCreate Burden on the player weenie is EncumbranceVal until PrivateUpdate arrives.
		bHasPlayerEncumbrance = true;
		PlayerEncumbranceVal = Decoded.Burden;
	}
	Obj.StackSize = Decoded.StackSize;
	Obj.MaxStackSize = Decoded.MaxStackSize;
	Obj.Structure = Decoded.Structure;
	Obj.MaxStructure = Decoded.MaxStructure;
	Obj.SpellDID = Decoded.SpellDID;
	Obj.ItemUseable = Decoded.ItemUseable;
	Obj.TargetType = Decoded.TargetType;
	Obj.MaterialType = Decoded.MaterialType;
	if (Obj.MaxStackSize > 0 && Obj.StackSize > Obj.MaxStackSize)
	{
		Obj.StackSize = Obj.MaxStackSize;
	}
	Obj.SetupId = Decoded.SetupId;
	Obj.MotionTableId = Decoded.MotionTableId;
	Obj.SoundTableId = Decoded.SoundTableId;
	Obj.PhysicsEffectTableId = Decoded.PhysicsEffectTableId;
	Obj.ItemType = Decoded.ItemType;
	Obj.Scale = Decoded.Scale > 0.f ? Decoded.Scale : 1.f;
	Obj.Position = Decoded.Position;
	Obj.bHasPosition = Decoded.bHasPosition;
	Obj.bIsPlayer = Decoded.bIsPlayer;
	Obj.bIsSelf = (PlayerGuid != 0 && Decoded.Guid == PlayerGuid);
	if (Obj.bIsSelf && (Obj.ObjectDescriptionFlags & ACEObjectDescFlag::Admin) != 0)
	{
		bLocalPlayerIsAdmin = true;
	}
	Obj.ParentGuid = Decoded.ParentGuid;
	Obj.ParentLocation = Decoded.ParentLocation;
	Obj.PlacementId = Decoded.PlacementId;
	Obj.ClothingPriority = Decoded.ClothingPriority;
	Obj.CurrentWieldedLocation = static_cast<int64>(Decoded.CurrentWieldedLocation);
	Obj.ValidLocations = static_cast<int64>(Decoded.ValidLocations);
	Obj.ObjectDescriptionFlags = Decoded.ObjectDescriptionFlags;
	Obj.PhysicsState = Decoded.PhysicsState;
	Obj.UseRadius = Decoded.UseRadius;
	Obj.AmmoType = Decoded.AmmoType;
	Obj.CombatUse = Decoded.CombatUse;
	Obj.RadarBlipColor = Decoded.RadarBlipColor;
	Obj.RadarBehavior = Decoded.RadarBehavior;
	Obj.Translucency = Decoded.Translucency;
	Obj.Omega = Decoded.Omega;
	Obj.Velocity = Decoded.Velocity;
	Obj.bHasVelocity = Decoded.bHasVelocity;
	Obj.DefaultScriptId = Decoded.DefaultScriptId;
	Obj.DefaultScriptIntensity = Decoded.DefaultScriptIntensity;
	Obj.InitialMotionCommand = Decoded.InitialMotionCommand;
	Obj.InitialMotionStyle = Decoded.InitialMotionStyle;
	Obj.bDying = !Obj.IsCorpse() && ACEMotion::NormalizeCommand(Obj.InitialMotionCommand) == ACEMotion::Dead;
	Obj.Appearance = Decoded.Appearance;
	if (Decoded.bHasPhysicsTimestamps)
	{
		FMemory::Memcpy(Obj.PhysicsTimestamps, Decoded.PhysicsTimestamps, sizeof(Obj.PhysicsTimestamps));
		Obj.bHasPhysicsTimestamps = true;
	}

	if (Obj.bIsSelf && Obj.bHasPosition)
	{
		PlayerPosition = Obj.Position;
	}

	UpsertWorldObject(Obj);
	Log(FString::Printf(TEXT("ObjectCreate '%s' guid=0x%08X setup=0x%08X parent=0x%08X loc=%d animParts=%d"),
		*Obj.Name, Obj.Guid, Obj.SetupId, Obj.ParentGuid, Obj.ParentLocation,
		Obj.Appearance.AnimPartChanges.Num()));
	MaybeEnterWorldComplete();
}

void FACESession::HandleObjDescEvent(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	FACEObjDesc Appearance;
	if (!FACEObjectCreateParser::ParseModelData(Reader, Appearance))
	{
		Log(TEXT("ObjDescEvent parse failed"));
		return;
	}
	uint16 IncomingInstance = 0;
	uint16 IncomingVisual = 0;
	if (Reader.CanRead(4))
	{
		IncomingInstance = Reader.ReadUInt16();
		IncomingVisual = Reader.ReadUInt16();
	}
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // optional pad / unused
	}

	FACEWorldObject* Existing = WorldObjects.Find(Guid);
	if (!Existing)
	{
		FACEWorldObject Stub;
		Stub.Guid = Guid;
		Stub.Appearance = Appearance;
		Stub.bIsSelf = (PlayerGuid != 0 && Guid == PlayerGuid);
		if (IncomingVisual != 0 || IncomingInstance != 0)
		{
			Stub.PhysicsTimestamps[ACEPhysicsTimeStamp::ObjDesc] = IncomingVisual;
			Stub.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance] = IncomingInstance;
			Stub.bHasPhysicsTimestamps = true;
		}
		UpsertWorldObject(Stub);
		return;
	}

	if (Existing->bHasPhysicsTimestamps)
	{
		if (!ACEPhysicsTimeStamp::IsNewer(Existing->PhysicsTimestamps[ACEPhysicsTimeStamp::ObjDesc], IncomingVisual)
			&& !ACEPhysicsTimeStamp::IsNewer(Existing->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance], IncomingInstance))
		{
			return;
		}
		if (ACEPhysicsTimeStamp::IsNewer(Existing->PhysicsTimestamps[ACEPhysicsTimeStamp::ObjDesc], IncomingVisual))
		{
			Existing->PhysicsTimestamps[ACEPhysicsTimeStamp::ObjDesc] = IncomingVisual;
		}
		if (ACEPhysicsTimeStamp::IsNewer(Existing->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance], IncomingInstance))
		{
			Existing->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance] = IncomingInstance;
		}
	}

	Existing->Appearance = Appearance;
	Existing->bIsSelf = (PlayerGuid != 0 && Guid == PlayerGuid);
	FACEWorldObject VisualUpdate = *Existing; VisualUpdate.bAppearanceOnlyUpdate = true;
	OnObjectCreated.Broadcast(VisualUpdate);
	Log(FString::Printf(TEXT("ObjDescEvent guid=0x%08X animParts=%d"), Guid, Appearance.AnimPartChanges.Num()));
}

void FACESession::HandleObjectDelete(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	// ObjectDelete = leave the 3D world. Keep WorldObjects only for items the player still
	// owns (main pack / side pack / wielded). Do NOT keep corpse/chest ViewContents rows or
	// arbitrary ContainerId — that hid lifestones/chests after false inventory parses.
	bool bKeepInventoryRecord = false;
	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		bool bInPlayerPack = (PlayerGuid != 0 && Obj->ContainerId == PlayerGuid);
		if (!bInPlayerPack && Obj->ContainerId != 0 && PlayerGuid != 0)
		{
			if (const TArray<FACEContainerItemRef>* Root = ContainerContents.Find(PlayerGuid))
			{
				for (const FACEContainerItemRef& Ref : *Root)
				{
					if (Ref.ContainerType != 0 && Ref.ItemGuid == Obj->ContainerId)
					{
						bInPlayerPack = true;
						break;
					}
				}
			}
			if (!bInPlayerPack)
			{
				if (const FACEWorldObject* Pack = WorldObjects.Find(Obj->ContainerId))
				{
					bInPlayerPack = Pack->ContainerId == PlayerGuid
						&& (Pack->ItemsCapacity > 0 || Pack->ContainersCapacity > 0
							|| (Pack->ItemType & ACEItemType::Container) != 0);
				}
			}
		}
		if (bInPlayerPack || Obj->WielderId == PlayerGuid
			|| (PlayerGuid != 0 && Obj->ParentGuid == PlayerGuid))
		{
			bKeepInventoryRecord = true;
			Obj->bHasPosition = false;
			Obj->ParentGuid = 0;
			Obj->ParentLocation = 0;
		}
	}
	if (!bKeepInventoryRecord)
	{
		if (SelectedObject.Guid == Guid) SelectObject(0);
		VRPoses.Remove(Guid);
		WorldObjects.Remove(Guid);
		RemoveFromContainerLists(Guid);
	}
	OnObjectDeleted.Broadcast(Guid);
}

void FACESession::HandleSound(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(12))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const int32 SoundType = static_cast<int32>(Reader.ReadUInt32());
	const float Volume = Reader.ReadFloat();
	OnSound.Broadcast(Guid, SoundType, Volume);
}

void FACESession::HandlePlayerTeleport(FACEBinaryReader& Reader)
{
	// GameMessagePlayerTeleport: ObjectTeleport sequence + align.
	// Destination UpdatePosition (0xF748) carries the TeleportSeq the controller watches;
	// this message is the retail "enter portal space" cue that accompanies it.
	if (!Reader.CanRead(2))
	{
		return;
	}
	const uint16 ObjectTeleportSeq = Reader.ReadUInt16();
	Reader.Align();
	Log(FString::Printf(TEXT("PlayerTeleport seq=%u"), ObjectTeleportSeq));
	SelectObject(0);
	// Retail SmartBox::HandlePlayerTeleport enters portal space on this message —
	// before the destination UpdatePosition arrives. Mirror that so the tunnel is up
	// instantly and the old world never flashes.
	OnPlayerTeleportStarted.Broadcast();
}

void FACESession::HandlePlayScriptId(FACEBinaryReader& Reader)
{
	// Explicit PhysicsScript DID, rather than the PlayScript enum used by PlayEffect.
	if (!Reader.CanRead(12))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const int32 PhysicsScriptId = static_cast<int32>(Reader.ReadUInt32());
	const float Intensity = Reader.ReadFloat();
	OnPlayScriptId.Broadcast(Guid, PhysicsScriptId, Intensity);
}

void FACESession::HandlePlayEffect(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(12))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const int32 ScriptType = static_cast<int32>(Reader.ReadUInt32());
	const float Intensity = Reader.ReadFloat();
	OnPlayEffect.Broadcast(Guid, ScriptType, Intensity);
}

void FACESession::HandleServerMessage(FACEBinaryReader& Reader)
{
	const FString Text = Reader.ReadString16L();
	const int32 Type = Reader.CanRead(4) ? Reader.ReadInt32() : 0;
	OnChatMessage.Broadcast(Text, TEXT(""), Type);
}

void FACESession::HandleHearSpeech(FACEBinaryReader& Reader)
{
	const FString Text = Reader.ReadString16L();
	const FString Sender = Reader.ReadString16L();
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32();
	}
	const int32 Type = Reader.CanRead(4) ? static_cast<int32>(Reader.ReadUInt32()) : 0;
	OnChatMessage.Broadcast(Text, Sender, Type);
}

void FACESession::HandleChannelBroadcast(FACEBinaryReader& Reader)
{
	// channelId, senderName (empty = "You"), message
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint32 ChannelId = Reader.ReadUInt32();
	const FString Sender = Reader.ReadString16L();
	const FString Text = Reader.ReadString16L();
	int32 Type;
	const FString Line = ACERetailChat::Format(ChannelId,Sender,Text,Type);
	// Retail OnChannelBroadcast remembers who last used @p/@m, separately
	// from private tells; our own empty-sender echoes cannot replace them.
	if (!Sender.IsEmpty())
	{
		if (ChannelId == ACEChatChannel::Patron) LastPatronTellSenderName = Sender;
		if (ChannelId == ACEChatChannel::Monarch) LastMonarchTellSenderName = Sender;
	}
	OnChatMessage.Broadcast(Line, Sender, Type);
}

void FACESession::HandleSetTurbineChatChannels(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(40))
	{
		return;
	}
	TurbineAllegianceChannel = Reader.ReadUInt32();
	TurbineGeneralChannel = Reader.ReadUInt32();
	TurbineTradeChannel = Reader.ReadUInt32();
	TurbineLfgChannel = Reader.ReadUInt32();
	TurbineRoleplayChannel = Reader.ReadUInt32();
	TurbineOlthoiChannel = Reader.ReadUInt32();
	TurbineSocietyChannel = Reader.ReadUInt32();
	Reader.ReadUInt32(); // SocietyCelestialHand
	Reader.ReadUInt32(); // SocietyEldrytchWeb
	Reader.ReadUInt32(); // SocietyRadiantBlood
	if (TurbineAllegianceChannel == 0) { TurbineAllegianceChannel = ACETurbineChat::Allegiance; }
	if (TurbineGeneralChannel == 0) { TurbineGeneralChannel = ACETurbineChat::General; }
	if (TurbineTradeChannel == 0) { TurbineTradeChannel = ACETurbineChat::Trade; }
	if (TurbineLfgChannel == 0) { TurbineLfgChannel = ACETurbineChat::LFG; }
	if (TurbineRoleplayChannel == 0) { TurbineRoleplayChannel = ACETurbineChat::Roleplay; }
	if (TurbineOlthoiChannel == 0) { TurbineOlthoiChannel = ACETurbineChat::Olthoi; }
	if (TurbineSocietyChannel == 0) { TurbineSocietyChannel = ACETurbineChat::Society; }
}

void FACESession::HandleTurbineChat(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(36))
	{
		return;
	}
	Reader.ReadUInt32(); // bytes to follow
	const uint32 BlobType = Reader.ReadUInt32();
	Reader.ReadUInt32(); // dispatch
	Reader.ReadUInt32();
	Reader.ReadUInt32();
	Reader.ReadUInt32();
	Reader.ReadUInt32();
	Reader.ReadUInt32();
	Reader.ReadUInt32(); // payload size
	if (BlobType == ACETurbineChat::BlobResponseBinary)
	{
		return;
	}
	if (BlobType != ACETurbineChat::BlobEventBinary)
	{
		return;
	}
	const uint32 ChannelId = Reader.ReadUInt32();
	const FString Sender = Reader.ReadPackedUnicode();
	const FString Text = Reader.ReadPackedUnicode();
	if (Reader.CanRead(4)) { Reader.ReadUInt32(); } // extra size
	if (Reader.CanRead(4)) { Reader.ReadUInt32(); } // speaker
	if (Reader.CanRead(4)) { Reader.ReadUInt32(); } // hresult
	const uint32 ChatType = Reader.CanRead(4) ? Reader.ReadUInt32() : ChannelId;

	const TCHAR* Tag = TEXT("General");
	int32 ChannelLogType = ACEChatMessageType::General;
	switch (ChatType)
	{
	case ACETurbineChat::Allegiance: Tag = TEXT("Allegiance"); ChannelLogType = ACEChatMessageType::Social; break;
	case ACETurbineChat::Trade: Tag = TEXT("Trade"); ChannelLogType = ACEChatMessageType::Trade; break;
	case ACETurbineChat::LFG: Tag = TEXT("LFG"); ChannelLogType = ACEChatMessageType::LFG; break;
	case ACETurbineChat::Roleplay: Tag = TEXT("Roleplay"); ChannelLogType = ACEChatMessageType::Roleplay; break;
	case ACETurbineChat::Society:
	case ACETurbineChat::SocietyCelestialHand:
	case ACETurbineChat::SocietyEldrytchWeb:
	case ACETurbineChat::SocietyRadiantBlood:
		Tag = TEXT("Society"); ChannelLogType = ACEChatMessageType::Society;
		break;
	case ACETurbineChat::Olthoi: Tag = TEXT("Olthoi"); ChannelLogType = ACEChatMessageType::General; break;
	default: Tag = TEXT("General"); break;
	}
	const FString Line = FString::Printf(TEXT("[%s] %s says, \"%s\""), Tag, *Sender, *Text);
	OnChatMessage.Broadcast(Line, Sender, ChannelLogType);
}

void FACESession::HandleHearRangedSpeech(FACEBinaryReader& Reader)
{
	const FString Text = Reader.ReadString16L();
	const FString Sender = Reader.ReadString16L();
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // sender id
	}
	if (Reader.CanRead(4))
	{
		Reader.ReadFloat(); // range
	}
	const int32 Type = Reader.CanRead(4) ? static_cast<int32>(Reader.ReadUInt32()) : 0;
	OnChatMessage.Broadcast(Text, Sender, Type);
}

void FACESession::HandleSoulEmote(FACEBinaryReader& Reader)
{
	// GameMessageSoulEmote: senderId, senderName, emoteText (OtherEmote fragment).
	uint32 SenderGuid = 0;
	if (Reader.CanRead(4))
	{
		SenderGuid = Reader.ReadUInt32();
	}
	const FString Sender = Reader.ReadString16L();
	const FString Text = Reader.ReadString16L();
	// Local player already appended "You …" — skip self echo.
	if (SenderGuid != 0 && SenderGuid == static_cast<uint32>(PlayerGuid))
	{
		return;
	}
	OnChatMessage.Broadcast(Text, Sender, ACEChatMessageType::Emote);
}

bool FACESession::RespondToConfirmation(uint32 Type, uint32 Context, bool Accept)
{
	if (State != EACESessionState::InWorld) return false;
	const int32 Index = Confirmations.IndexOfByPredicate([&](const FACEConfirmation& C) { return C.Type == Type && C.Context == Context; });
	if (Index == INDEX_NONE) return false;
	FACEBinaryWriter Body;
	Body.WriteUInt32(Type); Body.WriteUInt32(Context); Body.WriteUInt32(Accept ? 1u : 0u);
	SendGameAction(0x0275, Body.GetData(), ACEQueue::UIQueue);
	Confirmations.RemoveAt(Index);
	return true;
}

void FACESession::HandleGameEvent(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(12))
	{
		return;
	}
	Reader.ReadUInt32(); // player guid
	Reader.ReadUInt32(); // event sequence
	const uint32 EventType = Reader.ReadUInt32();

	switch (EventType)
	{
	case 0x0274: // CharacterConfirmationRequest
	{
		if (State != EACESessionState::InWorld || !Reader.CanRead(10)) break;
		FACEConfirmation Request;
		Request.Type = Reader.ReadUInt32(); Request.Context = Reader.ReadUInt32();
		Request.Prompt = Reader.ReadString16L();
		if (Request.Prompt.IsEmpty()) break;
		if (auto* Existing = Confirmations.FindByPredicate([&](const FACEConfirmation& C) { return C.Type == Request.Type && C.Context == Request.Context; }))
			*Existing = MoveTemp(Request);
		else if (Confirmations.Num() < 16) Confirmations.Add(MoveTemp(Request));
		break;
	}
	case 0x0276: // CharacterConfirmationDone: withdraw expired/resolved dialogs.
		if (Reader.CanRead(8))
		{
			const uint32 Type = Reader.ReadUInt32(), Context = Reader.ReadUInt32();
			Confirmations.RemoveAll([&](const FACEConfirmation& C) { return C.Type == Type && C.Context == Context; });
		}
		break;
	case 0xF7D1: HandleVRPose(Reader); break;
	case 0xF7D2: HandleHealthFeedback(Reader); break;
	case 0xF7D4: HandleVRRecovery(Reader); break;
	case 0xF7D5: HandleVRCasting(Reader); break;
	case 0xF7D3:
		if (State == EACESessionState::InWorld && (VRCapabilities & 256u) && (Reader.Remaining() == 12 || Reader.Remaining() == 16))
		{
			const int32 Spell = Reader.ReadUInt32(); const float Speed = Reader.ReadFloat(); const uint32 Gravity = Reader.ReadUInt32();
			const float Radius = Reader.CanRead(4) ? Reader.ReadFloat() : .05f;
			if (Spell > 0 && FMath::IsFinite(Speed) && Speed >= 0 && Speed <= 200 && Gravity <= 1
				&& FMath::IsFinite(Radius) && Radius >= 0.f && Radius <= 10.f)
			{
				if (VRSpellProfiles.Num() >= 128) VRSpellProfiles.Reset();
				VRSpellProfiles.Add(Spell,FVector(Speed,Gravity,Radius));
			}
		}
		break;
	case 0xF7D0:
		if (State == EACESessionState::InWorld && Reader.CanRead(8))
		{
			const uint32 Version = Reader.ReadUInt32();
			const uint32 Flags = Reader.ReadUInt32();
			VRCapabilities = Version == 1 ? Flags & 32767u : 0u;
			if (SupportsHealthFeedback() || SupportsVRRecovery())
            {
                FACEBinaryWriter Subscribe; Subscribe.WriteUInt32(1); Subscribe.WriteUInt32(4);
                Subscribe.WriteUInt32((SupportsHealthFeedback() ? 1u : 0u) | (SupportsVRRecovery() ? 2u : 0u)
                    | (SupportsVRCasting() ? 4u : 0u) | ((VRCapabilities & 8192u) ? 8u : 0u));
                SendGameAction(0xF7D0, Subscribe.GetData(), ACEQueue::WeenieQueue);
            }
            if ((VRCapabilities & 8u) != 0) ApplyVRWorldSnapshot(Reader);
			VRMissileWeapon = 0; VRMissileSpeed = 0.f;
			if ((VRCapabilities & 32u) && Reader.CanRead(8))
			{
				const int32 Weapon = Reader.ReadUInt32(); const float Speed = Reader.ReadFloat();
				if (FMath::IsFinite(Speed) && Speed > 0.f && Speed <= 50.f)
				{ VRMissileWeapon = Weapon; VRMissileSpeed = Speed; }
			}
			Log(TEXT("VR server capability response received"));
		}
		break;
	case ACEGameEvent::QueryAgeResponse:
	{
		if (!Reader.CanRead(4)) break;
		const FString Name=Reader.ReadString16L();
		if (!Reader.CanRead(4)) break;
		const FString Age=Reader.ReadString16L();
		OnChatMessage.Broadcast(Name.IsEmpty() ? TEXT("You have played for ")+Age+TEXT(".") : Name+TEXT(" has played for ")+Age+TEXT("."),TEXT(""),ACEChatMessageType::System);
		break;
	}
	case ACEGameEvent::PlayerDescription:
		HandlePlayerDescription(Reader);
		break;
	case ACEGameEvent::UpdateHealth:
		HandleUpdateHealth(Reader);
		break;
	case ACEGameEvent::IdentifyObjectResponse:
		HandleIdentifyObjectResponse(Reader);
		break;
	case ACEGameEvent::ViewContents:
		HandleViewContents(Reader);
		break;
	case ACEGameEvent::CloseGroundContainer:
		HandleCloseGroundContainer(Reader);
		break;
	case ACEGameEvent::ApproachVendor:
		HandleApproachVendor(Reader);
		break;
	case ACEGameEvent::RegisterTrade:
		HandleRegisterTrade(Reader);
		break;
	case ACEGameEvent::ChessJoinGameResponse:
	case ACEGameEvent::ChessStartGame:
	case ACEGameEvent::ChessMoveResponse:
	case ACEGameEvent::ChessOpponentTurn:
	case ACEGameEvent::ChessOpponentStalemate:
	case ACEGameEvent::ChessGameOver:
		HandleChessGameEvent(EventType, Reader);
		break;
	case ACEGameEvent::OpenTrade:
		HandleOpenTrade(Reader);
		break;
	case ACEGameEvent::CloseTrade:
		HandleCloseTrade(Reader);
		break;
	case ACEGameEvent::AddToTrade:
		HandleAddToTradeEvent(Reader);
		break;
	case ACEGameEvent::RemoveFromTrade:
		HandleRemoveFromTradeEvent(Reader);
		break;
	case ACEGameEvent::AcceptTrade:
		HandleAcceptTradeEvent(Reader);
		break;
	case ACEGameEvent::DeclineTrade:
		HandleDeclineTradeEvent(Reader);
		break;
	case ACEGameEvent::ResetTrade:
		HandleResetTradeEvent(Reader);
		break;
	case ACEGameEvent::TradeFailure:
		HandleTradeFailure(Reader);
		break;
	case ACEGameEvent::ClearTradeAcceptance:
		HandleClearTradeAcceptance(Reader);
		break;
	case ACEGameEvent::ChannelIndex:
	case ACEGameEvent::ChannelList:
	{
		if (!Reader.CanRead(4)) break;
		const uint32 Count = Reader.ReadUInt32();
		if (Count > 4096) break;
		TArray<FString> Names;
		for (uint32 I=0; I<Count && Reader.CanRead(2); ++I) Names.Add(Reader.ReadString16L());
		OnChatMessage.Broadcast(Names.IsEmpty() ? TEXT("No entries.") : FString::Join(Names,TEXT(", ")),
			FString(),ACEChatMessageType::System);
		break;
	}
	case ACEGameEvent::ChannelBroadcast:
		HandleChannelBroadcast(Reader);
		break;
	case ACEGameEvent::FellowshipFullUpdate:
		HandleFellowshipFullUpdate(Reader);
		break;
	case ACEGameEvent::FellowshipUpdateFellow:
		HandleFellowshipUpdateFellow(Reader);
		break;
	case ACEGameEvent::FellowshipQuit:
		HandleFellowshipQuitEvent(Reader);
		break;
	case ACEGameEvent::FellowshipDismiss:
		HandleFellowshipDismissEvent(Reader);
		break;
	case ACEGameEvent::FellowshipDisband:
		HandleFellowshipDisband(Reader);
		break;
	case ACEGameEvent::FellowshipFellowUpdateDone:
	case ACEGameEvent::FellowshipFellowStatsDone:
		break;
	case ACEGameEvent::AllegianceUpdate:
		HandleAllegianceUpdate(Reader);
		break;
	case ACEGameEvent::AllegianceUpdateDone:
	case ACEGameEvent::AllegianceLoginNotification:
	case ACEGameEvent::AllegianceInfoResponse:
		break;
	case ACEGameEvent::FriendsListUpdate:
		HandleFriendsListUpdate(Reader);
		break;
	case ACEGameEvent::SetSquelchDB:
		HandleSetSquelchDB(Reader);
		break;
	case ACEGameEvent::SalvageOperationsResult:
		HandleSalvageOperationsResult(Reader);
		break;
	case ACEGameEvent::SendClientContractTrackerTable:
		HandleContractTrackerTable(Reader);
		break;
	case ACEGameEvent::SendClientContractTracker:
		HandleContractTracker(Reader);
		break;
	case ACEGameEvent::HouseData:
		HandleHouseData(Reader);
		break;
	case ACEGameEvent::HouseStatus:
		HandleHouseStatus(Reader);
		break;
	case ACEGameEvent::SetTurbineChatChannels:
		HandleSetTurbineChatChannels(Reader);
		break;
	case ACEGameEvent::BookDataResponse:
		HandleBookDataResponse(Reader);
		break;
	case ACEGameEvent::BookPageDataResponse:
		HandleBookPageDataResponse(Reader);
		break;
	case ACEGameEvent::CharacterTitle:
		HandleCharacterTitle(Reader);
		break;
	case ACEGameEvent::UpdateTitle:
		HandleUpdateTitle(Reader);
		break;
	case ACEGameEvent::UseDone:
		HandleUseDone(Reader);
		break;
	case ACEGameEvent::ItemAppraiseDone:
		HandleItemAppraiseDone(Reader);
		break;
	case ACEGameEvent::MagicUpdateSpell:
		HandleMagicUpdateSpell(Reader);
		break;
	case ACEGameEvent::MagicRemoveSpell:
		HandleMagicRemoveSpell(Reader);
		break;
	case ACEGameEvent::MagicUpdateEnchantment:
		HandleMagicUpdateEnchantment(Reader);
		break;
	case ACEGameEvent::MagicRemoveEnchantment:
		HandleMagicRemoveEnchantment(Reader);
		break;
	case ACEGameEvent::MagicUpdateMultipleEnchantments:
		HandleMagicUpdateMultipleEnchantments(Reader);
		break;
	case ACEGameEvent::MagicRemoveMultipleEnchantments:
		HandleMagicRemoveMultipleEnchantments(Reader);
		break;
	case ACEGameEvent::MagicPurgeEnchantments:
		HandleMagicPurgeEnchantments(Reader);
		break;
	case ACEGameEvent::MagicPurgeBadEnchantments:
		HandleMagicPurgeBadEnchantments(Reader);
		break;
	case ACEGameEvent::MagicDispelEnchantment:
		HandleMagicDispelEnchantment(Reader);
		break;
	case ACEGameEvent::MagicDispelMultipleEnchantments:
		HandleMagicDispelMultipleEnchantments(Reader);
		break;
	case ACEGameEvent::PingResponse:
		HandlePingResponse(Reader);
		break;
	case ACEGameEvent::InventoryPutObjInContainer:
		HandleInventoryPutObjInContainer(Reader);
		break;
	case ACEGameEvent::WieldItem:
		HandleWieldItem(Reader);
		break;
	case ACEGameEvent::InventoryPutObjIn3D:
		HandleInventoryPutObjIn3D(Reader);
		break;
	case ACEGameEvent::VictimNotification:
		HandleCombatVictimNotification(Reader);
		break;
	case ACEGameEvent::KillerNotification:
		HandleCombatKillerNotification(Reader);
		break;
	case ACEGameEvent::AttackerNotification:
		HandleCombatAttackerNotification(Reader);
		break;
	case ACEGameEvent::CombatCommenceAttack:
		bServerAttackInProgress = true;
		LastAttackError = 0;
		++CombatEventRevision;
		break;
	case ACEGameEvent::AttackDone:
		if (!Reader.CanRead(4)) break;
		bServerAttackInProgress = false;
		LastAttackError = Reader.ReadUInt32();
		++CombatEventRevision;
		break;
	case ACEGameEvent::DefenderNotification:
		HandleCombatDefenderNotification(Reader);
		break;
	case ACEGameEvent::EvasionAttackerNotification:
		HandleCombatEvasionAttacker(Reader);
		break;
	case ACEGameEvent::EvasionDefenderNotification:
		HandleCombatEvasionDefender(Reader);
		break;
	case ACEGameEvent::WeenieError:
		HandleWeenieError(Reader);
		break;
	case ACEGameEvent::WeenieErrorWithString:
		HandleWeenieErrorWithString(Reader);
		break;
	case ACEGameEvent::CommunicationTransientString:
		HandleTransientString(Reader);
		break;
	case ACEGameEvent::InventoryServerSaveFailed:
		HandleInventoryServerSaveFailed(Reader);
		break;
	case ACEGameEvent::Tell:
		HandleTell(Reader);
		break;
	default:
		break;
	}
}

void FACESession::HandleCombatVictimNotification(FACEBinaryReader& Reader)
{
	const FString Msg = Reader.ReadString16L();
	if (!Msg.IsEmpty())
	{
		OnChatMessage.Broadcast(Msg, TEXT(""), ACEChatMessageType::CombatEnemy);
	}
}

void FACESession::HandleCombatKillerNotification(FACEBinaryReader& Reader)
{
	const FString Msg = Reader.ReadString16L();
	if (!Msg.IsEmpty())
	{
		OnChatMessage.Broadcast(Msg, TEXT(""), ACEChatMessageType::CombatSelf);
	}
}

void FACESession::HandleCombatAttackerNotification(FACEBinaryReader& Reader)
{
	const FString DefenderName = Reader.ReadString16L();
	if (!Reader.CanRead(4 + 8 + 4 + 4 + 8))
	{
		return;
	}
	const uint32 DamageType = Reader.ReadUInt32();
	const float Percent = static_cast<float>(Reader.ReadDouble());
	const uint32 Damage = Reader.ReadUInt32();
	const bool bCritical = Reader.ReadUInt32() != 0;
	const uint64 Conditions = Reader.ReadUInt64();
	const FString Msg = ACECombatChat::FormatAttackerNotification(
		DefenderName, DamageType, Percent, Damage, bCritical, Conditions);
	OnChatMessage.Broadcast(Msg, TEXT(""), ACEChatMessageType::CombatSelf);
	OnCombatFeedback.Broadcast(DefenderName, static_cast<int32>(FMath::Min(Damage, uint32(MAX_int32))), false, bCritical);
}

void FACESession::HandleCombatDefenderNotification(FACEBinaryReader& Reader)
{
	const FString AttackerName = Reader.ReadString16L();
	if (!Reader.CanRead(4 + 8 + 4 + 4 + 4 + 8))
	{
		return;
	}
	const uint32 DamageType = Reader.ReadUInt32();
	const float Percent = static_cast<float>(Reader.ReadDouble());
	const uint32 Damage = Reader.ReadUInt32();
	const uint32 DamageLocation = Reader.ReadUInt32();
	const bool bCritical = Reader.ReadUInt32() != 0;
	const uint64 Conditions = Reader.ReadUInt64();
	Reader.Align();
	const FString Msg = ACECombatChat::FormatDefenderNotification(
		AttackerName, DamageType, Percent, Damage, DamageLocation, bCritical, Conditions);
	OnChatMessage.Broadcast(Msg, TEXT(""), ACEChatMessageType::CombatEnemy);

	// Retail AutoTarget remembers the last attacker for 15s (name→GUID lookup).
	if (!AttackerName.IsEmpty())
	{
		for (const TPair<int32, FACEWorldObject>& Pair : WorldObjects)
		{
			if (Pair.Value.Name.Equals(AttackerName, ESearchCase::IgnoreCase)
				&& Pair.Key != PlayerGuid)
			{
				LastAttackerGuid = Pair.Key;
				LastAttackerTimeSeconds = FPlatformTime::Seconds();
				break;
			}
		}
	}
}

void FACESession::HandleCombatEvasionAttacker(FACEBinaryReader& Reader)
{
	const FString DefenderName = Reader.ReadString16L();
	OnCombatFeedback.Broadcast(DefenderName, 0, false, false);
	OnChatMessage.Broadcast(ACECombatChat::FormatEvasionAttacker(DefenderName), TEXT(""),
		ACEChatMessageType::CombatSelf);
}

void FACESession::HandleCombatEvasionDefender(FACEBinaryReader& Reader)
{
	const FString AttackerName = Reader.ReadString16L();
	OnCombatFeedback.Broadcast(AttackerName, 0, true, false);
	OnChatMessage.Broadcast(ACECombatChat::FormatEvasionDefender(AttackerName), TEXT(""),
		ACEChatMessageType::CombatEnemy);
}

void FACESession::HandleWeenieError(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint32 Code = Reader.ReadUInt32();
	if (Code == 0x003D) // A server failure must also release local approach prediction.
	{
		ReportMoveToFailure(Code);
		return;
	}
	// Portal cancel/teleport status — retail does not dump these into chat as "Error 0x…".
	if (Code == 0x003Bu || Code == 0x003Cu) // ILeftTheWorld / ITeleported
	{
		return;
	}
	const FString Msg = ACECombatChat::LookupWeenieError(Code);
	if (!Msg.IsEmpty())
	{
		// Retail shows WeenieErrors as the yellow center-top transient banner, not chat.
		OnChatMessage.Broadcast(Msg, TEXT(""), ACEChatMessageType::TransientInfo);
	}
}

void FACESession::ReportMoveToFailure(uint32 Error)
{
	OnMoveToFailed.Broadcast(Error);
	const FString Message = ACECombatChat::LookupWeenieError(Error);
	if (!Message.IsEmpty()) OnChatMessage.Broadcast(Message, TEXT(""), ACEChatMessageType::TransientInfo);
}

void FACESession::HandleWeenieErrorWithString(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint32 Code = Reader.ReadUInt32();
	const FString Arg = Reader.ReadString16L();
	const FString Msg = ACECombatChat::LookupWeenieErrorWithString(Code, Arg);
	if (!Msg.IsEmpty())
	{
		OnChatMessage.Broadcast(Msg, TEXT(""), (Code == 0x051B || Code == 0x051C)
			? ACEChatMessageType::System : ACEChatMessageType::TransientInfo);
	}
}

void FACESession::HandleTransientString(FACEBinaryReader& Reader)
{
	const FString Msg = Reader.ReadString16L();
	if (!Msg.IsEmpty())
	{
		OnChatMessage.Broadcast(Msg, TEXT(""), ACEChatMessageType::TransientInfo);
	}
}

void FACESession::HandleTell(FACEBinaryReader& Reader)
{
	// GameEvent Tell: message, senderName, senderId, targetId, chatType, pad.
	const FString Text = Reader.ReadString16L();
	const FString Sender = Reader.ReadString16L();
	int32 SenderId = 0;
	if (Reader.CanRead(4))
	{
		SenderId = static_cast<int32>(Reader.ReadUInt32());
	}
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // targetId
	}
	const int32 Type = Reader.CanRead(4)
		? static_cast<int32>(Reader.ReadUInt32())
		: ACEChatMessageType::Tell;
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32();
	}
	// NPC dialogue must not replace the last player teller (retail IID range).
	if ((Type == ACEChatMessageType::Tell || Type == ACEChatMessageType::AdminTell)
		&& static_cast<uint32>(SenderId) >= 0x50000001u
		&& static_cast<uint32>(SenderId) <= 0x6FFFFFFFu && SenderId != PlayerGuid)
	{
		LastTellSenderGuid = SenderId;
		LastTellSenderName = Sender;
	}
	OnChatMessage.Broadcast(Text, Sender, Type);
	if (const auto* NPC = WorldObjects.Find(SenderId); NPC && !NPC->bIsPlayer && (NPC->ItemType & ACEItemType::Creature))
		OnNPCSpeech.Broadcast(SenderId, Text);
}

void FACESession::HandlePlayerDescription(FACEBinaryReader& Reader)
{
	// Mirrors ACE GameEventPlayerDescription.WriteEventBody (CACQualities + attribute cache).
	constexpr uint32 FlagPropertyInt32  = 0x0001;
	constexpr uint32 FlagPropertyBool   = 0x0002;
	constexpr uint32 FlagPropertyDouble = 0x0004;
	constexpr uint32 FlagPropertyDid    = 0x0008;
	constexpr uint32 FlagPropertyString = 0x0010;
	constexpr uint32 FlagPosition       = 0x0020;
	constexpr uint32 FlagPropertyIid    = 0x0040;
	constexpr uint32 FlagPropertyInt64  = 0x0080;

	constexpr uint32 VectorAttribute = 0x0001;
	constexpr uint32 VectorSkill     = 0x0002;
	constexpr uint32 VectorSpell     = 0x0100;
	constexpr uint32 VectorEnchant   = 0x0200;

	auto SkillName = [](uint32 Id) -> FString
	{
		static const TCHAR* Names[] = {
			TEXT("None"), TEXT("Axe"), TEXT("Bow"), TEXT("Crossbow"), TEXT("Dagger"), TEXT("Mace"),
			TEXT("Melee Defense"), TEXT("Missile Defense"), TEXT("Sling"), TEXT("Spear"), TEXT("Staff"),
			TEXT("Sword"), TEXT("Thrown Weapon"), TEXT("Unarmed Combat"), TEXT("Arcane Lore"),
			TEXT("Magic Defense"), TEXT("Mana Conversion"), TEXT("Spellcraft"), TEXT("Item Tinkering"),
			TEXT("Assess Person"), TEXT("Deception"), TEXT("Healing"), TEXT("Jump"), TEXT("Lockpick"),
			TEXT("Run"), TEXT("Awareness"), TEXT("Arms and Armor Repair"), TEXT("Assess Creature"),
			TEXT("Weapon Tinkering"), TEXT("Armor Tinkering"), TEXT("Magic Item Tinkering"),
			TEXT("Creature Enchantment"), TEXT("Item Enchantment"), TEXT("Life Magic"), TEXT("War Magic"),
			TEXT("Leadership"), TEXT("Loyalty"), TEXT("Fletching"), TEXT("Alchemy"), TEXT("Cooking"),
			TEXT("Salvaging"), TEXT("Two Handed Combat"), TEXT("Gearcraft"), TEXT("Void Magic"),
			TEXT("Heavy Weapons"), TEXT("Light Weapons"), TEXT("Finesse Weapons"), TEXT("Missile Weapons"),
			TEXT("Shield"), TEXT("Dual Wield"), TEXT("Recklessness"), TEXT("Sneak Attack"),
			TEXT("Dirty Fighting"), TEXT("Challenge"), TEXT("Summoning")
		};
		return (Id < UE_ARRAY_COUNT(Names)) ? FString(Names[Id]) : FString::Printf(TEXT("Skill %u"), Id);
	};

	if (!Reader.CanRead(8))
	{
		return;
	}
	const uint32 PropertyFlags = Reader.ReadUInt32();
	Reader.ReadUInt32(); // weenie type

	FACEPlayerVitals Vitals;

	auto ReadTableHeader = [&Reader]() -> int32
	{
		if (!Reader.CanRead(4))
		{
			return -1;
		}
		const uint16 Count = Reader.ReadUInt16();
		Reader.ReadUInt16(); // bucket count
		return Count;
	};

	if (PropertyFlags & FlagPropertyInt32)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
		{
			const uint32 Key = Reader.ReadUInt32();
			const int32 Value = Reader.ReadInt32();
			Vitals.StatQualityInts.Add(static_cast<int32>(Key), Value);
			if (Key == 25) // PropertyInt.Level
			{
				Vitals.Level = Value;
			}
			else if (Key == 24) // PropertyInt.AvailableSkillCredits
			{
				Vitals.AvailableSkillCredits = Value;
			}
			else if (Key == 113) // PropertyInt.Gender
			{
				Vitals.Gender = Value;
			}
			else if (Key == 134) // PropertyInt.PlayerKillerStatus
			{
				Vitals.PlayerKillerStatus = Value;
			}
			else if (Key == 188) // PropertyInt.HeritageGroup
			{
				Vitals.HeritageGroup = Value;
			}
			else if (Key == 40) // PropertyInt.CombatMode
			{
				// CharacterDesc path seeds vitals before broadcast; keep raw value.
				Vitals.CombatMode = Value;
			}
			else if (Key == 5) // PropertyInt.EncumbranceVal — apply once Self WO exists
			{
				bHasPlayerEncumbrance = true;
				PlayerEncumbranceVal = Value;
				if (PlayerGuid != 0)
				{
					if (FACEWorldObject* Self = WorldObjects.Find(PlayerGuid))
					{
						Self->Burden = Value;
					}
				}
			}
			else if (Key == 322) { Vitals.AetheriaUnlocked = Value & 7; }
			else if (Key == 230) // PropertyInt.AugmentationIncreasedCarryingCapacity
			{
				Vitals.CarryingCapacityAugs = FMath::Clamp(Value, 0, 5);
			}
			else if (Key == 129) // PropertyInt.VitaeCpPool
			{
				VitaeCpPool = Value;
			}
			else if (Key == 139) // PropertyInt.DeathLevel
			{
				DeathLevel = Value;
			}
			else if (Key == 125) // PropertyInt.Age
			{
				Vitals.AgeSeconds = Value;
			}
			else if (Key == 43) // PropertyInt.NumDeaths
			{
				Vitals.NumDeaths = Value;
			}
			else if (Key == 181) // PropertyInt.ChessRank
			{
				Vitals.ChessRank = Value;
			}
			else if (Key == 192) // PropertyInt.FakeFishingSkill
			{
				Vitals.FishingSkill = Value;
			}
		}
	}
	if (PropertyFlags & FlagPropertyInt64)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(12); ++i)
		{
			const uint32 Key = Reader.ReadUInt32();
			const int64 Value = static_cast<int64>(Reader.ReadUInt64());
			if (Key == 1) // PropertyInt64.TotalExperience
			{
				Vitals.TotalExperience = Value;
			}
			else if (Key == 2) // PropertyInt64.AvailableExperience
			{
				Vitals.AvailableExperience = Value;
			}
			else if (Key == 6) // PropertyInt64.AvailableLuminance
			{
				Vitals.AvailableLuminance = Value;
			}
			else if (Key == 7) // PropertyInt64.MaximumLuminance
			{
				Vitals.MaximumLuminance = Value;
			}
		}
	}
	if (PropertyFlags & FlagPropertyBool)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
		{
			const uint32 Key = Reader.ReadUInt32();
			const uint32 Value = Reader.ReadUInt32();
			// PropertyBool.IsAdmin=44, IsArch=45, IsSentinel=46 (SendOnLogin).
			if ((Key == 44 || Key == 45 || Key == 46) && Value != 0)
			{
				bLocalPlayerIsAdmin = true;
			}
		}
	}
	if (PropertyFlags & FlagPropertyDouble)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(12); ++i)
		{
			Reader.ReadUInt32();
			Reader.ReadDouble();
		}
	}
	if (PropertyFlags & FlagPropertyString)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(6); ++i)
		{
			const uint32 Key = Reader.ReadUInt32();
			const FString Value = Reader.ReadString16L();
			if (Key == 5) // PropertyString.Template
			{
				Vitals.TemplateName = Value;
			}
		}
	}
	if (PropertyFlags & FlagPropertyDid)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
		{
			Reader.ReadUInt32();
			Reader.ReadUInt32();
		}
	}
	if (PropertyFlags & FlagPropertyIid)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
		{
			Reader.ReadUInt32();
			Reader.ReadUInt32();
		}
	}
	if (PropertyFlags & FlagPosition)
	{
		const int32 Count = ReadTableHeader();
		// key u32 + landblock u32 + xyz 3*f32 + quat 4*f32 = 36 bytes per entry
		for (int32 i = 0; i < Count && Reader.CanRead(36); ++i)
		{
			Reader.Skip(36);
		}
	}

	if (!Reader.CanRead(8))
	{
		return;
	}
	const uint32 VectorFlags = Reader.ReadUInt32();
	Reader.ReadUInt32(); // has health

	if (VectorFlags & VectorAttribute)
	{
		if (!Reader.CanRead(4))
		{
			return;
		}
		const uint32 AttribFlags = Reader.ReadUInt32();

		auto ReadAttribute = [&Reader](int32& OutCurrent, int32& OutRanks, int32& OutStart, int32& OutXp) -> bool
		{
			if (!Reader.CanRead(12))
			{
				return false;
			}
			OutRanks = static_cast<int32>(Reader.ReadUInt32());
			OutStart = static_cast<int32>(Reader.ReadUInt32());
			OutXp = static_cast<int32>(Reader.ReadUInt32());
			OutCurrent = OutRanks + OutStart;
			return true;
		};
		auto ReadVital = [&Reader](int32& OutRanks, int32& OutStart, int32& OutXp, int32& OutCurrent) -> bool
		{
			if (!Reader.CanRead(16))
			{
				return false;
			}
			OutRanks = static_cast<int32>(Reader.ReadUInt32());
			OutStart = static_cast<int32>(Reader.ReadUInt32());
			OutXp = static_cast<int32>(Reader.ReadUInt32());
			OutCurrent = static_cast<int32>(Reader.ReadUInt32());
			return true;
		};

		bool bOk = true;
		if (AttribFlags & 0x001) { bOk = bOk && ReadAttribute(Vitals.Strength, Vitals.StrengthRanks, Vitals.StrengthStart, Vitals.StrengthXpSpent); }
		if (AttribFlags & 0x002) { bOk = bOk && ReadAttribute(Vitals.Endurance, Vitals.EnduranceRanks, Vitals.EnduranceStart, Vitals.EnduranceXpSpent); }
		if (AttribFlags & 0x004) { bOk = bOk && ReadAttribute(Vitals.Quickness, Vitals.QuicknessRanks, Vitals.QuicknessStart, Vitals.QuicknessXpSpent); }
		if (AttribFlags & 0x008) { bOk = bOk && ReadAttribute(Vitals.Coordination, Vitals.CoordinationRanks, Vitals.CoordinationStart, Vitals.CoordinationXpSpent); }
		if (AttribFlags & 0x010) { bOk = bOk && ReadAttribute(Vitals.Focus, Vitals.FocusRanks, Vitals.FocusStart, Vitals.FocusXpSpent); }
		if (AttribFlags & 0x020) { bOk = bOk && ReadAttribute(Vitals.Self, Vitals.SelfRanks, Vitals.SelfStart, Vitals.SelfXpSpent); }
		if (AttribFlags & 0x040) { bOk = bOk && ReadVital(Vitals.HealthRanks, Vitals.HealthStart, Vitals.HealthXpSpent, Vitals.Health); }
		if (AttribFlags & 0x080) { bOk = bOk && ReadVital(Vitals.StaminaRanks, Vitals.StaminaStart, Vitals.StaminaXpSpent, Vitals.Stamina); }
		if (AttribFlags & 0x100) { bOk = bOk && ReadVital(Vitals.ManaRanks, Vitals.ManaStart, Vitals.ManaXpSpent, Vitals.Mana); }
		if (!bOk)
		{
			return;
		}
	}

	if (VectorFlags & VectorSkill)
	{
		const int32 Count = ReadTableHeader();
		Vitals.Skills.Reset();
		Vitals.Skills.Reserve(Count);
		// skillId u32, ranks u16, u16, sac u32, xp u32, init u32, resistance u32, lastUsed f64 = 32 bytes
		for (int32 i = 0; i < Count && Reader.CanRead(32); ++i)
		{
			FACESkillInfo Skill;
			Skill.SkillId = static_cast<int32>(Reader.ReadUInt32());
			Skill.Ranks = static_cast<int32>(Reader.ReadUInt16());
			Reader.ReadUInt16();
			Skill.AdvancementClass = static_cast<int32>(Reader.ReadUInt32());
			Skill.XpSpent = static_cast<int32>(Reader.ReadUInt32());
			Skill.InitLevel = static_cast<int32>(Reader.ReadUInt32());
			Reader.ReadUInt32(); // resistance_of_last_check
			Reader.ReadDouble(); // last_time_used
			Skill.Name = SkillName(static_cast<uint32>(Skill.SkillId));
			Skill.Current = Skill.InitLevel + Skill.Ranks;
			// Formula attributes (retail-ish base without buffs).
			switch (Skill.SkillId)
			{
			case 6:  Skill.Current += (Vitals.Coordination + Vitals.Quickness) / 3; break; // MeleeDefense
			case 7:  Skill.Current += (Vitals.Coordination + Vitals.Quickness) / 5; break; // MissileDefense
			case 15: Skill.Current += (Vitals.Focus + Vitals.Self) / 7; break; // MagicDefense
			case 22: Skill.Current += (Vitals.Strength + Vitals.Coordination) / 2; break; // Jump
			case 24: Skill.Current += Vitals.Quickness; break; // Run
			default: break;
			}
			if (Skill.SkillId == 24)
			{
				Vitals.RunSkillCurrent = Skill.Current;
			}
			if (Skill.SkillId == 22)
			{
				Vitals.JumpSkillCurrent = Skill.Current;
			}
			Vitals.Skills.Add(Skill);
		}
	}

	KnownSpells.Reset();
	SpellBars.SetNum(8);
	for (TArray<int32>& Bar : SpellBars) { Bar.Reset(); }
	ShortcutObjects.Reset();

	if (VectorFlags & VectorSpell)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
		{
			const int32 SpellId = Reader.ReadInt32();
			Reader.ReadFloat(); // configuration (ACE writes 2.0)
			if (SpellId > 0)
			{
				KnownSpells.AddUnique(SpellId);
			}
		}
	}

	if (VectorFlags & VectorEnchant)
	{
		if (Reader.CanRead(4))
		{
			ActiveEnchantments.Reset();
			const uint32 EnchantMask = Reader.ReadUInt32();
			auto ReadList = [this, &Reader]()
			{
				if (!Reader.CanRead(4)) { return; }
				const uint32 N = Reader.ReadUInt32();
				for (uint32 i = 0; i < N; ++i)
				{
					FACEActiveEnchantment Entry;
					if (!ReadEnchantmentRecord(Reader, Entry))
					{
						break;
					}
					UpsertEnchantment(Entry);
				}
			};
			if (EnchantMask & 0x1) { ReadList(); } // Multiplicative
			if (EnchantMask & 0x2) { ReadList(); } // Additive
			if (EnchantMask & 0x8)
			{
				// Cooldowns — keep marked so UI can filter them out.
				if (Reader.CanRead(4))
				{
					const uint32 N = Reader.ReadUInt32();
					for (uint32 i = 0; i < N; ++i)
					{
						FACEActiveEnchantment Entry;
						if (!ReadEnchantmentRecord(Reader, Entry))
						{
							break;
						}
						Entry.bCooldown = true;
						UpsertEnchantment(Entry);
					}
				}
			}
			if (EnchantMask & 0x4)
			{
				FACEActiveEnchantment Vitae;
				if (ReadEnchantmentRecord(Reader, Vitae))
				{
					Vitae.bVitae = true;
					UpsertEnchantment(Vitae);
				}
			}
			RecomputeAttributeEnchantments(Vitals);
		}
	}

	// Player module / options (shortcuts, 8 spell bars, filters…).
	if (Reader.CanRead(8))
	{
		constexpr uint32 OptShortcut = 0x00000001;
		constexpr uint32 OptDesiredComps = 0x00000008;
		constexpr uint32 OptSpellbookFilters = 0x00000020;
		constexpr uint32 OptCharacterOptions2 = 0x00000040;
		constexpr uint32 OptGameplayOptions = 0x00000200;
		constexpr uint32 OptSpellLists8 = 0x00000400;

		const uint32 OptionFlags = Reader.ReadUInt32();
		CharacterOptions1 = Reader.ReadUInt32();

		if (OptionFlags & OptShortcut)
		{
			if (Reader.CanRead(4))
			{
				const int32 Count = Reader.ReadInt32();
				for (int32 i = 0; i < Count && Reader.CanRead(12); ++i)
				{
					const int32 Index = Reader.ReadInt32();
					const int32 ObjectGuid = Reader.ReadInt32();
					Reader.Skip(4); // layered spell u16+u16 (unused)
					if (Index >= 0 && Index < 100)
					{
						if (ShortcutObjects.Num() <= Index)
						{
							ShortcutObjects.SetNumZeroed(Index + 1);
						}
						ShortcutObjects[Index] = ObjectGuid;
					}
				}
			}
		}

		if (OptionFlags & OptSpellLists8)
		{
			for (int32 Bar = 0; Bar < 8; ++Bar)
			{
				if (!Reader.CanRead(4)) { break; }
				const int32 Count = Reader.ReadInt32();
				const int32 BarSize = FMath::Max(56, Count);
				SpellBars[Bar].SetNumZeroed(BarSize);
				for (int32 i = 0; i < Count && Reader.CanRead(4); ++i)
				{
					const int32 SpellId = Reader.ReadInt32();
					if (i < BarSize)
					{
						SpellBars[Bar][i] = SpellId;
					}
				}
			}
		}
		else if (Reader.CanRead(4))
		{
			Reader.ReadUInt32(); // empty single list sentinel
		}

		DesiredComponents.Reset();
		if (OptionFlags & OptDesiredComps)
		{
			const int32 Count = ReadTableHeader();
			for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
			{
				const int32 Wcid = static_cast<int32>(Reader.ReadUInt32());
				const int32 Quantity = Reader.ReadInt32();
				if (Wcid != 0)
				{
					DesiredComponents.Add(Wcid, Quantity);
				}
			}
		}

		// SpellbookFilters is always written by ACE after DesiredComps.
		if (Reader.CanRead(4))
		{
			Reader.ReadUInt32();
		}

		if (OptionFlags & OptCharacterOptions2)
		{
			if (Reader.CanRead(4)) { CharacterOptions2 = Reader.ReadUInt32(); }
		}

		// GameplayOptions is a raw byte[] with no length prefix (System.IO.BinaryWriter).
		// Inventory + equipped lists follow it. Scan for a valid inventory footer.
		auto TryParseInventoryFooter = [this](FACEBinaryReader& R) -> bool
		{
			if (!R.CanRead(4))
			{
				return false;
			}
			const int32 Start = R.Tell();
			const uint32 InvCount = R.ReadUInt32();
			if (InvCount > 200u || !R.CanRead(static_cast<int32>(InvCount) * 8 + 4))
			{
				R.Seek(Start);
				return false;
			}
			TArray<FACEContainerItemRef> Items;
			Items.Reserve(static_cast<int32>(InvCount));
			for (uint32 i = 0; i < InvCount; ++i)
			{
				FACEContainerItemRef Ref;
				Ref.ItemGuid = static_cast<int32>(R.ReadUInt32());
				Ref.ContainerType = static_cast<int32>(R.ReadUInt32());
				if (Ref.ItemGuid == 0 || Ref.ContainerType < 0 || Ref.ContainerType > 2)
				{
					R.Seek(Start);
					return false;
				}
				// Reject GameplayOptions false-positives that claim world props (lifestone/chest).
				if (const FACEWorldObject* Obj = WorldObjects.Find(Ref.ItemGuid))
				{
					const bool bWorldPlaced = Obj->bHasPosition && Obj->ContainerId == 0
						&& Obj->WielderId == 0 && Obj->ParentGuid == 0;
					if (bWorldPlaced)
					{
						R.Seek(Start);
						return false;
					}
				}
				Items.Add(Ref);

			}
			if (!R.CanRead(4))
			{
				R.Seek(Start);
				return false;
			}
			const uint32 EquipCount = R.ReadUInt32();
			if (EquipCount > 100u || !R.CanRead(static_cast<int32>(EquipCount) * 12))
			{
				R.Seek(Start);
				return false;
			}
			TMap<int32, TPair<int64, uint32>> Equipment;
            for (uint32 I = 0; I < EquipCount; ++I)
            {
                const int32 Guid = R.ReadUInt32();
                const int64 Location = R.ReadUInt32();
                const uint32 Priority = R.ReadUInt32();
                if (!Guid || Equipment.Contains(Guid)) { R.Seek(Start); return false; }
                Equipment.Add(Guid, {Location, Priority});
            }
			// Prefer exact consume; allow tiny alignment pad.
			if (R.Remaining() > 3)
			{
				R.Seek(Start);
				return false;
			}
			ContainerContents.FindOrAdd(PlayerGuid) = MoveTemp(Items);
			LoginEquipment = MoveTemp(Equipment);
			ApplyPlayerInventoryProfile();
			return true;
		};

		const int32 TailStart = Reader.Tell();
		const int32 TailBytes = Reader.Remaining();
		bool bInvParsed = false;
		if (!(OptionFlags & OptGameplayOptions))
		{
			bInvParsed = TryParseInventoryFooter(Reader);
		}
		else
		{
			// Skip unknown GameplayOptions blob length by probing inventory start.
			for (int32 Skip = 0; Skip <= TailBytes - 8 && !bInvParsed; ++Skip)
			{
				Reader.Seek(TailStart + Skip);
				bInvParsed = TryParseInventoryFooter(Reader);
			}
		}
		if (!bInvParsed)
		{
			Reader.Seek(TailStart); // leave unread; vitals already applied
		}
	}

	Vitals.RecomputeMaxVitals();
	Vitals.bValid = true;
	PlayerVitals = Vitals;
	Log(FString::Printf(TEXT("PlayerDescription: L%d H%d/%d S%d/%d M%d/%d run=%d skills=%d spells=%d"),
		Vitals.Level, Vitals.Health, Vitals.MaxHealth, Vitals.Stamina, Vitals.MaxStamina,
		Vitals.Mana, Vitals.MaxMana, Vitals.RunSkillCurrent, Vitals.Skills.Num(), KnownSpells.Num()));
	NotifyVitalsChanged();
	OnEnchantmentsChanged.Broadcast();
}

void FACESession::HandlePrivateUpdateAttribute(FACEBinaryReader& Reader)
{
	// byte sequence, attribute enum u32, ranks u32, startingValue u32, xp u32
	if (!Reader.CanRead(17))
	{
		return;
	}
	Reader.ReadUInt8();
	const uint32 Attribute = Reader.ReadUInt32();
	const int32 Ranks = static_cast<int32>(Reader.ReadUInt32());
	const int32 Start = static_cast<int32>(Reader.ReadUInt32());
	const int32 XpSpent = static_cast<int32>(Reader.ReadUInt32());
	const int32 Current = Ranks + Start;

	switch (Attribute)
	{
	case 1:
		PlayerVitals.Strength = Current;
		PlayerVitals.StrengthRanks = Ranks;
		PlayerVitals.StrengthStart = Start;
		PlayerVitals.StrengthXpSpent = XpSpent;
		break;
	case 2:
		PlayerVitals.Endurance = Current;
		PlayerVitals.EnduranceRanks = Ranks;
		PlayerVitals.EnduranceStart = Start;
		PlayerVitals.EnduranceXpSpent = XpSpent;
		break;
	case 3:
		PlayerVitals.Quickness = Current;
		PlayerVitals.QuicknessRanks = Ranks;
		PlayerVitals.QuicknessStart = Start;
		PlayerVitals.QuicknessXpSpent = XpSpent;
		break;
	case 4:
		PlayerVitals.Coordination = Current;
		PlayerVitals.CoordinationRanks = Ranks;
		PlayerVitals.CoordinationStart = Start;
		PlayerVitals.CoordinationXpSpent = XpSpent;
		break;
	case 5:
		PlayerVitals.Focus = Current;
		PlayerVitals.FocusRanks = Ranks;
		PlayerVitals.FocusStart = Start;
		PlayerVitals.FocusXpSpent = XpSpent;
		break;
	case 6:
		PlayerVitals.Self = Current;
		PlayerVitals.SelfRanks = Ranks;
		PlayerVitals.SelfStart = Start;
		PlayerVitals.SelfXpSpent = XpSpent;
		break;
	default: return;
	}
	PlayerVitals.RecomputeMaxVitals();
	NotifyVitalsChanged();
}

void FACESession::HandlePrivateUpdateSkill(FACEBinaryReader& Reader)
{
	// byte sequence, skillId u32, ranks u16, adjustPP u16, sac u32, xp u32, init u32,
	// resistance u32, lastUsed f64 — matches GameMessagePrivateUpdateSkill / PlayerDescription.
	if (!Reader.CanRead(33))
	{
		return;
	}
	Reader.ReadUInt8();
	const int32 SkillId = static_cast<int32>(Reader.ReadUInt32());
	const int32 Ranks = static_cast<int32>(Reader.ReadUInt16());
	Reader.ReadUInt16(); // adjustPP
	const int32 Sac = static_cast<int32>(Reader.ReadUInt32());
	const int32 XpSpent = static_cast<int32>(Reader.ReadUInt32());
	const int32 InitLevel = static_cast<int32>(Reader.ReadUInt32());
	Reader.ReadUInt32(); // resistance
	Reader.ReadDouble(); // lastUsed

	FACESkillInfo* Found = nullptr;
	for (FACESkillInfo& Sk : PlayerVitals.Skills)
	{
		if (Sk.SkillId == SkillId)
		{
			Found = &Sk;
			break;
		}
	}
	if (!Found)
	{
		FACESkillInfo& Added = PlayerVitals.Skills.AddDefaulted_GetRef();
		Added.SkillId = SkillId;
		static const TCHAR* Names[] = {
			TEXT("None"), TEXT("Axe"), TEXT("Bow"), TEXT("Crossbow"), TEXT("Dagger"), TEXT("Mace"),
			TEXT("Melee Defense"), TEXT("Missile Defense"), TEXT("Sling"), TEXT("Spear"), TEXT("Staff"),
			TEXT("Sword"), TEXT("Thrown Weapon"), TEXT("Unarmed Combat"), TEXT("Arcane Lore"),
			TEXT("Magic Defense"), TEXT("Mana Conversion"), TEXT("Spellcraft"), TEXT("Item Tinkering"),
			TEXT("Assess Person"), TEXT("Deception"), TEXT("Healing"), TEXT("Jump"), TEXT("Lockpick"),
			TEXT("Run"), TEXT("Awareness"), TEXT("Arms and Armor Repair"), TEXT("Assess Creature"),
			TEXT("Weapon Tinkering"), TEXT("Armor Tinkering"), TEXT("Magic Item Tinkering"),
			TEXT("Creature Enchantment"), TEXT("Item Enchantment"), TEXT("Life Magic"), TEXT("War Magic"),
			TEXT("Leadership"), TEXT("Loyalty"), TEXT("Fletching"), TEXT("Alchemy"), TEXT("Cooking"),
			TEXT("Salvaging"), TEXT("Two Handed Combat"), TEXT("Gearcraft"), TEXT("Void Magic"),
			TEXT("Heavy Weapons"), TEXT("Light Weapons"), TEXT("Finesse Weapons"), TEXT("Missile Weapons"),
			TEXT("Shield"), TEXT("Dual Wield"), TEXT("Recklessness"), TEXT("Sneak Attack"),
			TEXT("Dirty Fighting"), TEXT("Challenge"), TEXT("Summoning")
		};
		Added.Name = (SkillId >= 0 && SkillId < static_cast<int32>(UE_ARRAY_COUNT(Names)))
			? FString(Names[SkillId])
			: FString::Printf(TEXT("Skill %d"), SkillId);
		Found = &Added;
	}
	Found->Ranks = Ranks;
	Found->AdvancementClass = Sac;
	Found->XpSpent = XpSpent;
	Found->InitLevel = InitLevel;
	Found->Current = InitLevel + Ranks;
	switch (SkillId)
	{
	case 6:  Found->Current += (PlayerVitals.Coordination + PlayerVitals.Quickness) / 3; break;
	case 7:  Found->Current += (PlayerVitals.Coordination + PlayerVitals.Quickness) / 5; break;
	case 15: Found->Current += (PlayerVitals.Focus + PlayerVitals.Self) / 7; break;
	case 22: Found->Current += (PlayerVitals.Strength + PlayerVitals.Coordination) / 2; break;
	case 24:
		Found->Current += PlayerVitals.Quickness;
		PlayerVitals.RunSkillCurrent = Found->Current;
		break;
	default: break;
	}
	NotifyVitalsChanged();
}

void FACESession::HandlePrivateUpdateVital(FACEBinaryReader& Reader)
{
	// byte sequence, vital (PropertyAttribute2nd Max*) u32, ranks u32, start u32, xp u32, current u32
	if (!Reader.CanRead(21))
	{
		return;
	}
	Reader.ReadUInt8();
	const uint32 Vital = Reader.ReadUInt32();
	const int32 Ranks = static_cast<int32>(Reader.ReadUInt32());
	const int32 Start = static_cast<int32>(Reader.ReadUInt32());
	const int32 XpSpent = static_cast<int32>(Reader.ReadUInt32());
	const int32 Current = static_cast<int32>(Reader.ReadUInt32());

	switch (Vital)
	{
	case 1: // PropertyAttribute2nd.MaxHealth — ranks/start/xp + current HP
		PlayerVitals.HealthRanks = Ranks;
		PlayerVitals.HealthStart = Start;
		PlayerVitals.HealthXpSpent = XpSpent;
		PlayerVitals.Health = Current;
		break;
	case 2: // PropertyAttribute2nd.Health / Vital.Health — current only
		PlayerVitals.Health = Current;
		break;
	case 3: // MaxStamina
		PlayerVitals.StaminaRanks = Ranks;
		PlayerVitals.StaminaStart = Start;
		PlayerVitals.StaminaXpSpent = XpSpent;
		PlayerVitals.Stamina = Current;
		break;
	case 4: // Stamina
		PlayerVitals.Stamina = Current;
		break;
	case 5: // MaxMana
		PlayerVitals.ManaRanks = Ranks;
		PlayerVitals.ManaStart = Start;
		PlayerVitals.ManaXpSpent = XpSpent;
		PlayerVitals.Mana = Current;
		break;
	case 6: // Mana
		PlayerVitals.Mana = Current;
		break;
	default: return;
	}
	PlayerVitals.RecomputeMaxVitals();
	// Current can briefly exceed unbuffed Max while enchantments are pending — keep UI sane.
	PlayerVitals.MaxHealth = FMath::Max(PlayerVitals.MaxHealth, PlayerVitals.Health);
	PlayerVitals.MaxStamina = FMath::Max(PlayerVitals.MaxStamina, PlayerVitals.Stamina);
	PlayerVitals.MaxMana = FMath::Max(PlayerVitals.MaxMana, PlayerVitals.Mana);
	PlayerVitals.bValid = true;
	NotifyVitalsChanged();
}

void FACESession::HandlePrivateUpdateAttribute2ndLevel(FACEBinaryReader& Reader)
{
	// byte sequence, vital (Health=2/Stamina=4/Mana=6) u32, current u32
	if (!Reader.CanRead(9))
	{
		return;
	}
	Reader.ReadUInt8();
	const uint32 Vital = Reader.ReadUInt32();
	const int32 Current = static_cast<int32>(Reader.ReadUInt32());

	switch (Vital)
	{
	case 2: PlayerVitals.Health = Current; break;
	case 4: PlayerVitals.Stamina = Current; break;
	case 6: PlayerVitals.Mana = Current; break;
	default: return;
	}
	PlayerVitals.RecomputeMaxVitals();
	PlayerVitals.MaxHealth = FMath::Max(PlayerVitals.MaxHealth, PlayerVitals.Health);
	PlayerVitals.MaxStamina = FMath::Max(PlayerVitals.MaxStamina, PlayerVitals.Stamina);
	PlayerVitals.MaxMana = FMath::Max(PlayerVitals.MaxMana, PlayerVitals.Mana);
	PlayerVitals.bValid = true;
	NotifyVitalsChanged();
}

void FACESession::HandlePrivateUpdatePropertyInt(FACEBinaryReader& Reader)
{
	// byte sequence, property key u32, value i32
	if (!Reader.CanRead(9))
	{
		return;
	}
	Reader.ReadUInt8();
	const uint32 Key = Reader.ReadUInt32();
	const int32 Value = Reader.ReadInt32();
	PlayerVitals.StatQualityInts.Add(static_cast<int32>(Key), Value);
	if (Key == 25) // Level
	{
		PlayerVitals.Level = Value;
		NotifyVitalsChanged();
	}
	else if (Key == 24) // AvailableSkillCredits
	{
		PlayerVitals.AvailableSkillCredits = Value;
		NotifyVitalsChanged();
	}
	else if (Key == 134) // PlayerKillerStatus
	{
		ApplyPlayerKillerStatus(PlayerGuid, Value);
	}
	else if (Key == 40) // PropertyInt.CombatMode
	{
		ApplyServerCombatMode(Value);
	}
	else if (Key == 5) // PropertyInt.EncumbranceVal
	{
		bHasPlayerEncumbrance = true;
		PlayerEncumbranceVal = Value;
		if (FACEWorldObject* Self = WorldObjects.Find(PlayerGuid))
		{
			Self->Burden = Value;
		}
	}
	else if (Key == 322) { PlayerVitals.AetheriaUnlocked = Value & 7; NotifyVitalsChanged(); }
	else if (Key == 230) // PropertyInt.AugmentationIncreasedCarryingCapacity
	{
		PlayerVitals.CarryingCapacityAugs = FMath::Clamp(Value, 0, 5);
		NotifyVitalsChanged();
	}
	else if (Key == 129) // PropertyInt.VitaeCpPool
	{
		VitaeCpPool = Value;
		OnEnchantmentsChanged.Broadcast();
	}
	else if (Key == 139) // PropertyInt.DeathLevel
	{
		DeathLevel = Value;
		OnEnchantmentsChanged.Broadcast();
	}
}

void FACESession::ApplyServerCombatMode(int32 Mode)
{
	const double Now = FPlatformTime::Seconds();
	if (PendingCombatMode != 0 && Now < PendingCombatModeUntil)
	{
		if (Mode == static_cast<int32>(PendingCombatMode))
		{
			PendingCombatMode = 0;
			DeferredServerCombatMode = INDEX_NONE;
			PlayerVitals.CombatMode = Mode;
			NotifyVitalsChanged();
			return;
		}
		// SwitchCombatStyles briefly publishes NonCombat — keep optimistic mode so
		// MoveToState / UI do not flash peace (casting recovers via LastCombatMode).
		if (Mode == static_cast<int32>(ACECombatMode::NonCombat)
			&& PendingCombatMode != ACECombatMode::NonCombat)
		{
			DeferredServerCombatMode = Mode;
			return;
		}
	}

	PendingCombatMode = 0;
	DeferredServerCombatMode = INDEX_NONE;
	if (PlayerVitals.CombatMode == Mode)
	{
		return;
	}
	PlayerVitals.CombatMode = Mode;
	NotifyVitalsChanged();
}

void FACESession::HandlePrivateUpdatePropertyInt64(FACEBinaryReader& Reader)
{
	// byte sequence, property key u32, value i64
	if (!Reader.CanRead(13))
	{
		return;
	}
	Reader.ReadUInt8();
	const uint32 Key = Reader.ReadUInt32();
	const int64 Value = static_cast<int64>(Reader.ReadUInt64());
	if (Key == 1)
	{
		PlayerVitals.TotalExperience = Value;
		NotifyVitalsChanged();
	}
	else if (Key == 2)
	{
		PlayerVitals.AvailableExperience = Value;
		NotifyVitalsChanged();
	}
	else if (Key == 6)
	{
		PlayerVitals.AvailableLuminance = Value;
		NotifyVitalsChanged();
	}
	else if (Key == 7)
	{
		PlayerVitals.MaximumLuminance = Value;
		NotifyVitalsChanged();
	}
}

void FACESession::SendTalk(const FString& Message)
{
	if (State != EACESessionState::InWorld || Message.IsEmpty())
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(Message);
	SendGameAction(ACEGameAction::Talk, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendChatChannel(uint32 ChannelId, const FString& Message)
{
	if (State != EACESessionState::InWorld || Message.IsEmpty() || ChannelId == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(ChannelId);
	W.WriteString16L(Message);
	SendGameAction(ACEGameAction::ChatChannel, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SetSelectedObjectInternal(const FACESelectedObject& Sel)
{
	const uint32 Serial = SelectedObject.SelectionSerial + 1;
	SelectedObject = Sel;
	SelectedObject.SelectionSerial = Serial;
	OnSelectionChanged.Broadcast(SelectedObject);
}

void FACESession::SelectObject(int32 ObjectGuid)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}

	FACESelectedObject Sel;
	if (ObjectGuid != 0)
	{
		FACEWorldObject Obj;
		if (GetWorldObject(ObjectGuid, Obj))
		{
			Sel.Guid = ObjectGuid;
			Sel.Name = Obj.Name;
			Sel.bValid = true;
			const bool bCreature = (Obj.ItemType & ACEItemType::Creature) != 0 || Obj.bIsPlayer;
			Sel.bShowHealth = bCreature;
			Sel.HealthFraction = 1.f;
		}
		else
		{
			Sel.Guid = ObjectGuid;
			Sel.Name = TEXT("Unknown");
			Sel.bValid = true;
		}
	}
	SetSelectedObjectInternal(Sel);

	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ObjectGuid));
	SendGameAction(ACEGameAction::QueryHealth, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendIdentifyObject(int32 ObjectGuid)
{
	if (State != EACESessionState::InWorld || ObjectGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ObjectGuid));
	SendGameAction(ACEGameAction::IdentifyObject, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendRaiseAttribute(uint32 AttributeId, uint32 XpAmount)
{
	if (State != EACESessionState::InWorld || AttributeId < 1 || AttributeId > 6 || XpAmount == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(AttributeId);
	W.WriteUInt32(XpAmount);
	SendGameAction(ACEGameAction::RaiseAttribute, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendRaiseVital(uint32 VitalId, uint32 XpAmount)
{
	// Vital IDs match PropertyAttribute2nd Max*: Health=1, Stamina=3, Mana=5.
	if (State != EACESessionState::InWorld || XpAmount == 0
		|| (VitalId != 1 && VitalId != 3 && VitalId != 5))
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(VitalId);
	W.WriteUInt32(XpAmount);
	SendGameAction(ACEGameAction::RaiseVital, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendRaiseSkill(uint32 SkillId, uint32 XpAmount)
{
	if (State != EACESessionState::InWorld || SkillId == 0 || XpAmount == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(SkillId);
	W.WriteUInt32(XpAmount);
	SendGameAction(ACEGameAction::RaiseSkill, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendTrainSkill(uint32 SkillId, int32 CreditsSpent)
{
	if (State != EACESessionState::InWorld || SkillId == 0 || CreditsSpent <= 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(SkillId);
	W.WriteInt32(CreditsSpent);
	SendGameAction(ACEGameAction::TrainSkill, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendEmote(const FString& EmoteText)
{
	if (State != EACESessionState::InWorld || EmoteText.IsEmpty())
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(EmoteText);
	SendGameAction(ACEGameAction::Emote, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendSoulEmote(const FString& EmoteText)
{
	if (State != EACESessionState::InWorld || EmoteText.IsEmpty())
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(EmoteText);
	SendGameAction(ACEGameAction::SoulEmote, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendSoulEmoteMotion(uint32 MotionCommand)
{
	// Point is the animation transition; the client-requestable soul emote is
	// PointState. Older hotkeys and DAT pose names can resolve to the transition.
	if (MotionCommand == 0x13000084u) MotionCommand = 0x430000f0u;
	if (State != EACESessionState::InWorld || MotionCommand == 0 || bLogOffPending)
	{
		return;
	}
	{
		const uint32 Cell = static_cast<uint32>(PlayerPosition.CellId);
		if ((Cell & 0xFFFFu) < 0x0100u && PlayerPosition.Location.Z < -40.f)
		{
			return;
		}
	}

	// PackedFlags: low 11 bits = RawMotionFlags; bits 11+ = CommandListLength.
	constexpr uint32 CmdListLenShift = 11u;
	uint32 PackedFlags = ACERawMotionFlags::CurrentHoldKey | ACERawMotionFlags::CurrentStyle
		| (1u << CmdListLenShift);

	FACEBinaryWriter W;
	W.WriteUInt32(PackedFlags);
	W.WriteUInt32(ACEMotion::HoldKeyNone);
	W.WriteUInt32(CurrentStance != 0 ? CurrentStance : ACEMotion::StanceNonCombat);

	// MotionItem (client autonomous): ushort command, ushort seq|0x8000, float speed.
	W.WriteUInt16(static_cast<uint16>(MotionCommand));
	++MotionActionSeq;
	W.WriteUInt16(static_cast<uint16>((MotionActionSeq & 0x7FFFu) | 0x8000u));
	W.WriteFloat(1.f);

	W.WriteUInt32(static_cast<uint32>(PlayerPosition.CellId));
	W.WriteFloat(PlayerPosition.Location.X);
	W.WriteFloat(PlayerPosition.Location.Y);
	W.WriteFloat(PlayerPosition.Location.Z);
	W.WriteFloat(PlayerPosition.RotationW);
	W.WriteFloat(PlayerPosition.RotationXYZ.X);
	W.WriteFloat(PlayerPosition.RotationXYZ.Y);
	W.WriteFloat(PlayerPosition.RotationXYZ.Z);

	W.WriteUInt16(InstanceSeq);
	W.WriteUInt16(ServerControlSeq);
	W.WriteUInt16(TeleportSeq);
	W.WriteUInt16(ForcePositionSeq);
	W.WriteUInt8(0x01); // contact
	W.Align();

	SendGameAction(ACEGameAction::MoveToState, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendTell(const FString& TargetName, const FString& Message)
{
	if (State != EACESessionState::InWorld || TargetName.IsEmpty() || Message.IsEmpty())
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(Message);
	W.WriteString16L(TargetName);
	SendGameAction(ACEGameAction::Tell, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendTalkDirect(int32 TargetGuid, const FString& Message)
{
	if (State != EACESessionState::InWorld || TargetGuid == 0 || Message.IsEmpty())
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(Message);
	W.WriteUInt32(static_cast<uint32>(TargetGuid));
	SendGameAction(ACEGameAction::TalkDirect, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendTeleToLifestone()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::TeleToLifestone, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendTeleToMarketplace()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::TeleToMarketPlace, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendTeleToHouse()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::TeleToHouse, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendTeleToMansion()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::TeleToMansion, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendRecallAllegianceHometown()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::RecallAllegianceHometown, {}, ACEQueue::WeenieQueue);
}

FString FACESession::GetPlayerName() const
{
	if (const FACEWorldObject* Obj = WorldObjects.Find(PlayerGuid))
	{
		return Obj->Name;
	}
	return FString();
}

uint32 FACESession::GetTurbineChannelId(uint32 ChatType) const
{
	switch (ChatType)
	{
	case ACETurbineChat::Allegiance: return TurbineAllegianceChannel;
	case ACETurbineChat::General: return TurbineGeneralChannel;
	case ACETurbineChat::Trade: return TurbineTradeChannel;
	case ACETurbineChat::LFG: return TurbineLfgChannel;
	case ACETurbineChat::Roleplay: return TurbineRoleplayChannel;
	case ACETurbineChat::Society:
	case ACETurbineChat::SocietyCelestialHand:
	case ACETurbineChat::SocietyEldrytchWeb:
	case ACETurbineChat::SocietyRadiantBlood:
		return TurbineSocietyChannel;
	case ACETurbineChat::Olthoi: return TurbineOlthoiChannel;
	default: return TurbineGeneralChannel;
	}
}

void FACESession::SendSimpleGameAction(uint32 ActionType)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ActionType, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendGameActionU32(uint32 ActionType, uint32 Value)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(Value);
	SendGameAction(ActionType, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendGameActionString(uint32 ActionType, const FString& Value)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(Value);
	SendGameAction(ActionType, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendGameActionStringU32(uint32 ActionType, const FString& Value, uint32 Extra)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteString16L(Value);
	W.WriteUInt32(Extra);
	SendGameAction(ActionType, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendTurbineChat(uint32 ChannelId, uint32 ChatType, const FString& Message)
{
	if (State != EACESessionState::InWorld || Message.IsEmpty())
	{
		return;
	}
	const uint32 ContextId = TurbineChatContextId++;
	if (TurbineChatContextId == 0 || TurbineChatContextId > 0x71u)
	{
		TurbineChatContextId = 1;
	}

	FACEBinaryWriter W;
	const int32 FirstSizePos = W.Num();
	W.WriteUInt32(0);
	W.WriteUInt32(ACETurbineChat::BlobRequestBinary);
	W.WriteUInt32(ACETurbineChat::DispatchById);
	W.WriteUInt32(1);
	W.WriteUInt32(0);
	W.WriteUInt32(0);
	W.WriteUInt32(0);
	W.WriteUInt32(0);
	const int32 SecondSizePos = W.Num();
	W.WriteUInt32(0);
	W.WriteUInt32(ContextId);
	W.WriteUInt32(2);
	W.WriteUInt32(2);
	W.WriteUInt32(ChannelId);
	W.WritePackedUnicode(Message);
	W.WriteUInt32(0x0Cu);
	W.WriteUInt32(static_cast<uint32>(PlayerGuid));
	W.WriteUInt32(0);
	W.WriteUInt32(ChatType);
	W.WriteUInt32At(FirstSizePos, static_cast<uint32>(W.Num() - FirstSizePos - 4));
	W.WriteUInt32At(SecondSizePos, static_cast<uint32>(W.Num() - SecondSizePos - 4));
	SendGameMessage(ACEOpcode::TurbineChat, W.GetData(), ACEQueue::LoginQueue);
}

void FACESession::SendPutItemInContainer(int32 ItemGuid, int32 ContainerGuid, int32 Placement)
{
	if (State != EACESessionState::InWorld || ItemGuid == 0 || ContainerGuid == 0)
	{
		return;
	}
	// Retail keeps the authoritative IDList until ServerSaysContainID. Updating it
	// optimistically re-applied shifts when acknowledgements arrived after another
	// move, so the displayed order differed from the order saved on the server.
	// Placement is an insertion index, never a sparse grid address (IDList::AddAtNum).
	if (const FACEWorldObject* Item = WorldObjects.Find(ItemGuid))
	{
		int32 Count = 0;
		for (const auto& Pair : WorldObjects)
		{
			const auto& Other = Pair.Value;
			if (Other.Guid != ItemGuid && Other.ContainerId == ContainerGuid
				&& Other.CurrentWieldedLocation == 0 && Other.WielderId == 0 && Other.ParentGuid == 0
				&& IsPackSlotItem(Other) == IsPackSlotItem(*Item)) ++Count;
		}
		Placement = FMath::Clamp(Placement, 0, Count);
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ItemGuid));
	W.WriteUInt32(static_cast<uint32>(ContainerGuid));
	W.WriteInt32(Placement);
	SendGameAction(ACEGameAction::PutItemInContainer, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendStackableMerge(int32 MergeFromGuid, int32 MergeToGuid, int32 Amount)
{
	if (State != EACESessionState::InWorld || MergeFromGuid == 0 || MergeToGuid == 0 || Amount <= 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(MergeFromGuid));
	W.WriteUInt32(static_cast<uint32>(MergeToGuid));
	W.WriteInt32(Amount);
	SendGameAction(ACEGameAction::StackableMerge, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendStackableSplitToContainer(int32 StackGuid, int32 ContainerGuid, int32 Placement, int32 Amount)
{
	if (State != EACESessionState::InWorld || StackGuid == 0 || ContainerGuid == 0 || Amount <= 0)
	{
		return;
	}
	TArray<FACEWorldObject> Destination;
	GetPackItems(ContainerGuid, Destination);
	Placement = FMath::Clamp(Placement, 0, Destination.Num());
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(StackGuid));
	W.WriteUInt32(static_cast<uint32>(ContainerGuid));
	W.WriteInt32(Placement);
	W.WriteInt32(Amount);
	SendGameAction(ACEGameAction::StackableSplitToContainer, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendStackableSplitTo3D(int32 StackGuid, int32 Amount)
{
	if (State != EACESessionState::InWorld || StackGuid == 0 || Amount <= 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(StackGuid));
	W.WriteInt32(Amount);
	SendGameAction(ACEGameAction::StackableSplitTo3D, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendStackableSplitToWield(int32 StackGuid, int64 WieldLocation, int32 Amount)
{
	if (State != EACESessionState::InWorld || StackGuid == 0 || WieldLocation == 0 || Amount <= 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(StackGuid));
	W.WriteInt32(static_cast<int32>(WieldLocation));
	W.WriteInt32(Amount);
	SendGameAction(ACEGameAction::StackableSplitToWield, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendUseWithTarget(int32 SourceGuid, int32 TargetGuid)
{
	if (State != EACESessionState::InWorld || SourceGuid == 0 || TargetGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(SourceGuid));
	W.WriteUInt32(static_cast<uint32>(TargetGuid));
	SendGameAction(ACEGameAction::UseWithTarget, W.GetData(), ACEQueue::WeenieQueue);
	bUseBusy = true;
	Log(FString::Printf(TEXT("UseWithTarget src=0x%08X tgt=0x%08X"), SourceGuid, TargetGuid));
}

void FACESession::SendSetTitle(uint32 TitleId)
{
	if (State != EACESessionState::InWorld || TitleId == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(TitleId);
	SendGameAction(ACEGameAction::TitleSet, W.GetData(), ACEQueue::WeenieQueue);
}

bool FACESession::IsCharacterOptionSet(int32 Option) const
{
	const FACECharacterOptionDesc* Desc = ACECharacterOptions::Find(Option);
	if (!Desc)
	{
		return false;
	}
	const uint32 Bits = Desc->bInOptions2 ? CharacterOptions2 : CharacterOptions1;
	return (Bits & Desc->Flag) != 0;
}

void FACESession::SendSetSingleCharacterOption(int32 Option, bool bValue)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	if (const FACECharacterOptionDesc* Desc = ACECharacterOptions::Find(Option))
	{
		uint32& Bits = Desc->bInOptions2 ? CharacterOptions2 : CharacterOptions1;
		if (bValue) { Bits |= Desc->Flag; } else { Bits &= ~Desc->Flag; }
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(Option));
	W.WriteUInt32(bValue ? 1u : 0u);
	SendGameAction(ACEGameAction::SetSingleCharacterOption, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendCharacterOptions(uint32 Options1, uint32 Options2)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	CharacterOptions1 = Options1;
	CharacterOptions2 = Options2;
	// Minimal retail payload: flags with only CharacterOptions2 present, then options1, the
	// always-written tab-1 spell count, then options2.
	FACEBinaryWriter W;
	W.WriteUInt32(0x00000040u);
	W.WriteUInt32(Options1);
	W.WriteUInt32(0);
	W.WriteUInt32(Options2);
	SendGameAction(ACEGameAction::SetCharacterOptions, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendGetAndWieldItem(int32 ItemGuid, int64 WieldLocation)
{
	if (State != EACESessionState::InWorld || ItemGuid == 0 || WieldLocation == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ItemGuid));
	W.WriteInt32(static_cast<int32>(WieldLocation));
	SendGameAction(ACEGameAction::GetAndWieldItem, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendDropItem(int32 ItemGuid)
{
	if (State != EACESessionState::InWorld || ItemGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ItemGuid));
	SendGameAction(ACEGameAction::DropItem, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendGiveObjectRequest(int32 TargetGuid, int32 ItemGuid, int32 Amount)
{
	if (State != EACESessionState::InWorld || TargetGuid == 0 || ItemGuid == 0 || Amount <= 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(TargetGuid));
	W.WriteUInt32(static_cast<uint32>(ItemGuid));
	W.WriteInt32(Amount);
	SendGameAction(ACEGameAction::GiveObjectRequest, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendNoLongerViewingContents(int32 ContainerGuid)
{
	if (State != EACESessionState::InWorld || ContainerGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ContainerGuid));
	SendGameAction(ACEGameAction::NoLongerViewingContents, W.GetData(), ACEQueue::WeenieQueue);
	if (OpenExternalContainerGuid == ContainerGuid)
	{
		OpenExternalContainerGuid = 0;
	}
	if (OpenVendorGuid == ContainerGuid)
	{
		OpenVendorGuid = 0;
	}
}

void FACESession::SendBuyItems(int32 VendorGuid, const TArray<TPair<int32, int32>>& AmountAndObjectId)
{
	if (State != EACESessionState::InWorld || VendorGuid == 0 || AmountAndObjectId.Num() == 0)
	{
		return;
	}
	// Revalidate at send time: stock can change while a purchase is in the cart.
	TArray<TPair<int32,int32>> Available;
	if (VendorGuid != OpenVendorGuid) return;
	for (const auto& Pair : AmountAndObjectId)
	{
		const auto* Stock=VendorMerchandise.FindByPredicate([&](const FACEWorldObject& Item){return Item.Guid==Pair.Value;});
		if (!Stock || Pair.Key<=0) continue;
		const int32 Limit=ACEInventoryRules::VendorPurchaseLimit(*Stock);
		if (Limit<=0) continue;
		if (auto* Existing=Available.FindByPredicate([&](const auto& Item){return Item.Value==Pair.Value;}))
			Existing->Key=static_cast<int32>(FMath::Min<int64>(Limit,static_cast<int64>(Existing->Key)+Pair.Key));
		else Available.Emplace(FMath::Min(Pair.Key,Limit),Pair.Value);
	}
	if (Available.IsEmpty()) return;
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(VendorGuid));
	W.WriteUInt32(static_cast<uint32>(Available.Num()));
	for (const TPair<int32, int32>& Pair : Available)
	{
		W.WriteInt32(Pair.Key);   // amount
		W.WriteUInt32(static_cast<uint32>(Pair.Value)); // objectID
	}
	SendGameAction(ACEGameAction::Buy, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendSellItems(int32 VendorGuid, const TArray<TPair<int32, int32>>& AmountAndObjectId)
{
	if (State != EACESessionState::InWorld || VendorGuid == 0 || AmountAndObjectId.Num() == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(VendorGuid));
	W.WriteUInt32(static_cast<uint32>(AmountAndObjectId.Num()));
	for (const TPair<int32, int32>& Pair : AmountAndObjectId)
	{
		W.WriteInt32(Pair.Key);   // amount
		W.WriteUInt32(static_cast<uint32>(Pair.Value)); // objectID
	}
	SendGameAction(ACEGameAction::Sell, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendOpenTrade(int32 OtherPlayerGuid)
{
	if (State != EACESessionState::InWorld || OtherPlayerGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(OtherPlayerGuid));
	SendGameAction(ACEGameAction::OpenTradeNegotiations, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendCloseTrade()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::CloseTradeNegotiations, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendAddToTrade(int32 ItemGuid, int32 TradeSlotSide)
{
	if (State != EACESessionState::InWorld || ItemGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ItemGuid));
	W.WriteUInt32(static_cast<uint32>(TradeSlotSide));
	SendGameAction(ACEGameAction::AddToTrade, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendAcceptTrade()
{
	if (State != EACESessionState::InWorld || TradePartnerGuid == 0)
	{
		return;
	}
	// ACE GameActionAcceptTrade reads a Trade blob then ignores it.
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(TradePartnerGuid));
	W.WriteDouble(0.0);
	W.WriteUInt32(0); // tradeStatus
	W.WriteUInt32(static_cast<uint32>(TradeInitiatorGuid != 0 ? TradeInitiatorGuid : PlayerGuid));
	W.WriteUInt32(1); // initiatorAccepts
	W.WriteUInt32(0); // partnerAccepts
	SendGameAction(ACEGameAction::AcceptTrade, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendDeclineTrade()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::DeclineTrade, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendResetTrade()
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	SendGameAction(ACEGameAction::ResetTrade, {}, ACEQueue::WeenieQueue);
}

void FACESession::SendAddSpellToBar(int32 SpellId, int32 SlotIndex, int32 BarIndex)
{
	if (State != EACESessionState::InWorld || SpellId == 0)
	{
		return;
	}
	BarIndex = FMath::Clamp(BarIndex, 0, 7);
	if (SpellBars.Num() < 8)
	{
		SpellBars.SetNum(8);
	}
	TArray<int32>& Bar = SpellBars[BarIndex];
	// Favorites are a growing list; the thirteen-cell viewport is not a storage limit.

	// Dense retail bars: count filled prefix (trailing zeros are empty).
	auto CountFilled = [&Bar]() -> int32
	{
		int32 N = 0;
		for (int32 i = 0; i < Bar.Num(); ++i)
		{
			if (Bar[i] == 0) { break; }
			++N;
		}
		return N;
	};

	// Server AddSpellToBar rejects duplicates — remove first when relocating.
	int32 ExistingIdx = INDEX_NONE;
	for (int32 i = 0; i < Bar.Num(); ++i)
	{
		if (Bar[i] == SpellId)
		{
			ExistingIdx = i;
			break;
		}
	}
	if (ExistingIdx != INDEX_NONE)
	{
		FACEBinaryWriter Rem;
		Rem.WriteUInt32(static_cast<uint32>(SpellId));
		Rem.WriteUInt32(static_cast<uint32>(BarIndex));
		SendGameAction(ACEGameAction::RemoveFromSpellBar, Rem.GetData(), ACEQueue::WeenieQueue);
		for (int32 i = ExistingIdx; i < Bar.Num() - 1; ++i)
		{
			Bar[i] = Bar[i + 1];
		}
		Bar[Bar.Num() - 1] = 0;
		if (SlotIndex > ExistingIdx)
		{
			--SlotIndex;
		}
	}

	int32 Count = CountFilled();
	// ACE Character.AddSpellToBar: clamp past-end drops onto the first empty (append).
	int32 InsertAt = FMath::Clamp(SlotIndex, 0, Count);
	if (InsertAt > Count)
	{
		InsertAt = Count;
	}
	if (Bar.Num() <= Count)
	{
		Bar.Add(0);
	}

	// ACE HandleActionAddSpellFavorite: spellId, spellBarPositionId (0-based), spellBarId (0-based).
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(SpellId));
	W.WriteUInt32(static_cast<uint32>(InsertAt));
	W.WriteUInt32(static_cast<uint32>(BarIndex));
	SendGameAction(ACEGameAction::AddToSpellBar, W.GetData(), ACEQueue::WeenieQueue);

	// Mirror Character.AddSpellToBar insert/shift — server does not echo bar changes.
	for (int32 i = Count; i > InsertAt; --i)
	{
		Bar[i] = Bar[i - 1];
	}
	Bar[InsertAt] = SpellId;
}

void FACESession::SendRemoveSpellFromBar(int32 SpellId, int32 BarIndex)
{
	if (State != EACESessionState::InWorld || SpellId == 0)
	{
		return;
	}
	BarIndex = FMath::Clamp(BarIndex, 0, 7);
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(SpellId));
	W.WriteUInt32(static_cast<uint32>(BarIndex));
	SendGameAction(ACEGameAction::RemoveFromSpellBar, W.GetData(), ACEQueue::WeenieQueue);

	if (SpellBars.IsValidIndex(BarIndex))
	{
		TArray<int32>& Bar = SpellBars[BarIndex];
		int32 Found = INDEX_NONE;
		for (int32 i = 0; i < Bar.Num(); ++i)
		{
			if (Bar[i] == SpellId)
			{
				Found = i;
				break;
			}
		}
		if (Found != INDEX_NONE)
		{
			for (int32 i = Found; i < Bar.Num() - 1; ++i)
			{
				Bar[i] = Bar[i + 1];
			}
			Bar[Bar.Num() - 1] = 0;
		}
	}
}

void FACESession::SendSetDesiredComponentLevel(int32 ComponentWcid, int32 Amount)
{
	if (State != EACESessionState::InWorld || ComponentWcid == 0)
	{
		return;
	}
	const int32 Clamped = FMath::Clamp(Amount, 0, 5000);
	if (Clamped > 0)
	{
		DesiredComponents.Add(ComponentWcid, Clamped);
	}
	else
	{
		DesiredComponents.Remove(ComponentWcid);
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ComponentWcid));
	W.WriteUInt32(static_cast<uint32>(Clamped));
	SendGameAction(ACEGameAction::SetDesiredComponentLevel, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendAddShortcut(int32 SlotIndex, int32 ObjectGuid)
{
	if (State != EACESessionState::InWorld || ObjectGuid == 0)
	{
		return;
	}
	const uint32 Index = static_cast<uint32>(FMath::Clamp(SlotIndex, 0, 17));
	// Shortcut wire: index u32, objectId u32, LayeredSpell (spellId u16 + layer u16) unused zeros.
	FACEBinaryWriter W;
	W.WriteUInt32(Index);
	W.WriteUInt32(static_cast<uint32>(ObjectGuid));
	W.WriteUInt16(0);
	W.WriteUInt16(0);
	SendGameAction(ACEGameAction::AddShortCut, W.GetData(), ACEQueue::WeenieQueue);

	if (ShortcutObjects.Num() <= static_cast<int32>(Index))
	{
		ShortcutObjects.SetNumZeroed(static_cast<int32>(Index) + 1);
	}
	ShortcutObjects[static_cast<int32>(Index)] = ObjectGuid;
}

void FACESession::SendRemoveShortcut(int32 SlotIndex)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	const uint32 Index = static_cast<uint32>(FMath::Clamp(SlotIndex, 0, 17));
	FACEBinaryWriter W;
	W.WriteUInt32(Index);
	SendGameAction(ACEGameAction::RemoveShortCut, W.GetData(), ACEQueue::WeenieQueue);
	if (ShortcutObjects.IsValidIndex(static_cast<int32>(Index)))
	{
		ShortcutObjects[static_cast<int32>(Index)] = 0;
	}
}

void FACESession::SendCastSpell(int32 SpellId, int32 TargetGuid)
{
	if (State != EACESessionState::InWorld || SpellId == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	if (TargetGuid != 0)
	{
		W.WriteUInt32(static_cast<uint32>(TargetGuid));
		W.WriteUInt32(static_cast<uint32>(SpellId));
		SendGameAction(ACEGameAction::CastTargetedSpell, W.GetData(), ACEQueue::WeenieQueue);
	}
	else
	{
		W.WriteUInt32(static_cast<uint32>(SpellId));
		SendGameAction(ACEGameAction::CastUntargetedSpell, W.GetData(), ACEQueue::WeenieQueue);
	}
}

const TArray<int32>& FACESession::GetSpellBar(int32 BarIndex) const
{
	static const TArray<int32> Empty;
	if (!SpellBars.IsValidIndex(BarIndex))
	{
		return Empty;
	}
	return SpellBars[BarIndex];
}

int32 FACESession::GetShortcutObject(int32 SlotIndex) const
{
	return ShortcutObjects.IsValidIndex(SlotIndex) ? ShortcutObjects[SlotIndex] : 0;
}

void FACESession::SetActiveSpellBar(int32 BarIndex)
{
	ActiveSpellBar = FMath::Clamp(BarIndex, 0, 7);
}

void FACESession::HandleMagicUpdateSpell(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 SpellId = static_cast<int32>(Reader.ReadUInt16());
	Reader.ReadUInt16(); // layer
	if (SpellId > 0)
	{
		KnownSpells.AddUnique(SpellId);
	}
}

void FACESession::HandleMagicRemoveSpell(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 SpellId = static_cast<int32>(Reader.ReadUInt16());
	Reader.ReadUInt16();
	KnownSpells.Remove(SpellId);
}

bool FACESession::ReadEnchantmentRecord(FACEBinaryReader& Reader, FACEActiveEnchantment& Out)
{
	// Matches ACE.Server Network.Structure.Enchantment.Write
	if (!Reader.CanRead(60))
	{
		return false;
	}
	Out = FACEActiveEnchantment();
	Out.ReceivedAt = FPlatformTime::Seconds();
	Out.SpellId = static_cast<int32>(Reader.ReadUInt16());
	Out.Layer = static_cast<int32>(Reader.ReadUInt16());
	Out.SpellCategory = static_cast<int32>(Reader.ReadUInt16());
	const uint16 HasSet = Reader.ReadUInt16();
	Out.PowerLevel = static_cast<int32>(Reader.ReadUInt32()); // PowerLevel
	Out.StartTime = static_cast<float>(Reader.ReadDouble());
	Out.Duration = static_cast<float>(Reader.ReadDouble());
	Out.CasterGuid = static_cast<int32>(Reader.ReadUInt32());
	Reader.ReadFloat();  // DegradeModifier
	Reader.ReadFloat();  // DegradeLimit
	Reader.ReadDouble(); // LastTimeDegraded
	Out.StatModType = static_cast<int32>(Reader.ReadUInt32());
	Out.StatModKey = static_cast<int32>(Reader.ReadUInt32());
	Out.StatModValue = Reader.ReadFloat();
	if (HasSet != 0)
	{
		if (!Reader.CanRead(4))
		{
			return false;
		}
		Reader.ReadUInt32(); // SpellSetID
	}
	constexpr uint32 FlagBeneficial = 0x2000000u;
	constexpr uint32 FlagVitae = 0x0800000u;
	constexpr uint32 FlagCooldown = 0x1000000u;
	Out.bBeneficial = (static_cast<uint32>(Out.StatModType) & FlagBeneficial) != 0;
	Out.bVitae = (static_cast<uint32>(Out.StatModType) & FlagVitae) != 0 || Out.SpellId == 666;
	Out.bCooldown = (static_cast<uint32>(Out.StatModType) & FlagCooldown) != 0;
	return true;
}

void FACESession::UpsertEnchantment(const FACEActiveEnchantment& Entry)
{
	ActiveEnchantments.RemoveAll([&Entry](const FACEActiveEnchantment& E)
	{
		return E.SpellId == Entry.SpellId && E.Layer == Entry.Layer && E.CasterGuid == Entry.CasterGuid;
	});
	ActiveEnchantments.Add(Entry);
}

void FACESession::RemoveEnchantment(uint16 SpellId, uint16 Layer)
{
	ActiveEnchantments.RemoveAll([SpellId, Layer](const FACEActiveEnchantment& E)
	{
		return E.SpellId == static_cast<int32>(SpellId) && E.Layer == static_cast<int32>(Layer);
	});
}

void FACESession::NotifyEnchantmentsChanged()
{
	RecomputeAttributeEnchantments(PlayerVitals);
	NotifyVitalsChanged();
	OnEnchantmentsChanged.Broadcast();
}

void FACESession::GetActiveEnchantments(TArray<FACEActiveEnchantment>& Out) const
{
	Out.Reset();
	for (const FACEActiveEnchantment& E : ActiveEnchantments)
	{
		if (!E.bCooldown)
		{
			Out.Add(E);
		}
	}
}

bool FACESession::TryGetVitaeMultiplier(float& OutMultiplier) const
{
	for (const FACEActiveEnchantment& E : ActiveEnchantments)
	{
		if (E.bVitae || E.SpellId == 666)
		{
			OutMultiplier = E.StatModValue;
			return true;
		}
	}
	return false;
}

void FACESession::SendPingRequest()
{
	if (State != EACESessionState::InWorld || (PingRequestSentAt > 0.0 && FPlatformTime::Seconds() - PingRequestSentAt < 120.0))
	{
		return;
	}
	PingRequestSentAt = FPlatformTime::Seconds();
	SendGameAction(ACEGameAction::PingRequest, {}, ACEQueue::UIQueue);
}

void FACESession::RecordLinkTraffic(double Now, uint32 Sent, uint32 Retransmits)
{
	const int64 Second = FMath::FloorToInt64(Now);
	auto& Bucket = LinkTraffic[Second % UE_ARRAY_COUNT(LinkTraffic)];
	if (Bucket.Second != Second) { Bucket = FLinkTrafficBucket(); Bucket.Second = Second; }
	Bucket.Sent += Sent;
	Bucket.Retransmits += Retransmits;
}

FACELinkStatus FACESession::GetLinkStatus() const
{
	return GetLinkStatusAt(FPlatformTime::Seconds());
}

FACELinkStatus FACESession::GetLinkStatusAt(double Now) const
{
	FACELinkStatus Status = LinkStatus;
	Status.bConnected = State >= EACESessionState::CharacterSelect && State < EACESessionState::Failed;
	Status.SecondsSinceLastPacket = LastServerPacketAt > 0.0 ? FMath::Max(0.0, Now - LastServerPacketAt) : 0.f;
	uint64 Sent = 0, Retransmits = 0;
	const int64 Second = FMath::FloorToInt64(Now);
	for (const auto& Bucket : LinkTraffic)
		if (Bucket.Second >= 0 && Bucket.Second <= Second && Second - Bucket.Second < UE_ARRAY_COUNT(LinkTraffic))
		{ Sent += Bucket.Sent; Retransmits += Bucket.Retransmits; }
	Status.PacketLossPercent = Sent + Retransmits > 0 ? 100.f * Retransmits / (Sent + Retransmits) : 0.f;
	return Status;
}

float FACESession::RecordEchoRequest(double SentAt)
{
	if (EchoTimeOrigin == 0.0) EchoTimeOrigin = SentAt - 1.0;
	const float Token = static_cast<float>(SentAt - EchoTimeOrigin);
	for (auto It = PendingEchoTimes.CreateIterator(); It; ++It)
		if (SentAt - It.Value() > 30.0) It.RemoveCurrent();
	PendingEchoTimes.Add(Token, SentAt);
	return Token;
}

void FACESession::UpdateLinkStatusFromEcho(float ClientTimeSent, double ReceivedAt)
{
	// The wire timestamp is float. Never subtract two absolute float clock values:
	// FPlatformTime's large origin can round a normal internet roundtrip to zero.
	double SentAt = 0.0;
	if (PendingEchoTimes.RemoveAndCopyValue(ClientTimeSent, SentAt)
		&& ReceivedAt > SentAt && ReceivedAt - SentAt <= 30.0)
	{
		LinkStatus.RoundTripSeconds = static_cast<float>(ReceivedAt - SentAt);
		LinkStatus.bHasPing = true;
	}
	OnLinkStatusChanged.Broadcast(GetLinkStatus());
}

void FACESession::HandlePingResponse(FACEBinaryReader& /*Reader*/)
{
	if (PingRequestSentAt > 0.0)
	{
		LinkStatus.RoundTripSeconds = static_cast<float>(FPlatformTime::Seconds() - PingRequestSentAt);
		LinkStatus.bHasPing = true;
		PingRequestSentAt = 0.0;
	}
	OnLinkStatusChanged.Broadcast(GetLinkStatus());
}

void FACESession::RecomputeAttributeEnchantments(FACEPlayerVitals& OutVitals)
{
	auto Reset = [](float& Mul, float& Add)
	{
		Mul = 1.f;
		Add = 0.f;
	};
	Reset(OutVitals.StrengthEnchantMul, OutVitals.StrengthEnchantAdd);
	Reset(OutVitals.EnduranceEnchantMul, OutVitals.EnduranceEnchantAdd);
	Reset(OutVitals.QuicknessEnchantMul, OutVitals.QuicknessEnchantAdd);
	Reset(OutVitals.CoordinationEnchantMul, OutVitals.CoordinationEnchantAdd);
	Reset(OutVitals.FocusEnchantMul, OutVitals.FocusEnchantAdd);
	Reset(OutVitals.SelfEnchantMul, OutVitals.SelfEnchantAdd);

	constexpr uint32 FlagAttribute = 0x0000001u;
	constexpr uint32 FlagMultiplicative = 0x0004000u;
	constexpr uint32 FlagAdditive = 0x0008000u;

	for (const FACEActiveEnchantment& E : ActiveEnchantments)
	{
		if (E.bCooldown || (static_cast<uint32>(E.StatModType) & FlagAttribute) == 0)
		{
			continue;
		}
		float* Mul = nullptr;
		float* Add = nullptr;
		switch (E.StatModKey)
		{
		case 1: Mul = &OutVitals.StrengthEnchantMul; Add = &OutVitals.StrengthEnchantAdd; break;
		case 2: Mul = &OutVitals.EnduranceEnchantMul; Add = &OutVitals.EnduranceEnchantAdd; break;
		case 3: Mul = &OutVitals.QuicknessEnchantMul; Add = &OutVitals.QuicknessEnchantAdd; break;
		case 4: Mul = &OutVitals.CoordinationEnchantMul; Add = &OutVitals.CoordinationEnchantAdd; break;
		case 5: Mul = &OutVitals.FocusEnchantMul; Add = &OutVitals.FocusEnchantAdd; break;
		case 6: Mul = &OutVitals.SelfEnchantMul; Add = &OutVitals.SelfEnchantAdd; break;
		default: break;
		}
		if (!Mul || !Add)
		{
			continue;
		}
		if ((static_cast<uint32>(E.StatModType) & FlagMultiplicative) != 0)
		{
			*Mul *= E.StatModValue;
		}
		else if ((static_cast<uint32>(E.StatModType) & FlagAdditive) != 0)
		{
			*Add += E.StatModValue;
		}
	}
}

void FACESession::HandleMagicUpdateEnchantment(FACEBinaryReader& Reader)
{
	FACEActiveEnchantment Entry;
	if (!ReadEnchantmentRecord(Reader, Entry))
	{
		return;
	}
	UpsertEnchantment(Entry);
	NotifyEnchantmentsChanged();
}

void FACESession::HandleMagicRemoveEnchantment(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint16 SpellId = Reader.ReadUInt16();
	const uint16 Layer = Reader.ReadUInt16();
	const int32 Before = ActiveEnchantments.Num();
	RemoveEnchantment(SpellId, Layer);
	if (ActiveEnchantments.Num() != Before)
	{
		NotifyEnchantmentsChanged();
	}
}

void FACESession::HandleMagicUpdateMultipleEnchantments(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint32 Count = Reader.ReadUInt32();
	for (uint32 i = 0; i < Count; ++i)
	{
		FACEActiveEnchantment Entry;
		if (!ReadEnchantmentRecord(Reader, Entry))
		{
			break;
		}
		UpsertEnchantment(Entry);
	}
	NotifyEnchantmentsChanged();
}

void FACESession::HandleMagicRemoveMultipleEnchantments(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const uint32 Count = Reader.ReadUInt32();
	bool bChanged = false;
	for (uint32 i = 0; i < Count && Reader.CanRead(4); ++i)
	{
		const uint16 SpellId = Reader.ReadUInt16();
		const uint16 Layer = Reader.ReadUInt16();
		const int32 Before = ActiveEnchantments.Num();
		RemoveEnchantment(SpellId, Layer);
		bChanged |= ActiveEnchantments.Num() != Before;
	}
	if (bChanged)
	{
		NotifyEnchantmentsChanged();
	}
}

void FACESession::HandleMagicPurgeEnchantments(FACEBinaryReader& /*Reader*/)
{
	if (ActiveEnchantments.Num() == 0)
	{
		return;
	}
	// Keep Vitae; purge other enchantments (retail MagicPurgeEnchantments).
	ActiveEnchantments.RemoveAll([](const FACEActiveEnchantment& E)
	{
		return !E.bVitae && E.SpellId != 666;
	});
	NotifyEnchantmentsChanged();
}

void FACESession::HandleMagicPurgeBadEnchantments(FACEBinaryReader& /*Reader*/)
{
	const int32 Before = ActiveEnchantments.Num();
	ActiveEnchantments.RemoveAll([](const FACEActiveEnchantment& E)
	{
		return !E.bBeneficial && !E.bVitae && !E.bCooldown && E.SpellId != 666;
	});
	if (ActiveEnchantments.Num() != Before)
	{
		NotifyEnchantmentsChanged();
	}
}

void FACESession::HandleMagicDispelEnchantment(FACEBinaryReader& Reader)
{
	HandleMagicRemoveEnchantment(Reader);
}

void FACESession::HandleMagicDispelMultipleEnchantments(FACEBinaryReader& Reader)
{
	HandleMagicRemoveMultipleEnchantments(Reader);
}

bool FACESession::IsNearbyHealthObject(int32 Guid) const
{
	if (Guid == PlayerGuid) return true;
	const auto* Object = WorldObjects.Find(Guid);
	return Object && Object->bHasPosition && Object->Position.CellId && PlayerPosition.CellId
		&& FVector::DistSquared(Object->Position.ToUnrealLocation(1.f),PlayerPosition.ToUnrealLocation(1.f)) <= FMath::Square(192.f);
}

void FACESession::HandleUpdateHealth(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(8))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const float Fraction = Reader.ReadFloat();
	if (!FMath::IsFinite(Fraction) || !IsNearbyHealthObject(Guid)) return;
	OnObjectHealth.Broadcast(Guid, Fraction);
	if (SelectedObject.bValid && SelectedObject.Guid == Guid)
	{
		SelectedObject.HealthFraction = FMath::Clamp(Fraction, 0.f, 1.f);
		SelectedObject.bShowHealth = true;
		OnSelectionChanged.Broadcast(SelectedObject);
	}
}

void FACESession::HandleIdentifyObjectResponse(FACEBinaryReader& Reader)
{
	// GameEventIdentifyObjectResponse: u32 objectGuid + AppraiseInfo
	if (!Reader.CanRead(12))
	{
		return;
	}
	FACEAppraisalInfo Info;
	Info.ObjectGuid = static_cast<int32>(Reader.ReadUInt32());
	const uint32 Flags = Reader.ReadUInt32();
	Info.bSuccess = Reader.ReadUInt32() != 0;

	FACEWorldObject Known;
	if (GetWorldObject(Info.ObjectGuid, Known))
	{
		Info.Name = Known.Name;
		Info.ItemType = Known.ItemType;
		Info.bIsCreature = (Known.ItemType & ACEItemType::Creature) != 0 || Known.bIsPlayer;
		if (Known.IconId != 0)
		{
			Info.IconDid = Known.IconId;
		}
	}

	auto ReadTableHeader = [&Reader]() -> int32
	{
		if (!Reader.CanRead(4)) { return -1; }
		const uint16 Count = Reader.ReadUInt16();
		Reader.ReadUInt16();
		return Count;
	};

	FString Summary;
	constexpr uint32 FlagInt = 0x0001;
	constexpr uint32 FlagBool = 0x0002;
	constexpr uint32 FlagFloat = 0x0004;
	constexpr uint32 FlagString = 0x0008;
	constexpr uint32 FlagSpellBook = 0x0010;
	constexpr uint32 FlagWeapon = 0x0020;
	constexpr uint32 FlagHook = 0x0040;
	constexpr uint32 FlagArmor = 0x0080;
	constexpr uint32 FlagCreature = 0x0100;
	constexpr uint32 FlagArmorEnc = 0x0200;
	constexpr uint32 FlagResistEnc = 0x0400;
	constexpr uint32 FlagWeaponEnc = 0x0800;
	constexpr uint32 FlagDid = 0x1000;
	constexpr uint32 FlagInt64 = 0x2000;
	constexpr uint32 FlagArmorLevels = 0x4000;

	if (Flags & FlagInt)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
		{
			const uint32 Prop = Reader.ReadUInt32();
			const int32 Val = Reader.ReadInt32();
			Info.IntProperties.Add(Prop, Val);
			if (Prop == 1) { Info.ItemType = Val; }
			else if (Prop == 25) { Info.Level = Val; }
			else if (Prop == 2) { Info.CreatureType = Val; }
			else if (Prop == 19) { Info.Value = Val; Info.bHasValue = true; }
			else if (Prop == 5) { Info.Burden = Val; Info.bHasBurden = true; }
			else if (Prop == 33) // Bonded
			{
				if (FACEWorldObject* Obj = WorldObjects.Find(Info.ObjectGuid))
				{
					Obj->Bonded = Val;
				}
			}
			else if (Prop == 114) // Attuned
			{
				if (FACEWorldObject* Obj = WorldObjects.Find(Info.ObjectGuid))
				{
					Obj->Attuned = Val;
				}
			}
			else if (Prop == 18) // UiEffects
			{
				if (FACEWorldObject* Obj = WorldObjects.Find(Info.ObjectGuid))
				{
					Obj->UiEffects = Val;
				}
			}
			else if (Prop == 307) { Info.DamageRating = Val; }
			else if (Prop == 308) { Info.DamageResistRating = Val; }
			else if (Prop == 313) { Info.CritRating = Val; }
			else if (Prop == 314) { Info.CritDamageRating = Val; }
			else if (Prop == 315) { Info.CritResistRating = Val; }
			else if (Prop == 316) { Info.CritDamageResistRating = Val; }
		}
	}
	if (Flags & FlagInt64)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(12); ++i)
		{
			const uint32 Prop = Reader.ReadUInt32();
			Info.Int64Properties.Add(Prop, Reader.ReadUInt64());
		}
	}
	if (Flags & FlagBool)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
		{
			const uint32 Prop = Reader.ReadUInt32();
			Info.BoolProperties.Add(Prop, Reader.ReadUInt32() != 0);
		}
	}
	if (Flags & FlagFloat)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(12); ++i)
		{
			const uint32 Prop = Reader.ReadUInt32();
			Info.FloatProperties.Add(Prop, Reader.ReadDouble());
		}
	}
	if (Flags & FlagString)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(4); ++i)
		{
			const uint32 Prop = Reader.ReadUInt32();
			const FString Val = Reader.ReadString16L();
			Info.StringProperties.Add(Prop, Val);
			if (Prop == 1) { Info.Name = Val; }
			else if (Prop == 7) // PropertyString.Inscription
			{
				Info.Inscription = Val;
			}
			else if ((Prop == 14 || Prop == 15 || Prop == 16) && !Val.IsEmpty()) { Summary += Val + TEXT("\n"); }
		}
	}
	if (Flags & FlagDid)
	{
		const int32 Count = ReadTableHeader();
		for (int32 i = 0; i < Count && Reader.CanRead(8); ++i)
		{
			const uint32 Prop = Reader.ReadUInt32();
			const uint32 Did = Reader.ReadUInt32();
			if (Prop == 8 && Did != 0) // PropertyDataId.Icon
			{
				Info.IconDid = static_cast<int32>(Did);
			}
			ApplyPropertyDataID(Info.ObjectGuid, Prop, Did);
		}
	}
	if (Flags & FlagSpellBook)
	{
		if (Reader.CanRead(4))
		{
			const uint32 SpellCount = Reader.ReadUInt32();
			for (uint32 i = 0; i < SpellCount && Reader.CanRead(4); ++i)
			{
				const int32 SpellId = static_cast<int32>(Reader.ReadUInt32() & 0x7FFFFFFF);
				Info.SpellIds.Add(SpellId);
			}
			if (Info.SpellIds.Num() > 0)
			{
				Summary += FString::Printf(TEXT("Spells (%d)\n"), Info.SpellIds.Num());
				for (int32 Sid : Info.SpellIds)
				{
					Summary += FString::Printf(TEXT("  Spell %d\n"), Sid);
				}
			}
		}
	}
	if (Flags & FlagArmor)
	{
		if (Reader.CanRead(32))
		{
			const float Slash = Reader.ReadFloat();
			const float Pierce = Reader.ReadFloat();
			const float Bludgeon = Reader.ReadFloat();
			const float Cold = Reader.ReadFloat();
			const float Fire = Reader.ReadFloat();
			const float Acid = Reader.ReadFloat();
			const float Nether = Reader.ReadFloat();
			const float Electric = Reader.ReadFloat();
			Info.ArmorResistances = {Slash, Pierce, Bludgeon, Cold, Fire, Acid, Nether, Electric};
		}
	}
	if (Flags & FlagCreature)
	{
		if (Reader.CanRead(12))
		{
			const uint32 CreatureFlags = Reader.ReadUInt32();
			Info.Health = static_cast<int32>(Reader.ReadUInt32());
			Info.MaxHealth = static_cast<int32>(Reader.ReadUInt32());
			Info.bIsCreature = true;
			if (CreatureFlags & 0x8) // ShowAttributes — 10 u32 attrs/vitals
			{
				if (Reader.CanRead(40))
				{
					Info.Strength = static_cast<int32>(Reader.ReadUInt32());
					Info.Endurance = static_cast<int32>(Reader.ReadUInt32());
					Info.Quickness = static_cast<int32>(Reader.ReadUInt32());
					Info.Coordination = static_cast<int32>(Reader.ReadUInt32());
					Info.Focus = static_cast<int32>(Reader.ReadUInt32());
					Info.Self = static_cast<int32>(Reader.ReadUInt32());
					Info.Stamina = static_cast<int32>(Reader.ReadUInt32());
					Info.Mana = static_cast<int32>(Reader.ReadUInt32());
					Info.MaxStamina = static_cast<int32>(Reader.ReadUInt32());
					Info.MaxMana = static_cast<int32>(Reader.ReadUInt32());
					Summary += FString::Printf(
						TEXT("Str %d  End %d  Qui %d\nCoo %d  Foc %d  Sel %d\nStamina %d/%d  Mana %d/%d\n"),
						Info.Strength, Info.Endurance, Info.Quickness,
						Info.Coordination, Info.Focus, Info.Self,
						Info.Stamina, Info.MaxStamina, Info.Mana, Info.MaxMana);
				}
			}
			if (CreatureFlags & 0x1)
			{
				if (Reader.CanRead(4))
				{
					Info.AttributeHighlights = Reader.ReadUInt16();
					Info.AttributeColors = Reader.ReadUInt16();
				}
			}
		}
	}
	if (Flags & FlagWeapon)
	{
		if (Reader.CanRead(60))
		{
			Info.bHasWeaponProfile = true;
			Info.DamageType = static_cast<int32>(Reader.ReadUInt32());
			Info.WeaponTime = static_cast<int32>(Reader.ReadUInt32());
			Info.WeaponSkill = Reader.ReadUInt32();
			Info.Damage = static_cast<int32>(Reader.ReadUInt32());
			Info.DamageVariance = static_cast<float>(Reader.ReadDouble());
			Info.WeaponDamageMod = Reader.ReadDouble();
			Info.WeaponLength = Reader.ReadDouble();
			Info.WeaponMaxVelocity = Reader.ReadDouble();
			Info.WeaponOffense = static_cast<float>(Reader.ReadDouble());
			Info.bWeaponMaxVelocityEstimated = Reader.ReadUInt32() != 0;
			Summary += FString::Printf(TEXT("Damage %d  Speed %d  Offense %.2f\n"),
				Info.Damage, Info.WeaponTime, Info.WeaponOffense);
		}
	}
	if (Flags & FlagHook)
	{
		if (Reader.CanRead(12)) { Reader.Skip(12); }
	}
	if (Flags & FlagArmorEnc) { if (Reader.CanRead(4)) Reader.Skip(4); }
	if (Flags & FlagWeaponEnc) { if (Reader.CanRead(4)) Reader.Skip(4); }
	if (Flags & FlagResistEnc) { if (Reader.CanRead(4)) Reader.Skip(4); }
	if (Flags & FlagArmorLevels)
	{
		if (Reader.CanRead(36))
		{
			Info.ArmorLevels.SetNum(9);
			for (int32 i = 0; i < 9; ++i)
			{
				Info.ArmorLevels[i] = static_cast<int32>(Reader.ReadUInt32());
			}
			Summary += FString::Printf(
				TEXT("Armor Levels\n  Head %d  Chest %d  Abdomen %d\n  UpperArm %d  LowerArm %d  Hand %d\n  UpperLeg %d  LowerLeg %d  Foot %d\n"),
				Info.ArmorLevels[0], Info.ArmorLevels[1], Info.ArmorLevels[2],
				Info.ArmorLevels[3], Info.ArmorLevels[4], Info.ArmorLevels[5],
				Info.ArmorLevels[6], Info.ArmorLevels[7], Info.ArmorLevels[8]);
		}
	}

	if (Info.Name.IsEmpty()) { Info.Name = FString::Printf(TEXT("Object 0x%08X"), Info.ObjectGuid); }
	// Mag-nus ItemExamineUI hosts Value/Burden in dedicated chrome (ItemValueText /
	// ItemBurdenText) — keep them out of the body Summary so they are not duplicated.
	FString Header;
	if (Info.Level > 0) { Header += FString::Printf(TEXT("Level %d\n"), Info.Level); }
	if (Info.bIsCreature && Info.MaxHealth > 0)
	{
		Header += FString::Printf(TEXT("Health  %d / %d\n"), Info.Health, Info.MaxHealth);
	}
	if (Info.DamageRating || Info.DamageResistRating || Info.CritRating || Info.CritDamageRating
		|| Info.CritResistRating || Info.CritDamageResistRating)
	{
		Header += FString::Printf(
			TEXT("Ratings  Dmg %d  DR %d  Crit %d  CritDmg %d\n  CritResist %d  CritDmgResist %d\n"),
			Info.DamageRating, Info.DamageResistRating, Info.CritRating, Info.CritDamageRating,
			Info.CritResistRating, Info.CritDamageResistRating);
	}
	Info.Summary = (Header + Summary).TrimStartAndEnd();
	OnAppraisal.Broadcast(Info);
}

void FACESession::HandleViewContents(FACEBinaryReader& Reader)
{
	// GameEventViewContents: u32 containerGuid, u32 itemCount, then (u32 itemGuid, u32 containerType)*
	if (!Reader.CanRead(8))
	{
		return;
	}
	const int32 ContainerGuid = static_cast<int32>(Reader.ReadUInt32());
	const uint32 ItemCount = Reader.ReadUInt32();
	TArray<FACEContainerItemRef> Items;
	Items.Reserve(static_cast<int32>(ItemCount));
	for (uint32 i = 0; i < ItemCount && Reader.CanRead(8); ++i)
	{
		FACEContainerItemRef Entry;
		Entry.ItemGuid = static_cast<int32>(Reader.ReadUInt32());
		Entry.ContainerType = static_cast<int32>(Reader.ReadUInt32());
		Items.Add(Entry);

		// Keep ObjectCreate records consistent when ViewContents arrives first or without Container flag.
		if (FACEWorldObject* Existing = WorldObjects.Find(Entry.ItemGuid))
		{
			if (Existing->ContainerId == 0)
			{
				Existing->ContainerId = ContainerGuid;
			}

		}
	}
	ContainerContents.FindOrAdd(ContainerGuid) = MoveTemp(Items);
	RestampContainerListPlacements(ContainerGuid);

	bool bIsPlayerInventory = (ContainerGuid == PlayerGuid);
	if (!bIsPlayerInventory)
	{
		TArray<FACEWorldObject> Packs;
		GetPlayerPacks(Packs);
		for (const FACEWorldObject& Pack : Packs)
		{
			if (Pack.Guid == ContainerGuid)
			{
				bIsPlayerInventory = true;
				break;
			}
		}
	}

	bool bExternalByType = false;
	bool bIsVendor = false;
	if (const FACEWorldObject* ContainerObj = WorldObjects.Find(ContainerGuid))
	{
		bIsVendor = ContainerObj->IsVendor();
		bExternalByType = ContainerObj->IsCorpse() || ContainerObj->IsOpenable();
	}

	// Vendors use ApproachVendor (0x0062) for UI — don't open ExternalContainer for them.
	if (bIsVendor)
	{
		return;
	}

	if (!bIsPlayerInventory || bExternalByType)
	{
		OpenExternalContainerGuid = ContainerGuid;
		OnViewContentsExternal.Broadcast(ContainerGuid);
	}
}

void FACESession::HandleCloseGroundContainer(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 ContainerGuid = static_cast<int32>(Reader.ReadUInt32());
	// Do not clear ContainerContents here — loot UI may still be open (PendingLootClose /
	// range debounce). Clearing made GetPackItems fall back to an unstable TMap scan and
	// the item list appeared to flip. Binder calls ClearContainerContents on hide.
	if (OpenExternalContainerGuid == ContainerGuid)
	{
		OpenExternalContainerGuid = 0;
	}
	OnCloseGroundContainer.Broadcast(ContainerGuid);
}

void FACESession::ClearContainerContents(int32 ContainerGuid)
{
	if (ContainerGuid != 0)
	{
		ContainerContents.Remove(ContainerGuid);
	}
}

bool FACESession::CanVendorBuyItem(const FACEWorldObject& Item) const
{
	// Retail VendorProfile::InqAcceptability uses the per-unit public value.
	if ((VendorItemTypes & static_cast<uint32>(Item.ItemType)) == 0
		|| (Item.ObjectDescriptionFlags & ACEObjectDescFlag::Retained) != 0) return false;
	const int32 UnitValue = Item.Value / FMath::Max(1, Item.StackSize);
	if (UnitValue <= 0) return false;
	if (VendorMaxValue != -1 && UnitValue > VendorMaxValue)
		return (Item.ItemType & ACEItemType::PromissoryNote) != 0;
	return VendorMinValue == -1 || UnitValue >= VendorMinValue;
}

void FACESession::HandleApproachVendor(FACEBinaryReader& Reader)
{
	// GameEventApproachVendor: vendorGuid + merchandise header + GameDataOnly item list.
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 VendorGuid = static_cast<int32>(Reader.ReadUInt32());
	OpenVendorGuid = VendorGuid;
	VendorMerchandise.Reset();
	VendorBuyRate = 1.f;
	VendorItemTypes = MAX_uint32;
	VendorMinValue = VendorMaxValue = -1;
	VendorSellRate = 1.f;

	// merchandiseItemTypes, minValue, maxValue, dealMagic, buyPrice, sellPrice, altCurrencyWcid
	if (Reader.CanRead(28))
	{
		VendorItemTypes = Reader.ReadUInt32();
		VendorMinValue = static_cast<int32>(Reader.ReadUInt32());
		VendorMaxValue = static_cast<int32>(Reader.ReadUInt32());
		Reader.ReadUInt32(); // DealMagicalItems
		VendorBuyRate = Reader.ReadFloat();
		VendorSellRate = Reader.ReadFloat();
		Reader.ReadUInt32(); // AlternateCurrency WCID
	}
	// altCurrencyCount
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32();
	}
	// altCurrency plural name (String16L)
	if (Reader.CanRead(4))
	{
		Reader.ReadString16L();
	}

	if (!Reader.CanRead(4))
	{
		OnApproachVendor.Broadcast(VendorGuid);
		return;
	}
	const int32 NumItems = static_cast<int32>(Reader.ReadUInt32());
	TArray<FACEContainerItemRef>& Contents = ContainerContents.FindOrAdd(VendorGuid);
	Contents.Reset();
	for (int32 i = 0; i < NumItems && Reader.CanRead(4); ++i)
	{
		// packed: (stackSize & 0xFFFFFF) | (pwdType << 24); pwdType always -1 (PublicWeenieDesc).
		const uint32 Supply = Reader.ReadUInt32() & 0x00FFFFFFu;
		FACEDecodedObject Decoded;
		if (!FACEObjectCreateParser::ParseGameDataOnly(Reader, Decoded) || !Decoded.bParseOk)
		{
			break;
		}
		FACEWorldObject Obj;
		Obj.Guid = Decoded.Guid;
		Obj.Name = Decoded.Name;
		Obj.WeenieClassId = Decoded.WeenieClassId;
		Obj.IconId = Decoded.IconId;
		Obj.IconOverlayId = Decoded.IconOverlayId;
		Obj.IconUnderlayId = Decoded.IconUnderlayId;
		Obj.UiEffects = static_cast<int32>(Decoded.UiEffects);
		Obj.ItemType = Decoded.ItemType;
		Obj.Value = Decoded.Value;
		Obj.Burden = Decoded.Burden;
		Obj.VendorQuantityAvailable = Supply == 0x00FFFFFFu ? -1 : static_cast<int32>(Supply);
		Obj.StackSize = Decoded.StackSize;
		Obj.MaxStackSize = Decoded.MaxStackSize;
		Obj.Structure = Decoded.Structure;
		Obj.MaxStructure = Decoded.MaxStructure;
		Obj.ItemUseable = Decoded.ItemUseable;
		Obj.TargetType = Decoded.TargetType;
		Obj.MaterialType = Decoded.MaterialType;
		Obj.ValidLocations = static_cast<int64>(Decoded.ValidLocations);
		Obj.ObjectDescriptionFlags = Decoded.ObjectDescriptionFlags;
		Obj.ContainerId = VendorGuid;
		WorldObjects.Add(Obj.Guid, Obj);
		VendorMerchandise.Add(Obj);
		FACEContainerItemRef Ref;
		Ref.ItemGuid = Obj.Guid;
		Ref.ContainerType = 0;
		Contents.Add(Ref);
	}

	OnApproachVendor.Broadcast(VendorGuid);
}

void FACESession::HandleRegisterTrade(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(8))
	{
		return;
	}
	const int32 InitiatorGuid = static_cast<int32>(Reader.ReadUInt32());
	const int32 PartnerGuid = static_cast<int32>(Reader.ReadUInt32());
	if (Reader.CanRead(8))
	{
		Reader.Skip(8); // unused long
	}
	TradeAcceptedByGuid = 0;
	TradeSelfItems.Reset();
	TradePartnerItems.Reset();
	TradeInitiatorGuid = InitiatorGuid;
	if (InitiatorGuid == PlayerGuid)
	{
		TradePartnerGuid = PartnerGuid;
	}
	else
	{
		TradePartnerGuid = InitiatorGuid;
	}
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::RegisterTrade));
}

void FACESession::HandleOpenTrade(FACEBinaryReader& Reader)
{
	if (Reader.CanRead(4))
	{
		const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
		if (TradePartnerGuid == 0 && Guid != 0 && Guid != PlayerGuid)
		{
			TradePartnerGuid = Guid;
		}
	}
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::OpenTrade));
}

void FACESession::HandleCloseTrade(FACEBinaryReader& Reader)
{
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // EndTradeReason
	}
	TradePartnerGuid = 0;
	TradeInitiatorGuid = 0;
	TradeAcceptedByGuid = 0;
	TradeSelfItems.Reset();
	TradePartnerItems.Reset();
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::CloseTrade));
}

void FACESession::HandleAcceptTradeEvent(FACEBinaryReader& Reader)
{
	if (Reader.CanRead(4))
	{
		TradeAcceptedByGuid = static_cast<int32>(Reader.ReadUInt32());
	}
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::AcceptTrade));
}

void FACESession::HandleDeclineTradeEvent(FACEBinaryReader& Reader)
{
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // whoDeclined
	}
	TradeAcceptedByGuid = 0;
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::DeclineTrade));
}

void FACESession::HandleResetTradeEvent(FACEBinaryReader& Reader)
{
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // whoReset
	}
	// Retail: resetting the trade clears both accept flags.
	TradeAcceptedByGuid = 0;
	TradeSelfItems.Reset();
	TradePartnerItems.Reset();
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::ResetTrade));
}

void FACESession::HandleAddToTradeEvent(FACEBinaryReader& Reader)
{
	// objectGuid, tradeSide (1=self, 2=partner), location
	if (!Reader.CanRead(12))
	{
		OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::AddToTrade));
		return;
	}
	const int32 ItemGuid = static_cast<int32>(Reader.ReadUInt32());
	const int32 TradeSide = Reader.ReadInt32();
	Reader.ReadInt32(); // location
	TArray<int32>& Side = (TradeSide == 2) ? TradePartnerItems : TradeSelfItems;
	Side.Remove(ItemGuid);
	Side.Add(ItemGuid);
	TradeAcceptedByGuid = 0;
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::AddToTrade));
}

void FACESession::HandleRemoveFromTradeEvent(FACEBinaryReader& Reader)
{
	// objectGuid, tradeSide
	if (Reader.CanRead(8))
	{
		const int32 ItemGuid = static_cast<int32>(Reader.ReadUInt32());
		const int32 TradeSide = Reader.ReadInt32();
		TArray<int32>& Side = (TradeSide == 2) ? TradePartnerItems : TradeSelfItems;
		Side.Remove(ItemGuid);
	}
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::RemoveFromTrade));
}

void FACESession::HandleTradeFailure(FACEBinaryReader& Reader)
{
	if (Reader.CanRead(8))
	{
		Reader.Skip(8); // objectGuid, reason
	}
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::TradeFailure));
}

void FACESession::HandleClearTradeAcceptance(FACEBinaryReader& Reader)
{
	(void)Reader;
	// ACE clears ItemsInTradeWindow when broadcasting ClearTradeAcceptance.
	TradeAcceptedByGuid = 0;
	TradeSelfItems.Reset();
	TradePartnerItems.Reset();
	OnTradeEvent.Broadcast(static_cast<int32>(ACEGameEvent::ClearTradeAcceptance));
}

void FACESession::HandleCharacterTitle(FACEBinaryReader& Reader)
{
	// status(1), displayTitleId, numTitles, titleId[]
	if (!Reader.CanRead(12))
	{
		return;
	}
	Reader.ReadUInt32(); // status
	DisplayTitleId = Reader.ReadUInt32();
	const uint32 Num = Reader.ReadUInt32();
	CharacterTitleIds.Reset();
	CharacterTitleIds.Reserve(static_cast<int32>(Num));
	for (uint32 i = 0; i < Num && Reader.CanRead(4); ++i)
	{
		CharacterTitleIds.Add(Reader.ReadUInt32());
	}
	OnCharacterTitlesChanged.Broadcast();
}

void FACESession::HandleUpdateTitle(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(8))
	{
		return;
	}
	const uint32 TitleId = Reader.ReadUInt32();
	const uint32 bSetDisplay = Reader.ReadUInt32();
	if (!CharacterTitleIds.Contains(TitleId))
	{
		CharacterTitleIds.Add(TitleId);
	}
	if (bSetDisplay != 0)
	{
		DisplayTitleId = TitleId;
	}
	OnCharacterTitlesChanged.Broadcast();
}

void FACESession::HandleUseDone(FACEBinaryReader& Reader)
{
	uint32 Err = 0;
	if (Reader.CanRead(4))
	{
		Err = Reader.ReadUInt32();
	}
	bUseBusy = false;
	OnUseDone.Broadcast(Err);
	if (Err != 0)
	{
		const FString Msg = ACECombatChat::LookupWeenieError(Err);
		if (!Msg.IsEmpty())
		{
			OnChatMessage.Broadcast(Msg, TEXT(""), ACEChatMessageType::TransientInfo);
		}
	}
}

void FACESession::HandleItemAppraiseDone(FACEBinaryReader& Reader)
{
	(void)Reader;
	// Retail unlocks appraisal UI; IdentifyObjectResponse already delivered the payload.
}

const TArray<FACEContainerItemRef>* FACESession::GetContainerContents(int32 ContainerGuid) const
{
	return ContainerContents.Find(ContainerGuid);
}

void FACESession::RemoveFromContainerLists(int32 ItemGuid)
{
	for (TPair<int32, TArray<FACEContainerItemRef>>& Pair : ContainerContents)
	{
		Pair.Value.RemoveAll([ItemGuid](const FACEContainerItemRef& Ref)
		{
			return Ref.ItemGuid == ItemGuid;
		});
	}
}

bool FACESession::IsPackSlotItem(const FACEWorldObject& Obj)
{
	return Obj.ItemsCapacity > 0 || Obj.ContainersCapacity > 0
		|| (Obj.ItemType & ACEItemType::Container) != 0
		|| (Obj.ObjectDescriptionFlags & ACEObjectDescFlag::RequiresPackSlot) != 0;
}

int32 FACESession::ContentsInsertIndexForPlacement(const TArray<FACEContainerItemRef>& Contents,
	int32 Placement, int32 ContainerType)
{
	const bool bWantPack = ContainerType != 0;
	if (Placement < 0)
	{
		return Contents.Num();
	}
	int32 MatchingSeen = 0;
	for (int32 i = 0; i < Contents.Num(); ++i)
	{
		const bool bPack = Contents[i].ContainerType != 0;
		if (bPack != bWantPack)
		{
			continue;
		}
		if (MatchingSeen == Placement)
		{
			return i;
		}
		++MatchingSeen;
	}
	for (int32 i = Contents.Num() - 1; i >= 0; --i)
	{
		if ((Contents[i].ContainerType != 0) == bWantPack)
		{
			return i + 1;
		}
	}
	return Contents.Num();
}

void FACESession::ShiftContainerPlacementsAfterRemove(int32 ContainerGuid, int32 RemovedPlacement,
	bool bPackSlots)
{
	if (ContainerGuid == 0 || RemovedPlacement < 0)
	{
		return;
	}
	for (TPair<int32, FACEWorldObject>& Pair : WorldObjects)
	{
		FACEWorldObject& Obj = Pair.Value;
		if (Obj.ContainerId != ContainerGuid
			|| Obj.CurrentWieldedLocation != 0 || Obj.WielderId != 0 || Obj.ParentGuid != 0)
		{
			continue;
		}
		if (IsPackSlotItem(Obj) != bPackSlots)
		{
			continue;
		}
		if (Obj.PlacementPosition > RemovedPlacement)
		{
			--Obj.PlacementPosition;
		}
	}
}

void FACESession::ShiftContainerPlacementsForInsert(int32 ContainerGuid, int32 ItemGuid,
	int32 Placement, bool bPackSlots)
{
	if (ContainerGuid == 0 || ItemGuid == 0)
	{
		return;
	}
	int32 Count = 0;
	for (const auto& Pair : WorldObjects)
	{
		const auto& Other = Pair.Value;
		if (Other.Guid != ItemGuid && Other.ContainerId == ContainerGuid
			&& Other.CurrentWieldedLocation == 0 && Other.WielderId == 0 && Other.ParentGuid == 0
			&& IsPackSlotItem(Other) == bPackSlots) ++Count;
	}
	if (const auto* Refs = ContainerContents.Find(ContainerGuid))
    {
        int32 RefCount = 0;
        for (const auto& Ref : *Refs)
            if (Ref.ItemGuid != ItemGuid && (Ref.ContainerType != 0) == bPackSlots) ++RefCount;
        Count = FMath::Max(Count, RefCount);
    }
    const int32 Place = FMath::Clamp(Placement, 0, Count);
	for (TPair<int32, FACEWorldObject>& Pair : WorldObjects)
	{
		FACEWorldObject& Obj = Pair.Value;
		if (Obj.Guid == ItemGuid)
		{
			continue;
		}
		if (Obj.ContainerId != ContainerGuid
			|| Obj.CurrentWieldedLocation != 0 || Obj.WielderId != 0 || Obj.ParentGuid != 0)
		{
			continue;
		}
		if (IsPackSlotItem(Obj) != bPackSlots)
		{
			continue;
		}
		if (Obj.PlacementPosition >= Place)
		{
			++Obj.PlacementPosition;
		}
	}
	if (FACEWorldObject* Moved = WorldObjects.Find(ItemGuid))
	{
		Moved->ContainerId = ContainerGuid;
		Moved->PlacementPosition = Place;
	}
}

void FACESession::NormalizeContainerPlacements(int32 ContainerGuid, bool bPackSlots,
	int32 InsertGuid, int32 InsertAt)
{
	if (ContainerGuid == 0)
	{
		return;
	}
	TArray<FACEWorldObject*> Items;
	FACEWorldObject* Inserted = nullptr;
	for (TPair<int32, FACEWorldObject>& Pair : WorldObjects)
	{
		FACEWorldObject& Obj = Pair.Value;
		if (Obj.ContainerId != ContainerGuid
			|| Obj.CurrentWieldedLocation != 0 || Obj.WielderId != 0 || Obj.ParentGuid != 0)
		{
			continue;
		}
		if (IsPackSlotItem(Obj) != bPackSlots)
		{
			continue;
		}
		if (InsertGuid != 0 && Obj.Guid == InsertGuid)
		{
			Inserted = &Obj;
			continue;
		}
		Items.Add(&Obj);
	}
	// TArray<T*>::Sort dereferences pointers before invoking the predicate.
	Items.Sort([](const FACEWorldObject& A, const FACEWorldObject& B)
	{
		const int32 Pa = A.PlacementPosition >= 0 ? A.PlacementPosition : MAX_int32;
		const int32 Pb = B.PlacementPosition >= 0 ? B.PlacementPosition : MAX_int32;
		if (Pa != Pb)
		{
			return Pa < Pb;
		}
		return A.Guid < B.Guid;
	});
	if (Inserted)
	{
		const int32 Ins = (InsertAt >= 0) ? FMath::Clamp(InsertAt, 0, Items.Num()) : Items.Num();
		Items.Insert(Inserted, Ins);
	}
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		Items[i]->PlacementPosition = i;
	}
}

void FACESession::ApplyPlayerInventoryProfile()
{
    if (!PlayerGuid) return;
    if (auto* Pending = ContainerContents.Find(0))
    {
        auto Items = MoveTemp(*Pending);
        ContainerContents.Remove(0);
        ContainerContents.Add(PlayerGuid, MoveTemp(Items));
    }
    RestampContainerListPlacements(PlayerGuid);
    for (const auto& Entry : LoginEquipment)
    {
        FACEWorldObject& Obj = WorldObjects.FindOrAdd(Entry.Key);
        Obj.Guid = Entry.Key;
        Obj.ContainerId = 0;
        Obj.WielderId = PlayerGuid;
        Obj.CurrentWieldedLocation = Entry.Value.Key;
        Obj.ClothingPriority = Entry.Value.Value;
        Obj.PlacementPosition = INDEX_NONE;
    }
    LoginEquipment.Reset();
}

void FACESession::RestampContainerListPlacements(int32 ContainerGuid)
{
	TArray<FACEContainerItemRef>* Contents = ContainerContents.Find(ContainerGuid);
	if (!Contents)
	{
		return;
	}
	int32 ItemPlace = 0;
	int32 PackPlace = 0;
	for (const FACEContainerItemRef& Ref : *Contents)
	{
		FACEWorldObject* Obj = WorldObjects.Find(Ref.ItemGuid);
		const bool bPack = Ref.ContainerType != 0 || (Obj && IsPackSlotItem(*Obj));
		const int32 Placement = bPack ? PackPlace++ : ItemPlace++;
		// Unknown objects still occupy their wire-list position. ObjectCreate may
		// arrive after this authoritative contents snapshot.
		if (Obj && ContainerGuid != 0)
        {
            Obj->PlacementPosition = Placement;
            Obj->ContainerId = ContainerGuid;
            Obj->WielderId = Obj->ParentGuid = 0;
            Obj->CurrentWieldedLocation = 0;
            Obj->ParentLocation = 0;
        }
	}
}

void FACESession::ApplyPropertyDataID(int32 ObjectGuid, uint32 PropertyId, uint32 Value)
{
	FACEWorldObject* Obj = WorldObjects.Find(ObjectGuid);
	if (!Obj || Value == 0)
	{
		return;
	}
	switch (PropertyId)
	{
	case 8: // PropertyDataId.Icon
		Obj->IconId = static_cast<int32>(Value);
		break;
	case 28: // PropertyDataId.Spell — wand innate / item SpellDID
		Obj->SpellDID = static_cast<int32>(Value);
		break;
	case 50: // PropertyDataId.IconOverlay
		Obj->IconOverlayId = static_cast<int32>(Value);
		break;
	case 52: // PropertyDataId.IconUnderlay
		Obj->IconUnderlayId = static_cast<int32>(Value);
		break;
	default:
		break;
	}
}

void FACESession::HandleInventoryPutObjInContainer(FACEBinaryReader& Reader)
{
	// GameEventItemServerSaysContainId: itemGuid, containerGuid, placement, containerType.
	if (!Reader.CanRead(16))
	{
		return;
	}
	const int32 ItemGuid = static_cast<int32>(Reader.ReadUInt32());
	const int32 ContainerGuid = static_cast<int32>(Reader.ReadUInt32());
	const int32 Placement = Reader.ReadInt32();
	const int32 ContainerType = Reader.ReadInt32();

	int32 PrevContainer = 0;
	int32 PrevPlacement = INDEX_NONE;
	bool bPackSlots = ContainerType != 0;
	if (FACEWorldObject* Existing = WorldObjects.Find(ItemGuid))
	{
		PrevContainer = Existing->ContainerId;
		PrevPlacement = Existing->PlacementPosition;
		bPackSlots = ContainerType != 0 || IsPackSlotItem(*Existing);
	}

	FACEWorldObject* Obj = WorldObjects.Find(ItemGuid);
	if (!Obj)
	{
		// ContainId can arrive after we forgot a foreign object's world entry; keep a stub for UI.
		FACEWorldObject Stub;
		Stub.Guid = ItemGuid;
		WorldObjects.Add(ItemGuid, Stub);
		Obj = WorldObjects.Find(ItemGuid);
	}
	// Always drop prior container membership (corpse/chest/pack) before re-homing.
	RemoveFromContainerLists(ItemGuid);
	Obj->CurrentWieldedLocation = 0;
	Obj->WielderId = 0;
	Obj->ParentGuid = 0;
	Obj->ParentLocation = 0;
	Obj->ContainerId = 0;
	if (PrevContainer != 0)
	{
		ShiftContainerPlacementsAfterRemove(PrevContainer, PrevPlacement, bPackSlots);
	}
	ShiftContainerPlacementsForInsert(ContainerGuid, ItemGuid, Placement, bPackSlots);

	// Item is now in a container — never leave a freestanding world mesh behind.
	RemoveObjectFromWorldVisual(ItemGuid);

	// Keep ContainerContents in sync when a list already exists (PlayerDescription /
	// ViewContents). Never FindOrAdd a side-pack list here — a 1-item invented list would
	// hide the rest of that pack's ObjectCreate inventory in GetPackItems. Main pack
	// GetPackItems uses the ObjectCreate ContainerId scan and does not need this list.
	if (TArray<FACEContainerItemRef>* Contents = ContainerContents.Find(ContainerGuid))
	{
		FACEContainerItemRef Ref;
		Ref.ItemGuid = ItemGuid;
		Ref.ContainerType = ContainerType;
		Contents->Insert(Ref,
			ContentsInsertIndexForPlacement(*Contents, Placement, Ref.ContainerType));
	}
    if (PrevContainer) RestampContainerListPlacements(PrevContainer);
    RestampContainerListPlacements(ContainerGuid);
}

void FACESession::HandleWieldItem(FACEBinaryReader& Reader)
{
	// GameEventWieldItem: itemGuid, wield location (EquipMask).
	if (!Reader.CanRead(8))
	{
		return;
	}
	const int32 ItemGuid = static_cast<int32>(Reader.ReadUInt32());
	const int64 Location = static_cast<int64>(Reader.ReadInt32());
	// Retail: weapons in the same hand replace each other. Clothing/armor uses
	// ClothingPriority (CoverageMask) — pants UnderwearLegs and boots Feet share
	// LowerLegWear on EquipMask but must NOT clear each other.
	constexpr int64 WeaponHand = ACEEquipMask::MeleeWeapon | ACEEquipMask::TwoHanded
		| ACEEquipMask::MissileWeapon | ACEEquipMask::Held;
	constexpr int64 HeldVisual = WeaponHand | ACEEquipMask::Shield;
	const bool bWeaponHand = (Location & WeaponHand) != 0;
	int32 NewItemType = 0;
	if (FACEWorldObject* NewObj = WorldObjects.Find(ItemGuid))
	{
		NewItemType = NewObj->ItemType;
	}
	const uint32 NewCov = ACEInferClothingPriority(Location, NewItemType);

	for (TPair<int32, FACEWorldObject>& Pair : WorldObjects)
	{
		if (Pair.Key == ItemGuid)
		{
			continue;
		}
		FACEWorldObject& Other = Pair.Value;
		const bool bOurs = Other.WielderId == PlayerGuid || Other.ParentGuid == PlayerGuid;
		if (!bOurs || Other.CurrentWieldedLocation == 0)
		{
			continue;
		}
		const int64 EqLoc = Other.CurrentWieldedLocation;
		bool bOverlap = false;
		if (bWeaponHand && (EqLoc & WeaponHand) != 0)
		{
			bOverlap = true;
		}
		else if (NewCov != 0)
		{
			const uint32 EqCov = ACEInferClothingPriority(EqLoc, Other.ItemType);
			if (EqCov != 0)
			{
				bOverlap = (NewCov & EqCov) != 0;
			}
			else if ((EqLoc & Location) != 0)
			{
				bOverlap = true;
			}
		}
		else if ((EqLoc & Location) != 0)
		{
			bOverlap = true;
		}
		if (!bOverlap)
		{
			continue;
		}
		const bool bHadHeldMesh = Other.ParentLocation != 0;
		Other.CurrentWieldedLocation = 0;
		Other.WielderId = 0;
		Other.ParentGuid = 0;
		Other.ParentLocation = 0;
		if (Other.ContainerId == 0)
		{
			Other.ContainerId = PlayerGuid;
		}
		RemoveFromContainerLists(Other.Guid);
		if (TArray<FACEContainerItemRef>* Contents = ContainerContents.Find(Other.ContainerId))
		{
			FACEContainerItemRef Ref;
			Ref.ItemGuid = Other.Guid;
			Ref.ContainerType = 0;
			Contents->Add(Ref);
		}
		// Only destroy held weapon/shield/ammo actors — clothing is ObjDesc-only.
		if (bHadHeldMesh)
		{
			RemoveObjectFromWorldVisual(Other.Guid);
		}
	}
	if (FACEWorldObject* Obj = WorldObjects.Find(ItemGuid))
	{
		Obj->CurrentWieldedLocation = Location;
		Obj->WielderId = PlayerGuid;
		Obj->ContainerId = 0;
		RemoveFromContainerLists(ItemGuid);

		// Held/selectable gear needs Physics Parent + ParentLocation for the 3D attach.
		// Clothing/armor stay ParentLocation.None and update the wearer via ObjDescEvent.
		// Equipping an ammo stack does not attach a visible arrow. The server's
		// reload ParentEvent supplies its hand and placement once it is nocked.
		const bool bAmmoHasParent = (Location & ACEEquipMask::MissileAmmo) != 0
			&& Obj->ParentGuid == PlayerGuid && Obj->ParentLocation != 0;
		if ((Location & HeldVisual) != 0 || bAmmoHasParent)
		{
			Obj->ParentGuid = PlayerGuid;
			const bool bInferParent = Obj->ParentLocation == 0;
			if (bInferParent)
			{
				// Match ACE Creature_Equipment.GetPlacementLocation until ParentEvent.
				if ((Location & (ACEEquipMask::MeleeWeapon | ACEEquipMask::Held | ACEEquipMask::TwoHanded)) != 0)
				{
					Obj->ParentLocation = 1; // RightHand
				}
				else if ((Location & ACEEquipMask::Shield) != 0)
				{
					if ((Obj->ItemType & ACEItemType::Armor) != 0)
					{
						Obj->ParentLocation = 3; // Shield
					}
					else
					{
						Obj->ParentLocation = 8; // LeftWeapon (weapon in shield slot)
					}
				}
				else if ((Location & ACEEquipMask::MissileWeapon) != 0)
				{
					const int32 Style = ACECombatStance::InferCombatStyle(*Obj);
					const bool bBowLike = (Style & 0x00010) != 0 || (Style & 0x00020) != 0; // Bow | Crossbow
					Obj->ParentLocation = bBowLike ? 2 : 1; // LeftHand vs RightHand
				}
			}
			if (Obj->ParentLocation != 0)
			{
				const int32 HeldPlacement = ACEPlacementFromParentLocation(Obj->ParentLocation);
				if (HeldPlacement != 0 && bInferParent)
				{
					Obj->PlacementId = HeldPlacement;
				}
				OnObjectCreated.Broadcast(*Obj);
			}
		}
		else
		{
			// Worn gear: never invent a held attachment (that piles a torso mesh at the feet).
			Obj->ParentLocation = 0;
			if ((Location & ACEEquipMask::MissileAmmo) != 0) Obj->ParentGuid = 0;
		}
	}
	else
	{
		RemoveFromContainerLists(ItemGuid);
	}
}

void FACESession::HandleInventoryPutObjIn3D(FACEBinaryReader& Reader)
{
	// GameEventItemServerSaysMoveItem: itemGuid (item left our inventory for the world).
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 ItemGuid = static_cast<int32>(Reader.ReadUInt32());
	FACEPosition SpawnPos;
	bool bHavePos = false;
	int32 PrevContainer = 0;
	int32 PrevPlacement = INDEX_NONE;
	bool bPackSlots = false;
	if (FACEWorldObject* Obj = WorldObjects.Find(ItemGuid))
	{
		PrevContainer = Obj->ContainerId;
		PrevPlacement = Obj->PlacementPosition;
		bPackSlots = IsPackSlotItem(*Obj);
		Obj->ContainerId = 0;
		Obj->WielderId = 0;
		Obj->CurrentWieldedLocation = 0;
		Obj->ParentGuid = 0;
		Obj->ParentLocation = 0;
		Obj->PlacementPosition = INDEX_NONE;
		if (Obj->bHasPosition)
		{
			SpawnPos = Obj->Position;
			bHavePos = true;
		}
	}
	RemoveFromContainerLists(ItemGuid);
	if (PrevContainer != 0)
	{
		ShiftContainerPlacementsAfterRemove(PrevContainer, PrevPlacement, bPackSlots);
	}
	// UpdatePosition may have arrived before this ClearContainer — rebroadcast so the
	// world presenter spawns the freestanding mesh (critical for indoor drops).
	if (bHavePos)
	{
		OnPositionUpdate.Broadcast(ItemGuid, SpawnPos);
	}
}

void FACESession::RemoveObjectFromWorldVisual(int32 ObjectGuid)
{
	if (FACEWorldObject* Obj = WorldObjects.Find(ObjectGuid))
	{
		// Keep the last world Position for subsequent server corrections; clear the world flag.
		Obj->bHasPosition = false;
		Obj->ParentGuid = 0;
		Obj->ParentLocation = 0;
	}
	// Same signal WorldPresenter uses for ObjectDelete — destroys the actor only.
	// WorldObjects entry is intentionally kept for inventory / ContainId.
	OnObjectDeleted.Broadcast(ObjectGuid);
}

void FACESession::HandleInventoryServerSaveFailed(FACEBinaryReader& Reader)
{
	// GameEvent 0x00A0 — Mag-nus ACCWeenieObject::ServerSaysAttemptFailed.
	// Server rejected Put/Wield; keep the last acknowledged membership.
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 ItemGuid = static_cast<int32>(Reader.ReadUInt32());
	if (Reader.CanRead(4))
	{
		Reader.ReadUInt32(); // WeenieError (TransientString already notifies the player)
	}
	Log(FString::Printf(TEXT("InventoryServerSaveFailed guid=0x%08X — retained server inventory"), ItemGuid));
}

void FACESession::HandlePickupEvent(FACEBinaryReader& Reader)
{
	// Physics PickupEvent (0xF74A) removes a held/world visual. A missile
	// launch sends it for the equipped STACK before SetStackSize + reload.
	if (!Reader.CanRead(8)) return;
	const int32 ItemGuid = static_cast<int32>(Reader.ReadUInt32());
	Reader.ReadUInt16(); // instance seq
	Reader.ReadUInt16(); // position seq
	if (FACEWorldObject* Obj = WorldObjects.Find(ItemGuid))
	{
		const bool bWasEquipped = Obj->CurrentWieldedLocation != 0
			&& (Obj->WielderId == PlayerGuid || Obj->ParentGuid == PlayerGuid);
		const bool bHeldAmmo = bWasEquipped && (Obj->CurrentWieldedLocation & (ACEEquipMask::MissileAmmo | ACEEquipMask::MissileWeapon)) != 0;
		if (!bHeldAmmo)
		{
			Obj->WielderId = 0;
			Obj->CurrentWieldedLocation = 0;
			// ContainId follows real dequips. Preserve their existing pack fallback.
			if (bWasEquipped && Obj->ContainerId == 0 && PlayerGuid != 0)
			{
				Obj->ContainerId = PlayerGuid;
				RemoveFromContainerLists(ItemGuid);
				if (TArray<FACEContainerItemRef>* Contents = ContainerContents.Find(PlayerGuid))
				{
					FACEContainerItemRef Ref; Ref.ItemGuid = ItemGuid; Contents->Add(Ref);
				}
			}
		}
	}
	// Ammo remains equipped until authoritative ContainId/wield/remove updates.
	RemoveObjectFromWorldVisual(ItemGuid);
}
void FACESession::HandleParentEvent(FACEBinaryReader& Reader)
{
	// Physics ParentEvent (0xF749): parentGuid, childGuid, parentLocation, placement, seqs.
	if (!Reader.CanRead(20))
	{
		return;
	}
	const int32 ParentGuid = static_cast<int32>(Reader.ReadUInt32());
	const int32 ChildGuid = static_cast<int32>(Reader.ReadUInt32());
	const int32 ParentLocation = static_cast<int32>(Reader.ReadInt32());
	const int32 Placement = static_cast<int32>(Reader.ReadInt32());
	const uint16 IncomingInstance = Reader.ReadUInt16();
	const uint16 IncomingPosOrParent = Reader.ReadUInt16();
	if (FACEWorldObject* Obj = WorldObjects.Find(ChildGuid))
	{
		if (Obj->bHasPhysicsTimestamps)
		{
			if (!ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Position], IncomingPosOrParent)
				&& !ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance], IncomingInstance))
			{
				return;
			}
			if (ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Position], IncomingPosOrParent))
			{
				Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Position] = IncomingPosOrParent;
			}
			if (ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance], IncomingInstance))
			{
				Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance] = IncomingInstance;
			}
		}
		if (ParentGuid == 0 || ParentLocation == 0)
		{
			// Detach held mesh — do not clear ContainerId (ContainId / PickupEvent own that).
			Obj->ParentGuid = 0;
			Obj->ParentLocation = 0;
			RemoveObjectFromWorldVisual(ChildGuid);
			return;
		}
		Obj->ParentGuid = ParentGuid;
		Obj->ParentLocation = ParentLocation;
		if (Placement != 0)
		{
			Obj->PlacementId = Placement;
		}
		else if (ParentLocation != 0)
		{
			const int32 HeldPlacement = ACEPlacementFromParentLocation(ParentLocation);
			if (HeldPlacement != 0)
			{
				Obj->PlacementId = HeldPlacement;
			}
		}
		Obj->ContainerId = 0;
		Obj->bHasPosition = false; // world origin comes from parent attachment
		if (ParentGuid == PlayerGuid)
		{
			Obj->WielderId = PlayerGuid;
		}
		RemoveFromContainerLists(ChildGuid);
		OnObjectCreated.Broadcast(*Obj);
	}
}

void FACESession::HandleInventoryRemoveObject(FACEBinaryReader& Reader)
{
	// 0x0024: itemGuid removed from our inventory (given away / consumed / salvaged).
	if (!Reader.CanRead(4))
	{
		return;
	}
	const int32 ItemGuid = static_cast<int32>(Reader.ReadUInt32());
	int32 PrevContainer = 0;
	int32 PrevPlacement = INDEX_NONE;
	bool bPackSlots = false;
	if (FACEWorldObject* Obj = WorldObjects.Find(ItemGuid))
	{
		PrevContainer = Obj->ContainerId;
		PrevPlacement = Obj->PlacementPosition;
		bPackSlots = IsPackSlotItem(*Obj);
		Obj->ContainerId = 0;
	}
	RemoveFromContainerLists(ItemGuid);
	if (PrevContainer != 0)
	{
		ShiftContainerPlacementsAfterRemove(PrevContainer, PrevPlacement, bPackSlots);
	}
	WorldObjects.Remove(ItemGuid);
	OnObjectDeleted.Broadcast(ItemGuid);
}

void FACESession::HandleSetStackSize(FACEBinaryReader& Reader)
{
	// GameMessageSetStackSize 0x0197: u8 seq, u32 guid, u32 stackSize, u32 value
	if (!Reader.CanRead(13))
	{
		return;
	}
	Reader.ReadUInt8();
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const int32 Stack = static_cast<int32>(Reader.ReadUInt32());
	const int32 Value = static_cast<int32>(Reader.ReadUInt32());
	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		Obj->StackSize = Stack;
		if (Obj->MaxStackSize > 0)
		{
			Obj->StackSize = FMath::Clamp(Obj->StackSize, 0, Obj->MaxStackSize);
		}
		Obj->Value = Value;
		if (SelectedObject.bValid && SelectedObject.Guid == Guid)
		{
			SelectedObject.Name = Obj->Name;
			OnSelectionChanged.Broadcast(SelectedObject);
		}
	}
}

void FACESession::ApplyPlayerKillerStatus(int32 Guid, int32 Status)
{
	// Retail updates the public descriptor from quality 134, without waiting for
	// another ObjectCreate or Identify response. Never predict a /pkl success.
	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
		Obj->ObjectDescriptionFlags = ACEPlayerKillerStatus::UpdateDescriptionFlags(Obj->ObjectDescriptionFlags, Status);
	if (Guid == PlayerGuid)
	{
		PlayerVitals.PlayerKillerStatus = Status;
		PlayerVitals.StatQualityInts.Add(134, Status);
		NotifyVitalsChanged();
	}
	OnPkStatusUpdated.Broadcast(Guid, Status);
	if (SelectedObject.bValid && SelectedObject.Guid == Guid)
		OnSelectionChanged.Broadcast(SelectedObject);
}

void FACESession::HandlePublicUpdatePropertyInt(FACEBinaryReader& Reader)
{
	// 0x02CE: u8 sequence, u32 objectGuid, u32 propertyId, i32 value.
	if (!Reader.CanRead(13))
	{
		return;
	}
	Reader.ReadUInt8();
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const uint32 Prop = Reader.ReadUInt32();
	const int32 Value = Reader.ReadInt32();
	if (Prop == 134)
	{
		ApplyPlayerKillerStatus(Guid, Value);
		return;
	}
	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		if (Prop == 4) // PropertyInt.ClothingPriority
		{
			Obj->ClothingPriority = Value;
		}
		else if (Prop == 10) // PropertyInt.CurrentWieldedLocation
		{
			Obj->CurrentWieldedLocation = static_cast<int64>(Value);
		}
		else if (Prop == 33) // PropertyInt.Bonded
		{
			Obj->Bonded = Value;
		}
		else if (Prop == 114) // PropertyInt.Attuned
		{
			Obj->Attuned = Value;
		}
		else if (Prop == 53) // PropertyInt.PlacementPosition
		{
			Obj->PlacementPosition = Value;
		}
		else if (Prop == 18) // PropertyInt.UiEffects
		{
			Obj->UiEffects = Value;
		}
		else if (Prop == 11) // PropertyInt.MaxStackSize
		{
			Obj->MaxStackSize = Value;
		}
		else if (Prop == 12) // PropertyInt.StackSize
		{
			Obj->StackSize = Value;
			if (Obj->MaxStackSize > 0)
			{
				Obj->StackSize = FMath::Clamp(Obj->StackSize, 0, Obj->MaxStackSize);
			}
		}
		else if (Prop == 91) // PropertyInt.MaxStructure
		{
			Obj->MaxStructure = Value;
		}
		else if (Prop == 92) // PropertyInt.Structure
		{
			Obj->Structure = Value;
		}
		else if (Prop == 46) // PropertyInt.DefaultCombatStyle
		{
			Obj->DefaultCombatStyle = Value;
		}
		else if (Prop == 51) // PropertyInt.CombatUse
		{
			Obj->CombatUse = Value;
		}
		else if (Prop == 5) // PropertyInt.EncumbranceVal
		{
			Obj->Burden = Value;
			if (Guid == PlayerGuid)
			{
				bHasPlayerEncumbrance = true;
				PlayerEncumbranceVal = Value;
			}
		}
	}
}

void FACESession::HandlePublicUpdatePropertyDataID(FACEBinaryReader& Reader)
{
	// 0x02D8: u8 sequence, u32 objectGuid, u32 propertyId, u32 did.
	if (!Reader.CanRead(13))
	{
		return;
	}
	Reader.ReadUInt8();
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const uint32 Prop = Reader.ReadUInt32();
	const uint32 Value = Reader.ReadUInt32();
	ApplyPropertyDataID(Guid, Prop, Value);
}

void FACESession::HandlePrivateUpdatePropertyDataID(FACEBinaryReader& Reader)
{
	// 0x02D7: u8 sequence, u32 propertyId, u32 did (player-only).
	if (!Reader.CanRead(9))
	{
		return;
	}
	Reader.ReadUInt8();
	const uint32 Prop = Reader.ReadUInt32();
	const uint32 Value = Reader.ReadUInt32();
	if (PlayerGuid != 0)
	{
		ApplyPropertyDataID(PlayerGuid, Prop, Value);
	}
}

void FACESession::HandlePublicUpdateInstanceId(FACEBinaryReader& Reader)
{
	// 0x02DA: u8 sequence, u32 objectGuid, u32 propertyId, u32 instanceGuid.
	if (!Reader.CanRead(13))
	{
		return;
	}
	Reader.ReadUInt8();
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const uint32 Prop = Reader.ReadUInt32();
	const int32 Value = static_cast<int32>(Reader.ReadUInt32());
	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		if (Prop == 2) // PropertyInstanceId.Container
		{
			// ContainId is the ordered membership notification. Preserve the old
			// list until it arrives; changing ContainerId here loses the source pack
			// and shifts the destination a second time during reconciliation.
			if (Value != 0) return;
			Obj->ContainerId = Value;
		}
		else if (Prop == 3) // PropertyInstanceId.Wielder
		{
			Obj->WielderId = Value;
			if (Value != 0)
			{
				Obj->ContainerId = 0;
			}
		}
		else if (Prop == 26) // PropertyInstanceId.Monarch
		{
			Obj->MonarchGuid = Value;
		}
	}
}

void FACESession::GetEquippedItems(TArray<FACEWorldObject>& Out) const
{
	Out.Reset();
	for (const TPair<int32, FACEWorldObject>& Pair : WorldObjects)
	{
		const FACEWorldObject& Obj = Pair.Value;
		if (Obj.CurrentWieldedLocation == 0)
		{
			continue;
		}
		if (Obj.WielderId == PlayerGuid || Obj.ParentGuid == PlayerGuid)
		{
			Out.Add(Obj);
		}
	}
}

const FACEWorldObject* FACESession::FindEquippedItem(int64 LocationMask, int32 ItemTypeMask, int32 AmmoTypeMask) const
{
	if (PlayerGuid == 0 || LocationMask == 0) return nullptr;
	for (const auto& Pair : WorldObjects)
	{
		const auto& Item = Pair.Value;
		if ((Item.CurrentWieldedLocation & LocationMask) != 0
			&& (Item.WielderId == PlayerGuid || Item.ParentGuid == PlayerGuid)
			&& (ItemTypeMask == 0 || (Item.ItemType & ItemTypeMask) != 0)
			&& (AmmoTypeMask == 0 || Item.AmmoType == 0 || (Item.AmmoType & AmmoTypeMask) != 0))
			return &Item;
	}
	return nullptr;
}

void FACESession::GetPackItems(int32 ContainerGuid, TArray<FACEWorldObject>& Out) const
{
	Out.Reset();
	TSet<int32> Seen;

	auto IsPackObject = [](const FACEWorldObject& Obj) -> bool
	{
		return Obj.ItemsCapacity > 0 || Obj.ContainersCapacity > 0
			|| (Obj.ItemType & ACEItemType::Container) != 0
			|| (Obj.ObjectDescriptionFlags & ACEObjectDescFlag::RequiresPackSlot) != 0;
	};

	// Player main pack: PlayerDescription footer order is retail grid order (not TMap scan).
	if (ContainerGuid == PlayerGuid)
	{
		if (const TArray<FACEContainerItemRef>* Contents = ContainerContents.Find(PlayerGuid))
		{
			for (const FACEContainerItemRef& Ref : *Contents)
			{
				if (Ref.ContainerType != 0)
				{
					continue;
				}
				FACEWorldObject Obj;
				if (!GetWorldObject(Ref.ItemGuid, Obj))
				{
					continue;
				}
				if (IsPackObject(Obj))
				{
					Seen.Add(Ref.ItemGuid);
					continue;
				}
				Out.Add(Obj);
				Seen.Add(Ref.ItemGuid);
			}
			Out.Sort([](const FACEWorldObject& A, const FACEWorldObject& B)
			{
				const int32 Pa = A.PlacementPosition >= 0 ? A.PlacementPosition : MAX_int32;
				const int32 Pb = B.PlacementPosition >= 0 ? B.PlacementPosition : MAX_int32;
				if (Pa != Pb)
				{
					return Pa < Pb;
				}
				return A.Guid < B.Guid;
			});
		}
		TArray<FACEWorldObject> Late;
		for (const TPair<int32, FACEWorldObject>& Pair : WorldObjects)
		{
			const FACEWorldObject& Obj = Pair.Value;
			if (Seen.Contains(Obj.Guid))
			{
				continue;
			}
			if (Obj.ContainerId == PlayerGuid && Obj.CurrentWieldedLocation == 0 && Obj.WielderId == 0
				&& Obj.ParentGuid == 0 && !IsPackObject(Obj))
			{
				Late.Add(Obj);
			}
		}
		if (Late.Num() > 0)
		{
			for (const FACEWorldObject& Obj : Late)
			{
				Out.Add(Obj);
			}
		}
		Out.Sort([](const FACEWorldObject& A, const FACEWorldObject& B)
		{
			const int32 Pa = A.PlacementPosition >= 0 ? A.PlacementPosition : MAX_int32;
			const int32 Pb = B.PlacementPosition >= 0 ? B.PlacementPosition : MAX_int32;
			if (Pa != Pb)
			{
				return Pa < Pb;
			}
			return A.Guid < B.Guid;
		});
		return;
	}

	// ViewContents is authoritative for corpses / side packs.
	if (const TArray<FACEContainerItemRef>* Contents = ContainerContents.Find(ContainerGuid))
	{
		for (const FACEContainerItemRef& Ref : *Contents)
		{
			FACEWorldObject Obj;
			if (GetWorldObject(Ref.ItemGuid, Obj))
			{
				const bool bIsPack = Obj.ItemsCapacity > 0 || Obj.ContainersCapacity > 0
					|| (Obj.ItemType & ACEItemType::Container) != 0
					|| (Obj.ObjectDescriptionFlags & ACEObjectDescFlag::RequiresPackSlot) != 0
					|| Ref.ContainerType != 0;
				if (bIsPack)
				{
					Seen.Add(Ref.ItemGuid);
					continue;
				}
				Out.Add(Obj);
			}
			else if (Ref.ContainerType == 0)
			{
				FACEWorldObject Stub;
				Stub.Guid = Ref.ItemGuid;
				Stub.ContainerId = ContainerGuid;
				Stub.Name = FString::Printf(TEXT("Item 0x%08X"), Ref.ItemGuid);
				Out.Add(Stub);
			}
			Seen.Add(Ref.ItemGuid);
		}
		// ViewContents encounter order can drift from PlacementPosition after local
		// Put/ContainId shifts — grid cells key off PP (BuildSparsePackSlotGuids).
		Out.Sort([](const FACEWorldObject& A, const FACEWorldObject& B)
		{
			const int32 Pa = A.PlacementPosition >= 0 ? A.PlacementPosition : MAX_int32;
			const int32 Pb = B.PlacementPosition >= 0 ? B.PlacementPosition : MAX_int32;
			if (Pa != Pb)
			{
				return Pa < Pb;
			}
			return A.Guid < B.Guid;
		});
		return;
	}

	// ObjectCreate ContainerId scan (main pack, or pack before ViewContents).
	for (const TPair<int32, FACEWorldObject>& Pair : WorldObjects)
	{
		const FACEWorldObject& Obj = Pair.Value;
		if (Seen.Contains(Obj.Guid))
		{
			continue;
		}
		if (Obj.ContainerId == ContainerGuid && Obj.CurrentWieldedLocation == 0 && Obj.WielderId == 0
			&& Obj.ParentGuid == 0)
		{
			const bool bIsPack = Obj.ItemsCapacity > 0 || Obj.ContainersCapacity > 0
				|| (Obj.ItemType & ACEItemType::Container) != 0
				|| (Obj.ObjectDescriptionFlags & ACEObjectDescFlag::RequiresPackSlot) != 0;
			if (bIsPack && ContainerGuid == PlayerGuid)
			{
				continue;
			}
			Out.Add(Obj);
		}
	}

	// Stable-sort ObjectCreate fallback (TMap iteration order is unstable).
	Out.Sort([](const FACEWorldObject& A, const FACEWorldObject& B)
	{
		const int32 Pa = A.PlacementPosition >= 0 ? A.PlacementPosition : MAX_int32;
		const int32 Pb = B.PlacementPosition >= 0 ? B.PlacementPosition : MAX_int32;
		if (Pa != Pb)
		{
			return Pa < Pb;
		}
		return A.Guid < B.Guid;
	});
}

void FACESession::GetPlayerPacks(TArray<FACEWorldObject>& Out) const
{
	Out.Reset();
	TSet<int32> Seen;
	// Retail side slots = UseBackpackSlot items (Container + Foci) ordered by PlacementPosition.
	// GameEventViewContents tags those with ContainerType 1/2; keep encounter order among them
	// (server OrderBy PlacementPosition) and do not re-sort by possibly-stale ObjectCreate PP.
	constexpr int32 ContainerType_Container = 1;
	constexpr int32 ContainerType_Foci = 2;
	if (const TArray<FACEContainerItemRef>* Contents = ContainerContents.Find(PlayerGuid))
	{
		for (const FACEContainerItemRef& Ref : *Contents)
		{
			if (Ref.ContainerType != ContainerType_Container && Ref.ContainerType != ContainerType_Foci)
			{
				continue;
			}
			FACEWorldObject Obj;
			if (!GetWorldObject(Ref.ItemGuid, Obj))
			{
				continue;
			}
			if (Obj.CurrentWieldedLocation != 0)
			{
				continue;
			}
			Out.Add(Obj);
			Seen.Add(Obj.Guid);
		}
	}
	for (const TPair<int32, FACEWorldObject>& Pair : WorldObjects)
	{
		const FACEWorldObject& Obj = Pair.Value;
		if (Seen.Contains(Obj.Guid))
		{
			continue;
		}
		const bool bOnPlayer = Obj.ContainerId == PlayerGuid || Obj.ParentGuid == PlayerGuid;
		const bool bIsPack = Obj.ItemsCapacity > 0 || Obj.ContainersCapacity > 0
			|| (Obj.ItemType & ACEItemType::Container) != 0
			|| (Obj.ObjectDescriptionFlags & ACEObjectDescFlag::RequiresPackSlot) != 0;
		if (bOnPlayer && bIsPack && Obj.CurrentWieldedLocation == 0 && Obj.Guid != PlayerGuid)
		{
			Out.Add(Obj);
		}
	}
	Out.Sort([](const FACEWorldObject& A, const FACEWorldObject& B)
	{
		const int32 Pa = A.PlacementPosition >= 0 ? A.PlacementPosition : MAX_int32;
		const int32 Pb = B.PlacementPosition >= 0 ? B.PlacementPosition : MAX_int32;
		if (Pa != Pb)
		{
			return Pa < Pb;
		}
		return A.Guid < B.Guid;
	});
}

void FACESession::ApplyServerTime(double ServerTicks, double ReceivedAt, const TCHAR* Source)
{
	// Zero is a valid ACE epoch (Morningthaw 1, PY 10), not an unknown clock.
	if (!FMath::IsFinite(ServerTicks) || ServerTicks < 0.0) return;
	const double Now = FPlatformTime::Seconds();
	const double SampleNow = ServerTicks + FMath::Max(0.0, Now - ReceivedAt);
	const double Correction = bHasServerTime ? SampleNow - GetPortalYearTicks() : 0.0;
	if (!bHasServerTime || FMath::Abs(Correction) >= 1.0)
	{
		UE_LOG(LogTemp, Log, TEXT("ACEClock: %s ticks=%.3f correction=%+.3fs queued=%.3fs"),
			Source, ServerTicks, Correction, Now - ReceivedAt);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("ACEClock: %s ticks=%.3f correction=%+.3fs"), Source, ServerTicks, Correction);
	}
	bHasServerTime = true;
	PortalYearTicksAtConnect = ServerTicks;
	// Preserve arrival time when authenticated packets wait for a missing sequence.
	PortalYearTicksRealtime = ReceivedAt;
}

double FACESession::GetPortalYearTicks() const
{
	if (!bHasServerTime)
	{
		return 0.0;
	}
	return PortalYearTicksAtConnect + (FPlatformTime::Seconds() - PortalYearTicksRealtime);
}

bool FACESession::GetWorldObject(int32 Guid, FACEWorldObject& Out) const
{
	if (const FACEWorldObject* Found = WorldObjects.Find(Guid))
	{
		Out = *Found;
		return true;
	}
	return false;
}

void FACESession::HandleUpdatePosition(FACEBinaryReader& Reader)
{
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const uint32 PosFlags = Reader.ReadUInt32();
	FACEPosition Pos;
	Pos.CellId = static_cast<int32>(Reader.ReadUInt32());
	Pos.Location.X = Reader.ReadFloat();
	Pos.Location.Y = Reader.ReadFloat();
	Pos.Location.Z = Reader.ReadFloat();

	constexpr uint32 OrientationHasNoW = 0x08;
	constexpr uint32 OrientationHasNoX = 0x10;
	constexpr uint32 OrientationHasNoY = 0x20;
	constexpr uint32 OrientationHasNoZ = 0x40;
	constexpr uint32 HasVelocity = 0x01;
	constexpr uint32 HasPlacementID = 0x02;
	constexpr uint32 IsGrounded = 0x04;

	Pos.RotationW = (PosFlags & OrientationHasNoW) ? 0.f : Reader.ReadFloat();
	Pos.RotationXYZ.X = (PosFlags & OrientationHasNoX) ? 0.f : Reader.ReadFloat();
	Pos.RotationXYZ.Y = (PosFlags & OrientationHasNoY) ? 0.f : Reader.ReadFloat();
	Pos.RotationXYZ.Z = (PosFlags & OrientationHasNoZ) ? 0.f : Reader.ReadFloat();
	Pos.ReconstructOmittedRotation(PosFlags);
	Pos.bIsGrounded = (PosFlags & IsGrounded) != 0;

	if (PosFlags & HasVelocity)
	{
		Pos.Velocity.X = Reader.ReadFloat();
		Pos.Velocity.Y = Reader.ReadFloat();
		Pos.Velocity.Z = Reader.ReadFloat();
		Pos.bHasVelocity = true;
	}
	if (PosFlags & HasPlacementID)
	{
		Reader.ReadUInt32();
	}
	if (Reader.CanRead(8))
	{
		const uint16 IncomingInstance = Reader.ReadUInt16();
		const uint16 IncomingPosition = Reader.ReadUInt16();
		const uint16 IncomingTeleport = Reader.ReadUInt16();
		const uint16 IncomingForce = Reader.ReadUInt16();

		if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
		{
			if (Obj->bHasPhysicsTimestamps)
			{
				// SmartBox::UnpackPositionEvent requires the matching incarnation.
				// A newer position number from a previous visit cannot move this object.
				if (IncomingInstance != Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]
					|| ACEPhysicsTimeStamp::IsNewer(IncomingTeleport, Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Teleport]))
					return;
				const bool bNewPosition = ACEPhysicsTimeStamp::IsNewer(
					Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Position], IncomingPosition);
				const bool bForceSelf = Guid == PlayerGuid && ACEPhysicsTimeStamp::IsNewer(
					Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::ForcePosition], IncomingForce);
				if (!bNewPosition && !bForceSelf) return;
			}
			Obj->bHasPhysicsTimestamps = true;
			Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance] = IncomingInstance;
			Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Position] = IncomingPosition;
			Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Teleport] = IncomingTeleport;
			Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::ForcePosition] = IncomingForce;
			Obj->Position = Pos;
			Obj->bHasPosition = true;
		}

		if (Guid == PlayerGuid)
		{
			InstanceSeq = IncomingInstance;
			if (TeleportSeq != IncomingTeleport) SelectObject(0);
			TeleportSeq = IncomingTeleport;
			ForcePositionSeq = IncomingForce;
		}
	}
	else if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		Obj->Position = Pos;
		Obj->bHasPosition = true;
	}

	if (Guid == PlayerGuid)
	{
		PlayerPosition = Pos;
		if (const auto* Selected = WorldObjects.Find(SelectedObject.Guid);
			Selected && Selected->bHasPosition && !IsNearbyHealthObject(SelectedObject.Guid)) SelectObject(0);
		MaybeEnterWorldComplete();
	}

	OnPositionUpdate.Broadcast(Guid, Pos);
}

void FACESession::HandleUpdateMotion(FACEBinaryReader& Reader)
{
	// GameMessageUpdateMotion: guid, instance sequence, MovementData header/body.
	if (!Reader.CanRead(16))
	{
		return;
	}

	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const uint16 IncomingInstance = Reader.ReadUInt16();
	const uint16 IncomingMovement = Reader.ReadUInt16();
	const uint16 IncomingServerControl = Reader.ReadUInt16();
	Reader.ReadUInt8();  // autonomous
	Reader.Align();

	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		if (Obj->bHasPhysicsTimestamps)
		{
			if (IncomingInstance != Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]) return;
			if (!ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Movement], IncomingMovement)
				&& !ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::ServerControl], IncomingServerControl))
			{
				return;
			}
			if (ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Movement], IncomingMovement))
			{
				Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Movement] = IncomingMovement;
			}
			if (ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::ServerControl], IncomingServerControl))
			{
				Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::ServerControl] = IncomingServerControl;
			}
		}
	}

	if (!Reader.CanRead(4))
	{
		return;
	}

	const uint8 MovementType = Reader.ReadUInt8();
	const uint8 MotionFlags = Reader.ReadUInt8();
	const uint16 HeaderStyleRaw = Reader.ReadUInt16(); // MovementData.CurrentStyle (MotionStance)

	FACEObjectMotionState Motion;
	Motion.MovementType = MovementType;
	if (HeaderStyleRaw != 0)
	{
		Motion.CurrentStyle = static_cast<int32>(ACEMotion::ExpandPackedCommand(HeaderStyleRaw));
	}

	if (MovementType == 0) // MovementType.Invalid/general interpreted state
	{
		if (!Reader.CanRead(4))
		{
			return;
		}

		const uint32 Packed = Reader.ReadUInt32();
		const uint32 Flags = Packed & 0x7Fu;
		const uint32 CommandCount = Packed >> 7;
		uint16 ForwardCommand = 0x0003; // Ready
		uint16 SideStepCommand = 0;
		float ForwardSpeed = 1.f;
		float SideStepSpeed = 1.f;
		float TurnSpeed = 1.f;

		auto ReadU16If = [&](uint32 Flag, uint16& Out) -> bool
		{
			if ((Flags & Flag) == 0)
			{
				return true;
			}
			if (!Reader.CanRead(2))
			{
				return false;
			}
			Out = Reader.ReadUInt16();
			return true;
		};
		auto ReadFloatIf = [&](uint32 Flag, float& Out) -> bool
		{
			if ((Flags & Flag) == 0)
			{
				return true;
			}
			if (!Reader.CanRead(4))
			{
				return false;
			}
			Out = Reader.ReadFloat();
			return true;
		};

		uint16 StyleRaw = 0;
		uint16 TurnCommand = 0;
		if (!ReadU16If(0x01u, StyleRaw)
			|| !ReadU16If(0x02u, ForwardCommand)
			|| !ReadU16If(0x08u, SideStepCommand)
			|| !ReadU16If(0x20u, TurnCommand)
			|| !ReadFloatIf(0x04u, ForwardSpeed)
			|| !ReadFloatIf(0x10u, SideStepSpeed)
			|| !ReadFloatIf(0x40u, TurnSpeed))
		{
			return;
		}

		if (StyleRaw != 0)
		{
			Motion.CurrentStyle = static_cast<int32>(ACEMotion::ExpandPackedCommand(StyleRaw));
		}

		// MotionItem = command u16 + sequence u16 + speed f32. Melee attacks / FastTick windups live here.
		const int64 CommandBytes = static_cast<int64>(CommandCount) * 8;
		if (CommandBytes > MAX_int32 || !Reader.CanRead(static_cast<int32>(CommandBytes)))
		{
			return;
		}
		auto IsLocoOrDoorOrReady = [](uint16 Low) -> bool
		{
			return Low == 0x0003 || Low == 0x0005 || Low == 0x0006 || Low == 0x0007
				|| Low == 0x000D || Low == 0x000E || Low == 0x000F || Low == 0x0010
				|| Low == 0x000B || Low == 0x000C;
		};
		Motion.ActionFollowups.Reset();
		for (uint32 i = 0; i < CommandCount; ++i)
		{
			const uint16 RawCmd = Reader.ReadUInt16();
			Reader.ReadUInt16(); // sequence + autonomous bit
			const float Speed = Reader.ReadFloat();
			const uint32 Full = ACEMotion::ExpandPackedCommand(RawCmd);
			// FastTick magic packs every scarab windup into one CommandList — keep order
			// (first → ActionCommand, rest → ActionFollowups). Do not keep only the last.
			const uint16 Low = static_cast<uint16>(Full);
			const bool bStance = (Full & 0xFF000000u) == 0x80000000u;
			if (!IsLocoOrDoorOrReady(Low) && !bStance)
			{
				const uint32 Fam = Full & 0xFF000000u;
				const bool bChatPose = Fam == 0x13000000u || Fam == 0x42000000u || Fam == 0x43000000u;
				const float ClampedSpeed = bChatPose ? 1.f : FMath::Max(0.05f, Speed);
				if (Motion.ActionCommand == 0)
				{
					Motion.ActionCommand = static_cast<int32>(Full);
					Motion.ActionSpeed = ClampedSpeed;
				}
				else
				{
					Motion.ActionFollowups.Add(static_cast<int32>(Full));
				}
			}
		}
		Reader.Align();
		if ((MotionFlags & 0x01u) != 0 && Reader.CanRead(4))
		{
			Reader.ReadUInt32(); // sticky object
		}

		Motion.ForwardCommand = static_cast<int32>(ForwardCommand);
		switch (ForwardCommand)
		{
		case 0x0005: // WalkForward; observers use negative speed for backpedal
			Motion.Forward = FMath::Clamp(ForwardSpeed, -1.f, 1.f);
			Motion.ForwardUnitsPerSecond = 3.1199999f * ForwardSpeed;
			Motion.AnimPlayRate = FMath::Max(0.05f, FMath::Abs(ForwardSpeed));
			// Interpreted run is usually ForwardCommand=RunForward. When the server still
			// sends WalkForward, speed is GetRunRate() (>1). Also treat >=1 with high
			// units/sec as run so remotes don't stay on the walk cycle.
			Motion.bRunning = FMath::Abs(ForwardSpeed) > 1.05f;
			if (Motion.bRunning)
			{
				Motion.ForwardUnitsPerSecond = 4.f * ForwardSpeed;
				Motion.AnimPlayRate = FMath::Max(0.05f, FMath::Abs(ForwardSpeed));
				Motion.Forward = ForwardSpeed >= 0.f ? 1.f : -1.f;
			}
			break;
		case 0x0006: // WalkBackwards
			Motion.Forward = -FMath::Clamp(FMath::Abs(ForwardSpeed), 0.f, 1.f);
			Motion.ForwardUnitsPerSecond = -3.1199999f * FMath::Abs(ForwardSpeed) * 0.65f;
			Motion.AnimPlayRate = FMath::Max(0.05f, FMath::Abs(ForwardSpeed) * 0.65f);
			break;
		case 0x0007: // RunForward — ForwardSpeed is GetRunRate() (often > 1)
			Motion.Forward = ForwardSpeed >= 0.f ? 1.f : -1.f;
			Motion.ForwardUnitsPerSecond = 4.f * ForwardSpeed;
			Motion.AnimPlayRate = FMath::Max(0.05f, FMath::Abs(ForwardSpeed));
			Motion.bRunning = true;
			break;
		case ACEMotion::OnCommandU16:
		case ACEMotion::OffCommandU16:
			// Door / switch one-shot — not locomotion.
			break;
		default:
		{
			// Cast / MagicPowerUp / emotes are often ForwardCommand (EnqueueMotionMagic),
			// not MotionItems. Promote them to ActionCommand when Commands was empty.
			if (Motion.ActionCommand == 0 && ForwardCommand != 0)
			{
				const uint32 Full = ACEMotion::ExpandPackedCommand(ForwardCommand);
				const uint16 Low = static_cast<uint16>(Full);
				const bool bStance = (Full & 0xFF000000u) == 0x80000000u;
				if (!IsLocoOrDoorOrReady(Low) && !bStance)
				{
					Motion.ActionCommand = static_cast<int32>(Full);
					// ChatPose hold states use speed_mod 1; don't inherit a stale run rate.
					const uint32 Fam = Full & 0xFF000000u;
					if (Fam == 0x13000000u || Fam == 0x42000000u || Fam == 0x43000000u)
					{
						Motion.ActionSpeed = 1.f;
					}
					else
					{
						Motion.ActionSpeed = FMath::Max(0.05f, ForwardSpeed);
					}
				}
			}
			break;
		}
		}

		switch (SideStepCommand)
		{
		case 0x000f: // SideStepRight; negative speed means left
			Motion.Strafe = FMath::Clamp(SideStepSpeed, -1.f, 1.f);
			Motion.StrafeUnitsPerSecond = 1.25f * SideStepSpeed;
			if (FMath::IsNearlyZero(Motion.Forward))
			{
				Motion.AnimPlayRate = FMath::Max(0.05f, FMath::Abs(SideStepSpeed));
			}
			break;
		case 0x0010: // SideStepLeft
			Motion.Strafe = -FMath::Clamp(FMath::Abs(SideStepSpeed), 0.f, 1.f);
			Motion.StrafeUnitsPerSecond = -1.25f * FMath::Abs(SideStepSpeed);
			if (FMath::IsNearlyZero(Motion.Forward))
			{
				Motion.AnimPlayRate = FMath::Max(0.05f, FMath::Abs(SideStepSpeed));
			}
			break;
		default:
			break;
		}
		if (TurnCommand == 0x000d || TurnCommand == 0x000e)
		{
			Motion.Turn = TurnCommand == 0x000e
				? -FMath::Abs(TurnSpeed)
				: TurnSpeed;
		}
	}
	else if (MovementType == 6 || MovementType == 7) // MoveToObject / MoveToPosition
	{
		if (MovementType == 6)
		{
			if (!Reader.CanRead(4))
			{
				return;
			}
			Motion.MoveToTargetGuid = static_cast<int32>(Reader.ReadUInt32());
		}
		// Origin (cell + xyz), MoveToParameters (flags + six floats), RunRate.
		if (!Reader.CanRead(16 + 28 + 4))
		{
			return;
		}
		Motion.MoveToCellId = static_cast<int32>(Reader.ReadUInt32());
		Motion.MoveToLocalAce.X = Reader.ReadFloat();
		Motion.MoveToLocalAce.Y = Reader.ReadFloat();
		Motion.MoveToLocalAce.Z = Reader.ReadFloat();
		Motion.bHaveMoveToTarget = Motion.MoveToCellId != 0;
		Motion.MoveToFlags = Reader.ReadUInt32(); // MovementParameters flags
		Motion.MoveToDistance = Reader.ReadFloat(); // distance / use radius
		Reader.ReadFloat(); // minimum distance
		Motion.MoveToFailDistance = Reader.ReadFloat(); // fail distance (melee charge = 15; Use = MaxValue)
		// MaxValue means "no fail" on the server — treat as 0 for local checks.
		if (!FMath::IsFinite(Motion.MoveToFailDistance) || Motion.MoveToFailDistance == MAX_flt
			|| Motion.MoveToFailDistance < 0.f)
		{
			Motion.MoveToFailDistance = 0.f;
		}
		const float MoveSpeed = Reader.ReadFloat();
		Motion.MoveToSpeed = MoveSpeed;
		Motion.MoveToWalkRunThreshold = Reader.ReadFloat();
		Motion.MoveToDesiredHeading = Reader.ReadFloat();
		const float RunRate = Reader.ReadFloat();
		Motion.Forward = FMath::Clamp(FMath::Abs(MoveSpeed), 0.f, 1.f);
		Motion.MoveToRunRate = RunRate;
		FACEPosition MoveDestination; MoveDestination.CellId = Motion.MoveToCellId; MoveDestination.Location = Motion.MoveToLocalAce;
		const FACEWorldObject* Mover = WorldObjects.Find(Guid);
		const float Distance = Mover && Mover->bHasPosition
			? FVector::Dist2D(Mover->Position.ToUnrealLocation(1.f), MoveDestination.ToUnrealLocation(1.f)) : 0.f;
		Motion.UpdateMoveToGait(Distance);
		// Appearance keys off Forward amount + run flag — ensure a non-zero loco signal
		// whenever the server is actually moving us (Speed can be tiny but RunRate high).
		if (FMath::IsNearlyZero(Motion.Forward) && Motion.ForwardUnitsPerSecond > KINDA_SMALL_NUMBER)
		{
			Motion.Forward = 1.f;
		}
		Motion.ForwardCommand = Motion.bRunning
			? static_cast<int32>(0x0007) // RunForward packed
			: static_cast<int32>(0x0005); // WalkForward packed
	}
	else if (MovementType == 8) // TurnToObject
	{
		// TargetGuid, DesiredHeading, TurnToParameters (flags, speed, desiredHeading).
		if (!Reader.CanRead(4 + 4 + 4 + 4 + 4))
		{
			return;
		}
		Motion.MoveToTargetGuid = static_cast<int32>(Reader.ReadUInt32());
		Motion.MoveToDesiredHeading = Reader.ReadFloat();
		Reader.ReadUInt32(); // MovementParams flags
		Motion.TurnToSpeed = FMath::Max(0.05f, Reader.ReadFloat());
		const float ParamHeading = Reader.ReadFloat();
		if (!FMath::IsNearlyZero(ParamHeading))
		{
			Motion.MoveToDesiredHeading = ParamHeading;
		}
	}
	else if (MovementType == 9) // TurnToHeading
	{
		if (!Reader.CanRead(4 + 4 + 4))
		{
			return;
		}
		Reader.ReadUInt32(); // MovementParams flags
		Motion.TurnToSpeed = FMath::Max(0.05f, Reader.ReadFloat());
		Motion.MoveToDesiredHeading = Reader.ReadFloat();
	}

	Motion.bMoving = !FMath::IsNearlyZero(Motion.Forward)
		|| !FMath::IsNearlyZero(Motion.Strafe)
		|| !FMath::IsNearlyZero(Motion.Turn)
		|| Motion.MovementType == 6
		|| Motion.MovementType == 7;

	// Echo server stance back in our MoveToState so combat/magic mode is not wiped to NonCombat.
	// SwitchCombatStyles() briefly broadcasts NonCombat mid-transition — do NOT adopt that into
	// CurrentStance while CombatMode is still melee/missile/magic, or MoveToState undoes Magic.
	if (Guid == PlayerGuid && Motion.CurrentStyle != 0)
	{
		const uint32 Style = static_cast<uint32>(Motion.CurrentStyle);
		const bool bStyleNonCombat = Style == ACEMotion::StanceNonCombat;
		const bool bCombatModeActive = PlayerVitals.bValid
			&& PlayerVitals.CombatMode != static_cast<int32>(ACECombatMode::NonCombat)
			&& PlayerVitals.CombatMode != 0;
		if (!bStyleNonCombat || !bCombatModeActive)
		{
			SetCurrentStance(Style);
		}
	}

	// Persist door/chest resting pose for later ObjectCreate-equivalent recreates.
	if (Motion.ForwardCommand == ACEMotion::OnCommandU16
		|| Motion.ForwardCommand == ACEMotion::OffCommandU16)
	{
		if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
		{
			Obj->InitialMotionCommand = Motion.ForwardCommand;
		}
	}

	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		const bool bDeath = Motion.IsDeathMotion();
		if (Obj->bDying && !Obj->bIsPlayer && !bDeath) return;
		Obj->bDying = bDeath && !Obj->IsCorpse();
		if (bDeath)
		{
			Obj->InitialMotionCommand = ACEMotion::Dead;
			Obj->InitialMotionStyle = ACEMotion::StanceNonCombat;
			Obj->Velocity = FVector::ZeroVector;
			Obj->bHasVelocity = false;
			Motion.bMoving = false;
			Motion.Forward = Motion.Strafe = Motion.Turn = Motion.ForwardUnitsPerSecond = 0.f;
			if (!Obj->IsCorpse() && SelectedObject.Guid == Guid) SelectObject(0);
		}
		else if (ACEMotion::NormalizeCommand(Obj->InitialMotionCommand) == ACEMotion::Dead)
		{
			Obj->InitialMotionCommand = ACEMotion::Ready;
		}
	}
	OnMotionUpdate.Broadcast(Guid, Motion);
}

void FACESession::HandleVectorUpdate(FACEBinaryReader& Reader)
{
	// GameMessageVectorUpdate: guid, velocity xyz, omega xyz, instance seq, vector seq.
	if (!Reader.CanRead(4 + 12 + 12 + 4))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	FVector AceVelocity;
	AceVelocity.X = Reader.ReadFloat();
	AceVelocity.Y = Reader.ReadFloat();
	AceVelocity.Z = Reader.ReadFloat();
	FVector AceOmega;
	AceOmega.X = Reader.ReadFloat();
	AceOmega.Y = Reader.ReadFloat();
	AceOmega.Z = Reader.ReadFloat();
	Reader.ReadUInt16(); // instance sequence
	Reader.ReadUInt16(); // vector sequence

	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		Obj->Velocity = AceVelocity;
		Obj->bHasVelocity = !AceVelocity.IsNearlyZero();
		Obj->Omega = AceOmega;
	}
	OnVectorUpdate.Broadcast(Guid, AceVelocity, AceOmega);
}

void FACESession::HandleSetState(FACEBinaryReader& Reader)
{
	if (!Reader.CanRead(12))
	{
		return;
	}
	const int32 Guid = static_cast<int32>(Reader.ReadUInt32());
	const int32 PhysicsState = static_cast<int32>(Reader.ReadUInt32());
	const uint16 IncomingInstance = Reader.ReadUInt16();
	const uint16 IncomingState = Reader.ReadUInt16();
	if (FACEWorldObject* Obj = WorldObjects.Find(Guid))
	{
		if (Obj->bHasPhysicsTimestamps)
		{
			if (!ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::State], IncomingState)
				&& !ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance], IncomingInstance))
			{
				return;
			}
			if (ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::State], IncomingState))
			{
				Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::State] = IncomingState;
			}
			if (ACEPhysicsTimeStamp::IsNewer(Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance], IncomingInstance))
			{
				Obj->PhysicsTimestamps[ACEPhysicsTimeStamp::Instance] = IncomingInstance;
			}
		}
		Obj->PhysicsState = PhysicsState;
	}
	OnPhysicsStateUpdate.Broadcast(Guid, PhysicsState);
}

void FACESession::SendUseItem(int32 ObjectGuid)
{
	if (State != EACESessionState::InWorld || ObjectGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(ObjectGuid));
	SendGameAction(ACEGameAction::Use, W.GetData(), ACEQueue::WeenieQueue);
	bUseBusy = true;
	Log(FString::Printf(TEXT("Use item guid=0x%08X"), ObjectGuid));
}

void FACESession::SendChangeCombatMode(uint32 CombatMode)
{
	if (State != EACESessionState::InWorld)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(CombatMode);
	SendGameAction(ACEGameAction::ChangeCombatMode, W.GetData(), ACEQueue::WeenieQueue);
	// Optimistic CombatMode so SwitchCombatStyles' transient NonCombat UpdateMotion does not
	// overwrite CurrentStance / wipe Magic before PropertyInt.CombatMode arrives.
	PlayerVitals.CombatMode = static_cast<int32>(CombatMode);
	DeferredServerCombatMode = INDEX_NONE;
	if (CombatMode == ACECombatMode::NonCombat)
	{
		PendingCombatMode = 0;
		PendingCombatModeUntil = 0.0;
	}
	else
	{
		PendingCombatMode = CombatMode;
		PendingCombatModeUntil = FPlatformTime::Seconds() + 5.0;
	}
	TArray<FACEWorldObject> Equipped;
	GetEquippedItems(Equipped);
	const uint32 Stance = ACECombatStance::ResolveForCombatMode(Equipped, CombatMode);
	SetCurrentStance(Stance);
	Log(FString::Printf(TEXT("ChangeCombatMode=%u stance=0x%08X"), CombatMode, Stance));
	// Controller actions also change stance outside the retail binder. Notify
	// all UI consumers now so its mode icon and the copied wrist panel agree.
	OnVitalsUpdated.Broadcast(PlayerVitals);
}

void FACESession::SendTargetedMeleeAttack(int32 TargetGuid, uint32 AttackHeight, float PowerLevel)
{
	if (State != EACESessionState::InWorld || TargetGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(TargetGuid));
	W.WriteUInt32(AttackHeight);
	W.WriteFloat(FMath::Clamp(PowerLevel, 0.f, 1.f));
	SendGameAction(ACEGameAction::TargetedMeleeAttack, W.GetData(), ACEQueue::WeenieQueue);
	bServerAttackInProgress = true;
	LastAttackError = 0;
	++CombatEventRevision;
}

void FACESession::SendTargetedMissileAttack(int32 TargetGuid, uint32 AttackHeight, float AccuracyLevel)
{
	if (State != EACESessionState::InWorld || TargetGuid == 0)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(TargetGuid));
	W.WriteUInt32(AttackHeight);
	W.WriteFloat(FMath::Clamp(AccuracyLevel, 0.f, 1.f));
	SendGameAction(ACEGameAction::TargetedMissileAttack, W.GetData(), ACEQueue::WeenieQueue);
	bServerAttackInProgress = true;
	LastAttackError = 0;
	++CombatEventRevision;
}

bool FACESession::CreateCharacter(const FACECGSelection& Selection)
{
    if (State != EACESessionState::CharacterSelect || !SocketC2S || !ServerC2SAddr.IsValid() || !IssacClient || bCharacterCreationPending || PendingCharacterMutation != 0 || Characters.Num() >= CharacterSlotCount) return false;
    FACEBinaryWriter Payload; Payload.WriteString16L(AccountName); Selection.Write(Payload);
    bCharacterCreationPending = true;
    SendGameMessage(0xF656, Payload.GetData(), ACEQueue::UIQueue, true);
    return true;
}
void FACESession::HandleCharacterCreated(FACEBinaryReader& Reader)
{
    if (!bCharacterCreationPending || State != EACESessionState::CharacterSelect || !Reader.CanRead(4)) return;
    const uint32 Result = Reader.ReadUInt32();
    if (Result == 2) return; // Verification pending: retain the draft and prevent duplicate sends.
    FACECharacterInfo Info;
    if (Result == 1)
    {
        if (!Reader.CanRead(6)) return;
        Info.CharacterId = Reader.ReadUInt32();
        const int32 StringStart = Reader.Tell(); const uint16 Length = Reader.ReadUInt16();
        Reader.Seek(StringStart);
        if (!Reader.CanRead(((Length + 2 + 3) & ~3) + 4)) return;
        Info.Name = Reader.ReadString16L(); Reader.ReadUInt32();
        if (!Info.CharacterId || Info.Name.IsEmpty()) return;
        Characters.RemoveAll([&](const FACECharacterInfo& C) { return C.CharacterId == Info.CharacterId; });
        Characters.Add(Info);
    }
    bCharacterCreationPending = false;
    OnCharacterCreated.Broadcast(Result, Info);
}

bool FACESession::BuildCharacterMutation(int32 CharacterId, bool bRestore, TArray<uint8>& Payload) const
{
	Payload.Reset();
	if (State != EACESessionState::CharacterSelect || PendingCharacterMutation || bCharacterCreationPending) return false;
	const int32 Slot = Characters.IndexOfByPredicate([CharacterId](const FACECharacterInfo& C) { return C.CharacterId == CharacterId; });
	if (Slot == INDEX_NONE || (Characters[Slot].DeleteSeconds != 0) != bRestore || AccountName.IsEmpty()) return false;
	FACEBinaryWriter Writer;
	// CPlayerSystem::DeleteCharacter sends account + original server-list slot;
	// RestoreCharacter sends the GUID. Never sort the backing roster.
	if (bRestore) Writer.WriteUInt32(static_cast<uint32>(CharacterId));
	else { Writer.WriteString16L(AccountName); Writer.WriteUInt32(static_cast<uint32>(Slot)); }
	Payload = Writer.GetData();
	return true;
}

bool FACESession::DeleteCharacter(int32 CharacterId)
{
	TArray<uint8> Payload;
	if (!SocketC2S || !ServerC2SAddr.IsValid() || !IssacClient || !BuildCharacterMutation(CharacterId, false, Payload)) return false;
	PendingCharacterMutation = CharacterId; bRestoringCharacter = false;
	CharacterMutationSentAt = FPlatformTime::Seconds(); CharacterManagementError.Reset();
	SendGameMessage(0xF655, Payload, ACEQueue::UIQueue, true);
	return true;
}

bool FACESession::RestoreCharacter(int32 CharacterId)
{
	TArray<uint8> Payload;
	if (!SocketC2S || !ServerC2SAddr.IsValid() || !IssacClient || !BuildCharacterMutation(CharacterId, true, Payload)) return false;
	PendingCharacterMutation = CharacterId; bRestoringCharacter = true;
	CharacterMutationSentAt = FPlatformTime::Seconds(); CharacterManagementError.Reset();
	SendGameMessage(0xF7D9, Payload, ACEQueue::UIQueue, true);
	return true;
}

void FACESession::HandleCharacterRestored(FACEBinaryReader& Reader)
{
	if (State != EACESessionState::CharacterSelect || !PendingCharacterMutation || !bRestoringCharacter || !Reader.CanRead(4)) return;
	const uint32 Result = Reader.ReadUInt32();
	if (Result == 2) return; // Verification still in progress.
	if (Result != 1)
	{
		CharacterManagementError = Result == 3 ? TEXT("That character name is now in use. The character could not be restored.")
			: TEXT("The server could not restore this character.");
		PendingCharacterMutation = 0;
		return;
	}
	if (!Reader.CanRead(6)) return;
	FACECharacterInfo Info; Info.CharacterId = Reader.ReadUInt32();
	const int32 Start = Reader.Tell(); const uint16 Length = Reader.ReadUInt16(); Reader.Seek(Start);
	if (!Reader.CanRead(((Length + 2 + 3) & ~3) + 4)) return;
	Info.Name = Reader.ReadString16L(); Info.DeleteSeconds = Reader.ReadUInt32();
	if (Info.CharacterId != PendingCharacterMutation || Info.Name.IsEmpty()) return;
	if (auto* Existing = Characters.FindByPredicate([&](const FACECharacterInfo& C) { return C.CharacterId == Info.CharacterId; })) *Existing = Info;
	PendingCharacterMutation = 0; CharacterManagementError.Reset();
	OnCharacterList.Broadcast(Characters, ServerName);
}

bool FACESession::EnterWorld(int32 CharacterId)
{
	if (State != EACESessionState::CharacterSelect || PendingCharacterMutation) return false;
	const auto* Character=Characters.FindByPredicate([CharacterId](const FACECharacterInfo& C)
		{ return C.CharacterId==CharacterId; });
	if (!Character || Character->DeleteSeconds!=0) return false;
	PendingEnterCharacterId = CharacterId;
	bEnteredWorldSent = false;
	SetState(EACESessionState::EnteringWorld);
	SendGameMessage(ACEOpcode::CharacterEnterWorldRequest, {}, ACEQueue::UIQueue, true);
	Log(FString::Printf(TEXT("EnterWorld request character=%d — awaiting server ready"), CharacterId));
	return true;
}

bool FACESession::EnterWorldByName(const FString& CharacterName)
{
	for (const FACECharacterInfo& C : Characters)
	{
		if (C.Name.Equals(CharacterName, ESearchCase::IgnoreCase) ||
			C.Name.Equals(TEXT("+") + CharacterName, ESearchCase::IgnoreCase))
		{
			return EnterWorld(C.CharacterId);
		}
	}
	Log(FString::Printf(TEXT("Character '%s' not found"), *CharacterName));
	return false;
}

void FACESession::SendLoginComplete()
{
	// Safe to call on every portal exit — ACE OnTeleportComplete clears Teleporting each time.
	SendGameAction(ACEGameAction::LoginComplete, {}, ACEQueue::ControlQueue);
	bLoginCompleteSent = true;
	Log(TEXT("LoginComplete (exited portal space)"));
	MaybeEnterWorldComplete();
}

void FACESession::SetCurrentStance(uint32 Stance)
{
	uint32 Expanded = Stance;
	if (Expanded != 0 && Expanded <= 0xFFFFu)
	{
		Expanded = ACEMotion::ExpandPackedCommand(static_cast<uint16>(Expanded));
	}
	if ((Expanded & 0xFF000000u) == 0x80000000u)
	{
		CurrentStance = Expanded;
	}
}

void FACESession::SendMoveToState(float Forward, float Strafe, float Turn, bool bRunning, bool bContact,
	bool bStandingLongJump)
{
	if (State != EACESessionState::InWorld || bLogOffPending)
	{
		return;
	}

	// Same outdoor Z guard as AutonomousPosition — bad MoveToState poses desync movement.
	{
		const uint32 Cell = static_cast<uint32>(PlayerPosition.CellId);
		if ((Cell & 0xFFFFu) < 0x0100u && PlayerPosition.Location.Z < -40.f)
		{
			return;
		}
	}

	uint32 Flags = ACERawMotionFlags::CurrentHoldKey | ACERawMotionFlags::CurrentStyle;
	uint32 ForwardCommand = ACEMotion::Ready;
	float ForwardSpeed = 0.f;
	uint32 SidestepCommand = 0;
	float SidestepSpeed = 0.f;
	uint32 TurnCommand = 0;
	float TurnSpeed = 0.f;

	if (!FMath::IsNearlyZero(Forward))
	{
		Flags |= ACERawMotionFlags::ForwardCommand | ACERawMotionFlags::ForwardHoldKey | ACERawMotionFlags::ForwardSpeed;
		// Retail sends WalkForward/WalkBackwards and uses CurrentHoldKey=Run for sprint.
		// Sending RunForward directly skips MovementData's ForwardSpeed=GetRunRate() path.
		if (Forward > 0.f)
		{
			ForwardCommand = ACEMotion::WalkForward;
		}
		else
		{
			ForwardCommand = ACEMotion::WalkBackwards;
		}
		ForwardSpeed = FMath::Clamp(FMath::Abs(Forward), 0.f, 1.f);
	}

	if (!FMath::IsNearlyZero(Strafe))
	{
		Flags |= ACERawMotionFlags::SideStepCommand | ACERawMotionFlags::SideStepHoldKey | ACERawMotionFlags::SideStepSpeed;
		SidestepCommand = Strafe > 0.f ? ACEMotion::SideStepRight : ACEMotion::SideStepLeft;
		SidestepSpeed = FMath::Clamp(FMath::Abs(Strafe), 0.f, 1.f);
	}

	if (!FMath::IsNearlyZero(Turn))
	{
		Flags |= ACERawMotionFlags::TurnCommand | ACERawMotionFlags::TurnHoldKey | ACERawMotionFlags::TurnSpeed;
		TurnCommand = ACEMotion::TurnRight;
		TurnSpeed = FMath::Clamp(Turn, -1.f, 1.f);
	}

	FACEBinaryWriter W;
	W.WriteUInt32(Flags);
	W.WriteUInt32(bRunning ? ACEMotion::HoldKeyRun : ACEMotion::HoldKeyNone);
	// Must match server CurrentMotionState.Stance — hardcoding NonCombat forced peace mode
	// on every MoveToState (retail sends the active MotionStance here).
	W.WriteUInt32(CurrentStance != 0 ? CurrentStance : ACEMotion::StanceNonCombat);

	if (Flags & ACERawMotionFlags::ForwardCommand) W.WriteUInt32(ForwardCommand);
	if (Flags & ACERawMotionFlags::ForwardHoldKey) W.WriteUInt32(ACEMotion::HoldKeyNone);
	if (Flags & ACERawMotionFlags::ForwardSpeed) W.WriteFloat(ForwardSpeed);
	if (Flags & ACERawMotionFlags::SideStepCommand) W.WriteUInt32(SidestepCommand);
	if (Flags & ACERawMotionFlags::SideStepHoldKey) W.WriteUInt32(ACEMotion::HoldKeyNone);
	if (Flags & ACERawMotionFlags::SideStepSpeed) W.WriteFloat(SidestepSpeed);
	if (Flags & ACERawMotionFlags::TurnCommand) W.WriteUInt32(TurnCommand);
	if (Flags & ACERawMotionFlags::TurnHoldKey) W.WriteUInt32(ACEMotion::HoldKeyNone);
	if (Flags & ACERawMotionFlags::TurnSpeed) W.WriteFloat(TurnSpeed);

	// Position
	W.WriteUInt32(static_cast<uint32>(PlayerPosition.CellId));
	W.WriteFloat(PlayerPosition.Location.X);
	W.WriteFloat(PlayerPosition.Location.Y);
	W.WriteFloat(PlayerPosition.Location.Z);
	W.WriteFloat(PlayerPosition.RotationW);
	W.WriteFloat(PlayerPosition.RotationXYZ.X);
	W.WriteFloat(PlayerPosition.RotationXYZ.Y);
	W.WriteFloat(PlayerPosition.RotationXYZ.Z);

	W.WriteUInt16(InstanceSeq);
	W.WriteUInt16(ServerControlSeq);
	W.WriteUInt16(TeleportSeq);
	W.WriteUInt16(ForcePositionSeq);
	uint8 ContactFlags = 0;
	if (bContact)
	{
		ContactFlags |= 0x01;
	}
	if (bStandingLongJump)
	{
		ContactFlags |= 0x02;
	}
	W.WriteUInt8(ContactFlags);
	W.Align();

	SendGameAction(ACEGameAction::MoveToState, W.GetData(), ACEQueue::WeenieQueue);
	// Keep AutonomousPosition flowing while airborne (Contact=0) even with zero axes —
	// otherwise the server never gets landing poses and rubber-bands to the jump origin.
	bMoving = !FMath::IsNearlyZero(Forward) || !FMath::IsNearlyZero(Strafe)
		|| !FMath::IsNearlyZero(Turn) || !bContact;
	bAutoPosContact = bContact;
}

void FACESession::SendJump(float Extent, const FVector& LocalAceVelocity)
{
	if (State != EACESessionState::InWorld || bLogOffPending)
	{
		return;
	}
	FACEBinaryWriter W;
	W.WriteFloat(FMath::Clamp(Extent, 0.f, 1.f));
	W.WriteFloat(LocalAceVelocity.X);
	W.WriteFloat(LocalAceVelocity.Y);
	W.WriteFloat(LocalAceVelocity.Z);
	W.WriteUInt16(InstanceSeq);
	W.WriteUInt16(ServerControlSeq);
	W.WriteUInt16(TeleportSeq);
	W.WriteUInt16(ForcePositionSeq);
	// ACE.Server GameActionJump reads these after JumpPack (unused; retail pads them).
	W.WriteUInt32(0);
	W.WriteUInt32(0);
	SendGameAction(ACEGameAction::Jump, W.GetData(), ACEQueue::WeenieQueue);
}

void FACESession::SendStopMovement()
{
	SendMoveToState(0.f, 0.f, 0.f, false, true, false);
	bMoving = false;
}

void FACESession::SendAutonomousPosition(bool bContact)
{
	if (State != EACESessionState::InWorld || !PlayerPosition.IsValid() || bLogOffPending)
	{
		return;
	}

	// Outdoor: never report a deep underground Z — client fall-through poisons the
	// server pose and causes desync / bad re-login spawn (CharacterError / rubber-band).
	const uint32 Cell = static_cast<uint32>(PlayerPosition.CellId);
	if ((Cell & 0xFFFFu) < 0x0100u && PlayerPosition.Location.Z < -40.f)
	{
		return;
	}

	FACEBinaryWriter W;
	W.WriteUInt32(static_cast<uint32>(PlayerPosition.CellId));
	W.WriteFloat(PlayerPosition.Location.X);
	W.WriteFloat(PlayerPosition.Location.Y);
	W.WriteFloat(PlayerPosition.Location.Z);
	W.WriteFloat(PlayerPosition.RotationW);
	W.WriteFloat(PlayerPosition.RotationXYZ.X);
	W.WriteFloat(PlayerPosition.RotationXYZ.Y);
	W.WriteFloat(PlayerPosition.RotationXYZ.Z);
	W.WriteUInt16(InstanceSeq);
	W.WriteUInt16(ServerControlSeq);
	W.WriteUInt16(TeleportSeq);
	W.WriteUInt16(ForcePositionSeq);
	W.WriteUInt8(bContact ? 1 : 0);
	W.Align();

	SendGameAction(ACEGameAction::AutonomousPosition, W.GetData(), ACEQueue::SecureWeenieQueue);
}

void FACESession::FlushAutonomousPosition(bool bContact)
{
	bAutoPosContact = bContact;
	SendAutonomousPosition(bContact);
	AutoPosTimer = 0.f;
}

void FACESession::NotifyVitalsChanged()
{
	if (StatResolver) { StatResolver(PlayerVitals, ActiveEnchantments); }
	OnVitalsUpdated.Broadcast(PlayerVitals);
}
