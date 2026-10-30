#include "Mobile/ACEMobileWidget.h"
#include "Algo/AnyOf.h"
#include "Widgets/SNullWidget.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "ACERetailChat.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginIcon.h"
#include "Mods/ACEPluginPresentation.h"
#include "ACERuntimeOptions.h"
#include "GameFramework/GameUserSettings.h"
#include "Components/SceneCaptureComponent2D.h"
#include "VR/ACEVRMenu.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIResourceResolver.h"
#include "Engine/GameInstance.h"
#include "Misc/CoreDelegates.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Fonts/FontMeasure.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

namespace
{
const FLinearColor Gold(.78f,.60f,.27f),MobilePanelInk(.018f,.025f,.037f),TextColor(.96f,.93f,.85f);
constexpr int32 WheelCount=8;
TSharedRef<STextBlock> Text(const FString& Value,int32 Size=22)
{return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(TextColor).AutoWrapText(true);}
TSharedRef<SWidget> Button(const FString& Label,TFunction<void()> Action,bool Enabled=true,int32 Font=22)
{
    static const FButtonStyle Style=FButtonStyle()
        .SetNormal(FSlateRoundedBoxBrush(MobilePanelInk,8,Gold,2))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.12f,.09f,.04f),8,Gold,2))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.08f,.18f,.18f),8,TextColor,2));
    return SNew(SBox).MinDesiredHeight(56)[SNew(SButton).Tag(FName(*Label)).ButtonStyle(&Style).IsEnabled(Enabled)
        .IsFocusable(false).TouchMethod(EButtonTouchMethod::PreciseTap).ContentPadding(FMargin(16,10))
        .HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked_Lambda([Action=MoveTemp(Action)](){Action();return FReply::Handled();})
        [SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Font))
            .ColorAndOpacity(TextColor).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]];
}
class STouchSurface : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(STouchSurface){}SLATE_END_ARGS()
    void Construct(const FArguments&,UACEMobileWidget* InOwner){Owner=InOwner;SetCanTick(true);}
    FVector2D ComputeDesiredSize(float) const override{return {960,540};}
    void Tick(const FGeometry& G,double,float) override
    {
        if(!Owner.IsValid())return;
        if(!LastExtent.Equals(G.GetLocalSize(),.5f)){Owner->CancelTouches();LastExtent=G.GetLocalSize();}
        Owner->SurfaceResized(ACEMobile::LogicalSize(G.GetLocalSize()));
    }
    FVector2D Point(const FGeometry& G,const FPointerEvent& E) const
    {return G.AbsoluteToLocal(E.GetScreenSpacePosition())/G.GetLocalSize()*ACEMobile::LogicalSize(G.GetLocalSize());}
    FReply OnTouchStarted(const FGeometry& G,const FPointerEvent& E) override
    {if(!Owner.IsValid())return FReply::Unhandled();Owner->TouchBegin(E.GetPointerIndex(),Point(G,E),E.GetScreenSpacePosition());return FReply::Handled().CaptureMouse(AsShared());}
    FReply OnTouchMoved(const FGeometry& G,const FPointerEvent& E) override
    {return Owner.IsValid()?Owner->TouchMove(E.GetPointerIndex(),Point(G,E)):FReply::Unhandled();}
    FReply OnTouchEnded(const FGeometry& G,const FPointerEvent& E) override
    {if(Owner.IsValid())Owner->TouchEnd(E.GetPointerIndex(),Point(G,E),E.GetScreenSpacePosition());return FReply::Handled().ReleaseMouseCapture();}
    FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override
    {return E.GetEffectingButton()==EKeys::LeftMouseButton?OnTouchStarted(G,E):FReply::Unhandled();}
    FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E) override{return OnTouchMoved(G,E);}
    FReply OnMouseButtonUp(const FGeometry& G,const FPointerEvent& E) override{return OnTouchEnded(G,E);}
    void OnMouseCaptureLost(const FCaptureLostEvent& E) override{if(Owner.IsValid())Owner->CancelFinger(E.PointerIndex);}
    int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool)const override
    {return Owner.IsValid()?Owner->Paint(G,Out,L):L;}
private:TWeakObjectPtr<UACEMobileWidget> Owner;FVector2D LastExtent=FVector2D::ZeroVector;
};
}

