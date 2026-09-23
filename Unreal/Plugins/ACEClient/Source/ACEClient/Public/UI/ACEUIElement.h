#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "UI/ACEUITypes.h"

struct FACEUIStateMedia
{
	uint32 ImageFileId = 0;
	uint32 AlphaFileId = 0;
	uint32 DrawMode = 0;
	bool bHasMedia = false;
	bool bPassToChildren = false;
	TOptional<bool> bHidden;
	TOptional<FLinearColor> TextColor;
	TOptional<bool> bTextOutline;
	uint32 FontId = 0;
};

/** Retail instance properties override layout/state properties. Existing gm binders
 * assign visibility directly; preserve that distinction when states change. */
struct FACEUIVisibility
{
	bool Value = true;
	bool bInstanceOverride = false;
	operator bool() const { return Value; }
	FACEUIVisibility& operator=(bool InValue) { Value = InValue; bInstanceOverride = true; return *this; }
	void SetFromState(bool InValue) { if (!bInstanceOverride) Value = InValue; }
};

/**
 * Interactive UI element node — mirrors retail UIElement / UIRegion.
 * Geometry is integer local coords; screen origin accumulates parents + LayoutOffset.
 * Owned exclusively via TSharedPtr by UACEUIElementManager.
 */
struct FACEUIElement : public TSharedFromThis<FACEUIElement>
{
	/** Stable unique id for widget maps (ElementId collides across template instances). */
	uint32 InstanceId = 0;
	uint32 ElementId = 0;
	uint32 Type = ACEUI::ElementType::Field;
	uint32 BaseLayout = 0;
	uint32 BaseElement = 0;
	int32 X = 0;
	int32 Y = 0;
	int32 Width = 0;
	int32 Height = 0;
	int32 MinWidth = 1, MinHeight = 1;
	int32 MaxWidth = MAX_int32, MaxHeight = MAX_int32;
	/**
	 * Retail ElementDesc edge anchors (m_leftEdge..m_bottomEdge), driving
	 * UIElement::UpdateForParentSizeChange reflow on viewport size changes.
	 * Left/Top: 2 = follow parent delta, 3 = center, 4 = proportional; 0/1 = keep.
	 * Right/Bottom: 1 = follow parent delta, 3 = center, 4 = proportional; 0/2 = keep.
	 */
	uint8 LeftEdge = 0;
	uint8 TopEdge = 0;
	uint8 RightEdge = 0;
	uint8 BottomEdge = 0;
	/** Authored size snapshot for edge reflow (-1 until first reflow). */
	int32 LayoutAuthoredW = -1;
	int32 LayoutAuthoredH = -1;
	/** Last size written by edge reflow — size is only re-written when the reflow
	 *  result changes, so binder / user-resize mutations survive steady-state ticks. */
	int32 LastReflowW = -1;
	int32 LastReflowH = -1;
	/**
	 * Extra pixel offset applied when placing this node (edge-anchoring on large viewports).
	 * Authored X/Y stay retail; LayoutOffset = EdgeAnchor + UserDrag.
	 */
	int32 LayoutOffsetX = 0;
	int32 LayoutOffsetY = 0;
	/** Computed from viewport vs 800×600 (right/bottom floaties). */
	int32 EdgeAnchorX = 0;
	int32 EdgeAnchorY = 0;
	/** Player-dragged offset while UI is unlocked. */
	int32 UserDragX = 0;
	int32 UserDragY = 0;
	/**
	 * Extra height from dragging the bottom Resizebar (BottomBorder).
	 * Authored Height is preserved in AuthoredHeight once resize begins.
	 */
	int32 UserResizeH = 0;
	int32 AuthoredHeight = -1;
	int32 UserResizeW = 0;
	int32 AuthoredWidth = -1;
	bool bResizeLeft = false, bResizeRight = false, bResizeTop = false, bResizeBottom = false;

