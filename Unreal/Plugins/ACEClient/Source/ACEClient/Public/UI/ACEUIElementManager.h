#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/ACEUITypes.h"
#include "UI/ACEUIElement.h"
#include "ACEUIElementManager.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FACEUIElementHit, TSharedPtr<FACEUIElement>);

/**
 * Retail UIElementManager singleton analogue.
 * Owns root element trees, 800x600 virtual canvas mapping, hit-test, focus, and capture.
 * LayoutDesc inflation and gm*UI factories are registered later — no retail panels here yet.
 */
UCLASS()
class ACECLIENT_API UACEUIElementManager : public UObject
{
	GENERATED_BODY()

public:
	void Initialize();
	void Shutdown();

	int32 GetCanvasWidth() const { return ACEUI::ReferenceWidth; }
	int32 GetCanvasHeight() const { return ACEUI::ReferenceHeight; }

	/** Map viewport pixel → layout space (accounts for char-select uniform scale). */
	bool ViewportToCanvas(FVector2D ViewportPos, FVector2D ViewportSize, int32& OutCanvasX, int32& OutCanvasY) const;
	/** Full viewport rect (UI is not letterboxed). */
	FIntRect GetCanvasViewportRect(FVector2D ViewportSize) const;
	/**
	 * CharacterManagement: uniform fit of 800×600 into the viewport (≥1).
	 * Gameplay: (1,1) — floaties edge-anchor instead of stretching.
	 */
	FVector2D GetCanvasScale(FVector2D ViewportSize) const;

	/**
	 * Reposition classic_gameplay floaty roots toward screen edges without scaling.
	 * Corner anchors (not midpoint): Radar/Panel top-right; Toolbar bottom-right;
	 * MainChat/Combat/Env/PowerBar bottom; left/top chrome keep authored coords.
	 */
	void ApplyEdgeAnchoredLayout(int32 ViewportWidth, int32 ViewportHeight);

	TSharedPtr<FACEUIElement> GetSyntheticRoot() const { return SyntheticRoot; }
	TSharedPtr<FACEUIElement> GetFocusElement() const { return FocusElement; }
	TSharedPtr<FACEUIElement> GetActiveElement() const { return ActiveElement; }
	TSharedPtr<FACEUIElement> GetCaptureElement() const { return CaptureElement; }
	TSharedPtr<FACEUIElement> GetHoverElement() const { return HoverElement; }

	void SetFocusElement(const TSharedPtr<FACEUIElement>& Element);
	void SetActiveElement(const TSharedPtr<FACEUIElement>& Element);

	void ClearRoots();
	void AddRoot(const TSharedPtr<FACEUIElement>& Root);

	/** Depth-first search by ElementName (exact match). Prefers visible ancestors when duplicates exist. */
	TSharedPtr<FACEUIElement> FindElementByName(const FString& ElementName) const;
	/** Batch the HUD's many name queries; visibility is still evaluated live. */
	void BeginNameLookupPass();
	void EndNameLookupPass();
	void InvalidateNameLookupIndex();
	/** Find ElementName only under an ancestor with AncestorName (e.g. MainChat → ChatLog). */
	TSharedPtr<FACEUIElement> FindElementUnder(const FString& AncestorName, const FString& ElementName) const;
	/** Show/hide a named floaty (e.g. RootGameplay_FloatyPanel_Field). */
	bool SetElementVisibleByName(const FString& ElementName, bool bVisible);

	/** Global floaty lock (retail LockUI). When true, frames use *_Locked art and drag is disabled. */
	bool IsUiLocked() const { return bUiLocked; }
	void SetUiLocked(bool bLocked);
	void ToggleUiLocked() { SetUiLocked(!bUiLocked); }

	/** Apply locked/unlocked chrome visibility under all floaties. */
	void SyncLockedChromeVisibility();

	TSharedPtr<FACEUIElement> HitTestCanvas(int32 CanvasX, int32 CanvasY) const;
	/** Top retail window at a point, including its non-interactive background. */
	TSharedPtr<FACEUIElement> FindWindowAtCanvas(int32 CanvasX, int32 CanvasY) const;

