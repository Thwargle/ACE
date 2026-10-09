#include "Mods/ACEWaypoint.h"
#include "ACEDungeonMapContours.h"
#include "Mods/ACEWaypointTiles.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACERetailMap.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "XmlFile.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"

bool UACEPluginSubsystem::IsWaypointEnabled() const
{auto P=Find(TEXT("waypoint"));return P && P->Enabled;}
bool UACEPluginSubsystem::IsWaypointUnlocked() const
{
    auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const auto* Manager=Client?Client->GetUIElementManager():nullptr;
    return !WaypointOption(TEXT("locked"),false) && (!Manager || !Manager->IsUiLocked());
}
bool UACEPluginSubsystem::IsWaypointMapUnlocked() const
{
    const auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const auto* Manager=Client?Client->GetUIElementManager():nullptr;
    // Use the same lock icon as the rest of the HUD. Ignore the retired
    // map_locked preference so existing profiles cannot leave the map stuck.
    return !Manager||!Manager->IsUiLocked();
}
FACEPosition UACEPluginSubsystem::WaypointPlayerPosition() const
{
    FACEPosition P;auto* GI=GetGameInstance();
    if(auto* PC=Cast<AACEPlayerController>(GI->GetFirstLocalPlayerController()))if(PC->TryGetLocallyPredictedPosition(P))return P;
    if(auto* Client=GI->GetSubsystem<UACEClientSubsystem>())P=Client->GetPlayerPosition();return P;
}
bool UACEPluginSubsystem::WaypointOption(const FString& Key,bool Default) const
{auto P=Find(TEXT("waypoint"));bool B;return P && P->Profile->TryGetBoolField(Key,B)?B:Default;}
void UACEPluginSubsystem::SetWaypointOption(const FString& Key,bool Value)
{
    if(auto P=Find(TEXT("waypoint")))
    {P->Profile->SetBoolField(Key,Value);SaveProfile(P->Id,P->ProfileName,ProfileJson(P->Id),false);}
}
bool UACEPluginSubsystem::SetWaypoint(const FString& Text)
{
    FVector2D Point;if(!ACEWaypoint::Parse(Text,Point)){Notice=TEXT("Enter coordinates such as 42.0N, 33.6E (N/S first, then E/W).");return false;}
    SetWaypoint(Point);return true;
}
float UACEPluginSubsystem::WaypointMapOpacity() const
{
    double Value=1;if(auto P=Find(TEXT("waypoint")))P->Profile->TryGetNumberField(TEXT("map_opacity"),Value);
    return FMath::IsFinite(Value)?FMath::Clamp(float(Value),.1f,1.f):1.f;
}
void UACEPluginSubsystem::SetWaypointMapOpacity(float Value)
{
    if(!FMath::IsFinite(Value))return;
    if(auto P=Find(TEXT("waypoint")))
    {P->Profile->SetNumberField(TEXT("map_opacity"),FMath::Clamp(Value,.1f,1.f));SaveProfile(P->Id,P->ProfileName,ProfileJson(P->Id),false);}
}
void UACEPluginSubsystem::SetWaypoint(FVector2D Point,uint32 Dungeon)
{
    if(!IsWaypointEnabled() || !FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y) || FMath::Abs(Point.X)>102 || FMath::Abs(Point.Y)>102)return;
    if(auto P=Find(TEXT("waypoint")))
    {
        P->Profile->SetNumberField(TEXT("east"),Point.X);P->Profile->SetNumberField(TEXT("north"),Point.Y);
        P->Profile->SetNumberField(TEXT("dungeon_landblock"),Dungeon);P->Profile->SetBoolField(TEXT("arrow"),true);
        SaveProfile(P->Id,P->ProfileName,ProfileJson(P->Id),false);Notice=TEXT("Waypoint: ")+ACEWaypoint::Format(Point);
    }
}
bool UACEPluginSubsystem::GetWaypoint(FVector2D& Point,uint32& Dungeon) const
{
    auto P=Find(TEXT("waypoint"));double X,Y,D=0;
    if(!P || !P->Profile->TryGetNumberField(TEXT("east"),X) || !P->Profile->TryGetNumberField(TEXT("north"),Y) || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || FMath::Abs(X)>102 || FMath::Abs(Y)>102)return false;
    P->Profile->TryGetNumberField(TEXT("dungeon_landblock"),D);Dungeon=FMath::IsFinite(D) && D>=0 && D<=MAX_uint32?uint32(D):0;Point=FVector2D(X,Y);return true;
}
void UACEPluginSubsystem::ClearWaypoint()
{if(auto P=Find(TEXT("waypoint"))){P->Profile->RemoveField(TEXT("east"));P->Profile->RemoveField(TEXT("north"));SaveProfile(P->Id,P->ProfileName,ProfileJson(P->Id),false);}}