	void RecomputeLayoutOffset()
	{
		LayoutOffsetX = EdgeAnchorX + UserDragX;
		LayoutOffsetY = EdgeAnchorY + UserDragY;
	}

	int32 GetLayoutHeight() const
	{
		const int32 Base = AuthoredHeight >= 0 ? AuthoredHeight : Height;
		return FMath::Max(1, Base + UserResizeH);
	}
	uint32 ZLevel = 0;
	/** Transient: preserves LayoutDesc sibling order across stable Z sorts. */
	int32 SortKey = 0;
	/** Only the cloned VR overhead meter removes its toolbar backing. */
	bool bFloatingHealthArtwork = false;
	uint32 ImageFileId = 0;
	uint32 AlphaFileId = 0;
	uint32 DrawMode = 0;
	uint32 DefaultState = 0;
	uint32 PaintState = 0;
	TMap<uint32, FACEUIStateMedia> States;
	bool bPassStateToChildren = false;
	bool bPaintPassStateToChildren = false;
	bool bRollover = false;
	bool bGhosted = false;
	bool bHighlighted = false;
    bool bUseExplicitState = false;
	bool bDrawAfterChildren = false;
	uint32 FontId = 0;
	FMargin TextMargins = FMargin(0.f);
	uint32 TextHorizontalJustification = 2;
	uint32 TextVerticalJustification = 4;
	bool bHasTextLayout = false;
	int32 FontHeight = 0;
	TOptional<FLinearColor> TextColor;
	FLinearColor TextOutlineColor = FLinearColor::Black;
	bool bTextOutline = false;
	bool bTextOneLine = false;
	bool bDefaultHidden = false;
	const FACEUIStateMedia* ResolvePaintState(bool bCaptured, bool bHovered, bool bFocused)
	{
		uint32 Desired = DefaultState;
		if (TSharedPtr<FACEUIElement> P = Parent.Pin(); !bUseExplicitState && P && P->bPaintPassStateToChildren)
			Desired = P->PaintState;
		if (!bUseExplicitState && (Type == ACEUI::ElementType::Button || bBooleanButton))
		{
			// UIElement_Button::UpdateState_: normal, rollover, pressed; +5 when selected.
			Desired = bHighlighted ? 6 : 1;
			if (bCaptured || (bRollover && bHovered)) Desired = bHighlighted ? 7 : 2;
			if (bCaptured && bHovered) Desired = bHighlighted ? 8 : 3;
			if (bGhosted || !bActivatable) Desired = 13;
		}
		else if (!bUseExplicitState && bFocused) Desired = 4;
		// Composite checkbox definitions omit rollover states 2/7.
		if (!bUseExplicitState && bBooleanButton && !States.Contains(Desired)) Desired = bHighlighted ? 6 : 1;
		const FACEUIStateMedia* State = States.Find(Desired);
		// Some toolbar definitions contain an empty depressed state. Keep the
		// selected artwork visible while the mouse is held, instead of a blank hole.
		if (bBooleanButton && (Desired == 3 || Desired == 8) && State
			&& State->bHasMedia && State->ImageFileId == 0)
		{
			State = States.Find(6);
			if (!State || !State->ImageFileId) State = States.Find(1);
		}
		PaintState = State ? Desired : 0;
		bPaintPassStateToChildren = State ? State->bPassToChildren : bPassStateToChildren;
		bVisible.SetFromState(!(State && State->bHidden.IsSet() ? State->bHidden.GetValue() : bDefaultHidden));
		return State;
	}
	bool IsPaintVisible() const
	{
		return bVisible;
	}
	FACEUIVisibility bVisible;
	bool bActivatable = true;
	bool bBooleanButton = false;
	bool bPanelTab = false;
	bool bAcceptsFocus = false;
	/**
	 * For ElementType::Meter: 0..1 fill amount. Negative = unset (paint full fill art).
	 * Applied by clipping the meter's fill layer (not meter_background).
	 */
	float MeterFillFraction = -1.f;
	/** Optional tint applied when painting this element's image (meters, etc.). */
	FLinearColor ImageTint = FLinearColor::White;
	/** ChatInterface background opacity; text remains independently readable. */
	float ImageOpacity = 1.f;
	FString DebugName;
    uint32 TextEntryId = 0;
    uint32 TooltipEntryId = 0;
	FString ElementName;

