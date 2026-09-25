#pragma once

#include "ACEInputBindings.h"

// Retail action names remain stable across import, export, settings and dispatch.
// Use nonconflicting gmDefaultMap defaults. Additional actions stay unbound when
// their retail chord conflicts with an existing AC:Unreal shortcut or chat key.
inline TArray<ACEInputBindings::FAction> ACERetailAdditionalActions()
{
 using namespace ACEInputBindings;
 return {
  {Action(TEXT("SelectionSelf")),TEXT("Select yourself"),TEXT("UI"),{}},
  {Action(TEXT("SelectionClosestPlayer")),TEXT("Closest player"),TEXT("UI"),{}},
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
  {Action(TEXT("ToggleFloatingChatWindow1")),TEXT("Chat window 1"),TEXT("UI"),{}},
  {Action(TEXT("ToggleFloatingChatWindow2")),TEXT("Chat window 2"),TEXT("UI"),{}},
  {Action(TEXT("ToggleFloatingChatWindow3")),TEXT("Chat window 3"),TEXT("UI"),{}},
  {Action(TEXT("ToggleFloatingChatWindow4")),TEXT("Chat window 4"),TEXT("UI"),{}},
  {Action(TEXT("LOGOUT")),TEXT("Log out"),TEXT("UI"),{FInputChord(EKeys::Escape,true,false,false,false)}},
  {Action(TEXT("CaptureScreenshot")),TEXT("Capture screenshot"),TEXT("UI"),{FInputChord(EKeys::Multiply)}},
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
}