bool UACEPluginSubsystem::ImportWaypointLocations(const FString& Path)
{
    // Import data only. Never load or execute the Decal DLL next to the atlas.
    const int64 Size=IFileManager::Get().FileSize(*Path);FString Source;
    if(Size<=0 || Size>16*1024*1024 || !FFileHelper::LoadFileToString(Source,*Path))
    {Notice=TEXT("Choose a readable GoArrow locations.xml file, no larger than 16 MB.");return false;}
    FXmlFile Xml(Source,EConstructMethod::ConstructFromBuffer);const auto* Root=Xml.GetRootNode();
    if(!Xml.IsValid() || !Root || Root->GetTag()!=TEXT("locations") || Root->GetChildrenNodes().Num()>20000)
    {Notice=TEXT("Expected GoArrow locations.xml (<locations> with <loc> entries). Existing locations were preserved.");return false;}
    TArray<TSharedPtr<FJsonValue>> Records;
    for(const auto* Node:Root->GetChildrenNodes())
    {
        if(Node->GetTag()!=TEXT("loc"))continue;
        const FString Name=Node->GetAttribute(TEXT("name")),Type=Node->GetAttribute(TEXT("type"));double NS=0,EW=0;
        if(Name==TEXT("GAVersion"))continue;
        if(Name.IsEmpty()||Name.Len()>256||!LexTryParseString(NS,*Node->GetAttribute(TEXT("NS")))||!LexTryParseString(EW,*Node->GetAttribute(TEXT("EW")))||!FMath::IsFinite(NS)||!FMath::IsFinite(EW)||FMath::Abs(NS)>102||FMath::Abs(EW)>102)
        {Notice=TEXT("Atlas contains invalid coordinates or names. Existing locations were preserved.");return false;}
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),Name);O->SetStringField(TEXT("type"),Type.Left(64));O->SetNumberField(TEXT("east"),EW);O->SetNumberField(TEXT("north"),NS);Records.Add(MakeShared<FJsonValueObject>(O));
    }
    if(Records.IsEmpty()){Notice=TEXT("No locations found. Existing locations were preserved.");return false;}
    auto Data=MakeShared<FJsonObject>();Data->SetArrayField(TEXT("locations"),Records);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));
    const FString Directory=UserDirectory()/TEXT("Waypoint");IFileManager::Get().MakeDirectory(*Directory,true);
    const FString File=Directory/TEXT("locations.json"),Temp=File+TEXT(".tmp");
    if(!FFileHelper::SaveStringToFile(Json,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!IFileManager::Get().Move(*File,*Temp,true))
    {Notice=TEXT("Could not save the imported atlas.");return false;}
    bWaypointLocationsLoaded=false;++WaypointLocationsRevision;GetWaypointLocations();
    Notice=FString::Printf(TEXT("Imported %d locations. Atlas data may differ on custom servers."),Records.Num());return true;
}
const TArray<FACEWaypointLandmark>& UACEPluginSubsystem::GetWaypointLocations()
{
    if(bWaypointLocationsLoaded)return WaypointLocations;bWaypointLocationsLoaded=true;WaypointLocations.Reset();
    const FString File=UserDirectory()/TEXT("Waypoint/locations.json");FString Json;TSharedPtr<FJsonObject> Data;
    if(IFileManager::Get().FileSize(*File)>16*1024*1024 || !FFileHelper::LoadFileToString(Json,*File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||!Data)return WaypointLocations;
    const TArray<TSharedPtr<FJsonValue>>* Records=nullptr;if(!Data->TryGetArrayField(TEXT("locations"),Records)||Records->Num()>20000)return WaypointLocations;
    for(const auto& V:*Records)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;if(!V->TryGetObject(O))continue;FACEWaypointLandmark M;double EW,NS;
        if((*O)->TryGetStringField(TEXT("name"),M.Name)&&(*O)->TryGetStringField(TEXT("type"),M.Type)&&(*O)->TryGetNumberField(TEXT("east"),EW)&&(*O)->TryGetNumberField(TEXT("north"),NS)&&FMath::IsFinite(EW)&&FMath::IsFinite(NS)&&FMath::Abs(EW)<=102&&FMath::Abs(NS)<=102)
        {M.Name=M.Name.Left(256);M.Coordinates=FVector2D(EW,NS);WaypointLocations.Add(M);}
    }
    return WaypointLocations;
}

namespace
{
const FLinearColor Accent(.15f,.78f,1),Ink(.9f,.94f,1),Muted(.5f,.62f,.73f);
const FSlateBrush& Background(){static FSlateRoundedBoxBrush B(FLinearColor(.015f,.024f,.04f),8.f);return B;}
TSharedRef<STextBlock> WaypointLabel(const FString& S,int Size=16){return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(Ink).AutoWrapText(true);}
const FButtonStyle& ButtonStyle()
{
    static FButtonStyle B=FButtonStyle().SetNormal(FSlateRoundedBoxBrush(FLinearColor(.035f,.085f,.13f),5.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.07f,.23f,.32f),5.f,Accent,1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.025f,.14f,.22f),5.f));return B;
}
TSharedRef<SButton> WaypointButton(const FString& S,TFunction<void()> Click)
{return SNew(SButton).IsFocusable(false).ButtonStyle(&ButtonStyle()).ContentPadding(FMargin(12,8)).OnClicked_Lambda([Click]{Click();return FReply::Handled();})[WaypointLabel(S)];}
void Text(FSlateWindowElementList& Out,int L,const FGeometry& G,FVector2D At,const FString& S,int Size,FLinearColor Color,bool Center=false)
{
    const auto Font=FCoreStyle::GetDefaultFontStyle("Regular",Size);
    if(Center)At.X-=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S,Font).X*.5;
    FSlateDrawElement::MakeText(Out,L,G.ToPaintGeometry(G.GetLocalSize(),FSlateLayoutTransform(At)),S,Font,ESlateDrawEffect::None,Color);
}
void Line(FSlateWindowElementList& Out,int L,const FGeometry& G,const TArray<FVector2D>& Points,FLinearColor Color,float Width=1)
{FSlateDrawElement::MakeLines(Out,L,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);}
void Triangle(FSlateWindowElementList& Out,int L,const FGeometry& G,FVector2D A,FVector2D B,FVector2D C,FLinearColor Color)
{
    TArray<FSlateVertex> V;
    for(auto P:{A,B,C})V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(),FVector2f(P),FVector2f::ZeroVector,Color.ToFColor(false)));
    FSlateDrawElement::MakeCustomVerts(Out,L,FSlateResourceHandle(),V,TArray<SlateIndex>{0,1,2},nullptr,0,0);
}
class SWaypointMapIcon : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWaypointMapIcon){} SLATE_END_ARGS()
    void Construct(const FArguments&){SetVisibility(EVisibility::HitTestInvisible);}
    FVector2D ComputeDesiredSize(float)const override{return FVector2D(34,34);}
    int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool)const override
    {
        const FVector2D C=G.GetLocalSize()*.5;const double R=FMath::Min(C.X,C.Y)-3;
        for(double Width:{1.,.45})
        {TArray<FVector2D> Points;for(int I=0;I<=32;++I){const double A=I*UE_DOUBLE_PI/16;Points.Add(C+FVector2D(FMath::Cos(A)*R*Width,FMath::Sin(A)*R));}Line(Out,L,G,Points,Accent,1.4f);}
        Line(Out,L,G,{C+FVector2D(-R,0),C+FVector2D(R,0)},Accent,1.4f);
        return L;
    }
};
class SWaypointArrow : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWaypointArrow){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_END_ARGS()
    void Construct(const FArguments& A){Host=A._Host;SetVisibility(EVisibility::HitTestInvisible);}
    FVector2D ComputeDesiredSize(float)const override{return FVector2D(260,190);}
    void Tick(const FGeometry& G,double T,float D)override{SLeafWidget::Tick(G,T,D);Invalidate(EInvalidateWidgetReason::Paint);}
    int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool)const override
    {
        auto* H=Host.Get();if(!H)return L;const auto P=H->WaypointPlayerPosition();
        const float W=G.GetLocalSize().X;const FVector2D Center(W*.5,79);
        if(!P.IsValid()){Text(Out,L+1,G,{W*.5,74},TEXT("Enter the world to navigate"),14,Muted,true);return L+1;}
        const auto Current=ACEWaypoint::Coordinates(P);FVector2D Target;uint32 Dungeon=0;
        Text(Out,L+1,G,{W*.5,10},ACEWaypoint::Format(Current),16,Ink,true);
        bool Active=H->GetWaypoint(Target,Dungeon);
        const bool OtherDungeon=Active && Dungeon && Dungeon!=(uint32(P.CellId)&0xFFFF0000);
        const double Distance=Active?(Target-Current).Size()*240.:0;
        const FVector F=P.GetAceForwardInAcSpace();
        const double Angle=Active?ACEWaypoint::RelativeBearing(Current,Target,{F.X,F.Y}):0;
        auto Rotate=[&](FVector2D V){return Center+FVector2D(V.X*FMath::Cos(Angle)-V.Y*FMath::Sin(Angle),(V.X*FMath::Sin(Angle)+V.Y*FMath::Cos(Angle))*.8);};
        const auto Tip=Rotate({0,-48}),Left=Rotate({-28,30}),Right=Rotate({28,30}),Ridge=Rotate({0,13});
        const auto Color=Active&&!OtherDungeon?Accent:Muted;
        Triangle(Out,L+1,G,Tip,Left,Ridge,Color*.55f);Triangle(Out,L+1,G,Tip,Ridge,Right,Color);
        Line(Out,L+2,G,{Tip,Left,Ridge,Right,Tip},Color,1.5f);
        FString Caption=!Active?TEXT("Choose a destination"):OtherDungeon?TEXT("Destination in another dungeon"):Distance<3?TEXT("Arrived"):TEXT("To ")+ACEWaypoint::Format(Target);
        Text(Out,L+2,G,{W*.5,132},Caption,13,Ink,true);
        if(Active && !OtherDungeon && H->WaypointOption(TEXT("distance")))Text(Out,L+2,G,{W*.5,154},FString::Printf(TEXT("%.0f m remaining"),Distance),14,Accent,true);
        return L+2;
    }
