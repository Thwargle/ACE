#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "ACETypes.h"

// Weenie setup/motion/scale from ACEmulator/ACE-World-16PY-Patches:
// Database/Patches/9 WeenieDefaults/Creature/{Carenzi,Gromnie,PhyntosWasp}.
// Physics spheres themselves must come from client_portal.dat, never the
// setup's enclosing visual radius or a single capsule around the wings.
namespace ACECreatureFixtures
{
 struct FModel {const TCHAR* Name; uint32 Weenie,Setup,Motion;float Scale;};
 inline constexpr FModel Models[]={
  {TEXT("Carenzi Burrower"),11492,0x02000A95,0x090000BD,1.75f},
  {TEXT("Rust Gromnie"),1611,0x02000037,0x0900001B,.9f},
  {TEXT("White Phyntos Wasp"),7105,0x02001121,0x09000167,1.2f}};
 inline void Apply(FACEWorldObject& Object,int32 Index)
 {
  const auto& Model=Models[Index%3];
  Object.Name=Model.Name;Object.SetupId=Model.Setup;Object.MotionTableId=Model.Motion;
  Object.Scale=Model.Scale;Object.ItemType=ACEItemType::Creature;
  Object.bIsPlayer=false;Object.bIsSelf=false;
  Object.PhysicsState=ACEPhysicsState::Gravity|ACEPhysicsState::ReportCollisions;
 }
}
#endif
