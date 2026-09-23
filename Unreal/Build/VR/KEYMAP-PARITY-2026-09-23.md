# Retail custom keymap compatibility

Reviewed `Roogon II.keymap` against the retail client input code and the shipped
`client_portal.dat` action/default maps (0x26000000, 0x14000000, 0x14000002).
The supplied file is unchanged. The regression fixture omits the user's historic
device identifiers but retains the control definitions and bindings.

## What the old warning meant

The old importer accepted **111 bindings** and generated **67 diagnostic groups**:
43 unsupported action names, 15 device/control diagnostic groups, and 9 input-map
contexts. It did not mean that 67 of 111 bindings were rejected. Repeated failures
for one action/control category were collapsed into a single warning.

The updated importer accepts **156 action bindings**, plus **17 explicit
DoNothing overrides**. The remaining report identifies **19 unsupported control
entries** individually, and separately lists the nine unchanged contexts.

## Added action support

- Targeting: SelectionSelf, SelectionClosestPlayer, SelectionPreviousPlayer,
  SelectionNextPlayer, SelectionPreviousFellow, SelectionNextFellow,
  SelectionClosestCompassItem, SelectionPreviousCompassItem,
  SelectionNextCompassItem, SelectionUseClosestUnopenedCorpse.
- Inventory: SelectionDrop, SelectionGive, SelectionSplitStack.
- Panels: ToggleKeyboardPanel, TogglePositiveEffectsPanel,
  ToggleNegativeEffectsPanel, ToggleVitaePanel, ToggleLinkStatusPanel,
  ToggleConfigOptionsPanel, ToggleJournalPanel, ToggleQuestManagementPanel,
  TogglePageListPanel, ToggleFriendsPanel, ToggleHousePanel,
  ToggleFloatingChatWindow1 through ToggleFloatingChatWindow4.
- Other: LOGOUT, CaptureScreenshot, CameraInstantMouseLook.
- Emotes: AFKState, BlowKiss, BeSeeingYou, BowDeep, TapFootState, ThinkerState,
  Winded, Woah, YMCA.

Mouse-wheel direction bindings now import/export and control camera zoom.
DIK_APPS (the keyboard Menu key) now maps to the contracts shortcut in this file.

## Interaction corrections

- Explicit DoNothing bindings suppress fallback actions, including Ctrl+Up and
  Alt+A/Alt+D. They survive settings reload and supported-keymap export/import;
  explicitly rebinding the chord replaces the override.
- Keyboard zoom-in/out follows the retail action direction. Wheel bindings can
  be removed or reversed instead of being hardcoded.
- Instant mouse look works while held and does not appraise on release.
- Player/radar selection wraps through eligible nearby objects using retail's
  planar-distance-plus-height ordering. Fellowship selection follows member
  order, includes self, and skips unavailable objects. Unopened-corpse selection
  uses the existing interaction path and records successful container opens.
- Split Stack focuses the existing quantity field. Entered quantities are
  clamped to the actual stack and used by giving, dropping, and inventory
  transfers. Giving enters target selection rather than using the item.
- Panel shortcuts select their appropriate subtab. For example, Friends changes
  the social panel to Friends rather than merely toggling the current social tab.
- Imported combat maps retain their stance-specific priority, so a spell slot
  does not also use an inventory shortcut. This file's Alt+number chat shortcuts
  and Ctrl+Shift+number second-row shortcuts retain their distinct meanings.
- The existing Load File dialog presents a scrollable report, then returns to
  the mapping draft. Keyboard OK applies the draft; Cancel discards it.

## Remaining limitations

There is no claim of complete retail input-map parity:

- 15 entries address a legacy DirectInput joystick: its X/Y/RZ axes, POV control,
  and buttons for strafing, targeting, quickslots, combat, or DoNothing. These
  device-specific indices are not guessed into modern controller/VR bindings.
- One DoNothing entry uses DIK_WEBBACK, which is not mapped.
- CameraViewMapMode (numpad Enter), ToggleHelp (F1), and TogglePluginManager
  (Ctrl+Shift+F1) remain unsupported. They are not silently assigned a different
  action. Existing overhead camera is distinct from retail's distant map view.
- SystemKeys, MouseCommands, ScrollableControls, EditControls,
  CopyAndPasteControls, DialogBoxes, ToggleChatEntry, TargetedUsage, and
  CameraAlternateControls are not remapped by this importer (49 records).
  Existing application/OS controls remain active. Standard Slate text controls
  provide Ctrl+C/X/V and Ctrl+Insert/Shift+Delete/Shift+Insert. Custom alternate
  camera and widget-context remapping still require separate implementation.
- Save As exports supported bindings and explicit supported DoNothing overrides;
  it is not a lossless archival copy of unsupported devices or input contexts.
  Keep the original retail file if it is also used with retail.

Default Enter/slash chat and second-row shortcut conveniences remain in place
for fresh profiles. Importing this file replaces those mappings with its explicit
choices. Existing customized profiles are not reset.

## Primary references and validation

- Retail CMasterInputMap/CInputMap serialization and DeviceKeyMapEntry parsing.
- Retail CPlayerSystem::OnAction / SelectNext, ClientFellowshipSystem traversal,
  gmToolbarUI::RecvNotice_SplitStack, ItemHolder target/drop handling,
  CameraManager/CameraSet, and gmKeyboardUI.
- ACE.RetailParity.KeyboardBindings exercises the supplied file, disabled chords,
  modifier priority, combat contexts, save/reload, and export/import.
- ACE.RetailParity.UIScreens exercises player/radar/fellowship selection, stack
  quantities, actual panel shortcuts, and renders the import report and controls.
- Validation outputs are in the ignored Unreal/Saved/KeymapAudit directory.

Validation passed: Win64 editor build, Android Development build, shared Quest
source verification, KeyboardBindings on the final source, UIScreens on desktop
and ES3.1 mobile rendering, and CameraAndEdges after the instant-mouse-look fix.
These automated checks do not substitute for a live-server keyboard/controller
play session. No physical legacy joystick was tested.

These are shared client-source changes for desktop, PC VR, and standalone Quest.
The changes are included in release v68. Live headset acceptance remains pending.