void UACEMobileWidget::InitializeMobile(AACEPlayerController* Controller)
{
    PC=Controller;Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    Client->OnChatMessage.AddUniqueDynamic(this,&UACEMobileWidget::ChatReceived);
    Client->OnBookChanged.AddUniqueDynamic(this,&UACEMobileWidget::BookChanged);
    Settings=GetMutableDefault<UACEMobileSettings>();Settings->Sanitize();FitControls();
    UACEMobileSettings::ApplyTextureBudget();
    BackgroundHandle=FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this,&UACEMobileWidget::CancelTouches);
    DeactivateHandle=FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this,&UACEMobileWidget::CancelTouches);
    CharacterCreatedHandle=Client->GetSession()->OnCharacterCreated.AddWeakLambda(this,[this](uint32 Result,const FACECharacterInfo&)
    {
        if(!Creation)return;
        if(Result==1){Creation.Reset();CreationMessage.Reset();OpenPanel("Characters");}
        else {CreationMessage=Result==3?TEXT("That name is unavailable. Choose another name."):FString::Printf(TEXT("The server could not create this character (%u). Please try again."),Result);BuildPanel();}
    });
}
void UACEMobileWidget::NativeDestruct()
{
    CancelTouches();
    ReleaseCharacterPreview();
    if(Client)Client->OnChatMessage.RemoveDynamic(this,&UACEMobileWidget::ChatReceived);
    if(Client)Client->OnBookChanged.RemoveDynamic(this,&UACEMobileWidget::BookChanged);
    if(Client && Client->GetSession())Client->GetSession()->OnCharacterCreated.Remove(CharacterCreatedHandle);
    if(Menu){Menu->NativeDestruct();Menu=nullptr;}
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
    FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivateHandle);
    Super::NativeDestruct();
}
TSharedRef<SWidget> UACEMobileWidget::RebuildWidget()
{
    return SNew(SSafeZone).IsTitleSafe(true)[SNew(SOverlay)
        +SOverlay::Slot()[SNew(STouchSurface,this)]
        +SOverlay::Slot()[SAssignNew(HUDHost,SBox).Visibility_Lambda([this](){return Client && Client->GetSessionState()==EACESessionState::InWorld && !bWheel && Panel.IsNone() ? (bEditing?EVisibility::HitTestInvisible:EVisibility::SelfHitTestInvisible):EVisibility::Collapsed;})]
        +SOverlay::Slot()[SAssignNew(PluginRailHost,SBox).Visibility_Lambda([this](){return Client && Client->GetSessionState()==EACESessionState::InWorld && !bEditing && !bWheel && Panel.IsNone()?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;})]
        +SOverlay::Slot()[SAssignNew(PanelHost,SBox).Visibility(EVisibility::Collapsed)]];
}
void UACEMobileWidget::ReleaseSlateResources(bool Children)
{Super::ReleaseSlateResources(Children);PanelHost.Reset();PluginRailHost.Reset();HUDHost.Reset();HUDSignature.Reset();HUDPluginIds.Reset();PinnedChatLines.Reset();PinnedChatScroll.Reset();PinnedChatEntry.Reset();RailPluginIds.Reset();RailSize=FVector2D::ZeroVector;ChatEntry.Reset();ChatLines.Reset();ChatScroll.Reset();}
void UACEMobileWidget::ChatReceived(const FString&,const FString&,int32){bChatDirty=true;}
void UACEMobileWidget::BookChanged(){bBookDirty=true;}
void UACEMobileWidget::ResolveConfirmation(bool Accept)
{
    if(auto* B=PC?PC->GetActionDeckBinder():nullptr)
    {
        if(B->ManaStoneConfirmSource)B->FinishManaStoneConfirmation(Accept);
        else B->FinishServerConfirmation(Accept);
    }
    ConfirmationText.Reset();ClosePanel();
}
void UACEMobileWidget::RefreshChat()
{
    if(!bChatDirty || !Client)return;
    auto Refresh=[&](TSharedPtr<SVerticalBox> Lines,TSharedPtr<SScrollBox> Scroll,int32 Font)
    {
        if(!Lines)return;
        const bool AtEnd=!Scroll || Scroll->GetScrollOffsetOfEnd()-Scroll->GetScrollOffset()<32;
        Lines->ClearChildren();const auto& History=Client->GetChatHistory();
        for(int32 I=FMath::Max(0,History.Num()-100);I<History.Num();++I)
            Lines->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(History[I].Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Font)).AutoWrapText(true).ColorAndOpacity(ACERetailChat::Color(History[I].Type))];
        if(AtEnd && Scroll)Scroll->ScrollToEnd();
    };
    Refresh(ChatLines,ChatScroll,22);Refresh(PinnedChatLines,PinnedChatScroll,15);bChatDirty=false;
}
bool UACEMobileWidget::IsBlockingMovement() const
{return !Panel.IsNone() || bEditing || (PinnedChatEntry && PinnedChatEntry->HasKeyboardFocus()) || !Client || Client->GetSessionState()!=EACESessionState::InWorld || !PC || PC->IsWorldTransitionActive();}
void UACEMobileWidget::CancelFinger(int32 Finger)
{
    if(bEditing && Fingers.Contains(Finger))SaveLayout();
    if(Fingers.Remove(Finger)>0 && PC)PC->CancelMobileMovement();
}
void UACEMobileWidget::CancelTouches()
{
    if(bEditing)SaveLayout();
    Fingers.Reset();LookDelta=FVector2D::ZeroVector;
    // Backgrounding must also cancel tap-started attacks/approaches after the
    // initiating finger has already been released.
    if(PC)PC->CancelMobileMovement();
}
bool UACEMobileWidget::IsJumpHeld()const
{return !IsBlockingMovement() && Algo::AnyOf(Fingers,[](const auto& F){return F.Value.Action=="Jump";});}
bool UACEMobileWidget::IsWalkHeld()const
{return !IsBlockingMovement() && Algo::AnyOf(Fingers,[](const auto& F){return F.Value.Action=="Walk";});}
FVector2D UACEMobileWidget::Movement()const
{
    if(IsBlockingMovement())return FVector2D::ZeroVector;
    for(const auto& Pair:Fingers)if(Pair.Value.Action=="Move" && Layout.IsValidIndex(Pair.Value.Control))
        return ACEMobile::MovementAxes(Pair.Value.Point-Layout[Pair.Value.Control].Position*ViewSize,Layout[Pair.Value.Control].Size*.5f);
    return FVector2D::ZeroVector;
}
float UACEMobileWidget::Turning()const
{
    if(IsBlockingMovement())return 0;
    for(const auto& Pair:Fingers)if(Pair.Value.Action=="Turn" && Layout.IsValidIndex(Pair.Value.Control))
    {
        if(!ACEMobile::HitControl(Layout[Pair.Value.Control],ViewSize,Pair.Value.Point))return 0;
        const double X=Pair.Value.Point.X-Layout[Pair.Value.Control].Position.X*ViewSize.X;
        return FMath::Abs(X)<Layout[Pair.Value.Control].Size/10.f?0.f:X<0?-1.f:1.f;
    }
    return 0;
}
FVector2D UACEMobileWidget::ConsumeLook(){const auto D=LookDelta;LookDelta=FVector2D::ZeroVector;return D;}
void UACEMobileWidget::SurfaceResized(FVector2D Size)
{
    if(Size.Equals(ViewSize,.5f))return;
    CancelTouches();ViewSize=Size;FitControls();
}
void UACEMobileWidget::SaveLayout()
{
    if(!Settings)return;
    bool Changed=false;
    for(const auto& Control:Layout)
    {
        auto* Preferred=Settings->Controls.FindByPredicate([&](const auto& C){return C.Id==Control.Id;});
        if(!Preferred){Settings->Controls.Add(Control);Changed=true;continue;}
        const auto* Before=FittedLayout.FindByPredicate([&](const auto& C){return C.Id==Control.Id;});
        // Persist only the fields the user changed. Fitting controls around a
        // panel or foldable screen must not rewrite the preferred layout.
        if(!Before || Control.Action!=Before->Action){Preferred->Action=Control.Action;Changed=true;}
        if(!Before || !Control.Position.Equals(Before->Position,1.e-6)){Preferred->Position=Control.Position;Changed=true;}
        if(!Before || Control.Size!=Before->Size){Preferred->Size=Control.Size;Changed=true;}
        if(!Before || Control.bVisible!=Before->bVisible){Preferred->bVisible=Control.bVisible;Changed=true;}
    }
    FittedLayout=Layout;
    if(Changed)Settings->SaveConfig();
}
FName UACEMobileWidget::HeaderAt(FVector2D P)const
{
    if(P.Y<12 || P.Y>76)return NAME_None;
    if(bEditing)
    {
        if(P.X<150)return "Done";
        if(P.X<310)return "Rebind";
        if(P.X<470)return "Resize";
        if(P.X>ViewSize.X-160)return "Reset";
        return NAME_None;
    }
    if(P.X>ViewSize.X-108 && P.X<ViewSize.X-8)return "Menu";
    if(P.X>ViewSize.X-216 && P.X<ViewSize.X-116)return "Plugins";
    if(P.X>ViewSize.X-324 && P.X<ViewSize.X-224)return "Inventory";
    if(P.X>ViewSize.X-432 && P.X<ViewSize.X-332)return "Chat";
    return NAME_None;
}
FReply UACEMobileWidget::TouchBegin(int32 Finger,FVector2D Point,FVector2D Absolute)
{
    if(!Panel.IsNone() || Fingers.Contains(Finger))return FReply::Handled();
    FFinger F;F.Start=F.Point=Point;F.Action=HeaderAt(Point);
    if(F.Action.IsNone())
    {
        if(bWheel)F.Action="Wheel";
        else for(int32 I=Layout.Num()-1;I>=0;--I)if(Layout[I].bVisible && ACEMobile::HitControl(Layout[I],ViewSize,Point))
        {
            // A second finger cannot take over a held joystick or button.
            if(Algo::AnyOf(Fingers,[I](const auto& V){return V.Value.Control==I;}))return FReply::Handled();
            F.Control=I;F.Action=Layout[I].Action;if(bEditing)SelectedControl=I;break;
        }
        if(F.Action.IsNone())F.Action="Look";
    }
    Fingers.Add(Finger,F);return FReply::Handled();
}
FReply UACEMobileWidget::TouchMove(int32 Finger,FVector2D Point)
{
    auto* F=Fingers.Find(Finger);if(!F)return FReply::Handled();
    if(FVector2D::Distance(F->Start,Point)>10)F->Moved=true;
    if(bEditing && Layout.IsValidIndex(F->Control))
    {
        const auto P=Point/ViewSize;
        if(ACEMobile::CanPlace(Layout,F->Control,P,Layout[F->Control].Size,ViewSize,ControlsTop))Layout[F->Control].Position=P;
    }
    else if(!IsBlockingMovement() && !bWheel && F->Action=="Look" && F->Moved)
    {
        // Pinching uses two look fingers only; movement/button fingers never
        // become pinch endpoints or alter a camera gesture's ownership.
        const FFinger* Other=nullptr;
        for(const auto& Pair:Fingers)if(Pair.Key!=Finger && Pair.Value.Action=="Look"){Other=&Pair.Value;break;}
        if(Other && PC)
        {
            const double Change=FVector2D::Distance(Point,Other->Point)-FVector2D::Distance(F->Point,Other->Point);
            PC->ApplyCameraWheelZoom(Change*.02f);
            for(auto& Pair:Fingers)if(Pair.Value.Action=="Look")Pair.Value.Moved=true;
        }
        else LookDelta+=FVector2D(Point.X-F->Point.X,F->Point.Y-Point.Y)*Settings->LookSensitivity;
    }
    F->Point=Point;return FReply::Handled();
}
FReply UACEMobileWidget::TouchEnd(int32 Finger,FVector2D Point,FVector2D Absolute)
{
    const auto* Found=Fingers.Find(Finger);if(!Found)return FReply::Handled();
    const FFinger F=*Found;Fingers.Remove(Finger);
    if(bEditing && F.Control!=INDEX_NONE){SaveLayout();return FReply::Handled();}
    if(F.Action=="Wheel" && bWheel)
    {
        if(Point.Y>ViewSize.Y-62)
        {
            const double X=Point.X-ViewSize.X/2;
            if(X>=-320 && X<=-120)WheelPage=FMath::Max(0,WheelPage-1);
            else if(X>=120 && X<=320)WheelPage=FMath::Min(FMath::Max(0,(WheelSpells.Num()-1)/WheelCount),WheelPage+1);
            else if(X>=-100 && X<=100){bWheel=false;CancelTouches();}
        }
        else if(Point.Y>=84 && Point.Y<140)
        {
            const int32 Tab=FMath::Clamp(FMath::FloorToInt((Point.X-(ViewSize.X-640)/2)/80),0,7);
            if(Point.X>=(ViewSize.X-640)/2 && Point.X<=(ViewSize.X+640)/2){Client->SetActiveSpellBar(Tab);WheelPage=0;RefreshWheel();}
        }
        else
        {
            const int32 Count=FMath::Clamp(WheelSpells.Num()-WheelPage*WheelCount,0,WheelCount);
            const int32 I=ACEMobile::WheelIndex(Point-FVector2D(ViewSize.X/2,FMath::Max(300.,ViewSize.Y*.5)),Count,60,165);
            if(I!=INDEX_NONE && WheelSpells.IsValidIndex(WheelPage*WheelCount+I))
            {SelectedSpell=WheelSpells[WheelPage*WheelCount+I];if(auto* B=PC->GetActionDeckBinder())B->SelectVRSpell(SelectedSpell);bWheel=false;CancelTouches();}
        }
        return FReply::Handled();
    }
    if(F.Action=="Look")
    {if(!F.Moved && !IsBlockingMovement() && PC)PC->MobileSelectAt(Absolute);return FReply::Handled();}
    if(F.Action=="Move" || F.Action=="Turn" || F.Action=="Jump" || F.Action=="Walk")return FReply::Handled();
    const bool StillInside=F.Control!=INDEX_NONE?Layout.IsValidIndex(F.Control) && ACEMobile::HitControl(Layout[F.Control],ViewSize,Point):HeaderAt(Point)==F.Action;
    if(StillInside)Action(F.Action);
    return FReply::Handled();
}
void UACEMobileWidget::RefreshWheel()
{
    WheelSpells=Client->GetSpellBar(Client->GetActiveSpellBar()).FilterByPredicate([](int32 S){return S!=0;});
    WheelPage=FMath::Clamp(WheelPage,0,FMath::Max(0,(WheelSpells.Num()-1)/WheelCount));
    WheelIcons.Reset();if(auto* R=Client->GetUIResourceResolver())for(int32 Spell:WheelSpells)WheelIcons.Add(R->ResolveSpellIcon(Spell));
}
void UACEMobileWidget::Action(FName Name)
{
    if(Name=="Menu" || Name=="Inventory" || Name=="Chat" || Name=="Plugins" || Name=="Inspect"){OpenPanel(Name);return;}
    if(Name=="Done"){bEditing=false;SaveLayout();CancelTouches();return;}
    if(Name=="Reset"){Settings->ResetLayout();Settings->SaveConfig();FitControls();SelectedControl=INDEX_NONE;return;}
    if(Name=="Rebind" || Name=="Resize"){if(Layout.IsValidIndex(SelectedControl))OpenPanel(Name);return;}
    if(IsBlockingMovement())return;
    if(Name=="Spell wheel"){bWheel=!bWheel;WheelPage=0;RefreshWheel();CancelTouches();return;}
    if(PC)PC->MobileAction(Name,SelectedSpell);
    NextPresentationRefresh=0;
}
void UACEMobileWidget::OpenPanel(FName Page)
{
    CancelTouches();bWheel=false;Panel=Page;
    if(PC)PC->CancelMobileMovement();
    BuildPanel();
}
void UACEMobileWidget::ClosePanel()
{
    if(Menu)Menu->CancelItemPointer();
    if(Panel=="Book" && Client)Client->ClearBook();
    Panel=NAME_None;CancelTouches();ChatEntry.Reset();ChatLines.Reset();ChatScroll.Reset();
    if(PanelHost){PanelHost->SetContent(SNullWidget::NullWidget);PanelHost->SetVisibility(EVisibility::Collapsed);}
    if(PC)PC->RefreshDesktopDisplayInputMode();
}
void UACEMobileWidget::UpdateMobile()
{
    if(!Client || !PC)return;
    if(!Creation && CreationActor)ReleaseCharacterPreview();
    if(Panel=="Create character" && (CreationTab==4 || CreationTab==5) && CreationCapture
        && FPlatformTime::Seconds()-LastCreationCapture>=.1)
    {LastCreationCapture=FPlatformTime::Seconds();CreationCapture->CaptureScene();}
    const auto State=Client->GetSessionState();
    if(LastSession!=int32(State))
    {
        LastSession=int32(State);CancelTouches();bWheel=bEditing=false;ClosePanel();
        Creation.Reset();ReleaseCharacterPreview();ConfirmationText.Reset();bBookDirty=false;
        if(State!=EACESessionState::InWorld && Menu){Menu->NativeDestruct();Menu=nullptr;SelectedSpell=0;}
        if(State==EACESessionState::CharacterSelect)OpenPanel("Characters");
        LastVendor=LastContainer=LastTrade=0;
    }
    if(PC->IsWorldTransitionActive())CancelTouches();
    if(State==EACESessionState::CharacterSelect && LastRosterSize!=Client->GetCharacters().Num())
    {LastRosterSize=Client->GetCharacters().Num();if(Panel=="Characters")BuildPanel();}
    if(State!=EACESessionState::InWorld)return;
    if(FPlatformTime::Seconds()>=NextPresentationRefresh)
    {RefreshHUDPanels();RefreshPresentation();RefreshPluginRail();RefreshChat();NextPresentationRefresh=FPlatformTime::Seconds()+.1;}
    if(Client->GetOpenVendorGuid()!=LastVendor)
    {LastVendor=Client->GetOpenVendorGuid();if(LastVendor)OpenPanel("Vendor");}
    if(Client->GetOpenExternalContainerGuid()!=LastContainer)
    {LastContainer=Client->GetOpenExternalContainerGuid();if(LastContainer)OpenPanel("Loot");}
    if(Client->GetTradePartnerGuid()!=LastTrade)
    {LastTrade=Client->GetTradePartnerGuid();if(LastTrade)OpenPanel("Trade");}
    if(bBookDirty)
    {
        bBookDirty=false;
        if(Client->GetSession()->GetBookInfo().bOpen)OpenPanel("Book");
    }
    if(auto* B=PC->GetActionDeckBinder())
    {
        FString Prompt=B->ServerConfirmPrompt;
        if(B->ManaStoneConfirmSource)
        {
            FACEWorldObject Target;Client->GetWorldObject(B->ManaStoneConfirmTarget,Target);
            Prompt=FString::Printf(TEXT("Destroy %s and drain its mana into this stone?"),*Target.Name);
        }
        if(!Prompt.IsEmpty() && (Panel!="Confirmation" || Prompt!=ConfirmationText))
        {ConfirmationText=Prompt;OpenPanel("Confirmation");}
        else if(Prompt.IsEmpty() && Panel=="Confirmation"){ConfirmationText.Reset();ClosePanel();}
    }
    if(Menu && !Panel.IsNone())Menu->RefreshIfDirty();
    if(Panel=="Chat")RefreshChat();
    // Session validates casts. Do not copy the whole spellbook every frame.
}
void UACEMobileWidget::Rebind(FName Name)
{if(Layout.IsValidIndex(SelectedControl) && Layout[SelectedControl].Id!="Move" && Layout[SelectedControl].Id!="Turn"){Layout[SelectedControl].Action=Name;SaveLayout();}ClosePanel();}
void UACEMobileWidget::BuildPanel()
{
    if(!PanelHost || !Client)return;
    RefreshPresentation();
    auto Content=SNew(SVerticalBox);
    const FName Page=Panel;
    if(Page.ToString().StartsWith(TEXT("Plugin:")))
    {
        const FString Id=Page.ToString().Mid(7);
        Content->AddSlot().AutoHeight().Padding(4)[Button(WantsHUDPlugin(Id)?TEXT("On screen — hide panel"):TEXT("Show this plugin on screen"),[this,Id](){ToggleHUDPlugin(Id);BuildPanel();})];
    }
    FString Title=Page.ToString();
    if(Title.StartsWith(TEXT("Plugin:")))if(auto* Host=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>())
        for(const auto& Plugin:Host->Plugins)if(Plugin->Id==Title.Mid(7)){Title=Plugin->DisplayTitle();break;}
    Content->AddSlot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Title,26)]
        +SHorizontalBox::Slot().AutoWidth()[Button(Page=="Characters"?TEXT("Back to login"):TEXT("Close"),[this,Page](){if(Page=="Characters")Client->Logout();else if(Page=="Confirmation")ResolveConfirmation(false);else if(Page=="Create character"){Creation.Reset();OpenPanel("Characters");}else ClosePanel();},Page!="Create character" || !Client->GetSession()->IsCharacterCreationPending())]];
    auto Body=SNew(SVerticalBox);
    auto Scroll=SNew(SScrollBox)+SScrollBox::Slot()[Body];
    Content->AddSlot().FillHeight(1).Padding(8)[Scroll];
    if(Page=="Menu")
    {
        auto Grid=SNew(SWrapBox).UseAllottedSize(true);
        for(FName P:{FName("Inventory"),FName("Inspect"),FName("Equipment"),FName("Spellbook"),FName("Hotbars"),FName("Character"),FName("Fellowship"),FName("Allegiance"),FName("Loot"),FName("Vendor"),FName("Trade"),FName("Salvage"),FName("Plugins"),FName("Options"),FName("Chat"),FName("Touch settings"),FName("Video"),FName("Combat settings")})
            Grid->AddSlot().Padding(4)[SNew(SBox).WidthOverride(210)[MenuTile(P,[this,P](){OpenPanel(P);})]];
        Body->AddSlot().AutoHeight()[Grid];
        Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Edit control layout"),[this](){ClosePanel();bEditing=true;SelectedControl=0;CancelTouches();})];
        Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Log out"),[this](){OpenPanel("Confirm logout");})];
        Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("D-pad: run / strafe; hold Walk to walk. Left/right arrows: turn. Swipe empty space to orbit; pinch to zoom. Tap an object, then Inspect, Use or Attack."))];
    }
    else if(Page=="Plugins")
    {
        Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("Installed plugins — enable a plugin, then open its controls here."),20)];
        if(auto* Host=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>())for(const auto& Plugin:Host->Plugins)
        {
            const FString Id=Plugin->Id;
            Body->AddSlot().AutoHeight().Padding(4,8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8)[SNew(SBox).WidthOverride(42).HeightOverride(42)[SNew(SACEPluginBadge).Plugin(Plugin)]]
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8)[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[Text(Plugin->DisplayTitle(),22)]
                    +SVerticalBox::Slot().AutoHeight()[Text(Plugin->Description,16)]]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4)[Button(Plugin->Enabled?TEXT("Disable"):TEXT("Enable"),[this,Host,Id,Enable=!Plugin->Enabled](){Host->SetEnabled(Id,Enable);BuildPanel();},true,18)]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4)[Button(TEXT("Open"),[this,Id](){OpenPanel(FName(*(TEXT("Plugin:")+Id)));},Plugin->Enabled,18)]];
        }
    }
    else if(Page.ToString().StartsWith(TEXT("Plugin:")))
    {
        Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Back to plugins"),[this](){OpenPanel("Plugins");})];
        if(auto* Host=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>())
            Body->AddSlot().AutoHeight().Padding(4)[Host->MakePanel(Page.ToString().Mid(7))];
    }
    else if(Page=="Confirm logout")Body->AddSlot().AutoHeight()[Button(TEXT("Return to character selection"),[this](){ClosePanel();Client->Logout();})];
    else if(Page=="Characters")
    {
        for(const auto& C:Client->GetCharacters())Body->AddSlot().AutoHeight().Padding(4)[Button(C.DeleteSeconds>0?TEXT("Restore ")+C.Name:C.Name,[this,Id=C.CharacterId,Deleted=C.DeleteSeconds>0](){if(Deleted)Client->GetSession()->RestoreCharacter(Id);else Client->EnterWorld(Id);})];
        Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Create character"),[this](){StartCharacterCreation();},Client->GetCharacters().Num()<Client->GetSession()->GetCharacterSlotCount())];
        Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Back to login"),[this](){Client->Logout();})];
    }
    else if(Page=="Create character")BuildCharacterCreation(Body);
    else if(Page=="Confirmation")
    {
        Body->AddSlot().AutoHeight().Padding(16)[Text(ConfirmationText,26)];
        Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("Yes"),[this](){ResolveConfirmation(true);})];
        Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("No"),[this](){ResolveConfirmation(false);})];
    }
    else if(Page=="Book")
    {
        const auto& Book=Client->GetSession()->GetBookInfo();FACEWorldObject Object;Client->GetWorldObject(Book.BookGuid,Object);
        const int32 Total=FMath::Max(1,Book.NumPages);
        Body->AddSlot().AutoHeight().Padding(8)[Text(FString::Printf(TEXT("%s — %d / %d"),*Object.Name,Book.CurrentPage+1,Total),26)];
        if(!Book.AuthorName.IsEmpty())Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("By ")+Book.AuthorName)];
        auto Pages=SNew(SHorizontalBox);
        Pages->AddSlot().FillWidth(1).Padding(4)[Button(TEXT("Previous page"),[this,Id=Book.BookGuid,P=Book.CurrentPage](){Client->SendBookPageData(Id,P-1);},Book.CurrentPage>0)];
        Pages->AddSlot().FillWidth(1).Padding(4)[Button(TEXT("Next page"),[this,Id=Book.BookGuid,P=Book.CurrentPage](){Client->SendBookPageData(Id,P+1);},Book.CurrentPage+1<Total)];
        Body->AddSlot().AutoHeight()[Pages];
        const FString PageText=Book.Pages.IsValidIndex(Book.CurrentPage)?Book.Pages[Book.CurrentPage]:FString();
        Body->AddSlot().AutoHeight().Padding(12)[Text(!PageText.IsEmpty()?PageText:Book.CurrentPage==0 && !Book.Inscription.IsEmpty()?Book.Inscription:TEXT("Waiting for page..."),24)];
    }
    else if(Page=="Combat settings")
    {
        if(auto* B=PC->GetActionDeckBinder())
        {
            Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("Attack height"))];
            auto Heights=SNew(SHorizontalBox);
            for(int32 I=1;I<=3;++I)Heights->AddSlot().FillWidth(1).Padding(4)[Button(FString(B->CombatAttackHeight==uint32(I)?TEXT("Selected: "):TEXT(""))+(I==1?TEXT("Low"):I==2?TEXT("Medium"):TEXT("High")),[this,B,I](){B->CombatAttackHeight=I;BuildPanel();})];
            Body->AddSlot().AutoHeight()[Heights];
            Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("Power / accuracy: low to high"))];
            Body->AddSlot().AutoHeight().Padding(16)[SNew(SBox).HeightOverride(56)[SNew(SSlider).Value(B->RequestedAttackPower)
                .OnValueChanged_Lambda([B](float V){B->SetRequestedAttackPower(V);})]];
            Body->AddSlot().AutoHeight().Padding(4)[Button(B->bCombatAutoRepeat?TEXT("Auto repeat: On"):TEXT("Auto repeat: Off"),[this,B](){B->bCombatAutoRepeat=!B->bCombatAutoRepeat;Client->SendSetSingleCharacterOption(0,B->bCombatAutoRepeat);BuildPanel();})];
            Body->AddSlot().AutoHeight().Padding(4)[Button(B->bCombatAutoTarget?TEXT("Auto target: On"):TEXT("Auto target: Off"),[this,B](){B->bCombatAutoTarget=!B->bCombatAutoTarget;Client->SendSetSingleCharacterOption(0x0D,B->bCombatAutoTarget);BuildPanel();})];
        }
    }
    else if(Page=="Touch settings")
    {
        Body->AddSlot().AutoHeight().Padding(8)[Button(Settings->bPinnedChat?TEXT("Chat: Pinned on screen — use full-screen only"):TEXT("Chat: Full-screen — pin on screen"),[this](){SetPinnedChat(!Settings->bPinnedChat);BuildPanel();})];
        Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("On-screen plugins. Swipe across the panels to see more; tap a panel title to open its full controls."))];
        if(auto* Host=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>())for(const auto& Plugin:Host->Plugins)if(Plugin->Enabled)
            Body->AddSlot().AutoHeight().Padding(4)[Button(Plugin->DisplayTitle()+(WantsHUDPlugin(Plugin->Id)?TEXT(": On screen — hide"):TEXT(": Menu only — show on screen")),[this,Id=Plugin->Id](){ToggleHUDPlugin(Id);BuildPanel();})];
        Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("Camera sensitivity"))];
        Body->AddSlot().AutoHeight().Padding(16)[SNew(SBox).HeightOverride(56)[SNew(SSlider).MinValue(.05f).MaxValue(.8f).Value(Settings->LookSensitivity)
            .OnValueChanged_Lambda([this](float V){Settings->LookSensitivity=V;Settings->SaveConfig();})]];
        Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("Control opacity"))];
        Body->AddSlot().AutoHeight().Padding(16)[SNew(SBox).HeightOverride(56)[SNew(SSlider).MinValue(.25f).MaxValue(1.f).Value(Settings->ControlOpacity)
            .OnValueChanged_Lambda([this](float V){Settings->ControlOpacity=V;Settings->SaveConfig();})]];
        Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("Mirror layout (left / right handed)"),[this](){for(auto& C:Settings->Controls)C.Position.X=1-C.Position.X;Settings->bLeftHanded=!Settings->bLeftHanded;Settings->SaveConfig();FitControls();})];
        Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("Add a button"),[this](){if(Layout.Num()<20){FACETouchControl C;C.Id=FName(*FString::Printf(TEXT("Extra%d"),Layout.Num()));C.Action="Attack";C.Position={.5f,.5f};Layout.Add(C);SaveLayout();FitControls();SelectedControl=Layout.Num()-1;ClosePanel();bEditing=true;}},Layout.Num()<20)];
        Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("Edit layout to move buttons. Tap a control, then Rebind or Resize. Overlapping placements are rejected. The menu rail always stays reachable."))];
        for(int32 I=0;I<Layout.Num();++I)if(!Layout[I].bVisible)
            Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Show ")+Layout[I].Id.ToString(),[this,I](){Layout[I].bVisible=true;SaveLayout();FitControls();BuildPanel();})];
    }
    else if(Page=="Video")
    {
        if(auto* Video=UGameUserSettings::GetGameUserSettings())
        {
            Body->AddSlot().AutoHeight().Padding(6)[Text(FString::Printf(TEXT("Shadows: %d    Textures: %d    Frame limit: %.0f"),Video->GetShadowQuality(),Video->GetTextureQuality(),Video->GetFrameRateLimit()))];
            auto Choices=[&](const FString& Label,TFunction<void(int32)> Set)
            {
                Body->AddSlot().AutoHeight().Padding(6)[Text(Label)];auto Row=SNew(SHorizontalBox);
                const TCHAR* Names[]={TEXT("Off / lowest"),TEXT("Low"),TEXT("Medium"),TEXT("High")};
                for(int32 I=0;I<4;++I)Row->AddSlot().FillWidth(1).Padding(3)[Button(Names[I],[this,Set,I](){Set(I);ACERuntimeOptions::Apply();BuildPanel();})];
                Body->AddSlot().AutoHeight()[Row];
            };
            Choices(TEXT("Shadows"),[Video](int32 V){Video->SetShadowQuality(V);Video->ApplyNonResolutionSettings();Video->SaveSettings();});
            Choices(TEXT("Texture detail"),[Video](int32 V){Video->SetTextureQuality(V);Video->ApplyNonResolutionSettings();Video->SaveSettings();});
            Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("Limit to 30 FPS (battery saver)"),[this,Video](){Video->SetFrameRateLimit(30);Video->ApplyNonResolutionSettings();Video->SaveSettings();BuildPanel();})];
            Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("Limit to 60 FPS"),[this,Video](){Video->SetFrameRateLimit(60);Video->ApplyNonResolutionSettings();Video->SaveSettings();BuildPanel();})];
            Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("Toggle dynamic lights"),[this](){ACERuntimeOptions::Set(TEXT("DynamicLightingQuality"),ACERuntimeOptions::DynamicLightingQuality()?0:2);ACERuntimeOptions::Apply();BuildPanel();})];
        }
    }
    else if(Page=="Rebind" && Layout.IsValidIndex(SelectedControl))
    {
        Body->AddSlot().AutoHeight()[Text(Layout[SelectedControl].Id.ToString()+TEXT(" — choose an action"))];
        auto Grid=SNew(SWrapBox).UseAllottedSize(true);
        const bool Fixed=Layout[SelectedControl].Id=="Move" || Layout[SelectedControl].Id=="Turn";
        if(Fixed)Body->AddSlot().AutoHeight()[Text(TEXT("The D-pad and turn buttons can be moved and resized. Their movement function is fixed."))];
        else for(FName A:ACEMobile::Actions())Grid->AddSlot().Padding(4)[SNew(SBox).WidthOverride(210)[Button(A.ToString(),[this,A](){Rebind(A);})]];
        Body->AddSlot().AutoHeight()[Grid];
        if(!Fixed)Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Hide this button"),[this](){Layout[SelectedControl].bVisible=false;SaveLayout();ClosePanel();})];
    }
    else if(Page=="Resize" && Layout.IsValidIndex(SelectedControl))
    {
        Body->AddSlot().AutoHeight()[Text(TEXT("Choose a size. Sizes that would overlap another control are disabled."))];
        for(float Size:{56.f,64.f,80.f,100.f,120.f,144.f,160.f,168.f,192.f,224.f})
        {
            const auto& C=Layout[SelectedControl];
            const bool Allowed=(C.Id=="Turn"?Size>=160:C.Id=="Move"?Size>=168:Size<=160) && ACEMobile::CanPlace(Layout,SelectedControl,C.Position,Size,ViewSize,ControlsTop);
            Body->AddSlot().AutoHeight().Padding(4)[Button(FString::Printf(TEXT("%.0f"),Size),[this,Size](){Layout[SelectedControl].Size=Size;SaveLayout();ClosePanel();},Allowed)];
        }
    }
    else if(Page=="Chat")
    {
        Body->AddSlot().AutoHeight().Padding(4)[Button(Settings->bPinnedChat?TEXT("Chat: Pinned — switch to full-screen only"):TEXT("Pin chat on screen"),[this](){SetPinnedChat(!Settings->bPinnedChat);BuildPanel();})];
        Body->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(240)[SAssignNew(ChatScroll,SScrollBox)+SScrollBox::Slot()[SAssignNew(ChatLines,SVerticalBox)]]];
        Body->AddSlot().AutoHeight().Padding(4)[SNew(SBox).MinDesiredHeight(56)[SAssignNew(ChatEntry,SEditableTextBox)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular",24)).HintText(FText::FromString(TEXT("Message or /command")))]];
        Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Send"),[this](){if(ChatEntry){const FString Message=ChatEntry->GetText().ToString();if(auto* B=PC->GetActionDeckBinder())B->TrySendChatFromEntry(&Message);ChatEntry->SetText(FText::GetEmpty());}})];
        bChatDirty=true;RefreshChat();
    }
    else
    {
        auto* Binder=PC->GetActionDeckBinder();
        if(Binder)
        {
            if(!Menu){Menu=CreateWidget<UACEVRMenu>(PC);Menu->bTouchMode=true;Menu->InitializeMenu(nullptr,Client,Binder);Menu->TouchClose=[Weak=TWeakObjectPtr<UACEMobileWidget>(this)](){if(Weak.IsValid())Weak->ClosePanel();};}
            Menu->OpenPage(Page);Content->RemoveSlot(Scroll);Content->AddSlot().FillHeight(1)[Menu->TakeWidget()];Menu->RefreshIfDirty();
        }
    }
    PanelHost->SetContent(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.85f)).Padding(6)
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SBox).WidthOverride(948).HeightOverride(528)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(MobilePanelInk).Padding(6)[Content]]]]);
    PanelHost->SetVisibility(EVisibility::Visible);
}

