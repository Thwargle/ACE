#pragma once

#include "CoreMinimal.h"

/** Top-level GameMessage opcodes used by this plugin (ACE / retail client 1802). */
namespace ACEOpcode
{
	constexpr uint32 CharacterLogOff                = 0xF653;
	constexpr uint32 CharacterEnterWorld            = 0xF657;
	constexpr uint32 CharacterList                  = 0xF658;
	constexpr uint32 CharacterError                 = 0xF659;
	constexpr uint32 HearSpeech                     = 0x02BB;
	constexpr uint32 HearRangedSpeech               = 0x02BC;
	/** Soul emote broadcast (senderId, senderName, emoteText) — Mag-nus 0x01E2. */
	constexpr uint32 SoulEmote                      = 0x01E2;
	constexpr uint32 InventoryRemoveObject          = 0x0024;
	constexpr uint32 SetStackSize                   = 0x0197;
	constexpr uint32 PrivateUpdatePropertyInt       = 0x02CD;
	constexpr uint32 PublicUpdatePropertyInt        = 0x02CE;
	constexpr uint32 PrivateUpdatePropertyInt64     = 0x02CF;
	constexpr uint32 PrivateUpdatePropertyDataID    = 0x02D7;
	constexpr uint32 PublicUpdatePropertyDataID     = 0x02D8;
	constexpr uint32 PublicUpdateInstanceId         = 0x02DA;
	constexpr uint32 PrivateUpdateSkill             = 0x02DD;
	constexpr uint32 PrivateUpdateAttribute         = 0x02E3;
	constexpr uint32 PrivateUpdateVital             = 0x02E7;
	constexpr uint32 PublicUpdateVital              = 0x02E8;
	constexpr uint32 PrivateUpdateAttribute2ndLevel = 0x02E9;
	constexpr uint32 ObjectCreate                   = 0xF745;
	constexpr uint32 PlayerCreate                   = 0xF746;
	constexpr uint32 ObjectDelete                    = 0xF747;
	constexpr uint32 UpdatePosition                 = 0xF748;
	constexpr uint32 ParentEvent                    = 0xF749;
	constexpr uint32 PickupEvent                    = 0xF74A;
	constexpr uint32 SetState                       = 0xF74B;
	constexpr uint32 UpdateMotion                   = 0xF74C;
	constexpr uint32 VectorUpdate                   = 0xF74E;
	constexpr uint32 Sound                          = 0xF750;
	constexpr uint32 PlayerTeleport                 = 0xF751;
	constexpr uint32 PlayScriptId                   = 0xF754;
	constexpr uint32 PlayEffect                     = 0xF755;
	constexpr uint32 ObjDescEvent                   = 0xF625;
	constexpr uint32 CharacterEnterWorldRequest     = 0xF7C8;
	constexpr uint32 UpdateObject                   = 0xF7DB; // Hook morph / full object refresh
	constexpr uint32 CharacterEnterWorldServerReady = 0xF7DF;
	constexpr uint32 ServerMessage                  = 0xF7E0;
	constexpr uint32 ServerName                     = 0xF7E1;
	constexpr uint32 AccountBoot                    = 0xF7DC;
	constexpr uint32 DDD_Interrogation              = 0xF7E5;
	constexpr uint32 DDD_InterrogationResponse      = 0xF7E6;
	constexpr uint32 DDD_EndDDD                     = 0xF7EA;
	constexpr uint32 GameEvent                      = 0xF7B0;
	constexpr uint32 GameAction                     = 0xF7B1;
	/** Turbine global chat (General / Trade / LFG / Roleplay / Society / Olthoi). */
	constexpr uint32 TurbineChat                    = 0xF7DE;
}

