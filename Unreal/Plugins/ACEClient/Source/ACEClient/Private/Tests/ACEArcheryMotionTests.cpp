#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatMotionPlayer.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEArcheryMotionTest,"ACE.RetailParity.ArcheryMotion",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEArcheryMotionTest::RunTest(const FString&)
{
 FACEDatDatabase Portal;
 if(!TestTrue(TEXT("Retail DAT opens"),Portal.Open(TEXT("C:/Turbine/Asheron's Call/client_portal.dat"))))return false;
 FACEDatMotionPlayer Player(&Portal);
 if(!TestTrue(TEXT("Retail human motion table loads"),Player.SetMotionTable(0x09000001)))return false;
 constexpr uint32 Bow=0x8000003fu, Crossbow=0x80000041u, Reload=0x40000016u;
 auto Link=[&](uint32 Style,uint32 From,uint32 To)->const FACEDatMotionData*
 {
  for(uint32 Key:{(Style<<16)|(From&0xFFFFFF),Style<<16})
   if(const auto* Links=Player.MotionTable->Links.Find(Key))
    if(const auto* Data=Links->Find(To))return Data;
  return nullptr;
 };
 const auto* Nock=Link(Bow,Reload,ACEMotion::Ready);
 if(!TestNotNull(TEXT("Retail has a separate nock/draw link"),Nock))return false;
 TestEqual(TEXT("Nock continues the reload animation"),Nock->Anims[0].AnimId,0x030004AEu);
 TestEqual(TEXT("Nock starts at the authored split frame"),Nock->Anims[0].LowFrame,22);
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
 Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT{GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 if(!TestTrue(TEXT("World DAT loads"),GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
 FACEWorldObject Obj;Obj.Guid=12345;Obj.SetupId=0x02000001;Obj.MotionTableId=0x09000001;Obj.bIsPlayer=true;
 auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Obj,100,false);
 auto* App=Actor->Appearance.Get();
 for(uint32 Setup:{0x02000001u,0x0200004eu})for(uint32 Style:{Bow,Crossbow})for(float Dt:{1.f/30,1.f/90})
 {
  Obj.SetupId=Setup;
  if(!TestTrue(TEXT("Avatar geometry builds"),App->ApplyWorldObject(Obj,100,false)))return false;
  App->ClearActionMotion();App->PreferredStyle=ACEMotion::StanceNonCombat;App->SetLocomotionInput(0,0,false,1);
  App->SetPreferredStyle(Style);
  TestEqual(TEXT("Entering missile stance plays the style linkage"),App->ActionCommand,Style);
  auto CheckClip=[&](uint32 From,uint32 To,uint32 SourceStyle,float Rate,bool Holds)
  {
   const auto* Reference=Link(SourceStyle,From,To);
   if(!TestNotNull(TEXT("Reference linkage exists"),Reference))return;
   bool Complete=false;
   for(int Frame=0;Frame<FMath::CeilToInt(8.f/Dt);++Frame)
   {
    const float Time=App->AnimTime+Dt*Rate;
    App->TickComponent(Dt,LEVELTICK_All,nullptr);
    TArray<FTransform> Expected;int32 Count=0;bool Finished=false;
    TestTrue(TEXT("Authored sequence evaluates"),Player.EvaluateAnimSequence(Reference->Anims,Time,App->GetPartCount(),Expected,100,Count,false,&Finished));
    if(Time/Rate>.3f)
     for(int32 Part=0;Part<Count;++Part)
     {
      const auto Actual=App->GetPartMesh(Part)->GetRelativeTransform();
      if(!TestTrue(FString::Printf(TEXT("%08X -> %08X part %d at %.3f follows DAT"),From,To,Part,Time),
       Actual.GetTranslation().Equals(Expected[Part].GetTranslation(),.04001)
       && Actual.GetRotation().Equals(Expected[Part].GetRotation(),.000011)))return;
     }
    if(Finished){Complete=true;break;}
   }
   TestTrue(TEXT("Complete authored movement plays"),Complete);
   TestTrue(TEXT("Endpoint persists only for the reload/aim state"),Holds?App->ActionCommand==To && App->bHoldActionFinal:App->ActionCommand==0);
  };
  CheckClip(ACEMotion::Ready,Style,ACEMotion::StanceNonCombat,1,false);
  for(float Rate:{1.f,1.7f})
  {
   App->PlayActionMotion(Reload,Rate,Style,false);
   CheckClip(ACEMotion::Ready,Reload,Style,Rate,true);
   if(Rate==1.f)App->FinishMissileMotion(); // Local server Ready.
   else App->CancelHeldActionMotion(); // Remote server Ready.
   TestEqual(TEXT("Ready retains Reload as its source"),App->ActionFromCommand,Reload);
   CheckClip(Reload,ACEMotion::Ready,Style,1,false);
  }
  App->PlayActionMotion(Reload,1,Style,false);App->TickComponent(Dt,LEVELTICK_All,nullptr);
  App->CancelHeldActionMotion();App->CancelHeldActionMotion();
  TestEqual(TEXT("An early Ready queues exactly one nock/draw"),App->PendingActionCommands.Num(),1);
  bool SawNock=false;
  for(int Frame=0;Frame<FMath::CeilToInt(8.f/Dt)&&App->ActionCommand;++Frame)
  {
   App->TickComponent(Dt,LEVELTICK_All,nullptr);
   SawNock|=App->ActionCommand==ACEMotion::Ready && App->ActionFromCommand==Reload;
  }
  TestTrue(TEXT("Early Ready finishes both reload segments"),SawNock && App->ActionCommand==0);
  App->SetHeldActionMotion(0x40000015u,Style); // Locally predicted Falling.
  App->FinishMissileMotion();
  TestEqual(TEXT("A late server Ready cannot cancel the local jump"),App->ActionCommand,0x40000015u);
  App->ClearActionMotion();
  App->PreferredStyle=ACEMotion::StanceNonCombat;App->SetPreferredStyle(Style);
  App->PlayActionMotion(Reload,1,Style,false);
  if(Dt>1.f/60){App->FinishMissileMotion();App->FinishMissileMotion();}
  else {App->CancelHeldActionMotion();App->CancelHeldActionMotion();}
  TestEqual(TEXT("Reload/Ready arriving during stance entry are queued once"),App->PendingActionCommands.Num(),2);
  SawNock=false;
  for(int Frame=0;Frame<FMath::CeilToInt(8.f/Dt)&&App->ActionCommand;++Frame)
  {
   App->TickComponent(Dt,LEVELTICK_All,nullptr);
   SawNock|=App->ActionCommand==ACEMotion::Ready && App->ActionFromCommand==Reload;
  }
  TestTrue(TEXT("Coalesced stance/reload/Ready plays the entire sequence"),SawNock && App->ActionCommand==0);
 }
 // A real loaded ammunition setup follows the animated hand, with no quiver
 // fallback. Network instance/sequence validation is covered by LoginAmmoAttachment.
 FACEWorldObject Ammo;Ammo.Guid=12346;Ammo.SetupId=0x02000BBA;Ammo.ParentGuid=Obj.Guid;
 Ammo.ParentLocation=1;Ammo.PlacementId=1;Ammo.CurrentWieldedLocation=ACEEquipMask::MissileAmmo;
 auto* Held=World->SpawnActor<AACEWorldEntityActor>();Held->InitializeFromObject(Ammo,100,true);
 Held->AttachToParentActor(Actor,1);
 TestTrue(TEXT("Loaded ammunition has visible DAT geometry"),Held->Appearance->GetPartCount()>0 && !Held->IsHidden());
 TestTrue(TEXT("Loaded ammunition is attached to its wielder"),Held->IsAttachedToParent());
 const FVector Before=Held->GetActorLocation();
 App->SetPreferredStyle(Bow);App->ClearActionMotion();App->PlayActionMotion(Reload,1,Bow,false);
 for(int Frame=0;Frame<15;++Frame)App->TickComponent(1.f/60,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Ammunition follows the reaching hand"),!Held->GetActorLocation().Equals(Before,.1));
 return true;
}
#endif
