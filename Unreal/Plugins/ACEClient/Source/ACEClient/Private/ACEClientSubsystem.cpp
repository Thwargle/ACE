#include "ACEClientSubsystem.h"
#include "ACECameraSettings.h"
#include "ACECharacterOptions.h"
#include "ACETerrainPresenterComponent.h"
#include "GameFramework/GameModeBase.h"
#include "ACESession.h"
#include "ACEInventoryRules.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACERetailGameTime.h"
#include "ACEPlayerController.h"
#include "VR/ACEVRComponent.h"
#include "GameFramework/Pawn.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIFlow.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

void UACEClientSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UACEDatSubsystem>();
	Session = MakeShared<FACESession>();

	UIElementManager = NewObject<UACEUIElementManager>(this);
	UIElementManager->Initialize();
	UIResourceResolver = NewObject<UACEUIResourceResolver>(this);
	UILayoutResolver = NewObject<UACEUILayoutResolver>(this);
	if (UGameInstance* GI = GetGameInstance())
	{
		UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>();
		Session->StatResolver = [Dat](FACEPlayerVitals& Vitals, const TArray<FACEActiveEnchantment>& Enchantments)
		{
			if (Dat) { Dat->RecomputePlayerStats(Vitals, Enchantments); }
		};
		UIResourceResolver->Initialize(Dat);
		UILayoutResolver->Initialize(Dat, UIElementManager);
	}
	UIFlow = NewObject<UACEUIFlow>(this);
	UIFlow->Initialize(UIElementManager, UILayoutResolver);

	Session->OnStateChanged.AddLambda([this](EACESessionState S)
	{
		OnSessionStateChanged.Broadcast(S);
	});
	Session->OnCharacterList.AddLambda([this](const TArray<FACECharacterInfo>& Chars, const FString& Name)
	{
		OnCharacterList.Broadcast(Chars, Name);
	});
	Session->OnEnteredWorld.AddLambda([this](int32 Guid, const FACEPosition& Pos)
	{
		OnEnteredWorld.Broadcast(Guid, Pos);
	});
	Session->OnPositionUpdate.AddLambda([this](int32 Guid, const FACEPosition& Pos)
	{
		OnPositionUpdate.Broadcast(Guid, Pos);
	});
	Session->OnPlayerTeleportStarted.AddLambda([this]()
	{
		OnPlayerTeleportStarted.Broadcast();
	});
	Session->OnMotionUpdate.AddLambda([this](int32 Guid, const FACEObjectMotionState& Motion)
	{
		OnMotionUpdate.Broadcast(Guid, Motion);
	});
	Session->OnVectorUpdate.AddLambda([this](int32 Guid, const FVector& AceVelocity, const FVector& AceOmega)
	{
		OnVectorUpdate.Broadcast(Guid, AceVelocity, AceOmega);
	});
	Session->OnObjectCreated.AddLambda([this](const FACEWorldObject& Obj)
	{
		OnObjectCreated.Broadcast(Obj);
	});
	Session->OnObjectDeleted.AddLambda([this](int32 Guid)
	{
		OnObjectDeleted.Broadcast(Guid);
	});
	Session->OnPhysicsStateUpdate.AddLambda([this](int32 Guid, int32 PhysicsState)
	{
		OnPhysicsStateUpdate.Broadcast(Guid, PhysicsState);
	});
	Session->OnPkStatusUpdated.AddLambda([this](int32 Guid, int32 Status)
	{
		OnPkStatusUpdated.Broadcast(Guid, Status);
	});
	Session->OnSound.AddLambda([this](int32 Guid, int32 SoundType, float Volume)
	{
		OnSound.Broadcast(Guid, SoundType, Volume);
	});
	Session->OnPlayScriptId.AddLambda([this](int32 Guid, int32 PhysicsScriptId, float Intensity)
	{
		OnPlayScriptId.Broadcast(Guid, PhysicsScriptId, Intensity);
	});
	Session->OnPlayEffect.AddLambda([this](int32 Guid, int32 ScriptType, float Intensity)
	{
		OnPlayEffect.Broadcast(Guid, ScriptType, Intensity);
	});
	Session->OnChatMessage.AddLambda([this](const FString& Text, const FString& Sender, int32 Type)
	{
		OnChatMessage.Broadcast(Text, Sender, Type);
	});
	Session->OnVitalsUpdated.AddLambda([this](const FACEPlayerVitals& Vitals)
	{
		if (Vitals.bValid)
		{
			SetRunSkill(Vitals.RunSkillCurrent);
			SetJumpSkill(Vitals.JumpSkillCurrent);
		}
		OnVitalsUpdated.Broadcast(Vitals);
	});
	Session->OnObjectHealth.AddLambda([this](int32 Guid, float Fraction)
	{
		OnObjectHealth.Broadcast(Guid, Fraction);
	});
	Session->OnAppraisal.AddLambda([this](const FACEAppraisalInfo& Info)
	{
		OnAppraisal.Broadcast(Info);
	});
	Session->OnSelectionChanged.AddLambda([this](const FACESelectedObject& Sel)
	{
		OnSelectionChanged.Broadcast(Sel);
	});
	Session->OnLog.AddLambda([this](const FString& Msg)
	{
		OnLogMessage.Broadcast(Msg);
	});
	Session->OnViewContentsExternal.AddLambda([this](int32 Guid)
	{
		OnExternalContainerOpened.Broadcast(Guid);
	});
	Session->OnCloseGroundContainer.AddLambda([this](int32 Guid)
	{
		OnExternalContainerClosed.Broadcast(Guid);
	});
	Session->OnApproachVendor.AddLambda([this](int32 Guid)
	{
		OnVendorOpened.Broadcast(Guid);
	});
	Session->OnTradeEvent.AddLambda([this](int32 EventType)
	{
		OnTradeStateChanged.Broadcast(EventType);
	});
	Session->OnChessEvent.AddLambda([this](const FACEChessEvent& Event)
	{
		OnChessEvent.Broadcast(Event);
	});
	Session->OnCharacterTitlesChanged.AddLambda([this]()
	{
		OnCharacterTitlesChanged.Broadcast();
	});
	Session->OnFellowshipChanged.AddLambda([this]()
	{
		OnFellowshipChanged.Broadcast();
	});
	Session->OnAllegianceChanged.AddLambda([this]()
	{
		OnAllegianceChanged.Broadcast();
	});
	Session->OnFriendsChanged.AddLambda([this]()
	{
		OnFriendsChanged.Broadcast();
	});
	Session->OnContractsChanged.AddLambda([this]()
	{
		OnContractsChanged.Broadcast();
	});
	Session->OnSquelchChanged.AddLambda([this]()
	{
		OnSquelchChanged.Broadcast();
	});
	Session->OnHouseChanged.AddLambda([this]()
	{
		OnHouseChanged.Broadcast();
	});
	Session->OnBookChanged.AddLambda([this]()
	{
		OnBookChanged.Broadcast();
	});
	Session->OnEnchantmentsChanged.AddLambda([this]()
	{
		OnEnchantmentsChanged.Broadcast();
	});
	Session->OnLinkStatusChanged.AddLambda([this](const FACELinkStatus& Status)
	{
		OnLinkStatusChanged.Broadcast(Status);
	});
}

