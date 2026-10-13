#include "Mods/ACEPluginDesktop.h"
#include "Mods/ACEPluginPresentation.h"
#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginIcon.h"
#include "Widgets/Images/SImage.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "UI/ACEUIElementManager.h"
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
    const FSlateBrush& FrameBrush(){return FACEPluginStyle::Current()->Frame;}
    const FButtonStyle& DockButtonStyle(){return FACEPluginStyle::Current()->DockButton;}
    TSharedRef<SWidget> DockIcon(const FString& Id)
    {return SNew(SBox).WidthOverride(26).HeightOverride(26)[SNew(SACEPluginIcon).Id(Id)];}
    TSharedRef<STextBlock> Caption(const FString& Text, int32 Size = 12)
    { return SNew(SACEPluginText).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)); }

    class SPluginFrame : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SPluginFrame) : _Frameless(false), _CompactWhenLocked(false), _Minimized(false) {}
            SLATE_ATTRIBUTE(FString, Title)
            SLATE_ARGUMENT(bool, Frameless)
            SLATE_ARGUMENT(TFunction<void(FVector2D,bool)>, Move)
            SLATE_ARGUMENT(TFunction<void(FVector2D,bool)>, Resize)
            SLATE_ARGUMENT(TFunction<void()>, Raise)
            SLATE_ARGUMENT(TFunction<void()>, Close)
            SLATE_ARGUMENT(bool, CompactWhenLocked)
            SLATE_ATTRIBUTE(bool, Locked)
            SLATE_ARGUMENT(TFunction<bool()>, CanManipulate)
            SLATE_ATTRIBUTE(bool, Minimized)
            SLATE_ARGUMENT(TFunction<void()>, ToggleMinimized)
            SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Retail=FACEPluginStyle::Current();
            Move = Args._Move; Resize = Args._Resize; Raise = Args._Raise;
            Locked=Args._Locked;
            Frameless=Args._Frameless;
            CanManipulate=Args._CanManipulate;
            if(Frameless){ChildSlot[Args._Content.Widget];return;}
            auto Title = SNew(SHorizontalBox);
            const bool IsBar=bool(Args._ToggleMinimized);
            if(IsBar)
            {
                Title->AddSlot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(22).HeightOverride(2)
                    [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.6f,.65f,.7f))]];
                Title->AddSlot().AutoWidth()[SNew(SButton).Tag(TEXT("PluginBarMinimize")).ButtonStyle(&DockButtonStyle()).IsFocusable(false).ContentPadding(FMargin(5,0))
                    .ToolTipText_Lambda([Mini=Args._Minimized](){return FText::FromString(Mini.Get()?TEXT("Expand plugin bar"):TEXT("Minimize plugin bar"));})
                    .OnClicked_Lambda([Toggle=Args._ToggleMinimized](){Toggle();return FReply::Handled();})
                    [SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))
                        .Text_Lambda([Mini=Args._Minimized](){return FText::FromString(Mini.Get()?TEXT("+"):TEXT("-"));})]];
            }
            else Title->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SACEPluginText).Text_Lambda([Title=Args._Title]{return FText::FromString(Title.Get());}).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))];
            if (Args._Close) Title->AddSlot().AutoWidth()[SNew(SButton).IsFocusable(false).ButtonStyle(&DockButtonStyle())
                .ToolTipText(FText::FromString(TEXT("Hide window (plugin keeps running)")))
                .OnClicked_Lambda([Close=Args._Close](){Close();return FReply::Handled();})[Caption(TEXT("X"))]];
            ChildSlot[SNew(SBorder).BorderImage(&FrameBrush()).Padding(IsBar?0:3)
                [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(IsBar?18:24)
                    .Tag(Args._CompactWhenLocked?FName(TEXT("LedgerHeader")):FName(NAME_None))
                    .Visibility_Lambda([Lock=Locked,Ledger=Args._CompactWhenLocked]{return Ledger&&Lock.Get(false)?EVisibility::Collapsed:EVisibility::Visible;})
                    [SNew(SBorder).Tag(IsBar?TEXT("PluginBarHandle"):TEXT("PluginWindowHandle")).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(.025f,.012f,.004f,1)).Padding(IsBar?0:3)
                    .ToolTipText_Lambda([this](){return FText::FromString(CanManipulate&&!CanManipulate()?TEXT("Unlock the UI or hold Quick-show pinned windows to move"):TEXT("Drag to move"));})
                    .Cursor_Lambda([this](){return CanManipulate&&!CanManipulate()?EMouseCursor::Default:EMouseCursor::CardinalCross;})
                    .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& E)
                    {
                        if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
                        if(!Move||(CanManipulate&&!CanManipulate()))return FReply::Handled();
                        Dragging=true;return FReply::Handled().CaptureMouse(SharedThis(this));
                    })[Title]]]
                +SVerticalBox::Slot().FillHeight(1)[SNew(SBox).Padding(0,IsBar?0:3)
                    .Visibility_Lambda([Mini=Args._Minimized](){return Mini.Get(false)?EVisibility::Collapsed:EVisibility::Visible;})[Args._Content.Widget]]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SBorder).Padding(FMargin(12,2)).Visibility_Lambda([this]{return Resize&&(!CanManipulate||CanManipulate())?EVisibility::Visible:EVisibility::Collapsed;})
                    .ToolTipText(FText::FromString(TEXT("Drag to resize"))).Cursor(EMouseCursor::ResizeSouthEast)
                    .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent& E){if(E.GetEffectingButton()!=EKeys::LeftMouseButton||(CanManipulate&&!CanManipulate()))return FReply::Unhandled();Resizing=true;return FReply::Handled().CaptureMouse(SharedThis(this));})[Caption(TEXT("◢"))]]]];
        }
        virtual FReply OnPreviewMouseButtonDown(const FGeometry&,const FPointerEvent& E) override
        {
            if(Raise)Raise();
            return FReply::Unhandled();
        }
        virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override
        {
            if(!Frameless||E.GetEffectingButton()!=EKeys::LeftMouseButton||(CanManipulate&&!CanManipulate()))return FReply::Unhandled();
            const auto At=G.AbsoluteToLocal(E.GetScreenSpacePosition());
            Resizing=Resize&&At.X>=G.GetLocalSize().X-24&&At.Y>=G.GetLocalSize().Y-24;
            Dragging=!Resizing;return FReply::Handled().CaptureMouse(SharedThis(this));
        }
        virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E) override
        {
            if((!Dragging&&!Resizing)||!HasMouseCapture())return FReply::Unhandled();
            auto& Action=Resizing?Resize:Move;
            if(CanManipulate&&!CanManipulate())
            {Action(FVector2D::ZeroVector,true);Dragging=Resizing=false;return FReply::Handled().ReleaseMouseCapture();}
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
        TSharedPtr<FACEPluginStyle> Retail;
        bool Dragging=false,Resizing=false,Frameless=false;
        TFunction<void(FVector2D,bool)> Move,Resize;
        TFunction<void()> Raise;
        TAttribute<bool> Locked;
        TFunction<bool()> CanManipulate;
    };
}

