#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SConstraintCanvas.h"

class UACEPluginSubsystem;
class SVerticalBox;

/** Nonmodal desktop plugin launcher and independent movable plugin windows. */
class SACEPluginDesktop : public SCompoundWidget
{
    friend class FACEPluginHostTest;
public:
    SLATE_BEGIN_ARGS(SACEPluginDesktop) {} SLATE_ARGUMENT(UACEPluginSubsystem*, Host) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void Refresh();
    void Toggle(const FString& Id);
    bool IsOpen(const FString& Id) const;
    void SaveOverlayLayout();
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override;
private:
    struct FWindow
    {
        TSharedPtr<SWidget> Widget;
        SConstraintCanvas::FSlot* Slot = nullptr;
        FVector2D Position, Size;
        bool Open = true;
    };
    TWeakObjectPtr<UACEPluginSubsystem> Host;
    TSharedPtr<SConstraintCanvas> Canvas;
    TSharedPtr<SVerticalBox> Buttons;
    TMap<FString, FWindow> Windows;
    FString Signature;
    FVector2D ViewSize = FVector2D::ZeroVector;
    int32 TopOrder = 1;
    void Layout(FWindow& Window);
    void Move(const FString& Id, FVector2D Delta, bool Save);
    void Resize(const FString& Id, FVector2D Delta, bool Save);
    void Raise(const FString& Id);
    void Hide(const FString& Id);
};