/** Nested GameEvent types (inside 0xF7B0). */
namespace ACEGameEvent
{
	constexpr uint32 QueryAgeResponse = 0x01C3;
	constexpr uint32 PlayerDescription         = 0x0013;
	constexpr uint32 InventoryPutObjInContainer = 0x0022;
	constexpr uint32 WieldItem                 = 0x0023;
	constexpr uint32 CloseGroundContainer      = 0x0052;
	constexpr uint32 ApproachVendor            = 0x0062;
	constexpr uint32 AllegianceUpdate          = 0x0020;
	constexpr uint32 FriendsListUpdate         = 0x0021;
	/** SquelchDB (accounts hash, characters hash, globals). */
	constexpr uint32 SetSquelchDB              = 0x01F4;
	constexpr uint32 CharacterTitle            = 0x0029;
	constexpr uint32 UpdateTitle               = 0x002B;
	constexpr uint32 FellowshipQuit            = 0x00A3;
	constexpr uint32 FellowshipDismiss         = 0x00A4;
	constexpr uint32 ChannelBroadcast          = 0x0147;
	constexpr uint32 ChannelList               = 0x0148;
	constexpr uint32 ChannelIndex              = 0x0149;
	constexpr uint32 IdentifyObjectResponse    = 0x00C9;
	constexpr uint32 ViewContents              = 0x0196;
	constexpr uint32 InventoryPutObjIn3D       = 0x019A;
	constexpr uint32 VictimNotification        = 0x01AC;
	constexpr uint32 KillerNotification        = 0x01AD;
	constexpr uint32 AttackerNotification      = 0x01B1;
	constexpr uint32 DefenderNotification      = 0x01B2;
	constexpr uint32 EvasionAttackerNotification = 0x01B3;
	constexpr uint32 EvasionDefenderNotification = 0x01B4;
	constexpr uint32 MagicRemoveSpell          = 0x01A8;
	constexpr uint32 InventoryServerSaveFailed = 0x00A0;
	constexpr uint32 UpdateHealth              = 0x01C0;
	constexpr uint32 UseDone                   = 0x01C7;
	constexpr uint32 AttackDone                = 0x01A7;
	constexpr uint32 CombatCommenceAttack      = 0x01B8;
	constexpr uint32 AllegianceUpdateDone      = 0x01C8;
	constexpr uint32 FellowshipFellowUpdateDone = 0x01C9;
	constexpr uint32 FellowshipFellowStatsDone = 0x01CA;
	constexpr uint32 ItemAppraiseDone          = 0x01CB;
	constexpr uint32 RegisterTrade             = 0x01FD;
	constexpr uint32 OpenTrade                 = 0x01FE;
	constexpr uint32 CloseTrade                = 0x01FF;
	constexpr uint32 AddToTrade                = 0x0200;
	constexpr uint32 RemoveFromTrade           = 0x0201;
	constexpr uint32 AcceptTrade               = 0x0202;
	constexpr uint32 DeclineTrade              = 0x0203;
	constexpr uint32 ResetTrade                = 0x0205;
	constexpr uint32 TradeFailure              = 0x0207;
	constexpr uint32 ClearTradeAcceptance      = 0x0208;
	constexpr uint32 AllegianceLoginNotification = 0x027A;
	constexpr uint32 AllegianceInfoResponse    = 0x027C;
	/** Chess (retail CM_Game): join/start/move/opponent/stalemate/over. */
	constexpr uint32 ChessJoinGameResponse     = 0x0281;
	constexpr uint32 ChessStartGame            = 0x0282;
	constexpr uint32 ChessMoveResponse         = 0x0283;
	constexpr uint32 ChessOpponentTurn         = 0x0284;
	constexpr uint32 ChessOpponentStalemate    = 0x0285;
	constexpr uint32 ChessGameOver             = 0x028C;
	constexpr uint32 WeenieError               = 0x028A;
	constexpr uint32 WeenieErrorWithString     = 0x028B;
	constexpr uint32 SalvageOperationsResult   = 0x02B4;
	constexpr uint32 Tell                      = 0x02BD;
	constexpr uint32 FellowshipFullUpdate      = 0x02BE;
	constexpr uint32 FellowshipDisband         = 0x02BF;
	constexpr uint32 FellowshipUpdateFellow    = 0x02C0;
	constexpr uint32 MagicUpdateSpell          = 0x02C1;
	constexpr uint32 MagicUpdateEnchantment    = 0x02C2;
	constexpr uint32 MagicRemoveEnchantment    = 0x02C3;
	constexpr uint32 MagicUpdateMultipleEnchantments = 0x02C4;
	constexpr uint32 MagicRemoveMultipleEnchantments = 0x02C5;
	constexpr uint32 MagicPurgeEnchantments    = 0x02C6;
	constexpr uint32 MagicDispelEnchantment    = 0x02C7;
	constexpr uint32 MagicDispelMultipleEnchantments = 0x02C8;
	constexpr uint32 PingResponse              = 0x01EA;
	constexpr uint32 MagicPurgeBadEnchantments = 0x0312;
	constexpr uint32 CommunicationTransientString = 0x02EB;
	constexpr uint32 SendClientContractTrackerTable = 0x0314;
	constexpr uint32 SendClientContractTracker = 0x0315;
	/** House panel payload for dwelling owners (buy/rent items + location). */
	constexpr uint32 HouseData                 = 0x0225;
	/** Answer to HouseQuery when the character owns nothing (WeenieError). */
	constexpr uint32 HouseStatus               = 0x0226;
	/** Room ids for Turbine chat (General/Trade/LFG/…). */
	constexpr uint32 SetTurbineChatChannels    = 0x0295;
	/** Book TOC / page metadata after using a book. */
	constexpr uint32 BookDataResponse          = 0x00B4;
	/** One book page's author + text. */
	constexpr uint32 BookPageDataResponse      = 0x00B8;
}