private:TWeakObjectPtr<UACEPluginSubsystem> Host;
};

class SWaypointMap : public SLeafWidget
{
    friend class FACEWaypointOverlayTest;
public:
    SLATE_BEGIN_ARGS(SWaypointMap){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(bool,Overlay) SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {
        Host=A._Host;Overlay=A._Overlay;SetClipping(EWidgetClipping::ClipToBounds);
        if(Host.IsValid())
        {Resources.Reset(NewObject<UACEUIResourceResolver>());Resources->Initialize(Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>());MapTexture.Reset(Resources->ResolveTexture(0x0600127D));MapBrush.SetResourceObject(MapTexture.Get());MapBrush.DrawAs=ESlateBrushDrawType::Image;}
        if(Overlay)Zoom=PinnedWorldZoom;
        SetToolTipText(FText::FromString(Overlay?TEXT("Drag to move • Bottom-right corner to resize • Wheel to zoom • Configure in the map window"):TEXT("Drag to pan • Wheel to zoom • Click a marker or map position to navigate")));
    }
    FVector2D ComputeDesiredSize(float)const override{return FVector2D(560,450);}
    void SetSearch(const FString& Value){Search=Value.Left(128);NextMarkers=0;}
    void ResetView(){Zoom=Dungeon?3.5:1;Pan=FVector2D::ZeroVector;Invalidate(EInvalidateWidgetReason::Paint);}
    void CenterOnPlayer(){if(Host.IsValid()){const auto P=Host->WaypointPlayerPosition();if(P.IsValid()){const auto C=ACEWaypoint::Coordinates(P);Pan=-(Project(C,LastSize)-LastSize*.5-Pan);}}}
    void ChangeZoom(double Factor,FVector2D Cursor)
    {const double Old=Zoom;Zoom=FMath::Clamp(Zoom*Factor,.6,48.);Pan=Cursor-LastSize*.5-(Cursor-LastSize*.5-Pan)*(Zoom/Old);if(Overlay){(Dungeon?PinnedDungeonZoom:PinnedWorldZoom)=Zoom;CenterOnPlayer();}Invalidate(EInvalidateWidgetReason::Paint);}
    FReply OnMouseWheel(const FGeometry& G,const FPointerEvent& E)override{LastSize=G.GetLocalSize();ChangeZoom(FMath::Pow(1.25,E.GetWheelDelta()),G.AbsoluteToLocal(E.GetScreenSpacePosition()));return FReply::Handled();}
    FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override
    {if(Overlay||E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();Press=Last=G.AbsoluteToLocal(E.GetScreenSpacePosition());Dragged=false;return FReply::Handled().CaptureMouse(SharedThis(this));}
    FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E)override
    {
        auto P=G.AbsoluteToLocal(E.GetScreenSpacePosition());Hover=P;
        if(HasMouseCapture()){Dragged|=(P-Press).Size()>3;if(Dragged)Pan+=P-Last;Last=P;Invalidate(EInvalidateWidgetReason::Paint);return FReply::Handled();}
        return FReply::Unhandled();
    }
    FReply OnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)override
    {
        if(!HasMouseCapture() || E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
        if(!Dragged && Host.IsValid())
        {
            const auto At=G.AbsoluteToLocal(E.GetScreenSpacePosition());auto Target=Unproject(At,G.GetLocalSize());
            double Near=12;for(int32 Index:PaintedMarkers){if(!Markers.IsValidIndex(Index))continue;const auto& M=Markers[Index];const double D=(Project(M.Point,G.GetLocalSize())-At).Size();if(D<Near){Near=D;Target=M.Point;}}
            Host->SetWaypoint(Target,Dungeon?Landblock:0);
        }
        return FReply::Handled().ReleaseMouseCapture();
    }
    void Tick(const FGeometry& G,double T,float D)override
    {
        SLeafWidget::Tick(G,T,D);LastSize=G.GetLocalSize();auto* H=Host.Get();if(!H)return;Player=H->WaypointPlayerPosition();
        const auto PreviousCenter=Unproject(LastSize*.5,LastSize);
        const auto Facing=Player.GetAceForwardInAcSpace();
        const auto NextUp=H->WaypointOption(TEXT("heading_up"),false)&&Player.IsValid()?FVector2D(Facing.X,Facing.Y).GetSafeNormal():FVector2D(0,1);
        if(!NextUp.IsNearlyZero()&&!NextUp.Equals(MapUp))
        {MapUp=NextUp;Pan=-ACEWaypoint::ToMap((PreviousCenter-Center())*PixelsPerUnit(LastSize),MapUp);}
        const uint32 LB=uint32(Player.CellId)&0xFFFF0000;
        const bool Inside=(uint32(Player.CellId)&65535)>=256;
        const bool NextDungeon=Overlay?Inside:H->WaypointOption(TEXT("dungeon"),false);
        if(Dungeon!=NextDungeon || (NextDungeon && (LB!=Landblock || Inside!=WasInside)))
        {Dungeon=NextDungeon;Landblock=LB;WasInside=Inside;NextMarkers=0;NextCellRetry=0;Floors.Reset();Contours=FACEDungeonMapContours();Environments.Reset();CellIndex=0;CellCount=0;ResetView();
         if(Overlay)Zoom=Dungeon?PinnedDungeonZoom:PinnedWorldZoom;
         if(Dungeon)CenterOnPlayer();}
        // The overlay may first open indoors or before the portal DAT is ready.
        if(!Dungeon&&!MapTexture.IsValid()&&Resources.IsValid()&&T>=NextMapRetry)
        {NextMapRetry=T+1;MapTexture.Reset(Resources->ResolveTexture(0x0600127D));MapBrush.SetResourceObject(MapTexture.Get());}
        // A pinned map can reopen before the asynchronous cell DAT is ready.
        // Retry without forcing disk initialization on the rendering thread.
        if(Dungeon&&WasInside&&!CellCount&&T>=NextCellRetry)
        {
            NextCellRetry=T+.5;
            if(auto* Dat=H->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();Dat&&Dat->IsCellReady())
            {FACEDatLandblockInfo Info;if(Dat->LoadLandblockInfo(LB,Info))CellCount=FMath::Min(Info.NumCells,4096u);}
        }
        if(Dungeon && CellIndex<CellCount)LoadDungeonCells();
        if(Dungeon && CellCount && CellIndex==CellCount && !Contours.IsComplete())
        {
            const int32 First=Contours.Lines.Num();Contours.BuildStep();
            for(int32 I=First;I<Contours.Lines.Num();++I)
            {
                const auto& Edge=Contours.Lines[I];FFloor Floor;Floor.Z=(Edge.A.Z+Edge.B.Z)*.5;
                for(auto V:{Edge.A,Edge.B}){FACEPosition P;P.CellId=int32(Landblock|0x100);P.Location=V;Floor.Points.Add(ACEWaypoint::Coordinates(P));}
                Floors.Add(MoveTemp(Floor));
            }
        }
        if(Overlay)CenterOnPlayer();
        if(!Dungeon)if(auto* Dat=H->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();Dat&&Dat->IsCellReady())
        {
            FBox2D Visible(ForceInit);for(const auto Corner:{FVector2D::ZeroVector,FVector2D(LastSize.X,0),LastSize,FVector2D(0,LastSize.Y)})Visible+=Unproject(Corner,LastSize);
            const FVector2D Origin=G.LocalToAbsolute(FVector2D::ZeroVector);
            const double DisplayScale=FMath::Max((FVector2D(G.LocalToAbsolute(FVector2D(1,0)))-Origin).Size(),(FVector2D(G.LocalToAbsolute(FVector2D(0,1)))-Origin).Size());
            Tiles.Update(Dat->GetDatDirectory(),Visible,PixelsPerUnit(LastSize)*DisplayScale);
        }
        if(T>NextMarkers){NextMarkers=T+1;RefreshMarkers();}
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool)const override
    {
        const float Opacity=Overlay&&Host.IsValid()?Host->WaypointMapOpacity():1.f;
        auto DrawLine=[Opacity](FSlateWindowElementList& O,int Layer,const FGeometry& Geo,const TArray<FVector2D>& Points,FLinearColor Color,float Width=1.f){Color.A*=Opacity;Line(O,Layer,Geo,Points,Color,Width);};
        auto DrawText=[Opacity](FSlateWindowElementList& O,int Layer,const FGeometry& Geo,FVector2D At,const FString& S,int Size,FLinearColor Color,bool Center=false){Color.A*=Opacity;Text(O,Layer,Geo,At,S,Size,Color,Center);};
        auto DrawTriangle=[Opacity](FSlateWindowElementList& O,int Layer,const FGeometry& Geo,FVector2D A,FVector2D B,FVector2D C,FLinearColor Color){Color.A*=Opacity;Triangle(O,Layer,Geo,A,B,C,Color);};
        const FVector2D Size=G.GetLocalSize();
        if(!Overlay)FSlateDrawElement::MakeBox(Out,L,G.ToPaintGeometry(),&Background(),ESlateDrawEffect::None,FLinearColor(.015f,.024f,.04f));
        Out.PushClip(FSlateClippingZone(G));
        if(!Dungeon && MapTexture.IsValid())
        {
            PaintTile(Out,L+1,G,FBox2D({-101.95,-101.95},{102.05,102.05}),MapBrush);
        }
        if(!Dungeon)
        {
            for(const auto& Tile:Tiles.GetTiles())
            {
                const auto Bounds=FACEWaypointTiles::Bounds(Tile->Key);
                PaintTile(Out,L+1,G,Bounds,Tile->Brush);
            }
        }
        if(Dungeon)
        {
            for(const auto& Floor:Floors)
            {
                const bool Current=FMath::Abs(Floor.Z-Player.Location.Z)<4;
                TArray<FVector2D> Points;for(auto V:Floor.Points)Points.Add(Project(V,Size));if(Points.Num()>1){DrawLine(Out,L+1,G,Points,Current?FLinearColor(.25f,.72f,.82f):FLinearColor(.12f,.2f,.27f),Current?1.5f:1.f);}
            }
            if(!CellCount)DrawText(Out,L+2,G,{16,16},TEXT("Enter a dungeon or building to see its layout."),16,Muted);
            else if(CellIndex<CellCount || !Contours.IsComplete())DrawText(Out,L+2,G,{16,16},FString::Printf(TEXT("Loading layout: %u / %u"),CellIndex,CellCount),14,Muted);
        }
        PaintedMarkers.Reset();TArray<FBox2D,TInlineAllocator<64>> Labels;
        for(int32 Index=0;Index<Markers.Num();++Index)
        {
            const auto& M=Markers[Index];
            if(M.Detailed && Zoom<3 && Search.IsEmpty())continue;
            const auto P=Project(M.Point,Size);if(P.X<0||P.Y<0||P.X>Size.X||P.Y>Size.Y)continue;
            // Declutter labels only. Nearby portals remain visible and clickable.
            PaintedMarkers.Add(Index);
            const FLinearColor Color=M.Portal?FLinearColor(.85f,.5f,1):FLinearColor(1,.8f,.4f);
            DrawLine(Out,L+2,G,{P+FVector2D(0,-5),P+FVector2D(5,0),P+FVector2D(0,5),P+FVector2D(-5,0),P+FVector2D(0,-5)},Color,2);
            const bool Hovered=(Hover-P).Size()<14;
            if(Zoom>=3 || Hovered)
            {
                const FString Label=Hovered?M.Name:ACEWaypoint::MarkerLabel(M.Name);
                if(M.LabelSize.X<0)M.LabelSize=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(ACEWaypoint::MarkerLabel(M.Name),FCoreStyle::GetDefaultFontStyle("Regular",13));
                const FVector2D Padding(2,1),BoxSize=(Hovered?FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label,FCoreStyle::GetDefaultFontStyle("Regular",13)):M.LabelSize)+Padding*2;
                const FVector2D MaxOrigin=Size-BoxSize-FVector2D(2,2);
                if(MaxOrigin.X<2||MaxOrigin.Y<2)continue;
                FVector2D Origin=P+FVector2D(8,-8)-Padding;
                Origin.X=FMath::Clamp(Origin.X,2.,MaxOrigin.X);
                Origin.Y=FMath::Clamp(Origin.Y,2.,MaxOrigin.Y);
                // Keep complete labels in view and leave space for north at every heading.
                const FBox2D CompassArea({Size.X-72,0},{Size.X,72});
                if(CompassArea.Intersect(FBox2D(Origin,Origin+BoxSize)))
                {
                    Origin.Y=74;
                    if(Origin.Y>MaxOrigin.Y)continue;
                }
                const FVector2D At=Origin+Padding;const FBox2D Box(Origin,Origin+BoxSize);
                if(Hovered||!Labels.ContainsByPredicate([&](const FBox2D& Other){return Other.Intersect(Box);}))
                {
                    Labels.Add(Box);
                    if(!Overlay)FSlateDrawElement::MakeBox(Out,L+3,G.ToPaintGeometry(Box.GetSize(),FSlateLayoutTransform(Box.Min)),&Background(),ESlateDrawEffect::None,FLinearColor(0,0,0,.85f));
                    DrawText(Out,L+4,G,At,Label,13,Ink);
                }
            }
        }
        if(Player.IsValid())
        {
            const auto P=Project(ACEWaypoint::Coordinates(Player),Size);const auto F=Player.GetAceForwardInAcSpace();
            const auto Forward=ACEWaypoint::ToMap({F.X,F.Y},MapUp);const FVector2D Right(-Forward.Y,Forward.X);
            DrawTriangle(Out,L+4,G,P+Forward*10,P-Forward*7+Right*6,P-Forward*7-Right*6,Accent);
        }
        FVector2D Destination;uint32 TargetLB=0;
        if(Host.IsValid()&&Host->GetWaypoint(Destination,TargetLB)&&(!TargetLB || (Dungeon&&TargetLB==Landblock)))
        {
            const auto P=Project(Destination,Size);DrawLine(Out,L+4,G,{P+FVector2D(-8,-8),P+FVector2D(8,8)},FLinearColor::White,2);DrawLine(Out,L+4,G,{P+FVector2D(-8,8),P+FVector2D(8,-8)},FLinearColor::White,2);
        }
        const auto North=ACEWaypoint::ToMap({0,1},MapUp);const FVector2D Compass(Size.X-36,36);
        DrawLine(Out,L+4,G,{Compass,Compass+North*15},Ink,2);DrawText(Out,L+4,G,Compass+North*23,TEXT("N"),12,Ink,true);
        if(!Dungeon&&Tiles.IsLoading())DrawText(Out,L+4,G,{12,Size.Y-22},TEXT("Loading terrain detail…"),13,Ink);
        if(Overlay)
        {
            DrawLine(Out,L+5,G,{{1,Size.Y-1},{1,1},{Size.X-1,1}},FLinearColor(.78f,.65f,.36f),1);
            DrawLine(Out,L+5,G,{{Size.X-1,1},{Size.X-1,Size.Y-1},{1,Size.Y-1}},FLinearColor(.32f,.23f,.10f),1);
        }
        Out.PopClip();return L+5;
    }
private:
    struct FMarker{FVector2D Point;FString Name;bool Portal=false,Detailed=false;mutable FVector2D LabelSize=FVector2D(-1,-1);};
    struct FFloor{TArray<FVector2D> Points;double Z=0;};
    TWeakObjectPtr<UACEPluginSubsystem> Host;
    TStrongObjectPtr<UACEUIResourceResolver> Resources;TStrongObjectPtr<UTexture2D> MapTexture;FSlateBrush MapBrush;
    FACEWaypointTiles Tiles;
    TArray<FMarker> Markers;mutable TArray<int32> PaintedMarkers;TArray<FFloor> Floors;FACEDungeonMapContours Contours;TMap<uint32,FACEDatEnvironment> Environments;
    FACEPosition Player;FVector2D Pan=FVector2D::ZeroVector,Press,Last,Hover=FVector2D(-100,-100),LastSize=FVector2D(560,450);
    FVector2D MapUp=FVector2D(0,1);
    double Zoom=1,PinnedWorldZoom=8,PinnedDungeonZoom=3.5,NextMarkers=0,NextCellRetry=0,NextMapRetry=0;bool Dragged=false,Dungeon=false,WasInside=false,Overlay=false;FString Search;uint32 Landblock=0,CellIndex=0,CellCount=0;
    FVector2D Center()const{return Dungeon?ACEWaypoint::Coordinates(FACEPositionForCenter()):FVector2D::ZeroVector;}
    FACEPosition FACEPositionForCenter()const{FACEPosition P;P.CellId=int32(Landblock|0x100);P.Location=FVector(96,96,0);return P;}
    double PixelsPerUnit(FVector2D Size)const{return FMath::Min(Size.X,Size.Y)*Zoom/(Dungeon?1.5:204.);}
    FVector2D Project(FVector2D P,FVector2D Size)const{return Size*.5+Pan+ACEWaypoint::ToMap((P-Center())*PixelsPerUnit(Size),MapUp);}
    FVector2D Unproject(FVector2D P,FVector2D Size)const{return Center()+ACEWaypoint::FromMap((P-Size*.5-Pan)/PixelsPerUnit(Size),MapUp);}
    void PaintTile(FSlateWindowElementList& Out,int32 L,const FGeometry& G,const FBox2D& Bounds,const FSlateBrush& Brush)const
    {
        const auto Size=G.GetLocalSize();
        const FVector2D Corners[]={Project({Bounds.Min.X,Bounds.Max.Y},Size),Project(Bounds.Max,Size),Project({Bounds.Max.X,Bounds.Min.Y},Size),Project(Bounds.Min,Size)};
        FBox2D Screen(ForceInit);for(const auto& V:Corners)Screen+=V;
        if(!Screen.Intersect(FBox2D(FVector2D::ZeroVector,Size)))return;
        const FVector2f UV[]={{0,0},{1,0},{1,1},{0,1}};TArray<FSlateVertex> Vertices;Vertices.Reserve(4);
        for(int32 I=0;I<4;++I)Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(),FVector2f(Corners[I]),UV[I],FColor(255,255,255,FMath::RoundToInt(255*(Overlay&&Host.IsValid()?Host->WaypointMapOpacity():1.f)))));
        const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush);
        FSlateDrawElement::MakeCustomVerts(Out,L,Resource,Vertices,TArray<SlateIndex>{0,1,2,0,2,3},nullptr,0,0);
    }
    void RefreshMarkers()
    {
        Markers.Reset();auto* H=Host.Get();if(!H)return;
        if(!Dungeon && H->WaypointOption(TEXT("towns")))for(const auto& Town:ACERetailMap::Locations)
            Markers.Add({{(Town.X+Town.Width*.5)/257.*204.-101.95,102.05-(Town.Y+Town.Height*.5)/267.*204.},Town.Name,false});
        if(!Dungeon)for(const auto& M:H->GetWaypointLocations())
        {
            const bool Portal=M.Type.Contains(TEXT("Portal"))||M.Type==TEXT("Dungeon");
            const bool Town=M.Type==TEXT("Town");
            if(H->WaypointOption(Portal?TEXT("portals"):Town?TEXT("towns"):TEXT("pois")))Markers.Add({M.Coordinates,M.Name,Portal,!Town});
        }
        if(H->WaypointOption(TEXT("portals")))if(auto* C=H->GetGameInstance()->GetSubsystem<UACEClientSubsystem>())if(auto Session=C->GetSession())for(const auto& Pair:Session->GetWorldObjects())
        {
            const auto& O=Pair.Value;
            if(!O.Position.IsValid() || !(O.ItemType&ACEItemType::Portal))continue;
            const bool Indoor=(uint32(O.Position.CellId)&65535)>=256;
            if(Dungeon ? (uint32(O.Position.CellId)&0xFFFF0000)!=Landblock : Indoor)continue;
            Markers.Add({ACEWaypoint::Coordinates(O.Position),O.Name,true});
        }
        if(!Search.IsEmpty())Markers.RemoveAll([this](const FMarker& M){return !M.Name.Contains(Search,ESearchCase::IgnoreCase);});
    }
    void LoadDungeonCells()
    {
        auto* Dat=Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();if(!Dat || !Dat->GetPortalDat())return;
        // Only read geometry. Do not spawn world meshes, textures, collision or actors for the map.
        const double Until=FPlatformTime::Seconds()+.002;
        for(int N=0;N<2 && CellIndex<CellCount && FPlatformTime::Seconds()<Until;++N)
        {
            FACEDatEnvCell Cell;if(!Dat->LoadEnvCell(Landblock|(0x100+CellIndex++),Cell))continue;
            if(!Environments.Contains(Cell.EnvironmentId))
            {
                TArray<uint8> Blob;FACEDatEnvironment Env;
                if(!Dat->GetPortalDat()->ReadFile(Cell.EnvironmentId,Blob))continue;FACEDatCursor Cursor(Blob);
                if(!ACEDatUnpack::UnpackEnvironment(Cursor,Env))continue;Environments.Add(Cell.EnvironmentId,MoveTemp(Env));
            }
            const auto* Structure=Environments[Cell.EnvironmentId].Cells.Find(Cell.CellStructure);if(!Structure)continue;
            // Match the actual collision mesh: unused physics faces and draw-only
            // portal planes are not walls and must not close walkable connections.
            for(uint16 Id:Structure->PhysicsCollisionPolyIds)
            {
                const auto* Poly=Structure->PhysicsPolygons.Find(Id);if(!Poly)continue;
                TArray<FVector> Points;
                for(auto Vertex:Poly->VertexIds)if(const auto* V=Structure->Vertices.Find(uint16(Vertex)))Points.Add(FVector(Cell.Orientation.RotateVector(V->Origin)+Cell.Origin));
                if(Points.Num()!=Poly->VertexIds.Num())continue;
                Contours.AddPolygon(Points);
            }
        }
    }
};

class SWaypointPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWaypointPanel){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(bool,Map) SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {
        Host=A._Host;
        auto Body=SNew(SVerticalBox);Body->AddSlot().AutoHeight()[WaypointLabel(A._Map?TEXT("Explore Dereth"):TEXT("Waypoint"),26)];
        auto Input=SNew(SHorizontalBox);
        Input->AddSlot().FillWidth(1)[SAssignNew(Entry,SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).HintText(FText::FromString(TEXT("42.0N, 33.6E")))
            .OnTextCommitted_Lambda([this](const FText& T,ETextCommit::Type Type){if(Type==ETextCommit::OnEnter)Navigate(T.ToString());})];
        Input->AddSlot().AutoWidth().Padding(8,0)[WaypointButton(TEXT("Go"),[this]{Navigate(Entry->GetText().ToString());})];
        Input->AddSlot().AutoWidth().Padding(4,0)[WaypointButton(TEXT("Clear"),[this]{if(Host.IsValid())Host->ClearWaypoint();})];
        Body->AddSlot().AutoHeight().Padding(0,12)[Input];
        if(A._Map)
        {
            auto Controls=SNew(SHorizontalBox);
            Controls->AddSlot().AutoWidth()[Check(TEXT("dungeon"),TEXT("Dungeon"),false)];
            Controls->AddSlot().AutoWidth()[Check(TEXT("towns"),TEXT("Towns"),true)];
            Controls->AddSlot().AutoWidth()[Check(TEXT("portals"),TEXT("Portals"),true)];
            Controls->AddSlot().AutoWidth()[Check(TEXT("pois"),TEXT("POIs"),true)];
            Body->AddSlot().AutoHeight()[Controls];
            Body->AddSlot().AutoHeight().Padding(0,6)[Check(TEXT("heading_up"),TEXT("Player facing up (off: north up)"),false)];
            Body->AddSlot().AutoHeight()[Check(TEXT("dungeon_overlay"),TEXT("Pin map to interface"),false)];
            Body->AddSlot().AutoHeight()[Check(TEXT("map_minimized"),TEXT("Minimize pinned map (keep direction arrow)"),false)];
            Body->AddSlot().AutoHeight().Padding(0,6)[WaypointLabel(TEXT("Pinned map opacity"),14)];
            Body->AddSlot().AutoHeight()[SNew(SSlider).MinValue(.1f).MaxValue(1.f)
                .Value_Lambda([this](){return Host.IsValid()?Host->WaypointMapOpacity():1.f;})
                .OnValueChanged_Lambda([this](float Value){if(Host.IsValid())Host->SetWaypointMapOpacity(Value);})];
            Body->AddSlot().AutoHeight()[WaypointLabel(TEXT("The pinned map follows you, automatically switches between overworld and interior layouts, and stays visible when this window closes. Both views share the same position and size. Use the game UI lock/unlock icon to move, resize or zoom it. Uncheck Pin to hide it."),13)];
            Body->AddSlot().AutoHeight().Padding(0,6)[SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).HintText(FText::FromString(TEXT("Filter locations by name"))).OnTextChanged_Lambda([this](const FText& T){if(Map)Map->SetSearch(T.ToString());})];
            Body->AddSlot().FillHeight(1).Padding(0,10)[SAssignNew(Map,SWaypointMap).Host(Host.Get())];
            Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[WaypointButton(TEXT("+"),[this]{Map->ChangeZoom(1.5,Map->GetCachedGeometry().GetLocalSize()*.5);})]
                +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[WaypointButton(TEXT("−"),[this]{Map->ChangeZoom(1/1.5,Map->GetCachedGeometry().GetLocalSize()*.5);})]
                +SHorizontalBox::Slot().AutoWidth()[WaypointButton(TEXT("Center on me"),[this]{Map->CenterOnPlayer();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[WaypointButton(TEXT("Reset view"),[this]{Map->ResetView();})]];
            Body->AddSlot().AutoHeight().Padding(0,8)[WaypointButton(TEXT("Waypoint settings"),[this]{if(Host.IsValid())ChildSlot[Host->MakeWaypointPanel()];})];
            Body->AddSlot().AutoHeight().Padding(0,8)[WaypointLabel(TEXT("Drag to pan · Scroll to zoom · Click to navigate\nTowns and nearby portals. Atlas detail appears when zoomed in or searched. Dungeon floors near your elevation are brighter."),13)];
        }
        else
        {
            Body->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(190)[SNew(SWaypointArrow).Host(Host.Get())]];
            Body->AddSlot().AutoHeight().Padding(0,8)[Check(TEXT("distance"),TEXT("Show distance to destination"),true)];
            Body->AddSlot().AutoHeight()[Check(TEXT("arrow"),TEXT("Show floating arrow"),true)];
            Body->AddSlot().AutoHeight().Padding(0,8)[Check(TEXT("locked"),TEXT("Lock arrow position"),false)];
            Body->AddSlot().AutoHeight()[WaypointButton(TEXT("Open map"),[this]{if(Host.IsValid())ChildSlot[Host->MakeWaypointPanel(true)];})];
            Body->AddSlot().AutoHeight().Padding(0,12)[WaypointLabel(TEXT("GoArrow atlas (optional)"),18)];
            Body->AddSlot().AutoHeight()[SAssignNew(AtlasPath,SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).HintText(FText::FromString(TEXT("Full path to GoArrow locations.xml")))];
            Body->AddSlot().AutoHeight().Padding(0,6)[WaypointButton(TEXT("Import locations"),[this]{if(Host.IsValid()){Host->ImportWaypointLocations(AtlasPath->GetText().ToString().TrimStartAndEnd());Status=Host->Notice;}})];
            Body->AddSlot().AutoHeight().Padding(0,12)[WaypointLabel(TEXT("Click coordinates in chat or enter them above. The arrow shows your current coordinates. Unlock the game UI and drag its title to move it; its position is saved."),15)];
        }
        Body->AddSlot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(Accent).AutoWrapText(true)
            .Text_Lambda([this]{return FText::FromString(Status);})];
        TSharedRef<SWidget> Content=Body;
        if(!A._Map)Content=SNew(SScrollBox)+SScrollBox::Slot()[Body];
        ChildSlot[SNew(SBorder).BorderImage(&Background()).Padding(18)[Content]];
    }