	/** Raise a floaty (or the floaty containing Element) above sibling floaties. */
	void BringFloatyToFront(const TSharedPtr<FACEUIElement>& Element);

	/** Mouse in viewport space; converts to canvas then routes like retail. */
	void NotifyMouseMove(FVector2D ViewportPos, FVector2D ViewportSize);
	void NotifyMouseDown(FVector2D ViewportPos, FVector2D ViewportSize, FKey Button);
	void NotifyMouseUp(FVector2D ViewportPos, FVector2D ViewportSize, FKey Button, bool bRequireSameTarget = false);
	/** Drop capture without activating the button that was pressed. */
	void CancelPointerCapture();

	/** True if the last press-drag moved a floaty (skip click activation). */
	bool DidConsumeDragClick() const { return bDragMoved; }

	static TSharedPtr<FACEUIElement> FindFloatyRoot(const TSharedPtr<FACEUIElement>& Element);
	static bool IsFloatyDragHandle(const TSharedPtr<FACEUIElement>& Element);
	static bool IsFloatyResizeHandle(const TSharedPtr<FACEUIElement>& Element);
	/** Relayout frame borders / PanelPages / chat log after UserResizeH changes. */
	static void ApplyFloatyResizeLayout(const TSharedPtr<FACEUIElement>& Floaty);

	/** Persist / restore floaty UserDrag + UserResizeH (+ lock) in GameUserSettings.ini. */
	void SaveFloatyLayout() const;
	void LoadFloatyLayout();

	/** True if canvas coords hit any visible interactive (or solid floaty) UI. */
	bool IsCanvasOverUI(int32 CanvasX, int32 CanvasY) const;

	using FElementFactory = TFunction<TSharedPtr<FACEUIElement>(uint32 /*ElementId*/, uint32 /*Type*/)>;
	void RegisterElementFactory(uint32 Type, FElementFactory Factory);
	TSharedPtr<FACEUIElement> CreateElementByType(uint32 Type, uint32 ElementId, const FString& DebugName = FString());

	FACEUIElementHit OnHoverChanged;
	/** Fired on successful left-click (mouse up over an element after mouse down capture). */
	FACEUIElementHit OnElementActivated;

private:
	mutable TMap<FString, TArray<TSharedPtr<FACEUIElement>>> NameLookupIndex;
	mutable bool bNameLookupIndexValid = false;
	mutable uint64 NameLookupRevision = 0;
	int32 NameLookupPassDepth = 0;
	void BuildNameLookupIndex() const;
	TSharedPtr<FACEUIElement> SyntheticRoot;
	TArray<TSharedPtr<FACEUIElement>> Roots;
	TSharedPtr<FACEUIElement> FocusElement;
	TSharedPtr<FACEUIElement> ActiveElement;
	TSharedPtr<FACEUIElement> CaptureElement;
	TSharedPtr<FACEUIElement> HoverElement;
	TMap<uint32, FElementFactory> Factories;
	int32 CaptureRefCount = 0;
	bool bUiLocked = true;
	uint32 NextFloatyZ = 1000;

	TSharedPtr<FACEUIElement> DragFloaty;
	int32 DragGrabCanvasX = 0;
	int32 DragGrabCanvasY = 0;
	int32 DragStartUserX = 0;
	int32 DragStartUserY = 0;
	bool bDragMoved = false;

	/** Bottom-border height resize (separate from move-drag). */
	TSharedPtr<FACEUIElement> ResizeFloaty;
	int32 ResizeGrabCanvasY = 0;
	int32 ResizeStartUserH = 0;
	int32 ResizeStartUserDragY = 0;
	bool bResizeBottomAnchored = false;
	int32 ResizeGrabCanvasX = 0, ResizeStartUserW = 0, ResizeStartUserDragX = 0;
	bool bResizeChat = false, bResizeLeft = false, bResizeRight = false, bResizeTop = false, bResizeBottom = false;
};