/** ChatMessageType values (F7E0 / speech / combat GameEvents). */
namespace ACEChatMessageType
{
	constexpr int32 Broadcast    = 0x00;
	constexpr int32 AllChannels  = 0x01;
	constexpr int32 Speech       = 0x02;
	constexpr int32 Tell         = 0x03;
	constexpr int32 OutgoingTell = 0x04;
	constexpr int32 System       = 0x05;
	constexpr int32 Combat       = 0x06;
	constexpr int32 Magic        = 0x07;
	constexpr int32 Channel      = 0x08; // admin / audit channels
	constexpr int32 ChannelSend  = 0x09;
	constexpr int32 Social       = 0x0A;
	constexpr int32 SocialSend   = 0x0B;
	constexpr int32 Emote        = 0x0C; // creature speech / *emotes* / NPC chatter
	constexpr int32 Advancement  = 0x0D;
	constexpr int32 Abuse        = 0x0E;
	constexpr int32 Help         = 0x0F;
	constexpr int32 Appraisal    = 0x10;
	constexpr int32 Spellcasting = 0x11;
	constexpr int32 Allegiance   = 0x12;
	constexpr int32 Fellowship   = 0x13;
	constexpr int32 WorldBroadcast = 0x14;
	constexpr int32 CombatEnemy  = 0x15;
	constexpr int32 CombatSelf   = 0x16;
	constexpr int32 Recall       = 0x17;
	constexpr int32 Craft        = 0x18;
	constexpr int32 Salvaging    = 0x19;
	constexpr int32 ChatError    = 0x1A; // client-local feedback; filtered from logs by default in retail
	constexpr int32 General      = 0x1B;
	constexpr int32 Trade        = 0x1C;
	constexpr int32 LFG          = 0x1D;
	constexpr int32 Roleplay     = 0x1E;
	constexpr int32 AdminTell    = 0x1F;
	constexpr int32 Society      = 0x20;
	inline bool IsGlobalChannel(int32 Type)
	{
		return (Type >= General && Type <= Roleplay) || Type == Society;
	}
	/**
	 * Client-internal (not a retail LogTextType): WeenieError / CommunicationTransientString
	 * messages — retail shows these as a yellow center-top banner that fades, not chat text.
	 */
	constexpr int32 TransientInfo = 0x30;
}

/** Retail ChatChannel destination ids (GameAction ChatChannel 0x0147). */
namespace ACEChatChannel
{
	constexpr uint32 Fellow               = 0x00000800;
	constexpr uint32 Vassals              = 0x00001000;
	constexpr uint32 Patron               = 0x00002000;
	constexpr uint32 Monarch              = 0x00004000;
	constexpr uint32 CoVassals            = 0x01000000;
	constexpr uint32 AllegianceBroadcast  = 0x02000000;
}

/** TurbineChatChannel ids + ChatType (ACE.Server Entity / ACE.Entity.Enum.ChatType). */
namespace ACETurbineChat
{
	constexpr uint32 Allegiance           = 1;
	constexpr uint32 General              = 2;
	constexpr uint32 Trade                = 3;
	constexpr uint32 LFG                  = 4;
	constexpr uint32 Roleplay             = 5;
	constexpr uint32 Society              = 6;
	constexpr uint32 SocietyCelestialHand = 7;
	constexpr uint32 SocietyEldrytchWeb   = 8;
	constexpr uint32 SocietyRadiantBlood  = 9;
	constexpr uint32 Olthoi               = 10;

	constexpr uint32 BlobEventBinary    = 1;
	constexpr uint32 BlobRequestBinary  = 3;
	constexpr uint32 BlobResponseBinary = 5;
	constexpr uint32 DispatchByName     = 1;
	constexpr uint32 DispatchById       = 2;
}