void UACEClientSubsystem::Deinitialize()
{
	if (UILayoutResolver)
	{
		UILayoutResolver->Shutdown();
		UILayoutResolver = nullptr;
	}
	if (UIResourceResolver)
	{
		UIResourceResolver->Shutdown();
		UIResourceResolver = nullptr;
	}
	if (UIFlow)
	{
		UIFlow->Shutdown();
		UIFlow = nullptr;
	}
	if (UIElementManager)
	{
		UIElementManager->Shutdown();
		UIElementManager = nullptr;
	}
	if (Session)
	{
		// Best-effort CharacterLogOff before sockets close (PIE stop / map unload).
		Session->RequestLogOff();
		Session->Disconnect();
		Session.Reset();
	}
	Super::Deinitialize();
}

void UACEClientSubsystem::Tick(float DeltaTime)
{
	if (Session)
	{
		Session->Tick(DeltaTime);
		TickInventorySort(DeltaTime);
	}
}

bool UACEClientSubsystem::IsOwnedInventoryItem(const FACEWorldObject& Object) const
{
	if (!Object.ContainerId || Object.WielderId || Object.ParentGuid || Object.CurrentWieldedLocation) return false;
	if (Object.ContainerId==GetPlayerGuid()) return true;
	for (const auto& Pack:GetPlayerPacks()) if (Pack.Guid==Object.ContainerId) return true;
	return false;
}

bool UACEClientSubsystem::SortInventoryItem(int32 Guid)
{
	FACEWorldObject Source;
	if (!GetWorldObject(Guid,Source) || !IsOwnedInventoryItem(Source)) return false;
	if (SortSourceGuid || GetTradeSelfItems().Contains(Guid)) return true;
	SortSourceGuid=Guid; SortPlayerGuid=GetPlayerGuid(); SortMergeTargetGuid=0; SortWaitSeconds=0;
	TickInventorySort(0);
	return true;
}

void UACEClientSubsystem::TickInventorySort(float DeltaTime)
{
	if (!SortSourceGuid) return;
	if (GetSessionState()!=EACESessionState::InWorld || SortPlayerGuid!=GetPlayerGuid())
	{
		SortSourceGuid=0; return;
	}
	FACEWorldObject Source;
	const bool bHaveSource=GetWorldObject(SortSourceGuid,Source);
	const auto Offered=GetTradeSelfItems();
	if (Offered.Contains(SortSourceGuid) || (bHaveSource && !IsOwnedInventoryItem(Source)))
	{
		SortSourceGuid=0; return;
	}
	if (SortMergeTargetGuid)
	{
		// Do not predict/delete stacks locally or issue another merge from stale
		// quantities. Both server quantity updates must have arrived first.
		SortWaitSeconds+=DeltaTime;
		FACEWorldObject Target;
		if (!GetWorldObject(SortMergeTargetGuid,Target) || !IsOwnedInventoryItem(Target)
			|| Offered.Contains(Target.Guid) || SortWaitSeconds>8.f)
		{
			SortSourceGuid=0; return;
		}
		if (Target.StackSize!=SortExpectedTarget
			|| (SortExpectedSource>0 ? (!bHaveSource || Source.StackSize!=SortExpectedSource) : bHaveSource)) return;
		SendPutItemInContainer(Target.Guid,SortPlayerGuid,0);
		SortMergeTargetGuid=0;
	}
	if (!bHaveSource) { SortSourceGuid=0; return; }
	TArray<FACEWorldObject> Candidates=GetPackItems(SortPlayerGuid);
	for (const auto& Pack:GetPlayerPacks()) Candidates.Append(GetPackItems(Pack.Guid));
	for (const auto& Target:Candidates)
	{
		if (!IsOwnedInventoryItem(Target) || Offered.Contains(Target.Guid)) continue;
		const int32 Amount=ACEInventoryRules::MergeAmount(Source,Target);
		if (Amount<=0) continue;
		SortMergeTargetGuid=Target.Guid; SortWaitSeconds=0;
		SortExpectedSource=FMath::Max(1,Source.StackSize)-Amount;
		SortExpectedTarget=FMath::Max(1,Target.StackSize)+Amount;
		SendStackableMerge(Source.Guid,Target.Guid,Amount);
		return;
	}
	SendPutItemInContainer(Source.Guid,SortPlayerGuid,0);
	SortSourceGuid=0;
}

bool UACEClientSubsystem::Login(const FString& Host, int32 Port, const FString& Account, const FString& Password, bool bGDLE)
{
	if (!Session)
	{
		return false;
	}
	FACELoginCredentials Creds;
	Creds.Host = Host;
	Creds.Port = Port;
	Creds.Account = Account;
	Creds.Password = Password;
	Creds.bGDLE = bGDLE;
	return Session->Connect(Creds);
}

void UACEClientSubsystem::Logout()
{
	if (Session)
	{
		Session->RequestLogOff();
	}
}

bool UACEClientSubsystem::EnterWorld(int32 CharacterId)
{
	return Session ? Session->EnterWorld(CharacterId) : false;
}

