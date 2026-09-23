// User-supplied retail bindings; historical device GUIDs removed.
static const FString RetailCustomKeymap = FString(TEXT(R"KEYMAP(Devices [ Keyboard [ GUID_SysKeyboard ] Mouse [ GUID_SysMouse ] Joystick [ Unused ] Joystick [ LegacyDevice ] ]
MetaKeys
[
  1 [ 0 DIK_LSHIFT ]
  2 [ 0 DIK_LCONTROL ]
  2 [ 0 DIK_RCONTROL ]
  3 [ 0 DIK_LMENU ]
  3 [ 0 DIK_RALT ]
  4 [ 0 DIK_LWIN ]
  4 [ 0 DIK_RWIN ]
  5 [ 1 DIMOFS_BUTTON3 ]
  6 [ 1 DIMOFS_BUTTON4 ]
]

Bindings
[
  SystemKeys
  [
    AltEnter [ "" [ 0 DIK_RETURN ] 0x00000004 ]
    AltTab [ "" [ 0 DIK_TAB ] 0x00000004 ]
    AltF4 [ "" [ 0 DIK_F4 ] 0x00000004 ]
    CtrlShiftEsc [ "" [ 0 DIK_ESCAPE ] 0x00000003 ]
  ]

  MouseCommands
  [
    PointerX [ "" [ 1 DIMOFS_X ] 0x00000000 Analog ]
    PointerY [ "" [ 1 DIMOFS_Y ] 0x00000000 Analog ]
    SelectLeft [ "" [ 1 DIMOFS_BUTTON0 ] ]
    SelectRight [ "" [ 1 DIMOFS_BUTTON1 ] ]
    SelectMid [ "" [ 1 DIMOFS_BUTTON2 ] ]
    SelectDblLeft [ "" [ 1 DIMOFS_BUTTON0 ] 0x00000000 MouseDblClick ]
    SelectDblRight [ "" [ 1 DIMOFS_BUTTON1 ] 0x00000000 MouseDblClick ]
    SelectDblMid [ "" [ 1 DIMOFS_BUTTON2 ] 0x00000000 MouseDblClick ]
  ]

  CameraControls
  [
    DoNothing [ "" [ 3 DIJOFS_POV0 POVUp ] ]
    DoNothing [ "" [ 1 DIMOFS_BUTTON4 ] ]
    CameraMoveToward [ "" [ 1 DIMOFS_Z AxisPositive ] ]
    DoNothing [ "" [ 1 DIMOFS_BUTTON3 ] ]
    CameraMoveAway [ "" [ 1 DIMOFS_Z AxisNegative ] ]
    CameraInstantMouseLook [ "" [ 1 DIMOFS_BUTTON2 ] ]
    CameraRotateRight [ "" [ 0 DIK_NUMPAD4 ] ]
    CameraRotateLeft [ "" [ 0 DIK_NUMPAD6 ] ]
    CameraRotateUp [ "" [ 0 DIK_NUMPAD8 ] ]
    CameraRotateDown [ "" [ 0 DIK_NUMPAD2 ] ]
    CameraMoveToward [ "" [ 0 DIK_NUMPADMINUS ] ]
    CameraMoveAway [ "" [ 0 DIK_NUMPADPLUS ] ]
    CameraViewDefault [ "" [ 0 DIK_NUMPAD0 ] ]
    CameraViewFirstPerson [ "" [ 0 DIK_DECIMAL ] ]
    CameraViewLookDown [ "" [ 0 DIK_NUMPAD5 ] ]
    CameraViewMapMode [ "" [ 0 DIK_NUMPADENTER ] ]
  ]

  ScrollableControls
  [
    ScrollUp [ "" [ 1 DIMOFS_Z AxisPositive ] ]
    ScrollDown [ "" [ 1 DIMOFS_Z AxisNegative ] ]
    ScrollUp [ "" [ 0 DIK_UPARROW ] 0x00000002 ]
    ScrollDown [ "" [ 0 DIK_DOWNARROW ] 0x00000002 ]
  ]

  EditControls
  [
    CursorCharLeft [ "" [ 0 DIK_LEFT ] ]
    CursorCharRight [ "" [ 0 DIK_RIGHTARROW ] ]
    CursorPreviousLine [ "" [ 0 DIK_UPARROW ] ]
    CursorNextLine [ "" [ 0 DIK_DOWNARROW ] ]
    CursorPreviousPage [ "" [ 0 DIK_PGUP ] ]
    CursorNextPage [ "" [ 0 DIK_PGDN ] ]
    CursorWordLeft [ "" [ 0 DIK_LEFT ] 0x00000002 ]
    CursorWordRight [ "" [ 0 DIK_RIGHTARROW ] 0x00000002 ]
    CursorStartOfLine [ "" [ 0 DIK_HOME ] ]
    CursorStartOfDocument [ "" [ 0 DIK_HOME ] 0x00000002 ]
    CursorEndOfLine [ "" [ 0 DIK_END ] ]
    CursorEndOfDocument [ "" [ 0 DIK_END ] 0x00000002 ]
    EscapeKey [ "" [ 0 DIK_ESCAPE ] ]
    AcceptInput [ "" [ 0 DIK_RETURN ] ]
    DeleteKey [ "" [ 0 DIK_DELETE ] ]
    BackspaceKey [ "" [ 0 DIK_BACK ] ]
  ]

  CopyAndPasteControls
  [
    CopyText [ "" [ 0 DIK_C ] 0x00000002 ]
    CopyText [ "" [ 0 DIK_INSERT ] 0x00000002 ]
    CutText [ "" [ 0 DIK_X ] 0x00000002 ]
    CutText [ "" [ 0 DIK_DELETE ] 0x00000001 ]
    PasteText [ "" [ 0 DIK_V ] 0x00000002 ]
    PasteText [ "" [ 0 DIK_INSERT ] 0x00000001 ]
  ]

  DialogBoxes
  [
    EscapeKey [ "" [ 0 DIK_ESCAPE ] ]
    AcceptInput [ "" [ 0 DIK_RETURN ] ]
  ]

  MovementCommands
  [
    MovementJump [ "" [ 0 DIK_J ] ]
    MovementForward [ "" [ 3 DIJOFS_Y AxisNegative ] ]
    MovementTurnLeft [ "" [ 3 DIJOFS_X AxisNegative ] ]
    MovementBackup [ "" [ 3 DIJOFS_Y AxisPositive ] ]
    MovementTurnRight [ "" [ 3 DIJOFS_X AxisPositive ] ]
    DoNothing [ "" [ 3 DIJOFS_RZ AxisPositive ] ]
    MovementStrafeRight [ "" [ 3 DIJOFS_BUTTON11 ] ]
    DoNothing [ "" [ 3 DIJOFS_RZ AxisNegative ] ]
    MovementStrafeLeft [ "" [ 3 "DIJOFS_BUTTON(7)" ] ]
    DoNothing [ "" [ 0 DIK_UPARROW ] 0x00000002 ]
    MovementRunLock [ "" [ 0 DIK_UPARROW ] 0x00000004 ]
    MovementRunLock [ "" [ 0 DIK_Q ] 0x00000004 ]
    MovementForward [ "" [ 0 DIK_W ] ]
    MovementForward [ "" [ 0 DIK_UPARROW ] ]
    MovementBackup [ "" [ 0 DIK_X ] ]
    MovementBackup [ "" [ 0 DIK_DOWNARROW ] ]
    MovementTurnLeft [ "" [ 0 DIK_A ] ]
    MovementTurnLeft [ "" [ 0 DIK_LEFT ] ]
    MovementTurnRight [ "" [ 0 DIK_D ] ]
    MovementTurnRight [ "" [ 0 DIK_RIGHTARROW ] ]
    MovementStrafeLeft [ "" [ 0 DIK_Z ] ]
    DoNothing [ "" [ 0 DIK_A ] 0x00000004 ]
    MovementStrafeLeft [ "" [ 0 DIK_LEFT ] 0x00000004 ]
    MovementStrafeRight [ "" [ 0 DIK_C ] ]
    DoNothing [ "" [ 0 DIK_D ] 0x00000004 ]
    MovementStrafeRight [ "" [ 0 DIK_RIGHTARROW ] 0x00000004 ]
    MovementWalkMode [ "" [ 0 DIK_LSHIFT ] ]
    DoNothing [ "" [ 0 DIK_Q ] ]
    MovementStop [ "" [ 0 DIK_S ] ]
    Ready [ "" [ 0 DIK_Y ] ]
    Sitting [ "" [ 0 DIK_G ] ]
    Crouch [ "" [ 0 DIK_H ] ]
    Sleeping [ "" [ 0 DIK_B ] ]
  ]

  ItemSelectionCommands
  [
    SelectionUseClosestUnopenedCorpse [ "" [ 0 DIK_LWIN ] ]
    SelectionSelf [ "" [ 0 DIK_NUMPAD1 ] ]
    DoN)KEYMAP")) + TEXT(R"KEYMAP(othing [ "" [ 0 DIK_RWIN ] ]
    SelectionClosestMonster [ "" [ 3 DIJOFS_BUTTON1 ] ]
    SelectionClosestMonster [ "" [ 0 DIK_SPACE ] ]
    SelectionNextMonster [ "" [ 0 DIK_RALT ] ]
    DoNothing [ "" [ 0 DIK_NUMPAD9 ] ]
    SelectionGive [ "" [ 0 DIK_NUMPAD9 ] 0x00000002 ]
    DoNothing [ "" [ 0 DIK_NUMPAD3 ] ]
    SelectionDrop [ "" [ 0 DIK_NUMPAD3 ] 0x00000002 ]
    SelectionPickUp [ "" [ 0 DIK_F ] ]
    SelectionSplitStack [ "" [ 0 DIK_T ] ]
    SelectionClosestCompassItem [ "" [ 0 DIK_BACK ] ]
    SelectionPreviousCompassItem [ "" [ 0 DIK_MINUS ] ]
    SelectionNextCompassItem [ "" [ 0 DIK_EQUALS ] ]
    SelectionClosestItem [ "" [ 0 DIK_BACKSLASH ] ]
    SelectionPreviousItem [ "" [ 0 DIK_LBRACKET ] ]
    SelectionNextItem [ "" [ 0 DIK_RBRACKET ] ]
    SelectionClosestMonster [ "" [ 0 DIK_APOSTROPHE ] ]
    SelectionPreviousMonster [ "" [ 0 DIK_L ] ]
    SelectionNextMonster [ "" [ 0 DIK_SEMICOLON ] ]
    DoNothing [ "" [ 0 DIK_HOME ] ]
    SelectionClosestPlayer [ "" [ 0 DIK_SLASH ] ]
    SelectionPreviousPlayer [ "" [ 0 DIK_COMMA ] ]
    SelectionNextPlayer [ "" [ 0 DIK_PERIOD ] ]
    SelectionPreviousFellow [ "" [ 0 DIK_N ] ]
    SelectionNextFellow [ "" [ 0 DIK_M ] ]
  ]

  UICommands
  [
    ToggleKeyboardPanel [ "" [ 0 DIK_F2 ] ]
    SelectionExamine [ "" [ 0 DIK_NUMPADSLASH ] ]
    TogglePositiveEffectsPanel [ "" [ 0 DIK_O ] ]
    ToggleNegativeEffectsPanel [ "" [ 0 DIK_I ] ]
    ToggleVitaePanel [ "" [ 0 DIK_P ] ]
    ToggleLinkStatusPanel [ "" [ 0 DIK_F11 ] 0x00000004 ]
    DoNothing [ "" [ 0 DIK_WEBBACK ] ]
    LOGOUT [ "" [ 0 DIK_X ] 0x00000004 ]
    LOGOUT [ "" [ 0 DIK_Q ] 0x00000002 ]
    ToggleConfigOptionsPanel [ "" [ 0 DIK_F12 ] 0x00000004 ]
    ToggleJournalPanel [ "" [ 0 DIK_0 ] ]
    ToggleQuestManagementPanel [ "" [ 0 DIK_0 ] 0x00000004 ]
    TogglePageListPanel [ "" [ 0 DIK_0 ] 0x00000002 ]
    DoNothing [ "" [ 0 DIK_F ] 0x00000004 ]
    ToggleAllegiancePanel [ "" [ 0 DIK_F3 ] 0x00000004 ]
    USE [ "" [ 0 DIK_RCONTROL ] ]
    ToggleContractsPanel [ "" [ 0 DIK_APPS ] ]
    SelectionExamine [ "" [ 0 DIK_E ] ]
    CaptureScreenshot [ "" [ 0 DIK_NUMPADSTAR ] ]
    ToggleHelp [ "" [ 0 DIK_F1 ] ]
    TogglePluginManager [ "" [ 0 DIK_F1 ] 0x00000003 ]
    ToggleFriendsPanel [ "" [ 0 DIK_F3 ] ]
    ToggleFellowshipPanel [ "" [ 0 DIK_F4 ] ]
    ToggleSpellbookPanel [ "" [ 0 DIK_F5 ] ]
    ToggleSpellComponentsPanel [ "" [ 0 DIK_F6 ] ]
    ToggleAttributesPanel [ "" [ 0 DIK_F8 ] ]
    ToggleSkillsPanel [ "" [ 0 DIK_F9 ] ]
    ToggleHousePanel [ "" [ 0 DIK_F10 ] ]
    ToggleOptionsPanel [ "" [ 0 DIK_F11 ] ]
    ToggleInventoryPanel [ "" [ 0 DIK_F12 ] ]
    ToggleFloatingChatWindow1 [ "" [ 0 DIK_1 ] 0x00000004 ]
    ToggleFloatingChatWindow2 [ "" [ 0 DIK_2 ] 0x00000004 ]
    ToggleFloatingChatWindow3 [ "" [ 0 DIK_3 ] 0x00000004 ]
    ToggleFloatingChatWindow4 [ "" [ 0 DIK_4 ] 0x00000004 ]
    USE [ "" [ 0 DIK_R ] ]
    EscapeKey [ "" [ 0 DIK_ESCAPE ] ]
    DoNothing [ "" [ 0 DIK_ESCAPE ] 0x00000001 ]
  ]

  QuickslotCommands
  [
    UseQuickSlot_7 [ "" [ 3 DIJOFS_BUTTON2 ] ]
    UseQuickSlot_8 [ "" [ 3 "DIJOFS_BUTTON(3)" ] ]
    UseQuickSlot_9 [ "" [ 3 DIJOFS_BUTTON5 ] ]
    UseQuickSlot_10 [ "" [ 0 DIK_1 ] 0x00000003 ]
    UseQuickSlot_11 [ "" [ 0 DIK_2 ] 0x00000003 ]
    UseQuickSlot_12 [ "" [ 0 DIK_3 ] 0x00000003 ]
    UseQuickSlot_13 [ "" [ 0 DIK_4 ] 0x00000003 ]
    UseQuickSlot_1 [ "" [ 0 DIK_1 ] ]
    UseQuickSlot_2 [ "" [ 0 DIK_2 ] ]
    UseQuickSlot_3 [ "" [ 0 DIK_3 ] ]
    UseQuickSlot_4 [ "" [ 0 DIK_4 ] ]
    UseQuickSlot_5 [ "" [ 0 DIK_5 ] ]
    UseQuickSlot_6 [ "" [ 0 DIK_6 ] ]
    UseQuickSlot_7 [ "" [ 0 DIK_7 ] ]
    UseQuickSlot_8 [ "" [ 0 DIK_8 ] ]
    UseQuickSlot_9 [ "" [ 0 DIK_9 ] ]
    UseQuickSlot_14 [ "" [ 0 DIK_5 ] 0x00000004 ]
    UseQuickSlot_15 [ "" [ 0 DIK_6 ] 0x00000004 ]
    UseQuickSlot_16 [ "" [ 0 DIK_7 ] 0x00000004 ]
    UseQuickSlot_17 [ "" [ 0 DIK_8 ] 0x00000004 ]
    UseQuickSlot_18 [ "" [ 0 DIK_9 ] 0x00000004 ]
    UseQuickSlot_1 [ "" [ 0 DIK_1 ] 0x00000002 ]
    UseQuickSlot_2 [ "" [ 0 DIK_2 ] 0x00000002 ]
    UseQuickSlot_3 [ "" [ 0 DIK_3 ] 0x00000002 ]
    UseQuickSlot_4 [ "" [ 0 DIK_4 ] 0x00000002 ]
    UseQuickSlot_5 [ "" [ 0 DIK_5 ] 0x00000002 ]
    UseQuickSlot_6 [ "" [ 0 DIK_6 ] 0x00000002 ]
    UseQuickSlot_7 [ "" [ 0 DIK_7 ] 0x00000002 ]
    UseQuickSlot_8 [ "" [ 0 DIK_8 ] 0x00000002 ]
    UseQuickSlot_9 [ "" [ 0 DIK_9 ] 0x00000002 ]
  ]

  ToggleChatEntry
  [
    DoNothing [ "" [ 0 DIK_LMENU ] ]
    DoNothing [ "" [ 0 DIK_TAB ] ]
  ]

  ChatCommands
  [
    DoNothing [ "" [ 0 DIK_LCONTROL ] ]
    EnterChatMode [ "" [ 0 DIK_RETURN ] ]
  ]

  Combat
  [
    CombatToggleCombat [ "" [ 3 "DIJOFS_BUTTON(4)" ] ]
    CombatToggleCombat [ "" [ 0 DIK_GRAVE ] ]
  ]

  MeleeCombat
  [
    DoNothing [ "" [ 3 "DIJOFS_BUTTON(0)" ] ]
    CombatDecreaseAttackPower [ "" [ 0 DIK_INSERT ] ]
    CombatIncreaseAttackPower [ "" [ 0 DIK_PGUP ] ])KEYMAP") + TEXT(R"KEYMAP(
    CombatLowAttack [ "" [ 0 DIK_DELETE ] ]
    CombatMediumAttack [ "" [ 0 DIK_END ] ]
    CombatHighAttack [ "" [ 0 DIK_PGDN ] ]
  ]

  MissileCombat
  [
    CombatDecreaseMissileAccuracy [ "" [ 0 DIK_INSERT ] ]
    CombatIncreaseMissileAccuracy [ "" [ 0 DIK_PGUP ] ]
    CombatAimLow [ "" [ 0 DIK_DELETE ] ]
    CombatAimMedium [ "" [ 0 DIK_END ] ]
    CombatAimHigh [ "" [ 0 DIK_PGDN ] ]
  ]

  MagicCombat
  [
    DoNothing [ "" [ 0 DIK_HOME ] 0x00000002 ]
    DoNothing [ "" [ 0 DIK_END ] 0x00000002 ]
    CombatPrevSpellTab [ "" [ 0 DIK_INSERT ] ]
    CombatNextSpellTab [ "" [ 0 DIK_PGUP ] ]
    CombatPrevSpell [ "" [ 0 DIK_DELETE ] ]
    CombatCastCurrentSpell [ "" [ 0 DIK_END ] ]
    CombatNextSpell [ "" [ 0 DIK_PGDN ] ]
    CombatFirstSpellTab [ "" [ 0 DIK_INSERT ] 0x00000002 ]
    CombatLastSpellTab [ "" [ 0 DIK_PGUP ] 0x00000002 ]
    CombatFirstSpell [ "" [ 0 DIK_DELETE ] 0x00000002 ]
    CombatLastSpell [ "" [ 0 DIK_PGDN ] 0x00000002 ]
    UseSpellSlot_1 [ "" [ 0 DIK_1 ] ]
    UseSpellSlot_2 [ "" [ 0 DIK_2 ] ]
    UseSpellSlot_3 [ "" [ 0 DIK_3 ] ]
    UseSpellSlot_4 [ "" [ 0 DIK_4 ] ]
    UseSpellSlot_5 [ "" [ 0 DIK_5 ] ]
    UseSpellSlot_6 [ "" [ 0 DIK_6 ] ]
    UseSpellSlot_7 [ "" [ 0 DIK_7 ] ]
    UseSpellSlot_8 [ "" [ 0 DIK_8 ] ]
    UseSpellSlot_9 [ "" [ 0 DIK_9 ] ]
  ]

  Emotes
  [
    AFKState [ "" [ 0 DIK_NUMPAD7 ] ]
    Wave [ "" [ 0 DIK_J ] 0x00000004 ]
    BlowKiss [ "" [ 0 DIK_K ] 0x00000004 ]
    BeSeeingYou [ "" [ 0 DIK_S ] 0x00000004 ]
    BowDeep [ "" [ 0 DIK_U ] 0x00000004 ]
    TapFootState [ "" [ 0 DIK_T ] 0x00000004 ]
    ThinkerState [ "" [ 0 DIK_R ] 0x00000004 ]
    Winded [ "" [ 0 DIK_W ] 0x00000004 ]
    Woah [ "" [ 0 DIK_Z ] 0x00000004 ]
    YMCA [ "" [ 0 DIK_Y ] 0x00000004 ]
    DoNothing [ "" [ 0 DIK_C ] 0x00000004 ]
    DoNothing [ "" [ 0 DIK_B ] 0x00000004 ]
    PointState [ "" [ 0 DIK_K ] ]
    BowDeep [ "" [ 0 DIK_U ] ]
  ]

  TargetedUsage
  [
    SelectLeft [ "" [ 1 DIMOFS_BUTTON0 ] ]
    SelectRight [ "" [ 1 DIMOFS_BUTTON1 ] ]
  ]

  CameraAlternateControls
  [
    DoNothing [ "" [ 0 DIK_END ] ]
    CameraRotateLeft [ "" [ 0 DIK_LEFT ] ]
    CameraRotateRight [ "" [ 0 DIK_RIGHTARROW ] ]
    CameraRotateUp [ "" [ 0 DIK_UPARROW ] ]
    CameraRotateDown [ "" [ 0 DIK_DOWNARROW ] ]
  ]
]
)KEYMAP");