/** Nested GameAction opcodes (inside 0xF7B1). */
namespace ACEGameAction
{
	constexpr uint32 QueryAge = 0x01C2;
	constexpr uint32 QueryBirth = 0x01C4;
	constexpr uint32 EnterPkLite = 0x028F;
	constexpr uint32 LoginComplete         = 0x00A1;
	constexpr uint32 SetAfkMode            = 0x000F;
	constexpr uint32 SetAfkMessage         = 0x0010;
	constexpr uint32 Talk                  = 0x0015;
	constexpr uint32 RemoveAllFriends      = 0x0025;
	constexpr uint32 TalkDirect            = 0x0032;
	constexpr uint32 Tell                  = 0x005D;
	constexpr uint32 TeleToLifestone       = 0x0063;
	constexpr uint32 RemoveFriend          = 0x0017;
	constexpr uint32 AddFriend             = 0x0018;
	constexpr uint32 SwearAllegiance       = 0x001D;
	constexpr uint32 BreakAllegiance       = 0x001E;
	constexpr uint32 AllegianceUpdateRequest = 0x001F;
	constexpr uint32 ChatChannel           = 0x0147;
	constexpr uint32 AddChannel            = 0x0145;
	constexpr uint32 RemoveChannel         = 0x0146;
	constexpr uint32 ListChannels          = 0x0148;
	constexpr uint32 IndexChannels         = 0x0149;
	constexpr uint32 FellowshipCreate      = 0x00A2;
	constexpr uint32 FellowshipQuit        = 0x00A3;
	constexpr uint32 FellowshipDismiss     = 0x00A4;
	constexpr uint32 FellowshipRecruit     = 0x00A5;
	constexpr uint32 FellowshipUpdateRequest = 0x00A6;
	constexpr uint32 TargetedMeleeAttack   = 0x0008;
	constexpr uint32 TargetedMissileAttack = 0x000A;
	constexpr uint32 CancelAttack          = 0x01B7;
	constexpr uint32 CommenceAttack        = 0x01B8;
	constexpr uint32 PutItemInContainer    = 0x0019;
	constexpr uint32 GetAndWieldItem       = 0x001A;
	constexpr uint32 DropItem              = 0x001B;
	/** Merge stackable inventory items (fromGuid, toGuid, amount). */
	constexpr uint32 StackableMerge        = 0x0054;
	/** Split stack into a new stack in a container (stackId, containerId, place, amount). */
	constexpr uint32 StackableSplitToContainer = 0x0055;
	/** Split stack onto the ground (stackId, amount). */
	constexpr uint32 StackableSplitTo3D    = 0x0056;
	/** Split stack directly onto a wield location (stackId, EquipMask, amount). */
	constexpr uint32 StackableSplitToWield = 0x019B;
	/** Give item to player/NPC (collectors, quest turn-ins). Payload: target, item, amount. */
	constexpr uint32 GiveObjectRequest     = 0x00CD;
	constexpr uint32 IdentifyObject        = 0x00C8;
	constexpr uint32 SetInscription        = 0x00BF;
	constexpr uint32 ChangeCombatMode      = 0x0053;
	constexpr uint32 Buy                   = 0x005F;
	constexpr uint32 Sell                  = 0x0060;
	constexpr uint32 RaiseVital            = 0x0044;
	constexpr uint32 RaiseAttribute        = 0x0045;
	constexpr uint32 RaiseSkill            = 0x0046;
	constexpr uint32 TrainSkill            = 0x0047;
	constexpr uint32 CastUntargetedSpell   = 0x0048;
	constexpr uint32 CastTargetedSpell     = 0x004A;
	constexpr uint32 UseWithTarget         = 0x0035;
	constexpr uint32 Use                   = 0x0036;
	constexpr uint32 TitleSet              = 0x002C;
	/** Per-checkbox character option (PlayerOption index, value). */
	constexpr uint32 SetSingleCharacterOption = 0x0005;
	/** Full character options push (flags, options1, tab1 spell count, options2). */
	constexpr uint32 SetCharacterOptions   = 0x01A1;
	/** Squelch add/remove: ModifyCharacterSquelch / ModifyAccountSquelch / ModifyGlobalSquelch. */
	constexpr uint32 ModifyCharacterSquelch = 0x0058;
	constexpr uint32 ModifyAccountSquelch  = 0x0059;
	constexpr uint32 ModifyGlobalSquelch   = 0x005B;
	/** Abandon a quest contract (contract id). */
	constexpr uint32 AbandonContract        = 0x0316;
	constexpr uint32 NoLongerViewingContents = 0x0195;
	constexpr uint32 Emote                 = 0x01DF;
	constexpr uint32 SoulEmote             = 0x01E1;
	constexpr uint32 PingRequest           = 0x01E9;
	constexpr uint32 AddToSpellBar         = 0x01E3;
	constexpr uint32 RemoveFromSpellBar    = 0x01E4;
	/** Fill component book entry (component wcid, quantity to rebuy). */
	constexpr uint32 SetDesiredComponentLevel = 0x0224;
	constexpr uint32 AddShortCut           = 0x019C;
	constexpr uint32 RemoveShortCut        = 0x019D;
	constexpr uint32 QueryHealth           = 0x01BF;
	constexpr uint32 OpenTradeNegotiations = 0x01F6;
	constexpr uint32 CloseTradeNegotiations = 0x01F7;
	constexpr uint32 AddToTrade            = 0x01F8;
	constexpr uint32 AcceptTrade           = 0x01FA;
	constexpr uint32 DeclineTrade          = 0x01FB;
	constexpr uint32 ResetTrade            = 0x0204;
	constexpr uint32 CreateTinkeringTool   = 0x027D;
	constexpr uint32 FellowshipAssignNewLeader = 0x0290;
	constexpr uint32 FellowshipChangeOpenness = 0x0291;
	/** Ask the server for the housing panel payload (HouseData / HouseStatus). */
	constexpr uint32 BuyHouse              = 0x021C;
	constexpr uint32 HouseQuery            = 0x021E;
	constexpr uint32 AbandonHouse          = 0x021F;
	constexpr uint32 AddPermanentGuest    = 0x0245;
	constexpr uint32 RemovePermanentGuest = 0x0246;
	constexpr uint32 SetOpenHouseStatus   = 0x0247;
	constexpr uint32 ChangeStoragePermission = 0x0249;
	constexpr uint32 BootSpecificHouseGuest = 0x024A;
	constexpr uint32 RemoveAllStoragePermission = 0x024C;
	constexpr uint32 RequestFullGuestList = 0x024D;
	constexpr uint32 AddAllStoragePermission = 0x025C;
	constexpr uint32 RemoveAllPermanentGuests = 0x025E;
	constexpr uint32 BootEveryone         = 0x025F;
	constexpr uint32 SetHooksVisibility   = 0x0266;
	constexpr uint32 ModifyAllegianceGuestPermission = 0x0267;
	constexpr uint32 ModifyAllegianceStoragePermission = 0x0268;
	constexpr uint32 ListAvailableHouses  = 0x0270;
	constexpr uint32 Suicide              = 0x0279;
	/** Report abuse (character name, status mask, complaint text). */
	constexpr uint32 AbuseLogRequest       = 0x0140;
	/** Open / page a book (BookData 0x00AA, BookPageData 0x00AE). */
	constexpr uint32 BookData              = 0x00AA;
	constexpr uint32 BookPageData          = 0x00AE;
	/** Chess: join / resign / move / pass / offer stalemate. */
	constexpr uint32 ChessJoin             = 0x0269;
	constexpr uint32 ChessQuit             = 0x026A;
	constexpr uint32 ChessMove             = 0x026B;
	constexpr uint32 ChessMovePass         = 0x026D;
	constexpr uint32 ChessStalemate        = 0x026E;
	constexpr uint32 TeleToHouse           = 0x0262;
	constexpr uint32 TeleToMansion         = 0x0278;
	constexpr uint32 TeleToMarketPlace     = 0x028D;
	constexpr uint32 RecallAllegianceHometown = 0x02AB;
	constexpr uint32 Jump                  = 0xF61B;
	constexpr uint32 MoveToState           = 0xF61C;
	constexpr uint32 AutonomousPosition    = 0xF753;
}