bool UACEClientSubsystem::EnterWorldByName(const FString& CharacterName)
{
	return Session ? Session->EnterWorldByName(CharacterName) : false;
}

void UACEClientSubsystem::SendMovement(float Forward, float Strafe, float Turn, bool bRunning)
{
	SendMovementEx(Forward, Strafe, Turn, bRunning, false, true);
}

void UACEClientSubsystem::SendMovementEx(float Forward, float Strafe, float Turn, bool bRunning, bool bStandingLongJump, bool bContact)
{
	if (Session)
	{
		Session->SendMoveToState(Forward, Strafe, Turn, bRunning, bContact, bStandingLongJump);
	}
}

void UACEClientSubsystem::SendJump(float Extent, const FVector& LocalAceVelocity)
{
	if (Session)
	{
		Session->SendJump(Extent, LocalAceVelocity);
	}
}

void UACEClientSubsystem::StopMovement()
{
	if (Session)
	{
		Session->SendStopMovement();
	}
}

void UACEClientSubsystem::SendUseItem(int32 ObjectGuid)
{
	if (Session)
	{
		Session->SendUseItem(ObjectGuid);
	}
}

void UACEClientSubsystem::SendChangeCombatMode(int32 CombatMode)
{
	if (Session)
	{
		Session->SendChangeCombatMode(static_cast<uint32>(CombatMode));
	}
}

void UACEClientSubsystem::SendTargetedMeleeAttack(int32 TargetGuid, int32 AttackHeight, float PowerLevel)
{
	if (Session)
	{
		Session->SendTargetedMeleeAttack(TargetGuid, static_cast<uint32>(AttackHeight), PowerLevel);
	}
}

void UACEClientSubsystem::SendTargetedMissileAttack(int32 TargetGuid, int32 AttackHeight, float AccuracyLevel)
{
	if (Session)
	{
		Session->SendTargetedMissileAttack(TargetGuid, static_cast<uint32>(AttackHeight), AccuracyLevel);
	}
}

void UACEClientSubsystem::SendChatMessage(const FString& Message)
{
	if (Session)
	{
		Session->SendTalk(Message);
	}
}

void UACEClientSubsystem::SendChatChannel(int32 ChannelId, const FString& Message)
{
	if (Session && ChannelId != 0 && !Message.IsEmpty())
	{
		Session->SendChatChannel(static_cast<uint32>(ChannelId), Message);
	}
}

bool UACEClientSubsystem::IsWorldObjectVisible(const FACEWorldObject& Object) const
{
	const FACEWorldObject* Occupant=&Object;
	for (int32 Depth=0; Depth<8; ++Depth)
	{
		const int32 Parent=Occupant->ParentGuid ? Occupant->ParentGuid : Occupant->WielderId;
		if (!Parent) break;
		if (Parent==GetPlayerGuid() || !Session) return true;
		Occupant=Session->GetWorldObjects().Find(Parent);
		if (!Occupant) return true;
	}
	if (!Occupant->bHasPosition || Occupant->ContainerId) return true;
	const auto* World = GetWorld();
	const auto* GameMode = World ? World->GetAuthGameMode() : nullptr;
	const auto* Terrain = GameMode ? GameMode->FindComponentByClass<UACETerrainPresenterComponent>() : nullptr;
	return !Terrain || Terrain->IsWorldCellVisible(Occupant->Position.CellId);
}

void UACEClientSubsystem::SelectObject(int32 ObjectGuid)
{
	FACEWorldObject Object;
	if (ObjectGuid && GetWorldObject(ObjectGuid, Object)
		&& ((Object.bDying && !Object.IsCorpse()) || !IsWorldObjectVisible(Object)
			|| (Object.PhysicsState & (ACEPhysicsState::Missile | ACEPhysicsState::ParticleEmitter)) != 0)) ObjectGuid=0;
	if (Session)
	{
		Session->SelectObject(ObjectGuid);
	}
}

void UACEClientSubsystem::SendSetInscription(int32 ObjectGuid, const FString& Text)
{
	if (Session) Session->SendSetInscription(ObjectGuid, Text);
}

void UACEClientSubsystem::SendIdentifyObject(int32 ObjectGuid)
{
	if (Session)
	{
		++IdentifyRequestSerial;
		IdentifyRequestGuid = ObjectGuid;
		Session->SendIdentifyObject(ObjectGuid);
	}
}

void UACEClientSubsystem::SendRaiseAttribute(int32 AttributeId, int32 XpAmount)
{
	if (Session && AttributeId > 0 && XpAmount > 0)
	{
		Session->SendRaiseAttribute(static_cast<uint32>(AttributeId), static_cast<uint32>(XpAmount));
	}
}

void UACEClientSubsystem::SendRaiseVital(int32 VitalId, int32 XpAmount)
{
	if (Session && VitalId > 0 && XpAmount > 0)
	{
		Session->SendRaiseVital(static_cast<uint32>(VitalId), static_cast<uint32>(XpAmount));
	}
}

void UACEClientSubsystem::SendRaiseSkill(int32 SkillId, int32 XpAmount)
{
	if (Session && SkillId > 0 && XpAmount > 0)
	{
		Session->SendRaiseSkill(static_cast<uint32>(SkillId), static_cast<uint32>(XpAmount));
	}
}

void UACEClientSubsystem::SendTrainSkill(int32 SkillId, int32 CreditsSpent)
{
	if (Session && SkillId > 0 && CreditsSpent > 0)
	{
		Session->SendTrainSkill(static_cast<uint32>(SkillId), CreditsSpent);
	}
}

void UACEClientSubsystem::SendEmote(const FString& EmoteText)
{
	if (Session && !EmoteText.IsEmpty())
	{
		Session->SendEmote(EmoteText);
	}
}

void UACEClientSubsystem::SendSoulEmote(const FString& EmoteText)
{
	if (Session && !EmoteText.IsEmpty())
	{
		Session->SendSoulEmote(EmoteText);
	}
}

void UACEClientSubsystem::SendSoulEmoteMotion(int32 MotionCommand)
{
	if (Session && MotionCommand != 0)
	{
		Session->SendSoulEmoteMotion(static_cast<uint32>(MotionCommand));
	}
}

