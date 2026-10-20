#pragma once

#include "CoreTypes.h"

namespace ACEAttackMotion
{
 // Retail MotionCommand attack families, not a height-to-animation mapping.
 // The server's combat maneuver table selects the exact action and stance.
 constexpr bool IsAction(uint32 Command)
 {
  return (Command >= 0x10000058u && Command <= 0x1000006Eu) // Thrust/Slash/Backhand/Shoot/Attack1-3 and creature attacks
   || (Command >= 0x100000CDu && Command <= 0x100000D2u) // SpecialAttack and MissileAttack
   || Command == 0x100000E3u // Bite
   || (Command >= 0x1000011Fu && Command <= 0x1000012Au) // Double/triple slash/thrust
   || (Command >= 0x10000173u && Command <= 0x1000019Au); // Offhand, Attack4-6 and punches
 }

 constexpr bool IsAim(uint32 Command)
 {
  return Command >= 0x4000001Eu && Command <= 0x4000002Au; // AimHigh90 through AimLow90
 }

 constexpr bool IsAttack(uint32 Command) { return IsAction(Command) || IsAim(Command); }
}