/** AttackHeight for TargetedMelee/MissileAttack. */
namespace ACEAttackHeight
{
	constexpr uint32 High   = 1;
	constexpr uint32 Medium = 2;
	constexpr uint32 Low    = 3;
}

/** CombatMode values for GameAction ChangeCombatMode (0x0053). */
namespace ACECombatMode
{
	constexpr uint32 NonCombat = 0x01;
	constexpr uint32 Melee     = 0x02;
	constexpr uint32 Missile   = 0x04;
	constexpr uint32 Magic     = 0x08;
}

/** Message queues used as fragment Header.Queue. */
namespace ACEQueue
{
	constexpr uint16 InvalidQueue       = 0x00;
	constexpr uint16 EventQueue         = 0x01;
	constexpr uint16 ControlQueue       = 0x02;
	constexpr uint16 WeenieQueue        = 0x03;
	constexpr uint16 LoginQueue         = 0x04;
	constexpr uint16 DatabaseQueue      = 0x05;
	constexpr uint16 SecureControlQueue = 0x06;
	constexpr uint16 SecureWeenieQueue  = 0x07;
	constexpr uint16 SecureLoginQueue   = 0x08;
	constexpr uint16 UIQueue            = 0x09;
	constexpr uint16 SmartboxQueue      = 0x0A;
	constexpr uint16 ObserverQueue      = 0x0B;
}

/** Motion / hold-key constants used for locomotion. */
namespace ACEMotion
{
	constexpr uint32 HoldKeyNone        = 0x1;
	constexpr uint32 HoldKeyRun         = 0x2;
	constexpr uint32 StanceHandCombat   = 0x8000003C;
	constexpr uint32 StanceNonCombat    = 0x8000003D;
	constexpr uint32 StanceSwordCombat  = 0x8000003E;
	constexpr uint32 StanceBowCombat    = 0x8000003F;
	constexpr uint32 StanceSwordShield  = 0x80000040;
	constexpr uint32 StanceCrossbow     = 0x80000041;
	constexpr uint32 StanceSling        = 0x80000043;
	constexpr uint32 StanceTwoHandedSword = 0x80000044;
	constexpr uint32 StanceTwoHandedStaff = 0x80000045;
	constexpr uint32 StanceDualWield    = 0x80000046;
	constexpr uint32 StanceThrownWeapon = 0x80000047;
	constexpr uint32 StanceMagic        = 0x80000049;
	constexpr uint32 StanceAtlatl       = 0x8000013B;
	constexpr uint32 StanceThrownShield = 0x8000013C;
	constexpr uint32 Ready              = 0x41000003;
	constexpr uint32 WalkForward        = 0x45000005;
	constexpr uint32 WalkBackwards      = 0x45000006;
	constexpr uint32 RunForward         = 0x44000007;
	constexpr uint32 On                 = 0x4000000B; // door open
	constexpr uint32 Off                = 0x4000000C; // door closed
	constexpr uint32 Dead               = 0x40000011; // die / corpse pose
	constexpr uint32 Crouch             = 0x41000012;
	constexpr uint32 Sitting            = 0x41000013;
	constexpr uint32 Sleeping           = 0x41000014;
	constexpr uint32 TurnRight          = 0x6500000D;
	constexpr uint32 SideStepRight      = 0x6500000F;
	constexpr uint32 SideStepLeft       = 0x65000010;
	constexpr uint16 OnCommandU16       = 0x000B;
	constexpr uint16 OffCommandU16      = 0x000C;
	constexpr uint16 DeadCommandU16     = 0x0011;
	constexpr uint16 CrouchCommandU16   = 0x0012;
	constexpr uint16 SittingCommandU16  = 0x0013;
	constexpr uint16 SleepingCommandU16 = 0x0014;

