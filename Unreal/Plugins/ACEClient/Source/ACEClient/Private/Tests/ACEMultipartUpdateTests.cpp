#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "VR/ACEVRMath.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEMultipartUpdateTest, "ACE.Performance.MultipartUpdates",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEMultipartUpdateTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->Init();
 ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
 // Frozen reference for the previous full-array stance-blend implementation.
 auto ReferenceBlend=[](UACECharacterAppearanceComponent* App,const TArray<FTransform>& Pose,int32 Count,float Dt)
 {
  TArray<FTransform> Blended;const TArray<FTransform>* Use=&Pose;
  if(App->StanceBlendAlpha<1.f-KINDA_SMALL_NUMBER && App->StanceBlendFrom.Num()==App->PartMeshes.Num())
  {
   Blended=Pose;Use=&Blended;
   App->StanceBlendAlpha=FMath::Clamp(App->StanceBlendAlpha+Dt/App->PoseBlendDuration,0.f,1.f);
   for(int32 I=0;I<FMath::Min(Blended.Num(),App->StanceBlendFrom.Num());++I)
   {FTransform Out;Out.Blend(App->StanceBlendFrom[I],Blended[I],App->StanceBlendAlpha);Blended[I]=Out;}
   if(App->StanceBlendAlpha>=1.f-KINDA_SMALL_NUMBER){App->StanceBlendFrom.Reset();App->StanceBlendAlpha=1.f;}
  }
  for(int32 I=0;I<App->PartMeshes.Num();++I)
  {
   if(I<Count && Use->IsValidIndex(I))
   {FTransform Part=(*Use)[I];if(App->BindTransforms.IsValidIndex(I))Part.SetScale3D(App->BindTransforms[I].GetScale3D());App->ApplyPartTransform(I,Part);}
   else if(App->BindTransforms.IsValidIndex(I))App->ApplyPartTransform(I,App->BindTransforms[I]);
  }
 };
 for(uint32 Setup : {0x02000001u,0x0200004eu})
 {
  FACEWorldObject Obj;Obj.Guid=12345;Obj.SetupId=Setup;Obj.MotionTableId=0x09000001;Obj.bIsPlayer=true;
  UACECharacterAppearanceComponent* Apps[2];AACEWorldEntityActor* Actors[2];
  for(int Variant=0;Variant<2;++Variant)
  {
   Actors[Variant]=World->SpawnActor<AACEWorldEntityActor>();Actors[Variant]->InitializeFromObject(Obj,100,false);
   Apps[Variant]=Actors[Variant]->Appearance;
   if(!TestTrue(TEXT("Build both real avatars"),Apps[Variant]->ApplyWorldObject(Obj,100,false)))return false;
  }
  for(int Frame=0;Frame<120;++Frame)
  {
   const float Dt=Frame%3==0?1.f/30.f:1.f/90.f;
   if(Frame%13==0) for(auto* App:Apps)App->BeginPoseBlendFromCurrent(Frame%2?.06f:.2f);
   TArray<FTransform> Pose;int32 Count=0;
   Dat->EvaluateMotionCommand(Obj.MotionTableId,Frame<60?0x44000007u:0x45000005u,Frame/90.f,
    Apps[0]->GetPartCount(),Pose,100,Count,nullptr,nullptr,ACEMotion::StanceNonCombat);
   // Missing trailing frames and an empty pose must retain the bind fallback.
   if(Frame==30){Count=5;Pose.SetNum(5);}if(Frame==75){Count=0;Pose.Reset();}
   const auto Input=Pose;
   ReferenceBlend(Apps[0],Pose,Count,Dt);Apps[1]->ApplyAnimatedPartsWithBlend(Pose,Count,Dt);
   TestEqual(TEXT("Pose input retains its length"),Pose.Num(),Input.Num());
   for(int I=0;I<Pose.Num();++I)TestTrue(TEXT("Pose input is not modified"),Pose[I].Equals(Input[I],0));
   TestEqual(TEXT("Blend completion time is unchanged"),Apps[1]->StanceBlendAlpha,Apps[0]->StanceBlendAlpha);
   for(int I=0;I<Apps[0]->GetPartCount();++I)
    TestTrue(TEXT("Every blended part matches the former implementation"),Apps[0]->GetPartMesh(I)->GetRelativeTransform().Equals(Apps[1]->GetPartMesh(I)->GetRelativeTransform(),.00001));
  }
  FTransform HeadBind;Apps[0]->GetPartBindTransform(16,HeadBind);
  TArray<FTransform> Reference;int RootUpdates[2]={0,0};
  for(int Variant=0;Variant<2;++Variant)
  {
   auto* App=Apps[Variant];App->bVRPoseControlled=true;App->ResetVRLowerBody();
   const auto Handle=App->GetMeshRoot()->TransformUpdated.AddLambda([&](USceneComponent*,EUpdateTransformFlags,ETeleportType){++RootUpdates[Variant];});
   ON_SCOPE_EXIT {App->GetMeshRoot()->TransformUpdated.Remove(Handle);};
   // A held attachment follows the final part transform with no extra update delay.
   auto* Attachment=NewObject<USceneComponent>(Actors[Variant]);Attachment->SetupAttachment(App->GetPartMesh(9));
   Attachment->SetRelativeLocation(FVector(2,3,4));Attachment->RegisterComponent();
   RootUpdates[Variant]=0; // Exclude attachment registration's initial parent update.
   for(int Frame=0;Frame<180;++Frame)
   {
    // Measure steady updates after the first pose initializes the hierarchy.
    if(Frame==1)RootUpdates[Variant]=0;
    const float Dt=(Frame==73 || Frame==129)?0.f:Frame%3==0?1.f/30.f:1.f/90.f;
    const FTransform Head(FRotator(-40+Frame*.15,Frame*.7,6),FVector(Frame*.5,20,170));
    const FTransform Body=ACEVRMath::BodyFromHead(Head,HeadBind,FVector(0,-8,17),.9f);
    const FTransform Left(FQuat::Identity,Head.GetLocation()+FVector(25,-20,-30));
    const FTransform Right(FQuat::Identity,Head.GetLocation()+FVector(55,20,-20));
    const bool LeftTracked=Frame<90,RightTracked=Frame%40<20;
    if(Variant==0)
    {
     // Former call sites first propagated the unbent root, updated legs,
     // reset untracked upper parts, then propagated the bent root again.
     App->GetMeshRoot()->SetWorldTransform(Body);App->UpdateVRLowerBody(Dt);
     for(int I=9;I<App->GetPartCount();++I)
     {
      const bool Tracked=(LeftTracked && ((I>=10 && I<=12)||I==27))||(RightTracked && ((I>=13 && I<=15)||I==28));
      if(I!=16 && I!=21 && I!=22 && !Tracked)App->GetPartMesh(I)->SetRelativeTransform(App->BindTransforms[I]);
     }
    }
    App->UpdateVRUpperBody(Body,Head,Left,Right,LeftTracked,RightTracked,Dt);
    if(Variant==1)App->UpdateVRLowerBody(Dt);
    for(int I=0;I<App->GetPartCount();++I)
    {
     const auto Pose=App->GetPartMesh(I)->GetComponentTransform();
     if(Variant==0)Reference.Add(Pose);
     else TestTrue(TEXT("Single-root update preserves every final world-space body part"),Pose.Equals(Reference[Frame*App->GetPartCount()+I],.00001));
    }
    TestTrue(TEXT("Held attachments see the final body transform immediately"),Attachment->GetComponentTransform().Equals(Attachment->GetRelativeTransform()*App->GetPartMesh(9)->GetComponentTransform(),.00001));
   }
   App->GetMeshRoot()->TransformUpdated.Remove(Handle);
   Attachment->DestroyComponent();
  }
  TestEqual(TEXT("New tracked body propagates its root once per steady frame"),RootUpdates[1],179);
  TestEqual(TEXT("Previous tracked body propagated twice except on paused frames"),RootUpdates[0],356);
  AddInfo(FString::Printf(TEXT("Setup %08X: root propagations %d -> %d across 179 steady frames"),Setup,RootUpdates[0],RootUpdates[1]));
  for(auto* Actor:Actors)Actor->Destroy();
 }
 return true;
}
#endif
