#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Mods/ACEWaypoint.h"
#include "UI/ACERetailTextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWaypointCoordinatesTest,"ACE.Plugins.Waypoint.CoordinatesAndHeading",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEWaypointCoordinatesTest::RunTest(const FString&)
{
    FVector2D P;
    TestTrue(TEXT("Retail chat syntax"),ACEWaypoint::Parse(TEXT("42.0N, 33.6E"),P));
    TestEqual(TEXT("EW is X"),P,FVector2D(33.6,42));
    TestTrue(TEXT("Lowercase and whitespace"),ACEWaypoint::Parse(TEXT(" 12.5s 7.25w "),P));
    TestEqual(TEXT("Southern/western hemispheres"),P,FVector2D(-7.25,-12.5));
    for(const FString Bad:{TEXT("103N, 20E"),TEXT("-1N, 2E"),TEXT("1.2.3N, 4E"),TEXT("1N, 2Evil"),TEXT("NaNN, 2E"),TEXT("1E, 2N"),TEXT("1N, 2E extra")})
        TestFalse(*(TEXT("Reject invalid coordinate: ")+Bad),ACEWaypoint::Parse(Bad,P));
    const FString Chat=TEXT("Alice says, meet at 42.0N, 33.6E; then 12.5S, 7.25W.");
    const auto Links=ACEWaypoint::FindCoordinates(Chat);
    TestEqual(TEXT("Two independent clickable destinations"),Links.Num(),2);
    if(Links.Num()==2)
    {
        TestEqual(TEXT("Span preserves message text"),Chat.Mid(Links[0].Begin,Links[0].End-Links[0].Begin),FString(TEXT("42.0N, 33.6E")));
        auto* Row=NewObject<UACERetailTextBlock>();Row->TextLinks.Add({Links[0].Begin,Links[0].End});
        TestFalse(TEXT("Sender remains ordinary text"),Row->IsLinkAt(0));TestTrue(TEXT("Coordinate gets link treatment"),Row->IsLinkAt(Links[0].Begin));
        TestFalse(TEXT("Trailing separator is not clickable"),Row->IsLinkAt(Links[0].End));
        Row->AreLinksEnabled=[]{return false;};TestFalse(TEXT("Disabling plugin disables existing links"),Row->IsLinkAt(Links[0].Begin));
    }
    const FVector2D Origin(0,0),North(0,1);
    TestTrue(TEXT("Forward arrow points up"),FMath::IsNearlyZero(ACEWaypoint::RelativeBearing(Origin,{0,1},North)));
    TestTrue(TEXT("East is right when facing north"),FMath::IsNearlyEqual(ACEWaypoint::RelativeBearing(Origin,{1,0},North),UE_DOUBLE_PI/2));
    TestTrue(TEXT("West is left when facing north"),FMath::IsNearlyEqual(ACEWaypoint::RelativeBearing(Origin,{-1,0},North),-UE_DOUBLE_PI/2));
    TestTrue(TEXT("Behind points down"),FMath::IsNearlyEqual(FMath::Abs(ACEWaypoint::RelativeBearing(Origin,{0,-1},North)),UE_DOUBLE_PI));
    TestTrue(TEXT("Facing east makes north left"),ACEWaypoint::RelativeBearing(Origin,{0,1},{1,0})<0);
    FACEPosition A;A.CellId=int32(0x7D640001);A.Location=FVector(191,80,0);
    FACEPosition B=A;B.CellId=int32(0x7E640001);B.Location.X=1;
    TestTrue(TEXT("Landblock transition remains two metres"),FMath::IsNearlyEqual((ACEWaypoint::Coordinates(B)-ACEWaypoint::Coordinates(A)).Size()*240.,2.,.0001));
    B=A;B.CellId=int32(0x7D640100);TestEqual(TEXT("Buildings preserve map coordinates"),ACEWaypoint::Coordinates(A),ACEWaypoint::Coordinates(B));
    for(const FVector2D Up:{FVector2D(0,1),FVector2D(1,0),FVector2D(0,-1),FVector2D(-1,0),FVector2D(.6,.8)})
    {
        TestTrue(TEXT("Player facing goes up at every heading"),ACEWaypoint::ToMap(Up,Up).Equals({0,-1}));
        const FVector2D Point(33.6,-42);
        TestTrue(TEXT("Rotated map clicks round trip to world coordinates"),ACEWaypoint::FromMap(ACEWaypoint::ToMap(Point,Up),Up).Equals(Point));
        TestTrue(TEXT("Rotation preserves distances"),FMath::IsNearlyEqual(ACEWaypoint::ToMap(Point,Up).Size(),Point.Size()));
    }
    return true;
}

