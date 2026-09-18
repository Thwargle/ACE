#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "UI/ACEUITypes.h"
#include "ACETypes.h"
#include "ACEGameHUDWidget.generated.h"

class UCanvasPanel;
class UBorder;
class UButton;
class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UEditableTextBox;
class UImage;
class UACEClientSubsystem;
class UACEDatSubsystem;
class UTexture2D;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class AActor;
class UACECharacterAppearanceComponent;

/** Right-hand panel pages, toggled by the toolbar buttons (retail classic_panel PanelPages). */
UENUM()
enum class EACEHudPanelPage : uint8
{
	None,
	Inventory,
	Character,
	Spellbook,
	Map,
	Social,
	Quests,
	Options,
};

/** How retail UI chrome textures fill an element (status strip tiles; buttons stretch). */
UENUM()
enum class EACEHudArtTile : uint8
{
	None,
	Horizontal,
	Vertical,
	Both,
};

/**
 * In-world gameplay HUD recreating the retail classic (pre-ToD) screen from the DAT
 * LayoutDescs, textured with client_portal.dat UI art. Art is rebound once DATs finish
 * loading — RebuildWidget often runs before portal.dat is open.
 * Chat/toolbar/panel are floaty windows: unpin to drag anywhere on the full viewport.
 */
UCLASS()
class ACECLIENT_API UACEGameHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "ACE|UI")
	void FocusChatEntry();

	UFUNCTION(BlueprintPure, Category = "ACE|UI")
	bool IsChatEntryFocused() const;

	/** Number-key behavior: cast in combat, use the inventory shortcut in peace mode. */
	void ActivateHotbarSlot(int32 SlotIndex);