void UACEClientSubsystem::SendTell(const FString& TargetName, const FString& Message)
{
	if (Session)
	{
		Session->SendTell(TargetName, Message);
	}
}

void UACEClientSubsystem::SendTalkDirect(int32 TargetGuid, const FString& Message)
{
	if (Session)
	{
		Session->SendTalkDirect(TargetGuid, Message);
	}
}

void UACEClientSubsystem::SendTeleToLifestone()
{
	if (Session)
	{
		Session->SendTeleToLifestone();
	}
}

void UACEClientSubsystem::SendTeleToMarketplace()
{
	if (Session)
	{
		Session->SendTeleToMarketplace();
	}
}

void UACEClientSubsystem::SendTeleToHouse()
{
	if (Session)
	{
		Session->SendTeleToHouse();
	}
}

void UACEClientSubsystem::SendTeleToMansion()
{
	if (Session)
	{
		Session->SendTeleToMansion();
	}
}

void UACEClientSubsystem::SendRecallAllegianceHometown()
{
	if (Session)
	{
		Session->SendRecallAllegianceHometown();
	}
}

int32 UACEClientSubsystem::GetLastTellSenderGuid() const
{
	return Session ? Session->GetLastTellSenderGuid() : 0;
}

FString UACEClientSubsystem::GetLastTellSenderName() const
{
	return Session ? Session->GetLastTellSenderName() : FString();
}

void UACEClientSubsystem::SendPutItemInContainer(int32 ItemGuid, int32 ContainerGuid, int32 Placement)
{
	if (Session)
	{
		Session->SendPutItemInContainer(ItemGuid, ContainerGuid, Placement);
	}
}

void UACEClientSubsystem::SendStackableMerge(int32 MergeFromGuid, int32 MergeToGuid, int32 Amount)
{
	if (Session)
	{
		Session->SendStackableMerge(MergeFromGuid, MergeToGuid, Amount);
	}
}

void UACEClientSubsystem::SendStackableSplitToContainer(int32 StackGuid, int32 ContainerGuid, int32 Placement, int32 Amount)
{
	if (Session)
	{
		Session->SendStackableSplitToContainer(StackGuid, ContainerGuid, Placement, Amount);
	}
}

void UACEClientSubsystem::SendStackableSplitTo3D(int32 StackGuid, int32 Amount)
{
	if (Amount <= 0) return;
	if (Session)
	{
		auto* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		auto* VR = PC && PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UACEVRComponent>() : nullptr;
		if (VR && VR->TryDropInventoryItem(StackGuid, Amount)) return;
		Session->SendStackableSplitTo3D(StackGuid, Amount);
	}
}

void UACEClientSubsystem::SendStackableSplitToWield(int32 StackGuid, int64 WieldLocation, int32 Amount)
{
	if (Session)
	{
		Session->SendStackableSplitToWield(StackGuid, WieldLocation, Amount);
	}
}

void UACEClientSubsystem::SendUseWithTarget(int32 SourceGuid, int32 TargetGuid)
{
	if (Session)
	{
		Session->SendUseWithTarget(SourceGuid, TargetGuid);
	}
}

void UACEClientSubsystem::SendSetTitle(int32 TitleId)
{
	if (Session)
	{
		Session->SendSetTitle(static_cast<uint32>(TitleId));
	}
}

bool UACEClientSubsystem::IsCharacterOptionSet(int32 Option) const
{
	// Retail sends this bit clear by default. Use the local camera preference so
	// fresh characters get mouse turning and server echoes cannot undo an opt-out.
	if (Option == 0x31) return ACECameraSettings::GetUseMouseTurning();
	return Session ? Session->IsCharacterOptionSet(Option) : false;
}

void UACEClientSubsystem::SendSetSingleCharacterOption(int32 Option, bool bValue)
{
	if (Option == 0x31) ACECameraSettings::SetUseMouseTurning(bValue);
	if (Session)
	{
		Session->SendSetSingleCharacterOption(Option, bValue);
	}
}

void UACEClientSubsystem::SendCharacterOptions(uint32 Options1, uint32 Options2)
{
	ACECameraSettings::SetUseMouseTurning((Options2 & ACECharacterOptions::MouseTurningFlag) != 0);
	if (Session)
	{
		Session->SendCharacterOptions(Options1, Options2);
	}
}

uint32 UACEClientSubsystem::GetCharacterOptions1() const
{
	return Session ? Session->GetCharacterOptions1() : 0;
}

uint32 UACEClientSubsystem::GetCharacterOptions2() const
{
	const uint32 Bits = Session ? Session->GetCharacterOptions2() : 0;
	return ACECameraSettings::GetUseMouseTurning() ? Bits | ACECharacterOptions::MouseTurningFlag : Bits & ~ACECharacterOptions::MouseTurningFlag;
}

TArray<int32> UACEClientSubsystem::GetCharacterTitleIds() const
{
	TArray<int32> Out;
	if (!Session)
	{
		return Out;
	}
	for (uint32 Id : Session->GetCharacterTitleIds())
	{
		Out.Add(static_cast<int32>(Id));
	}
	return Out;
}

int32 UACEClientSubsystem::GetDisplayTitleId() const
{
	return Session ? static_cast<int32>(Session->GetDisplayTitleId()) : 0;
}

bool UACEClientSubsystem::IsUseBusy() const
{
	return Session && Session->IsUseBusy();
}

void UACEClientSubsystem::SendGetAndWieldItem(int32 ItemGuid, int64 WieldLocation)
{
	if (Session)
	{
		Session->SendGetAndWieldItem(ItemGuid, WieldLocation);
	}
}

void UACEClientSubsystem::SendDropItem(int32 ItemGuid)
{
	if (Session)
	{
		auto* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		auto* VR = PC && PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UACEVRComponent>() : nullptr;
		if (VR && VR->TryDropInventoryItem(ItemGuid)) return;
		Session->SendDropItem(ItemGuid);
	}
}