	/**
	 * Expand a packed MotionCommand / MotionStance ushort from the wire
	 * (MotionItem / InterpretedState) back to the full 32-bit MotionCommand.
	 * Mirrors ACE.Server PackedCommandExtensions.RawToInterpreted — full enum table
	 * so Sanctuary/emotes/recalls do not collapse to the 0x4000… heuristic.
	 */
	inline uint32 ExpandPackedCommand(uint16 Raw)
	{
		static constexpr uint32 Known[] = {
			0x1000004au,
			0x1000004bu,
			0x1000004du,
			0x1000004eu,
			0x1000004fu,
			0x10000050u,
			0x10000051u,
			0x10000052u,
			0x10000053u,
			0x10000054u,
			0x10000055u,
			0x10000056u,
			0x10000057u,
			0x10000058u,
			0x10000059u,
			0x1000005au,
			0x1000005bu,
			0x1000005cu,
			0x1000005du,
			0x1000005eu,
			0x1000005fu,
			0x10000060u,
			0x10000061u,
			0x10000062u,
			0x10000063u,
			0x10000064u,
			0x10000065u,
			0x10000066u,
			0x10000067u,
			0x10000068u,
			0x10000069u,
			0x1000006au,
			0x1000006bu,
			0x1000006cu,
			0x1000006du,
			0x1000006eu,
			0x1000006fu,
			0x10000070u,
			0x10000071u,
			0x10000072u,
			0x10000073u,
			0x10000074u,
			0x10000075u,
			0x10000076u,
			0x10000077u,
			0x10000078u,
			0x1000009cu,
			0x1000009du,
			0x1000009eu,
			0x1000009fu,
			0x100000a0u,
			0x100000a1u,
			0x100000cdu,
			0x100000ceu,
			0x100000cfu,
			0x100000d0u,
			0x100000d1u,
			0x100000d2u,
			0x100000e2u,
			0x100000e3u,
			0x1000010eu,
			0x1000010fu,
			0x10000110u,
			0x10000111u,
			0x1000011eu,
			0x1000011fu,
			0x10000120u,
			0x10000121u,
			0x10000122u,
			0x10000123u,
			0x10000124u,
			0x10000125u,
			0x10000126u,
			0x10000127u,
			0x10000128u,
			0x10000129u,
			0x1000012au,
			0x1000012bu,
			0x1000012cu,
			0x1000012du,
			0x1000012eu,
			0x1000012fu,
			0x10000130u,
			0x10000131u,
			0x10000132u,
			0x10000133u,
			0x10000134u,
			0x1000013au,
			0x10000153u,
			0x10000162u,
			0x10000165u,
			0x10000166u,
			0x10000167u,
			0x10000171u,
			0x10000172u,
			0x10000173u,
			0x10000174u,
			0x10000175u,
			0x10000176u,
			0x10000177u,
			0x10000178u,
			0x10000179u,
			0x1000017au,
			0x1000017bu,
			0x1000017cu,
			0x1000017du,
			0x1000017eu,
			0x1000017fu,
			0x10000180u,
			0x10000181u,
			0x10000182u,
			0x10000183u,
			0x10000184u,
			0x10000185u,
			0x10000186u,
			0x10000187u,
			0x10000188u,
			0x10000189u,
			0x1000018au,
			0x1000018bu,
			0x1000018cu,
			0x1000018du,
			0x1000018eu,
			0x1000018fu,
			0x10000190u,
			0x10000191u,
			0x10000192u,
			0x10000193u,
			0x10000194u,
			0x10000195u,
			0x10000196u,
			0x10000197u,
			0x10000198u,
			0x10000199u,
			0x1000019au,
			0x1000019bu,
			0x1200009bu,
			0x120000d4u,
			0x120000dfu,
			0x1300004cu,
			0x13000079u,
			0x1300007au,
			0x1300007bu,
			0x1300007cu,
			0x1300007du,
			0x1300007eu,
			0x1300007fu,
			0x13000080u,
			0x13000081u,
			0x13000082u,
			0x13000083u,
			0x13000084u,
			0x13000085u,
			0x13000086u,
			0x13000087u,
			0x13000088u,
			0x13000089u,
			0x1300008au,
			0x1300008bu,
			0x1300008cu,
			0x1300008du,
			0x1300008eu,
			0x1300008fu,
			0x13000090u,
			0x13000091u,
			0x13000092u,
			0x13000093u,
			0x13000094u,
			0x13000095u,
			0x13000096u,
			0x13000097u,
			0x13000098u,
			0x13000099u,
			0x1300009au,
			0x130000cau,
			0x130000cbu,
			0x130000ccu,
			0x13000119u,
			0x13000135u,
			0x1300014au,
			0x1300014bu,
			0x1300014cu,
			0x1300014du,
			0x1300014eu,
			0x1300014fu,
			0x13000150u,
			0x13000151u,
			0x13000152u,
			0x2000003au,
			0x2500003bu,
			0x40000004u,
			0x40000008u,
			0x40000009u,
			0x4000000au,
			0x4000000bu,
			0x4000000cu,
			0x40000011u,
			0x40000015u,
			0x40000016u,
			0x40000017u,
			0x40000018u,
			0x40000019u,
			0x4000001au,
			0x4000001bu,
			0x4000001cu,
			0x4000001du,
			0x4000001eu,
			0x4000001fu,
			0x40000020u,
			0x40000021u,
			0x40000022u,
			0x40000023u,
			0x40000024u,
			0x40000025u,
			0x40000026u,
			0x40000027u,
			0x40000028u,
			0x40000029u,
			0x4000002au,
			0x4000002bu,
			0x4000002cu,
			0x4000002du,
			0x4000002eu,
			0x4000002fu,
			0x40000030u,
			0x40000031u,
			0x40000032u,
			0x40000033u,
			0x40000034u,
			0x40000035u,
			0x40000036u,
			0x40000037u,
			0x40000038u,
			0x40000039u,
			0x400000d3u,
			0x400000e0u,
			0x400000e1u,
			0x400000e4u,
			0x400000e5u,
			0x400000e6u,
			0x40000136u,
			0x40000137u,
			0x40000138u,
			0x40000139u,
			0x41000003u,
			0x41000012u,
			0x41000013u,
			0x41000014u,
			0x420000f9u,
			0x430000eau,
			0x430000ebu,
			0x430000ecu,
			0x430000edu,
			0x430000eeu,
			0x430000efu,
			0x430000f0u,
			0x430000f1u,
			0x430000f2u,
			0x430000f3u,
			0x430000f4u,
			0x430000f5u,
			0x430000f6u,
			0x430000f7u,
			0x430000f8u,
			0x430000fau,
			0x430000fbu,
			0x430000fcu,
			0x430000fdu,
			0x43000118u,
			0x4300011au,
			0x4300011bu,
			0x4300011cu,
			0x4300013du,
			0x4300013eu,
			0x4300013fu,
			0x43000140u,
			0x43000141u,
			0x43000142u,
			0x43000143u,
			0x43000144u,
			0x43000145u,
			0x43000146u,
			0x43000147u,
			0x43000148u,
			0x43000149u,
			0x44000007u,
			0x45000005u,
			0x45000006u,
			0x6500000du,
			0x6500000eu,
			0x6500000fu,
			0x65000010u,
			0x8000003cu,
			0x8000003du,
			0x8000003eu,
			0x8000003fu,
			0x80000040u,
			0x80000041u,
			0x80000042u,
			0x80000043u,
			0x80000044u,
			0x80000045u,
			0x80000046u,
			0x80000047u,
			0x80000048u,
			0x80000049u,
			0x800000e8u,
			0x800000e9u,
			0x8000013bu,
			0x8000013cu,
			0x80000a2u,
			0x80000a9u,
			0x80000b5u,
			0x80000b6u,
			0x80000b7u,
			0x85000001u,
			0x85000002u,
			0x90000a3u,
			0x90000a4u,
			0x90000a5u,
			0x90000a6u,
			0x90000a7u,
			0x90000a8u,
			0x90000aau,
			0x90000abu,
			0x90000acu,
			0x90000adu,
			0x90000aeu,
			0x90000afu,
			0x90000b0u,
			0x90000b1u,
			0x90000b8u,
			0x90000b9u,
			0x90000c0u,
			0x90000c2u,
			0x90000c3u,
			0x90000c4u,
			0x90000c6u,
			0x90000c7u,
			0x90000c8u,
			0x90000c9u,
			0x90000d5u,
			0x90000d6u,
			0x90000d7u,
			0x90000d8u,
			0x90000d9u,
			0x90000dau,
			0x90000dbu,
			0x90000dcu,
			0x90000ddu,
			0x90000deu,
			0x90000e7u,
			0x90000feu,
			0x90000ffu,
			0x9000100u,
			0x9000101u,
			0x9000102u,
			0x9000103u,
			0x9000104u,
			0x9000105u,
			0x9000106u,
			0x9000107u,
			0x9000108u,
			0x9000109u,
			0x900010au,
			0x900010bu,
			0x900010cu,
			0x900010du,
			0x900010fu,
			0x9000110u,
			0x9000111u,
			0x9000112u,
			0x9000113u,
			0x9000114u,
			0x900011du,
			0x9000154u,
			0x9000155u,
			0x9000156u,
			0x9000157u,
			0x9000158u,
			0x9000159u,
			0x900015au,
			0x900015bu,
			0x900015cu,
			0x900015du,
			0x900015eu,
			0x900015fu,
			0x9000160u,
			0x9000161u,
			0x9000162u,
			0x9000163u,
			0x9000164u,
			0x9000168u,
			0x9000169u,
			0x900016au,
			0x900016bu,
			0x900016cu,
			0x900016du,
			0x900016eu,
			0x900016fu,
			0x9000170u,
			0xc0000c1u,
			0xd0000b2u,
			0xd0000b3u,
			0xd0000b4u,
			0xd0000bau,
			0xd0000bbu,
			0xd0000bcu,
			0xd0000bdu,
			0xd0000beu,
			0xd0000bfu,
			0xd0000c5u,
		};
		for (uint32 Full : Known)
		{
			if (static_cast<uint16>(Full) == Raw)
			{
				return Full;
			}
		}
		// Last-resort family guess when a future DAT adds an unknown packed id.
		if (Raw >= 0x3Cu && Raw <= 0x50u)
		{
			return 0x80000000u | Raw; // stance
		}
		if ((Raw >= 0x4Au && Raw <= 0x78u) || (Raw >= 0x9Cu && Raw <= 0xA1u)
			|| (Raw >= 0xCDu && Raw <= 0xD2u) || (Raw >= 0x11Fu && Raw <= 0x134u)
			|| Raw == 0x153u || (Raw >= 0x165u && Raw <= 0x167u)
			|| (Raw >= 0x171u && Raw <= 0x188u))
		{
			return 0x10000000u | Raw;
		}
		if ((Raw >= 0x79u && Raw <= 0x9Au) || (Raw >= 0xCAu && Raw <= 0xCCu)
			|| (Raw >= 0x14Au && Raw <= 0x152u))
		{
			return 0x13000000u | Raw;
		}
		return 0x40000000u | Raw;
	}

