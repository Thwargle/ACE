#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../ACEBodySweep.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERoofEdgeTest,"ACE.RetailParity.RoofEdges",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACERoofEdgeTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto Box=[&](FVector Center,FVector Extent)
 {
  auto* A=World->SpawnActor<AActor>(); auto* B=NewObject<UBoxComponent>(A); A->SetRootComponent(B);
  B->SetBoxExtent(Extent); B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  B->SetCollisionResponseToAllChannels(ECR_Block); B->RegisterComponent(); A->SetActorLocation(Center);
  return A;
 };
 Box(FVector(0,0,200),FVector(100,100,1)); // roof, only two centimeters thick
 Box(FVector(600,0,200),FVector(1,200,200));
 const FCollisionShape Body=FCollisionShape::MakeCapsule(30,90);
 const FCollisionQueryParams Query(SCENE_QUERY_STAT(ACERoofEdge),false);
 for (float Offset : {0.f,15.f,25.f,29.f})
 {
  const auto Hit=ACEBodySweep::MoveAirborne(*World,FVector(100+Offset,0,360),FVector(100+Offset,0,80),Body,Query,true);
  TestTrue(TEXT("Falling lower sphere lands even with its center outside the roof"),Hit.bLanded);
  TestTrue(TEXT("Roof edge cannot be crossed during a long frame"),Hit.Position.Z>260.f);
 }
 const auto Corner=ACEBodySweep::MoveAirborne(*World,FVector(120,120,360),FVector(120,120,80),Body,Query,true);
 TestTrue(TEXT("Lower sphere catches the roof corner"),Corner.bLanded && Corner.Position.Z>260.f);
 const auto Jump=ACEBodySweep::MoveAirborne(*World,FVector(0,0,291),FVector(60,0,500),Body,Query,false);
 TestTrue(TEXT("Touching a roof does not prevent jumping away from it"),Jump.Position.Z>490.f);
 const auto VerticalJump=ACEBodySweep::MoveAirborne(*World,FVector(0,0,291),FVector(0,0,500),Body,Query,false);
 TestTrue(TEXT("A vertical jump can leave its touching support"),VerticalJump.Position.Z>490.f);
 const auto Ceiling=ACEBodySweep::MoveAirborne(*World,FVector(0,0,90),FVector(80,0,350),Body,Query,false);
 TestTrue(TEXT("Jumping under a thin roof never passes through its underside"),Ceiling.Position.Z<110.f);
 const auto Wall=ACEBodySweep::MoveAirborne(*World,FVector(560,-50,100),FVector(650,70,350),Body,Query,false);
 TestTrue(TEXT("A diagonal airborne slide remains outside the wall"),Wall.Position.X<570.f);
 TestTrue(TEXT("An airborne wall slide retains vertical progress"),Wall.Position.Z>200.f);
 const auto WallOverlap=ACEBodySweep::MoveAirborne(*World,FVector(575,0,300),FVector(575,0,260),Body,Query,true);
 TestTrue(TEXT("Existing ledge-side overlap cannot hold a falling player midair"),WallOverlap.Position.Z<270.f);
 const auto SoffitOverlap=ACEBodySweep::MoveAirborne(*World,FVector(0,0,115),FVector(0,0,60),Body,Query,true);
 TestTrue(TEXT("Existing soffit overlap permits falling away from the roof"),SoffitOverlap.Position.Z<70.f);
 auto* Slope=Box(FVector(2000,0,200),FVector(300,300,1));
 Slope->SetActorRotation(FRotator(30,0,0));
 const FVector OnSlope(2000,0,291.2f); // ray feet are clear, lower sphere overlaps the pitch
 FHitResult Initial;
 TestTrue(TEXT("Sloped-roof fixture starts with the reported support overlap"),
  World->SweepSingleByChannel(Initial,OnSlope,OnSlope+FVector(0,0,1),FQuat::Identity,ECC_Pawn,Body,Query) && Initial.bStartPenetrating);
 const auto SlopeLaunch=ACEBodySweep::MoveAirborne(*World,OnSlope,OnSlope+FVector(0,0,30),Body,Query,false);
 TestTrue(TEXT("Charged jump leaves a pitched roof despite initial lower-sphere overlap"),SlopeLaunch.Position.Z>OnSlope.Z+20);
 const auto SlopeLand=ACEBodySweep::MoveAirborne(*World,OnSlope,OnSlope-FVector(0,0,3),Body,Query,true);
 TestTrue(TEXT("An overlapping roof support completes landing instead of locking movement"),SlopeLand.bLanded);
 FHitResult Clear;
 TestFalse(TEXT("Recovered landing removes the support penetration"),
  World->SweepSingleByChannel(Clear,SlopeLand.Position,SlopeLand.Position+FVector(0,0,.1f),FQuat::Identity,ECC_Pawn,Body,Query) && Clear.bStartPenetrating);
 // Retail permits landing on a steeper face than it permits walking up. Test
 // an existing lower-sphere overlap too, not just a clean downward sweep.
 for (float Pitch : {50.f,60.f,75.f})
 {
  Slope->SetActorRotation(FRotator(Pitch,0,0));
  const auto Land=ACEBodySweep::MoveAirborne(*World,OnSlope,OnSlope-FVector(0,0,3),Body,Query,true);
  AddInfo(FString::Printf(TEXT("Roof pitch=%.0f landed=%d normal=%s position=%s"),Pitch,Land.bLanded,*Land.ContactNormal.ToString(),*Land.Position.ToString()));
  TestTrue(TEXT("A landable pitched roof releases the airborne movement lock"),Land.bLanded);
 }
 Box(FVector(2000,0,-5),FVector(1000,1000,5));
 for(float Dt:{1.f/90,1.f/20})
 {
  Slope->SetActorRotation(FRotator(60,0,0));
  FVector At(2000,0,600),Velocity(0,0,-200);bool Landed=false;float Elapsed=0;int32 SlopeContacts=0;
  for(;Elapsed<3.f && !Landed;Elapsed+=Dt)
  {
   const FVector To=At+Velocity*Dt+FVector(0,0,-490*Dt*Dt);Velocity.Z-=980*Dt;
   const auto Move=ACEBodySweep::MoveAirborne(*World,At,To,Body,Query,Velocity.Z<=0,.6641741f);
   At=Move.Position;Landed=Move.bLanded;
   if(!Move.ContactNormal.IsNearlyZero())
   {
    const float Into=FVector::DotProduct(Velocity,Move.ContactNormal);
    if(Into<0)Velocity-=Move.ContactNormal*(Into*1.05f);
    if(Move.ContactNormal.Z>0 && Move.ContactNormal.Z<.6641741f)++SlopeContacts;
   }
  }
  AddInfo(FString::Printf(TEXT("VR steep slide %.0f FPS: %.2fs, %d slope contacts, final %s"),1/Dt,Elapsed,SlopeContacts,*At.ToString()));
  TestTrue(TEXT("VR steep contact keeps accelerating until real ground, within three seconds"),Landed && SlopeContacts>0 && At.Z<92.f);
 }
 World->DestroyWorld(false); return true;
}
#endif
