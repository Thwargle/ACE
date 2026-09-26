#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "VR/ACEVRPose.h"
#include "ACECharacterAppearanceComponent.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "Components/CapsuleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEBinaryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEMovementReviewTest,"ACE.RetailParity.MovementReview",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEMovementReviewTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
 // Spike Strafe (1842, projectile weenie 7278) shares the sword gfx with
 // ordinary equipment. Resting 101 points its blade backwards; retail applies
 // PhysicsDesc placement 0 for flight. Other ring setups must retain their
 // authored multi-part placement rather than receive a blanket 180-degree fix.
 for(uint32 Setup:{0x0200087Du,0x02000885u,0x02000986u})
 {
  FACEWorldObject Spell;Spell.Guid=12880;Spell.SetupId=Setup;
  Spell.PhysicsState=ACEPhysicsState::Missile|ACEPhysicsState::AlignPath;
  Spell.bHasPosition=true;Spell.Position.CellId=0x7D640019;Spell.Position.Location=FVector(50,100,50);
  auto* Projectile=World->SpawnActor<AACEWorldEntityActor>();Projectile->InitializeFromObject(Spell,100,true);
  TestEqual(TEXT("Ring/wall projectile honors network default placement"),Projectile->Appearance->AppliedPlacementId,0);
  const auto* Authored=Dat->GetOrBuildSetupMesh(Setup,100,0);
  TestNotNull(TEXT("Real spell setup can be built"),Authored);
  if(Authored)TestEqual(TEXT("Real projectile part meshes exist"),Projectile->Appearance->PartMeshes.Num(),Authored->Parts.Num());
  if(Authored)for(int32 Part=0;Part<Projectile->Appearance->PartMeshes.Num();++Part)
  {
   FTransform Pose;Projectile->Appearance->GetPartCurrentTransform(Part,Pose);
   TestTrue(TEXT("Every projectile part retains its authored default frame"),Authored->Parts.IsValidIndex(Part)&&Pose.Equals(Authored->Parts[Part].BindTransform,.001));
  }
  for(FVector Velocity:{FVector(0,4,0),FVector(4,0,0),FVector(-4,-4,1),FVector(3,2,-1)})
  {
   Projectile->ApplyPhysicsVelocity(Velocity);Projectile->Tick(1.f/90);
   const FVector Direction=FACEPosition::AceVectorToUnreal(Velocity,1).GetSafeNormal();
   TestTrue(TEXT("Projectile +Y aligns with travel, including pitch"),FVector::DotProduct(Projectile->GetActorQuat().RotateVector(FVector::YAxisVector),Direction)>.999);
   if(Setup==0x0200087D)
   {
    auto* Part=Projectile->Appearance->GetPartMesh(0);
    TestTrue(TEXT("Spike Strafe's actual blade points tip-first"),Part&&FVector::DotProduct(Part->GetComponentTransform().TransformVectorNoScale(FVector::UpVector),Direction)>.999);
   }
  }
  const FQuat Before=Projectile->GetActorQuat();
  Projectile->ApplyPhysicsVelocity(FVector(0,4,0),FVector(2,0,0));Projectile->Tick(.1f);
  TestFalse(TEXT("Authored spinning projectile omega is preserved"),Projectile->GetActorQuat().Equals(Before,.001));
  Projectile->Destroy();
 }
 // Academy Cestus (weenie 12753) has authored hand frames 1/2, but no frame 7.
 // Verify the rendered pose, not just an enum mapping: a missing frame silently
 // falls back to the setup's unrotated default and leaves the offhand hanging.
 for(int32 Hand:{1,8})for(int32 SuppliedPlacement:{0,1,2,101})
 {
  if((Hand==1 && SuppliedPlacement==2) || (Hand==8 && SuppliedPlacement==1))continue;
  FACEWorldObject Weapon;Weapon.SetupId=0x0200061D;Weapon.ParentGuid=12345;
  Weapon.ParentLocation=Hand;Weapon.PlacementId=SuppliedPlacement;Weapon.ItemType=ACEItemType::MeleeWeapon;
  auto* Owner=World->SpawnActor<AActor>();auto* Held=NewObject<UACECharacterAppearanceComponent>(Owner);
  Owner->AddInstanceComponent(Held);Held->RegisterComponent();
  TestTrue(TEXT("Academy Cestus builds as held equipment"),Held->ApplyWorldObject(Weapon,100,false));
  const auto* Expected=Dat->GetOrBuildSetupMesh(Weapon.SetupId,100,Hand==1?1:2);
  FTransform Actual;
  TestTrue(TEXT("Academy Cestus uses the correct authored hand frame"),Expected && Held->GetPartCurrentTransform(0,Actual)
   && Actual.Equals(Expected->Parts[0].BindTransform,.001f));
  Owner->Destroy();
 }
 FACEWorldObject Obj;Obj.Guid=12345;Obj.SetupId=0x02000001;Obj.MotionTableId=0x09000001;
 Obj.ItemType=ACEItemType::Creature;Obj.Name=TEXT("Missile animation fixture");
 auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Obj,100,true);
 auto* App=Actor->Appearance.Get();
 // Real DAT poses must keep progressing at the old 40/80 m LOD boundaries,
 // including low headset frame rates and sparse server position corrections.
 {
  auto* PC=World->SpawnActor<APlayerController>();
  PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
  PC->PlayerCameraManager->InitializeFor(PC);
  TestNotNull(TEXT("Animation LOD test has a camera"),PC->PlayerCameraManager.Get());
  const FVector Eye=PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
  Actor->bIsPlayer=true; App->SetLocomotionInput(1,0,true,1);
  for(float Distance:{3900.f,4100.f,8100.f,15000.f})for(float Dt:{1.f/90,1.f/50,1.f/15})
  {
   Actor->SetActorLocation(Eye+FVector(Distance,0,0));
   App->DeferredPoseDeltaTime=0;App->AnimTime=0;
   int Updates=0;float Previous=0;
   const int Frames=FMath::RoundToInt(1.f/Dt);
   for(int I=0;I<Frames;++I)
   {
    App->TickComponent(Dt,LEVELTICK_All,nullptr);
    if(App->AnimTime!=Previous)++Updates;
    Previous=App->AnimTime;
   }
   TestTrue(TEXT("Distant walking players still receive frequent poses"),Updates>=FMath::Min(15,Frames));
   TestTrue(TEXT("Animation clock advances a full second, independent of frame-number masks"),FMath::IsNearlyEqual(App->AnimTime,Frames*Dt,.04f));
  }
  PC->PlayerCameraManager->Destroy();PC->Destroy();Actor->bIsPlayer=false;Actor->SetActorLocation(FVector::ZeroVector);
 }
 {
  auto* Walker=World->SpawnActor<AACEWorldEntityActor>();Walker->bIsPlayer=true;Walker->bClampToGround=false;
  FACEPosition P;P.CellId=0x7D640019;P.Location=FVector(50,50,100);P.RotationW=1;P.RotationXYZ=FVector::ZeroVector;
  Walker->ApplyACEPosition(P);Walker->RemoteMotion.bMoving=true;Walker->RemoteMotion.Forward=1;Walker->RemoteMotion.ForwardUnitsPerSecond=4;
  const FVector Start=Walker->GetActorLocation();
  for(int I=0;I<150;++I)Walker->Tick(.01f);
  TestTrue(TEXT("A late 1 Hz packet does not stop a walking player after 1.15 seconds"),FVector::Dist2D(Walker->RemotePredictLocation,Start)>590);
  const FQuat Before=Walker->GetActorQuat();P.RotationW=FMath::Cos(PI/4);P.RotationXYZ=FVector(0,0,FMath::Sin(PI/4));
  Walker->ApplyACEPosition(P);
  TestTrue(TEXT("Incoming walking heading does not snap the displayed actor"),Walker->GetActorQuat().Equals(Before,.001));
  Walker->Tick(.01f);
  TestTrue(TEXT("Heading starts blending on the following frame"),!Walker->GetActorQuat().Equals(Before,.001) && !Walker->GetActorQuat().Equals(P.ToUnrealQuat(),.001));
  for(int I=0;I<1000;++I)Walker->Tick(.01f);
  TestTrue(TEXT("A missing stop packet cannot extrapolate forever"),FVector::Dist(Walker->RemotePredictLocation,Walker->RemoteAnchorLocation)<=801);
  P.Location.X+=100;Walker->ApplyACEPosition(P);
  TestTrue(TEXT("Large teleport still snaps immediately"),Walker->GetActorLocation().Equals(P.ToUnrealLocation(100),.01));
  Walker->Destroy();
 }
 // Sparse server positions must not leave remote feet on a horizontal shelf.
 // Exercise the actor presentation, collision and tracked-root path on a slope.
 {
  FACEPosition P; P.CellId=0x016C0101; P.Location=FVector(50,50,100);
  const FVector Origin=P.ToUnrealLocation(100);
  auto* FloorActor=World->SpawnActor<AActor>();
  auto* Floor=NewObject<UProceduralMeshComponent>(FloorActor); FloorActor->SetRootComponent(Floor); Floor->RegisterComponent();
  Floor->SetCollisionObjectType(ECC_WorldStatic); Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Floor->SetCollisionResponseToAllChannels(ECR_Block);
  Floor->CreateMeshSection_LinearColor(0,{FVector(-1000,-1000,400),FVector(1000,-1000,400),FVector(1000,1000,-400),FVector(-1000,1000,-400)},
   {0,1,2,0,2,3},{},{},{},{},true);
  FloorActor->SetActorLocation(Origin);
  for(bool Grounded:{false,true})
  {
   FACEWorldObject Spawn;Spawn.Guid=12344;Spawn.SetupId=0x02000001;Spawn.ItemType=ACEItemType::Creature;Spawn.bIsPlayer=true;
   Spawn.bHasPosition=true;Spawn.Position=P;Spawn.Position.bIsGrounded=Grounded;
   Spawn.bHasVelocity=true;Spawn.Velocity=FVector(0,4,Grounded?0:3);
   auto* Walker=World->SpawnActor<AACEWorldEntityActor>();Walker->InitializeFromObject(Spawn,100,false);
   TestEqual(TEXT("Grounded position wins over the descriptor velocity exactly once"),Walker->bHavePhysicsVelocity,!Grounded);
   Walker->RemoteMotion.bMoving=true;Walker->RemoteMotion.Forward=1;Walker->RemoteMotion.ForwardUnitsPerSecond=4;
   for(int I=0;I<90;++I)Walker->Tick(1.f/90);
   const FVector L=Walker->GetActorLocation()-Origin;
   TestTrue(TEXT("A grounded velocity-bearing spawn follows the slope; a real jump stays airborne"),
    Grounded ? FMath::Abs(L.Z+.4*L.Y-1)<1.5 : L.Z>200);
   Walker->Destroy();
  }
  for(bool VR:{false,true})for(bool Player:{false,true})for(int Frames:{30,90,144})
  {
   auto* Walker=World->SpawnActor<AACEWorldEntityActor>();Walker->bIsPlayer=Player;Walker->ItemType=ACEItemType::Creature;
   Walker->ApplyACEPosition(P);Walker->RemoteMotion.bMoving=true;Walker->RemoteMotion.Forward=1;Walker->RemoteMotion.ForwardUnitsPerSecond=4;
   const float Dt=1.f/Frames;double MaxError=0;
   for(int I=1;I<=Frames*2;++I)
   {
    if(VR)
    {
     FACEVRPose Pose;Pose.Version=2;Pose.Root=(Origin+FVector(0,I*Dt*400, -FMath::FloorToFloat(I*Dt)*160))/100;
     Walker->ApplyRemoteVRRoot(Pose,Dt);
    }
    else Walker->Tick(Dt);
    const FVector L=Walker->GetActorLocation()-Origin;
    MaxError=FMath::Max(MaxError,FMath::Abs(L.Z-(-.4*L.Y+1)));
   }
   TestTrue(FString::Printf(TEXT("%s %s follows downhill ground every frame at %d FPS (max %.3fcm)"),
    VR?TEXT("tracked"):TEXT("ordinary"),Player?TEXT("player"):TEXT("creature"),Frames,MaxError),MaxError<1.5);
   TestTrue(TEXT("Remote keeps walking downhill between sparse packets"),Walker->GetActorLocation().Y-Origin.Y>650);
   Walker->Destroy();
  }
  auto* Walker=World->SpawnActor<AACEWorldEntityActor>();Walker->bIsPlayer=true;Walker->bClampToGround=false;
  Walker->ApplyACEPosition(P);
  Walker->RemoteMotion.MovementType=7;Walker->RemoteMotion.bHaveMoveToTarget=true;
  Walker->RemoteMotion.MoveToCellId=P.CellId;Walker->RemoteMotion.MoveToLocalAce=P.Location+FVector(-5,0,0);
  const FQuat Before=Walker->GetActorQuat();Walker->Tick(1.f/90);
  const double Turn=FMath::RadiansToDegrees(Before.AngularDistance(Walker->GetActorQuat()));
  TestTrue(TEXT("MoveTo direction changes blend instead of snapping 90 degrees"),Turn>0 && Turn<20);
  Walker->RemoteMotion=FACEObjectMotionState();Walker->ApplyACEPosition(P);
  Walker->ApplyPhysicsVelocity(FVector(4,0,2));const FQuat JumpFacing=Walker->RemotePredictRotation;
  FACEPosition Correction=P;Correction.Location.X+=1;Correction.bHasVelocity=true;Correction.Velocity=FVector(4,0,2);
  const FVector BeforeJump=Walker->GetActorLocation();Walker->ApplyACEPosition(Correction);Walker->Tick(1.f/90);
  TestTrue(TEXT("Velocity-bearing position correction is smoothed"),FVector::Distance(Walker->GetActorLocation(),BeforeJump)<30);
  TestTrue(TEXT("Strafing/jumping velocity does not rotate the model to face sideways"),Walker->RemotePredictRotation.Equals(JumpFacing,.001));
  Walker->bClampToGround=true;Walker->ApplyACEPosition(P);Walker->ApplyPhysicsVelocity(FVector(0,0,4));
  FVector Airborne=Origin+FVector(0,0,100);Walker->ClampLocationToGround(Airborne);
  TestTrue(TEXT("Ground following does not pull an airborne creature down"),Airborne.Equals(Origin+FVector(0,0,100),.01));
  Walker->Destroy();FloorActor->Destroy();
 }
 {
  auto Session=GI->GetSubsystem<UACEClientSubsystem>()->GetSession();
  FACEWorldObject Remote;Remote.Guid=12899;Remote.bHasPhysicsTimestamps=true;
  Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=2;Remote.PhysicsTimestamps[ACEPhysicsTimeStamp::Vector]=65534;
  Session->WorldObjects.Add(Remote.Guid,Remote);int Updates=0;
  const auto Handle=Session->OnVectorUpdate.AddLambda([&](int32,const FVector&,const FVector&){++Updates;});
  auto Receive=[&](uint16 Instance,uint16 Sequence) {
   FACEBinaryWriter W;W.WriteUInt32(Remote.Guid);for(float V:{1.f,2.f,3.f,0.f,0.f,0.f})W.WriteFloat(V);
   W.WriteUInt16(Instance);W.WriteUInt16(Sequence);FACEBinaryReader R(W.GetData());Session->HandleVectorUpdate(R);
  };
  Receive(2,65535);Receive(2,0);Receive(2,65535);Receive(2,0);Receive(1,1);
  TestEqual(TEXT("Vector wrap accepted; duplicate, stale and old-incarnation velocities rejected"),Updates,2);
  Session->OnVectorUpdate.Remove(Handle);Session->WorldObjects.Remove(Remote.Guid);
 }
 // The local pawn survives the death teleport; it is not recreated like a corpse.
 {
  auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
  auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
  auto* Pawn=World->SpawnActor<APawn>();auto* Local=NewObject<UACECharacterAppearanceComponent>(Pawn);
  Pawn->AddInstanceComponent(Local);Local->RegisterComponent();PC->Possess(Pawn);
  FACEWorldObject Player=Obj;Player.bIsPlayer=true;Player.bIsSelf=true;
  Local->ApplyWorldObject(Player,100,false);
  Session->PlayerGuid=Player.Guid;Session->State=EACESessionState::InWorld;
  PC->bUsePortalTransitionOnTeleport=false;
  for(float Elapsed:{.1f,10.f})
  {
   Local->PlayActionMotion(ACEMotion::Dead,1,ACEMotion::StanceNonCombat);
   Local->TickComponent(Elapsed,LEVELTICK_All,nullptr);
   PC->HandlePlayerTeleportStarted();
   TestTrue(TEXT("Respawn releases both playing and held death poses"),Local->AnimMode==UACECharacterAppearanceComponent::EACEAnimMode::Locomotion);
   TestEqual(TEXT("Respawn discards the death command"),Local->ActionCommand,0u);
  }
  // Ordinary stance/motion echoes during death must not resurrect the local body.
  Local->PlayActionMotion(ACEMotion::Dead,1,ACEMotion::StanceNonCombat);
  FACEObjectMotionState Ready;Ready.CurrentStyle=ACEMotion::StanceNonCombat;Ready.ForwardCommand=ACEMotion::Ready;
  PC->HandleMotionUpdate(Player.Guid,Ready);
  TestEqual(TEXT("A late idle echo cannot stand the player up before respawn"),Local->ActionCommand,ACEMotion::Dead);
  Session->State=EACESessionState::Disconnected;Session->PlayerGuid=0;
  PC->UnPossess();Pawn->Destroy();PC->Destroy();
 }
 // Delayed movement acknowledgements arrive after mouse/keyboard turns, including
 // after the player releases the controls and prediction relinquishes ownership.
 {
  auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
  auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
  auto* Pawn=World->SpawnActor<APawn>();auto* Root=NewObject<USceneComponent>(Pawn);
  Pawn->SetRootComponent(Root);Root->RegisterComponent();PC->Possess(Pawn);
  PC->bUsePortalTransitionOnTeleport=false;PC->bUseEnterWorldLoadScreen=false;
  FACEWorldObject Self=Obj;Self.bIsPlayer=true;Self.bIsSelf=true;
  Self.bHasPosition=true;Self.Position.CellId=0x7D640019;Self.Position.Location=FVector(50,100,50);
  Session->WorldObjects.Add(Self.Guid,Self);Session->PlayerGuid=Self.Guid;Session->State=EACESessionState::InWorld;
  uint16 Sequence=0;
  const auto Handle=Session->OnPositionUpdate.AddLambda([PC](int32 Guid,const FACEPosition& Pose){PC->HandlePositionUpdate(Guid,Pose);});
  auto Receive=[&](const FACEPosition& Pose,uint16 Teleport,uint16 Force)
  {
   FACEBinaryWriter Writer;
   Writer.WriteUInt32(Self.Guid);Writer.WriteUInt32(4);Writer.WriteUInt32(Pose.CellId);
   Writer.WriteFloat(Pose.Location.X);Writer.WriteFloat(Pose.Location.Y);Writer.WriteFloat(Pose.Location.Z);
   Writer.WriteFloat(Pose.RotationW);Writer.WriteFloat(Pose.RotationXYZ.X);Writer.WriteFloat(Pose.RotationXYZ.Y);Writer.WriteFloat(Pose.RotationXYZ.Z);
   Writer.WriteUInt16(0);Writer.WriteUInt16(++Sequence);Writer.WriteUInt16(Teleport);Writer.WriteUInt16(Force);
   FACEBinaryReader Reader(Writer.GetData());Session->HandleUpdatePosition(Reader);
  };
  uint16 Force=0;
  for(bool Predicting:{false,true})for(bool Forced:{false,true})for(float Heading:{45.f,179.f,-179.f})
  {
   FACEPosition Local=Self.Position;Local.SetAceFacingFromUnrealDir2D(FACEPosition::UnrealDirFromAceHeadingDegrees(Heading));
   Pawn->SetActorLocation(Local.ToUnrealLocation(100)+FVector(0,0,88));Pawn->SetActorRotation(Local.ToUnrealQuat());
   PC->PredictedPose=Local;PC->bHavePredictedPose=Predicting;PC->bLocalPredicting=Predicting;
   Session->PlayerPosition=Local;
   FACEPosition Delayed=Self.Position;Delayed.Location+=FVector(.3f,.2f,-.1f);
   if(Forced)++Force;
   Receive(Delayed,0,Force);
   TestTrue(TEXT("Delayed position packet preserves current local facing after mouse or keyboard turning"),
    Client->GetPlayerPosition().ToUnrealQuat().Equals(Local.ToUnrealQuat(),.00001f));
   TestTrue(TEXT("Delayed packet does not rotate the camera's pawn"),Pawn->GetActorQuat().Equals(Local.ToUnrealQuat(),.00001f));
   TestTrue(TEXT("Server anchor remains intact for positional reconciliation"),PC->LastServerPose.ToUnrealLocation(100).Equals(Delayed.ToUnrealLocation(100),.01));
   if(Forced)
   {
    TestTrue(TEXT("Forced correction still applies authoritative XYZ"),PC->PredictedPose.ToUnrealLocation(100).Equals(Delayed.ToUnrealLocation(100),.01));
    TestTrue(TEXT("Forced correction still moves the body"),Pawn->GetActorLocation().Equals(Delayed.ToUnrealLocation(100)+FVector(0,0,88),.01));
   }
  }
  FACEPosition Arrival=Self.Position;Arrival.SetAceFacingFromUnrealDir2D(FVector(1,0,0));
  Receive(Arrival,1,++Force);
  TestTrue(TEXT("Teleport applies destination heading even when force sequence also changes"),
   Pawn->GetActorQuat().Equals(Arrival.ToUnrealQuat(),.00001f) && Client->GetPlayerPosition().ToUnrealQuat().Equals(Arrival.ToUnrealQuat(),.00001f));
  FACEObjectMotionState Turn;Turn.MovementType=9;Turn.MoveToDesiredHeading=123.f;
  PC->HandleMotionUpdate(Self.Guid,Turn);
  FACEPosition Expected=Arrival;Expected.SetAceFacingFromUnrealDir2D(FACEPosition::UnrealDirFromAceHeadingDegrees(123.f));
  TestTrue(TEXT("Explicit server TurnToHeading still rotates the player"),Pawn->GetActorQuat().Equals(Expected.ToUnrealQuat(),.00001f));
  Session->OnPositionUpdate.Remove(Handle);Session->WorldObjects.Remove(Self.Guid);
  Session->State=EACESessionState::Disconnected;Session->PlayerGuid=0;Session->TeleportSeq=0;Session->ForcePositionSeq=0;
  PC->UnPossess();Pawn->Destroy();PC->Destroy();
 }
 for (uint32 Style : {0x8000003Fu,0x80000041u,0x80000047u})
 {
  for (uint32 Action : {0x40000016u,0x4000001Eu,0x40000020u,0x100000D0u})
  {
   App->CancelHeldActionMotion();App->SetPreferredStyle(Style);
   App->PlayActionMotion(Action,1.f,Style);
   for(int32 Frame=0;Frame<300;++Frame)
   {
    App->SetLocomotionInput(1,0,true,1);
    App->TickComponent(1.f/60,LEVELTICK_All,nullptr);
   }
   TestTrue(FString::Printf(TEXT("Missile action %08X stance %08X returns to locomotion"),Action,Style),App->AnimMode!=UACECharacterAppearanceComponent::EACEAnimMode::ActionOneShot);
   if(Action==0x4000001Eu || Action==0x40000016u)
    TestTrue(TEXT("Missile gesture played authored frames before completing"),App->bActionEverEvaluated);
  }
 }
 for(uint32 Action : {0x4000001Au,0x4000001Bu})
 {
  App->CancelHeldActionMotion();App->SetPreferredStyle(0x8000003Du);
  App->PlayActionMotion(Action,1.f,0x8000003Du);
  for(int32 Frame=0;Frame<300;++Frame)App->TickComponent(1.f/60,LEVELTICK_All,nullptr);
  TestTrue(TEXT("Eating/drinking plays authored frames"),App->bActionEverEvaluated);
  TestTrue(TEXT("Eating/drinking finishes without movement input"),App->AnimMode!=UACECharacterAppearanceComponent::EACEAnimMode::ActionOneShot);
 }
 // Exercise authored monster missile links without movement input masking a held pose.
 int32 MissileFixtures=0;
 for(uint32 TableId=0x09000001;TableId<0x09000200;++TableId)
 {
  TArray<uint8> Bytes;if(!Dat->GetPortalDat()->ReadFile(TableId,Bytes))continue;
  FACEDatCursor Cursor(Bytes);FACEDatMotionTable Table;
  if(!ACEDatUnpack::UnpackMotionTable(Cursor,Table))continue;
  TSet<uint32> MissileKeys;
  for(const auto& Cycle:Table.Cycles)
   if((Cycle.Key&0xffff)==0x16 || ((Cycle.Key&0xffff)>=0x1e && (Cycle.Key&0xffff)<=0x2a)) MissileKeys.Add(Cycle.Key);
  for(const auto& Links:Table.Links)
  {
   if((Links.Key&0xffff)!=3)continue;
   for(const auto& Link:Links.Value)
    if(Link.Key==0x40000016 || (Link.Key>=0x4000001e && Link.Key<=0x4000002a))
     MissileKeys.Add((Links.Key&0xffff0000)|(Link.Key&0xffff));
  }
  for(uint32 Key:MissileKeys)
   {
    const uint32 Style=0x80000000u|(Key>>16), Action=0x40000000u|(Key&0xffff);
    ++MissileFixtures;
    App->AnimMode=UACECharacterAppearanceComponent::EACEAnimMode::Locomotion;App->ActionCommand=0;
    App->MotionTableId=TableId;App->SetPreferredStyle(Style);App->SetLocomotionInput(0,0,false,1);
    App->PlayActionMotion(Action,1,Style);
    for(int32 Frame=0;Frame<450;++Frame)App->TickComponent(1.f/30,LEVELTICK_All,nullptr);
    FACEObjectMotionState Ready;Ready.ForwardCommand=ACEMotion::Ready;Ready.CurrentStyle=Style;
    Actor->ApplyMotionState(Ready);
    App->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestTrue(FString::Printf(TEXT("Server Ready releases missile %08X table %08X stance %08X"),Action,TableId,Style),
     App->AnimMode!=UACECharacterAppearanceComponent::EACEAnimMode::ActionOneShot);
   }
 }
 AddInfo(FString::Printf(TEXT("Checked %d authored missile links"),MissileFixtures));
 TestTrue(TEXT("Missile regression includes creature tables"),MissileFixtures>10);
 App->AnimMode=UACECharacterAppearanceComponent::EACEAnimMode::Locomotion;App->ActionCommand=0;App->MotionTableId=Obj.MotionTableId;
 auto* Presenter=NewObject<UACEWorldPresenterComponent>(Actor);
 Presenter->Client=GI->GetSubsystem<UACEClientSubsystem>();Presenter->Spawned.Add(Obj.Guid,Actor);
 FACEObjectMotionState Eat;Eat.ActionCommand=0x4000001A;Eat.ActionSpeed=1;Eat.CurrentStyle=0x8000003D;
 Presenter->HandleMotionUpdate(Obj.Guid,Eat);
 for(int32 Frame=0;Frame<300;++Frame)App->TickComponent(1.f/60,LEVELTICK_All,nullptr);
 const uint64 Revision=App->GetAppearanceRevision();
 for(int32 I=0;I<10;++I)Presenter->SpawnOrUpdateEntity(Obj,true);
 TestEqual(TEXT("Repeated identical object updates do not rebuild appearance"),App->GetAppearanceRevision(),Revision);
 TestTrue(TEXT("Descriptor echoes do not restart a completed eating animation"),App->AnimMode!=UACECharacterAppearanceComponent::EACEAnimMode::ActionOneShot);
 Actor->SetActorLocation(FVector(40,50,100));Actor->RemotePredictLocation=Actor->GetActorLocation();Actor->RemoteAnchorLocation=Actor->GetActorLocation();Actor->bHaveRemotePredict=true;
 Actor->ApplyPhysicsVelocity(FVector::ZeroVector);
 FACEWorldObject Visual=Obj;Visual.bAppearanceOnlyUpdate=true;Visual.bHasPosition=true;Visual.Position.CellId=0x7D640019;
 Visual.bHasVelocity=true;Visual.Velocity=FVector(0,0,5);
 const FVector BeforeRefresh=Actor->GetActorLocation(), BeforePrediction=Actor->RemotePredictLocation;
 Actor->InitializeFromObject(Visual,100,false);
 TestTrue(TEXT("Equipment descriptor cannot rewind player placement"),Actor->GetActorLocation().Equals(BeforeRefresh) && Actor->RemotePredictLocation.Equals(BeforePrediction));
 TestTrue(TEXT("Equipment descriptor cannot replay cached jump velocity"),Actor->AcePhysicsVelocity.IsNearlyZero());
 App->AnimTime=.61f;const FTransform BeforePart=App->GetPartMesh(2)->GetRelativeTransform();
 FACEWorldObject Recolored=Obj;Recolored.Translucency=.01f;
 App->ApplyWorldObject(Recolored,100,false);
 TestEqual(TEXT("Clothing rebuild preserves locomotion phase"),App->AnimTime,.61f);
 TestTrue(TEXT("Clothing rebuild preserves current limb pose"),App->GetPartMesh(2)->GetRelativeTransform().Equals(BeforePart));
 auto* NewVisual=World->SpawnActor<AACEWorldEntityActor>();NewVisual->InitializeFromObject(Visual,100,false);
 TestTrue(TEXT("First appearance event still places a newly visible actor"),NewVisual->GetActorLocation().Equals(Visual.Position.ToUnrealLocation(100),.1));NewVisual->Destroy();

 Presenter->Spawned.Reset();
 FACEWorldObject Held;Held.Guid=12346;Held.SetupId=0x020000A8;Held.Name=TEXT("Equipped fixture");
 Held.ParentGuid=Obj.Guid;Held.ParentLocation=1;Held.ItemType=ACEItemType::MeleeWeapon;
 auto* Weapon=World->SpawnActor<AACEWorldEntityActor>();Weapon->InitializeFromObject(Held,100,true);
 Weapon->AttachToParentActor(Actor,1);
 TestTrue(TEXT("Weapon attaches to a real animated part"),Weapon->GetRootComponent()->GetAttachParent()==App->GetPartMesh(Weapon->HeldParentPartIndex));
 Presenter->Spawned.Add(Held.Guid,Weapon);
 int32 AttachmentTransforms=0;
 const auto TransformHandle=Weapon->GetRootComponent()->TransformUpdated.AddLambda(
  [&AttachmentTransforms](USceneComponent*,EUpdateTransformFlags,ETeleportType){++AttachmentTransforms;});
 for(int32 Frame=0;Frame<120;++Frame) Presenter->DestroySpawnedSelf(Obj.Guid);
 TestEqual(TEXT("No duplicate self means equipment never detaches or retransforms"),AttachmentTransforms,0);
 TestTrue(TEXT("Duplicate check preserves held-item visibility picking"),Weapon->CollisionProxy->GetCollisionEnabled()==ECollisionEnabled::QueryOnly);
 TestEqual(TEXT("Held item still cannot block pawn movement"),Weapon->CollisionProxy->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
 TestTrue(TEXT("Normal equipment does not enter the attachment retry queue"),Presenter->PendingAttachments.IsEmpty());
 Weapon->GetRootComponent()->TransformUpdated.Remove(TransformHandle);
 FlushRenderingCommands();
 const double Start=FPlatformTime::Seconds();
 for(int32 Frame=0;Frame<1000;++Frame) Weapon->Tick(1.f/60);
 const double StableTime=FPlatformTime::Seconds()-Start;
 TestTrue(TEXT("Equipped item remains pickable without per-frame collision setup"),Weapon->Appearance->GetPartMesh(0)!=nullptr);
 const double RepeatedStart=FPlatformTime::Seconds();
 for(int32 Frame=0;Frame<1000;++Frame)
 {
  Weapon->ConfigureAttachedPickCollision();
  for(auto Part:Weapon->Appearance->PartMeshes) if(Part) Part->RecreatePhysicsState();
 }
 AddInfo(FString::Printf(TEXT("1000 held-weapon ticks %.3f ms; previous repeated collision path %.3f ms"),StableTime*1000,(FPlatformTime::Seconds()-RepeatedStart)*1000));
 Presenter->Spawned.Add(Obj.Guid,Actor);
 Presenter->DestroySpawnedSelf(Obj.Guid);
 TestFalse(TEXT("An actual duplicate self is still removed"),Presenter->Spawned.Contains(Obj.Guid));
 TestTrue(TEXT("Duplicate's equipment is retained for attachment to the real pawn"),Presenter->Spawned.Contains(Held.Guid));
 TestTrue(TEXT("Equipment awaits the real pawn when none is possessed yet"),Presenter->PendingAttachments.Contains(Held.Guid));
 Presenter->Spawned.Reset(); Presenter->PendingAttachments.Reset();
 Weapon->Destroy();

 // Real scaled rat approaching our locally moving avatar. The old predictor
 // stopped at 60cm centre-to-centre and drew the rat inside the player's body.
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
 const auto SavedSession=Client->Session;
 Client->Session=MakeShared<FACESession>(); auto& Session=*Client->Session;
 FACEWorldObject Self;Self.Guid=100;Self.SetupId=0x02000001;Self.Scale=1.f;
 Self.bHasPosition=true;Self.Position.CellId=0x7D640019;Self.Position.Location=FVector(50,100,50);
 Session.PlayerGuid=Self.Guid;Session.PlayerPosition=Self.Position;
 Session.WorldObjects.Add(Self.Guid,Self);
 float Step,HumanHeight,HumanRadius;uint32 Anim;
 TestTrue(TEXT("Read retail human body dimensions"),Dat->TryGetSetupPhysics(Self.SetupId,Step,HumanHeight,HumanRadius,Anim));
 for(float RatScale:{2.1f,2.5f})
 {
  FACEWorldObject Rat;Rat.Guid=12348;Rat.SetupId=0x0200003D;Rat.Scale=RatScale;
  Rat.ItemType=ACEItemType::Creature;Rat.bHasPosition=true;Rat.Position=Self.Position;
  Rat.Position.Location.Y-=2.f;
  auto* RatActor=World->SpawnActor<AACEWorldEntityActor>();RatActor->InitializeFromObject(Rat,100,false);
  RatActor->bClampToGround=false;RatActor->bPendingGroundClamp=false;
  RatActor->SetActorEnableCollision(false);
  const FVector PlayerFeet=Session.PlayerPosition.ToUnrealLocation(100);
  const float ExpectedSeparation=60.f+RatActor->GetMeleeBodyRadius()+HumanRadius*100.f;
  RatActor->SetActorLocation(PlayerFeet-FVector(0,ExpectedSeparation,0));
  RatActor->RemoteAnchorLocation=RatActor->RemotePredictLocation=RatActor->GetActorLocation();
  RatActor->bHaveRemotePredict=true;
  FACEObjectMotionState Approach;Approach.MovementType=6;Approach.MoveToTargetGuid=Self.Guid;
  Approach.MoveToFlags=0x403;Approach.MoveToDistance=.6f;Approach.MoveToSpeed=1.f;
  RatActor->RemoteMotion=Approach;
  for(int32 I=0;I<60;++I)RatActor->Tick(1.f/60);
  TestTrue(TEXT("Rat stays at the server body-edge stopping distance"),FMath::Abs(FVector::Dist2D(PlayerFeet,RatActor->GetActorLocation())-ExpectedSeparation)<.1f);
  // The local player has a dedicated live position. Its object profile may
  // carry dimensions without a usable world-position echo.
  Session.WorldObjects[Self.Guid].Position=FACEPosition();
  RatActor->ApproachTargetSetup=0; RatActor->RemoteMotion=Approach;
  for(int32 I=0;I<60;++I)RatActor->Tick(1.f/60);
  TestTrue(TEXT("Self profile without a position still contributes its body radius"),FMath::Abs(FVector::Dist2D(PlayerFeet,RatActor->GetActorLocation())-ExpectedSeparation)<.1f);
  // Change only the live player pose; the world-object network echo is stale.
  Session.PlayerPosition.Location.Y+=1.f;
  const FVector NewPlayerFeet=Session.PlayerPosition.ToUnrealLocation(100);
  for(int32 I=0;I<240;++I)RatActor->Tick(1.f/60);
  TestTrue(TEXT("Rat follows the live local player pose without walking inside it"),FMath::Abs(FVector::Dist2D(NewPlayerFeet,RatActor->GetActorLocation())-ExpectedSeparation)<.2f);
  // Starting inside approach distance must still turn, without translating.
  const FVector RatFeet=RatActor->GetActorLocation();
  Session.PlayerPosition.SetLocationFromUnreal(RatFeet+FVector(30,0,0),100);
  RatActor->RemoteMotion=Approach;
  for(int32 I=0;I<60;++I)RatActor->Tick(1.f/60);
  TestTrue(TEXT("Close MoveTo faces the attacker without stepping into them"),RatActor->GetActorQuat().RotateVector(FVector::YAxisVector).X>.99 && FVector::Dist2D(RatFeet,RatActor->GetActorLocation())<.1);
  // A real interpreted swing carries StickToObject after the command list.
  Session.WorldObjects.Add(Rat.Guid,Rat);
  const auto MotionHandle=Session.OnMotionUpdate.AddLambda([&](int32 Guid,const FACEObjectMotionState& Motion){if(Guid==Rat.Guid)RatActor->ApplyMotionState(Motion);});
  auto ReceiveSticky=[&](int32 Target)
  {
   FACEBinaryWriter W;W.WriteUInt32(Rat.Guid);W.WriteUInt16(0);W.WriteUInt16(1);W.WriteUInt16(0);W.WriteUInt8(0);W.Align();
   W.WriteUInt8(0);W.WriteUInt8(Target?1:0);W.WriteUInt16(0x3c);W.WriteUInt32(0);W.Align();if(Target)W.WriteUInt32(Target);
   FACEBinaryReader R(W.GetData());Session.HandleUpdateMotion(R);
  };
  ReceiveSticky(Self.Guid);
  TestEqual(TEXT("Interpreted combat retains server facing target"),RatActor->RemoteMotion.StickyTargetGuid,Self.Guid);
  for(FVector Offset:{FVector(-30,0,0),FVector(0,-30,0),FVector(0,30,0)})
  {
   Session.PlayerPosition.SetLocationFromUnreal(RatFeet+Offset,100);
   for(int32 I=0;I<90;++I)RatActor->Tick(1.f/90);
   TestTrue(TEXT("Stationary attack tracks nearby live player, not stale object echo"),FVector::DotProduct(RatActor->GetActorQuat().RotateVector(FVector::YAxisVector),Offset.GetSafeNormal())>.99);
   TestTrue(TEXT("Combat facing does not pull monster inside player"),FVector::Dist2D(RatFeet,RatActor->GetActorLocation())<.1);
  }
  ReceiveSticky(0);TestEqual(TEXT("Next ordinary motion releases combat facing"),RatActor->RemoteMotion.StickyTargetGuid,0);
  ReceiveSticky(Self.Guid);FACEObjectMotionState Dead;Dead.ActionCommand=ACEMotion::Dead;Dead.StickyTargetGuid=Self.Guid;RatActor->ApplyMotionState(Dead);
  TestEqual(TEXT("Death stops target tracking"),RatActor->RemoteMotion.StickyTargetGuid,0);
  Session.OnMotionUpdate.Remove(MotionHandle);Session.WorldObjects.Remove(Rat.Guid);
  RatActor->Destroy();Session.PlayerPosition=Self.Position;Session.WorldObjects[Self.Guid]=Self;
 }
 FACEObjectMotionState Range;Range.MoveToFlags=0x400;
 TestEqual(TEXT("Body overlap remains a signed distance"),Range.ApproachDistance(FVector(0,.3,0),.3f,2,.3f,1),-FMath::Sqrt(4.f+.09f));
 TestEqual(TEXT("Raised targets include retail vertical separation"),Range.ApproachDistance(FVector(0,0,3),.5,2,.5,1),FMath::Sqrt(5.f));
 Range.MoveToFlags=0;
 TestEqual(TEXT("MoveTo without UseSpheres still measures point distance"),Range.ApproachDistance(FVector(0,0,3),.5,2,.5,1),3.f);
 Client->Session=SavedSession;
 // A hanging sign's initial placement honors Stuck, and so must the following
 // correction ticks. Ordinary loose items still settle onto the same floor.
 auto* FloorActor=World->SpawnActor<AActor>();
 auto* Floor=NewObject<UProceduralMeshComponent>(FloorActor);FloorActor->SetRootComponent(Floor);Floor->RegisterComponent();
 Floor->SetCollisionObjectType(ECC_WorldStatic);Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Floor->CreateMeshSection_LinearColor(0,{FVector(-1000,-1000,0),FVector(1000,-1000,0),FVector(1000,1000,0),FVector(-1000,1000,0)},
  {0,1,2,0,2,3},{},{},{},{},true);
 FACEWorldObject SignObject;SignObject.Guid=23456;SignObject.Name=TEXT("Hanging smithy sign");SignObject.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
 auto* Sign=World->SpawnActor<AACEWorldEntityActor>();Sign->InitializeFromObject(SignObject,100,false);
 Sign->LastAceCellId=0xC98C0101;Sign->bClampToGround=true;
 for (bool Stuck : {true,false})
 {
  Sign->ObjectDescriptionFlags=Stuck ? ACEObjectDescFlag::Stuck : 0;
  Sign->SetActorLocation(FVector(0,0,150));Sign->RemotePredictLocation=FVector(1,0,150);
  Sign->RemoteAnchorLocation=Sign->RemotePredictLocation;Sign->RemotePredictRotation=FQuat::Identity;Sign->bHaveRemotePredict=true;
  for(int Frame=0;Frame<30;++Frame)Sign->Tick(1.f/60);
  TestTrue(Stuck ? TEXT("Hanging sign retains its authored height during correction") : TEXT("Loose item still settles to the floor"),
   FMath::IsNearlyEqual(Sign->GetActorLocation().Z,Stuck?150.:1.,.01));
 }
 Sign->Destroy();
 // Real Rithwic Smithy data: Stuck + Gravity + HasPhysicsBSP, no motion table.
 // A vector update must not drop this sign ~7m onto the road as observed in v16.
 FACEWorldObject Smithy;Smithy.Guid=0x7C88C032;Smithy.WeenieClassId=644;Smithy.Name=TEXT("Smithy");
 Smithy.SetupId=0x02000489;Smithy.ItemType=128;Smithy.PhysicsState=0x10418;Smithy.ObjectDescriptionFlags=20;
 Smithy.bHasPosition=true;Smithy.Position.CellId=0xC88C0100;
 Smithy.Position.Location=FVector(127.837,191.683,28.9329);Smithy.Position.RotationW=0;Smithy.Position.RotationXYZ=FVector(0,0,-1);
 auto* Fixed=World->SpawnActor<AACEWorldEntityActor>();Fixed->InitializeFromObject(Smithy,100,true);
 const FVector Authored=Smithy.Position.ToUnrealLocation(100);
 FloorActor->SetActorLocation(FVector(Authored.X,Authored.Y,2200));
 TestTrue(TEXT("Smithy DAT geometry is present"),Fixed->Appearance->HasAppearance());
 Fixed->ApplyPhysicsVelocity(FVector(0,0,-.1));
 for(int Frame=0;Frame<180;++Frame) Fixed->Tick(1.f/60);
 TestTrue(TEXT("Fixed Smithy ignores ballistic gravity from vector updates"),Fixed->GetActorLocation().Equals(Authored,.01));
 FACEPosition Corrected=Smithy.Position;Corrected.Location.Z+=.25;
 Fixed->ApplyACEPosition(Corrected);
 for(int Frame=0;Frame<60;++Frame) Fixed->Tick(1.f/60);
 TestTrue(TEXT("Fixed props still accept authoritative position corrections"),Fixed->GetActorLocation().Equals(Corrected.ToUnrealLocation(100),.01));
 // Stuck is also set on creatures. Their jump physics must remain active.
 Fixed->ItemType=ACEItemType::Creature;Fixed->ApplyPhysicsVelocity(FVector(0,0,5));
 Fixed->Tick(1.f/60);
 TestTrue(TEXT("Stuck creatures still jump"),Fixed->GetActorLocation().Z>Corrected.ToUnrealLocation(100).Z);
 Fixed->ItemType=128;Fixed->PhysicsState|=ACEPhysicsState::Missile;
 const FVector MissileStart=Fixed->GetActorLocation();Fixed->ApplyPhysicsVelocity(FVector(1,0,5));Fixed->Tick(1.f/60);
 TestTrue(TEXT("Missiles still integrate velocity"),!Fixed->GetActorLocation().Equals(MissileStart,.1));
 Fixed->Destroy();
 // Town Network portal: the same authored Stuck + Gravity combination must
 // retain its elevated spawn height, including deferred appearance/echoes.
 FACEWorldObject Portal=Smithy;Portal.Guid=0x7C88C066;Portal.WeenieClassId=43065;
 Portal.Name=TEXT("Portal to Town Network");Portal.SetupId=0x020001B3;
 Portal.ItemType=65536;Portal.PhysicsState=3084;Portal.Position.Location.Z=22.198;
 auto* Floating=World->SpawnActor<AACEWorldEntityActor>();Floating->InitializeFromObject(Portal,100,false);
 Floating->ApplyPhysicsVelocity(FVector(0,0,-.1),FVector(0,0,.5));
 Floating->ApplyDatAppearanceFromObject(Portal);
 for(int Frame=0;Frame<180;++Frame) Floating->Tick(1.f/60);
 Floating->InitializeFromObject(Portal,100,false);
 for(int Frame=0;Frame<60;++Frame) Floating->Tick(1.f/60);
 TestTrue(TEXT("Floating portal retains authored height across loading and descriptor echoes"),
  Floating->GetActorLocation().Equals(Portal.Position.ToUnrealLocation(100),.01));
 // Angular velocity remains available to the appearance animation path.
 Floating->ApplyPhysicsVelocity(FVector(0,0,-.1),FVector(0,0,.5));
 TestTrue(TEXT("Fixed prop preserves authored animation omega"),Floating->GetAcePhysicsOmega().Equals(FVector(0,0,.5)));
 Floating->Destroy();
 // Actual Gnawer Shreth (weenie 4108): head/limbs exceed its .509m
 // movement radius. Those visible points must remain attackable.
 FACEWorldObject Shreth;Shreth.Guid=23457;Shreth.SetupId=0x020005C4;Shreth.MotionTableId=0x09000080;
 Shreth.ItemType=ACEItemType::Creature;Shreth.Scale=.6f;
 auto* Creature=World->SpawnActor<AACEWorldEntityActor>();Creature->InitializeFromObject(Shreth,100,true);
 TestTrue(TEXT("Gnawer DAT appearance loads"),Creature->Appearance->HasAppearance());
 int32 Extremities=0;
 for(int32 P=0;P<Creature->Appearance->GetPartCount();++P)
  if(auto* Part=Cast<UProceduralMeshComponent>(Creature->Appearance->GetPartMesh(P)))
   for(int32 S=0;S<Part->GetNumSections();++S)
    if(const auto* Section=Part->GetProcMeshSection(S);Section && Section->bSectionVisible)
     for(const auto& Vertex:Section->ProcVertexBuffer)
     {
      const FVector Point=Part->GetComponentTransform().TransformPosition(Vertex.Position);
      if(FVector(Point.X,Point.Y,0).Size()<Creature->GetMeleeBodyRadius()+18.f)continue;
      float Along;
      TestTrue(TEXT("Swing at any visible Shreth extremity is a candidate"),Creature->FindMeleeContact(Point-FVector(18,0,0),Point+FVector(18,0,0),Along));
      ++Extremities;
     }
 TestTrue(TEXT("Fixture exercises geometry outside the old hit cylinder"),Extremities>0);
 const uint64 HighlightRevision=Creature->Appearance->GetAppearanceRevision();
 Creature->SetSelectionHighlight(1.f);
 for(int32 P=0;P<Creature->Appearance->GetPartCount();++P)
  if(auto* Part=Cast<UPrimitiveComponent>(Creature->Appearance->GetPartMesh(P)))
   TestEqual(TEXT("Selected object highlights its existing parts"),Part->GetCustomPrimitiveData().Data[1],1.f);
 Creature->SetSelectionHighlight(0.f);
 TestEqual(TEXT("Highlighting never rebuilds the mesh"),Creature->Appearance->GetAppearanceRevision(),HighlightRevision);
 Creature->Destroy();
 // The actual mace's resting model must clear an inclined floor at every
 // corner, including after rotation and a small network correction.
 FloorActor->SetActorLocation(FVector::ZeroVector);
 Floor->CreateMeshSection_LinearColor(0,{FVector(-1000,-1000,-500),FVector(1000,-1000,500),FVector(1000,1000,500),FVector(-1000,1000,-500)},
  {0,1,2,0,2,3},{},{},{},{},true);
 FACEWorldObject Dropped;Dropped.Guid=23458;Dropped.SetupId=0x0200013A;
 Dropped.ItemType=ACEItemType::MeleeWeapon;Dropped.PhysicsState=ACEPhysicsState::Gravity;
 auto* Mace=World->SpawnActor<AACEWorldEntityActor>();Mace->InitializeFromObject(Dropped,100,true);
 Mace->LastAceCellId=0xC98C0101;
 TestTrue(TEXT("Dropped mace reuses its DAT appearance"),Mace->Appearance->HasAppearance());
 for(float Yaw : {0.f,45.f,90.f,170.f})
 {
  FVector Location(0,0,0);const FQuat Rotation=FRotator(0,Yaw,0).Quaternion();
  TestTrue(TEXT("Inclined floor is found"),Mace->ClampLocationToGround(Location,&Rotation));
  TestTrue(TEXT("Ground footprint is cached"),Mace->GroundSupportPoints.Num()>0);
  const FTransform Pose(Rotation,Location,Mace->GetActorScale3D());
  for(const FVector& Local:Mace->GroundSupportPoints)
  {
   const FVector Point=Pose.TransformPosition(Local);
   TestTrue(TEXT("Whole resting item clears the slope"),Point.Z>=Point.X*.5+.8);
  }
  Mace->SetActorLocationAndRotation(Location,Rotation);
  Mace->RemotePredictLocation=Location+FVector(1,0,0);Mace->RemoteAnchorLocation=Mace->RemotePredictLocation;
  Mace->RemotePredictRotation=Rotation;Mace->bHaveRemotePredict=true;
  for(int I=0;I<30;++I)Mace->Tick(1.f/60);
  for(const FVector& Local:Mace->GroundSupportPoints)
  {
   const FVector Point=Mace->GetActorTransform().TransformPosition(Local);
   TestTrue(TEXT("Position smoothing preserves slope support"),Point.Z>=Point.X*.5+.8);
  }
 }
 Mace->Destroy();FloorActor->Destroy();
 // Locate authored Arcanum stair geometry without moving retail placements.
 if(FParse::Param(FCommandLine::Get(),TEXT("ACEStairProbe")))
 {
  FString Geometry;
  for(uint32 Id=0x7D640100;Id<0x7D640174;++Id)
  {
   const auto* Cell=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Cell)continue;
   if(Id<0x7D640149 || Id>=0x7D640150) continue;
   const FTransform Frame=Cell->GetCellLocalToLandblock(100);
   for(const auto& Section:Cell->CollisionSections)
    for(int32 I=0;I+2<Section.Triangles.Num();I+=3)
    {
     Geometry+=FString::Printf(TEXT("%08X"),Id);
     for(int32 J=0;J<3;++J)
     {
      const FVector P=Frame.TransformPosition(Section.Vertices[Section.Triangles[I+J]]);
      Geometry+=FString::Printf(TEXT(",%.3f,%.3f,%.3f"),P.X,P.Y,P.Z);
     }
     Geometry+=TEXT("\n");
    }
   for(const auto& Stab:Cell->StaticObjects)
   {
    const auto* Setup=Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
    if(!Setup)continue;
    const FTransform Place(FACEPosition::AceQuatToUnreal(FQuat(Stab.Orientation)),FACEPosition::AceVectorToUnreal(FVector(Stab.Origin),100));
    for(const auto& Part:Setup->Parts) for(const auto& Section:Part.Sections)
     if(Section.bCollisionOnly) for(int32 I=0;I+2<Section.Triangles.Num();I+=3)
     {
      Geometry+=FString::Printf(TEXT("%08X/%08X"),Id,Stab.Id);
      for(int32 J=0;J<3;++J)
      {
       const FVector P=Place.TransformPosition(Part.BindTransform.TransformPosition(Section.Vertices[Section.Triangles[I+J]]));
       Geometry+=FString::Printf(TEXT(",%.3f,%.3f,%.3f"),P.X,P.Y,P.Z);
      }
      Geometry+=TEXT("\n");
     }
   }
  }
  FFileHelper::SaveStringToFile(Geometry,*(FPaths::ProjectSavedDir()/TEXT("Automation/ArcanumCollision.csv")));
 }
 // Sparse remote poses must follow the height under each independently
 // interpolated XY point, including at low frame rates.
 FVector HillStart=FVector::ZeroVector;bool FoundHill=false;
 Dat->GetOrBuildLandblockMesh(0x7D640000,100);
 for(int32 X=12;X<170 && !FoundHill;X+=12)for(int32 Y=12;Y<170 && !FoundHill;Y+=12)
 {
  FACEPosition P;P.CellId=0x7D64000C;P.Location=FVector(X,Y,12);
  FVector V=P.ToUnrealLocation(100);float A=0,B=0;
  if(Dat->SampleOutdoorGroundZ(V.X,V.Y,100,A) && Dat->SampleOutdoorGroundZ(V.X,V.Y+800,100,B)
    && FMath::Abs(B-A)>50 && FMath::Abs(B-A)<200){HillStart=V;HillStart.Z=A+1;FoundHill=true;}
 }
 TestTrue(TEXT("Regression finds an actual DAT incline"),FoundHill);
 if(FoundHill)for(float Dt:{1.f/90,1.f/15})for(float Direction:{-1.f,1.f})
 {
  FACEWorldObject Remote;Remote.Guid=290001;Remote.SetupId=0x02000001;Remote.ItemType=ACEItemType::Creature;Remote.bIsPlayer=true;
  auto* Walker=World->SpawnActor<AACEWorldEntityActor>();Walker->InitializeFromObject(Remote,100,false);
  Walker->LastAceCellId=0x7D64000C;Walker->bClampToGround=true;Walker->bPendingGroundClamp=false;
  FVector HillPose=HillStart;if(Direction<0)HillPose.Y+=800;
  float HillZ=0;Dat->SampleOutdoorGroundZ(HillPose.X,HillPose.Y,100,HillZ);HillPose.Z=HillZ+1;
  Walker->SetActorLocation(HillPose);Walker->RemotePredictLocation=Walker->RemoteAnchorLocation=HillPose;Walker->bHaveRemotePredict=true;
  Walker->RemotePredictRotation=FQuat::Identity;Walker->RemoteMotion.bMoving=true;Walker->RemoteMotion.ForwardUnitsPerSecond=4*Direction;
  for(int32 Frame=0;Frame<FMath::FloorToInt(2.f/Dt);++Frame)
  {
   Walker->Tick(Dt);const FVector Drawn=Walker->GetActorLocation();float Ground=0,PredictGround=0;
   Dat->SampleOutdoorGroundZ(Drawn.X,Drawn.Y,100,Ground);
   Dat->SampleOutdoorGroundZ(Walker->RemotePredictLocation.X,Walker->RemotePredictLocation.Y,100,PredictGround);
   TestTrue(TEXT("Remote feet stay on terrain every frame"),FMath::Abs(Drawn.Z-Ground-1)<.1);
   TestTrue(TEXT("Prediction does not reuse the displayed point's stale terrain height"),FMath::Abs(Walker->RemotePredictLocation.Z-PredictGround-1)<.1);
  }
  Walker->Destroy();
 }
 GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
