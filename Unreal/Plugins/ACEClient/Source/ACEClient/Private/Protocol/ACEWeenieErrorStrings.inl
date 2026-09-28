// Shared 028A/028B failure catalog. Sources: ACE.Entity WeenieError /
// WeenieErrorWithString (last-client-tested summaries) and retail
// ClientCommunicationSystem::HandleFailureEvent (including chat destinations).
// %s substitutes the one wire argument everywhere; it is never a printf format.
// Empty text is an intentionally silent status. nullptr denotes an internal
// failure with no retail player-facing text: use the generic failure and log its code.
// Supplemental readable messages cover named failures handled by other retail UI.
// Keep every server enum entry here; Tests/check_weenie_error_catalog.py enforces coverage.
static const FWeenieErrorEntry WeenieErrorEntries[] =
{
	{0x0000, TEXT(""), ACEChatMessageType::System}, // None
	{0x0001, nullptr, ACEChatMessageType::ChatError}, // NoMem
	{0x0002, nullptr, ACEChatMessageType::ChatError}, // BadParam
	{0x0003, nullptr, ACEChatMessageType::ChatError}, // DivZero
	{0x0004, nullptr, ACEChatMessageType::ChatError}, // SegV
	{0x0005, nullptr, ACEChatMessageType::ChatError}, // Unimplemented
	{0x0006, nullptr, ACEChatMessageType::ChatError}, // UnknownMessageType
	{0x0007, nullptr, ACEChatMessageType::ChatError}, // NoAnimationTable
	{0x0008, nullptr, ACEChatMessageType::ChatError}, // NoPhysicsObject
	{0x0009, nullptr, ACEChatMessageType::ChatError}, // NoBookieObject
	{0x000A, nullptr, ACEChatMessageType::ChatError}, // NoWslObject
	{0x000B, nullptr, ACEChatMessageType::ChatError}, // NoMotionInterpreter
	{0x000C, nullptr, ACEChatMessageType::ChatError}, // UnhandledSwitch
	{0x000D, nullptr, ACEChatMessageType::ChatError}, // DefaultConstructorCalled
	{0x000E, nullptr, ACEChatMessageType::ChatError}, // InvalidCombatManeuver
	{0x000F, nullptr, ACEChatMessageType::ChatError}, // BadCast
	{0x0010, nullptr, ACEChatMessageType::ChatError}, // MissingQuality
	{0x0012, nullptr, ACEChatMessageType::ChatError}, // MissingDatabaseObject
	{0x0013, nullptr, ACEChatMessageType::ChatError}, // NoCallbackSet
	{0x0014, nullptr, ACEChatMessageType::ChatError}, // CorruptQuality
	{0x0015, nullptr, ACEChatMessageType::ChatError}, // BadContext
	{0x0016, nullptr, ACEChatMessageType::ChatError}, // NoEphseqManager
	{0x0017, TEXT("You failed to go to non-combat mode."), ACEChatMessageType::ChatError}, // BadMovementEvent
	{0x0018, nullptr, ACEChatMessageType::ChatError}, // CannotCreateNewObject
	{0x0019, nullptr, ACEChatMessageType::ChatError}, // NoControllerObject
	{0x001A, nullptr, ACEChatMessageType::ChatError}, // CannotSendEvent
	{0x001B, nullptr, ACEChatMessageType::ChatError}, // PhysicsCantTransition
	{0x001C, nullptr, ACEChatMessageType::ChatError}, // PhysicsMaxDistanceExceeded
	{0x001D, TEXT("You're too busy!"), ACEChatMessageType::ChatError}, // YoureTooBusy
	{0x001E, TEXT("%s is too busy to accept gifts right now."), ACEChatMessageType::System}, // _IsTooBusyToAcceptGifts
	{0x001F, nullptr, ACEChatMessageType::ChatError}, // CannotSendMessage
	{0x0020, TEXT("You must control both objects!"), ACEChatMessageType::ChatError}, // IllegalInventoryTransaction
	{0x0021, nullptr, ACEChatMessageType::ChatError}, // ExternalWeenieObject
	{0x0022, nullptr, ACEChatMessageType::ChatError}, // InternalWeenieObject
	{0x0023, TEXT("Unable to move to object!"), ACEChatMessageType::ChatError}, // MotionFailure
	{0x0024, TEXT("You can't jump while in the air"), ACEChatMessageType::ChatError}, // YouCantJumpWhileInTheAir
	{0x0025, nullptr, ACEChatMessageType::ChatError}, // InqCylSphereFailure
	{0x0026, TEXT("That is not a valid command."), ACEChatMessageType::ChatError}, // ThatIsNotAValidCommand
	{0x0027, nullptr, ACEChatMessageType::ChatError}, // CarryingItem
	{0x0028, TEXT("The item is under someone else's control!"), ACEChatMessageType::ChatError}, // Frozen
	{0x0029, TEXT("You cannot pick that up!"), ACEChatMessageType::ChatError}, // Stuck
	{0x002A, TEXT("You are too encumbered to carry that!"), ACEChatMessageType::ChatError}, // YouAreTooEncumbered
	{0x002B, TEXT("%s cannot carry anymore."), ACEChatMessageType::System}, // _CannotCarryAnymore
	{0x002C, nullptr, ACEChatMessageType::ChatError}, // BadContain
	{0x002D, nullptr, ACEChatMessageType::ChatError}, // BadParent
	{0x002E, nullptr, ACEChatMessageType::ChatError}, // BadDrop
	{0x002F, nullptr, ACEChatMessageType::ChatError}, // BadRelease
	{0x0030, nullptr, ACEChatMessageType::ChatError}, // MsgBadMsg
	{0x0031, nullptr, ACEChatMessageType::ChatError}, // MsgUnpackFailed
	{0x0032, nullptr, ACEChatMessageType::ChatError}, // MsgNoMsg
	{0x0033, nullptr, ACEChatMessageType::ChatError}, // MsgUnderflow
	{0x0034, nullptr, ACEChatMessageType::ChatError}, // MsgOverflow
	{0x0035, nullptr, ACEChatMessageType::ChatError}, // MsgCallbackFailed
	{0x0036, TEXT("Action cancelled!"), ACEChatMessageType::ChatError}, // ActionCancelled
	{0x0037, TEXT("Unable to move to object!"), ACEChatMessageType::ChatError}, // ObjectGone
	{0x0038, TEXT("Unable to move to object!"), ACEChatMessageType::ChatError}, // NoObject
	{0x0039, TEXT("Unable to move to object!"), ACEChatMessageType::ChatError}, // CantGetThere
	{0x003A, TEXT("You can't do that... you're dead!"), ACEChatMessageType::ChatError}, // Dead
	{0x003B, TEXT(""), ACEChatMessageType::ChatError}, // ILeftTheWorld
	{0x003C, TEXT(""), ACEChatMessageType::ChatError}, // ITeleported
	{0x003D, TEXT("You charged too far!"), ACEChatMessageType::ChatError}, // YouChargedTooFar
	{0x003E, TEXT("You are too tired to do that!"), ACEChatMessageType::ChatError}, // YouAreTooTiredToDoThat
	{0x003F, TEXT("You can't crouch while in combat mode."), ACEChatMessageType::ChatError}, // CantCrouchInCombat
	{0x0040, TEXT("You can't sit while in combat mode."), ACEChatMessageType::ChatError}, // CantSitInCombat
	{0x0041, TEXT("You can't lie down while in combat mode."), ACEChatMessageType::ChatError}, // CantLieDownInCombat
	{0x0042, TEXT("You can't perform that emote while in combat mode."), ACEChatMessageType::ChatError}, // CantChatEmoteInCombat
	{0x0043, nullptr, ACEChatMessageType::ChatError}, // NoMtableData
	{0x0044, TEXT("You must be standing to perform that emote."), ACEChatMessageType::ChatError}, // CantChatEmoteNotStanding
	{0x0045, TEXT("You have too many actions pending."), ACEChatMessageType::ChatError}, // TooManyActions
	{0x0046, nullptr, ACEChatMessageType::ChatError}, // Hidden
	{0x0047, TEXT("Unable to move to that position."), ACEChatMessageType::ChatError}, // GeneralMovementFailure
	{0x0048, TEXT("You can't jump from this position"), ACEChatMessageType::ChatError}, // YouCantJumpFromThisPosition
	{0x0049, TEXT("You're too loaded down to jump"), ACEChatMessageType::ChatError}, // CantJumpLoadedDown
	{0x004A, TEXT("Ack! You killed yourself!"), ACEChatMessageType::System}, // YouKilledYourself
	{0x004B, nullptr, ACEChatMessageType::ChatError}, // MsgResponseFailure
	{0x004C, TEXT("That object cannot be moved."), ACEChatMessageType::ChatError}, // ObjectIsStatic
	{0x004D, TEXT("Invalid PK status!"), ACEChatMessageType::ChatError}, // InvalidPkStatus
	{0x004E, TEXT("You fail to affect %s because you cannot affect anyone!"), ACEChatMessageType::Magic}, // YouFailToAffect_YouCannotAffectAnyone
	{0x004F, TEXT("You fail to affect %s because %s cannot be harmed!"), ACEChatMessageType::Magic}, // YouFailToAffect_TheyCannotBeHarmed
	{0x0050, TEXT("You fail to affect %s because beneficial spells do not affect %s!"), ACEChatMessageType::Magic}, // YouFailToAffect_WithBeneficialSpells
	{0x0051, TEXT("You fail to affect %s because you are not a player killer!"), ACEChatMessageType::Magic}, // YouFailToAffect_YouAreNotPK
	{0x0052, TEXT("You fail to affect %s because %s is not a player killer!"), ACEChatMessageType::Magic}, // YouFailToAffect_TheyAreNotPK
	{0x0053, TEXT("You fail to affect %s because you are not the same sort of player killer as %s!"), ACEChatMessageType::Magic}, // YouFailToAffect_NotSamePKType
	{0x0054, TEXT("You fail to affect %s because you are acting across a house boundary!"), ACEChatMessageType::Magic}, // YouFailToAffect_AcrossHouseBoundary
	{0x03E9, nullptr, ACEChatMessageType::ChatError}, // InvalidXpAmount
	{0x03EA, nullptr, ACEChatMessageType::ChatError}, // InvalidPpCalculation
	{0x03EB, nullptr, ACEChatMessageType::ChatError}, // InvalidCpCalculation
	{0x03EC, nullptr, ACEChatMessageType::ChatError}, // UnhandledStatAnswer
	{0x03ED, nullptr, ACEChatMessageType::ChatError}, // HeartAttack
	{0x03EE, TEXT("The container is closed!"), ACEChatMessageType::ChatError}, // TheContainerIsClosed
	{0x03EF, TEXT("%s is not accepting gifts right now."), ACEChatMessageType::System}, // _IsNotAcceptingGiftsRightNow
	{0x03F0, TEXT("That inventory location is invalid."), ACEChatMessageType::ChatError}, // InvalidInventoryLocation
	{0x03F1, TEXT("You failed to go to non-combat mode."), ACEChatMessageType::ChatError}, // ChangeCombatModeFailure
	{0x03F2, TEXT("That inventory location is full."), ACEChatMessageType::ChatError}, // FullInventoryLocation
	{0x03F3, TEXT("That inventory location is already occupied."), ACEChatMessageType::ChatError}, // ConflictingInventoryLocation
	{0x03F4, nullptr, ACEChatMessageType::ChatError}, // ItemNotPending
	{0x03F5, TEXT("Unable to equip that item."), ACEChatMessageType::ChatError}, // BeWieldedFailure
	{0x03F6, TEXT("Unable to drop that item."), ACEChatMessageType::ChatError}, // BeDroppedFailure
	{0x03F7, TEXT("You are too fatigued to attack!"), ACEChatMessageType::ChatError}, // YouAreTooFatiguedToAttack
	{0x03F8, TEXT("You are out of ammunition!"), ACEChatMessageType::ChatError}, // YouAreOutOfAmmunition
	{0x03F9, TEXT("Your missile attack misfired!"), ACEChatMessageType::ChatError}, // YourAttackMisfired
	{0x03FA, TEXT("You've attempted an impossible spell path!"), ACEChatMessageType::ChatError}, // YouveAttemptedAnImpossibleSpellPath
	{0x03FB, nullptr, ACEChatMessageType::ChatError}, // MagicIncompleteAnimList
	{0x03FC, TEXT("Invalid spell type."), ACEChatMessageType::ChatError}, // MagicInvalidSpellType
	{0x03FD, nullptr, ACEChatMessageType::ChatError}, // MagicInqPositionAndVelocityFailure
	{0x03FE, TEXT("You don't know that spell!"), ACEChatMessageType::ChatError}, // YouDontKnowThatSpell
	{0x03FF, TEXT("Incorrect target type"), ACEChatMessageType::ChatError}, // IncorrectTargetType
	{0x0400, TEXT("You don't have all the components for this spell."), ACEChatMessageType::ChatError}, // YouDontHaveAllTheComponents
	{0x0401, TEXT("You don't have enough Mana to cast this spell."), ACEChatMessageType::ChatError}, // YouDontHaveEnoughManaToCast
	{0x0402, TEXT("Your spell fizzled."), ACEChatMessageType::Magic}, // YourSpellFizzled
	{0x0403, TEXT("Your spell's target is missing!"), ACEChatMessageType::ChatError}, // YourSpellTargetIsMissing
	{0x0404, TEXT("Your projectile spell mislaunched!"), ACEChatMessageType::ChatError}, // YourProjectileSpellMislaunched
	{0x0405, nullptr, ACEChatMessageType::ChatError}, // MagicSpellbookAddSpellFailure
	{0x0406, TEXT("Your spell target is out of range."), ACEChatMessageType::ChatError}, // MagicTargetOutOfRange
	{0x0407, TEXT("Your spell cannot be cast outside"), ACEChatMessageType::ChatError}, // YourSpellCannotBeCastOutside
	{0x0408, TEXT("Your spell cannot be cast inside"), ACEChatMessageType::ChatError}, // YourSpellCannotBeCastInside
	{0x0409, TEXT("Unable to cast that spell."), ACEChatMessageType::ChatError}, // MagicGeneralFailure
	{0x040A, TEXT("You are unprepared to cast a spell"), ACEChatMessageType::ChatError}, // YouAreUnpreparedToCastASpell
	{0x040B, TEXT("You've already sworn your Allegiance"), ACEChatMessageType::ChatError}, // YouveAlreadySwornAllegiance
	{0x040C, TEXT("You don't have enough experience available to swear Allegiance"), ACEChatMessageType::ChatError}, // CantSwearAllegianceInsufficientXp
	{0x040D, TEXT("That player is not accepting allegiance requests."), ACEChatMessageType::ChatError}, // AllegianceIgnoringRequests
	{0x040E, TEXT("That player has squelched you."), ACEChatMessageType::ChatError}, // AllegianceSquelched
	{0x040F, TEXT("You are too far away to swear allegiance."), ACEChatMessageType::ChatError}, // AllegianceMaxDistanceExceeded
	{0x0410, TEXT("Your level does not meet the requirements to swear allegiance."), ACEChatMessageType::ChatError}, // AllegianceIllegalLevel
	{0x0411, TEXT("Unable to create the allegiance."), ACEChatMessageType::ChatError}, // AllegianceBadCreation
	{0x0412, TEXT("That player is too busy to accept your allegiance."), ACEChatMessageType::ChatError}, // AllegiancePatronBusy
	{0x0413, TEXT("%s is already one of your followers"), ACEChatMessageType::ChatError}, // _IsAlreadyOneOfYourFollowers
	{0x0414, TEXT("You are not in an allegiance!"), ACEChatMessageType::ChatError}, // YouAreNotInAllegiance
	{0x0415, TEXT("Unable to remove that allegiance relationship."), ACEChatMessageType::ChatError}, // AllegianceRemoveHierarchyFailure
	{0x0416, TEXT("%s cannot have any more Vassals"), ACEChatMessageType::ChatError}, // _CannotHaveAnyMoreVassals
	{0x0417, TEXT("That player is not accepting fellowship requests."), ACEChatMessageType::ChatError}, // FellowshipIgnoringRequests
	{0x0418, TEXT("That player has squelched you."), ACEChatMessageType::ChatError}, // FellowshipSquelched
	{0x0419, TEXT("You are too far away to recruit that player."), ACEChatMessageType::ChatError}, // FellowshipMaxDistanceExceeded
	{0x041A, TEXT("That player is already in a fellowship."), ACEChatMessageType::ChatError}, // FellowshipMember
	{0x041B, TEXT("That player does not meet the fellowship level requirements."), ACEChatMessageType::ChatError}, // FellowshipIllegalLevel
	{0x041C, TEXT("That player is too busy to join the fellowship."), ACEChatMessageType::ChatError}, // FellowshipRecruitBusy
	{0x041D, TEXT("You must be the leader of a Fellowship"), ACEChatMessageType::ChatError}, // YouMustBeLeaderOfFellowship
	{0x041E, TEXT("Your Fellowship is full"), ACEChatMessageType::ChatError}, // YourFellowshipIsFull
	{0x041F, TEXT("That Fellowship name is not permitted"), ACEChatMessageType::ChatError}, // FellowshipNameIsNotPermitted
	{0x0420, TEXT("Your level is too low."), ACEChatMessageType::ChatError}, // LevelTooLow
	{0x0421, TEXT("Your level is too high."), ACEChatMessageType::ChatError}, // LevelTooHigh
	{0x0422, TEXT("That channel doesn't exist."), ACEChatMessageType::ChatError}, // ThatChannelDoesntExist
	{0x0423, TEXT("You can't use that channel."), ACEChatMessageType::ChatError}, // YouCantUseThatChannel
	{0x0424, TEXT("You're already on that channel."), ACEChatMessageType::ChatError}, // YouAreAlreadyOnThatChannel
	{0x0425, TEXT("You're not currently on that channel."), ACEChatMessageType::ChatError}, // YouAreNotOnThatChannel
	{0x0426, TEXT("That item can't be dropped."), ACEChatMessageType::ChatError}, // AttunedItem
	{0x0427, TEXT("You cannot merge different stacks!"), ACEChatMessageType::ChatError}, // YouCannotMergeDifferentStacks
	{0x0428, TEXT("You cannot merge enchanted items!"), ACEChatMessageType::ChatError}, // YouCannotMergeEnchantedItems
	{0x0429, TEXT("You must control at least one stack!"), ACEChatMessageType::ChatError}, // YouMustControlAtLeastOneStack
	{0x042A, TEXT("You are already attacking."), ACEChatMessageType::ChatError}, // CurrentlyAttacking
	{0x042B, TEXT("You cannot make a missile attack right now."), ACEChatMessageType::ChatError}, // MissileAttackNotOk
	{0x042C, TEXT("Target not acquired."), ACEChatMessageType::ChatError}, // TargetNotAcquired
	{0x042D, TEXT("You cannot make that shot."), ACEChatMessageType::ChatError}, // ImpossibleShot
	{0x042E, TEXT("You cannot use that weapon skill."), ACEChatMessageType::ChatError}, // BadWeaponSkill
	{0x042F, TEXT("Unable to unequip that item."), ACEChatMessageType::ChatError}, // UnwieldFailure
	{0x0430, TEXT("Unable to launch that attack."), ACEChatMessageType::ChatError}, // LaunchFailure
	{0x0431, TEXT("Unable to reload that weapon."), ACEChatMessageType::ChatError}, // ReloadFailure
	{0x0432, TEXT("Your craft attempt fails."), ACEChatMessageType::ChatError}, // UnableToMakeCraftReq
	{0x0433, TEXT("Your craft attempt fails."), ACEChatMessageType::ChatError}, // CraftAnimationFailed
	{0x0434, TEXT("Given that number of items, you cannot craft anything."), ACEChatMessageType::ChatError}, // YouCantCraftWithThatNumberOfItems
	{0x0435, TEXT("Your craft attempt fails."), ACEChatMessageType::ChatError}, // CraftGeneralErrorUiMsg
	{0x0436, TEXT(""), ACEChatMessageType::ChatError}, // CraftGeneralErrorNoUiMsg
	{0x0437, TEXT("Either you or one of the items involved does not pass the requirements for this craft interaction."), ACEChatMessageType::ChatError}, // YouDoNotPassCraftingRequirements
	{0x0438, TEXT("You do not have all the neccessary items."), ACEChatMessageType::ChatError}, // YouDoNotHaveAllTheNecessaryItems
	{0x0439, TEXT("Not all the items are avaliable."), ACEChatMessageType::ChatError}, // NotAllTheItemsAreAvailable
	{0x043A, TEXT("You must be at rest in peace mode to do trade skills."), ACEChatMessageType::ChatError}, // YouMustBeInPeaceModeToTrade
	{0x043B, TEXT("You are not trained in that trade skill."), ACEChatMessageType::ChatError}, // YouAreNotTrainedInThatTradeSkill
	{0x043C, TEXT("Your hands must be free."), ACEChatMessageType::ChatError}, // YourHandsMustBeFree
	{0x043D, TEXT("You cannot link to that portal!"), ACEChatMessageType::Magic}, // YouCannotLinkToThatPortal
	{0x043E, TEXT("You have solved this quest too recently!"), ACEChatMessageType::System}, // YouHaveSolvedThisQuestTooRecently
	{0x043F, TEXT("You have solved this quest too many times!"), ACEChatMessageType::System}, // YouHaveSolvedThisQuestTooManyTimes
	{0x0440, TEXT("That quest could not be found."), ACEChatMessageType::ChatError}, // QuestUnknown
	{0x0441, nullptr, ACEChatMessageType::ChatError}, // QuestTableCorrupt
	{0x0442, TEXT("That quest is invalid."), ACEChatMessageType::ChatError}, // QuestBad
	{0x0443, TEXT("You already have that quest."), ACEChatMessageType::ChatError}, // QuestDuplicate
	{0x0444, TEXT("You have not completed the required quest."), ACEChatMessageType::ChatError}, // QuestUnsolved
	{0x0445, TEXT("This item requires you to complete a specific quest before you can pick it up!"), ACEChatMessageType::System}, // ItemRequiresQuestToBePickedUp
	{0x0446, TEXT("Too much time has passed since you completed that quest."), ACEChatMessageType::ChatError}, // QuestSolvedTooLongAgo
	{0x044C, TEXT("That player is not accepting trade requests."), ACEChatMessageType::ChatError}, // TradeIgnoringRequests
	{0x044D, TEXT("That player has squelched you."), ACEChatMessageType::ChatError}, // TradeSquelched
	{0x044E, TEXT("You are too far away to trade."), ACEChatMessageType::ChatError}, // TradeMaxDistanceExceeded
	{0x044F, TEXT("You or that player are already trading."), ACEChatMessageType::ChatError}, // TradeAlreadyTrading
	{0x0450, TEXT("That player is too busy to trade."), ACEChatMessageType::ChatError}, // TradeBusy
	{0x0451, TEXT("Trade closed."), ACEChatMessageType::ChatError}, // TradeClosed
	{0x0452, TEXT("The trade has expired."), ACEChatMessageType::ChatError}, // TradeExpired
	{0x0453, TEXT("That item is already being traded."), ACEChatMessageType::ChatError}, // TradeItemBeingTraded
	{0x0454, TEXT("You cannot trade a container that is not empty."), ACEChatMessageType::ChatError}, // TradeNonEmptyContainer
	{0x0455, TEXT("You must leave combat mode to trade."), ACEChatMessageType::ChatError}, // TradeNonCombatMode
	{0x0456, TEXT("The trade is incomplete."), ACEChatMessageType::ChatError}, // TradeIncomplete
	{0x0457, TEXT("The trade has changed. Please review the items and accept again."), ACEChatMessageType::ChatError}, // TradeStampMismatch
	{0x0458, TEXT("There is no open trade."), ACEChatMessageType::ChatError}, // TradeUnopened
	{0x0459, TEXT("The trade is empty."), ACEChatMessageType::ChatError}, // TradeEmpty
	{0x045A, TEXT("You have already accepted this trade."), ACEChatMessageType::ChatError}, // TradeAlreadyAccepted
	{0x045B, TEXT("The trade is out of sync. Please close it and try again."), ACEChatMessageType::ChatError}, // TradeOutOfSync
	{0x045C, TEXT("Player killers may not interact with that portal!"), ACEChatMessageType::Magic}, // PKsMayNotUsePortal
	{0x045D, TEXT("Non-player killers may not interact with that portal!"), ACEChatMessageType::Magic}, // NonPKsMayNotUsePortal
	{0x045E, TEXT("You do not own a house!"), ACEChatMessageType::System}, // HouseAbandoned
	{0x045F, TEXT("You do not own a house!"), ACEChatMessageType::ChatError}, // HouseEvicted
	{0x0460, TEXT("You already own a house."), ACEChatMessageType::ChatError}, // HouseAlreadyOwned
	{0x0461, TEXT("Unable to purchase that house."), ACEChatMessageType::ChatError}, // HouseBuyFailed
	{0x0462, TEXT("Unable to pay the rent for that house."), ACEChatMessageType::ChatError}, // HouseRentFailed
	{0x0463, TEXT("That item is on a hook."), ACEChatMessageType::ChatError}, // Hooked
	{0x0465, TEXT("You cannot cast from this position."), ACEChatMessageType::ChatError}, // MagicInvalidPosition
	{0x0466, TEXT("You must purchase Asheron's Call: Dark Majesty to interact with that portal."), ACEChatMessageType::Magic}, // YouMustHaveDarkMajestyToUsePortal
	{0x0467, TEXT("That ammunition cannot be used with this weapon."), ACEChatMessageType::ChatError}, // InvalidAmmoType
	{0x0468, TEXT("Your skill is too low."), ACEChatMessageType::ChatError}, // SkillTooLow
	{0x0469, TEXT("You have used all the hooks you are allowed to use for this house."), ACEChatMessageType::System}, // YouHaveUsedAllTheHooks
	{0x046A, TEXT("%s doesn't know what to do with that."), ACEChatMessageType::System}, // TradeAiDoesntWant | _DoesntKnowWhatToDoWithThat
	{0x046B, TEXT("You do not own the house containing that hook."), ACEChatMessageType::ChatError}, // HookHouseNotOwned
	{0x0474, TEXT("You must complete a quest to interact with that portal."), ACEChatMessageType::Magic}, // YouMustCompleteQuestToUsePortal
	{0x047E, TEXT("You are not in an allegiance."), ACEChatMessageType::ChatError}, // HouseNoAllegiance
	{0x047F, TEXT("You must own a house to use this command."), ACEChatMessageType::ChatError}, // YouMustOwnHouseToUseCommand
	{0x0480, TEXT("Your monarch does not own a mansion or a villa!"), ACEChatMessageType::System}, // YourMonarchDoesNotOwnAMansionOrVilla
	{0x0481, TEXT("Your monarch does not own a mansion or a villa!"), ACEChatMessageType::ChatError}, // YourMonarchsHouseIsNotAMansionOrVilla
	{0x0482, TEXT("Your monarch has closed the mansion to the Allegiance."), ACEChatMessageType::ChatError}, // YourMonarchHasClosedTheMansion
	{0x0488, TEXT("You must be above level %s to purchase this dwelling."), ACEChatMessageType::System}, // YouMustBeAboveLevel_ToBuyHouse
	{0x0489, TEXT("You must be at or below level %s to purchase this dwelling."), ACEChatMessageType::System}, // YouMustBeAtOrBelowLevel_ToBuyHouse
	{0x048A, TEXT("You must be a monarch to purchase this dwelling."), ACEChatMessageType::System}, // YouMustBeMonarchToPurchaseDwelling
	{0x048B, TEXT("You must be above allegiance rank %s to purchase this dwelling."), ACEChatMessageType::System}, // YouMustBeAboveAllegianceRank_ToBuyHouse
	{0x048C, TEXT("You must be at or below allegiance rank %s to purchase this dwelling."), ACEChatMessageType::System}, // YouMustBeAtOrBelowAllegianceRank_ToBuyHouse
	{0x048D, TEXT("The allegiance request has expired."), ACEChatMessageType::ChatError}, // AllegianceTimeout
	{0x048E, TEXT("Your offer of Allegiance has been ignored."), ACEChatMessageType::ChatError}, // YourOfferOfAllegianceWasIgnored
	{0x048F, TEXT("You are already involved in something!"), ACEChatMessageType::ChatError}, // ConfirmationInProgress
	{0x0490, TEXT("You must be a monarch to use this command."), ACEChatMessageType::ChatError}, // YouMustBeAMonarchToUseCommand
	{0x0491, TEXT("You must specify a character to boot."), ACEChatMessageType::ChatError}, // YouMustSpecifyCharacterToBoot
	{0x0492, TEXT("You can't boot yourself!"), ACEChatMessageType::ChatError}, // YouCantBootYourself
	{0x0493, TEXT("That character does not exist."), ACEChatMessageType::ChatError}, // ThatCharacterDoesNotExist
	{0x0494, TEXT("That person is not a member of your Allegiance!"), ACEChatMessageType::ChatError}, // ThatPersonIsNotInYourAllegiance
	{0x0495, TEXT("No patron from which to break!"), ACEChatMessageType::ChatError}, // CantBreakFromPatronNotInAllegiance
	{0x0496, TEXT("Your Allegiance has been dissolved!"), ACEChatMessageType::System}, // YourAllegianceHasBeenDissolved
	{0x0497, TEXT("Your patron's Allegiance to you has been broken!"), ACEChatMessageType::System}, // YourPatronsAllegianceHasBeenBroken
	{0x0498, TEXT("You have moved too far!"), ACEChatMessageType::ChatError}, // YouHaveMovedTooFar
	{0x0499, TEXT("That is not a valid destination!"), ACEChatMessageType::ChatError}, // TeleToInvalidPosition
	{0x049A, TEXT("You must purchase Asheron's Call -- Dark Majesty to use this function."), ACEChatMessageType::ChatError}, // MustHaveDarkMajestyToUse
	{0x049B, TEXT("You fail to link with the lifestone!"), ACEChatMessageType::Magic}, // YouFailToLinkWithLifestone
	{0x049C, TEXT("You wandered too far to link with the lifestone!"), ACEChatMessageType::Magic}, // YouWanderedTooFarToLinkWithLifestone
	{0x049D, TEXT("You successfully link with the lifestone!"), ACEChatMessageType::Magic}, // YouSuccessfullyLinkWithLifestone
	{0x049E, TEXT("You must have linked with a lifestone in order to recall to it!"), ACEChatMessageType::Magic}, // YouMustLinkToLifestoneToRecall
	{0x049F, TEXT("You fail to recall to the lifestone!"), ACEChatMessageType::Magic}, // YouFailToRecallToLifestone
	{0x04A0, TEXT("You fail to link with the portal!"), ACEChatMessageType::Magic}, // YouFailToLinkWithPortal
	{0x04A1, TEXT("You successfully link with the portal!"), ACEChatMessageType::Magic}, // YouSuccessfullyLinkWithPortal
	{0x04A2, TEXT("You fail to recall to the portal!"), ACEChatMessageType::Magic}, // YouFailToRecallToPortal
	{0x04A3, TEXT("You must have linked with a portal in order to recall to it!"), ACEChatMessageType::Magic}, // YouMustLinkToPortalToRecall
	{0x04A4, TEXT("You fail to summon the portal!"), ACEChatMessageType::Magic}, // YouFailToSummonPortal
	{0x04A5, TEXT("You must have linked with a portal in order to summon it!"), ACEChatMessageType::Magic}, // YouMustLinkToPortalToSummonIt
	{0x04A6, TEXT("You fail to teleport!"), ACEChatMessageType::Magic}, // YouFailToTeleport
	{0x04A7, TEXT("You have been teleported too recently!"), ACEChatMessageType::Magic}, // YouHaveBeenTeleportedTooRecently
	{0x04A8, TEXT("You must be an Advocate to interact with that portal."), ACEChatMessageType::Magic}, // YouMustBeAnAdvocateToUsePortal
	{0x04A9, TEXT("That creature cannot use this portal."), ACEChatMessageType::ChatError}, // PortalAisNotAllowed
	{0x04AA, TEXT("Players may not interact with that portal."), ACEChatMessageType::Magic}, // PlayersMayNotUsePortal
	{0x04AB, TEXT("You are not powerful enough to interact with that portal!"), ACEChatMessageType::Magic}, // YouAreNotPowerfulEnoughToUsePortal
	{0x04AC, TEXT("You are too powerful to interact with that portal!"), ACEChatMessageType::Magic}, // YouAreTooPowerfulToUsePortal
	{0x04AD, TEXT("You cannot recall to that portal!"), ACEChatMessageType::Magic}, // YouCannotRecallPortal
	{0x04AE, TEXT("You cannot summon that portal!"), ACEChatMessageType::Magic}, // YouCannotSummonPortal
	{0x04AF, TEXT("The lock is already unlocked."), ACEChatMessageType::ChatError}, // LockAlreadyUnlocked
	{0x04B0, TEXT("You can't lock or unlock that!"), ACEChatMessageType::ChatError}, // YouCannotLockOrUnlockThat
	{0x04B1, TEXT("You can't lock or unlock what is open!"), ACEChatMessageType::ChatError}, // YouCannotLockWhatIsOpen
	{0x04B2, TEXT("The key doesn't fit this lock."), ACEChatMessageType::System}, // KeyDoesntFitThisLock
	{0x04B3, TEXT("The lock has been used too recently."), ACEChatMessageType::ChatError}, // LockUsedTooRecently
	{0x04B4, TEXT("You aren't trained in lockpicking!"), ACEChatMessageType::ChatError}, // YouArentTrainedInLockpicking
	{0x04B5, TEXT("You must specify a character to query."), ACEChatMessageType::ChatError}, // AllegianceInfoEmptyName
	{0x04B6, TEXT("Please use the allegiance panel to view your own information."), ACEChatMessageType::ChatError}, // AllegianceInfoSelf
	{0x04B7, TEXT("You have used that command too recently."), ACEChatMessageType::ChatError}, // AllegianceInfoTooRecent
	{0x04B8, TEXT("That character could not be found."), ACEChatMessageType::ChatError}, // AbuseNoSuchCharacter
	{0x04B9, TEXT("You cannot report yourself."), ACEChatMessageType::ChatError}, // AbuseReportedSelf
	{0x04BA, TEXT("Your report has been handled."), ACEChatMessageType::ChatError}, // AbuseComplaintHandled
	{0x04BD, TEXT("You do not own that salvage tool!"), ACEChatMessageType::System}, // YouDoNotOwnThatSalvageTool
	{0x04BE, TEXT("You do not own that item!"), ACEChatMessageType::System}, // YouDoNotOwnThatItem
	{0x04BF, TEXT("The %s was not suitable for salvaging."), ACEChatMessageType::ChatError}, // The_WasNotSuitableForSalvaging
	{0x04C0, TEXT("The %s contains the wrong material."), ACEChatMessageType::ChatError}, // The_ContainseTheWrongMaterial
	{0x04C1, TEXT("The material cannot be created."), ACEChatMessageType::System}, // MaterialCannotBeCreated
	{0x04C2, TEXT("The list of items you are attempting to salvage is invalid."), ACEChatMessageType::System}, // ItemsAttemptingToSalvageIsInvalid
	{0x04C3, TEXT("You cannot salvage items that you are trading!"), ACEChatMessageType::System}, // YouCannotSalvageItemsInTrading
	{0x04C4, TEXT("You must be a guest in this house to interact with that portal."), ACEChatMessageType::Magic}, // YouMustBeHouseGuestToUsePortal
	{0x04C5, TEXT("Your Allegiance Rank is too low to use that item's magic."), ACEChatMessageType::ChatError}, // YourAllegianceRankIsTooLowToUseMagic
	{0x04C6, TEXT("You must be %s to use that item's magic."), ACEChatMessageType::ChatError}, // YouMustBe_ToUseItemMagic
	{0x04C7, TEXT("Your Arcane Lore skill is too low to use that item's magic."), ACEChatMessageType::ChatError}, // YourArcaneLoreIsTooLowToUseMagic
	{0x04C8, TEXT("That item doesn't have enough Mana."), ACEChatMessageType::ChatError}, // ItemDoesntHaveEnoughMana
	{0x04C9, TEXT("Your %s is too low to use that item's magic."), ACEChatMessageType::ChatError}, // Your_IsTooLowToUseItemMagic
	{0x04CA, TEXT("Only %s may use that item's magic."), ACEChatMessageType::ChatError}, // Only_MayUseItemMagic
	{0x04CB, TEXT("You must have %s specialized to use that item's magic."), ACEChatMessageType::ChatError}, // YouMustSpecialize_ToUseItemMagic
	{0x04CC, TEXT("You have been involved in a player killer battle too recently to do that!"), ACEChatMessageType::Magic}, // YouHaveBeenInPKBattleTooRecently
	{0x04CD, TEXT("That character is too busy to trade right now."), ACEChatMessageType::ChatError}, // TradeAiRefuseEmote
	{0x04CE, TEXT("%s is too busy to accept gifts right now."), ACEChatMessageType::System}, // AiRefuseItemDuringEmote
	{0x04CF, TEXT("%s cannot accept stacked objects. Try giving one at a time."), ACEChatMessageType::System}, // _CannotAcceptStackedItems
	{0x04D0, TEXT("You have failed to alter your skill."), ACEChatMessageType::System}, // YouFailToAlterSkill
	{0x04D1, TEXT("Your %s skill must be trained, not untrained or specialized, in order to be altered in this way!"), ACEChatMessageType::System}, // Your_SkillMustBeTrained
	{0x04D2, TEXT("You do not have enough skill credits to specialize your %s skill."), ACEChatMessageType::System}, // NotEnoughSkillCreditsToSpecialize
	{0x04D3, TEXT("You have too many available experience points to be able to absorb the experience points from your %s skill. Please spend some of your experience points and try again."), ACEChatMessageType::System}, // TooMuchXPToRecoverFromSkill
	{0x04D4, TEXT("Your %s skill is already untrained!"), ACEChatMessageType::System}, // Your_SkillIsAlreadyUntrained
	{0x04D5, TEXT("You are currently wielding items which require a certain level of %s.  Your %s skill cannot be lowered while you are wielding these items.  Please remove these items and try again."), ACEChatMessageType::System}, // CannotLowerSkillWhileWieldingItem
	{0x04D6, TEXT("You have succeeded in specializing your %s skill!"), ACEChatMessageType::System}, // YouHaveSucceededSpecializing_Skill
	{0x04D7, TEXT("You have succeeded in lowering your %s skill from specialized to trained!"), ACEChatMessageType::System}, // YouHaveSucceededUnspecializing_Skill
	{0x04D8, TEXT("You have succeeded in untraining your %s skill!"), ACEChatMessageType::System}, // YouHaveSucceededUntraining_Skill
	{0x04D9, TEXT("Although you cannot untrain your %s skill, you have succeeded in recovering all the experience you had invested in it."), ACEChatMessageType::System}, // CannotUntrain_SkillButRecoveredXP
	{0x04DA, TEXT("You have too many credits invested in specialized skills already! Before you can specialize your %s skill, you will need to unspecialize some other skill."), ACEChatMessageType::System}, // TooManyCreditsInSpecializedSkills
	{0x04DB, TEXT("The fellowship invitation was declined."), ACEChatMessageType::ChatError}, // FellowshipDeclined
	{0x04DC, TEXT("The fellowship invitation has expired."), ACEChatMessageType::System}, // FellowshipTimeout
	{0x04DD, TEXT("You have failed to alter your attributes."), ACEChatMessageType::System}, // YouHaveFailedToAlterAttributes
	{0x04DE, TEXT("%s"), ACEChatMessageType::System}, // AttributeTransferFromTooLow
	{0x04DF, TEXT("%s"), ACEChatMessageType::System}, // AttributeTransferToTooHigh
	{0x04E0, TEXT("You are currently wielding items which require a certain level of skill. Your attributes cannot be transferred while you are wielding these items. Please remove these items and try again."), ACEChatMessageType::System}, // CannotTransferAttributesWhileWieldingItem
	{0x04E1, TEXT("You have succeeded in transferring your attributes!"), ACEChatMessageType::System}, // YouHaveSucceededTransferringAttributes
	{0x04E2, TEXT("This hook is a duplicated housing object. You may not add items to a duplicated housing object. Please empty the hook and allow it to reset."), ACEChatMessageType::System}, // HookIsDuplicated
	{0x04E3, TEXT("That item is of the wrong type to be placed on this hook."), ACEChatMessageType::System}, // ItemIsWrongTypeForHook
	{0x04E4, TEXT("This chest is a duplicated housing object. You may not add items to a duplicated housing object. Please empty everything -- including backpacks -- out of the chest and allow the chest to reset."), ACEChatMessageType::System}, // HousingChestIsDuplicated
	{0x04E5, TEXT("This hook was a duplicated housing object. Since it is now empty, it will be deleted momentarily. Once it is gone, it is safe to use the other, non-duplicated hook that is here."), ACEChatMessageType::System}, // HookWillBeDeleted
	{0x04E6, TEXT("This chest was a duplicated housing object. Since it is now empty, it will be deleted momentarily. Once it is gone, it is safe to use the other, non-duplicated chest that is here."), ACEChatMessageType::System}, // HousingChestWillBeDeleted
	{0x04E7, TEXT("You cannot swear allegiance to anyone because you own a monarch-only house. Please abandon your house and try again."), ACEChatMessageType::System}, // CannotSwearAllegianceWhileOwningMansion
	{0x04E8, TEXT("The %s cannot be used while on a hook and only the owner may open the hook."), ACEChatMessageType::System}, // ItemUnusableOnHook_CannotOpen
	{0x04E9, TEXT("The %s cannot be used while on a hook, use the '@house hooks on' command to make the hook openable."), ACEChatMessageType::System}, // ItemUnusableOnHook_CanOpen
	{0x04EA, TEXT("The %s can only be used while on a hook."), ACEChatMessageType::System}, // ItemOnlyUsableOnHook
	{0x04EB, TEXT("You can't do that while in the air!"), ACEChatMessageType::ChatError}, // YouCantDoThatWhileInTheAir
	{0x04EC, TEXT("You cannot modify your player killer status while you are recovering from a PK death."), ACEChatMessageType::System}, // CannotChangePKStatusWhileRecovering
	{0x04ED, TEXT("Advocates may not change their player killer status!"), ACEChatMessageType::System}, // AdvocatesCannotChangePKStatus
	{0x04EE, TEXT("Your level is too low to change your player killer status with this object."), ACEChatMessageType::System}, // LevelTooLowToChangePKStatusWithObject
	{0x04EF, TEXT("Your level is too high to change your player killer status with this object."), ACEChatMessageType::System}, // LevelTooHighToChangePKStatusWithObject
	{0x04F0, TEXT("You feel a harsh dissonance, and you sense that an act of killing you have committed recently is interfering with the conversion."), ACEChatMessageType::System}, // YouFeelAHarshDissonance
	{0x04F1, TEXT("Bael'Zharon's power flows through you again. You are once more a player killer."), ACEChatMessageType::System}, // YouArePKAgain
	{0x04F2, TEXT("Bael'Zharon has granted you respite after your moment of weakness. You are temporarily no longer a player killer."), ACEChatMessageType::System}, // YouAreTemporarilyNoLongerPK
	{0x04F3, TEXT("Lite Player Killers may not interact with that portal!"), ACEChatMessageType::Magic}, // PKLiteMayNotUsePortal
	{0x04F4, TEXT("%s fails to affect you because %s cannot affect anyone!"), ACEChatMessageType::Magic}, // _FailsToAffectYou_TheyCannotAffectAnyone
	{0x04F5, TEXT("%s fails to affect you because you cannot be harmed!"), ACEChatMessageType::Magic}, // _FailsToAffectYou_YouCannotBeHarmed
	{0x04F6, TEXT("%s fails to affect you because %s is not a player killer!"), ACEChatMessageType::Magic}, // _FailsToAffectYou_TheyAreNotPK
	{0x04F7, TEXT("%s fails to affect you because you are not a player killer!"), ACEChatMessageType::Magic}, // _FailsToAffectYou_YouAreNotPK
	{0x04F8, TEXT("%s fails to affect you because you are not the same sort of player killer as %s!"), ACEChatMessageType::Magic}, // _FailsToAffectYou_NotSamePKType
	{0x04F9, TEXT("%s fails to affect you across a house boundary!"), ACEChatMessageType::Magic}, // _FailsToAffectYouAcrossHouseBoundary
	{0x04FA, TEXT("%s is an invalid target."), ACEChatMessageType::Magic}, // _IsAnInvalidTarget
	{0x04FB, TEXT("You are an invalid target for the spell of %s."), ACEChatMessageType::Magic}, // YouAreInvalidTargetForSpellOf_
	{0x04FC, TEXT("You aren't trained in healing!"), ACEChatMessageType::ChatError}, // YouArentTrainedInHealing
	{0x04FD, TEXT("You don't own that healing kit!"), ACEChatMessageType::ChatError}, // YouDontOwnThatHealingKit
	{0x04FE, TEXT("You can't heal that!"), ACEChatMessageType::ChatError}, // YouCantHealThat
	{0x04FF, TEXT("%s is already at full health!"), ACEChatMessageType::ChatError}, // _IsAtFullHealth
	{0x0500, TEXT("You aren't ready to heal!"), ACEChatMessageType::ChatError}, // YouArentReadyToHeal
	{0x0501, TEXT("You can only use Healing Kits on player characters."), ACEChatMessageType::ChatError}, // YouCanOnlyHealPlayers
	{0x0502, TEXT("The Lifestone's magic protects you from the attack!"), ACEChatMessageType::Magic}, // LifestoneMagicProtectsYou
	{0x0503, TEXT("The portal's residual energy protects you from the attack!"), ACEChatMessageType::Magic}, // PortalEnergyProtectsYou
	{0x0504, TEXT("You are enveloped in a feeling of warmth as you are brought back into the protection of the Light. You are once again a Non-Player Killer."), ACEChatMessageType::System}, // YouAreNonPKAgain
	{0x0505, TEXT("You're too close to your sanctuary!"), ACEChatMessageType::ChatError}, // YoureTooCloseToYourSanctuary
	{0x0506, TEXT("You can't do that -- you're trading!"), ACEChatMessageType::ChatError}, // CantDoThatTradeInProgress
	{0x0507, TEXT("Only Non-Player Killers may enter PK Lite. Please see @help pklite for more details about this command."), ACEChatMessageType::System}, // OnlyNonPKsMayEnterPKLite
	{0x0508, TEXT("A cold wind touches your heart. You are now a Player Killer Lite."), ACEChatMessageType::System}, // YouAreNowPKLite
	{0x0509, TEXT("%s has no appropriate targets equipped for this spell."), ACEChatMessageType::Magic}, // _HasNoSpellTargets
	{0x050A, TEXT("You have no appropriate targets equipped for %s's spell."), ACEChatMessageType::Magic}, // YouHaveNoTargetsForSpellOf_
	{0x050B, TEXT("%s is now an open fellowship; anyone may recruit new members."), ACEChatMessageType::System}, // _IsNowOpenFellowship
	{0x050C, TEXT("%s is now a closed fellowship."), ACEChatMessageType::System}, // _IsNowClosedFellowship
	{0x050D, TEXT("%s is now the leader of this fellowship."), ACEChatMessageType::System}, // _IsNowLeaderOfFellowship
	{0x050E, TEXT("You have passed leadership of the fellowship to %s"), ACEChatMessageType::System}, // YouHavePassedFellowshipLeadershipTo_
	{0x050F, TEXT("You do not belong to a Fellowship."), ACEChatMessageType::ChatError}, // YouDoNotBelongToAFellowship
	{0x0510, TEXT("You may not hook any more %s on your house.  You already have the maximum number of %s hooked or you are not permitted to hook any on your type of house."), ACEChatMessageType::System}, // MaxNumberOf_Hooked
	{0x0511, TEXT(""), ACEChatMessageType::ChatError}, // UsingMaxHooksSilent
	{0x0512, TEXT("You are now using the maximum number of hooks.  You cannot use another hook until you take an item off one of your hooks."), ACEChatMessageType::System}, // YouAreNowUsingMaxHooks
	{0x0513, TEXT("You are no longer using the maximum number of hooks.  You may again add items to your hooks."), ACEChatMessageType::System}, // YouAreNoLongerUsingMaxHooks
	{0x0514, TEXT("You now have the maximum number of %s hooked.  You cannot hook any additional %s until you remove one or more from your house."), ACEChatMessageType::System}, // MaxNumberOf_HookedUntilOneIsRemoved
	{0x0515, TEXT("You no longer have the maximum number of %s hooked.  You may hook additional %s."), ACEChatMessageType::System}, // NoLongerMaxNumberOf_Hooked
	{0x0516, TEXT("You are not permitted to use that hook."), ACEChatMessageType::System}, // YouAreNotPermittedToUseThatHook
	{0x0517, TEXT("%s is not close enough to your level."), ACEChatMessageType::System}, // _IsNotCloseEnoughToYourLevel
	{0x0518, TEXT("This fellowship is locked; %s cannot be recruited into the fellowship."), ACEChatMessageType::System}, // LockedFellowshipCannotRecruit_
	{0x0519, TEXT("The fellowship is locked, you were not added to the fellowship."), ACEChatMessageType::System}, // LockedFellowshipCannotRecruitYou
	{0x051A, TEXT("Only the original owner may use that item's magic."), ACEChatMessageType::ChatError}, // ActivationNotAllowedNotOwner
	{0x051B, TEXT("You have entered the %s channel."), ACEChatMessageType::System}, // YouHaveEnteredThe_Channel
	{0x051C, TEXT("You have left the %s channel."), ACEChatMessageType::System}, // YouHaveLeftThe_Channel
	{0x051D, TEXT("Turbine Chat is enabled."), ACEChatMessageType::System}, // TurbineChatIsEnabled
	{0x051E, TEXT("%s will not receive your message, please use urgent assistance to speak with an in-game representative"), ACEChatMessageType::System}, // _WillNotReceiveMessage
	{0x051F, TEXT("Message Blocked: %s"), ACEChatMessageType::ChatError}, // MessageBlocked_
	{0x0520, TEXT("You cannot add anymore people to the list of players that you can hear."), ACEChatMessageType::System}, // YouCannotAddPeopleToHearList
	{0x0521, TEXT("%s has been added to the list of people you can hear."), ACEChatMessageType::System}, // _HasBeenAddedToHearList
	{0x0522, TEXT("%s has been removed from the list of people you can hear."), ACEChatMessageType::System}, // _HasBeenRemovedFromHearList
	{0x0523, TEXT("You are now deaf to player's screams."), ACEChatMessageType::System}, // YouAreNowDeafTo_Screams
	{0x0524, TEXT("You can hear all players once again."), ACEChatMessageType::System}, // YouCanHearAllPlayersOnceAgain
	{0x0525, TEXT("You fail to remove %s from your loud list."), ACEChatMessageType::System}, // FailToRemove_FromLoudList
	{0x0526, TEXT("You chicken out."), ACEChatMessageType::ChatError}, // YouChickenOut
	{0x0527, TEXT("You cannot posssibly succeed."), ACEChatMessageType::ChatError}, // YouCanPossiblySucceed
	{0x0528, TEXT("The fellowship is locked; you cannot open locked fellowships."), ACEChatMessageType::System}, // FellowshipIsLocked | YouCannotOpenLockedFellowship
	{0x0529, TEXT("Trade Complete!"), ACEChatMessageType::ChatError}, // TradeComplete
	{0x052A, TEXT("That is not a salvaging tool."), ACEChatMessageType::ChatError}, // NotASalvageTool
	{0x052B, TEXT("That person is not available now."), ACEChatMessageType::ChatError}, // CharacterNotAvailable
	{0x052C, TEXT("You are now snooping on %s."), ACEChatMessageType::System}, // YouAreNowSnoopingOn_
	{0x052D, TEXT("You are no longer snooping on %s."), ACEChatMessageType::System}, // YouAreNoLongerSnoopingOn_
	{0x052E, TEXT("You fail to snoop on %s."), ACEChatMessageType::System}, // YouFailToSnoopOn_
	{0x052F, TEXT("%s attempted to snoop on you."), ACEChatMessageType::System}, // _AttemptedToSnoopOnYou
	{0x0530, TEXT("%s is already being snooped on, only one person may snoop on another at a time."), ACEChatMessageType::System}, // _IsAlreadyBeingSnoopedOn
	{0x0531, TEXT("%s is in limbo and cannot receive your message."), ACEChatMessageType::System}, // _IsInLimbo
	{0x0532, TEXT("You must wait 30 days after purchasing a house before you may purchase another with any character on the same account. This applies to all housing except apartments."), ACEChatMessageType::System}, // YouMustWaitToPurchaseHouse
	{0x0533, TEXT("You have been booted from your allegiance chat room. Use \"@allegiance chat on\" to rejoin. (%s)."), ACEChatMessageType::System}, // YouHaveBeenBootedFromAllegianceChat
	{0x0534, TEXT("%s has been booted from the allegiance chat room."), ACEChatMessageType::System}, // _HasBeenBootedFromAllegianceChat
	{0x0535, TEXT("You do not have the authority within your allegiance to do that."), ACEChatMessageType::System}, // YouDoNotHaveAuthorityInAllegiance
	{0x0536, TEXT("The account of %s is already banned from the allegiance."), ACEChatMessageType::System}, // AccountOf_IsAlreadyBannedFromAllegiance
	{0x0537, TEXT("The account of %s is not banned from the allegiance."), ACEChatMessageType::System}, // AccountOf_IsNotBannedFromAllegiance
	{0x0538, TEXT("The account of %s was not unbanned from the allegiance."), ACEChatMessageType::System}, // AccountOf_WasNotUnbannedFromAllegiance
	{0x0539, TEXT("The account of %s has been banned from the allegiance."), ACEChatMessageType::System}, // AccountOf_IsBannedFromAllegiance
	{0x053A, TEXT("The account of %s is no longer banned from the allegiance."), ACEChatMessageType::System}, // AccountOf_IsUnbannedFromAllegiance
	{0x053B, TEXT("Banned Characters: %s"), ACEChatMessageType::System}, // ListOfBannedCharacters
	{0x053E, TEXT("%s is banned from the allegiance!"), ACEChatMessageType::System}, // _IsBannedFromAllegiance
	{0x053F, TEXT("You are banned from %s's allegiance!"), ACEChatMessageType::System}, // YouAreBannedFromAllegiance
	{0x0540, TEXT("You have the maximum number of accounts banned.!"), ACEChatMessageType::System}, // YouHaveMaxAccountsBanned
	{0x0541, TEXT("%s is now an allegiance officer."), ACEChatMessageType::System}, // _IsNowAllegianceOfficer
	{0x0542, TEXT("An unspecified error occurred while attempting to set %s as an allegiance officer."), ACEChatMessageType::System}, // ErrorSetting_AsAllegianceOfficer
	{0x0543, TEXT("%s is no longer an allegiance officer."), ACEChatMessageType::System}, // _IsNoLongerAllegianceOfficer
	{0x0544, TEXT("An unspecified error occurred while attempting to remove %s as an allegiance officer."), ACEChatMessageType::System}, // ErrorRemoving_AsAllegianceOFficer
	{0x0545, TEXT("You already have the maximum number of allegiance officers. You must remove some before you add any more."), ACEChatMessageType::System}, // YouHaveMaxAllegianceOfficers
	{0x0546, TEXT("Your allegiance officers have been cleared."), ACEChatMessageType::System}, // YourAllegianceOfficersHaveBeenCleared
	{0x0547, TEXT("You must wait %s before communicating again!"), ACEChatMessageType::System}, // YouMustWait_BeforeCommunicating
	{0x0548, TEXT("You cannot join any chat channels while gagged."), ACEChatMessageType::System}, // YouCannotJoinChannelsWhileGagged
	{0x0549, TEXT("Your allegiance officer status has been modified. You now hold the position of: %s."), ACEChatMessageType::System}, // YourAllegianceOfficerStatusChanged
	{0x054A, TEXT("You are no longer an allegiance officer."), ACEChatMessageType::System}, // YouAreNoLongerAllegianceOfficer
	{0x054B, TEXT("%s is already an allegiance officer of that level."), ACEChatMessageType::System}, // _IsAlreadyAllegianceOfficerOfThatLevel
	{0x054C, TEXT("Your allegiance does not have a hometown."), ACEChatMessageType::System}, // YourAllegianceDoesNotHaveHometown
	{0x054D, TEXT("The %s is currently in use."), ACEChatMessageType::ChatError}, // The_IsCurrentlyInUse
	{0x054E, TEXT("The hook does not contain a usable item. You cannot open the hook because you do not own the house to which it belongs."), ACEChatMessageType::System}, // HookItemNotUsable_CannotOpen
	{0x054F, TEXT("The hook does not contain a usable item. Use the '@house hooks on'command to make the hook openable."), ACEChatMessageType::System}, // HookItemNotUsable_CanOpen
	{0x0550, TEXT("Out of Range!"), ACEChatMessageType::ChatError}, // MissileOutOfRange
	{0x0551, TEXT("You are not listening to the %s channel!"), ACEChatMessageType::System}, // YouAreNotListeningTo_Channel
	{0x0552, TEXT("You must purchase Asheron's Call -- Throne of Destiny to use this function."), ACEChatMessageType::ChatError}, // MustPurchaseThroneOfDestinyToUseFunction
	{0x0553, TEXT("You must purchase Asheron's Call -- Throne of Destiny to use this item."), ACEChatMessageType::ChatError}, // MustPurchaseThroneOfDestinyToUseItem
	{0x0554, TEXT("You must purchase Asheron's Call -- Throne of Destiny to use this portal."), ACEChatMessageType::ChatError}, // MustPurchaseThroneOfDestinyToUsePortal
	{0x0555, TEXT("You must purchase Asheron's Call -- Throne of Destiny to access this quest."), ACEChatMessageType::ChatError}, // MustPurchaseThroneOfDestinyToAccessQuest
	{0x0556, TEXT("You have failed to complete the augmentation."), ACEChatMessageType::System}, // YouFailedToCompleteAugmentation
	{0x0557, TEXT("You have used this augmentation too many times already."), ACEChatMessageType::System}, // AugmentationUsedTooManyTimes
	{0x0558, TEXT("You have used augmentations of this type too many times already."), ACEChatMessageType::System}, // AugmentationTypeUsedTooManyTimes
	{0x0559, TEXT("You do not have enough unspent experience available to purchase this augmentation."), ACEChatMessageType::System}, // AugmentationNotEnoughExperience
	{0x055A, TEXT("%s"), ACEChatMessageType::System}, // AugmentationSkillNotTrained
	{0x055B, TEXT("Congratulations! You have succeeded in acquiring the %s augmentation."), ACEChatMessageType::System}, // YouSuccededAcquiringAugmentation
	{0x055C, TEXT("Although your augmentation will not allow you to untrain your %s skill, you have succeeded in recovering all the experience you had invested in it."), ACEChatMessageType::System}, // YouSucceededRecoveringXPFromSkill_AugmentationNotUntrainable
	{0x055D, TEXT("You must exit the Training Academy before that command will be available to you."), ACEChatMessageType::System}, // ExitTrainingAcademyToUseCommand
	{0x055E, TEXT("%s"), ACEChatMessageType::System}, // AFK
	{0x055F, TEXT("Only Player Killer characters may use this command!"), ACEChatMessageType::System}, // OnlyPKsMayUseCommand
	{0x0560, TEXT("Only Player Killer Lite characters may use this command!"), ACEChatMessageType::System}, // OnlyPKLiteMayUseCommand
	{0x0561, TEXT("You may only have a maximum of 50 friends at once. If you wish to add more friends, you must first remove some."), ACEChatMessageType::ChatError}, // MaxFriendsExceeded
	{0x0562, TEXT("%s is already on your friends list!"), ACEChatMessageType::System}, // _IsAlreadyOnYourFriendsList
	{0x0563, TEXT("That character is not on your friends list!"), ACEChatMessageType::System}, // ThatCharacterNotOnYourFriendsList
	{0x0564, TEXT("Only the character who owns the house may use this command."), ACEChatMessageType::System}, // OnlyHouseOwnerCanUseCommand
	{0x0565, TEXT("That allegiance name is invalid because it is empty. Please use the @allegiance name clear command to clear your allegiance name."), ACEChatMessageType::System}, // InvalidAllegianceNameCantBeEmpty
	{0x0566, TEXT("That allegiance name is too long. Please choose another name."), ACEChatMessageType::System}, // InvalidAllegianceNameTooLong
	{0x0567, TEXT("That allegiance name contains illegal characters. Please choose another name using only letters, spaces, - and '."), ACEChatMessageType::System}, // InvalidAllegianceNameBadCharacters
	{0x0568, TEXT("That allegiance name is not appropriate. Please choose another name."), ACEChatMessageType::System}, // InvalidAllegianceNameInappropriate
	{0x0569, TEXT("That allegiance name is already in use. Please choose another name."), ACEChatMessageType::System}, // InvalidAllegianceNameAlreadyInUse
	{0x056A, TEXT("You may only change your allegiance name once every 24 hours. You may change your allegiance name again in %s."), ACEChatMessageType::System}, // YouMayOnlyChangeAllegianceNameOnceEvery24Hours
	{0x056B, TEXT("Your allegiance name has been cleared."), ACEChatMessageType::System}, // AllegianceNameCleared
	{0x056C, TEXT("That is already the name of your allegiance!"), ACEChatMessageType::System}, // InvalidAllegianceNameSameName
	{0x056D, TEXT("%s is the monarch and cannot be promoted or demoted."), ACEChatMessageType::System}, // _IsTheMonarchAndCannotBePromotedOrDemoted
	{0x056E, TEXT("That level of allegiance officer is now known as: %s."), ACEChatMessageType::System}, // ThatLevelOfAllegianceOfficerIsNowKnownAs_
	{0x056F, TEXT("That is an invalid officer level."), ACEChatMessageType::System}, // InvalidOfficerLevel
	{0x0570, TEXT("That allegiance officer title is not appropriate."), ACEChatMessageType::System}, // AllegianceOfficerTitleIsNotAppropriate
	{0x0571, TEXT("That allegiance name is too long. Please choose another name."), ACEChatMessageType::System}, // AllegianceNameIsTooLong
	{0x0572, TEXT("All of your allegiance officer titles have been cleared."), ACEChatMessageType::System}, // AllegianceOfficerTitlesCleared
	{0x0573, TEXT("That allegiance title contains illegal characters. Please choose another name using only letters, spaces, - and '."), ACEChatMessageType::System}, // AllegianceTitleHasIllegalChars
	{0x0574, TEXT("Your allegiance is currently: %s."), ACEChatMessageType::System}, // YourAllegianceIsCurrently_
	{0x0575, TEXT("Your allegiance is now: %s."), ACEChatMessageType::System}, // YourAllegianceIsNow_
	{0x0576, TEXT("You may not accept the offer of allegiance from %s because your allegiance is locked."), ACEChatMessageType::System}, // YouCannotAcceptAllegiance_YourAllegianceIsLocked
	{0x0577, TEXT("You may not swear allegiance at this time because the allegiance of %s is locked."), ACEChatMessageType::System}, // YouCannotSwearAllegiance_AllegianceOf_IsLocked
	{0x0578, TEXT("You have pre-approved %s to join your allegiance."), ACEChatMessageType::System}, // YouHavePreApproved_ToJoinAllegiance
	{0x0579, TEXT("You have not pre-approved any vassals to join your allegiance."), ACEChatMessageType::System}, // YouHaveNotPreApprovedVassals
	{0x057A, TEXT("%s is already a member of your allegiance!"), ACEChatMessageType::System}, // _IsAlreadyMemberOfYourAllegiance
	{0x057B, TEXT("%s has been pre-approved to join your allegiance."), ACEChatMessageType::System}, // _HasBeenPreApprovedToJoinYourAllegiance
	{0x057C, TEXT("You have cleared the pre-approved vassal for your allegiance."), ACEChatMessageType::System}, // YouHaveClearedPreApprovedVassal
	{0x057D, TEXT("That character is already gagged!"), ACEChatMessageType::System}, // CharIsAlreadyGagged
	{0x057E, TEXT("That character is not currently gagged!"), ACEChatMessageType::System}, // CharIsNotCurrentlyGagged
	{0x057F, TEXT("Your allegiance chat privileges have been temporarily removed by %s. Until they are restored, you may not view or speak in the allegiance chat channel."), ACEChatMessageType::System}, // YourAllegianceChatPrivilegesRemoved
	{0x0580, TEXT("%s is now temporarily unable to view or speak in allegiance chat. The gag will run out in 5 minutes, or %s may be explicitly ungagged before then."), ACEChatMessageType::System}, // _IsTemporarilyGaggedInAllegianceChat
	{0x0581, TEXT("Your allegiance chat privileges have been restored."), ACEChatMessageType::System}, // YourAllegianceChatPrivilegesRestored
	{0x0582, TEXT("Your allegiance chat privileges have been restored by %s."), ACEChatMessageType::System}, // YourAllegianceChatPrivilegesRestoredBy_
	{0x0583, TEXT("You have restored allegiance chat privileges to %s."), ACEChatMessageType::System}, // YouRestoreAllegianceChatPrivilegesTo_
	{0x0584, TEXT("You cannot pick up more of that item!"), ACEChatMessageType::ChatError}, // TooManyUniqueItems
	{0x0585, TEXT("You are restricted to clothes and armor created for your race."), ACEChatMessageType::ChatError}, // HeritageRequiresSpecificArmor
	{0x0586, TEXT("That item was specifically created for another race."), ACEChatMessageType::ChatError}, // ArmorRequiresSpecificHeritage
	{0x0587, TEXT("Olthoi cannot interact with that!"), ACEChatMessageType::Magic}, // OlthoiCannotInteractWithThat
	{0x0588, TEXT("Olthoi cannot use regular lifestones! Asheron would not allow it!"), ACEChatMessageType::Magic}, // OlthoiCannotUseLifestones
	{0x0589, TEXT("The vendor looks at you in horror!"), ACEChatMessageType::Magic}, // OlthoiVendorLooksInHorror
	{0x058A, TEXT("%s cowers from you!"), ACEChatMessageType::System}, // _CowersFromYou
	{0x058B, TEXT("As a mindless engine of destruction an Olthoi cannot join a fellowship!"), ACEChatMessageType::Magic}, // OlthoiCannotJoinFellowship
	{0x058C, TEXT("The Olthoi only have an allegiance to the Olthoi Queen!"), ACEChatMessageType::Magic}, // OlthoiCannotJoinAllegiance
	{0x058D, TEXT("You cannot use that item!"), ACEChatMessageType::Magic}, // YouCannotUseThatItem
	{0x058E, TEXT("This person will not interact with you!"), ACEChatMessageType::Magic}, // ThisPersonWillNotInteractWithYou
	{0x058F, TEXT("Only Olthoi may pass through this portal!"), ACEChatMessageType::Magic}, // OnlyOlthoiMayUsePortal
	{0x0590, TEXT("Olthoi may not pass through this portal!"), ACEChatMessageType::Magic}, // OlthoiMayNotUsePortal
	{0x0591, TEXT("You may not pass through this portal while Vitae weakens you!"), ACEChatMessageType::Magic}, // YouMayNotUsePortalWithVitae
	{0x0592, TEXT("This character must be two weeks old or have been created on an account at least two weeks old to use this portal!"), ACEChatMessageType::Magic}, // YouMustBeTwoWeeksOldToUsePortal
	{0x0593, TEXT("Olthoi characters can only use Lifestone and PK Arena recalls!"), ACEChatMessageType::Magic}, // OlthoiCanOnlyRecallToLifestone
	{0x0594, TEXT("Unable to complete that contract action."), ACEChatMessageType::ChatError}, // ContractError
};