void UACEClientSubsystem::SendGiveObjectRequest(int32 TargetGuid, int32 ItemGuid, int32 Amount)
{
	if (Session)
	{
		int32 GiveAmount = Amount;
		if (GiveAmount <= 0)
		{
			FACEWorldObject Obj;
			if (Session->GetWorldObject(ItemGuid, Obj) && Obj.StackSize > 0)
			{
				GiveAmount = Obj.StackSize;
			}
			else
			{
				GiveAmount = 1;
			}
		}
		Session->SendGiveObjectRequest(TargetGuid, ItemGuid, FMath::Max(1, GiveAmount));
	}
}

void UACEClientSubsystem::SendNoLongerViewingContents(int32 ContainerGuid)
{
	if (Session)
	{
		Session->SendNoLongerViewingContents(ContainerGuid);
	}
}

void UACEClientSubsystem::SendBuyItems(int32 VendorGuid, const TArray<TPair<int32, int32>>& AmountAndObjectId)
{
	if (Session)
	{
		Session->SendBuyItems(VendorGuid, AmountAndObjectId);
	}
}

void UACEClientSubsystem::SendSellItems(int32 VendorGuid, const TArray<TPair<int32, int32>>& AmountAndObjectId)
{
	if (Session)
	{
		Session->SendSellItems(VendorGuid, AmountAndObjectId);
	}
}

void UACEClientSubsystem::SendOpenTrade(int32 OtherPlayerGuid)
{
	if (Session)
	{
		Session->SendOpenTrade(OtherPlayerGuid);
	}
}

void UACEClientSubsystem::SendCloseTrade()
{
	if (Session)
	{
		Session->SendCloseTrade();
	}
}

void UACEClientSubsystem::SendAddToTrade(int32 ItemGuid, int32 TradeSlotSide)
{
	if (Session)
	{
		Session->SendAddToTrade(ItemGuid, TradeSlotSide);
	}
}

void UACEClientSubsystem::SendAcceptTrade()
{
	if (Session)
	{
		Session->SendAcceptTrade();
	}
}

void UACEClientSubsystem::SendDeclineTrade()
{
	if (Session)
	{
		Session->SendDeclineTrade();
	}
}

void UACEClientSubsystem::SendResetTrade()
{
	if (Session)
	{
		Session->SendResetTrade();
	}
}

int32 UACEClientSubsystem::GetOpenExternalContainerGuid() const
{
	return Session ? Session->GetOpenExternalContainerGuid() : 0;
}

int32 UACEClientSubsystem::GetOpenVendorGuid() const
{
	return Session ? Session->GetOpenVendorGuid() : 0;
}

TArray<FACEWorldObject> UACEClientSubsystem::GetVendorMerchandise() const
{
	return Session ? Session->GetVendorMerchandise() : TArray<FACEWorldObject>();
}

float UACEClientSubsystem::GetVendorBuyRate() const
{
	return Session ? Session->GetVendorBuyRate() : 1.f;
}

float UACEClientSubsystem::GetVendorSellRate() const
{
	return Session ? Session->GetVendorSellRate() : 1.f;
}

int32 UACEClientSubsystem::GetTradePartnerGuid() const
{
	return Session ? Session->GetTradePartnerGuid() : 0;
}

int32 UACEClientSubsystem::GetTradeAcceptedGuid() const
{
	return Session ? Session->GetTradeAcceptedGuid() : 0;
}

TArray<int32> UACEClientSubsystem::GetTradeSelfItems() const
{
	return Session ? Session->GetTradeSelfItems() : TArray<int32>();
}

TArray<int32> UACEClientSubsystem::GetTradePartnerItems() const
{
	return Session ? Session->GetTradePartnerItems() : TArray<int32>();
}

FACEFellowshipInfo UACEClientSubsystem::GetFellowship() const
{
	return Session ? Session->GetFellowship() : FACEFellowshipInfo();
}

FACEAllegianceInfo UACEClientSubsystem::GetAllegiance() const
{
	return Session ? Session->GetAllegiance() : FACEAllegianceInfo();
}

TArray<FACEFriendInfo> UACEClientSubsystem::GetFriends() const
{
	return Session ? Session->GetFriends() : TArray<FACEFriendInfo>();
}

TArray<FACEContractEntry> UACEClientSubsystem::GetContracts() const
{
	return Session ? Session->GetContracts() : TArray<FACEContractEntry>();
}

void UACEClientSubsystem::SendAbandonContract(int32 ContractId)
{
	if (Session) { Session->SendAbandonContract(ContractId); }
}

void UACEClientSubsystem::SendFellowshipCreate(const FString& Name, bool bShareXP)
{
	if (Session) { Session->SendFellowshipCreate(Name, bShareXP); }
}

void UACEClientSubsystem::SendFellowshipQuit(bool bDisband)
{
	if (Session) { Session->SendFellowshipQuit(bDisband); }
}

void UACEClientSubsystem::SendFellowshipDismiss(int32 MemberGuid)
{
	if (Session) { Session->SendFellowshipDismiss(MemberGuid); }
}

void UACEClientSubsystem::SendFellowshipRecruit(int32 PlayerGuid)
{
	if (Session) { Session->SendFellowshipRecruit(PlayerGuid); }
}

void UACEClientSubsystem::SendFellowshipUpdateRequest(bool bPanelOpen)
{
	if (Session) { Session->SendFellowshipUpdateRequest(bPanelOpen); }
}

void UACEClientSubsystem::SendFellowshipAssignNewLeader(int32 MemberGuid)
{
	if (Session) { Session->SendFellowshipAssignNewLeader(MemberGuid); }
}

void UACEClientSubsystem::SendFellowshipChangeOpenness(bool bOpen)
{
	if (Session) { Session->SendFellowshipChangeOpenness(bOpen); }
}

FACEHouseInfo UACEClientSubsystem::GetHouseInfo() const
{
	return Session ? Session->GetHouseInfo() : FACEHouseInfo();
}

void UACEClientSubsystem::SendHouseQuery()
{
	if (Session) { Session->SendHouseQuery(); }
}

FACEBookInfo UACEClientSubsystem::GetBookInfo() const
{
	return Session ? Session->GetBookInfo() : FACEBookInfo();
}

