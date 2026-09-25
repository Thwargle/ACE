#pragma once
#include "CoreMinimal.h"

namespace ACEVRInputLayout
{
 struct FButton {FName Input;const TCHAR* Physical;const TCHAR* Action;};
 inline TConstArrayView<FButton> Buttons()
 {
  static const FButton Values[]={
   {"VRInventory",TEXT("Left X"),TEXT("Inventory / hold for options")},
   {"VRCombat",TEXT("Left Y"),TEXT("Combat stance")},
   {"VRSettings",TEXT("Left Menu"),TEXT("VR options")},
   {"VRSpellWheel",TEXT("Left stick click"),TEXT("Spell wheel")},
   {"VRSelect",TEXT("Right A"),TEXT("Select / next spell")},
   {"VRPrevious",TEXT("Right B"),TEXT("Previous spell")},
   {"VRLeftTrigger",TEXT("Left trigger"),TEXT("Left action / UI click")},
   {"VRRightTrigger",TEXT("Right trigger"),TEXT("Right action / UI click")},
   {"VRLeftGrip",TEXT("Left grip"),TEXT("Left grab / draw")},
   {"VRRightGrip",TEXT("Right grip"),TEXT("Right grab / draw")},
   {"VRJump",TEXT("Right stick click"),TEXT("Charge jump / release")}};
  return Values;
 }
 inline bool IsAction(FName Name){for(const auto& B:Buttons())if(B.Input==Name)return true;return false;}
 inline FString ActionLabel(FName Name){for(const auto& B:Buttons())if(B.Input==Name)return B.Action;return FString();}
}
