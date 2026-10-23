#include "UI/ACEFrameRateWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UI/ACEUIElementManager.h"
#include "ACERuntimeOptions.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
    constexpr const TCHAR* LayoutSection=TEXT("ACEClient.DatHUD");
    const FVector2D CounterSize(320,48);
}

TSharedRef<SWidget> UACEFrameRateWidget::RebuildWidget()
{
    static const FSlateRoundedBoxBrush Background(FLinearColor(.015f,.018f,.024f,.9f),6.f);
    SetVisibility(CanMove()?ESlateVisibility::Visible:ESlateVisibility::HitTestInvisible);
    return SNew(SBorder).BorderImage(&Background).Padding(FMargin(12,6))
        .Cursor_Lambda([this](){return CanMove()?EMouseCursor::CardinalCross:EMouseCursor::Default;})
        [SAssignNew(Label,STextBlock).Text(FText::FromString(CounterText))
            .Visibility(EVisibility::HitTestInvisible)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold",24)).ColorAndOpacity(FLinearColor(.97f,.92f,.76f))];
}
bool UACEFrameRateWidget::CanMove() const
{
    return bDesktop && UIManager.IsValid() && !UIManager->IsUiLocked();
}
void UACEFrameRateWidget::UpdatePresentation(UACEUIElementManager* Manager, bool Desktop, FVector2D ViewportSize, bool ForceLayout)
{
    UIManager=Manager;bDesktop=Desktop;ViewSize=ViewportSize;
    if (!CanMove()) CancelDrag();
    SetVisibility(CanMove()?ESlateVisibility::Visible:ESlateVisibility::HitTestInvisible);
    if (!Desktop) return;
    if (!bLoadedPosition)
    {
        bLoadedPosition=true;
        double X=16,Y=64;
        if (GConfig && GConfig->GetDouble(LayoutSection,TEXT("FPS_X"),X,GGameUserSettingsIni)
            && GConfig->GetDouble(LayoutSection,TEXT("FPS_Y"),Y,GGameUserSettingsIni)
            && FMath::IsFinite(X) && FMath::IsFinite(Y)) Position=FVector2D(X,Y);
    }
    Place(ForceLayout);
}
void UACEFrameRateWidget::Place(bool Force)
{
    FVector2D At=Position;
    if (ACERuntimeOptions::Get(TEXT("ConstrainUIWindows"))>.5f && ViewSize.X>0 && ViewSize.Y>0)
    {
        At.X=FMath::Clamp(At.X,0.,FMath::Max(0.,ViewSize.X-CounterSize.X));
        At.Y=FMath::Clamp(At.Y,0.,FMath::Max(0.,ViewSize.Y-CounterSize.Y));
    }
    // A temporary smaller viewport fits the display without replacing the
    // user's saved position. Avoid invalidating layout every sample/frame.
    if (Force || !PlacedPosition.Equals(At))
    {
        PlacedPosition=At;
        SetAlignmentInViewport(FVector2D::ZeroVector);
        SetDesiredSizeInViewport(CounterSize);
        SetPositionInViewport(At,false);
    }
}
FReply UACEFrameRateWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!CanMove() || Event.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
    bDragging=true;bMoved=false;
    DragStart=Event.GetScreenSpacePosition();DragPosition=PlacedPosition;
    DragScale=FMath::Max(.01f,Geometry.GetAccumulatedLayoutTransform().GetScale());
    return FReply::Handled().CaptureMouse(TakeWidget());
}
FReply UACEFrameRateWidget::NativeOnMouseMove(const FGeometry&, const FPointerEvent& Event)
{
    if (!bDragging) return FReply::Unhandled();
    if (!CanMove() || !Event.IsMouseButtonDown(EKeys::LeftMouseButton))
    { FinishDrag();return FReply::Handled().ReleaseMouseCapture(); }
    const FVector2D Delta=(Event.GetScreenSpacePosition()-DragStart)/DragScale;
    if (!Delta.IsNearlyZero() || bMoved)
    {
        bMoved=true;Position=DragPosition+Delta;Place();
        Position=PlacedPosition; // Commit the constrained location on a user drag.
    }
    return FReply::Handled();
}
FReply UACEFrameRateWidget::NativeOnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
    if (!bDragging || Event.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
    FinishDrag();return FReply::Handled().ReleaseMouseCapture();
}
void UACEFrameRateWidget::FinishDrag()
{
    if (bDragging && bMoved && GConfig)
    {
        GConfig->SetDouble(LayoutSection,TEXT("FPS_X"),Position.X,GGameUserSettingsIni);
        GConfig->SetDouble(LayoutSection,TEXT("FPS_Y"),Position.Y,GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
    }
    bDragging=bMoved=false;
}
void UACEFrameRateWidget::CancelDrag()
{
    FinishDrag();
    if (HasMouseCapture() && FSlateApplication::IsInitialized()) FSlateApplication::Get().ReleaseAllPointerCapture();
}
void UACEFrameRateWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
    FinishDrag();Super::NativeOnMouseCaptureLost(Event);
}
void UACEFrameRateWidget::NativeDestruct()
{
    CancelDrag();Super::NativeDestruct();
}
void UACEFrameRateWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Label.Reset();
}
void UACEFrameRateWidget::ResetSample()
{
    CancelDrag();
    WindowStart=-1.;Frames=0;CounterText=TEXT("FPS --");
    if(Label)Label->SetText(FText::FromString(CounterText));
}
void UACEFrameRateWidget::Sample(double Now)
{
    if(!FMath::IsFinite(Now))return;
    if(WindowStart<0. || Now<WindowStart){WindowStart=Now;Frames=0;return;}
    ++Frames;
    const double Elapsed=Now-WindowStart;
    if(Elapsed<.5)return;
    // Wall-clock intervals include stalls; simulation delta can be clamped.
    CounterText=FString::Printf(TEXT("%.0f FPS  |  %.1f ms"),Frames/Elapsed,Elapsed*1000./Frames);
    if(Label)Label->SetText(FText::FromString(CounterText));
    WindowStart=Now;Frames=0;
}
