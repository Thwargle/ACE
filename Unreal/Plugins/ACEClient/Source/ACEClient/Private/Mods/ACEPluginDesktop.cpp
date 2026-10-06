#include "Mods/ACEPluginDesktop.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

namespace
{
    const FString BarId = TEXT("__bar");
    const FSlateBrush& FrameBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor(.025f,.035f,.045f,1),6.f,FLinearColor(.18f,.35f,.46f,1),1.f);
        return Brush;
    }
    const FButtonStyle& DockButtonStyle()
    {
        static const FButtonStyle Style = FButtonStyle()
            .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.06f,.07f,.08f,1),2.f,FLinearColor(.4f,.34f,.2f,1),1.f))
            .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.17f,.19f,.22f,1),2.f,FLinearColor(1,.8f,.3f,1),2.f))
            .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.28f,.22f,.08f,1),2.f));
        return Style;
    }
    TSharedRef<STextBlock> Caption(const FString& Text, int32 Size = 12)
    { return SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)); }

    class SPluginFrame : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SPluginFrame) {}
            SLATE_ARGUMENT(FString, Title)
            SLATE_ARGUMENT(bool, Frameless)
            SLATE_ARGUMENT(TFunction<void(FVector2D,bool)>, Move)
            SLATE_ARGUMENT(TFunction<void(FVector2D,bool)>, Resize)
            SLATE_ARGUMENT(TFunction<void()>, Raise)
            SLATE_ARGUMENT(TFunction<void()>, Close)
            SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Move = Args._Move; Resize = Args._Resize; Raise = Args._Raise;
            Frameless=Args._Frameless;
            if(Frameless){ChildSlot[Args._Content.Widget];return;}
            auto Title = SNew(SHorizontalBox);
            Title->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Caption(Args._Title)];
            if (Args._Close) Title->AddSlot().AutoWidth()[SNew(SButton).IsFocusable(false).ButtonStyle(&DockButtonStyle())
                .ToolTipText(FText::FromString(TEXT("Hide window (plugin keeps running)")))
                .OnClicked_Lambda([Close=Args._Close](){Close();return FReply::Handled();})[Caption(TEXT("X"))]];
            ChildSlot[SNew(SBorder).BorderImage(&FrameBrush()).Padding(3)
                [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(24)
                    [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(.035f,.065f,.09f,1)).Padding(3)
                    .ToolTipText(FText::FromString(TEXT("Drag to move")))
                    .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& E)
                    {
                        if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
                        Dragging=true;return FReply::Handled().CaptureMouse(SharedThis(this));
                    })[Title]]]
                +SVerticalBox::Slot().FillHeight(1).Padding(0,3)[Args._Content.Widget]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SBorder).Padding(FMargin(12,2)).Visibility(Resize?EVisibility::Visible:EVisibility::Collapsed)
                    .ToolTipText(FText::FromString(TEXT("Drag to resize"))).Cursor(EMouseCursor::ResizeSouthEast)
                    .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent& E){if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();Resizing=true;return FReply::Handled().CaptureMouse(SharedThis(this));})[Caption(TEXT("◢"))]]]];
        }
        virtual FReply OnPreviewMouseButtonDown(const FGeometry&,const FPointerEvent&) override
        { if(Raise)Raise();return FReply::Unhandled(); }
        virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override
        {
            if(!Frameless||E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
            const auto At=G.AbsoluteToLocal(E.GetScreenSpacePosition());
            Resizing=Resize&&At.X>=G.GetLocalSize().X-24&&At.Y>=G.GetLocalSize().Y-24;
            Dragging=!Resizing;return FReply::Handled().CaptureMouse(SharedThis(this));
        }
        virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E) override
        {
            if((!Dragging&&!Resizing)||!HasMouseCapture())return FReply::Unhandled();
            auto& Action=Resizing?Resize:Move;
            Action(G.AbsoluteToLocal(E.GetScreenSpacePosition())-G.AbsoluteToLocal(E.GetLastScreenSpacePosition()),false);
            return FReply::Handled();
        }
        virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent& E) override
        {
            if((!Dragging&&!Resizing)||E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
            auto& Action=Resizing?Resize:Move;Action(FVector2D::ZeroVector,true);Dragging=Resizing=false;return FReply::Handled().ReleaseMouseCapture();
        }
        virtual void OnMouseCaptureLost(const FCaptureLostEvent& E) override
        { if(Dragging)Move(FVector2D::ZeroVector,true);if(Resizing&&Resize)Resize(FVector2D::ZeroVector,true);Dragging=Resizing=false;SCompoundWidget::OnMouseCaptureLost(E); }
    private:
        bool Dragging=false,Resizing=false,Frameless=false;
        TFunction<void(FVector2D,bool)> Move,Resize;
        TFunction<void()> Raise;
    };
}

