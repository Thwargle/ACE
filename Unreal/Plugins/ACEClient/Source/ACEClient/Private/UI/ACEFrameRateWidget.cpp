#include "UI/ACEFrameRateWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"

TSharedRef<SWidget> UACEFrameRateWidget::RebuildWidget()
{
    static const FSlateRoundedBoxBrush Background(FLinearColor(.015f,.018f,.024f,.9f),6.f);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    return SNew(SBorder).BorderImage(&Background).Padding(FMargin(12,6)).Visibility(EVisibility::HitTestInvisible)
        [SAssignNew(Label,STextBlock).Text(FText::FromString(CounterText))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold",24)).ColorAndOpacity(FLinearColor(.97f,.92f,.76f))];
}
void UACEFrameRateWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Label.Reset();
}
void UACEFrameRateWidget::ResetSample()
{
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