#include "Mods/ACEWaypointTiles.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Images/SImage.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWaypointTilesTest,"ACE.Plugins.Waypoint.TerrainTiles",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEWaypointTilesTest::RunTest(const FString&)
{
    const auto A=FACEWaypointTiles::Bounds({124,100,2}),B=FACEWaypointTiles::Bounds({126,100,2});
    TestEqual(TEXT("Adjacent terrain tiles share exact edge"),A.Max.X,B.Min.X);
    FACEPosition Position;Position.CellId=0x7C640001;Position.Location=FVector::ZeroVector;
    TestEqual(TEXT("Terrain tile origin matches world coordinates"),A.Min,ACEWaypoint::Coordinates(Position));
    for(double Scale:{2.,9.,10.,11.,20.,40.,64.,100.,160.,320.,640.,1000.})
    {
        const auto Detail=FACEWaypointTiles::Detail(Scale);
        TestTrue(TEXT("LOD has at least one texel per display pixel"),Detail.X*.8*Scale<=Detail.Y);
        TestTrue(TEXT("LOD stays within source detail and upload budget"),Detail.X>=1&&Detail.X<=32&&Detail.Y<=1024);
    }
    if(!FParse::Param(FCommandLine::Get(),TEXT("WaypointRender")))return true;
    FACEWaypointTiles Tiles;const auto View=FBox2D(A.Min+FVector2D(.01,.01),A.Max-FVector2D(.01,.01));
    const double Begin=FPlatformTime::Seconds();double MaxUpdate=0;
    do {const double Before=FPlatformTime::Seconds();Tiles.Update(TEXT("C:/Turbine/Asheron's Call"),View,100);MaxUpdate=FMath::Max(MaxUpdate,FPlatformTime::Seconds()-Before);FPlatformProcess::Sleep(.01f);}
    while(Tiles.GetTiles().IsEmpty()&&FPlatformTime::Seconds()-Begin<20);
    TestEqual(TEXT("Real DAT produces one visible high-detail tile"),Tiles.GetTiles().Num(),1);
    AddInfo(FString::Printf(TEXT("Terrain tile ready in %.2f sec; maximum game-thread update %.2f ms"),FPlatformTime::Seconds()-Begin,MaxUpdate*1000));
    if(Tiles.GetTiles().IsEmpty())return false;
    for(int I=0;I<10;++I)Tiles.Update(TEXT("C:/Turbine/Asheron's Call"),View,100);
    TestEqual(TEXT("Unchanged map reuses texture"),Tiles.GetTiles().Num(),1);
    auto Widget=SNew(SImage).Image(&Tiles.GetTiles()[0]->Brush);FWidgetRenderer Renderer(false,true);
    auto* Target=FWidgetRenderer::CreateTargetFor({512,512},TF_Bilinear,true);
    for(int I=0;I<3;++I){Renderer.DrawWidget(Target,Widget,{512,512},0);FlushRenderingCommands();}
    TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
    TSet<uint32> Colors;for(auto C:Pixels)Colors.Add(C.DWColor());TestTrue(TEXT("Tile contains detailed terrain rather than blank fallback"),Colors.Num()>100);
    TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(512,512,Pixels,Png);FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("Automation/Waypoint-TerrainTile.png")));Target->ReleaseResource();
    const auto FineBounds=FACEWaypointTiles::Bounds({124,100,1});
    const FBox2D FineView(FineBounds.Min+FVector2D(.01,.01),FineBounds.Max-FVector2D(.01,.01));
    const double FineBegin=FPlatformTime::Seconds();
    do {Tiles.Update(TEXT("C:/Turbine/Asheron's Call"),FineView,1000);FPlatformProcess::Sleep(.01f);}
    while(Tiles.IsLoading()&&FPlatformTime::Seconds()-FineBegin<20);
    TestFalse(TEXT("High-DPI terrain request completes"),Tiles.IsLoading());
    TestTrue(TEXT("High-DPI zoom receives real 1024-pixel DAT tile"),Tiles.GetTiles().ContainsByPredicate([](const auto& T){return T->Resolution==1024&&T->Texture.IsValid()&&T->Texture->GetSizeX()==1024;}));
    return true;
}
#endif
