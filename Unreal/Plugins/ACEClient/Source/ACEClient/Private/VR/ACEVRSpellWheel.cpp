#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "ACEVRUIStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/ACEUIResourceResolver.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
constexpr int32 WheelPageSize = 12;
constexpr float WheelSize = 720.f;
FVector2D RadialPoint(float Angle, float Radius)
{
    return FVector2D(360.f + FMath::Sin(Angle)*Radius, 355.f - FMath::Cos(Angle)*Radius);
}

class SSpellRing : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SSpellRing) {} SLATE_END_ARGS()
    void Construct(const FArguments&, int32 InCount, int32 InHover) { Count=FMath::Max(1,InCount); Hover=InHover; }
    FVector2D ComputeDesiredSize(float) const override { return FVector2D(WheelSize,820); }
    int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
        int32 Layer, const FWidgetStyle&, bool) const override
    {
        const float Step=2.f*PI/Count;
        for (int32 Slot=0; Slot<Count; ++Slot)
        {
            TArray<FVector2D> Arc, Inner, Outer;
            for (int32 J=0; J<=20; ++J)
            {
                const float Angle=Step*(Slot-.5f)+.018f+(Step-.036f)*J/20.f;
                Arc.Add(RadialPoint(Angle,255)); Inner.Add(RadialPoint(Angle,205)); Outer.Add(RadialPoint(Angle,305));
            }
            FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Arc,ESlateDrawEffect::None,
                Slot==Hover ? FLinearColor(FColor(97,64,23,245)) : FLinearColor(FColor(9,13,20,225)),true,98.f);
            FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Inner,ESlateDrawEffect::None,ACEVRUIStyle::Gold,true,2.f);
            FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Outer,ESlateDrawEffect::None,
                Slot==Hover ? ACEVRUIStyle::TextColor : ACEVRUIStyle::Gold,true,Slot==Hover ? 4.f : 2.f);
        }
        return Layer+1;
    }
private:
    int32 Count=1,Hover=INDEX_NONE;
};

class SSpellWheel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SSpellWheel) {} SLATE_END_ARGS()
    void Construct(const FArguments&, UACEClientSubsystem* Client, const TArray<int32>& Spells, int32 Hover, int32 Page)
    {
        const int32 First=Page*WheelPageSize, Count=FMath::Clamp(Spells.Num()-First,0,WheelPageSize);
        auto Canvas=SNew(SCanvas);
        auto TextAt=[&](FString Text, FVector2D Position, FVector2D Size, float Height, FLinearColor Color) {
            Canvas->AddSlot().Position(Position).Size(Size)
                [ACEVRUIStyle::Text(Client,Text,Height,Size.X,Color)];
        };
        auto* Resources=Client->GetUIResourceResolver();
        auto* Dat=Client->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
        Brushes.SetNum(Count); // Addresses remain stable for SImage's lifetime.
        for (int32 I=0; I<Count; ++I)
        {
            Brushes[I].SetResourceObject(Resources->ResolveSpellIcon(Spells[First+I]));
            Brushes[I].ImageSize=FVector2D(64,64);
            Brushes[I].DrawAs=ESlateBrushDrawType::Image;
            const FVector2D Center=RadialPoint(2.f*PI*I/Count,255);
            Canvas->AddSlot().Position(Center-FVector2D(34,34)).Size(FVector2D(68,68))
                [SNew(SImage).Image(&Brushes[I])];
        }
        FString Name; uint32 Icon=0;
        if (Spells.IsValidIndex(Hover)) Dat->TryGetSpellInfo(Spells[Hover],Name,Icon);
        if (Name.IsEmpty()) Name=Count ? TEXT("Point the right stick\ntoward a spell") : TEXT("This tab is empty\nAdd spells using the spellbook");
        auto Center=SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[ACEVRUIStyle::Text(Client,TEXT("SELECT SPELL"),24,286,ACEVRUIStyle::Gold)]
            + SVerticalBox::Slot().AutoHeight()[ACEVRUIStyle::Text(Client,Name,32,286,ACEVRUIStyle::TextColor)];
        Canvas->AddSlot().Position(FVector2D(195,275)).Size(FVector2D(330,170))[ACEVRUIStyle::Frame(Center,16)];
        static const TCHAR* Tabs[]={TEXT("I"),TEXT("II"),TEXT("III"),TEXT("IV"),TEXT("V"),TEXT("VI"),TEXT("VII"),TEXT("VIII")};
        for (int32 I=0; I<8; ++I)
        {
            const bool Active=I==Client->GetActiveSpellBar();
            Canvas->AddSlot().Position(FVector2D(88+I*70,2)).Size(FVector2D(62,42))
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(Active ? FLinearColor(FColor(97,64,23)) : ACEVRUIStyle::Background)
                .HAlign(HAlign_Center).Padding(3)[ACEVRUIStyle::Text(Client,Tabs[I],30,56,Active ? ACEVRUIStyle::TextColor : ACEVRUIStyle::Gold)]];
        }
        TextAt(TEXT("A / B: next / previous tab"),FVector2D(160,686),FVector2D(460,40),28,ACEVRUIStyle::TextColor);
        TextAt(TEXT("Trigger: select     Stick click / X: cancel"),FVector2D(80,726),FVector2D(610,40),26,ACEVRUIStyle::TextColor);
        if (Spells.Num()>WheelPageSize)
            TextAt(FString::Printf(TEXT("Grips: page %d / %d"),Page+1,FMath::DivideAndRoundUp(Spells.Num(),WheelPageSize)),
                FVector2D(220,770),FVector2D(360,40),26,ACEVRUIStyle::Gold);
        ChildSlot[SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
            + SOverlay::Slot()[SNew(SSpellRing,Count,Hover==INDEX_NONE ? INDEX_NONE : Hover-First)]
            + SOverlay::Slot()[Canvas]];
    }
