#pragma once

#include "ACEInputBindings.h"

// Retail action names remain stable across import, export, settings and dispatch.
// Defaults come from the retail gmDefaultMap and DefaultMap resources.
inline const TArray<ACEInputBindings::FAction>& ACERetailAdditionalActions()
{
 using namespace ACEInputBindings;
 static const TArray<FAction> Actions={
  {Action(TEXT("SelectionPreviousSelection")),TEXT("Previous selection"),TEXT("UI"),{FInputChord(EKeys::P)}},
  {Action(TEXT("SelectionSelf")),TEXT("Select yourself"),TEXT("UI"),{}},
  {Action(TEXT("SelectionClosestPlayer")),TEXT("Closest player"),TEXT("UI"),{FInputChord(EKeys::Slash)}},
  {Action(TEXT("SelectionPreviousPlayer")),TEXT("Previous player"),TEXT("UI"),{FInputChord(EKeys::Comma)}},
  {Action(TEXT("SelectionNextPlayer")),TEXT("Next player"),TEXT("UI"),{FInputChord(EKeys::Period)}},
  {Action(TEXT("SelectionPreviousFellow")),TEXT("Previous fellowship member"),TEXT("UI"),{FInputChord(EKeys::N)}},
  {Action(TEXT("SelectionNextFellow")),TEXT("Next fellowship member"),TEXT("UI"),{FInputChord(EKeys::M)}},
  {Action(TEXT("SelectionClosestCompassItem")),TEXT("Closest radar object"),TEXT("UI"),{FInputChord(EKeys::BackSpace)}},
  {Action(TEXT("SelectionPreviousCompassItem")),TEXT("Previous radar object"),TEXT("UI"),{FInputChord(EKeys::Hyphen)}},
  {Action(TEXT("SelectionNextCompassItem")),TEXT("Next radar object"),TEXT("UI"),{FInputChord(EKeys::Equals)}},
  {Action(TEXT("SelectionUseClosestUnopenedCorpse")),TEXT("Use closest unopened corpse"),TEXT("UI"),{}},
  {Action(TEXT("SelectionDrop")),TEXT("Drop selected item"),TEXT("UI"),{}},
  {Action(TEXT("SelectionGive")),TEXT("Give selected item"),TEXT("UI"),{}},
  {Action(TEXT("SelectionSplitStack")),TEXT("Split selected stack"),TEXT("UI"),{FInputChord(EKeys::T)}},
  {Action(TEXT("ToggleHelp")),TEXT("Help"),TEXT("UI"),{FInputChord(EKeys::F1)}},
  {Action(TEXT("TogglePluginManager")),TEXT("Plugin Manager"),TEXT("UI"),{FInputChord(EKeys::F1,true,true,false,false)}},
  {Action(TEXT("ToggleKeyboardPanel")),TEXT("Keyboard settings"),TEXT("UI"),{}},
  {Action(TEXT("TogglePositiveEffectsPanel")),TEXT("Beneficial spells in effect"),TEXT("UI"),{}},
  {Action(TEXT("ToggleNegativeEffectsPanel")),TEXT("Harmful spells in effect"),TEXT("UI"),{}},
  {Action(TEXT("ToggleVitaePanel")),TEXT("Vitae"),TEXT("UI"),{}},
  {Action(TEXT("ToggleLinkStatusPanel")),TEXT("Link status"),TEXT("UI"),{}},
  {Action(TEXT("ToggleConfigOptionsPanel")),TEXT("Configuration options"),TEXT("UI"),{}},
  {Action(TEXT("ToggleJournalPanel")),TEXT("Journal"),TEXT("UI"),{}},
  {Action(TEXT("ToggleQuestManagementPanel")),TEXT("Quest management"),TEXT("UI"),{}},
  {Action(TEXT("TogglePageListPanel")),TEXT("Journal page list"),TEXT("UI"),{}},
  {Action(TEXT("ToggleFriendsPanel")),TEXT("Friends"),TEXT("CharacterSettings"),{}},
  {Action(TEXT("ToggleHousePanel")),TEXT("House"),TEXT("UI"),{}},
  {Action(TEXT("ToggleFloatingChatWindow1")),TEXT("Chat window 1"),TEXT("UI"),{FInputChord(EKeys::One,false,false,true,false)}},
  {Action(TEXT("ToggleFloatingChatWindow2")),TEXT("Chat window 2"),TEXT("UI"),{FInputChord(EKeys::Two,false,false,true,false)}},
  {Action(TEXT("ToggleFloatingChatWindow3")),TEXT("Chat window 3"),TEXT("UI"),{FInputChord(EKeys::Three,false,false,true,false)}},
  {Action(TEXT("ToggleFloatingChatWindow4")),TEXT("Chat window 4"),TEXT("UI"),{FInputChord(EKeys::Four,false,false,true,false)}},
  {Action(TEXT("LOGOUT")),TEXT("Log out"),TEXT("UI"),{FInputChord(EKeys::Escape,true,false,false,false)}},
  {Action(TEXT("CaptureScreenshot")),TEXT("Capture screenshot"),TEXT("UI"),{FInputChord(EKeys::Multiply)}},
  {Action(TEXT("CameraActivateAlternateMode")),TEXT("Hold alternate camera"),TEXT("Camera"),{FInputChord(EKeys::Divide),FInputChord(EKeys::F2)}},
  {Action(TEXT("Alternate.CameraRotateLeft")),TEXT("Alternate camera: rotate left"),TEXT("Camera"),{FInputChord(EKeys::Left)},AlternateCameraContext},
  {Action(TEXT("Alternate.CameraRotateRight")),TEXT("Alternate camera: rotate right"),TEXT("Camera"),{FInputChord(EKeys::Right)},AlternateCameraContext},
  {Action(TEXT("Alternate.CameraRotateUp")),TEXT("Alternate camera: rotate up"),TEXT("Camera"),{FInputChord(EKeys::Up)},AlternateCameraContext},
  {Action(TEXT("Alternate.CameraRotateDown")),TEXT("Alternate camera: rotate down"),TEXT("Camera"),{FInputChord(EKeys::Down)},AlternateCameraContext},
  {Action(TEXT("CameraInstantMouseLook")),TEXT("Hold mouse look"),TEXT("Camera"),{FInputChord(EKeys::MiddleMouseButton)}},
  {Action(TEXT("CameraViewMapMode")),TEXT("Bird's-eye camera"),TEXT("Camera"),{FInputChord(NumpadEnterKey())}},
  {Action(TEXT("AFKState")),TEXT("AFK pose"),TEXT("Emotes"),{}},
  {Action(TEXT("BlowKiss")),TEXT("Blow a kiss"),TEXT("Emotes"),{}},
  {Action(TEXT("BeSeeingYou")),TEXT("Be seeing you"),TEXT("Emotes"),{}},
  {Action(TEXT("BowDeep")),TEXT("Bow deeply"),TEXT("Emotes"),{}},
  {Action(TEXT("TapFootState")),TEXT("Tap foot"),TEXT("Emotes"),{}},
  {Action(TEXT("ThinkerState")),TEXT("Thinker pose"),TEXT("Emotes"),{}},
  {Action(TEXT("Winded")),TEXT("Winded"),TEXT("Emotes"),{}},
  {Action(TEXT("Woah")),TEXT("Woah"),TEXT("Emotes"),{}},
  {Action(TEXT("YMCA")),TEXT("YMCA"),TEXT("Emotes"),{}}
 };
 return Actions;
}