private:
    TWeakObjectPtr<UACEPluginSubsystem> Host;TSharedPtr<SEditableTextBox> Entry,AtlasPath;TSharedPtr<SWaypointMap> Map;FString Status;
    void Navigate(const FString& S){if(Host.IsValid())Status=Host->SetWaypoint(S)?TEXT("Destination set"):Host->Notice;}
    TSharedRef<SWidget> Check(const FString& Key,const FString& Caption,bool Default)
    {return SNew(SCheckBox).Padding(6).IsChecked_Lambda([this,Key,Default]{return Host.IsValid()&&Host->WaypointOption(Key,Default)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([this,Key](ECheckBoxState V){if(Host.IsValid())Host->SetWaypointOption(Key,V==ECheckBoxState::Checked);})[WaypointLabel(Caption)];}
};
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeWaypointPanel(bool bMap,bool bArrowOnly)
{if(bArrowOnly)return SNew(SWaypointArrow).Host(this);return SNew(SWaypointPanel).Host(this).Map(bMap);}
TSharedRef<SWidget> UACEPluginSubsystem::MakeWaypointMapIcon(){return SNew(SWaypointMapIcon);}
TSharedRef<SWidget> UACEPluginSubsystem::MakeWaypointDungeonOverlay()
{
    const TWeakObjectPtr<UACEPluginSubsystem> H(this);
    return SNew(SOverlay)
        +SOverlay::Slot()[SNew(SWaypointMap).Host(this).Overlay(true)
            .Visibility_Lambda([H](){return !H.IsValid()||H->WaypointOption(TEXT("map_minimized"),false)?EVisibility::Collapsed:H->IsWaypointMapUnlocked()?EVisibility::Visible:EVisibility::HitTestInvisible;})]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top)[SNew(SButton).IsFocusable(false).ContentPadding(FMargin(5,1))
            .ToolTipText(FText::FromString(TEXT("Minimize / restore map; direction arrow stays visible")))
            .OnClicked_Lambda([H](){if(H.IsValid())H->SetWaypointOption(TEXT("map_minimized"),!H->WaypointOption(TEXT("map_minimized"),false));return FReply::Handled();})
            [SNew(STextBlock).Text_Lambda([H](){return FText::FromString(H.IsValid()&&H->WaypointOption(TEXT("map_minimized"),false)?TEXT("Map +"):TEXT("−"));})]];
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
namespace
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWaypointOverlayTest,"ACE.Plugins.Waypoint.PinnedDungeon",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEWaypointOverlayTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();
    ON_SCOPE_EXIT{GI->Shutdown();};
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    TSharedPtr<FACEClientPlugin> Plugin;
    for(const auto& Entry:Host->Plugins)if(Entry->Id==TEXT("waypoint"))Plugin=Entry;
    if(!Plugin||!C->GetSession())return false;
    Plugin->Profile=MakeShared<FJsonObject>();
    auto Map=SNew(SWaypointMap).Host(Host).Overlay(true);
    double Time=1;
    for(bool Facing:{false,true})for(int Width:{260,560})for(uint32 Cell:{0x7D640014u,0x01430171u,0x01430172u,0x7D640100u,0x7D64001Au})
    {
        FACEPosition P;P.CellId=int32(Cell);P.Location=FVector(49+Time,-75+Time,0);
        P.RotationW=FMath::Cos(.4);P.RotationXYZ=FVector(0,0,FMath::Sin(.4));
        C->GetSession()->SetLocalPosition(P);Plugin->Profile->SetBoolField(TEXT("heading_up"),Facing);
        const FVector2D Size(Width,320);const auto Geometry=FGeometry::MakeRoot(Size,FSlateLayoutTransform());
        Map->Tick(Geometry,Time++,.016f);
        Plugin->Profile->SetBoolField(TEXT("dungeon"),!((Cell&65535)>=256));
        Map->Tick(Geometry,Time++,.016f);
        TestEqual(TEXT("Pinned map automatically chooses world or interior independently of normal map"),Map->Dungeon,(Cell&65535)>=256);
        TestTrue(TEXT("Player stays at center after movement, turn, resize and landblock change"),Map->Project(ACEWaypoint::Coordinates(P),Size).Equals(Size*.5,.001));
        const auto Forward=P.GetAceForwardInAcSpace();
        TestTrue(TEXT("Pinned orientation obeys the shared setting"),Map->MapUp.Equals(Facing?FVector2D(Forward.X,Forward.Y).GetSafeNormal():FVector2D(0,1)));
        Map->ChangeZoom(1.25,{25,50});
        TestTrue(TEXT("Off-center wheel zoom cannot displace the tracked player"),Map->Project(ACEWaypoint::Coordinates(P),Size).Equals(Size*.5,.001));
    }
    {
        const double WorldZoom=Map->Zoom;
        auto P=C->GetPlayerPosition();P.CellId=0x01430171;C->GetSession()->SetLocalPosition(P);
        const auto Geometry=FGeometry::MakeRoot(FVector2D(420,420),FSlateLayoutTransform());
        Map->Tick(Geometry,Time++,.016f);Map->ChangeZoom(.8,{40,40});const double InteriorZoom=Map->Zoom;
        P.CellId=0x7D640014;C->GetSession()->SetLocalPosition(P);Map->Tick(Geometry,Time++,.016f);
        TestEqual(TEXT("Returning outside restores overworld zoom"),Map->Zoom,WorldZoom);
        TestTrue(TEXT("Leaving interior discards stale floor geometry"),Map->Floors.IsEmpty()&&Map->CellCount==0);
        P.CellId=0x01430171;C->GetSession()->SetLocalPosition(P);Map->Tick(Geometry,Time++,.016f);
        TestEqual(TEXT("Returning inside restores interior zoom"),Map->Zoom,InteriorZoom);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("WaypointRender")))
    {
        auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
        Dat->SetWorldStreamingAllowed(true);
        const double Deadline=FPlatformTime::Seconds()+15;
        while(Dat->IsDatLoading()&&FPlatformTime::Seconds()<Deadline)
        {FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);FPlatformProcess::Sleep(.005f);}
        if(!TestTrue(TEXT("Load real dungeon geometry"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
        FACEPosition P;P.CellId=0x01430171;P.Location=FVector(49,-75,0);
        P.RotationW=FMath::Cos(.4);P.RotationXYZ=FVector(0,0,FMath::Sin(.4));C->GetSession()->SetLocalPosition(P);
        const FVector2D Size(420,420);FWidgetRenderer Renderer(false,true);
        auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        Target->ClearColor=FLinearColor::Transparent;
        for(bool Facing:{false,true})
        {
            Plugin->Profile->SetBoolField(TEXT("heading_up"),Facing);
            auto Overlay=SNew(SWaypointMap).Host(Host).Overlay(true);
            // Multiple DrawWidget calls in one engine frame do not necessarily
            // tick Slate again. Advance streaming explicitly for this fixture.
            const auto Geometry=FGeometry::MakeRoot(Size,FSlateLayoutTransform());
            for(int Pass=0;Pass<2100;++Pass)
            {
                Overlay->Tick(Geometry,Time+=.016,.016f);
                if(Overlay->CellCount&&Overlay->CellIndex==Overlay->CellCount&&Overlay->Contours.IsComplete())break;
            }
            for(int Pass=0;Pass<3;++Pass){Renderer.DrawWidget(Target,Overlay,Size,.016f);FlushRenderingCommands();}
            TestTrue(TEXT("Pinned fixture contains dungeon floor outlines"),Overlay->Floors.Num()>0);
            TestTrue(TEXT("Real dungeon removes internal floor seams"),Overlay->Contours.Lines.Num()<Overlay->Contours.InputEdgeCount());
            AddInfo(FString::Printf(TEXT("Dungeon %08X: %d floor edges -> %d contour segments"),uint32(P.CellId),Overlay->Contours.InputEdgeCount(),Overlay->Contours.Lines.Num()));
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
            int32 Transparent=0;for(const auto& Pixel:Pixels)if(Pixel.A==0)++Transparent;
            TestTrue(TEXT("Pinned layout leaves the game visible behind its lines"),Transparent>Pixels.Num()*3/4);
            TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(420,420,Pixels,Png);
            FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("Automation")/FString::Printf(TEXT("Waypoint-Pinned-%s.png"),Facing?TEXT("Facing"):TEXT("North"))));
            P.CellId=0x7D640014;P.Location=FVector(60,78,12);C->GetSession()->SetLocalPosition(P);
            Overlay->Tick(Geometry,Time+=.016,.016f);
            TestTrue(TEXT("Pinned overworld resolves retail overview after leaving dungeon"),!Overlay->Dungeon&&Overlay->MapTexture.IsValid());
            for(int Pass=0;Pass<3;++Pass){Renderer.DrawWidget(Target,Overlay,Size,.016f);FlushRenderingCommands();}
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
            int32 Opaque=0;for(const auto& Pixel:Pixels)if(Pixel.A>0)++Opaque;
            TestTrue(TEXT("Pinned overworld renders terrain across the overlay"),Opaque>Pixels.Num()*3/4);
            FImageUtils::PNGCompressImageArray(420,420,Pixels,Png);
            FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("Automation")/FString::Printf(TEXT("Waypoint-Pinned-World-%s.png"),Facing?TEXT("Facing"):TEXT("North"))));
            P.CellId=0x01430171;P.Location=FVector(49,-75,0);C->GetSession()->SetLocalPosition(P);
        }
        // Nearby portals must all survive label decluttering, including those
        // inside the same 12px screen bucket used by the old implementation.
        auto Overlay=SNew(SWaypointMap).Host(Host).Overlay(true);
        P.CellId=0x7D640014;P.Location=FVector(60,78,12);C->GetSession()->SetLocalPosition(P);
        Overlay->Tick(FGeometry::MakeRoot(Size,FSlateLayoutTransform()),Time+=.016,.016f);
        Overlay->NextMarkers=TNumericLimits<double>::Max(); // Keep the synthetic cluster during Slate's render tick.
        const auto Here=ACEWaypoint::Coordinates(Overlay->Player);
        Overlay->Markers={{Here,TEXT("Portal to Holtburg"),true},{Here+FVector2D(.1,0),TEXT("Portal to Yaraq"),true},{Here+FVector2D(0,.1),TEXT("Portal to Arwic"),true}};
        Renderer.DrawWidget(Target,Overlay,Size,.016f);FlushRenderingCommands();
        TestEqual(TEXT("All closely spaced portals remain drawn and selectable"),Overlay->PaintedMarkers.Num(),3);
        TArray<FColor> OpaquePixels;FReadSurfaceDataFlags OpacityFlags;OpacityFlags.SetLinearToGamma(false);
        Target->GameThread_GetRenderTargetResource()->ReadPixels(OpaquePixels,OpacityFlags);
        uint64 FullAlpha=0;for(const auto& Pixel:OpaquePixels)FullAlpha+=Pixel.A;
        Plugin->Profile->SetNumberField(TEXT("map_opacity"),.4);
        Renderer.DrawWidget(Target,Overlay,Size,.016f);FlushRenderingCommands();
        Target->GameThread_GetRenderTargetResource()->ReadPixels(OpaquePixels,OpacityFlags);
        uint64 FadedAlpha=0;for(const auto& Pixel:OpaquePixels)FadedAlpha+=Pixel.A;
        TestTrue(TEXT("Opacity fades terrain and overlays together"),FullAlpha>0&&FadedAlpha<FullAlpha*.8);
        Plugin->Profile->SetNumberField(TEXT("map_opacity"),1);
        Target->ReleaseResource();
    }
    return true;
}
}
#endif