private:
    TArray<FSlateBrush> Brushes;
};
}

void UACEVRComponent::ToggleSpellWheel()
{
    if (bSpellWheelOpen) { CloseSpellWheel(); return; }
    if (!bActive || IsInputBlocked() || !HasEquippedCaster()) return;
    CancelGestures();
    bSpellWheelOpen=true; bWheelStickReady=TurnStick.Size()<.25f; WheelPage=0;
    WheelSpells.Reset();
    for (int32 Spell:Client->GetSpellBar(Client->GetActiveSpellBar())) if (Spell) WheelSpells.Add(Spell);
    WheelHover=WheelSpells.IndexOfByKey(SelectedSpell);
    if (WheelHover!=INDEX_NONE) WheelPage=WheelHover/WheelPageSize;
    if (!SpellWheelPanel)
    {
        SpellWheelPanel=NewObject<UWidgetComponent>(PresentationActor,TEXT("VRSpellWheel"));
        PresentationActor->AddInstanceComponent(SpellWheelPanel);
        SpellWheelPanel->SetupAttachment(TrackingOrigin);
        SpellWheelPanel->SetWidgetSpace(EWidgetSpace::World);
        SpellWheelPanel->SetDrawSize(FVector2D(WheelSize,820));
        SpellWheelPanel->SetBlendMode(EWidgetBlendMode::Transparent);
        SpellWheelPanel->SetBackgroundColor(FLinearColor::Transparent);
        SpellWheelPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SpellWheelPanel->SetTwoSided(true); SpellWheelPanel->SetCastShadow(false);
        SpellWheelPanel->SetTickWhenOffscreen(true); SpellWheelPanel->SetManuallyRedraw(true);
        SpellWheelPanel->SetTranslucentSortPriority(110); SpellWheelPanel->RegisterComponent();
    }
    // Capture the view once. The tracking origin carries locomotion, while head
    // movement does not chase the wheel or drag the selection away from the stick.
    const FTransform View(FRotator(0,Head->GetComponentRotation().Yaw,0),Head->GetComponentLocation());
    const FVector Position=View.TransformPosition(FVector(120,0,-5));
    SpellWheelPanel->SetWorldLocationAndRotation(Position,(Head->GetComponentLocation()-Position).Rotation());
    SpellWheelPanel->SetWorldScale3D(FVector(.095f)); SpellWheelPanel->SetVisibility(true);
    RefreshSpellWheel();
}

void UACEVRComponent::CloseSpellWheel()
{
    if (!bSpellWheelOpen) return;
    bSpellWheelOpen=false; bWheelTurnNeutral=true;
    if (SpellWheelPanel) SpellWheelPanel->SetVisibility(false);
}

void UACEVRComponent::RefreshSpellWheel()
{
    if (!SpellWheelPanel || !bSpellWheelOpen) return;
    SpellWheelPanel->SetSlateWidget(SNew(SSpellWheel,Client.Get(),WheelSpells,WheelHover,WheelPage));
    WheelRedraws=3; SpellWheelPanel->RequestRedraw();
}

void UACEVRComponent::ChangeWheelTab(int32 Direction)
{
    Client->SetActiveSpellBar((Client->GetActiveSpellBar()+Direction+8)%8);
    WheelSpells.Reset();
    for (int32 Spell:Client->GetSpellBar(Client->GetActiveSpellBar())) if (Spell) WheelSpells.Add(Spell);
    WheelPage=0; WheelHover=INDEX_NONE; bWheelStickReady=TurnStick.Size()<.25f;
    RefreshSpellWheel(); Pulse(false,.15f);
}

void UACEVRComponent::ChangeWheelPage(int32 Direction)
{
    const int32 Pages=FMath::Max(1,FMath::DivideAndRoundUp(WheelSpells.Num(),WheelPageSize));
    WheelPage=(WheelPage+Direction+Pages)%Pages; WheelHover=INDEX_NONE;
    bWheelStickReady=TurnStick.Size()<.25f; RefreshSpellWheel();
}

void UACEVRComponent::UpdateSpellWheel()
{
    if (!bSpellWheelOpen) return;
    if (IsInputBlocked() || !HasEquippedCaster()) { CloseSpellWheel(); return; }
    const float Length=TurnStick.Size();
    if (Length<.25f) bWheelStickReady=true;
    const int32 Count=FMath::Clamp(WheelSpells.Num()-WheelPage*WheelPageSize,0,WheelPageSize);
    if (bWheelStickReady && Length>.55f && Count)
    {
        float Angle=FMath::Atan2(TurnStick.X,TurnStick.Y);
        if (Angle<0) Angle+=2.f*PI;
        const int32 Hover=WheelPage*WheelPageSize+FMath::FloorToInt(Angle*Count/(2.f*PI)+.5f)%Count;
        if (Hover!=WheelHover) { WheelHover=Hover; RefreshSpellWheel(); Pulse(false,.12f); }
    }
    if (WheelRedraws>0) { --WheelRedraws; SpellWheelPanel->RequestRedraw(); }
}

void UACEVRComponent::ConfirmWheelSpell()
{
    if (!bSpellWheelOpen || IsInputBlocked() || !HasEquippedCaster() || !WheelSpells.IsValidIndex(WheelHover)) return;
    const int32 Spell=WheelSpells[WheelHover];
    CloseSpellWheel();
    // Confirmation selects only. A fresh trigger press is required to cast.
    if (SelectSpell(Spell)) SetCastFeedback(TEXT("Spell selected. Pull trigger to cast."));
}
