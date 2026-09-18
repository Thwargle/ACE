#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "ACEVRRetailSurface.generated.h"

class UACEUICanvasWidget;
class UWidgetComponent;
class UACEVRComponent;
class UACERetailTextBlock;
struct FACEUIElement;

/** A view of an existing retail window, retaining its artwork and input behavior. */
UCLASS()
class ACECLIENT_API UACEVRRetailSurface : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetSource(UWidgetComponent* Component, UACEUICanvasWidget* Canvas, FName Window);
	bool RefreshSurface();
	void SetStatusSource(UACEVRComponent* Rig) { StatusSource = Rig; }
	void SetVitalsSource(UACEVRComponent* Rig) { VitalsSource = Rig; }
	FVector2D ToCanvas(FVector2D Local) const { return SourceRect.Min + Local; }
	FVector2D GetSurfaceSize() const { return SourceRect.GetSize() + FVector2D(VitalsSource.IsValid() ? 44 : 0, StatusSource.IsValid() ? 48 : VitalsSource.IsValid() ? 68 : 0); }
	virtual FReply NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	virtual FReply NativeOnMouseMove(const FGeometry&, const FPointerEvent&) override;
	virtual FReply NativeOnMouseWheel(const FGeometry&, const FPointerEvent&) override;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
private:
	FPointerEvent Translate(const FGeometry&, const FPointerEvent&) const;
	UPROPERTY(Transient) TObjectPtr<UWidgetComponent> Source;
	UPROPERTY(Transient) TObjectPtr<UACEUICanvasWidget> Retail;
	UPROPERTY(Transient) TObjectPtr<UACERetailTextBlock> RecoveryLabel;
	TSharedPtr<FACEUIElement> RecoveryElement;
	FName WindowName;
	FBox2D SourceRect = FBox2D(FVector2D::ZeroVector, FVector2D(1, 1));
	FSlateBrush Brush;
	FSlateBrush StanceBrush;
	FVector2D CachedRenderSize = FVector2D::ZeroVector;
	TWeakObjectPtr<UACEVRComponent> StatusSource;
	TWeakObjectPtr<UACEVRComponent> VitalsSource;
};
