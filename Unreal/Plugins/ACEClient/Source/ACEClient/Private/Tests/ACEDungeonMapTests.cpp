#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Mods/ACEDungeonMapContours.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDungeonMapContoursTest,"ACE.Plugins.Waypoint.WalkableContours",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEDungeonMapContoursTest::RunTest(const FString&)
{
 auto Floor=[](FACEDungeonMapContours& Map,double X0,double X1,double Y0,double Y1,double Z=0.)
 {Map.AddPolygon({{X0,Y0,Z},{X1,Y0,Z},{X1,Y1,Z},{X0,Y1,Z}});};
 auto Finish=[](FACEDungeonMapContours& Map){while(!Map.IsComplete())Map.BuildStep();};
 auto Seam=[](const FACEDungeonMapContours& Map,double X)
 {double Length=0;for(const auto& E:Map.Lines)if(FMath::IsNearlyEqual(E.A.X,X,.001)&&FMath::IsNearlyEqual(E.B.X,X,.001))Length+=(E.B-E.A).Size();return Length;};
 {
  FACEDungeonMapContours Map;Floor(Map,0,10,0,10);Floor(Map,10,20,0,10);Finish(Map);
  TestEqual(TEXT("Open adjoining cells do not draw their shared border"),Seam(Map,10),0.);
  double Length=0;for(const auto& E:Map.Lines)Length+=(E.B-E.A).Size();
  TestEqual(TEXT("Room outside perimeter remains intact"),Length,60.);
 }
 {
  FACEDungeonMapContours Map;Floor(Map,0,10,0,10);Floor(Map,10,20,2,5);Floor(Map,10,20,5,8);Finish(Map);
  TestEqual(TEXT("Unequal floor subdivisions remove only the connected interval"),Seam(Map,10),4.);
 }
 {
  FACEDungeonMapContours Map;Floor(Map,0,10,0,10);Floor(Map,10,20,0,10);
  Map.AddPolygon({{10,0,0},{10,3,0},{10,3,3},{10,0,3}});
  Map.AddPolygon({{10,7,0},{10,10,0},{10,10,3},{10,7,3}});
  Map.AddPolygon({{10,3,2},{10,7,2},{10,7,3},{10,3,3}});Finish(Map);
  TestEqual(TEXT("Collision wall sides remain but the doorway under its lintel stays open"),Seam(Map,10),12.);
 }
 {
  FACEDungeonMapContours Map;Floor(Map,0,10,0,10);Floor(Map,10,20,0,10);
  Map.AddPolygon({{10,0,0},{10,10,0},{11,10,3},{11,0,3}});Finish(Map);
  TestEqual(TEXT("Tilted collision walls still separate adjoining floors"),Seam(Map,10),20.);
 }
 {
  FACEDungeonMapContours Map;Floor(Map,0,10,0,10);Floor(Map,10,20,0,10,5);Finish(Map);
  TestEqual(TEXT("Overlaid floors on separate elevations never connect"),Seam(Map,10),20.);
 }
 {
  FACEDungeonMapContours Map;Floor(Map,0,10,0,10);Floor(Map,0,10,0,10);Finish(Map);
  TestEqual(TEXT("Duplicate floor surfaces do not erase outside walls"),Seam(Map,10),20.);
 }
 {
  FACEDungeonMapContours Map;
  Map.AddPolygon({{0,0,0},{10,0,5},{10,10,5},{0,10,0}});
  Map.AddPolygon({{10,0,5},{20,0,5},{20,10,5},{10,10,5}});Finish(Map);
  TestEqual(TEXT("Ramp meeting a landing has no false border"),Seam(Map,10),0.);
 }
 {
  FACEDungeonMapContours Map;const double Begin=FPlatformTime::Seconds();
  for(int32 X=0;X<40;++X)for(int32 Y=0;Y<40;++Y)Floor(Map,X*10,(X+1)*10,Y*10,(Y+1)*10);
  Finish(Map);TestEqual(TEXT("1600 connected cells reduce to outer edge segments"),Map.Lines.Num(),160);
  AddInfo(FString::Printf(TEXT("1600-cell contour build: %.2f ms; %d input edges, %d visible segments"),(FPlatformTime::Seconds()-Begin)*1000,Map.InputEdgeCount(),Map.Lines.Num()));
 }
 return true;
}
#endif