void UACEClientSubsystem::ClearBook()
{
	if (Session) { Session->ClearBook(); }
}

void UACEClientSubsystem::SendAbuseLogRequest(const FString& CharacterName, int32 StatusMask,
	const FString& Complaint)
{
	if (Session)
	{
		Session->SendAbuseLogRequest(CharacterName, static_cast<uint32>(StatusMask), Complaint);
	}
}

void UACEClientSubsystem::SendBookData(int32 BookGuid)
{
	if (Session) { Session->SendBookData(BookGuid); }
}

void UACEClientSubsystem::SendBookPageData(int32 BookGuid, int32 PageIndex)
{
	if (Session) { Session->SendBookPageData(BookGuid, PageIndex); }
}

void UACEClientSubsystem::SendChessQuit()
{
	if (Session) { Session->SendChessQuit(); }
}

void UACEClientSubsystem::SendChessMovePass()
{
	if (Session) { Session->SendChessMovePass(); }
}

void UACEClientSubsystem::SendChessStalemate(bool bStalemate)
{
	if (Session) { Session->SendChessStalemate(bStalemate); }
}

void UACEClientSubsystem::SendChessJoin(int32 BoardGuid)
{
	if (Session) { Session->SendChessJoin(BoardGuid); }
}

void UACEClientSubsystem::SendChessMove(int32 FromX, int32 FromY, int32 ToX, int32 ToY)
{
	if (Session) { Session->SendChessMove(FromX, FromY, ToX, ToY); }
}

TArray<FACESquelchEntry> UACEClientSubsystem::GetSquelches() const
{
	return Session ? Session->GetSquelches() : TArray<FACESquelchEntry>();
}

void UACEClientSubsystem::SendModifyCharacterSquelch(bool bSquelch, int32 PlayerGuid,
	const FString& PlayerName, int32 MessageType)
{
	if (Session)
	{
		Session->SendModifyCharacterSquelch(bSquelch, PlayerGuid, PlayerName,
			static_cast<uint32>(MessageType));
	}
}

void UACEClientSubsystem::SendModifyAccountSquelch(bool bSquelch, const FString& PlayerName)
{
	if (Session) { Session->SendModifyAccountSquelch(bSquelch, PlayerName); }
}

TMap<int32, int32> UACEClientSubsystem::GetDesiredComponents() const
{
	return Session ? Session->GetDesiredComponents() : TMap<int32, int32>();
}

void UACEClientSubsystem::SendSetDesiredComponentLevel(int32 ComponentWcid, int32 Amount)
{
	if (Session) { Session->SendSetDesiredComponentLevel(ComponentWcid, Amount); }
}

void UACEClientSubsystem::SendSwearAllegiance(int32 TargetGuid)
{
	if (Session) { Session->SendSwearAllegiance(TargetGuid); }
}

void UACEClientSubsystem::SendBreakAllegiance(int32 TargetGuid)
{
	if (Session) { Session->SendBreakAllegiance(TargetGuid); }
}

void UACEClientSubsystem::SendAllegianceUpdateRequest(bool bPanelOpen)
{
	if (Session) { Session->SendAllegianceUpdateRequest(bPanelOpen); }
}

void UACEClientSubsystem::SendAddFriend(const FString& Name)
{
	if (Session) { Session->SendAddFriend(Name); }
}

void UACEClientSubsystem::SendRemoveFriend(int32 FriendGuid)
{
	if (Session) { Session->SendRemoveFriend(FriendGuid); }
}

void UACEClientSubsystem::SendCreateTinkeringTool(int32 ToolGuid, const TArray<int32>& ItemGuids)
{
	if (Session) { Session->SendCreateTinkeringTool(ToolGuid, ItemGuids); }
}

void UACEClientSubsystem::SendAddSpellToBar(int32 SpellId, int32 SlotIndex, int32 BarIndex)
{
	if (Session)
	{
		Session->SendAddSpellToBar(SpellId, SlotIndex, BarIndex);
	}
}

void UACEClientSubsystem::SendRemoveSpellFromBar(int32 SpellId, int32 BarIndex)
{
	if (Session)
	{
		Session->SendRemoveSpellFromBar(SpellId, BarIndex);
	}
}

void UACEClientSubsystem::SendAddShortcut(int32 SlotIndex, int32 ObjectGuid)
{
	if (Session)
	{
		Session->SendAddShortcut(SlotIndex, ObjectGuid);
	}
}

void UACEClientSubsystem::SendRemoveShortcut(int32 SlotIndex)
{
	if (Session)
	{
		Session->SendRemoveShortcut(SlotIndex);
	}
}

bool UACEClientSubsystem::SendCastSpell(int32 SpellId, int32 CasterItemGuid)
{
	if (!Session || SpellId == 0)
	{
		return false;
	}
	if (auto* VRController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (APawn* Pawn = VRController->GetPawn())
			if (auto* VR = Pawn->FindComponentByClass<UACEVRComponent>(); VR && VR->IsActive())
				return VR->SelectSpell(SpellId);

	// Match portal.dat SpellTable + ACE CreatePlayerSpell targeting:
	// NonComponentTargetType == ItemType.None → untargeted; SelfTargeted → self;
	// otherwise require a selected world target (no cast-into-air projectiles).
	constexpr uint32 SpellFlagSelfTargeted = 0x8u;
	uint32 Bitfield = 0;
	uint32 NonComponentTargetType = 0;
	FString SpellName;
	bool bHaveTargeting = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			bHaveTargeting = Dat->TryGetSpellTargeting(
				static_cast<uint32>(SpellId), Bitfield, NonComponentTargetType);
			uint32 IconDid = 0;
			Dat->TryGetSpellInfo(static_cast<uint32>(SpellId), SpellName, IconDid);
		}
	}

	const bool bNameLooksSelf = SpellName.EndsWith(TEXT(" Self"), ESearchCase::IgnoreCase);
	const bool bSelfTargeted = (Bitfield & SpellFlagSelfTargeted) != 0 || bNameLooksSelf;
	// Untargeted only when the DAT says ItemType.None and the spell is not self-targeted.
	const bool bUntargeted = bHaveTargeting && NonComponentTargetType == 0 && !bSelfTargeted;

	const FACESelectedObject Sel = Session->GetSelectedObject();
	const int32 PlayerGuid = Session->GetPlayerGuid();

	int32 Target = 0;
	if (bUntargeted)
	{
		Target = 0;
	}
	else if (bSelfTargeted)
	{
		// Self spells never require clicking yourself first.
		Target = PlayerGuid;
		if (Target == 0)
		{
			return false;
		}
	}
	else if (Sel.bValid && Sel.Guid != 0)
	{
		Target = Sel.Guid;
	}
	else
	{
		// Other / bolt spells need an explicit selection.
		return false;
	}

	if (Target != 0 && Target != PlayerGuid)
	{
		if (UWorld* World = GetWorld())
		{
			if (AACEPlayerController* PC = Cast<AACEPlayerController>(World->GetFirstPlayerController()))
			{
				PC->FaceWorldTarget(Target);
			}
		}
	}

	// Wand/orb BuiltInSpell: retail UseWithTarget(caster, target) so ACE VerifySpell
	// sees the weapon (CastTargetedSpell alone → MagicInvalidSpellType 0x03FC).
	if (CasterItemGuid != 0)
	{
		if (Target == 0)
		{
			Target = PlayerGuid;
		}
		if (Target == 0)
		{
			return false;
		}
		Session->SendUseWithTarget(CasterItemGuid, Target);
		return true;
	}

	Session->SendCastSpell(SpellId, Target);
	return true;
}

