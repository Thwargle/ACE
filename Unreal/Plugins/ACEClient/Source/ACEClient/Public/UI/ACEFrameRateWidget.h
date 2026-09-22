#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ACEFrameRateWidget.generated.h"

/** Small, noninteractive application-frame counter shared by desktop and VR. */
UCLASS()
class ACECLIENT_API UACEFrameRateWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    void Sample(double Now);
    void ResetSample();
    const FString& GetCounterText() const { return CounterText; }
private:
    double WindowStart=-1.;
    int32 Frames=0;
    FString CounterText=TEXT("FPS --");
    TSharedPtr<class STextBlock> Label;
};
