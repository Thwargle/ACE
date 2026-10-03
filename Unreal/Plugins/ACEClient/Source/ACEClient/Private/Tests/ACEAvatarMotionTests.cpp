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
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

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
 {
  auto* Reuse=IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Animation.ReusePoseBuffers"));
  const int32 OldReuse=Reuse->GetInt();ON_SCOPE_EXIT{Reuse->Set(OldReuse,ECVF_SetByCode);};
  TArray<FTransform> Reference;
  for(int32 Variant:{0,1})
  {
   Reuse->Set(Variant,ECVF_SetByCode);
   auto* Sample=World->SpawnActor<AACEWorldEntityActor>();Sample->InitializeFromObject(Obj,100,false);
   auto* Animation=Sample->Appearance.Get();
   if(!TestTrue(TEXT("Pose allocation comparison uses a real loaded avatar"),Animation->ApplyWorldObject(Obj,100,false) && Animation->GetPartCount()>0))return false;
   Animation->SetLocomotionInput(1,0,true,1);
   for(int32 Frame=0;Frame<90;++Frame)
   {
    Animation->TickComponent(1.f/90.f,LEVELTICK_All,nullptr);
    for(int32 Part=0;Part<Animation->GetPartCount();++Part)
    {
     const FTransform Pose=Animation->GetPartMesh(Part)->GetRelativeTransform();
     if(Variant==0) Reference.Add(Pose);
     else TestTrue(TEXT("Reused pose storage preserves each rendered part on every frame"),Pose.Equals(Reference[Frame*Animation->GetPartCount()+Part],.00001));
    }
   }
   if(Variant==1)
   {
    const auto* Storage=Animation->TickPoseScratch.GetData();
    for(int32 Frame=0;Frame<90;++Frame) Animation->TickComponent(1.f/90.f,LEVELTICK_All,nullptr);
    TestNotNull(TEXT("Warm animation retains its pose allocation"),Storage);
    TestTrue(TEXT("Steady animation reuses the same allocation across frames"),Storage==Animation->TickPoseScratch.GetData());
   }
   Sample->Destroy();
  }
  // Tracked avatars use a separate gait evaluator in desktop observers, PC VR
  // and Quest. Buffer reuse must preserve every foot/hip pose, including stops,
  // direction changes, teleport reset and a different human Setup.
  for (uint32 Setup : {0x02000001u, 0x0200004eu})
  {
   TArray<FTransform> GaitReference;
   auto GaitObject=Obj;GaitObject.SetupId=Setup;
   for (int32 Variant : {0,1})
   {
    Reuse->Set(Variant,ECVF_SetByCode);
    auto* Sample=World->SpawnActor<AACEWorldEntityActor>();Sample->InitializeFromObject(GaitObject,100,false);
    auto* Animation=Sample->Appearance.Get();
    if(!TestTrue(TEXT("VR gait fixture builds"),Animation->ApplyWorldObject(GaitObject,100,false)))return false;
    Animation->bVRPoseControlled=true;Animation->ResetVRLowerBody();
    for(int32 Frame=0;Frame<360;++Frame)
    {
     FVector Delta=Frame<90?FVector(0,2,0):Frame<180?FVector(0,5,0):Frame<240?FVector(2,0,0)
      :Frame<300?FVector::ZeroVector:FVector(0,-2,0);
     if(Frame==330)Delta.Z=1000;
     Sample->SetActorLocation(Sample->GetActorLocation()+Delta);
     Animation->UpdateVRLowerBody(1.f/90.f);
     for(int32 Part=0;Part<9;++Part)
     {
      const auto Pose=Animation->GetPartMesh(Part)->GetRelativeTransform();
      if(Variant==0)GaitReference.Add(Pose);
      else TestTrue(TEXT("Reused VR gait matches every original lower-body pose"),Pose.Equals(GaitReference[Frame*9+Part],.00001));
     }
    }
    const auto* Storage=Animation->VRGaitPoseScratch.GetData();
    for(int Phase=0;Phase<3;++Phase)
    {
     const double Begin=FPlatformTime::Seconds();
     for(int Frame=0;Frame<3000;++Frame)
     {
      Sample->SetActorLocation(Sample->GetActorLocation()+FVector(0,4,0));
      Animation->UpdateVRLowerBody(1.f/90.f);
     }
     AddInfo(FString::Printf(TEXT("VR gait %08X reuse=%d phase %d: %.3f us/update"),Setup,Variant,Phase,
      (FPlatformTime::Seconds()-Begin)*1.e6/3000));
    }
    if(Variant==1)
    {
     TestNotNull(TEXT("VR gait retains its warmed pose buffer"),Storage);
     TestTrue(TEXT("VR gait does not reallocate steady-state pose storage"),Storage==Animation->VRGaitPoseScratch.GetData());
    }
    Sample->Destroy();
   }
  }
 }
 {
  auto* Part=Cast<UProceduralMeshComponent>(App->GetPartMesh(9));Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  const auto Original=Part->GetRelativeTransform();auto Moved=Original;Moved.AddToTranslation(FVector(0,0,5));
  Actor->SetActorHiddenInGame(true);App->ApplyPartTransform(9,Moved);
  TestTrue(TEXT("Hidden non-colliding parts avoid render transform updates"),Part->GetRelativeTransform().Equals(Original));
  Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);App->ApplyPartTransform(9,Moved);
  TestTrue(TEXT("Hidden collision geometry continues to follow its animation"),Part->GetRelativeTransform().Equals(Moved));
  Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Actor->SetActorHiddenInGame(false);App->ApplyPartTransform(9,Original);
  TestTrue(TEXT("Visible parts immediately accept the current pose"),Part->GetRelativeTransform().Equals(Original));
 }
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
    const FTransform Frame=ACEVRMath::BodyFromHead(H,HeadBind,FVector(0,-8,17),Scale);
    Upper=App->UpdateVRUpperBody(Frame,H,Left,Right,Tracked,Tracked,Dt);
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
 auto* VR=NewObject<UACEVRComponent>(Pawn);Pawn->AddInstanceComponent(VR);VR->RegisterComponent();
 VR->PC=PC;VR->Client=Client;VR->Settings=NewObject<UACEVRSettings>();VR->bTracking=true;
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
   App->ClearJumpMotionIfAny(true);App->TickComponent(Dt,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Landing replaces unfinished and held airborne motion with its recovery link"),App->ActionFromCommand==0x40000015u && App->bActionUsesStateTransition && !App->bHoldActionFinal && !App->bHoldActionFinalAfterFinish && !App->QueuedHoldAction);
  }
 }
 // charge_jump does not replace the active Falling link/hold. Exercise the
 // desktop and VR input entry points, including key-repeat and early releases,
 // against the same DAT pose with no input as the animation reference.
 for(bool Tracked:{false,true})for(uint32 Style:{ACEMotion::StanceNonCombat,0x8000003fu,ACEMotion::StanceMagic})
 for(float Dt:{1.f/90,1.f/30})for(float Age:{.03f,.5f})
 {
  auto* ReferenceActor=World->SpawnActor<AACEWorldEntityActor>();ReferenceActor->InitializeFromObject(Obj,100,false);
  auto* Reference=ReferenceActor->Appearance.Get();
  TestTrue(TEXT("Jump spam reference avatar builds"),Reference->ApplyWorldObject(Obj,100,false));
  VR->bActive=Tracked;VR->bJumpHeld=false;
  App->ClearJumpMotionIfAny();App->SetPreferredStyle(Style);App->SetLocomotionInput(1,0,true,1);
  Reference->SetPreferredStyle(Style);Reference->SetLocomotionInput(1,0,true,1);
  PC->bJumpAirborne=false;PC->bJumpCharging=true;PC->JumpChargeExtent=.8f;PC->bStandingJumpLocked=false;
  PC->ReleaseJump(1,0);Reference->PlayActionMotion(0x40000015u,1,Style);
  for(int Frame=0;Frame<FMath::CeilToInt(Age/Dt);++Frame)
  {
   App->TickComponent(Dt,LEVELTICK_All,nullptr);Reference->TickComponent(Dt,LEVELTICK_All,nullptr);
  }
  const auto Velocity=PC->JumpWorldAceVelocity;
  PC->JumpAirborneSeconds=Age;
  bool Continuous=true,SamePose=true,SameFlight=true;
  auto Press=[&](){if(Tracked)VR->JumpDown();else PC->JumpPressed();};
  auto Release=[&](){if(Tracked)VR->JumpUp();else PC->JumpReleased();};
  for(int Tap=0;Tap<16;++Tap)
  {
   const float Time=App->AnimTime,Blend=App->StanceBlendAlpha;
   const bool Held=App->bHoldActionFinal,HoldPending=App->bHoldActionFinalAfterFinish;
   Press();PC->JumpChargeExtent=.2f;Press(); // OS/controller duplicate press.
   Continuous &= App->ActionCommand==0x40000015u && App->AnimTime==Time && App->StanceBlendAlpha==Blend
    && App->bHoldActionFinal==Held && App->bHoldActionFinalAfterFinish==HoldPending && PC->JumpChargeExtent==.2f;
   Release();Release();
   SameFlight &= PC->bJumpAirborne && PC->JumpWorldAceVelocity.Equals(Velocity) && PC->JumpAirborneSeconds==Age;
   App->TickComponent(Dt,LEVELTICK_All,nullptr);Reference->TickComponent(Dt,LEVELTICK_All,nullptr);
   Continuous &= App->ActionCommand==0x40000015u && App->AnimTime==Reference->AnimTime;
   // Once the takeoff blend has finished, every rendered part must match the
   // uninterrupted reference, including the held final frame before landing.
   if(Tap>=6)for(int Part=0;Part<App->GetPartCount();++Part)
    SamePose &= App->GetPartMesh(Part)->GetRelativeTransform().Equals(Reference->GetPartMesh(Part)->GetRelativeTransform(),.001);
  }
  const FString Case=FString::Printf(TEXT("vr=%d stance=%08X dt=%.3f age=%.2f"),Tracked,Style,Dt,Age);
  TestTrue(*FString::Printf(TEXT("Midair jump spam preserves the animation clock and hold (%s)"),*Case),Continuous);
  TestTrue(*FString::Printf(TEXT("Midair jump spam matches uninterrupted rendered poses (%s)"),*Case),SamePose);
  TestTrue(*FString::Printf(TEXT("Midair jump spam never replaces the current flight (%s)"),*Case),SameFlight);
  Press();PC->JumpChargeExtent=.6f;
  PC->bJumpAirborne=false;App->ClearJumpMotionIfAny(true); // HitGround animation handoff.
  TestTrue(TEXT("Landing keeps a held follow-up charge"),PC->bJumpCharging && PC->JumpChargeExtent==.6f);
  Release();
  TestTrue(TEXT("Release after landing starts a fresh airborne animation"),PC->bJumpAirborne && App->ActionCommand==0x40000015u && App->AnimTime==0.f && !App->bHoldActionFinal);
  App->ClearJumpMotionIfAny();ReferenceActor->Destroy();
 }
 // Grounded jump charging must still cancel a held retail emote.
 VR->bActive=false;PC->bJumpAirborne=false;PC->bJumpCharging=false;
 App->SetHeldActionMotion(ACEMotion::Sleeping);PC->JumpPressed();
 TestTrue(TEXT("Grounded jump charging starts the authored emote exit"),App->ActionCommand==ACEMotion::Ready
  && App->ActionFromCommand==ACEMotion::Sleeping && !App->bHoldActionFinal && PC->bJumpCharging);
 PC->UnPossess();Pawn->Destroy();PC->Destroy();Session->State=EACESessionState::Disconnected;
 return !HasAnyErrors();
}
#endif
