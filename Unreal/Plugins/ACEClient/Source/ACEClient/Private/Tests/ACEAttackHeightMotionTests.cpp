#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatMotionPlayer.h"
#include "ACEDatSubsystem.h"
#include "ACEAttackMotion.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACESession.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAttackHeightMotionTest, "ACE.RetailParity.AttackHeightMotion",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEAttackHeightMotionTest::RunTest(const FString&)
{
 FACEDatDatabase Portal;
 if (!TestTrue(TEXT("Retail DAT opens"), Portal.Open(TEXT("C:/Turbine/Asheron's Call/client_portal.dat")))) return false;
 FACEDatMotionPlayer Player(&Portal);
 if (!TestTrue(TEXT("Retail human motion table loads"), Player.SetMotionTable(0x09000001))) return false;
 TArray<uint8> Blob;
 if (!TestTrue(TEXT("Retail human combat maneuvers load"), Portal.ReadFile(0x30000000, Blob))) return false;
 FACEDatCursor Cursor(Blob);
 uint32 Id=0, Count=0;
 if (!Cursor.Read(Id) || !Cursor.Read(Count) || !TestEqual(TEXT("Combat table ID"), Id, 0x30000000u)
  || !TestTrue(TEXT("Complete combat table records"), Count>0 && uint64(Count)*20==uint64(Cursor.Remaining()))) return false;
 struct FManeuver { uint32 Style, Height, Type, Skill, Motion; };
 TArray<FManeuver> Maneuvers;
 TSet<uint32> Heights, Styles;
 for (uint32 Index=0; Index<Count; ++Index)
 {
  FManeuver M; Cursor.Read(M.Style); Cursor.Read(M.Height); Cursor.Read(M.Type); Cursor.Read(M.Skill); Cursor.Read(M.Motion);
  Maneuvers.Add(M); Heights.Add(M.Height); Styles.Add(M.Style);
  TestEqual(TEXT("Packed attack retains its exact retail command"), ACEMotion::ExpandPackedCommand(uint16(M.Motion)), M.Motion);
  TestTrue(TEXT("Every retail combat maneuver is recognized as an attack action"),ACEAttackMotion::IsAction(M.Motion));
 }
 TestTrue(TEXT("All three retail heights covered"), Heights.Contains(1) && Heights.Contains(2) && Heights.Contains(3));
 TestTrue(TEXT("Unarmed, sword, shield, dual wield and two-handed covered"), Styles.Contains(ACEMotion::StanceHandCombat)
  && Styles.Contains(ACEMotion::StanceSwordCombat) && Styles.Contains(ACEMotion::StanceSwordShield)
  && Styles.Contains(ACEMotion::StanceDualWield) && Styles.Contains(ACEMotion::StanceTwoHandedSword));
 // CMotionTable::get_link (0x00522670): exact style/source, then style-wide.
 // The full command is the inner key: low/medium/high are distinct actions.
 auto Link=[&](uint32 Style, uint32 From, uint32 To)->const FACEDatMotionData*
 {
  for (uint32 Key : {(Style<<16)|(From&0xFFFFFF), Style<<16})
   if (const auto* Links=Player.MotionTable->Links.Find(Key))
    if (const auto* Data=Links->Find(To)) return Data;
  return nullptr;
 };
 TestNull(TEXT("A missing attack must not borrow a different weapon stance"),
  Player.FindLink(ACEMotion::Ready, 0x1000005Au, ACEMotion::StanceMagic));
 TArray<FACEDatAnimData> InvalidTransition;
 TestFalse(TEXT("An unsupported attack cannot play just the preceding stance exit"),
  Player.FindTransitionAnims(ACEMotion::Sitting,0x1000005Au,InvalidTransition,ACEMotion::StanceNonCombat));
 const int32 MeleeManeuvers=Maneuvers.Num();
 // Missile height is represented by the server's elevation-specific Aim
 // substate, not by locally substituting a melee High/Medium/Low command.
 for (uint32 Style : {ACEMotion::StanceBowCombat,ACEMotion::StanceCrossbow,ACEMotion::StanceThrownWeapon,
  ACEMotion::StanceAtlatl,ACEMotion::StanceThrownShield})
  for (uint32 Aim=0x4000001Eu; Aim<=0x4000002Au; ++Aim)
  {
   if (!TestNotNull(FString::Printf(TEXT("Retail missile aim %08X/%08X"),Style,Aim),Link(Style,ACEMotion::Ready,Aim))) return false;
   Maneuvers.Add({Style,0,0,0,Aim});
  }

 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI);
 Context.OwningGameInstance=GI; Context.SetCurrentWorld(World); GI->OnWorldChanged(nullptr,World); GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);};
 if (!TestTrue(TEXT("World DAT loads"), GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
 FACEWorldObject Obj; Obj.Guid=12345; Obj.SetupId=0x02000001; Obj.MotionTableId=0x09000001; Obj.bIsPlayer=true;
 auto* Actor=World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Obj,100,false);
 auto* App=Actor->Appearance.Get();
 if (!TestTrue(TEXT("Combat fixture geometry builds"),App->ApplyWorldObject(Obj,100,false))) return false;
 FACESession Session; Session.PlayerGuid=1;
 Obj.bHasPhysicsTimestamps=true; Obj.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=3;
 Session.WorldObjects.Add(Obj.Guid,Obj);
 FACEObjectMotionState Received;
 Session.OnMotionUpdate.AddLambda([&](int32,const FACEObjectMotionState& Motion){Received=Motion;});
 uint16 Sequence=0;
 auto Receive=[&](uint32 Style, uint32 Motion, float Rate)
 {
  ++Sequence;
  FACEBinaryWriter W; W.WriteUInt32(Obj.Guid); W.WriteUInt16(3); W.WriteUInt16(Sequence);
  W.WriteUInt16(0); W.WriteUInt8(0); W.Align();
  W.WriteUInt8(0); W.WriteUInt8(0); W.WriteUInt16(uint16(Style));
  if (ACEAttackMotion::IsAim(Motion))
  {
   W.WriteUInt32(2u|4u); W.WriteUInt16(uint16(Motion)); W.WriteFloat(Rate);
  }
  else
  {
   W.WriteUInt32(1u<<7); W.WriteUInt16(uint16(Motion)); W.WriteUInt16(Sequence); W.WriteFloat(Rate);
  }
  W.Align();
  FACEBinaryReader R(W.GetData()); Session.HandleUpdateMotion(R);
  TestEqual(TEXT("Server selects the exact attack command"), uint32(Received.ActionCommand), Motion);
  TestEqual(TEXT("Server selects the weapon stance"), uint32(Received.CurrentStyle), Style);
  TestEqual(TEXT("Server attack speed preserved"), Received.ActionSpeed, Rate);
  App->PlayActionMotion(Received.ActionCommand,Received.ActionSpeed,Received.CurrentStyle,false,Received.bNewAction);
 };
 auto Reset=[&](uint32 Style)
 {
  App->ClearActionMotion(); App->PreferredStyle=Style; App->SetLocomotionInput(0,0,false,1);
  TArray<FTransform> Ready; int32 Parts=0;
  Player.EvaluateMotion(ACEMotion::Ready,0,App->GetPartCount(),Ready,100,Parts,nullptr,nullptr,Style);
  App->StanceBlendAlpha=1; App->StanceBlendFrom.Reset(); App->ApplyAnimatedPartsWithBlend(Ready,Parts,0);
 };
 // Separate network updates can arrive before the previous swing ends. A height
 // change must enqueue the new action just as repeated same-height attacks do.
 Reset(ACEMotion::StanceSwordCombat);
 Receive(ACEMotion::StanceSwordCombat,0x1000005Bu,1);
 App->TickComponent(1.f/90,LEVELTICK_All,nullptr);
 Receive(ACEMotion::StanceSwordCombat,0x1000005Du,2);
 TestEqual(TEXT("High swing finishes before the next low swing"),App->ActionCommand,0x1000005Bu);
 TestEqual(TEXT("Different-height attack is queued once"),App->PendingActionCommands.Num(),1);
 if (App->PendingActionCommands.Num()==1)
 {
  TestEqual(TEXT("Queued height retained"),App->PendingActionCommands[0],0x1000005Du);
  TestEqual(TEXT("Queued speed retained"),App->PendingActionPlayRates[0],2.f);
  for (int32 Frame=0; Frame<900 && App->ActionCommand==0x1000005Bu; ++Frame)
   App->TickComponent(1.f/90,LEVELTICK_All,nullptr);
  TestEqual(TEXT("Queued low swing starts after high completes"),App->ActionCommand,0x1000005Du);
  App->TickComponent(1.f/90,LEVELTICK_All,nullptr);
  TArray<FTransform> Expected; int32 Parts=0; bool Finished=false;
  Player.EvaluateAnimSequence(Link(ACEMotion::StanceSwordCombat,ACEMotion::Ready,0x1000005Du)->Anims,
   2.f/90,App->GetPartCount(),Expected,100,Parts,false,&Finished);
  for (int32 Part=0; Part<Parts; ++Part)
  {
   const auto Actual=App->GetPartMesh(Part)->GetRelativeTransform();
   TestTrue(TEXT("Queued low attack has its authored pose from the first frame"),
    Actual.GetTranslation().Equals(Expected[Part].GetTranslation(),.04001)
    && Actual.GetRotation().Equals(Expected[Part].GetRotation(),.000011));
  }
 }

 int32 ClipsChecked=0, PoseFrames=0;
 for (uint32 Setup : {0x02000001u,0x0200004Eu})
 {
  Obj.SetupId=Setup;
  if (!TestTrue(TEXT("Avatar geometry builds"),App->ApplyWorldObject(Obj,100,false))) return false;
  for (const auto& M : Maneuvers)
  {
   const auto* Reference=Link(M.Style,ACEMotion::Ready,M.Motion);
   if (!TestNotNull(FString::Printf(TEXT("Retail attack link %08X/%08X height %u"),M.Style,M.Motion,M.Height),Reference)) return false;
   TestTrue(TEXT("Link lookup preserves retail weapon/height"),Player.FindLink(ACEMotion::Ready,M.Motion,M.Style)==Reference);
   for (float Dt : {1.f/30,1.f/90}) for (float Rate : {1.f,2.5f})
   {
    Reset(M.Style); Receive(M.Style,M.Motion,Rate);
    bool Complete=false;
    for (int32 Frame=0; Frame<FMath::CeilToInt(8.f/Dt); ++Frame)
    {
     const float Time=App->AnimTime+Dt*Rate;
     App->TickComponent(Dt,LEVELTICK_All,nullptr);
     TArray<FTransform> Expected; int32 Parts=0; bool Finished=false;
     if (!TestTrue(TEXT("Authored attack evaluates"),Player.EvaluateAnimSequence(Reference->Anims,Time,App->GetPartCount(),Expected,100,Parts,false,&Finished))) return false;
     // Compare from the FIRST frame, including fast attacks that finish inside
     // the old 0.28-second generic stance blend. Keep authored height and arcs.
     for (int32 Part=0; Part<Parts; ++Part)
     {
      const auto Actual=App->GetPartMesh(Part)->GetRelativeTransform();
      if (!TestTrue(FString::Printf(TEXT("Height %u style %08X motion %08X setup %08X rate %.1f part %d t=%.3f matches retail pose"),
        M.Height,M.Style,M.Motion,Setup,Rate,Part,Time),
       Actual.GetTranslation().Equals(Expected[Part].GetTranslation(),.04001)
       && Actual.GetRotation().Equals(Expected[Part].GetRotation(),.000011))) return false;
     }
     ++PoseFrames;
     if (Finished) {Complete=true; break;}
    }
    TestTrue(TEXT("Entire attack plays; only missile aim holds its endpoint"),Complete
     && (ACEAttackMotion::IsAim(M.Motion) ? App->ActionCommand==M.Motion && App->bHoldActionFinal : App->ActionCommand==0));
    ++ClipsChecked;
   }
  }
 }
 AddInfo(FString::Printf(TEXT("Validated %d retail combat maneuvers in %d stances, %d missile aim variants, %d clips, %d pose frames."),
  MeleeManeuvers,Styles.Num(),Maneuvers.Num()-MeleeManeuvers,ClipsChecked,PoseFrames));
 return true;
}
#endif