	TWeakPtr<FACEUIElement> Parent;
	TArray<TSharedPtr<FACEUIElement>> Children;

	int32 GetDrawX() const { return X + LayoutOffsetX; }
	int32 GetDrawY() const { return Y + LayoutOffsetY; }

	FIntPoint GetScreenOrigin() const
	{
		FIntPoint Origin(GetDrawX(), GetDrawY());
		for (TSharedPtr<FACEUIElement> P = Parent.Pin(); P.IsValid(); P = P->Parent.Pin())
		{
			Origin.X += P->GetDrawX();
			Origin.Y += P->GetDrawY();
		}
		return Origin;
	}

	void SortChildrenByZ()
	{
		MarkTreeChanged();
		// Stable for equal ZLevel so LayoutDesc sibling order (paint / hit order) is preserved.
		// Decorative underlays always paint first — LayoutDesc gives InvBackgroundImage z=100 and
		// lists it last; without this it covers PaperDoll / packs / item grid.
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			if (Children[i].IsValid())
			{
				Children[i]->SortKey = i;
			}
		}
		Children.Sort([](const TSharedPtr<FACEUIElement>& A, const TSharedPtr<FACEUIElement>& B)
		{
			const bool Ba = A.IsValid() && A->IsDecorativeUnderlay();
			const bool Bb = B.IsValid() && B->IsDecorativeUnderlay();
			if (Ba != Bb)
			{
				return Ba && !Bb; // underlays first, regardless of authored ZLevel
			}
			const uint32 Za = A.IsValid() ? A->ZLevel : 0;
			const uint32 Zb = B.IsValid() ? B->ZLevel : 0;
			if (Za != Zb)
			{
				return Za < Zb;
			}
			const int32 Ka = A.IsValid() ? A->SortKey : 0;
			const int32 Kb = B.IsValid() ? B->SortKey : 0;
			return Ka < Kb;
		});
	}

	void AddChild(const TSharedPtr<FACEUIElement>& Child)
	{
		if (!Child.IsValid())
		{
			return;
		}
		if (auto Old = Child->Parent.Pin()) Old->RemoveChild(Child);
		Child->Parent = AsShared();
		Children.Add(Child);
		SortChildrenByZ();
	}

	// Membership/name/order changes invalidate lookup caches; visibility is read
	// at lookup time and must not rebuild the entire tree every HUD frame.
	uint64 TreeRevision = 0;
	void MarkTreeChanged()
	{
		++TreeRevision;
		if (auto P = Parent.Pin()) P->MarkTreeChanged();
	}
	void SetElementName(const FString& Name)
	{
		if (ElementName == Name) return;
		ElementName = Name;
		MarkTreeChanged();
	}
	void RemoveChild(const TSharedPtr<FACEUIElement>& Child)
	{
		if (Children.Remove(Child))
		{
			Child->Parent.Reset();
			MarkTreeChanged();
		}
	}
	void ClearChildren()
	{
		for (auto& Child : Children) if (Child) Child->Parent.Reset();
		Children.Reset();
		MarkTreeChanged();
	}

	/** Full-bleed backdrop art listed after content in LayoutDesc — never a click target. */
	bool IsDecorativeUnderlay() const
	{
		return ElementName.Contains(TEXT("Background"))
			|| ElementName.Contains(TEXT("Blackness"))
			|| ElementName.EndsWith(TEXT("Backdrop"))
			|| ElementName.EndsWith(TEXT("_Template"))
			|| ElementName.Contains(TEXT("Divider"))
			|| ElementName == TEXT("background_left")
			|| ElementName == TEXT("background_mid")
			|| ElementName == TEXT("background_right")
			|| ElementName == TEXT("SpellcastSlot_Background")
			|| ElementName == TEXT("SpellcastSlot_TabBackground")
			|| ElementName == TEXT("StatManagement_Template")
			|| ElementName == TEXT("SkillManagement_Attribute_Field")
			// Opaque page slab (0x06004CC2) — keep behind list overlays / filters.
			|| ElementName == TEXT("SpellbookPage")
			|| ElementName == TEXT("RootSpellbook_Field");
	}

	bool IsInteractiveHitTarget() const
	{
		// Set by the per-type factory (Text / Viewport) or by a binder disabling a control
		// (e.g. a dark, unaffordable Raise button must not activate).
		if (!bActivatable)
		{
			return false;
		}
		if (IsDecorativeUnderlay())
		{
			return false;
		}
		// Full-height sibling that overlaps Show Equipment; inventory drag uses overlay hit tests.
		if (ElementName == TEXT("PaperDollDragMask") || ElementName == TEXT("Paperdoll_Icon_DragAccept"))
		{
			return false;
		}
		if (bPanelTab || bBooleanButton || Type == ACEUI::ElementType::OptionCheckbox
			|| Type == ACEUI::ElementType::OptionCheckboxSlider
			|| Type == ACEUI::ElementType::OptionCheckboxBitfield
			|| Type == ACEUI::ElementType::OptionCheckboxBitfield64 || Type == ACEUI::ElementType::Menu
			|| Type == ACEUI::ElementType::Button || Type == ACEUI::ElementType::Meter
			|| Type == ACEUI::ElementType::Scrollbar || Type == ACEUI::ElementType::Resizebar
			|| Type == ACEUI::ElementType::Dragbar)
		{
			return true;
		}
		if (ImageFileId != 0)
		{
			return true;
		}
		// Retail toolbar/chat buttons often arrive as type 0 with a *Button name and no media.
		// Spellbook filter rows (School_*/Level_*) are empty shells — the image child is decorative.
		return ElementName.EndsWith(TEXT("Button")) || ElementName.EndsWith(TEXT("Indicator"))
			|| ElementName.EndsWith(TEXT("Slot")) || ElementName.EndsWith(TEXT("DragArea"))
			|| ElementName == TEXT("LockUI") || ElementName.Contains(TEXT("TitleDrag"))
			|| ElementName.EndsWith(TEXT("Tab"))
			|| ElementName.StartsWith(TEXT("School_")) || ElementName.StartsWith(TEXT("Level_"))
			|| ElementName == TEXT("Paperdoll_Slots_Checkbox")
			|| ElementName == TEXT("AutoRepeatAttack") || ElementName == TEXT("AutoTarget")
			|| ElementName == TEXT("ViewCombatTarget")
			|| ElementName == TEXT("HighAttack") || ElementName == TEXT("MediumAttack")
			|| ElementName == TEXT("LowAttack") || ElementName == TEXT("PowerSlider");
	}

	/** Point is in this element's parent-local space (same space as DrawX/DrawY). Topmost-first. */
	TSharedPtr<FACEUIElement> HitTestInParentSpace(int32 ParentLocalX, int32 ParentLocalY)
	{
		if (!bVisible || Width <= 0 || Height <= 0)
		{
			return nullptr;
		}
		const int32 DX = GetDrawX();
		const int32 DY = GetDrawY();
		if (ParentLocalX < DX || ParentLocalY < DY
			|| ParentLocalX >= DX + Width || ParentLocalY >= DY + Height)
		{
			return nullptr;
		}
		const int32 LocalX = ParentLocalX - DX;
		const int32 LocalY = ParentLocalY - DY;
		for (int32 i = Children.Num() - 1; i >= 0; --i)
		{
			if (TSharedPtr<FACEUIElement> Hit = Children[i]->HitTestInParentSpace(LocalX, LocalY))
			{
				return Hit;
			}
		}
		if (!IsInteractiveHitTarget())
		{
			return nullptr;
		}
		return AsShared();
	}
};
