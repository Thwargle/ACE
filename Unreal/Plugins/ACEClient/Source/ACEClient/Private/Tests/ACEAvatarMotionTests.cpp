#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "VR/ACEVRMath.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAvatarMotionTest,"ACE.RetailParity.AvatarMotion",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEAvatarMotionTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
 FACEWorldObject Obj;Obj.Guid=12345;Obj.SetupId=0x02000001;Obj.MotionTableId=0x09000001;Obj.bIsPlayer=true;
 auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Obj,100,false);
 auto* App=Actor->Appearance.Get();
 if(!TestTrue(TEXT("Retail avatar geometry builds"),App->ApplyWorldObject(Obj,100,false)))return false;
 for(uint32 Setup:{0x02000001u,0x0200004eu})for(float Dt:{1.f/90,1.f/30})
 {
  Obj.SetupId=Setup;App->bVRPoseControlled=false;
  App->ApplyWorldObject(Obj,100,false);
  if(!TestTrue(TEXT("Both retail human setups build"),App->HasAppearance() && App->GetSetupId()==Setup))return false;
  FTransform HeadBind,ChestBind,HipBind;App->GetPartBindTransform(16,HeadBind);
  App->GetPartBindTransform(9,ChestBind);App->GetPartBindTransform(0,HipBind);
  const FVector Waist=(ChestBind.GetLocation()+HipBind.GetLocation())*.5;
  const FTransform ActorBefore=Actor->GetActorTransform();
  App->bVRPoseControlled=true;App->ResetVRLowerBody();
  for(float Yaw:{0.f,137.f})for(float Scale:{.7f,1.f,1.4f})
  {
   const FTransform Head(FRotator(-65,Yaw,15),FVector(35,15,170));
   const FQuat Facing=FRotator(0,Yaw,0).Quaternion();
   const FTransform Left(Facing,Head.GetLocation()+Facing.RotateVector(FVector(20,-20,-35)*Scale));
   const FTransform Right(Facing,Head.GetLocation()+Facing.RotateVector(FVector(65,20,-15)*Scale));
   FTransform Upper;
   auto Tick=[&](const FTransform& H,bool Tracked)
   {
    App->GetMeshRoot()->SetWorldTransform(ACEVRMath::BodyFromHead(H,HeadBind,FVector(0,-8,17),Scale));
    Upper=App->UpdateVRUpperBody(H,Left,Right,Tracked,Tracked,Dt);
   };
   for(int Frame=0;Frame<FMath::RoundToInt(1/Dt);++Frame)Tick(Head,true);
   const auto Base=App->GetMeshRoot()->GetComponentTransform();
   const auto Chest=App->GetPartMesh(9)->GetComponentTransform();
   TestTrue(TEXT("Looking down visibly bends the chest by a subtle bounded amount"),App->VRTorsoAngles.X < -4 && App->VRTorsoAngles.X>=-8.001);
   TestTrue(TEXT("Asymmetric arm reach also twists the chest"),App->VRTorsoAngles.Y>.5 && App->VRTorsoAngles.Y<=4.001);
   TestTrue(TEXT("Chest and waist keep their shared attachment point"),Chest.TransformPosition(ChestBind.InverseTransformPosition(Waist)).Equals(Base.TransformPosition(Waist),.01));
   const auto HeadRotation=Head.GetRotation()*FRotator(0,-90,0).Quaternion()*HeadBind.GetRotation();
   TestTrue(TEXT("Bending keeps the chest neck at the tracked head"),Upper.TransformPosition(HeadBind.GetLocation()).Equals(Head.GetLocation()-HeadRotation.RotateVector(FVector(0,-8,17)*Scale),.01));
   TestTrue(TEXT("Body sway cannot move the collision root"),Actor->GetActorTransform().Equals(ActorBefore,.001));
   const FTransform Neutral(FRotator(0,Yaw,0),Head.GetLocation());
   for(int Frame=0;Frame<FMath::RoundToInt(1/Dt);++Frame)Tick(Neutral,false);
   TestTrue(TEXT("Lost hand tracking returns smoothly to neutral without accumulating drift"),App->VRTorsoAngles.Size()<.01);
  }
 }
 Actor->Destroy();
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
 Session->State=EACESessionState::InWorld;Session->PlayerGuid=Obj.Guid;
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;Client->SetJumpSkill(200);
 auto* Pawn=World->SpawnActor<APawn>();App=NewObject<UACECharacterAppearanceComponent>(Pawn);
 Pawn->AddInstanceComponent(App);App->RegisterComponent();PC->Possess(Pawn);
 Obj.SetupId=0x02000001;App->ApplyWorldObject(Obj,100,false);
 PC->PredictedPose.CellId=0x7D640019;PC->PredictedPose.Location=FVector(50,100,50);PC->bHavePredictedPose=true;
 // CMotionInterp::LeaveGround applies Falling; HitGround removes old links.
 // Reproduce immediate land/re-jump through the real controller, not only a
 // transform helper, using the DAT transitions for each common stance.
 for(uint32 Style:{ACEMotion::StanceNonCombat,0x8000003fu,ACEMotion::StanceMagic})for(float Dt:{1.f/90,1.f/30})
 {
  App->SetPreferredStyle(Style);App->SetLocomotionInput(1,0,true,1);
  for(int Jump=0;Jump<5;++Jump)
  {
   PC->bJumpAirborne=false;PC->bJumpCharging=true;PC->JumpChargeExtent=.1f;PC->ReleaseJump(1,0);
   TestEqual(TEXT("Takeoff enters the actual retail Falling transition"),App->ActionCommand,0x40000015u);
   TestFalse(TEXT("A new jump never starts at a held final frame"),App->bHoldActionFinal);
   TestTrue(TEXT("Every jump restarts its own animation clock"),App->AnimTime<.001);
   App->TickComponent(Dt,LEVELTICK_All,nullptr);
   FTransform Before;App->GetPartCurrentTransform(9,Before);
   for(int Frame=0;Frame<FMath::CeilToInt(.08f/Dt);++Frame)App->TickComponent(Dt,LEVELTICK_All,nullptr);
   FTransform After;App->GetPartCurrentTransform(9,After);
   TestTrue(TEXT("Successive jump takeoff poses visibly advance instead of freezing"),!After.Equals(Before,.01));
   TestTrue(TEXT("The short handoff finishes during the retail airborne link"),App->StanceBlendAlpha>.99);
   const float Time=App->AnimTime;App->PlayActionMotion(0x40000015u,1,Style);
   TestEqual(TEXT("Duplicate airborne motion echoes cannot restart the transition"),App->AnimTime,Time);
   if(Jump%2==0)for(int Frame=0;Frame<FMath::CeilToInt(.4f/Dt);++Frame)App->TickComponent(Dt,LEVELTICK_All,nullptr);
   App->ClearJumpMotionIfAny();App->TickComponent(Dt,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Landing releases both unfinished and held airborne motion"),App->ActionCommand==0 && !App->bHoldActionFinal && !App->bHoldActionFinalAfterFinish && !App->QueuedHoldAction);
  }
 }
 PC->UnPossess();Pawn->Destroy();PC->Destroy();Session->State=EACESessionState::Disconnected;
 return !HasAnyErrors();
}
#endif