void SACEPluginDesktop::Construct(const FArguments& Args)
{
    Host=Args._Host;
    SetVisibility(EVisibility::SelfHitTestInvisible);
    SetClipping(EWidgetClipping::ClipToBounds);
    ChildSlot[SAssignNew(Canvas,SConstraintCanvas)];
    FWindow Bar;
    Bar.Position=Host->GetPluginWindowPosition(BarId,FVector2D(8,80));Bar.Size=FVector2D(62,120);
    Bar.Widget=SNew(SPluginFrame).Title(TEXT("Mods"))
        .Move([this](FVector2D D,bool Save){Move(BarId,D,Save);})
        .Raise([this](){Raise(BarId);})
        [SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Buttons,SVerticalBox)]];
    Canvas->AddSlot().Expose(Bar.Slot).Alignment(FVector2D::ZeroVector).ZOrder(10000)[Bar.Widget.ToSharedRef()];
    Windows.Add(BarId,Bar);
    Refresh();
}
void SACEPluginDesktop::Layout(FWindow& W)
{
    if(ViewSize.X<=0||ViewSize.Y<=0)
    {W.Slot->SetOffset(FMargin(W.Position.X,W.Position.Y,W.Size.X,W.Size.Y));return;}
    const FVector2D Size(FMath::Min(W.Size.X,ViewSize.X),FMath::Min(W.Size.Y,ViewSize.Y));
    // Fit the rendered window without overwriting the user's saved placement
    // during login, minimization or a temporary viewport size change.
    const FVector2D At(FMath::Clamp(W.Position.X,0.,FMath::Max(0.,ViewSize.X-Size.X)),
        FMath::Clamp(W.Position.Y,0.,FMath::Max(0.,ViewSize.Y-Size.Y)));
    W.Slot->SetOffset(FMargin(At.X,At.Y,Size.X,Size.Y));
}
void SACEPluginDesktop::Tick(const FGeometry& G,double Time,float Delta)
{
    SCompoundWidget::Tick(G,Time,Delta);
    if(auto* Arrow=Windows.Find(TEXT("waypoint.arrow"));Arrow&&Arrow->Open&&Host.IsValid())
        Arrow->Widget->SetVisibility(Host->IsWaypointUnlocked()?EVisibility::Visible:EVisibility::HitTestInvisible);
    if(auto* Map=Windows.Find(TEXT("waypoint.dungeon"));Map&&Map->Open&&Host.IsValid())
    {
        const auto P=Host->WaypointPlayerPosition();
        Map->Widget->SetVisibility(!P.IsValid()?EVisibility::Collapsed:Host->IsWaypointMapUnlocked()?EVisibility::Visible:EVisibility::HitTestInvisible);
    }
    if(!G.GetLocalSize().Equals(ViewSize))
    {ViewSize=G.GetLocalSize();for(auto& Pair:Windows)Layout(Pair.Value);}
}
void SACEPluginDesktop::Move(const FString& Id,FVector2D Delta,bool Save)
{
    if(auto* W=Windows.Find(Id))
    {
        if(!Delta.IsNearlyZero())
        {
            const auto Offset=W->Slot->GetOffset();W->Position=FVector2D(Offset.Left,Offset.Top)+Delta;
            if(ViewSize.X>0&&ViewSize.Y>0)
            {W->Position.X=FMath::Clamp(W->Position.X,0.,FMath::Max(0.,ViewSize.X-W->Size.X));W->Position.Y=FMath::Clamp(W->Position.Y,0.,FMath::Max(0.,ViewSize.Y-W->Size.Y));}
        }
        Layout(*W);if(Save&&Host.IsValid())Host->SavePluginWindowPosition(Id,W->Position);
    }
}
void SACEPluginDesktop::SaveOverlayLayout()
{
    if(!Host.IsValid())return;
    for(const TCHAR* Id:{TEXT("waypoint.arrow"),TEXT("waypoint.dungeon")})if(const auto* W=Windows.Find(Id))
    {
        Host->SavePluginWindowPosition(Id,W->Position);
        if(FString(Id)==TEXT("waypoint.dungeon"))Host->SavePluginWindowPosition(FString(Id)+TEXT(".size"),W->Size);
    }
}
void SACEPluginDesktop::Resize(const FString& Id,FVector2D Delta,bool Save)
{
    if(auto* W=Windows.Find(Id))
    {W->Size+=Delta;const double MinWidth=Id==TEXT("waypoint.dungeon")?220.:400.;W->Size.X=FMath::Clamp(W->Size.X,MinWidth,FMath::Max(MinWidth,ViewSize.X));const double MinHeight=Id==TEXT("waypoint.map")?640.:Id==TEXT("waypoint.dungeon")?220.:320.;W->Size.Y=FMath::Clamp(W->Size.Y,MinHeight,FMath::Max(MinHeight,ViewSize.Y));Layout(*W);
     if(Save&&Host.IsValid())Host->SavePluginWindowPosition(Id+TEXT(".size"),W->Size);}
}
void SACEPluginDesktop::Raise(const FString& Id)
{if(Id!=BarId)if(auto* W=Windows.Find(Id))W->Slot->SetZOrder(++TopOrder);}
bool SACEPluginDesktop::IsOpen(const FString& Id) const
{const auto* W=Windows.Find(Id);return W&&W->Open;}
void SACEPluginDesktop::Hide(const FString& Id)
{
    if(auto* W=Windows.Find(Id))
    {
        const bool Focused=W->Widget->HasKeyboardFocus()||W->Widget->HasFocusedDescendants();
        W->Open=false;W->Widget->SetVisibility(EVisibility::Collapsed);
        if(Focused)FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}
void SACEPluginDesktop::Toggle(const FString& Id)
{
    if(!Host.IsValid())return;
    TSharedPtr<FACEClientPlugin> Plugin;
    for(auto P:Host->Plugins)if((P->Id==Id || (Id.StartsWith(TEXT("waypoint."))&&P->Id==TEXT("waypoint")))&&P->Enabled)Plugin=P;
    if(!Plugin)return;
    if(auto* W=Windows.Find(Id))
    {
        if(W->Open)Hide(Id);
        else {W->Open=true;W->Widget->SetVisibility(EVisibility::Visible);Raise(Id);}
        return;
    }
    FWindow W;W.Position=Host->GetPluginWindowPosition(Id,FVector2D(84+24*(Windows.Num()-1),80+24*(Windows.Num()-1)));W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(780,700));
    const bool Arrow=Id==TEXT("waypoint.arrow");
    const bool Dungeon=Id==TEXT("waypoint.dungeon");
    if(Arrow)W.Size=FVector2D(290,225);
    if(Dungeon)W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(420,420));
    W.Widget=SNew(SPluginFrame).Title(Arrow?TEXT("Waypoint · drag when unlocked"):Id==TEXT("waypoint.map")?TEXT("Waypoint Map"):Plugin->Name)
        .Frameless(Arrow||Dungeon)
        .Move([this,Id,Arrow,Dungeon](FVector2D D,bool Save){if(Save||(Dungeon?Host->IsWaypointMapUnlocked():!Arrow||Host->IsWaypointUnlocked()))Move(Id,D,Save);})
        .Resize(Arrow?TFunction<void(FVector2D,bool)>():TFunction<void(FVector2D,bool)>([this,Id,Dungeon](FVector2D D,bool Save){if(Save||!Dungeon||Host->IsWaypointMapUnlocked())Resize(Id,D,Save);}))
        .Raise([this,Id](){Raise(Id);}).Close([this,Id,Arrow](){Hide(Id);if(Arrow)Host->SetWaypointOption(TEXT("arrow"),false);})[Dungeon?Host->MakeWaypointDungeonOverlay():Arrow?Host->MakeWaypointPanel(false,true):Host->MakePanel(Id)];
    if(Dungeon)W.Widget->SetVisibility(EVisibility::Collapsed); // Tick validates the current cell before showing it.
    if(Arrow)W.Widget->SetVisibility(TAttribute<EVisibility>::CreateLambda([H=Host](){return H.IsValid()&&H->WaypointOption(TEXT("arrow"))?(H->IsWaypointUnlocked()?EVisibility::Visible:EVisibility::HitTestInvisible):EVisibility::Collapsed;}));
    Canvas->AddSlot().Expose(W.Slot).Alignment(FVector2D::ZeroVector).ZOrder(++TopOrder)[W.Widget.ToSharedRef()];Layout(W);Windows.Add(Id,W);
}
void SACEPluginDesktop::Refresh()
{
    if(!Host.IsValid())return;
    FString Next;TSet<FString> Enabled;
    for(auto P:Host->Plugins)if(P->Enabled){Next+=P->Id+TEXT("|");Enabled.Add(P->Id);}
    if(Enabled.Contains(TEXT("waypoint")))
    {
        Enabled.Add(TEXT("waypoint.map"));Enabled.Add(TEXT("waypoint.arrow"));Enabled.Add(TEXT("waypoint.dungeon"));
        const bool Show=Host->WaypointOption(TEXT("arrow"));
        if(Show && !IsOpen(TEXT("waypoint.arrow")))Toggle(TEXT("waypoint.arrow"));
        else if(!Show && IsOpen(TEXT("waypoint.arrow")))Hide(TEXT("waypoint.arrow"));
        const bool Pin=Host->WaypointOption(TEXT("dungeon_overlay"));
        if(Pin&&!IsOpen(TEXT("waypoint.dungeon")))Toggle(TEXT("waypoint.dungeon"));
        else if(!Pin&&IsOpen(TEXT("waypoint.dungeon")))Hide(TEXT("waypoint.dungeon"));
    }
    if(Next==Signature&&Buttons->NumSlots())return;
    Signature=Next;
    for(auto It=Windows.CreateIterator();It;++It)if(It.Key()!=BarId&&!Enabled.Contains(It.Key()))
    {Hide(It.Key());Canvas->RemoveSlot(It.Value().Widget.ToSharedRef());It.RemoveCurrent();}
    Buttons->ClearChildren();
    Buttons->AddSlot().AutoHeight().Padding(1)[SNew(SBox).HeightOverride(34)
        [SNew(SButton).ButtonStyle(&DockButtonStyle()).IsFocusable(false).HAlign(HAlign_Center)
        .ToolTipText(FText::FromString(TEXT("Manage plugins")))
        .OnClicked_Lambda([this](){if(Host.IsValid())Host->TogglePanel();return FReply::Handled();})[Caption(TEXT("+"),18)]]];
    for(auto P:Host->Plugins)if(P->Enabled)
    {
        FString Short;P->Manifest->TryGetStringField(TEXT("short_name"),Short);if(Short.IsEmpty())Short=P->Id.ToUpper();Short=Short.Left(4);
        Buttons->AddSlot().AutoHeight().Padding(1)[SNew(SBox).HeightOverride(42)
            [SNew(SButton).ButtonStyle(&DockButtonStyle()).IsFocusable(false).HAlign(HAlign_Center).ContentPadding(2)
            .ToolTipText_Lambda([this,P]()
            {
                if(P->Id==TEXT("looteditor"))
                {
                    TSharedPtr<FACEClientPlugin> UCM;bool Looting=false;
                    if(Host.IsValid())for(auto Plugin:Host->Plugins)if(Plugin->Id==TEXT("ucm")){UCM=Plugin;break;}
                    if(UCM)UCM->Profile->TryGetBoolField(TEXT("looting"),Looting);
                    return FText::FromString(P->Name+TEXT("\nClick to open/close the editor. This does not toggle looting.\nUCM looting: ")
                        +(Looting?TEXT("enabled"):TEXT("disabled"))+(UCM&&UCM->Running?TEXT(" (UCM running)"):TEXT(" (UCM stopped)")));
                }
                return FText::FromString(P->Name+TEXT("\nClick to show/hide\n")+P->Status);
            })
            .OnClicked_Lambda([this,Id=P->Id](){Toggle(Id);return FReply::Handled();})
            [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [SNew(STextBlock).Text(FText::FromString(Short)).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))
                .ColorAndOpacity_Lambda([this,Id=P->Id](){return IsOpen(Id)?FSlateColor(FLinearColor(1,.8f,.25f)):FSlateColor(FLinearColor::White);})]
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",8))
                .Text_Lambda([this,P](){return FText::FromString(P->Id==TEXT("looteditor")?TEXT("EDITOR"):P->Running?TEXT("RUN"):IsOpen(P->Id)?TEXT("OPEN"):TEXT("HIDDEN"));})
                .ColorAndOpacity_Lambda([P](){return FSlateColor(P->Running?FLinearColor(.3f,1,.4f):FLinearColor(.7f,.7f,.7f));})]]]];
    }
    if(Host->IsWaypointEnabled())Buttons->AddSlot().AutoHeight().Padding(1)[SNew(SBox).HeightOverride(42)
        [SNew(SButton).ButtonStyle(&DockButtonStyle()).IsFocusable(false).ToolTipText(FText::FromString(TEXT("Waypoint world / dungeon map")))
        .OnClicked_Lambda([this](){Toggle(TEXT("waypoint.map"));return FReply::Handled();})[Host->MakeWaypointMapIcon()]]];
    auto& Bar=Windows.FindChecked(BarId);Bar.Size.Y=34+44*Buttons->NumSlots();Layout(Bar);
}