private:
	struct FHudPiece
	{
		TWeakObjectPtr<UWidget> Widget;
		FVector4 VirtualRect = FVector4(0, 0, 0, 0);
		int32 WindowIndex = INDEX_NONE;
		/** Place at true viewport top-left (ignores letterbox centering). */
		bool bPinScreenTopLeft = false;
		/** Grows with the user-resizable right panel (background chrome / scroll areas). */
		bool bPanelStretchHeight = false;
		/** Rides the bottom edge of the resizable right panel (resize grip, footer rows). */
		bool bPanelAnchorBottom = false;
		/** Grows with the user-resizable chat history area. */
		bool bChatStretchHeight = false;
		/** Shifts up when chat grows taller (entry row stays put). */
		bool bChatShiftUp = false;
	};

	struct FHudWindow
	{
		FName PersistKey;
		FVector2D DefaultTopLeft = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		FVector2D Offset = FVector2D::ZeroVector;
		TWeakObjectPtr<UBorder> DragSurface;
	};

	/** Deferred DAT art binding — applied when portal.dat becomes ready. */
	struct FHudArtBinding
	{
		TWeakObjectPtr<UWidget> Widget;
		uint32 NormalDid = 0;
		uint32 PressedDid = 0;
		EACEHudArtTile Tile = EACEHudArtTile::None;
		FLinearColor Fallback = FLinearColor::Black;
		bool bIsButton = false;
		bool bApplied = false;
	};

	static constexpr int32 WindowVitals = 0;
	static constexpr int32 WindowRadar = 1;
	static constexpr int32 WindowPanel = 2;
	static constexpr int32 WindowToolbar = 3;
	static constexpr int32 WindowChat = 4;
	static constexpr int32 WindowInspect = 5;
	static constexpr int32 WindowCombat = 6;
	static constexpr int32 WindowCount = 7;

	/** What a manual (retail-style) icon drag is carrying. */
	enum class EACEDragKind : uint8 { None, Item, Spell };

	void BuildLayout();
	void BuildIndicators();
	void BuildVitalsWindow();
	void BuildRadarWindow();
	void BuildRightPanel();
	void BuildToolbar();
	void BuildChat();
	void BuildCombatHotbar();
	void RefreshCombatHotbar();
	void SetCombatHotbarVisible(bool bVisible);
	void UpdateInventoryButtonVisual();
	void RefreshSpellbookUI();
	void CastSpellFromBar(int32 SlotIndex);
	void SelectCombatSpellSlot(int32 SlotIndex);
	void HandleCombatSpellSlotClicked(int32 SlotIndex);

	UTexture2D* UiTex(uint32 TextureDid);
	FSlateBrush MakeFallbackBrush(const FLinearColor& Color, float Radius = 0.01f) const;
	FSlateBrush MakeTexBrush(UTexture2D* Texture, EACEHudArtTile Tile) const;
	void BindArt(UWidget* Widget, uint32 NormalDid, uint32 PressedDid, EACEHudArtTile Tile,
		const FLinearColor& Fallback, bool bIsButton);
	void EnsureArtApplied();
	bool ApplyArtBinding(FHudArtBinding& Binding);

	UBorder* AddPanel(int32 X, int32 Y, int32 W, int32 H, const FLinearColor& Color);
	UBorder* AddArt(int32 X, int32 Y, int32 W, int32 H, uint32 TextureDid, const FLinearColor& Fallback,
		EACEHudArtTile Tile = EACEHudArtTile::None);
	UButton* AddToolButton(int32 X, int32 Y, int32 W, int32 H, const FString& Text, int32 FontSize = 9,
		uint32 NormalDid = 0, uint32 PressedDid = 0);
	UTextBlock* AddCenteredLabel(UWidget* Parent, const FString& Text, const FLinearColor& Color, int32 FontSize);
	UTextBlock* AddCanvasLabel(int32 X, int32 Y, int32 W, int32 H, const FString& Text, const FLinearColor& Color, int32 FontSize, ETextJustify::Type Justify);
	void Track(UWidget* Widget, int32 X, int32 Y, int32 W, int32 H);
	void TrackPinnedTopLeft(UWidget* Widget, int32 X, int32 Y, int32 W, int32 H);
	void MarkLastPiece(bool bStretchHeight, bool bAnchorBottom);
	void MarkLastChatPiece(bool bStretchHeight, bool bShiftUp);
	void ApplyChatExtraHeight();
	void ApplyLetterboxLayout();
	void RefreshVitals();
	void RefreshRadar();
	void RefreshPanelPage();
	void EnsureWorldMapWidgets();
	void RefreshWorldMap();
	void AppendChatLine(const FString& Line, const FLinearColor& Color);
	void SetPanelPage(EACEHudPanelPage Page);
	void ShowIndicatorPanel(int32 IndicatorIndex);
	void SendChatText();
	void CycleChatFilter();
	void UpdateChatTargetButton();
	bool PassesChatFilter(int32 ChatType) const;
	void ToggleUiLock();
	void LoadWindowLayout();
	void SaveWindowLayout() const;
	bool CanDragWindows() const { return !bUiLocked; }
	FVector2D ScreenToVirtual(const FVector2D& ScreenPos) const;
	void GetVirtualScreenBounds(FVector2D& OutMin, FVector2D& OutMax) const;

	UFUNCTION() FEventReply HandleWindowMouseDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply HandleWindowMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() FEventReply HandleWindowMouseUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION() void HandleChatMessage(const FString& Text, const FString& Sender, int32 ChatType);
	UFUNCTION() void HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION() void OnSendClicked();
	UFUNCTION() void OnChatTargetClicked();
	UFUNCTION() void OnInventoryClicked();
	UFUNCTION() void OnCharacterClicked();
	UFUNCTION() void OnSpellbookClicked();
	UFUNCTION() void OnMapClicked();
	UFUNCTION() void OnSocialClicked();
	UFUNCTION() void OnQuestsClicked();
	UFUNCTION() void OnOptionsClicked();
	UFUNCTION() void OnCombatModeClicked();
	UFUNCTION() void OnHelpClicked();
	UFUNCTION() void OnIndicatorPortalStorm();
	UFUNCTION() void OnIndicatorMiniGame();
	UFUNCTION() void OnIndicatorVitae();
	UFUNCTION() void OnIndicatorPositive();
	UFUNCTION() void OnIndicatorNegative();
	UFUNCTION() void OnIndicatorBurden();
	UFUNCTION() void OnIndicatorLink();
	UFUNCTION() void OnLogoffClicked();
	UFUNCTION() void OnUiLockClicked();
	UFUNCTION() void OnExamineClicked();
	UFUNCTION() void OnUseSelectedClicked();
	UFUNCTION() void OnCharTabAttributes();
	UFUNCTION() void OnCharTabSkills();
	UFUNCTION() void OnCharTabTitles();
	UFUNCTION() void HandleSelectionChanged(const FACESelectedObject& Selection);
	UFUNCTION() void HandleAppraisal(const FACEAppraisalInfo& Appraisal);
	UFUNCTION() void OnCombatSpell0();
	UFUNCTION() void OnCombatSpell1();
	UFUNCTION() void OnCombatSpell2();
	UFUNCTION() void OnCombatSpell3();
	UFUNCTION() void OnCombatSpell4();
	UFUNCTION() void OnCombatSpell5();
	UFUNCTION() void OnCombatSpell6();
	UFUNCTION() void OnCombatSpell7();
	UFUNCTION() void OnCombatSpell8();
	UFUNCTION() void OnCombatSpell9();
	UFUNCTION() void OnCombatSpell10();
	UFUNCTION() void OnCombatSpell11();
	UFUNCTION() void OnSpellTab0();
	UFUNCTION() void OnSpellTab1();
	UFUNCTION() void OnSpellTab2();
	UFUNCTION() void OnSpellTab3();
	UFUNCTION() void OnSpellTab4();
	UFUNCTION() void OnSpellTab5();
	UFUNCTION() void OnSpellTab6();
	UFUNCTION() void OnSpellTab7();
	UFUNCTION() void OnCastSpellClicked();
	UFUNCTION() void OnCloseInspectClicked();

	int32 ResolveEquippedCombatMode() const;
	void ApplyCombatModeVisual(int32 Mode);
	void RefreshSelectionUI();
	void ApplyPreferredStance(int32 CombatMode);
	void UpdateUiLockVisual();
	void BuildInventoryWidgets(int32 PanelOX, int32 PanelOY);
	void RefreshInventoryUI();
	void EnsureInventorySlotCount(int32 SlotCount);
	void EnsurePackTabCount(int32 TabCount);
	void SetInventorySlotIcon(UBorder* IconCell, int32 IconDid, const FLinearColor& EmptyColor);
	void OnInventoryPackClicked(int32 PackIndex);
	UFUNCTION() FEventReply HandlePackTabMouseDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	void EnsureAttributeWidgets();
	void RefreshAttributePanel();
	void SetAttributePanelVisible(bool bVisible);
	void SelectAttributeRow(int32 RowIndex);
	void RaiseSelectedAttribute(int32 Multiplier);
	void EnsureInspectPreview();
	void UpdateInspectPreview(int32 ObjectGuid);
	void ClearInspectPreview();
	/** Shared scene-capture rig for 3D previews (inspect window, inventory paper doll). */
	bool EnsurePreviewRig(TObjectPtr<UTextureRenderTarget2D>& RenderTarget, TObjectPtr<AActor>& PreviewActor,
		TObjectPtr<USceneCaptureComponent2D>& Capture, int32 Width, int32 Height, const FVector& SpawnLocation);
	bool ApplyPreviewObject(AActor* PreviewActor, USceneCaptureComponent2D* Capture, const FACEWorldObject& Obj, float YawDegrees);
	void UpdatePaperDollPreview();
	void RefreshSkillsUI();
	void SelectSkillRow(int32 SkillIndex);
	void RaiseSelectedSkill(int32 Multiplier);
	void SetRaiseFooterVisible(bool bVisible);
	void RefreshRaiseFooter();
	bool IsLeftMouseButtonDown() const;
	void TickPointerCapture(float /*InDeltaTime*/);
	/** Manual drag & drop of item/spell icons (retail picks icons up and drops on targets). */
	void BeginPendingDrag(EACEDragKind Kind, int32 PayloadId, int32 IconDid, const FVector2D& ScreenPos, int32 SourcePackGuid, int32 SourceSlotIndex);
	void UpdateDragVisual(const FVector2D& ScreenPos);
	void FinishDrag(const FVector2D& ScreenPos);
	void CancelDrag();
	void ReleasePointerCapture();
	bool TryStartCellDrag(const FVector2D& ScreenPos);
	static bool WidgetContains(const UWidget* Widget, const FVector2D& ScreenPos);
	UFUNCTION() void OnPackTab0();
	UFUNCTION() void OnPackTab1();
	UFUNCTION() void OnPackTab2();
	UFUNCTION() void OnPackTab3();
	UFUNCTION() void OnPackTab4();
	UFUNCTION() void OnPackTab5();
	UFUNCTION() void OnPackTab6();
	UFUNCTION() void OnPackTab7();
	// Pack tabs beyond 0–7 use HandlePackTabMouseDown (dynamic count).
	UFUNCTION() void OnAttrRow0();
	UFUNCTION() void OnAttrRow1();
	UFUNCTION() void OnAttrRow2();
	UFUNCTION() void OnAttrRow3();
	UFUNCTION() void OnAttrRow4();
	UFUNCTION() void OnAttrRow5();
	UFUNCTION() void OnAttrRow6();
	UFUNCTION() void OnAttrRow7();
	UFUNCTION() void OnAttrRow8();
	UFUNCTION() void OnAttrRaise1();
	UFUNCTION() void OnAttrRaise10();
	UFUNCTION() void OnAttrRaiseAll();
	UFUNCTION() void OnSkillRowClicked();

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UACEClientSubsystem> Client;
	UPROPERTY(Transient) TObjectPtr<UACEDatSubsystem> Dat;

	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> HealthFillPieces;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> StaminaFillPieces;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> ManaFillPieces;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HealthLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StaminaLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ManaLabel;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> RadarCoordsText;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> RadarBlips;
	UPROPERTY(Transient) TObjectPtr<UButton> UiLockButton;

	UPROPERTY(Transient) TObjectPtr<UBorder> PanelPageRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PanelPageTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PanelPageBody;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PanelStatsLabels;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PanelStatsValues;
	UPROPERTY(Transient) TObjectPtr<UButton> CharTabAttributes;
	UPROPERTY(Transient) TObjectPtr<UButton> CharTabSkills;
	UPROPERTY(Transient) TObjectPtr<UButton> CharTabTitles;

	/** Retail classic_attribute / StatManagement_Template widgets. */
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrPanelRoot;
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrHeaderBg;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrHeaderName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrHeaderTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrHeaderTotalXpLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrHeaderTotalXpValue;
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrHeaderDivider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrLevelLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrLevelValue;
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrXpToLevelMeterBack;
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrXpToLevelMeterFill;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrXpToLevelLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrXpToLevelValue;
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrDividerTop;
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrDividerBottom;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrUnassignedXpText;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> AttrRowButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> AttrRowIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> AttrRowNames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> AttrRowValues;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrSelectedName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrSelectedHint;
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrRaiseMeterBack;
	UPROPERTY(Transient) TObjectPtr<UBorder> AttrRaiseMeterFill;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AttrRaiseXpText;
	UPROPERTY(Transient) TObjectPtr<UButton> AttrRaise1Button;
	UPROPERTY(Transient) TObjectPtr<UButton> AttrRaise10Button;
	UPROPERTY(Transient) TObjectPtr<UButton> AttrRaiseAllButton;
	int32 SelectedAttributeRow = 0;

	UPROPERTY(Transient) TObjectPtr<UScrollBox> ChatScroll;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> ChatEntry;
	UPROPERTY(Transient) TObjectPtr<UButton> ChatTargetButton;
	/** 0=All, 1=Speech, 2=Combat, 3=System — retail chat filter button next to the entry. */
	int32 ChatFilterMode = 0;
	UPROPERTY(Transient) TObjectPtr<UBorder> ChatResizeGrip;
	UPROPERTY(Transient) TObjectPtr<UButton> CombatModeButton;
	UPROPERTY(Transient) TObjectPtr<UButton> InventoryButton;
	UPROPERTY(Transient) TObjectPtr<UBorder> SelectedFieldArt;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectedNameLabel;
	UPROPERTY(Transient) TObjectPtr<UBorder> SelectedHealthFill;
	UPROPERTY(Transient) TObjectPtr<UBorder> InspectPanel;
	UPROPERTY(Transient) TObjectPtr<UBorder> InspectEdgeChrome;
	UPROPERTY(Transient) TObjectPtr<UBorder> InspectHeaderArt;
	UPROPERTY(Transient) TObjectPtr<UBorder> InspectModelFrame;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InspectTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InspectBody;
	UPROPERTY(Transient) TObjectPtr<UImage> InspectModelImage;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> InspectRenderTarget;
	UPROPERTY(Transient) TObjectPtr<AActor> InspectPreviewActor;
	UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> InspectCapture;
	int32 InspectPreviewGuid = 0;

	UPROPERTY(Transient) TObjectPtr<UBorder> CombatHotbarRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> CombatSpellButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> CombatSpellIconCells;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> CombatSpellSelectedOverlays;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CombatSpellSlotNumbers;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> CombatSpellTabButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CombatSpellNameLabel;
	UPROPERTY(Transient) TObjectPtr<UButton> CastSpellButton;
	int32 SelectedCombatSpellSlot = 0;
	int32 LastSpellSlotClickIndex = INDEX_NONE;
	double LastSpellSlotClickTime = 0.0;

	UPROPERTY(Transient) TObjectPtr<UBorder> InventoryRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InventoryHint;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> PaperDollSlots;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> InventoryIconSlots;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> PackTabButtons;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> InventoryGridScroll;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> InventoryGridRows;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> PackTabsScroll;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> PackTabsList;
	TArray<int32> PackTabGuids;
	int32 SelectedPackGuid = 0;
	int32 BuiltInventorySlotCount = 0;
	int32 BuiltPackTabCount = 0;
	/** Paper doll 3D character preview (scene capture of the local player's appearance). */
	UPROPERTY(Transient) TObjectPtr<UBorder> PaperDollModelFrame;
	UPROPERTY(Transient) TObjectPtr<UImage> PaperDollModelImage;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> PaperDollRenderTarget;
	UPROPERTY(Transient) TObjectPtr<AActor> PaperDollPreviewActor;
	UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> PaperDollCapture;
	int32 PaperDollAppliedGuid = 0;
	uint32 PaperDollAppliedHash = 0;
	/** Guids behind the visible inventory cells / doll slots (for drag & drop). */
	TArray<int32> InventoryCellGuids;
	TArray<int32> PaperDollGuids;

	/** Skills tab rows live in a scrollbox (panel is user-resizable). */
	UPROPERTY(Transient) TObjectPtr<UScrollBox> SkillsScroll;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> SpellbookScroll;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> SpellRowBorders;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SkillRowButtons;
	TArray<int32> SpellRowIds;
	TArray<int32> SpellRowIconDids;
	TArray<int32> SkillRowIds;
	int32 SelectedSkillId = 0;
	uint32 SkillsUiSignature = 0;
	uint32 SpellbookUiSignature = 0;

	/** Bottom-edge resize grip for the right panel. */
	UPROPERTY(Transient) TObjectPtr<UBorder> PanelResizeGrip;
	float PanelExtraHeight = 0.f;
	bool bResizingPanel = false;
	/** Extra chat history height (grows upward; entry row stays fixed). */
	float ChatExtraHeight = 0.f;
	bool bResizingChat = false;

	/** Manual drag & drop state (floating icon follows the mouse). */
	EACEDragKind DragKind = EACEDragKind::None;
	int32 DragPayloadId = 0;
	int32 DragIconDid = 0;
	int32 DragSourcePackGuid = 0;
	int32 DragSourceSlotIndex = INDEX_NONE;
	FVector2D DragStartScreen = FVector2D::ZeroVector;
	bool bDragActive = false;
	UPROPERTY(Transient) TObjectPtr<UImage> DragIconImage;

	/** classic_map (0x0600127D) + player/town markers inside the right panel. */
	UPROPERTY(Transient) TObjectPtr<UBorder> WorldMapRoot;
	UPROPERTY(Transient) TObjectPtr<UBorder> WorldMapImage;
	UPROPERTY(Transient) TObjectPtr<UBorder> WorldMapPlayerMarker;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> WorldMapTownButtons;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WorldMapHoverLabel;

	FACESelectedObject LastSelection;
	enum class EACECharSheetTab : uint8 { Attributes, Skills, Titles };
	EACECharSheetTab CharSheetTab = EACECharSheetTab::Attributes;

	TArray<FHudPiece> Pieces;
	TArray<FHudWindow> Windows;
	TArray<FHudArtBinding> ArtBindings;
	TArray<FVector4> BlipVirtualRects;

	EACEHudPanelPage CurrentPage = EACEHudPanelPage::None;
	FACEPlayerVitals LastVitals;
	int32 ChatLineCount = 0;
	int32 CombatMode = 1;
	int32 DragWindowIndex = INDEX_NONE;
	FVector2D DragGrabOffset = FVector2D::ZeroVector;
	float LastScale = 1.f;
	FVector2D LastLetterboxOffset = FVector2D::ZeroVector;
	FVector2D LastViewportSize = FVector2D::ZeroVector;
	int32 BuildWindowIndex = INDEX_NONE;
	bool bLayoutBuilt = false;
	bool bChatDelegateBound = false;
	bool bAllArtApplied = false;
	/** Retail lock icon: when true, floaty windows cannot be dragged (positions are saved). */
	bool bUiLocked = true;
};