void UACEMobileWidget::StartCharacterCreation()
{
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    Creation=MakeUnique<FACECharacterCreation>();CreationMessage.Reset();CreationTab=0;
    if(!Dat || !Dat->GetPortalDat() || !Creation->Load(*Dat->GetPortalDat(),CreationMessage))
    {Creation.Reset();CreationMessage=TEXT("Load the retail game files before creating a character.");}
    else {Creation->LoadStrings(Dat->GetDatDirectory());Creation->RandomizeCharacter();Creation->Selection.Slot=Client->GetCharacters().Num();}
    OpenPanel("Create character");
}
void UACEMobileWidget::BuildCharacterCreation(TSharedRef<SVerticalBox> Body)
{
    if(!CreationMessage.IsEmpty())Body->AddSlot().AutoHeight().Padding(8)[Text(CreationMessage)];
    if(!Creation)return;
    auto& Model=*Creation;auto& Selection=Model.Selection;
    const bool Pending=Client->GetSession()->IsCharacterCreationPending();
    if(Pending){Body->AddSlot().AutoHeight()[Text(TEXT("Waiting for the server to create your character..."))];return;}
    auto Tabs=SNew(SHorizontalBox);
    const TCHAR* Names[]={TEXT("Heritage"),TEXT("Class"),TEXT("Stats"),TEXT("Skills"),TEXT("Style"),TEXT("Finish")};
    for(int32 I=0;I<6;++I)Tabs->AddSlot().FillWidth(1).Padding(3)[Button(Names[I],[this,I](){CreationTab=I;CreationMessage.Reset();BuildPanel();},true,18)];
    Body->AddSlot().AutoHeight().Padding(4)[Tabs];
    if(CreationTab>=4)
    {
        auto Details=SNew(SVerticalBox);
        Body->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().Padding(6).VAlign(VAlign_Top)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[CharacterPreview()]
                +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Rotate"),[this](){CreationYaw+=45;BuildPanel();})]]
            +SHorizontalBox::Slot().FillWidth(1).Padding(8)[SNew(SBox).HeightOverride(356)[SNew(SScrollBox)+SScrollBox::Slot()[Details]]]];
        Body=Details;
    }
    if(CreationTab==0)
    {
        TArray<uint32> Ids;Model.Heritages.GetKeys(Ids);Ids.Sort();
        auto Choices=SNew(SWrapBox).UseAllottedSize(true);
        for(uint32 Id:Ids)Choices->AddSlot().Padding(4)[Button((Id==Selection.Heritage?TEXT("Selected: "):TEXT(""))+Model.Heritages[Id].Name,[this,Id](){Creation->SelectHeritage(Id);BuildPanel();})];
        Body->AddSlot().AutoHeight()[Choices];
        if(const auto* H=Model.Heritage())for(const auto& Sex:H->Sexes)
            Body->AddSlot().AutoHeight().Padding(4)[Button((Sex.Key==Selection.Sex?TEXT("Selected: "):TEXT(""))+Sex.Value.Name,[this,Id=Sex.Key](){Creation->SelectSex(Id);BuildPanel();})];
    }
    else if(CreationTab==1)
    {
        if(const auto* H=Model.Heritage())for(int32 I=0;I<H->Professions.Num();++I)
            Body->AddSlot().AutoHeight().Padding(4)[Button((I==Selection.Profession?TEXT("Selected: "):TEXT(""))+H->Professions[I].Name,[this,I](){Creation->SelectProfession(I);BuildPanel();})];
    }
    else if(CreationTab==2)
    {
        Body->AddSlot().AutoHeight().Padding(8)[Text(FString::Printf(TEXT("Attribute points remaining: %d"),Model.AttributeCredits()))];
        const TCHAR* Attributes[]={TEXT("Strength"),TEXT("Endurance"),TEXT("Coordination"),TEXT("Quickness"),TEXT("Focus"),TEXT("Self")};
        for(int32 I=0;I<6;++I)
        {
            auto Row=SNew(SHorizontalBox);
            Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(FString::Printf(TEXT("%s: %d"),Attributes[I],Selection.Attributes[I]))];
            for(int32 Delta:{-10,-1,1,10})Row->AddSlot().AutoWidth().Padding(3)[Button(FString::Printf(TEXT("%+d"),Delta),[this,I,Delta](){Creation->SetAttribute(I,Creation->Selection.Attributes[I]+Delta);BuildPanel();},!Selection.Locked[I])];
            Row->AddSlot().AutoWidth().Padding(3)[Button(Selection.Locked[I]?TEXT("Unlock"):TEXT("Lock"),[this,I](){Creation->Selection.Locked[I]=!Creation->Selection.Locked[I];BuildPanel();})];
            Body->AddSlot().AutoHeight().Padding(4)[Row];
        }
    }
    else if(CreationTab==3)
    {
        Body->AddSlot().AutoHeight().Padding(8)[Text(FString::Printf(TEXT("Skill credits remaining: %d"),Model.SkillCredits()))];
        TArray<uint32> Ids;Model.Skills.GetKeys(Ids);Ids.Sort([&](uint32 A,uint32 B){return Model.Skills[A].Name<Model.Skills[B].Name;});
        for(uint32 Id:Ids)
        {
            if(!Model.Skills[Id].Chargen || !Selection.Skills.IsValidIndex(Id))continue;
            const uint32 Level=Selection.Skills[Id];
            auto Row=SNew(SHorizontalBox);
            Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(Model.Skills[Id].Name+TEXT(" — ")+(Level==3?TEXT("Specialized"):Level==2?TEXT("Trained"):TEXT("Untrained")))];
            for(int32 Change:{-1,1})
            {
                auto Copy=Model;const bool Allowed=Copy.SetSkill(Id,Level+Change);
                Row->AddSlot().AutoWidth().Padding(4)[Button(Change<0?TEXT("Lower"):TEXT("Raise"),[this,Id,Change](){Creation->SetSkill(Id,Creation->Selection.Skills[Id]+Change);BuildPanel();},Allowed)];
            }
            Body->AddSlot().AutoHeight().Padding(4)[Row];
        }
    }
    else if(CreationTab==4)
    {
        Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("Randomize appearance"),[this](){Creation->RandomizeAppearance();BuildPanel();})];
        if(const auto* Sex=Model.Sex())
        {
            auto Cycle=[&](const TCHAR* Name,uint32& Field,int32 Count)
            {if(Count<1)return;uint32* Value=&Field;Body->AddSlot().AutoHeight().Padding(4)[Button(FString::Printf(TEXT("%s: %u / %d — change"),Name,Field+1,Count),[this,Value,Count](){*Value=(*Value+1)%Count;BuildPanel();})];};
            Cycle(TEXT("Hair style"),Selection.HairStyle,Sex->Hair.Num());Cycle(TEXT("Hair color"),Selection.HairColor,Sex->HairColors.Num());
            Cycle(TEXT("Eyes"),Selection.Eyes,Sex->Eyes.Num());Cycle(TEXT("Eye color"),Selection.EyeColor,Sex->EyeColors.Num());
            Cycle(TEXT("Nose"),Selection.Nose,Sex->Nose.Num());Cycle(TEXT("Mouth"),Selection.Mouth,Sex->Mouth.Num());
            Body->AddSlot().AutoHeight().Padding(8)[Text(TEXT("Skin shade"))];
            Body->AddSlot().AutoHeight().Padding(12)[SNew(SBox).HeightOverride(56)[SNew(SSlider).Value(Selection.SkinHue).OnValueChanged_Lambda([this](float V){Creation->Selection.SkinHue=V;})
                .OnMouseCaptureEnd_Lambda([this](){BuildPanel();})]];
            const TCHAR* GearNames[]={TEXT("Headwear"),TEXT("Shirt"),TEXT("Trousers"),TEXT("Footwear")};
            for(int32 I=0;I<4;++I)
            {
                const FString Gear=Sex->Gear[I].IsValidIndex(Selection.GearStyle[I])?Sex->Gear[I][Selection.GearStyle[I]].Name:TEXT("None");
                Body->AddSlot().AutoHeight().Padding(4)[Button(FString(GearNames[I])+TEXT(": ")+Gear,[this,I]()
                {auto& S=Creation->Selection;const int32 N=Creation->Sex()->Gear[I].Num();if(N)S.GearStyle[I]=I==0?(S.GearStyle[I]+2)%(N+1)-1:(S.GearStyle[I]+1)%N;auto Colors=Creation->GearColors(I);S.GearColor[I]=Colors.IsEmpty()?0:Colors[0];BuildPanel();})];
                const auto Colors=Model.GearColors(I);
                if(!Colors.IsEmpty())Body->AddSlot().AutoHeight().Padding(4)[Button(FString(GearNames[I])+TEXT(" color — change"),[this,I,Colors](){const int32 Index=Colors.Find(Creation->Selection.GearColor[I]);Creation->Selection.GearColor[I]=Colors[(Index+1)%Colors.Num()];BuildPanel();})];
            }
        }
    }
    else
    {
        const auto* H=Model.Heritage();
        Body->AddSlot().AutoHeight().Padding(8)[Text(FString::Printf(TEXT("%s — %s\nUnspent attribute points: %d. Skill credits: %d."),H?*H->Name:TEXT(""),H && H->Professions.IsValidIndex(Selection.Profession)?*H->Professions[Selection.Profession].Name:TEXT("Custom"),Model.AttributeCredits(),Model.SkillCredits()))];
        Body->AddSlot().AutoHeight().Padding(8)[SNew(SBox).MinDesiredHeight(56)[SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",24)).HintText(FText::FromString(TEXT("Character name"))).Text(FText::FromString(Selection.Name))
            .OnTextChanged_Lambda([this](const FText& Name){Creation->Selection.Name=Name.ToString();})]];
        for(int32 I=0;I<Model.StartAreas.Num();++I)Body->AddSlot().AutoHeight().Padding(4)[Button((Selection.StartArea==uint32(I)?TEXT("Selected: "):TEXT(""))+Model.StartAreas[I],[this,I](){Creation->Selection.StartArea=I;BuildPanel();})];
        Body->AddSlot().AutoHeight().Padding(8)[Button(TEXT("Create this character"),[this]()
        {
            Creation->Selection.Name=FACECharacterCreation::FormatName(Creation->Selection.Name.TrimStartAndEnd());
            if(Creation->Validate(CreationMessage) && !Client->GetSession()->CreateCharacter(Creation->Selection))CreationMessage=TEXT("Unable to create character. Check the connection and available slots.");
            BuildPanel();
        })];
    }
}