void UACEPluginSubsystem::UpdateDesktopDock()
{
    auto* GI=GetGameInstance();auto* V=GI->GetGameViewportClient();
    auto* PC=Cast<AACEPlayerController>(GI->GetFirstLocalPlayerController());
    auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    if(!V||!PC||!C||C->GetSessionState()!=EACESessionState::InWorld||PC->IsVRActive())
    {RemoveDesktopDock();return;}
    if(!DesktopDock)
    {
        DesktopDock=SNew(SACEPluginDesktop).Host(this);
        DesktopDock->SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak=TWeakObjectPtr<AACEPlayerController>(PC)]()
            {return Weak.IsValid()&&!Weak->IsDesktopInterfaceHidden()?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;}));
        V->AddViewportWidgetContent(DesktopDock.ToSharedRef(),1500);
    }
    DesktopDock->Refresh();
}
void UACEPluginSubsystem::RemoveDesktopDock()
{
    if(DesktopDock)DesktopDock->SaveOverlayLayout();
    if(DesktopDock)if(auto* V=GetGameInstance()->GetGameViewportClient())V->RemoveViewportWidgetContent(DesktopDock.ToSharedRef());
    DesktopDock.Reset();
}
void UACEPluginSubsystem::TogglePluginWindow(const FString& Id)
{UpdateDesktopDock();if(DesktopDock)DesktopDock->Toggle(Id);}
bool UACEPluginSubsystem::IsPluginWindowOpen(const FString& Id) const
{return DesktopDock&&DesktopDock->IsOpen(Id);}
FVector2D UACEPluginSubsystem::GetPluginWindowPosition(const FString& Id,const FVector2D& Default) const
{
    const TSharedPtr<FJsonObject>* Positions=nullptr;
    const TSharedPtr<FJsonObject>* Point=nullptr;double X,Y;
    if(Settings&&Settings->TryGetObjectField(TEXT("_window_positions"),Positions)&&(*Positions)->TryGetObjectField(Id,Point)
        &&(*Point)->TryGetNumberField(TEXT("x"),X)&&(*Point)->TryGetNumberField(TEXT("y"),Y)&&FMath::IsFinite(X)&&FMath::IsFinite(Y))return FVector2D(X,Y);
    return Default;
}
void UACEPluginSubsystem::SavePluginWindowPosition(const FString& Id,const FVector2D& Position)
{
    if(!Settings||!FMath::IsFinite(Position.X)||!FMath::IsFinite(Position.Y))return;
    const TSharedPtr<FJsonObject>* Existing=nullptr;
    auto Positions=Settings->TryGetObjectField(TEXT("_window_positions"),Existing)?*Existing:MakeShared<FJsonObject>();
    auto Point=MakeShared<FJsonObject>();Point->SetNumberField(TEXT("x"),Position.X);Point->SetNumberField(TEXT("y"),Position.Y);
    Positions->SetObjectField(Id,Point);Settings->SetObjectField(TEXT("_window_positions"),Positions);SaveSettings();
}
