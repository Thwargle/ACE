#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dat/ACEDatMotionPlayer.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEAnimationLifetimeTest, "ACE.RetailParity.AnimationLifetime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACEAnimationLifetimeTest::RunTest(const FString&)
{
 // Distinct frames and hooks make a wrong wrap, interpolation or repeated event
 // observable without depending on a particular installed DAT revision.
 FACEDatMotionPlayer Player(nullptr);
 FACEDatAnimation Animation; Animation.Id=0x0300ffff; Animation.NumFrames=8; Animation.NumParts=1;
 for(int32 I=0; I<8; ++I)
 {
  auto& Frame=Animation.PartFrames.AddDefaulted_GetRef();
  Frame.PartFrames.Add(FTransform3f(FQuat4f(FVector3f::UpVector,I*.2f),FVector3f(I*.1f,0,0)));
  auto& Hook=Frame.Hooks.AddDefaulted_GetRef(); Hook.Id=I; Hook.Direction=EACEAnimationHookDirection::Both;
 }
 Player.AnimCache.Add(Animation.Id,MoveTemp(Animation));
 FACEDatAnimData Clip; Clip.AnimId=0x0300ffff; Clip.LowFrame=2; Clip.HighFrame=5;
 TArray<FTransform> Initial,Later; TArray<FACEDatAnimationHook> InitialHooks,LaterHooks; int32 Count=0;
 for(float Rate : {4.f,-4.f})
 {
  Clip.Framerate=Rate;
  for(float Phase : {0.f,.125f,.375f,.75f,.875f})
  {
   const float Previous=Phase-.25f;
   TestTrue(TEXT("Short looping pose evaluates"),Player.EvaluateAnimData(Clip,Phase,1,Initial,100,Count,true,nullptr,&Previous,&InitialHooks));
   for(float Age : {3600.f,28800.f,86400.f})
   {
    const float Time=Age+Phase, Before=Time-.25f;
    TestTrue(TEXT("Long-lived looping pose evaluates"),Player.EvaluateAnimData(Clip,Time,1,Later,100,Count,true,nullptr,&Before,&LaterHooks));
    TestTrue(TEXT("One, eight and 24 hours preserve forward/reverse pose phase"),Later[0].Equals(Initial[0],.00001));
    TestEqual(TEXT("Aged motion emits only newly crossed hooks"),LaterHooks.Num(),InitialHooks.Num());
    for(int32 I=0; I<FMath::Min(LaterHooks.Num(),InitialHooks.Num()); ++I)
     TestEqual(TEXT("Hook identity and order survive cycle wrap"),LaterHooks[I].Id,InitialHooks[I].Id);
   }
  }
  const float Previous=0;
  Player.EvaluateAnimData(Clip,86400.375f,1,Later,100,Count,true,nullptr,&Previous,&LaterHooks);
  TestEqual(TEXT("A long hitch visits at most the most recent cycle of hook edges"),LaterHooks.Num(),4);
  TSet<uint32> Seen; for(const auto& Hook:LaterHooks)Seen.Add(Hook.Id);
  TestEqual(TEXT("A long hitch does not replay duplicate hooks"),Seen.Num(),4);
  bool Finished=false;
  Player.EvaluateAnimData(Clip,1.f,1,Initial,100,Count,false,&Finished);
  Player.EvaluateAnimData(Clip,86400.f,1,Later,100,Count,false,&Finished);
  TestTrue(TEXT("One-shot links still finish and hold their forward/reverse endpoint"),Finished && Later[0].Equals(Initial[0],.00001));
 }
 Clip.Framerate=0;
 Player.EvaluateAnimData(Clip,0,1,Initial,100,Count);
 Player.EvaluateAnimData(Clip,86400,1,Later,100,Count);
 TestTrue(TEXT("Zero-rate held pose is unchanged"),Later[0].Equals(Initial[0],.00001));
 // The final fractional frame blends into the first until the exact boundary.
 // The old .999 threshold wrapped slightly early and snapped that tiny interval.
 Clip.Framerate=4;
 Player.EvaluateAnimData(Clip,.9999f,1,Initial,100,Count);
 Player.EvaluateAnimData(Clip,1.f,1,Later,100,Count);
 TestTrue(TEXT("Last fractional frame approaches the first continuously"),
  FVector::Dist(Initial[0].GetLocation(),Later[0].GetLocation())<.15
  && Initial[0].GetRotation().AngularDistance(Later[0].GetRotation())<.001);

 // Isolate only the old/new frame reduction. These are CPU microbenchmarks,
 // not total frame time or a native-headset FPS measurement; no timing asserts.
 volatile float Checksum=0;
 constexpr int32 Samples=2048;
 for(float Age : {60.f,3600.f,28800.f})
 {
  double Ms[2]{};
  for(int32 Variant=0; Variant<2; ++Variant)
  {
   const double Start=FPlatformTime::Seconds();
   for(int32 I=0; I<Samples; ++I)
   {
    float Frame=2+(Age+float(I%8)*.125f)*4;
    if(Variant==0) { while(Frame>5.999f) Frame-=4; }
    else Frame=2+FMath::Fmod(Frame-2,4.f);
    Checksum=Checksum+Frame;
   }
   Ms[Variant]=(FPlatformTime::Seconds()-Start)*1000;
  }
  AddInfo(FString::Printf(TEXT("Frame reduction: age=%.0fs, %d samples, old=%.3fms, bounded=%.3fms"),Age,Samples,Ms[0],Ms[1]));
 }
 TestTrue(TEXT("Timing work produced finite frames"),FMath::IsFinite(float(Checksum)));
 return true;
}
#endif
