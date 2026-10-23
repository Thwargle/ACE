#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ACEFrameRateWidget.generated.h"

/** Application-frame counter; desktop placement follows the shared HUD lock. */
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
    void UpdatePresentation(class UACEUIElementManager* Manager, bool Desktop, FVector2D ViewportSize, bool ForceLayout=false);
    bool CanMove() const;
protected:
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    virtual void NativeDestruct() override;
private:
    friend class FACEFrameRateOverlayTest;
    void Place(bool Force=false);
    void FinishDrag();
    void CancelDrag();
    TWeakObjectPtr<class UACEUIElementManager> UIManager;
    FVector2D Position=FVector2D(16,64), PlacedPosition=FVector2D(-1,-1), ViewSize=FVector2D::ZeroVector;
    FVector2D DragStart, DragPosition;
    float DragScale=1.f;
    bool bDesktop=false, bLoadedPosition=false, bDragging=false, bMoved=false;
    double WindowStart=-1.;
    int32 Frames=0;
    FString CounterText=TEXT("FPS --");
    TSharedPtr<class STextBlock> Label;
};
