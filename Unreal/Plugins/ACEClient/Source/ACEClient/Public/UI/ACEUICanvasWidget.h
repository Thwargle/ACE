#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/ACEUIElement.h"
#include "ACEUICanvasWidget.generated.h"

class UACEUIElementManager;
class UACEUIResourceResolver;
class UACEUIGameplayBinder;
class UACEUICharSelectBinder;
class UACEUICharGenBinder;
class UBorder;
class UCanvasPanel;
class UTexture2D;

/**
 * Last values pushed to one element's UBorder. Slate setters invalidate layout/paint, so the
 * per-tick tree sync only calls them when something actually changed.
 */
struct FACEUIPaintState
{
	TWeakObjectPtr<UTexture2D> Texture;
	int32 X = MIN_int32;
	int32 Y = MIN_int32;
	int32 W = -1;
	int32 H = -1;
	int32 Z = MIN_int32;
	FIntRect ImageRect = FIntRect(0, 0, 0, 0);
	FLinearColor Color = FLinearColor(-1.f, -1.f, -1.f, -1.f);
	ESlateVisibility Visibility = ESlateVisibility::Collapsed;
	/** Tile mode the brush was built with (DAT drawMode 1 tiles the source art). */
	uint8 Tiling = 0xFF;
	/** Until the first push, Visibility does not reflect the widget (UBorder defaults visible). */
	bool bVisibilitySet = false;
	bool bHasBrush = false;
};

/**
 * Hosts retail LayoutDesc UI.
 * Character select: centers the authored 800×600 panel without scaling.
 * Gameplay floaties: edge-anchor on larger viewports (no stretch):
 *   Radar/Panel → top-right; Toolbar → bottom-right; MainChat/Combat/PowerBar → bottom;
 *   Indicators/SideVitals/Examination → authored top (SideVitals packed right of Indicators).
 */
UCLASS()
class ACECLIENT_API UACEUICanvasWidget : public UUserWidget
{
	GENERATED_BODY()
	friend class FACERetailScreenTest;
	friend class FACEUIInteractionParityTest;

public:
	void InitializeCanvas(UACEUIElementManager* InManager);
	void SetResourceResolver(UACEUIResourceResolver* InResolver);
	void SetGameplayBinder(UACEUIGameplayBinder* InBinder);
	void SetCharSelectBinder(UACEUICharSelectBinder* InBinder);
	void SetCharGenBinder(UACEUICharGenBinder* InBinder) { CharGenBinder=InBinder; SetIsFocusable(InBinder != nullptr); }
	UACEUICharGenBinder* GetCharGenBinder() const { return CharGenBinder; }
	virtual FReply NativeOnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event) override;

	UACEUIElementManager* GetManager() const { return Manager; }
	void ResetPointerOwnership() { PressedPointer = PressedUser = INDEX_NONE; }
	void CancelPointerGestures();
	void SetVRPointerFeedback(FVector2D Point, bool Pressed);
	void ClearVRPointerFeedback() { bVRPointerVisible = false; }
	const FString& GetVRPointerLabel() const { return VRPointerLabel; }
	const FString& GetVRPointerHint() const { return VRPointerHint; }
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool Enabled) const override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
	virtual FCursorReply NativeOnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) override;
	UACEUIResourceResolver* GetResourceResolver() const { return ResourceResolver; }
	UCanvasPanel* GetElementLayer() const { return ElementLayer; }
	float GetLastCanvasScale() const { return FMath::Min(LastScaleX, LastScaleY); }
	float GetLastScaleX() const { return LastScaleX; }
	float GetLastScaleY() const { return LastScaleY; }
	FVector2D GetLastScale2D() const { return FVector2D(LastScaleX, LastScaleY); }

	/** Convert widget-local (viewport) coords → layout (800×600) space. */
	FVector2D ViewportToLayout(FVector2D ViewportLocal) const;
	FVector2D LayoutToViewport(FVector2D LayoutPos) const;

	void PlaceWidgetAtElement(UWidget* Widget, const TSharedPtr<FACEUIElement>& Element, int32 ZOrder,
		const FMargin& Inset = FMargin(0.f));
	/** Keep generated content in its retail window's paint order. Null owner is a popup/drag layer. */
	void SetOverlayOrder(UWidget* Widget, const TSharedPtr<FACEUIElement>& OwnerElement, int32 LocalOrder);
	bool IsWidgetExposedAt(const UWidget* Widget, FVector2D Absolute) const;
	bool IsElementExposedAt(const TSharedPtr<FACEUIElement>& Element, FVector2D CanvasLocal) const;

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	int32 PressedPointer = INDEX_NONE;
	bool bVRPointerVisible = false, bVRPointerDown = false;
	FBox2D VRPointerBounds;
	FString VRPointerLabel, VRPointerHint;
	int32 VRPointerGuid = 0;
	double VRClickFlashUntil = 0;
	int32 PressedUser = INDEX_NONE;
	UPROPERTY()
	TObjectPtr<UACEUIElementManager> Manager;

	UPROPERTY()
	TObjectPtr<UACEUIResourceResolver> ResourceResolver;

	UPROPERTY()
	TObjectPtr<UACEUIGameplayBinder> GameplayBinder;

	UPROPERTY()
	TObjectPtr<UACEUICharSelectBinder> CharSelectBinder;
	UPROPERTY() TObjectPtr<UACEUICharGenBinder> CharGenBinder;

	UPROPERTY()
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY() TObjectPtr<UBorder> CharacterBackdrop;

	UPROPERTY()
	TObjectPtr<UCanvasPanel> ElementLayer;

	UPROPERTY() TMap<uint32, TObjectPtr<UBorder>> ImageWidgets;
	/** Hit-testable window bounds below its own content, above all lower windows. */
	UPROPERTY() TMap<uint32, TObjectPtr<UBorder>> WindowInputShields;
	void SyncWindowInputShields();
	/** Parallel to ImageWidgets — see FACEUIPaintState. */
	TMap<uint32, FACEUIPaintState> PaintStates;
	/** Reused across ticks so the sync doesn't reallocate a set every frame. */
	TSet<uint32> UsedIdsScratch;
	struct FOverlayOrder
	{
		TWeakPtr<FACEUIElement> Owner;
		int32 LocalOrder = 0;
		bool bGlobal = false;
	};
	TMap<TWeakObjectPtr<UWidget>, FOverlayOrder> OverlayOrders;
	void RefreshOverlayOrder();

	float LastScaleX = 1.f;
	float LastScaleY = 1.f;

	/** Push geometry / brush / visibility to a border, skipping unchanged Slate setters. */
	void ApplyPaintState(UBorder* Border, uint32 InstanceId, UTexture2D* Texture,
		const FLinearColor& Color, int32 X, int32 Y, int32 W, int32 H, int32 Z,
		ESlateVisibility InVisibility, ESlateBrushTileType::Type TileType = ESlateBrushTileType::NoTile,
		FVector2D BrushImageSize = FVector2D::ZeroVector, FIntRect ImageRect = FIntRect(0, 0, 0, 0));
	void SetCachedVisibility(UBorder* Border, uint32 InstanceId, ESlateVisibility InVisibility);

	void UpdateCanvasLayout();
	void SyncElementWidgets();
	void SyncElementRecursive(const TSharedPtr<FACEUIElement>& Element,
		int32 ClipMaxWidth, int32 ClipMaxHeight,
		int32 ClipX0, int32 ClipY0, int32 ClipX1, int32 ClipY1,
		int32& InOutZOrder, TSet<uint32>& UsedIds);
};