TArray<int32> UACEClientSubsystem::GetKnownSpells() const
{
	return Session ? Session->GetKnownSpells() : TArray<int32>();
}

TArray<int32> UACEClientSubsystem::GetSpellBar(int32 BarIndex) const
{
	return Session ? Session->GetSpellBar(BarIndex) : TArray<int32>();
}

void UACEClientSubsystem::SetActiveSpellBar(int32 BarIndex)
{
	if (Session)
	{
		Session->SetActiveSpellBar(BarIndex);
	}
}

int32 UACEClientSubsystem::GetActiveSpellBar() const
{
	return Session ? Session->GetActiveSpellBar() : 0;
}

int32 UACEClientSubsystem::GetShortcutObject(int32 SlotIndex) const
{
	return Session ? Session->GetShortcutObject(SlotIndex) : 0;
}

FACEPlayerVitals UACEClientSubsystem::GetPlayerVitals() const
{
	return Session ? Session->GetPlayerVitals() : FACEPlayerVitals();
}

bool UACEClientSubsystem::TryGetPlayerEncumbrance(int32& OutEncumbrance) const
{
	return Session ? Session->TryGetPlayerEncumbrance(OutEncumbrance) : false;
}

TArray<FACEActiveEnchantment> UACEClientSubsystem::GetActiveEnchantments() const
{
	TArray<FACEActiveEnchantment> Out;
	if (Session)
	{
		Session->GetActiveEnchantments(Out);
	}
	return Out;
}

bool UACEClientSubsystem::TryGetVitaeMultiplier(float& OutMultiplier) const
{
	return Session ? Session->TryGetVitaeMultiplier(OutMultiplier) : false;
}

int32 UACEClientSubsystem::GetVitaeCpPool() const
{
	return Session ? Session->GetVitaeCpPool() : 0;
}

int32 UACEClientSubsystem::GetDeathLevel() const
{
	return Session ? Session->GetDeathLevel() : 0;
}

FACELinkStatus UACEClientSubsystem::GetLinkStatus() const
{
	return Session ? Session->GetLinkStatus() : FACELinkStatus();
}

void UACEClientSubsystem::SendPingRequest()
{
	if (Session)
	{
		Session->SendPingRequest();
	}
}

FACESelectedObject UACEClientSubsystem::GetSelectedObject() const
{
	return Session ? Session->GetSelectedObject() : FACESelectedObject();
}

void UACEClientSubsystem::NotifyExitedPortalSpace()
{
	if (Session)
	{
		Session->NotifyExitedPortalSpace();
	}
}

void UACEClientSubsystem::SetReportedPosition(const FACEPosition& Position)
{
	if (!Session)
	{
		return;
	}
	// Do not feed outdoor fall-through Z back into AutoPos / MoveToState.
	const uint32 Cell = static_cast<uint32>(Position.CellId);
	if ((Cell & 0xFFFFu) < 0x0100u && Position.Location.Z < -40.f)
	{
		FACEPosition Clamped = Position;
		FACEPosition Cur = Session->GetPlayerPosition();
		Clamped.Location.Z = Cur.IsValid() ? Cur.Location.Z : 0.f;
		Session->SetLocalPosition(Clamped);
		return;
	}
	Session->SetLocalPosition(Position);
}

void UACEClientSubsystem::SetForcePositionReporting(bool bForce)
{
	if (Session)
	{
		Session->SetForcePositionReporting(bForce);
	}
}

void UACEClientSubsystem::FlushAutonomousPosition(bool bContact)
{
	if (Session)
	{
		Session->FlushAutonomousPosition(bContact);
	}
}

EACESessionState UACEClientSubsystem::GetSessionState() const
{
	return Session ? Session->GetState() : EACESessionState::Disconnected;
}

TArray<FACECharacterInfo> UACEClientSubsystem::GetCharacters() const
{
	return Session ? Session->GetCharacters() : TArray<FACECharacterInfo>();
}

FString UACEClientSubsystem::GetServerName() const
{
	return Session ? Session->GetServerName() : FString();
}

int32 UACEClientSubsystem::GetPlayerGuid() const
{
	return Session ? Session->GetPlayerGuid() : 0;
}

FACEPosition UACEClientSubsystem::GetPlayerPosition() const
{
	return Session ? Session->GetPlayerPosition() : FACEPosition();
}

int32 UACEClientSubsystem::GetTeleportSeq() const
{
	return Session ? static_cast<int32>(Session->GetTeleportSeq()) : 0;
}