void SACEPluginDesktop::Construct(const FArguments& Args)
{
    Host=Args._Host;FACEPluginStyle::Use(Args._Host);
    SetVisibility(EVisibility::SelfHitTestInvisible);
    SetClipping(EWidgetClipping::ClipToBounds);
    ChildSlot[SAssignNew(Canvas,SConstraintCanvas)];
    FWindow Bar;
    Bar.Position=Host->GetPluginWindowPosition(BarId,FVector2D(8,80));Bar.Size=FVector2D(38,120);Bar.PluginBar=true;
    Bar.Widget=SNew(SPluginFrame)
        .Minimized_Lambda([H=Host](){return H.IsValid()&&H->IsPluginBarMinimized();})
        .ToggleMinimized([H=Host](){if(H.IsValid())H->SetPluginBarMinimized(!H->IsPluginBarMinimized());})
        .CanManipulate([H=Host](){return H.IsValid()&&H->IsPluginBarUnlocked();})
        .Move([this](FVector2D D,bool Save){Move(BarId,D,Save);})
        .Raise([this](){Raise(BarId);})
        [SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
            +SScrollBox::Slot()[SAssignNew(Buttons,SVerticalBox)]];
    Canvas->AddSlot().Expose(Bar.Slot).Alignment(FVector2D::ZeroVector).ZOrder(10000)[Bar.Widget.ToSharedRef()];
    Windows.Add(BarId,Bar);
    Refresh();
}
FVector2D SACEPluginDesktop::DisplaySize(const FWindow& W) const
{
    if(Host.IsValid())
    {
        if(!W.LedgerId.IsEmpty())
        {
            auto Size=Host->LedgerSize(W.LedgerId);
            if(W.LedgerId==TEXT("supplyledger"))
            {
                Size.X=W.Size.X;
                if(W.Size.Y>0)Size.Y=W.Size.Y+(Host->IsPluginBarUnlocked()?68:0);
            }
            return Size;
        }
        if(W.PluginBar&&Host->IsPluginBarMinimized())return FVector2D(38,18);
        if(W.MapOverlay&&Host->WaypointOption(TEXT("map_minimized"),false))return FVector2D(70,26);
    }
    return W.Size;
}
void SACEPluginDesktop::Layout(FWindow& W)
{
    const FVector2D Display=DisplaySize(W);
    if(ViewSize.X<=0||ViewSize.Y<=0)
    {W.Slot->SetOffset(FMargin(W.Position.X,W.Position.Y,Display.X,Display.Y));return;}
    const FVector2D Size(FMath::Min(Display.X,ViewSize.X),FMath::Min(Display.Y,ViewSize.Y));
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
        Map->Widget->SetVisibility(!P.IsValid()?EVisibility::Collapsed:EVisibility::Visible);
        Layout(*Map); // Minimize without replacing the saved expanded size.
    }
    for(auto& Pair:Windows)if(!Pair.Value.LedgerId.IsEmpty())Layout(Pair.Value);
    if(!G.GetLocalSize().Equals(ViewSize))
    {ViewSize=G.GetLocalSize();for(auto& Pair:Windows)Layout(Pair.Value);}
}
void SACEPluginDesktop::Move(const FString& Id,FVector2D Delta,bool Save)
{
    if(auto* W=Windows.Find(Id))
    {
        if(!Delta.IsNearlyZero()&&(!Host.IsValid()||!Host->IsPluginBarUnlocked()))return;
        if(!Delta.IsNearlyZero())
        {
            const auto Offset=W->Slot->GetOffset();W->Position=FVector2D(Offset.Left,Offset.Top)+Delta;
            if(ViewSize.X>0&&ViewSize.Y>0)
            {const FVector2D Size=DisplaySize(*W);W->Position.X=FMath::Clamp(W->Position.X,0.,FMath::Max(0.,ViewSize.X-Size.X));W->Position.Y=FMath::Clamp(W->Position.Y,0.,FMath::Max(0.,ViewSize.Y-Size.Y));}
        }
        Layout(*W);if(Save&&Host.IsValid())Host->SavePluginWindowPosition(Id,W->Position);
    }
}
void SACEPluginDesktop::SaveOverlayLayout()
{
    if(!Host.IsValid())return;
    for(const TCHAR* Id:{TEXT("__bar"),TEXT("waypoint.arrow"),TEXT("waypoint.dungeon"),TEXT("supplyledger"),TEXT("huntledger")})if(const auto* W=Windows.Find(Id))
    {
        Host->SavePluginWindowPosition(Id,W->Position);
        if(FString(Id)==TEXT("waypoint.dungeon")||FString(Id)==TEXT("supplyledger"))Host->SavePluginWindowPosition(FString(Id)+TEXT(".size"),W->Size);
    }
}
void SACEPluginDesktop::Resize(const FString& Id,FVector2D Delta,bool Save)
{
    if(!Delta.IsNearlyZero()&&(!Host.IsValid()||!Host->IsPluginBarUnlocked()))return;
    if(auto* W=Windows.Find(Id))
    {
     if(Id==TEXT("supplyledger"))
     {
         // Persist the list area; pinning only hides the editor chrome.
         const double Chrome=Host->IsPluginBarUnlocked()?68.:0.;
         const FVector2D Size=DisplaySize(*W)+Delta;
         W->Size=FVector2D(FMath::Clamp(Size.X,140.,FMath::Max(140.,ViewSize.X)),
             FMath::Clamp(Size.Y-Chrome,40.,FMath::Max(40.,ViewSize.Y-Chrome)));
         Layout(*W);if(Save)Host->SavePluginWindowPosition(Id+TEXT(".size"),W->Size);return;
     }
     W->Size+=Delta;const double MinWidth=Id==TEXT("ucmmicro")?240.:Id==TEXT("waypoint.dungeon")?220.:400.;W->Size.X=FMath::Clamp(W->Size.X,MinWidth,FMath::Max(MinWidth,ViewSize.X));const double MinHeight=Id==TEXT("ucmmicro")?290.:Id==TEXT("waypoint.map")?640.:Id==TEXT("waypoint.dungeon")?220.:320.;W->Size.Y=FMath::Clamp(W->Size.Y,MinHeight,FMath::Max(MinHeight,ViewSize.Y));Layout(*W);
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
        if(Host.IsValid()&&!W->LedgerId.IsEmpty())Host->SetLedgerOption(Id,TEXT("visible"),false);
        W->Open=false;W->Widget->SetVisibility(EVisibility::Collapsed);
        if(Focused)FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}
void SACEPluginDesktop::Toggle(const FString& Id)
{
    if(!Host.IsValid())return;
    TSharedPtr<FACEClientPlugin> Plugin;
    for(auto P:Host->Plugins)if((P->Id==Id || (Id.StartsWith(TEXT("waypoint."))&&P->Id==TEXT("waypoint")) || (Id==TEXT("ucm.log")&&P->Id==TEXT("ucm")))&&(P->Enabled||Id==TEXT("ucm.log")))Plugin=P;
    if(!Plugin)return;
    FACEPluginStyle::Use(Host.Get());
    if(auto* W=Windows.Find(Id))
    {
        if(W->Open)Hide(Id);
        else {if(!W->LedgerId.IsEmpty())Host->SetLedgerOption(Id,TEXT("visible"),true);W->Open=true;W->Widget->SetVisibility(EVisibility::Visible);Raise(Id);}
        return;
    }
    FWindow W;W.Position=Host->GetPluginWindowPosition(Id,FVector2D(84+24*(Windows.Num()-1),80+24*(Windows.Num()-1)));W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(780,700));
    const bool Arrow=Id==TEXT("waypoint.arrow");
    const bool Dungeon=Id==TEXT("waypoint.dungeon");
    W.MapOverlay=Dungeon;
    const bool Ledger=UACEPluginSubsystem::IsLedger(Id);
    if(Ledger){W.LedgerId=Id;W.Size=Id==TEXT("supplyledger")?Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(200,0)):Host->LedgerSize(Id);Host->SetLedgerOption(Id,TEXT("visible"),true);}
    if(Id==TEXT("ucmmicro"))W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(270,360));
    if(Id==TEXT("ucm.log"))W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(700,440));
    if(Arrow)W.Size=FVector2D(290,225);
    if(Dungeon)W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(420,420));
    W.Widget=SNew(SPluginFrame).Title_Lambda([Id,Arrow,Plugin](){return Id==TEXT("ucm.log")?TEXT("UCM Log"):Arrow?TEXT("Waypoint · drag when unlocked"):Id==TEXT("waypoint.map")?TEXT("Waypoint Map"):Plugin->DisplayTitle();})
        .Frameless(Arrow||Dungeon)
        .CompactWhenLocked(Ledger)
        .Locked_Lambda([H=Host]{return H.IsValid()&&!H->IsPluginBarUnlocked();})
        .CanManipulate([H=Host]{return H.IsValid()&&H->IsPluginBarUnlocked();})
        .Move([this,Id](FVector2D D,bool Save){Move(Id,D,Save);})
        .Resize((Arrow||(Ledger&&Id!=TEXT("supplyledger")))?TFunction<void(FVector2D,bool)>():TFunction<void(FVector2D,bool)>([this,Id](FVector2D D,bool Save){Resize(Id,D,Save);}))
        .Raise([this,Id](){Raise(Id);}).Close([this,Id,Arrow](){Hide(Id);if(Arrow)Host->SetWaypointOption(TEXT("arrow"),false);})[Dungeon?Host->MakeWaypointDungeonOverlay():Arrow?Host->MakeWaypointPanel(false,true):Host->MakePanel(Id)];
    if(Dungeon)W.Widget->SetVisibility(EVisibility::Collapsed); // Tick validates the current cell before showing it.
    if(Arrow)W.Widget->SetVisibility(TAttribute<EVisibility>::CreateLambda([H=Host](){return H.IsValid()&&H->WaypointOption(TEXT("arrow"))?(H->IsWaypointUnlocked()?EVisibility::Visible:EVisibility::HitTestInvisible):EVisibility::Collapsed;}));
    Canvas->AddSlot().Expose(W.Slot).Alignment(FVector2D::ZeroVector).ZOrder(++TopOrder)[W.Widget.ToSharedRef()];Layout(W);Windows.Add(Id,W);
}
void SACEPluginDesktop::Refresh()
{
    if(!Host.IsValid())return;
    FACEPluginStyle::Use(Host.Get());
    for(const auto& P:Host->Plugins)if(P->Enabled&&UACEPluginSubsystem::IsLedger(P->Id)&&Host->LedgerOption(P->Id,TEXT("visible"))&&!IsOpen(P->Id))Toggle(P->Id);
    Windows.FindChecked(BarId).Widget->SetVisibility(Host->IsPluginBarVisible()?EVisibility::Visible:EVisibility::Collapsed);
    Layout(Windows.FindChecked(BarId));
    FString Next;TSet<FString> Enabled;
    for(auto P:Host->Plugins)if(P->Enabled){Next+=P->Id+TEXT("|");Enabled.Add(P->Id);}
    if(Host->Plugins.ContainsByPredicate([](const auto& P){return P->Id==TEXT("ucm");}))Enabled.Add(TEXT("ucm.log"));
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
    Buttons->AddSlot().AutoHeight().Padding(1)[SNew(SBox).HeightOverride(32)
        [SNew(SButton).ButtonStyle(&DockButtonStyle()).IsFocusable(false).HAlign(HAlign_Center)
        .ToolTipText(FText::FromString(TEXT("Manage plugins")))
        .OnClicked_Lambda([this](){if(Host.IsValid())Host->TogglePanel();return FReply::Handled();})[Caption(TEXT("+"),18)]]];
    for(auto P:Host->Plugins)if(P->Enabled)
    {
        Buttons->AddSlot().AutoHeight().Padding(1)[SNew(SBox).HeightOverride(32)
            [SNew(SButton).ButtonStyle(&DockButtonStyle()).IsFocusable(false).HAlign(HAlign_Center).ContentPadding(2)
            .ToolTipText_Lambda([this,P]()
            {
                if(P->Id==TEXT("looteditor"))
                {
                    TSharedPtr<FACEClientPlugin> UCM;bool Looting=false;
                    if(Host.IsValid())for(auto Plugin:Host->Plugins)if(Plugin->Id==TEXT("ucm")){UCM=Plugin;break;}
                    if(UCM)UCM->Profile->TryGetBoolField(TEXT("looting"),Looting);
                    return FText::FromString(P->DisplayTitle()+TEXT("\nClick to open/close the editor. This does not toggle looting.\nUCM looting: ")
                        +(Looting?TEXT("enabled"):TEXT("disabled"))+(UCM&&UCM->Running?TEXT(" (UCM running)"):TEXT(" (UCM stopped)")));
                }
                if(UACEPluginSubsystem::IsLedger(P->Id))return FText::FromString(P->DisplayTitle()+TEXT("\nClick to show/hide. Unlock the UI or hold Quick-show pinned windows to move and resize."));
                return FText::FromString(P->DisplayTitle()+TEXT("\nClick to show/hide\n")+P->Status);
            })
            .OnClicked_Lambda([this,Id=P->Id](){Toggle(Id);return FReply::Handled();})
            [SNew(SACEPluginBadge).Plugin(P)]]];
    }
    if(Enabled.Contains(TEXT("ucm")))Buttons->AddSlot().AutoHeight().Padding(1)[SNew(SBox).HeightOverride(32)
        [SNew(SButton).ButtonStyle(&DockButtonStyle()).IsFocusable(false).ToolTipText(FText::FromString(TEXT("UCM activity and problem log")))
        .OnClicked_Lambda([this](){Toggle(TEXT("ucm.log"));return FReply::Handled();})[DockIcon(TEXT("ucm.log"))]]];
    if(Host->IsWaypointEnabled())Buttons->AddSlot().AutoHeight().Padding(1)[SNew(SBox).HeightOverride(32)
        [SNew(SButton).ButtonStyle(&DockButtonStyle()).IsFocusable(false).ToolTipText(FText::FromString(TEXT("Waypoint world / dungeon map")))
        .OnClicked_Lambda([this](){Toggle(TEXT("waypoint.map"));return FReply::Handled();})[DockIcon(TEXT("waypoint.map"))]]];
    auto& Bar=Windows.FindChecked(BarId);Bar.Size.Y=18+34;
    for(auto P:Host->Plugins)if(P->Enabled)Bar.Size.Y+=34;
    if(Enabled.Contains(TEXT("ucm")))Bar.Size.Y+=34;
    if(Host->IsWaypointEnabled())Bar.Size.Y+=34;
    Layout(Bar);
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
bool UACEPluginSubsystem::IsPluginBarVisible() const
{
    bool Visible=true;if(Settings)Settings->TryGetBoolField(TEXT("_show_plugin_bar"),Visible);return Visible;
}
void UACEPluginSubsystem::SetPluginBarVisible(bool Visible)
{
    if(!Settings)return;
    Settings->SetBoolField(TEXT("_show_plugin_bar"),Visible);SaveSettings();
    if(DesktopDock)DesktopDock->Refresh();
}
bool UACEPluginSubsystem::IsPluginBarMinimized() const
{
    bool Minimized=false;if(Settings)Settings->TryGetBoolField(TEXT("_plugin_bar_minimized"),Minimized);return Minimized;
}
void UACEPluginSubsystem::SetPluginBarMinimized(bool Minimized)
{
    if(!Settings)return;
    Settings->SetBoolField(TEXT("_plugin_bar_minimized"),Minimized);SaveSettings();
    if(DesktopDock)DesktopDock->Refresh();
}
bool UACEPluginSubsystem::IsPluginBarUnlocked() const
{
    const auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const auto* Manager=Client?Client->GetUIElementManager():nullptr;
    return !Manager||!Manager->IsUiLocked();
}
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
