#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACECharacterAppearanceComponent.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "Components/CapsuleComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
 FACEWorldObject Obj;Obj.Guid=12345;Obj.SetupId=0x02000001;Obj.MotionTableId=0x09000001;
 Obj.ItemType=ACEItemType::Creature;Obj.Name=TEXT("Missile animation fixture");
 auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Obj,100,true);
 auto* App=Actor->Appearance.Get();
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
  RatActor->Destroy();Session.PlayerPosition=Self.Position;Session.WorldObjects[Self.Guid]=Self;
 }
 FACEObjectMotionState Range;Range.MoveToFlags=0x400;
 TestEqual(TEXT("Body overlap remains a signed distance"),Range.ApproachDistance(FVector(0,.3,0),.3,2,.3,1),-FMath::Sqrt(4.f+.09f));
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
