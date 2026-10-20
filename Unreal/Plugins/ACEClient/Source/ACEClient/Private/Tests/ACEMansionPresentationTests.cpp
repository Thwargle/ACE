#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ACEDatSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "ACEWorldEntityActor.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "ImageUtils.h"
#include "ShaderCompiler.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEMansionPresentationTest,"ACE.RetailParity.MansionPresentation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEMansionPresentationTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!TestTrue(TEXT("Retail DAT loads"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
 auto Frame=[](FVector3f P,FQuat4f Q){return FTransform(FACEPosition::AceQuatToUnreal(Q.W,FVector(Q.X,Q.Y,Q.Z)),FACEPosition::AceVectorToUnreal(FVector(P),100));};
 // Virindi Complex storage room, including repeated /reload-landblock arrivals.
 const FVector StorageOrigin=FACEPosition::AceVectorToUnreal(FVector(2*192,27*192,0),100);
 Dat->GetOrBuildEnvCellMesh(0x021B0146,100);
 auto* StorageRoom=World->SpawnActor<AACEEnvCellActor>();
 TestTrue(TEXT("Reported storage room loads"),StorageRoom->LoadEnvCell(0x021B0146,StorageOrigin,100));
 for(bool Delayed:{false,true})for(bool Velocity:{false,true})for(int32 Reload=0;Reload<3;++Reload)
 {
  StorageRoom->SetEnvCellCollisionActive(!Delayed);
  FACEWorldObject Object;Object.Guid=0x7021B030;Object.SetupId=0x02000A97;Object.MotionTableId=0x09000004;
  Object.ItemType=ACEItemType::Container;Object.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck|ACEObjectDescFlag::Openable;
  Object.PhysicsState=0x418;Object.bHasPosition=true;Object.Position.CellId=0x021B0146;
  Object.Position.Location=FVector(53.66,-33.66,-5.67);
  Object.bHasVelocity=Velocity;Object.Velocity=FVector(0,0,.5);
  auto* Chest=World->SpawnActor<AACEWorldEntityActor>();Chest->InitializeFromObject(Object,100,true);
  if(Delayed)
  {
   for(int32 I=0;I<120;++I)Chest->Tick(1.f/60);
   TestTrue(TEXT("Chest waits for unloaded floor collision"),FMath::Abs(Chest->GetActorLocation().Z+567)<.1);
   StorageRoom->SetEnvCellCollisionActive(true);
  }
  for(int32 I=0;I<180;++I)Chest->Tick(1.f/60);
  AddInfo(FString::Printf(TEXT("CHEST delayed=%d velocity=%d reload=%d Z=%.3f"),Delayed,Velocity,Reload,Chest->GetActorLocation().Z));
  TestTrue(TEXT("Storage chest settles to floor after each reload"),FMath::Abs(Chest->GetActorLocation().Z+599)<1.5);
  TestFalse(TEXT("Settled storage chest stops ticking"),Chest->IsActorTickEnabled());
  Chest->Destroy();
 }
 {
  FACEWorldObject Falling;Falling.Guid=0x7021B031;Falling.SetupId=0x02000A97;Falling.ItemType=ACEItemType::Container;
  Falling.PhysicsState=ACEPhysicsState::Gravity;Falling.bHasPosition=Falling.bHasVelocity=true;
  Falling.Position.CellId=0x021B0146;Falling.Position.Location=FVector(53.66,-33.66,20);
  auto* Prop=World->SpawnActor<AACEWorldEntityActor>();Prop->InitializeFromObject(Falling,100,true);
  const double StartZ=Prop->GetActorLocation().Z;Prop->Tick(.1f);
  TestTrue(TEXT("A loaded room without nearby support permits a prop to fall"),Prop->GetActorLocation().Z<StartZ-3);
  Prop->Destroy();
 }
 StorageRoom->Destroy();
 TestFalse(TEXT("Destroyed room releases its collision lookup"),AACEEnvCellActor::HasActiveCollision(World,0x021B0146));
 if(!FApp::CanEverRender())return !HasAnyErrors();
 constexpr uint32 Block=0xA9A10000;
 const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(169*192,161*192,0),100);
 FACEDatLandblockInfo Info;if(!Dat->LoadLandblockInfo(Block,Info))return false;
 TArray<AACEEnvCellActor*> Rooms; TArray<FVector> FloorViews;
 for(uint32 I=0;I<Info.NumCells;++I)
 {
  const auto* Cell=Dat->GetOrBuildEnvCellMesh(Block|(0x100+I),100);
  const FTransform CellFrame=Frame(Cell->Origin,Cell->Orientation)*FTransform(Origin);
  for(const auto& S:Cell->Sections)for(int32 T=0;T+2<S.Triangles.Num();T+=3)
  {
   const FVector A=CellFrame.TransformPosition(S.Vertices[S.Triangles[T]]),B=CellFrame.TransformPosition(S.Vertices[S.Triangles[T+1]]),C=CellFrame.TransformPosition(S.Vertices[S.Triangles[T+2]]);
   if(FMath::Abs(A.Z-14200)<.1 && FVector::CrossProduct(B-A,C-A).GetSafeNormal().Z<-.99) {FloorViews.Add((A+B+C)/3);break;}
  }
  auto* Room=World->SpawnActor<AACEEnvCellActor>();
  if(TestTrue(TEXT("Mansion room loads"),Room->LoadEnvCell(Block|(0x100+I),Origin,100)))Rooms.Add(Room);
  Room->SetEnvCellHiddenInGame(false);
 }
 auto* Host=World->SpawnActor<AActor>();
 for(const auto& B:Info.Buildings)
 {
  Dat->GetOrBuildSetupMesh(B.ModelId,100);
  auto* Mesh=NewObject<UStaticMeshComponent>(Host);Host->AddInstanceComponent(Mesh);
  Mesh->SetStaticMesh(Dat->GetOrCreateSetupStaticMesh(B.ModelId,100,false,0,false,true));
  Mesh->SetWorldTransform(Frame(B.Origin,B.Orientation)*FTransform(Origin));Mesh->RegisterComponent();
 }
 if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 auto* Target=NewObject<UTextureRenderTarget2D>(Host);Target->ClearColor=FLinearColor(.1f,.15f,.25f);Target->InitCustomFormat(640,480,PF_B8G8R8A8,false);
 auto* Capture=NewObject<USceneCaptureComponent2D>(Host);Capture->TextureTarget=Target;Capture->FOVAngle=90;Capture->CaptureSource=SCS_FinalColorLDR;
 Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetBloom(false);
 Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetAntiAliasing(false);Capture->ShowFlags.SetFog(false);Capture->RegisterComponent();
 const FVector Point=Origin+FACEPosition::AceVectorToUnreal(FVector(62.193657,147.428970,142),100);
 Dat->GetOrBuildLandblockMesh(Block,100);
 float TerrainZ=0;Dat->SampleOutdoorGroundZ(Point.X,Point.Y,100,TerrainZ);
 TestTrue(TEXT("Reported wooden floor is exactly coplanar with terrain"),FMath::Abs(TerrainZ-Point.Z)<.01);
 auto* Land=World->SpawnActor<AACELandblockActor>();TestTrue(TEXT("Mansion terrain loads"),Land->LoadLandblock(Block,100));
 Land->SetLandEnvCellFloorPriority(true,Dat);
 if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 auto Render=[&](const FString& Name)
 {
  World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();
  TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
  if(FParse::Param(FCommandLine::Get(),TEXT("ACESaveMansionCaptures")))
  {
   TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(640,480,Pixels,PNG);
   FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("MansionProbe")/(Name+TEXT(".png"))));
  }
  return Pixels;
 };
 auto Difference=[](const TArray<FColor>& A,const TArray<FColor>& B)
 {
  int32 Changed=0;
  for(int32 Y=260;Y<460;++Y)for(int32 X=40;X<600;++X)
  {
   const int32 I=Y*640+X;
   if(FMath::Abs(int32(A[I].R)-int32(B[I].R))>12 || FMath::Abs(int32(A[I].G)-int32(B[I].G))>12)++Changed;
  }
  return Changed;
 };
 TArray<FVector> Eyes;
 for(int32 I=0;I<4;++I)Eyes.Add(Point+FVector(I*.13,I*.17,180));
 for(const auto& Floor:FloorViews)if(Eyes.Num()<16)Eyes.Add(Floor+FVector(0,0,180));
 TestTrue(TEXT("Fixture covers multiple mansion rooms"),Eyes.Num()>8);
 for(int32 View=0;View<Eyes.Num();++View)
 {
  Capture->SetWorldLocationAndRotation(Eyes[View],FVector(0,1,-.7).Rotation());
  Land->SetActorHiddenInGame(false);
  const auto Combined=Render(FString::Printf(TEXT("Floor_%d"),View));
  Land->SetActorHiddenInGame(true);
  const auto RoomOnly=Render(FString::Printf(TEXT("Reference_%d"),View));
  if(!TestEqual(TEXT("Floor capture size"),Combined.Num(),640*480) || RoomOnly.Num()!=Combined.Num())continue;
  const int32 Changed=Difference(Combined,RoomOnly);
  AddInfo(FString::Printf(TEXT("Mansion floor view %d: %d terrain-overwritten pixels"),View,Changed));
  TestTrue(TEXT("Room floors win over terrain while walking through the mansion"),Changed<560);
  if(View==0)
  {
   // Prove this fixture catches the old equal-offset regression.
   Land->SetActorHiddenInGame(false);
   TArray<UProceduralMeshComponent*> Components;Land->GetComponents(Components);
   for(auto* M:Components)for(int32 I=0;I<M->GetNumMaterials();++I)
    if(auto* MID=Cast<UMaterialInstanceDynamic>(M->GetMaterial(I)))MID->SetScalarParameterValue(TEXT("LandDepthBias"),.25f);
   const auto Tie=Render(TEXT("OldEqualDepth"));
   TestTrue(TEXT("Equal terrain/room offsets reproduce the floor conflict"),Difference(Tie,RoomOnly)>5000);
   Land->SetLandEnvCellFloorPriority(false,Dat);Land->SetLandEnvCellFloorPriority(true,Dat);
  }
 }
 return !HasAnyErrors();
}
#endif