namespace
{
	const FACEDatRegionSky& GameClockRegion(const UACEClientSubsystem& Client)
	{
		if (const UGameInstance* GI = Client.GetGameInstance())
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
				if (const FACEDatRegionSky* Sky = Dat->GetRegionSkyInfo()) return *Sky;
		static const FACEDatRegionSky RetailDefaults;
		return RetailDefaults;
	}
}

bool UACEClientSubsystem::HasGameTime() const
{
	return Session && Session->HasServerTime();
}

float UACEClientSubsystem::GetGameDayFraction(float DefaultIfUnknown) const
{
	return HasGameTime() ? ACERetailGameTime::DayFraction(GetGameTimeTicks(), GameClockRegion(*this)) : DefaultIfUnknown;
}

double UACEClientSubsystem::GetGameTimeTicks() const
{
	return Session ? Session->GetPortalYearTicks() : 0.0;
}

int32 UACEClientSubsystem::GetGameDayNumber() const
{
	return HasGameTime() ? static_cast<int32>(ACERetailGameTime::DayIndex(GetGameTimeTicks(), GameClockRegion(*this))) : 0;
}

bool UACEClientSubsystem::GetWorldObject(int32 Guid, FACEWorldObject& OutObject) const
{
	return Session ? Session->GetWorldObject(Guid, OutObject) : false;
}

TArray<FACEWorldObject> UACEClientSubsystem::GetWorldObjects() const
{
	TArray<FACEWorldObject> Out;
	if (!Session)
	{
		return Out;
	}
	Session->GetWorldObjects().GenerateValueArray(Out);
	return Out;
}

TArray<FACEWorldObject> UACEClientSubsystem::GetEquippedItems() const
{
	TArray<FACEWorldObject> Out;
	if (Session)
	{
		Session->GetEquippedItems(Out);
	}
	return Out;
}

TArray<FACEWorldObject> UACEClientSubsystem::GetPackItems(int32 ContainerGuid) const
{
	TArray<FACEWorldObject> Out;
	if (Session)
	{
		Session->GetPackItems(ContainerGuid, Out);
	}
	return Out;
}

void UACEClientSubsystem::ClearContainerContents(int32 ContainerGuid)
{
	if (Session)
	{
		Session->ClearContainerContents(ContainerGuid);
	}
}

TArray<FACEWorldObject> UACEClientSubsystem::GetPlayerPacks() const
{
	TArray<FACEWorldObject> Out;
	if (Session)
	{
		Session->GetPlayerPacks(Out);
	}
	return Out;
}

float UACEClientSubsystem::GetMovementBurden() const
{
	int32 Encumbrance = 0;
	if (Session && Session->GetPlayerVitals().bValid && Session->TryGetPlayerEncumbrance(Encumbrance))
	{
		// Do not wait for the inventory UI to refresh before applying a weight or
		// Strength change to movement. Use the same capacity as the retail HUD.
		const auto& V = Session->GetPlayerVitals();
		const float Capacity = FMath::Max(1, V.GetBuffedStrength()) *
			(150.f + 30.f * FMath::Clamp(V.CarryingCapacityAugs, 0, 5));
		return FMath::Max(0, Encumbrance) / Capacity;
	}
	return Burden;
}

float UACEClientSubsystem::GetRunRate() const
{
	// ACE.Server EncumbranceSystem.GetBurdenMod + MovementSystem.GetRunRate
	const float Load = GetMovementBurden();
	float LoadMod = 1.f;
	if (Load >= 2.f)
	{
		LoadMod = 0.f;
	}
	else if (Load >= 1.f)
	{
		LoadMod = 2.f - Load;
	}

	// CACQualities::InqRunRate uses zero skill while exhausted. An unknown
	// stamina value before PlayerDescription must not slow a valid preview.
	const auto* Vitals = Session ? &Session->GetPlayerVitals() : nullptr;
	const int32 EffectiveSkill = Vitals && Vitals->bValid && Vitals->Stamina <= 0 ? 0 : RunSkill;
	// Retail's special case is EXACTLY 800 (verified against acclient.exe),
	// not a cap for all higher skills. Ordinary skills use the curve below.
	if (EffectiveSkill == 800)
	{
		return 18.f / 4.f;
	}
	const float Skill = static_cast<float>(FMath::Max(0, EffectiveSkill));
	return (LoadMod * (Skill / (Skill + 200.f) * 11.f) + 4.f) / 4.f;
}

float UACEClientSubsystem::GetJumpHeight(float Extent) const
{
	// ACE.Server MovementSystem.GetJumpHeight
	const float Load = GetMovementBurden();
	float LoadMod = 1.f;
	if (Load >= 2.f)
	{
		LoadMod = 0.f;
	}
	else if (Load >= 1.f)
	{
		LoadMod = 2.f - Load;
	}
	const float Power = FMath::Clamp(Extent, 0.f, 1.f);
	const float Skill = static_cast<float>(FMath::Max(0, JumpSkill));
	float Result = LoadMod * (Skill / (Skill + 1300.f) * 22.2f + 0.05f) * Power;
	if (Result < 0.35f)
	{
		Result = 0.35f;
	}
	return Result;
}

float UACEClientSubsystem::GetLocomotionSpeed(bool bRunning) const
{
	// MotionInterp: WalkAnimSpeed=3.12, RunAnimSpeed=4.0; run multiplies by GetRunRate().
	constexpr float WalkAnimSpeed = 3.12f;
	constexpr float RunAnimSpeed = 4.f;
	return bRunning ? (RunAnimSpeed * GetRunRate()) : WalkAnimSpeed;
}

float UACEClientSubsystem::GetSidestepSpeed(bool bRunning) const
{
	// MotionInterp.adjust_motion + get_state_velocity for SideStepRight.
	constexpr float WalkAnimSpeed = 3.12f;
	constexpr float SidestepAnimSpeed = 1.25f;
	constexpr float SidestepFactor = 0.5f;
	constexpr float MaxSidestepAnimRate = 3.0f;
	float Rate = SidestepFactor * (WalkAnimSpeed / SidestepAnimSpeed); // ≈1.248
	if (bRunning)
	{
		Rate *= GetRunRate();
		Rate = FMath::Min(Rate, MaxSidestepAnimRate);
	}
	return SidestepAnimSpeed * Rate;
}