	/** Expand a packed or full command to 32-bit. */
	inline uint32 NormalizeCommand(uint32 Cmd)
	{
		if (Cmd != 0 && Cmd <= 0xFFFFu)
		{
			return ExpandPackedCommand(static_cast<uint16>(Cmd));
		}
		return Cmd;
	}

	/**
	 * Idle poses that retail holds as Interpreted ForwardCommand (golem/skeleton Sleeping,
	 * Sitting, Crouch, Dead, chat-pose 0x4300/0x4200/0x1300). Snap to the final frame —
	 * do not play Ready→pose then fall through to locomotion.
	 */
	inline bool IsHeldRestCommand(uint32 Cmd)
	{
		Cmd = NormalizeCommand(Cmd);
		if (Cmd == 0)
		{
			return false;
		}
		const uint32 Fam = Cmd & 0xFF000000u;
		if (Fam == 0x13000000u || Fam == 0x42000000u || Fam == 0x43000000u)
		{
			return true;
		}
		const uint16 Low = static_cast<uint16>(Cmd);
		return Low == DeadCommandU16 || Low == CrouchCommandU16
			|| Low == SittingCommandU16 || Low == SleepingCommandU16;
	}
}

namespace ACERawMotionFlags
{
	constexpr uint32 CurrentHoldKey  = 0x1;
	constexpr uint32 CurrentStyle    = 0x2;
	constexpr uint32 ForwardCommand  = 0x4;
	constexpr uint32 ForwardHoldKey  = 0x8;
	constexpr uint32 ForwardSpeed    = 0x10;
	constexpr uint32 SideStepCommand = 0x20;
	constexpr uint32 SideStepHoldKey = 0x40;
	constexpr uint32 SideStepSpeed   = 0x80;
	constexpr uint32 TurnCommand     = 0x100;
	constexpr uint32 TurnHoldKey     = 0x200;
	constexpr uint32 TurnSpeed       = 0x400;
}

enum class EACEPacketHeaderFlags : uint32
{
	None              = 0x00000000,
	Retransmission    = 0x00000001,
	EncryptedChecksum = 0x00000002,
	BlobFragments     = 0x00000004,
	RequestRetransmit = 0x00001000,
	RejectRetransmit  = 0x00002000,
	AckSequence       = 0x00004000,
	Disconnect        = 0x00008000,
	LoginRequest      = 0x00010000,
	WorldLoginRequest = 0x00020000,
	ConnectRequest    = 0x00040000,
	ConnectResponse   = 0x00080000,
	NetError          = 0x00100000,
	TimeSync          = 0x01000000,
	EchoRequest       = 0x02000000,
	EchoResponse      = 0x04000000,
	Flow              = 0x08000000
};
ENUM_CLASS_FLAGS(EACEPacketHeaderFlags)
