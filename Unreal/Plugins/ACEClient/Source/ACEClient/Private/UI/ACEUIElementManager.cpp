#include "UI/ACEUIElementManager.h"
#include "UI/ACERetailUILayout.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<int32> CVarIndexedUILookups(TEXT("ace.UI.IndexedLookups"),1,
	TEXT("Cache element names until the UI tree changes; 0 uses recursive lookups for profiling."));
static TAutoConsoleVariable<int32> CVarReuseUILookupStorage(TEXT("ace.UI.ReuseLookupStorage"),1,
	TEXT("Reuse name-index allocations when the UI tree changes."));
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	constexpr uint32 DidLockUi = 0x060074B7;
	constexpr uint32 DidLockUiUnlocked = 0x060074B8;

	/** Persisted floaty drag offsets are relative to the anchor scheme; bump to invalidate. */
	constexpr int32 FloatyLayoutSchema = 2;

	bool IsFixedSizeFloaty(const FString& Name)
	{
		return Name == TEXT("RootGameplay_FloatySideVitals_Field")
			|| Name == TEXT("RootGameplay_FloatyVitals_Field")
			|| Name == TEXT("RootGameplay_FloatyIndicators_Field")
			|| Name == TEXT("RootGameplay_PowerBar_Field") || Name == TEXT("RootFloatyPowerBar_Field");
	}

	bool IsSavedLayoutWindow(const FString& Name)
	{
		return Name.StartsWith(TEXT("RootGameplay_Floaty"))
			|| Name == TEXT("RootGameplay_Radar_Field") || Name == TEXT("RootGameplay_PowerBar_Field");
	}

	bool IsAncestorVisible(const TSharedPtr<FACEUIElement>& Node)
	{
		for (TSharedPtr<FACEUIElement> Cur = Node; Cur.IsValid(); Cur = Cur->Parent.Pin())
		{
			if (!Cur->bVisible)
			{
				return false;
			}
		}
		return Node.IsValid();
	}

	bool IsFrameChromeName(const FString& Name)
	{
		return Name.Contains(TEXT("TopLeftCorner")) || Name.Contains(TEXT("TopRightCorner"))
			|| Name.Contains(TEXT("BottomLeftCorner")) || Name.Contains(TEXT("BottomRightCorner"))
			|| Name.Contains(TEXT("TopBorder")) || Name.Contains(TEXT("BottomBorder"))
			|| Name.Contains(TEXT("LeftBorder")) || Name.Contains(TEXT("RightBorder"));
	}

	void SyncLockedChromeRecursive(const TSharedPtr<FACEUIElement>& Node, bool bUiLocked)
	{
		if (!Node.IsValid())
		{
			return;
		}
		const FString& Name = Node->ElementName;
		if (!Name.IsEmpty() && IsFrameChromeName(Name))
		{
			const bool bLockedVariant = Name.Contains(TEXT("_Locked"));
			const FString OtherName = bLockedVariant ? Name.Replace(TEXT("_Locked"), TEXT("")) : Name + TEXT("_Locked");
			bool bHasAlternate = false;
			if (const auto Parent = Node->Parent.Pin())
				for (const auto& Sibling : Parent->Children)
					if (Sibling && Sibling->ElementName == OtherName) { bHasAlternate = true; break; }
			// Floaty chats have a single frame. Locking them disables dragging;
			// it must not hide the only border because no locked artwork exists.
			Node->bVisible = !bHasAlternate || (bLockedVariant ? bUiLocked : !bUiLocked);
		}
		if (Name == TEXT("LockUI"))
		{
			Node->ImageFileId = bUiLocked ? DidLockUi : DidLockUiUnlocked;
		}
		for (const TSharedPtr<FACEUIElement>& Child : Node->Children)
		{
			SyncLockedChromeRecursive(Child, bUiLocked);
		}
	}

	void CollectByName(
		const TSharedPtr<FACEUIElement>& Node, const FString& ElementName,
		TArray<TSharedPtr<FACEUIElement>>& Out)
	{
		if (!Node.IsValid())
		{
			return;
		}
		if (Node->ElementName == ElementName)
		{
			Out.Add(Node);
		}
		for (const TSharedPtr<FACEUIElement>& Child : Node->Children)
		{
			CollectByName(Child, ElementName, Out);
		}
	}

	TSharedPtr<FACEUIElement> FindElementByNameRecursive(
		const TSharedPtr<FACEUIElement>& Node, const FString& ElementName)
	{
		if (!Node.IsValid())
		{
			return nullptr;
		}
		if (Node->ElementName == ElementName)
		{
			return Node;
		}
		for (const TSharedPtr<FACEUIElement>& Child : Node->Children)
		{
			if (TSharedPtr<FACEUIElement> Found = FindElementByNameRecursive(Child, ElementName))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

void UACEUIElementManager::Initialize()
{
	SyntheticRoot = MakeShared<FACEUIElement>();
	SyntheticRoot->DebugName = TEXT("SyntheticRoot");
	SyntheticRoot->Type = ACEUI::ElementType::Field;
	SyntheticRoot->Width = ACEUI::ReferenceWidth;
	SyntheticRoot->Height = ACEUI::ReferenceHeight;
	SyntheticRoot->bActivatable = false;

	// Retail UIElement::RegisterElementClass equivalents. FACEUIElement is one struct, so a
	// factory's job is the per-type defaults: whether the node is a click target and whether
	// it takes focus. Types with no factory fall back to Field (see CreateElementByType).
	auto RegisterTyped = [this](uint32 Type, const TCHAR* Label, bool bActivatable, bool bAcceptsFocus)
	{
		RegisterElementFactory(Type,
			[Label, bActivatable, bAcceptsFocus](uint32 ElementId, uint32 InType) -> TSharedPtr<FACEUIElement>
			{
				TSharedPtr<FACEUIElement> El = MakeShared<FACEUIElement>();
				El->ElementId = ElementId;
				El->Type = InType;
				El->bActivatable = bActivatable;
				El->bAcceptsFocus = bAcceptsFocus;
				El->DebugName = FString::Printf(TEXT("%s_0x%08X"), Label, ElementId);
				return El;
			});
	};

	// Containers / plain regions.
	RegisterTyped(ACEUI::ElementType::Field, TEXT("Field"), true, false);
	RegisterTyped(ACEUI::ElementType::Panel, TEXT("Panel"), true, false);
	RegisterTyped(ACEUI::ElementType::GroupBox, TEXT("GroupBox"), true, false);
	// Controls the player presses / drags.
	RegisterTyped(ACEUI::ElementType::Button, TEXT("Button"), true, false);
	RegisterTyped(ACEUI::ElementType::Dragbar, TEXT("Dragbar"), true, false);
	RegisterTyped(ACEUI::ElementType::Resizebar, TEXT("Resizebar"), true, false);
	RegisterTyped(ACEUI::ElementType::Scrollbar, TEXT("Scrollbar"), true, false);
	RegisterTyped(ACEUI::ElementType::Menu, TEXT("Menu"), true, false);
	RegisterTyped(ACEUI::ElementType::ColorPicker, TEXT("ColorPicker"), true, false);
	RegisterTyped(ACEUI::ElementType::Meter, TEXT("Meter"), true, false);
	RegisterTyped(ACEUI::ElementType::ListBox, TEXT("ListBox"), true, false);
	RegisterTyped(ACEUI::ElementType::Browser, TEXT("Browser"), true, false);
	// Labels and 3D render hosts are never click targets — a Viewport that swallowed clicks
	// would block world picking through PortalSpace and the inventory paperdoll.
	RegisterTyped(ACEUI::ElementType::Text, TEXT("Text"), false, false);
	RegisterTyped(ACEUI::ElementType::Viewport, TEXT("Viewport"), false, false);
}

void UACEUIElementManager::Shutdown()
{
	ClearRoots();
	Factories.Reset();
	SyntheticRoot.Reset();
	FocusElement.Reset();
	ActiveElement.Reset();
	CaptureElement.Reset();
	HoverElement.Reset();
	DragFloaty.Reset();
	CaptureRefCount = 0;
	bDragMoved = false;
}

FIntRect UACEUIElementManager::GetCanvasViewportRect(FVector2D ViewportSize) const
{
	const int32 Vw = FMath::Max(1, FMath::FloorToInt(ViewportSize.X));
	const int32 Vh = FMath::Max(1, FMath::FloorToInt(ViewportSize.Y));
	return FIntRect(0, 0, Vw, Vh);
}

FVector2D UACEUIElementManager::GetCanvasScale(FVector2D ViewportSize) const
{
	// Retail bitmaps and glyphs are physical pixels, including the centered login panels.
	return FVector2D(1.f, 1.f);
}

namespace
{
	/**
	 * Retail UIElement::UpdateForParentSizeChange (acclient.c:158919): each edge of the
	 * authored box re-anchors independently against the parent's authored-vs-current size.
	 * Left/Top modes: 2 = follow parent delta, 3 = center, 4 = proportional, 0/1 = keep.
	 * Right/Bottom modes: 1 = follow parent delta, 3 = center, 4 = proportional, 0/2 = keep.
	 * Disagreeing edge pairs stretch the element (SmartBox/Keyboard fill, PowerBar widens).
	 */
	struct FRetailReflow
	{
		static void Reflow(const TSharedPtr<FACEUIElement>& El, int32 OPw, int32 OPh, int32 CPw, int32 CPh)
		{
			if (!El.IsValid())
			{
				return;
			}
			if (El->LayoutAuthoredW < 0)
			{
				El->LayoutAuthoredW = El->Width;
				El->LayoutAuthoredH = El->Height;
				El->LastReflowW = El->Width;
				El->LastReflowH = El->Height;
			}
			const int32 L = El->LayoutAuthoredX != MIN_int32 ? El->LayoutAuthoredX : El->X;
			const int32 T = El->LayoutAuthoredY != MIN_int32 ? El->LayoutAuthoredY : El->Y;
			const int32 W = El->LayoutAuthoredW;
			const int32 H = El->LayoutAuthoredH;
			const int32 R = L + W;
			const int32 B = T + H;
			const int32 DW = CPw - OPw;
			const int32 DH = CPh - OPh;
			const float SX = OPw > 0 ? static_cast<float>(CPw) / static_cast<float>(OPw) : 1.f;
			const float SY = OPh > 0 ? static_cast<float>(CPh) / static_cast<float>(OPh) : 1.f;

			int32 NewL = L;
			switch (El->LeftEdge)
			{
			case 2: NewL = L + DW; break;
			case 3: NewL = (CPw - W) / 2; break;
			case 4: NewL = FMath::RoundToInt(static_cast<float>(L) * SX); break;
			default: break;
			}
			int32 NewR = R;
			switch (El->RightEdge)
			{
			case 1: NewR = R + DW; break;
			case 3: NewR = (CPw + W) / 2; break;
			case 4: NewR = FMath::RoundToInt(static_cast<float>(R) * SX); break;
			default: break;
			}
			int32 NewT = T;
			switch (El->TopEdge)
			{
			case 2: NewT = T + DH; break;
			case 3: NewT = (CPh - H) / 2; break;
			case 4: NewT = FMath::RoundToInt(static_cast<float>(T) * SY); break;
			default: break;
			}
			int32 NewB = B;
			switch (El->BottomEdge)
			{
			case 1: NewB = B + DH; break;
			case 3: NewB = (CPh + H) / 2; break;
			case 4: NewB = FMath::RoundToInt(static_cast<float>(B) * SY); break;
			default: break;
			}

			El->EdgeAnchorX = NewL - El->X;
			El->EdgeAnchorY = NewT - El->Y;
			El->RecomputeLayoutOffset();

			// Only write size when the reflow result changes — elements with agreeing
			// edges keep authored size, so binder/user-resize mutations survive reflow.
			const int32 NewW = FMath::Clamp(NewR - NewL, El->MinWidth, FMath::Max(El->MinWidth, El->MaxWidth));
			const int32 NewH = FMath::Clamp(NewB - NewT, El->MinHeight, FMath::Max(El->MinHeight, El->MaxHeight));
			const bool bChangedW = NewW != El->LastReflowW;
			const bool bChangedH = NewH != El->LastReflowH;
			if (bChangedW)
			{
				El->Width = NewW;
				El->LastReflowW = NewW;
			}
			if (bChangedH)
			{
				El->Height = NewH;
				El->LastReflowH = NewH;
			}
			// Children reflow against this element's authored vs current size.
			if ((bChangedW || bChangedH) && El->ElementName != TEXT("ChatLogScrollbar"))
			{
				for (const TSharedPtr<FACEUIElement>& Child : El->Children)
				{
					Reflow(Child, W, H, El->Width, El->Height);
				}
			}
		}
	};

	bool IsChatFloaty(const TSharedPtr<FACEUIElement>& El)
	{
		return El && (El->ElementName == TEXT("RootGameplay_FloatyMainChat_Field")
			|| El->ElementName.StartsWith(TEXT("RootGameplay_FloatyChat")));
	}
}

void UACEUIElementManager::ApplyEdgeAnchoredLayout(int32 ViewportWidth, int32 ViewportHeight)
{
	const int32 Vw = FMath::Max(ACEUI::ReferenceWidth, ViewportWidth);
	const int32 Vh = FMath::Max(ACEUI::ReferenceHeight, ViewportHeight);
	const FVector2D CharScale = GetCanvasScale(FVector2D(
		static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight)));
	const float CharS = FMath::Max(KINDA_SMALL_NUMBER, CharScale.X);


	for (const TSharedPtr<FACEUIElement>& Root : Roots)
	{
		if (!Root.IsValid())
		{
			continue;
		}

		// Character screens keep their authored 800×600 pixels and center in the viewport.
        if (Root->ElementName == TEXT("RootCharGenMaster"))
        {
            Root->Width=800;Root->Height=600;
            Root->EdgeAnchorX=FMath::RoundToInt((ViewportWidth/CharS-800)*.5f);
            Root->EdgeAnchorY=FMath::RoundToInt((ViewportHeight/CharS-600)*.5f);
            Root->RecomputeLayoutOffset();continue;
        }
		if (Root->ElementName == TEXT("CharacterManagementField"))
		{
			Root->Width = ACEUI::ReferenceWidth;
			Root->Height = ACEUI::ReferenceHeight;
			Root->EdgeAnchorX = FMath::RoundToInt(
				(static_cast<float>(ViewportWidth) / CharS - static_cast<float>(ACEUI::ReferenceWidth)) * 0.5f);
			Root->EdgeAnchorY = FMath::RoundToInt(
				(static_cast<float>(ViewportHeight) / CharS - static_cast<float>(ACEUI::ReferenceHeight)) * 0.5f);
			Root->UserDragX = 0;
			Root->UserDragY = 0;
			Root->RecomputeLayoutOffset();

			for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
			{
				if (!Child.IsValid())
				{
					continue;
				}
				// The character binder owns Delete/Restore, modal and credits visibility.
				// Re-anchoring must not reopen a dismissed dialog every frame.
				Child->EdgeAnchorX = 0;
				Child->EdgeAnchorY = 0;
				Child->RecomputeLayoutOffset();
			}
			continue;
		}

		// Retail RefreshEvent: resize the root element to the display and let the
		// authored edge anchors reflow every descendant.
		if (Root->Width >= ACEUI::ReferenceWidth && Root->Height >= ACEUI::ReferenceHeight
			&& Root->ElementName.Contains(TEXT("RootGameplay")))
		{
			if (Root->LayoutAuthoredW < 0)
			{
				Root->LayoutAuthoredW = Root->Width;
				Root->LayoutAuthoredH = Root->Height;
			}
			Root->Width = Vw;
			Root->Height = Vh;
			Root->EdgeAnchorX = 0;
			Root->EdgeAnchorY = 0;
			Root->UserDragX = 0;
			Root->UserDragY = 0;
			Root->RecomputeLayoutOffset();

			for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
			{
				FRetailReflow::Reflow(Child,
					Root->LayoutAuthoredW, Root->LayoutAuthoredH, Root->Width, Root->Height);
			}

			// Retail UpdateForParentSizeChange clamps the runtime box back into the
			// parent — a floaty (dragged or reflowed) must never end up unreachable
			// offscreen. Adjust UserDrag so the correction persists into saves.
			for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
			{
				if (!Child.IsValid() || !Child->bVisible
					|| Child->Width <= 0 || Child->Height <= 0
					|| Child == DragFloaty || Child == ResizeFloaty)
				{
					continue;
				}
				// A saved/resized floaty may exceed the logical viewport after a
				// DPI increase or window shrink. Moving it to Y=0 cannot expose
				// its bottom controls; reduce its height and reflow the content
				// before clamping its position (also while the UI is locked).
				if (Child->Height > Vh && Child->ElementName.StartsWith(TEXT("RootGameplay_Floaty"))
					&& !IsFixedSizeFloaty(Child->ElementName))
				{
					if (Child->AuthoredHeight < 0) Child->AuthoredHeight = Child->Height;
					Child->UserResizeH = Vh - Child->AuthoredHeight;
					ApplyFloatyResizeLayout(Child);
				}
				const int32 DrawX = Child->GetDrawX();
				const int32 DrawY = Child->GetDrawY();
				int32 DeltaX = 0;
				int32 DeltaY = 0;
				if (DrawX + Child->Width > Vw)
				{
					DeltaX = Vw - (DrawX + Child->Width);
				}
				if (DrawX + DeltaX < 0)
				{
					DeltaX = -DrawX;
				}
				if (DrawY + Child->Height > Vh)
				{
					DeltaY = Vh - (DrawY + Child->Height);
				}
				if (DrawY + DeltaY < 0)
				{
					DeltaY = -DrawY;
				}
				if (DeltaX != 0 || DeltaY != 0)
				{
					Child->UserDragX += DeltaX;
					Child->UserDragY += DeltaY;
					Child->RecomputeLayoutOffset();
				}
			}
		}
	}
}

bool UACEUIElementManager::ViewportToCanvas(
	FVector2D ViewportPos, FVector2D ViewportSize, int32& OutCanvasX, int32& OutCanvasY) const
{
	if (ViewportSize.X < 1.f || ViewportSize.Y < 1.f)
	{
		return false;
	}
	if (ViewportPos.X < 0.f || ViewportPos.Y < 0.f
		|| ViewportPos.X >= ViewportSize.X || ViewportPos.Y >= ViewportSize.Y)
	{
		return false;
	}
	const FVector2D Scale = GetCanvasScale(ViewportSize);
	const float SX = FMath::Max(KINDA_SMALL_NUMBER, Scale.X);
	const float SY = FMath::Max(KINDA_SMALL_NUMBER, Scale.Y);
	OutCanvasX = FMath::FloorToInt(ViewportPos.X / SX);
	OutCanvasY = FMath::FloorToInt(ViewportPos.Y / SY);
	return true;
}

void UACEUIElementManager::ClearRoots()
{
	InvalidateNameLookupIndex();
	Roots.Reset();
	if (SyntheticRoot.IsValid())
	{
		SyntheticRoot->ClearChildren();
	}
	DragFloaty.Reset();
	ResizeFloaty.Reset();
	bDragMoved = false;
}

void UACEUIElementManager::AddRoot(const TSharedPtr<FACEUIElement>& Root)
{
	if (!Root.IsValid() || !SyntheticRoot.IsValid())
	{
		return;
	}
	SyntheticRoot->AddChild(Root);
	InvalidateNameLookupIndex();
	Roots.Add(Root);
	SyncLockedChromeVisibility();
}

void UACEUIElementManager::BeginNameLookupPass()
{
	++NameLookupPassDepth;
}

void UACEUIElementManager::EndNameLookupPass()
{
	check(NameLookupPassDepth > 0);
	--NameLookupPassDepth;
}

void UACEUIElementManager::InvalidateNameLookupIndex()
{
	bNameLookupIndexValid = false;
	// Do not retain references to removed windows. Reuse only the allocations,
	// never prior membership/visibility: menus can mutate children directly.
	if (CVarReuseUILookupStorage.GetValueOnGameThread())
		for (auto& Entry:NameLookupIndex) Entry.Value.Reset();
	else NameLookupIndex.Reset();
}

void UACEUIElementManager::BuildNameLookupIndex() const
{
	const uint64 Revision = SyntheticRoot ? SyntheticRoot->TreeRevision : 0;
	if (bNameLookupIndexValid && NameLookupRevision == Revision) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(ACE_UINameIndex);
	for (auto& Entry : NameLookupIndex) Entry.Value.Reset();
	TFunction<void(const TSharedPtr<FACEUIElement>&)> Visit = [&](const auto& Node)
	{
		if (!Node) return;
		if (!Node->ElementName.IsEmpty()) NameLookupIndex.FindOrAdd(Node->ElementName).Add(Node);
		for (const auto& Child : Node->Children) Visit(Child);
	};
	Visit(SyntheticRoot);
	for(auto It=NameLookupIndex.CreateIterator();It;++It) if(It.Value().IsEmpty()) It.RemoveCurrent();
	bNameLookupIndexValid = true;
	NameLookupRevision = Revision;
}

TSharedPtr<FACEUIElement> UACEUIElementManager::FindElementByName(const FString& ElementName) const
{
	if (ElementName.IsEmpty() || !SyntheticRoot.IsValid())
	{
		return nullptr;
	}
	TArray<TSharedPtr<FACEUIElement>> Matches;
	const TArray<TSharedPtr<FACEUIElement>>* Candidates = &Matches;
	if (NameLookupPassDepth > 0 && CVarIndexedUILookups.GetValueOnGameThread() != 0)
	{
		BuildNameLookupIndex();
		Candidates = NameLookupIndex.Find(ElementName);
		if (!Candidates) return nullptr;
	}
	else CollectByName(SyntheticRoot, ElementName, Matches);
	for (const TSharedPtr<FACEUIElement>& Match : *Candidates)
	{
		if (IsAncestorVisible(Match))
		{
			return Match;
		}
	}
	return Candidates->Num() > 0 ? (*Candidates)[0] : nullptr;
}

TSharedPtr<FACEUIElement> UACEUIElementManager::FindElementUnder(
	const FString& AncestorName, const FString& ElementName) const
{
	if (AncestorName.IsEmpty() || ElementName.IsEmpty() || !SyntheticRoot.IsValid())
	{
		return nullptr;
	}
	TArray<TSharedPtr<FACEUIElement>> Ancestors;
	if (NameLookupPassDepth > 0 && CVarIndexedUILookups.GetValueOnGameThread() != 0)
	{
		BuildNameLookupIndex();
		if (const auto* Found = NameLookupIndex.Find(AncestorName)) Ancestors = *Found;
	}
	else CollectByName(SyntheticRoot, AncestorName, Ancestors);
	TSharedPtr<FACEUIElement> Fallback;
	for (const auto& Ancestor : Ancestors)
	{
		if (auto Child = FindElementByNameRecursive(Ancestor, ElementName))
		{
			if (IsAncestorVisible(Ancestor)) return Child;
			if (!Fallback) Fallback = Child;
		}
	}
	return Fallback;
}

bool UACEUIElementManager::SetElementVisibleByName(const FString& ElementName, bool bVisible)
{
	TSharedPtr<FACEUIElement> El = FindElementByName(ElementName);
	if (!El.IsValid())
	{
		return false;
	}
	El->bVisible = bVisible;
	if (bVisible)
	{
		SyncLockedChromeVisibility();
	}
	return true;
}

void UACEUIElementManager::SetUiLocked(bool bLocked)
{
	if (bUiLocked == bLocked)
	{
		return;
	}
	bUiLocked = bLocked;
	if (bUiLocked)
	{
		CancelPointerCapture();
	}
	SyncLockedChromeVisibility();
	SaveFloatyLayout();
}

void UACEUIElementManager::SyncLockedChromeVisibility()
{
	if (!SyntheticRoot.IsValid())
	{
		return;
	}
	SyncLockedChromeRecursive(SyntheticRoot, bUiLocked);
}

TSharedPtr<FACEUIElement> UACEUIElementManager::FindFloatyRoot(const TSharedPtr<FACEUIElement>& Element)
{
	for (TSharedPtr<FACEUIElement> Cur = Element; Cur.IsValid(); Cur = Cur->Parent.Pin())
	{
		if (Cur->ElementName.StartsWith(TEXT("RootGameplay_Floaty"))
			|| Cur->ElementName == TEXT("RootGameplay_PowerBar_Field")
			|| Cur->ElementName == TEXT("RootFloatyPowerBar_Field"))
		{
			return Cur;
		}
	}
	return nullptr;
}

bool UACEUIElementManager::IsFloatyDragHandle(const TSharedPtr<FACEUIElement>& Element)
{
	if (!Element.IsValid())
	{
		return false;
	}
	const FString& Name = Element->ElementName;
	// This meter has no clickable controls. Its fill/label are safe grab areas,
	// so repositioning a charging jump does not require hitting a five-pixel rim.
	if (const auto Window = FindFloatyRoot(Element); Window
		&& (Window->ElementName == TEXT("RootGameplay_PowerBar_Field")
			|| Window->ElementName == TEXT("RootFloatyPowerBar_Field"))) return true;
	if (Name == TEXT("InvTitleText") || Name == TEXT("TitleText") || Name == TEXT("TitleBackground")
		|| Name == TEXT("TitleBar") || Name == TEXT("DisplayedBookNameText")) return true;
	if (Name.EndsWith(TEXT("DragArea")) || Name.Contains(TEXT("TitleDrag")))
	{
		return true;
	}
	if (Element->Type == ACEUI::ElementType::Dragbar)
	{
		return true;
	}
	// Top borders double as drag strips when unlocked.
	return Name.Contains(TEXT("TopBorder")) && !Name.Contains(TEXT("Locked"));
}

bool UACEUIElementManager::IsFloatyResizeHandle(const TSharedPtr<FACEUIElement>& Element)
{
	if (!Element.IsValid())
	{
		return false;
	}
	const auto Floaty = FindFloatyRoot(Element);
	if (!Floaty || IsFixedSizeFloaty(Floaty->ElementName)) return false;
	if (Floaty->ElementName == TEXT("RootGameplay_FloatyCombatPanel_Field"))
	{
		TFunction<bool(const TSharedPtr<FACEUIElement>&)> HasSpells = [&](const auto& Node) -> bool
		{
			if (Node->ElementName == TEXT("Spellcasting")) return Node->bVisible;
			for (const auto& Child : Node->Children) if (HasSpells(Child)) return true;
			return false;
		};
		if (HasSpells(Floaty))
			return Element->ElementName.Contains(TEXT("LeftBorder")) || Element->ElementName.Contains(TEXT("RightBorder"))
				|| Element->ElementName.Contains(TEXT("LeftCorner")) || Element->ElementName.Contains(TEXT("RightCorner"));
	}
	if (IsChatFloaty(Floaty))
		return Element->bResizeLeft || Element->bResizeRight || Element->bResizeTop || Element->bResizeBottom;
	if (Floaty->ElementName == TEXT("RootGameplay_FloatyToolbar_Field"))
		return Element->Type == ACEUI::ElementType::Resizebar && Element->bResizeBottom;
	// Bottom border / Resizebar — works locked or unlocked (chrome variant may differ).
	const FString& Name = Element->ElementName;
	if (Element->Type == ACEUI::ElementType::Resizebar && Name.Contains(TEXT("BottomBorder")))
	{
		return true;
	}
	return Name.Contains(TEXT("BottomBorder"));
}

EMouseCursor::Type UACEUIElementManager::GetWindowCursor(FVector2D ViewportPos, FVector2D ViewportSize) const
{
	if (bUiLocked) return EMouseCursor::Default;
	int32 X = 0, Y = 0;
	if (!ViewportToCanvas(ViewportPos, ViewportSize, X, Y) && !CaptureElement) return EMouseCursor::Default;
	const auto Hit = CaptureElement ? CaptureElement : HitTestCanvas(X, Y);
	if (!Hit || !Hit->IsPaintVisible() || !FindFloatyRoot(Hit)) return EMouseCursor::Default;
	if (IsFloatyResizeHandle(Hit))
	{
		if (FindFloatyRoot(Hit)->ElementName == TEXT("RootGameplay_FloatyCombatPanel_Field"))
			return EMouseCursor::ResizeLeftRight;
		// Non-chat panels currently expose their bottom grip only. Chat frames
		// carry the actual retail edge flags, including diagonal corner handles.
		if (!IsChatFloaty(FindFloatyRoot(Hit))) return EMouseCursor::ResizeUpDown;
		const bool H = Hit->bResizeLeft || Hit->bResizeRight;
		const bool V = Hit->bResizeTop || Hit->bResizeBottom;
		if (H && V) return Hit->bResizeLeft == Hit->bResizeTop
			? EMouseCursor::ResizeSouthEast : EMouseCursor::ResizeSouthWest;
		return H ? EMouseCursor::ResizeLeftRight : EMouseCursor::ResizeUpDown;
	}

	return IsFloatyDragHandle(Hit) ? EMouseCursor::CardinalCross : EMouseCursor::Default;
}

void UACEUIElementManager::ApplyFloatyResizeLayout(const TSharedPtr<FACEUIElement>& Floaty)
{
	if (!Floaty.IsValid())
	{
		return;
	}
	if (Floaty->AuthoredHeight < 0)
	{
		Floaty->AuthoredHeight = Floaty->Height;
	}
	if (Floaty->ElementName == TEXT("RootGameplay_FloatyCombatPanel_Field"))
	{
		// The combat binder lays out the frame and controls for each stance.
		// Reflowing its children here adds edge offsets a second time to the
		// binder's explicit positions (Cast and the right border drift inward).
		if (Floaty->AuthoredWidth >= 0)
			Floaty->Width = FMath::Clamp(Floaty->AuthoredWidth + Floaty->UserResizeW, Floaty->MinWidth, Floaty->MaxWidth);
		return;
	}
	if (Floaty->ElementName == TEXT("RootGameplay_FloatyPanel_Field"))
	{
		// Snapshot the resolved retail tree before binders place dynamic content.
		// Reflow every page from that same baseline, including currently hidden tabs.
		TFunction<void(const TSharedPtr<FACEUIElement>&)> Capture = [&](const auto& Node)
		{
			if (Node->LayoutAuthoredX != MIN_int32) return;
			Node->LayoutAuthoredX = Node->X; Node->LayoutAuthoredY = Node->Y;
			Node->LayoutAuthoredW = Node->LastReflowW = Node->Width;
			Node->LayoutAuthoredH = Node->LastReflowH = Node->Height;
			for (const auto& Child : Node->Children) Capture(Child);
		};
		for (const auto& Child : Floaty->Children) Capture(Child);
		if (Floaty->AuthoredWidth < 0) Floaty->AuthoredWidth = Floaty->Width;
		Floaty->Height = FMath::Clamp(Floaty->GetLayoutHeight(), Floaty->MinHeight, Floaty->MaxHeight);
		Floaty->UserResizeH = Floaty->Height - Floaty->AuthoredHeight;
		for (const auto& Child : Floaty->Children)
			FRetailReflow::Reflow(Child, Floaty->AuthoredWidth, Floaty->AuthoredHeight, Floaty->Width, Floaty->Height);
		return;
	}
	if (!IsChatFloaty(Floaty) && Floaty->AuthoredWidth >= 0)
	{
		const int32 NewW = FMath::Clamp(Floaty->AuthoredWidth + Floaty->UserResizeW, Floaty->MinWidth, Floaty->MaxWidth);
		if (NewW != Floaty->Width)
		{
			Floaty->Width = NewW;
			for (const auto& Child : Floaty->Children)
				FRetailReflow::Reflow(Child, Floaty->AuthoredWidth, Floaty->AuthoredHeight, NewW, Floaty->Height);
		}
	}
	if (Floaty->ElementName == TEXT("RootGameplay_FloatyToolbar_Field"))
		Floaty->UserResizeH = FMath::Clamp(Floaty->GetLayoutHeight(), Floaty->MinHeight, Floaty->MaxHeight) - Floaty->AuthoredHeight;
	if (IsChatFloaty(Floaty))
	{
		if (Floaty->AuthoredWidth < 0) Floaty->AuthoredWidth=Floaty->Width;
		Floaty->Width=FMath::Clamp(Floaty->AuthoredWidth+Floaty->UserResizeW,Floaty->MinWidth,Floaty->MaxWidth);
		Floaty->Height=FMath::Clamp(Floaty->GetLayoutHeight(),Floaty->MinHeight,Floaty->MaxHeight);
		for (const auto& Child : Floaty->Children)
			FRetailReflow::Reflow(Child,Floaty->AuthoredWidth,Floaty->AuthoredHeight,Floaty->Width,Floaty->Height);
		return;
	}
	const int32 NewH = Floaty->GetLayoutHeight();
	Floaty->Height = NewH;

	const int32 Border = 5;
	const int32 InnerH = FMath::Max(1, NewH - Border * 2);
	const int32 BottomY = NewH - Border;

	for (const TSharedPtr<FACEUIElement>& Child : Floaty->Children)
	{
		if (!Child.IsValid())
		{
			continue;
		}
		const FString& Name = Child->ElementName;
		if (Name.Contains(TEXT("LeftBorder")) || Name.Contains(TEXT("RightBorder")))
		{
			Child->Height = InnerH;
		}
		else if (Name.Contains(TEXT("BottomLeftCorner")) || Name.Contains(TEXT("BottomRightCorner"))
			|| Name.Contains(TEXT("BottomBorder")))
		{
			Child->Y = BottomY;
		}
		else if (Name == TEXT("ToolbarField"))
		{
			Child->Height = InnerH;
		}
		else if (Name == TEXT("PanelPages"))
		{
			Child->Height = InnerH;
			for (const TSharedPtr<FACEUIElement>& Page : Child->Children)
			{
				if (Page.IsValid())
				{
					// Retail reanchors the whole subtree when the shared panel grows.
					// Updating only the page height left effects descriptions midway
					// up the window and gave the spell list none of the extra space.
					if (Page->AuthoredHeight < 0) Page->AuthoredHeight = Page->Height;
					if (Page->AuthoredWidth < 0) Page->AuthoredWidth = Page->Width;
					Page->Height = InnerH;
					for (const auto& Content : Page->Children)
						FRetailReflow::Reflow(Content, Page->AuthoredWidth, Page->AuthoredHeight, Page->Width, InnerH);
				}
			}
		}
		else if (Name == TEXT("ChatLogField"))
		{
			// MainChat: entry strip is 17px above the bottom border.
			constexpr int32 EntryH = 17;
			const int32 LogH = FMath::Max(16, NewH - Border - EntryH - Child->Y);
			Child->Height = LogH;
			for (const TSharedPtr<FACEUIElement>& LogChild : Child->Children)
			{
				if (!LogChild.IsValid())
				{
					continue;
				}
				if (LogChild->ElementName == TEXT("ChatLog")
					|| LogChild->ElementName == TEXT("ChatLogScrollbar"))
				{
					LogChild->Height = LogH;
				}
			}
		}
		else if (Name == TEXT("ChatEntryField"))
		{
			Child->Y = NewH - Border - Child->Height;
		}
		else if (Name == TEXT("ItemExamineUI") || Name == TEXT("BasicCreatureExamineUI") || Name == TEXT("SpellExamineUI"))
		{
			Child->Height = FMath::Max(64, NewH - Border - Child->Y);
			for (const auto& Sub : Child->Children)
			{
				if (!Sub) continue;
				const FString& N = Sub->ElementName;
				if (N == TEXT("ItemDisplayText") || N.StartsWith(TEXT("ItemDisplayTextScrollbar")))
					Sub->Height = FMath::Max(32, Child->Height - 79);
				else if (N == TEXT("ItemExamBackground_Divider_Lower")) Sub->Y = Child->Height - 79;
				else if (N == TEXT("ItemExamBackground_Paper") || N == TEXT("ItemInscriptionText")) Sub->Y = Child->Height - 74;
				else if (N == TEXT("ItemSignatureText")) Sub->Y = Child->Height - 17;
				else if (N == TEXT("BasicCreatureExamBackground")) Sub->Height = Child->Height;
				else if (N == TEXT("BasicCreatureExam_ExtraInfo")) Sub->Height = FMath::Max(1, Child->Height - Sub->Y);
			}
		}
	}
}

TSharedPtr<FACEUIElement> UACEUIElementManager::FindWindowAtCanvas(int32 CanvasX, int32 CanvasY) const
{
    if (!SyntheticRoot) return nullptr;
    for (int32 Ri=SyntheticRoot->Children.Num()-1; Ri>=0; --Ri)
    {
        const auto Root=SyntheticRoot->Children[Ri];
        if (!Root || !Root->bVisible || Root->ElementName != TEXT("RootGameplay_Field")) continue;
        for (int32 I=Root->Children.Num()-1; I>=0; --I)
        {
            const auto Window=Root->Children[I];
            if (!Window || !Window->IsPaintVisible() || Window->Width<=0 || Window->Height<=0
                || Window->ElementName == TEXT("RootGameplay_SmartBox_Field")) continue;
            // The keyboard root is a full-screen anchoring field; only its
            // centered frame covers other windows or receives pointer input.
            const auto Bounds=Window->ElementName==TEXT("RootGameplay_Keyboard_Field")
                ? FindElementByNameRecursive(Window,TEXT("KeyboardFrame")) : Window;
            if(!Bounds)continue;
            const FIntPoint O=Bounds->GetScreenOrigin();
            if (CanvasX>=O.X && CanvasY>=O.Y && CanvasX<O.X+Bounds->Width && CanvasY<O.Y+Bounds->Height)
                return Window;
        }
    }
    return nullptr;
}

TSharedPtr<FACEUIElement> UACEUIElementManager::HitTestCanvas(int32 CanvasX, int32 CanvasY) const
{
    if (const auto Window=FindWindowAtCanvas(CanvasX,CanvasY))
    {
        const auto Parent=Window->Parent.Pin();
        const FIntPoint O=Parent ? Parent->GetScreenOrigin() : FIntPoint::ZeroValue;
        const auto Hit=Window->HitTestInParentSpace(CanvasX-O.X,CanvasY-O.Y);
        if (bUiLocked && (IsFloatyDragHandle(Hit) || IsFloatyResizeHandle(Hit))) return Window;
        if (!bUiLocked && FindFloatyRoot(Window))
        {
            // Buttons, slots and resize grips keep priority over title/drag art.
            const bool bControl = Hit && (Hit->Type == ACEUI::ElementType::Button
                || Hit->Type == ACEUI::ElementType::Resizebar || Hit->bPanelTab
                || Hit->ElementName.EndsWith(TEXT("Button")) || Hit->ElementName.EndsWith(TEXT("Slot")));
            if (!bControl)
            {
                TFunction<TSharedPtr<FACEUIElement>(const TSharedPtr<FACEUIElement>&)> FindDrag = [&](const auto& Node) -> TSharedPtr<FACEUIElement>
                {
                    if (!Node || !Node->IsPaintVisible()) return nullptr;
                    const FIntPoint P = Node->GetScreenOrigin();
                    if (CanvasX < P.X || CanvasY < P.Y || CanvasX >= P.X + Node->Width || CanvasY >= P.Y + Node->Height) return nullptr;
                    for (int32 I = Node->Children.Num()-1; I >= 0; --I)
                        if (const auto Drag = FindDrag(Node->Children[I])) return Drag;
                    return IsFloatyResizeHandle(Node) || IsFloatyDragHandle(Node) ? Node : nullptr;
                };
                if (const auto Drag = FindDrag(Window)) return Drag;
            }
        }
        return Hit ? Hit : Window; // Empty panel bodies must not activate controls behind them.
    }

	if (!SyntheticRoot.IsValid())
	{
		return nullptr;
	}
	for (int32 i = SyntheticRoot->Children.Num() - 1; i >= 0; --i)
	{
		if (TSharedPtr<FACEUIElement> Hit =
			SyntheticRoot->Children[i]->HitTestInParentSpace(CanvasX, CanvasY))
		{
			if (bUiLocked && (IsFloatyDragHandle(Hit) || IsFloatyResizeHandle(Hit)))
			{
				if (const auto Floaty = FindFloatyRoot(Hit)) return Floaty;
				return Hit->Parent.Pin();
			}
			return Hit;
		}
	}
	return nullptr;
}

void UACEUIElementManager::SetFocusElement(const TSharedPtr<FACEUIElement>& Element)
{
	FocusElement = Element;
}

void UACEUIElementManager::SetActiveElement(const TSharedPtr<FACEUIElement>& Element)
{
	ActiveElement = Element;
}

void UACEUIElementManager::NotifyMouseMove(FVector2D ViewportPos, FVector2D ViewportSize)
{
	int32 Cx = 0, Cy = 0;
	if (!ViewportToCanvas(ViewportPos, ViewportSize, Cx, Cy))
	{
		return;
	}

	if (ResizeFloaty.IsValid() && !bUiLocked && bResizeChat)
	{
		const int32 Dx=Cx-ResizeGrabCanvasX, Dy=Cy-ResizeGrabCanvasY;
		if (FMath::Abs(Dx)>2 || FMath::Abs(Dy)>2) bDragMoved=true;
		const int32 W=ResizeFloaty->AuthoredWidth, H=ResizeFloaty->AuthoredHeight;
		const int32 NewW=FMath::Clamp(W+ResizeStartUserW+(bResizeLeft ? -Dx : bResizeRight ? Dx : 0),ResizeFloaty->MinWidth,ResizeFloaty->MaxWidth);
		const int32 NewH=FMath::Clamp(H+ResizeStartUserH+(bResizeTop ? -Dy : bResizeBottom ? Dy : 0),ResizeFloaty->MinHeight,ResizeFloaty->MaxHeight);
		ResizeFloaty->UserResizeW=NewW-W; ResizeFloaty->UserResizeH=NewH-H;
		if (bResizeLeft) ResizeFloaty->UserDragX=ResizeStartUserDragX-(NewW-W-ResizeStartUserW);
		if (bResizeTop) ResizeFloaty->UserDragY=ResizeStartUserDragY-(NewH-H-ResizeStartUserH);
		ResizeFloaty->RecomputeLayoutOffset(); ApplyFloatyResizeLayout(ResizeFloaty);
		HoverElement=ResizeFloaty; return;
	}
	if (ResizeFloaty.IsValid() && !bUiLocked)
	{
		const int32 Dy = Cy - ResizeGrabCanvasY;
		if (FMath::Abs(Dy) > 2)
		{
			bDragMoved = true;
		}
		// Panel grows downward; bottom-anchored chat grows upward (lift via UserDragY).
		const int32 MinH = ResizeFloaty->MinHeight;
		const int32 AuthH = ResizeFloaty->AuthoredHeight >= 0
			? ResizeFloaty->AuthoredHeight : ResizeFloaty->Height;
		int32 NewExtra = ResizeStartUserH + (bResizeBottomAnchored ? -Dy : Dy);
		NewExtra = FMath::Clamp(NewExtra, MinH - AuthH, ResizeFloaty->MaxHeight - AuthH);
		ResizeFloaty->UserResizeH = NewExtra;
		if (bResizeBottomAnchored)
		{
			ResizeFloaty->UserDragY = ResizeStartUserDragY - (NewExtra - ResizeStartUserH);
			ResizeFloaty->RecomputeLayoutOffset();
		}
		ApplyFloatyResizeLayout(ResizeFloaty);
		HoverElement = ResizeFloaty;
		return;
	}

	if (DragFloaty.IsValid() && !bUiLocked)
	{
		const int32 Dx = Cx - DragGrabCanvasX;
		const int32 Dy = Cy - DragGrabCanvasY;
		if (FMath::Abs(Dx) > 2 || FMath::Abs(Dy) > 2)
		{
			bDragMoved = true;
		}
		DragFloaty->UserDragX = DragStartUserX + Dx;
		DragFloaty->UserDragY = DragStartUserY + Dy;
		DragFloaty->RecomputeLayoutOffset();
		HoverElement = DragFloaty;
		return;
	}

	TSharedPtr<FACEUIElement> Hit = CaptureElement.IsValid() ? CaptureElement : HitTestCanvas(Cx, Cy);
	if (Hit != HoverElement)
	{
		HoverElement = Hit;
		OnHoverChanged.Broadcast(HoverElement);
	}
}

void UACEUIElementManager::BringFloatyToFront(const TSharedPtr<FACEUIElement>& Element)
{
	TSharedPtr<FACEUIElement> Floaty;
	for (TSharedPtr<FACEUIElement> Cur = Element; Cur.IsValid(); Cur = Cur->Parent.Pin())
	{
		TSharedPtr<FACEUIElement> Parent = Cur->Parent.Pin();
		if (!Parent.IsValid())
		{
			continue;
		}
		// Direct children of RootGameplay_Field are the floaty / chrome windows.
		if (Parent->ElementName == TEXT("RootGameplay_Field")
			|| (Parent == SyntheticRoot && Cur->ElementName.StartsWith(TEXT("RootGameplay_"))))
		{
			Floaty = Cur;
			break;
		}
	}
	if (!Floaty.IsValid() || Floaty->ElementName == TEXT("RootGameplay_SmartBox_Field"))
	{
		return;
	}
	NextFloatyZ = FMath::Max(NextFloatyZ + 1, 1000u);
	if (NextFloatyZ >= 9000)
	{
		NextFloatyZ = 1000;
	}
	Floaty->ZLevel = NextFloatyZ;
	if (TSharedPtr<FACEUIElement> Parent = Floaty->Parent.Pin())
	{
		Parent->SortChildrenByZ();
		InvalidateNameLookupIndex();
	}
}

void UACEUIElementManager::NotifyMouseDown(FVector2D ViewportPos, FVector2D ViewportSize, FKey Button)
{
	int32 Cx = 0, Cy = 0;
	if (!ViewportToCanvas(ViewportPos, ViewportSize, Cx, Cy))
	{
		return;
	}
	bDragMoved = false;
	DragFloaty.Reset();
	ResizeFloaty.Reset();

	TSharedPtr<FACEUIElement> Hit = HitTestCanvas(Cx, Cy);
	HoverElement = Hit;
	if (Hit.IsValid())
	{
		BringFloatyToFront(Hit);

		CaptureElement = Hit;
		CaptureRefCount = FMath::Max(CaptureRefCount + 1, 1);
		ActiveElement = Hit;
		if (Hit->bAcceptsFocus)
		{
			FocusElement = Hit;
		}

		if (!bUiLocked && Button == EKeys::LeftMouseButton && IsFloatyResizeHandle(Hit))
		{
			TSharedPtr<FACEUIElement> Floaty = FindFloatyRoot(Hit);
			// Retail: the vitals bar (health/stamina/mana indicators) is fixed-size —
			// its bottom border never acts as a resize grip.
			if (Floaty.IsValid() && !IsFixedSizeFloaty(Floaty->ElementName))
			{
				if (Floaty->AuthoredHeight < 0)
				{
					Floaty->AuthoredHeight = Floaty->Height;
				}
				ResizeFloaty = Floaty;
				bResizeChat=IsChatFloaty(Floaty);
				const bool bSpellResize = Floaty->ElementName == TEXT("RootGameplay_FloatyCombatPanel_Field")
					&& (Hit->ElementName.Contains(TEXT("Left")) || Hit->ElementName.Contains(TEXT("Right")));
				bResizeChat |= bSpellResize;
				if (Floaty->AuthoredWidth<0) Floaty->AuthoredWidth=Floaty->Width;
				ResizeGrabCanvasX=Cx; ResizeStartUserW=Floaty->UserResizeW; ResizeStartUserDragX=Floaty->UserDragX;
				bResizeLeft=Hit->bResizeLeft; bResizeRight=Hit->bResizeRight;
				bResizeTop=Hit->bResizeTop; bResizeBottom=Hit->bResizeBottom;
				if (bSpellResize)
				{
					bResizeLeft=Hit->ElementName.Contains(TEXT("Left")); bResizeRight=!bResizeLeft;
					bResizeTop=bResizeBottom=false;
				}
				ResizeGrabCanvasY = Cy;
				ResizeStartUserH = Floaty->UserResizeH;
				ResizeStartUserDragY = Floaty->UserDragY;
				bResizeBottomAnchored =
					Floaty->ElementName == TEXT("RootGameplay_FloatyMainChat_Field")
					|| Floaty->ElementName == TEXT("RootGameplay_FloatyCombatPanel_Field")
					|| Floaty->ElementName == TEXT("RootGameplay_FloatyEnvPanel_Field");
			}
		}
		else if (!bUiLocked && Button == EKeys::LeftMouseButton && IsFloatyDragHandle(Hit))
		{
			if (TSharedPtr<FACEUIElement> Floaty = FindFloatyRoot(Hit))
			{
				DragFloaty = Floaty;
				DragGrabCanvasX = Cx;
				DragGrabCanvasY = Cy;
				DragStartUserX = Floaty->UserDragX;
				DragStartUserY = Floaty->UserDragY;
			}
		}
	}
	else
	{
		FocusElement.Reset();
	}
}

void UACEUIElementManager::CancelPointerCapture()
{
	if (bDragMoved) SaveFloatyLayout();
	CaptureRefCount = 0; CaptureElement.Reset(); ActiveElement.Reset();
	DragFloaty.Reset(); ResizeFloaty.Reset(); bDragMoved = false;
}

void UACEUIElementManager::NotifyMouseUp(FVector2D ViewportPos, FVector2D ViewportSize, FKey Button, bool bRequireSameTarget)
{
	TSharedPtr<FACEUIElement> Released;
	int32 Cx = 0, Cy = 0;
	if (ViewportToCanvas(ViewportPos, ViewportSize, Cx, Cy))
	{
		Released = HitTestCanvas(Cx, Cy);
	}

	const bool bHadCapture = CaptureElement.IsValid();
	TSharedPtr<FACEUIElement> Activated = ActiveElement;
	const bool bSkipClick = bDragMoved;
	DragFloaty.Reset();
	ResizeFloaty.Reset();

	if (CaptureRefCount > 0)
	{
		--CaptureRefCount;
	}
	if (CaptureRefCount <= 0)
	{
		CaptureRefCount = 0;
		CaptureElement.Reset();
	}

	if (bHadCapture && Button == EKeys::LeftMouseButton && !bSkipClick
		&& (!bRequireSameTarget || Released == Activated))
	{
		if (Released.IsValid())
		{
			Activated = Released;
		}
		if (Activated.IsValid())
		{
			OnElementActivated.Broadcast(Activated);
		}
	}

	ActiveElement.Reset();
	if (bSkipClick)
	{
		SaveFloatyLayout();
	}
	bDragMoved = false;
}

bool UACEUIElementManager::IsCanvasOverUI(int32 CanvasX, int32 CanvasY) const
{
	if (HitTestCanvas(CanvasX, CanvasY).IsValid())
	{
		return true;
	}
	if (!SyntheticRoot.IsValid())
	{
		return false;
	}
	// Full floaty bounds (including empty panel body) must block world pick.
	for (const TSharedPtr<FACEUIElement>& Root : SyntheticRoot->Children)
	{
		if (!Root.IsValid() || !Root->bVisible)
		{
			continue;
		}
		for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
		{
			if (!Child.IsValid() || !Child->bVisible)
			{
				continue;
			}
			if (!Child->ElementName.StartsWith(TEXT("RootGameplay_")))
			{
				continue;
			}
			if (Child->ElementName == TEXT("RootGameplay_SmartBox_Field"))
			{
				continue;
			}
			const FIntPoint O = Child->GetScreenOrigin();
			if (CanvasX >= O.X && CanvasY >= O.Y
				&& CanvasX < O.X + Child->Width && CanvasY < O.Y + Child->Height)
			{
				return true;
			}
		}
	}
	return false;
}

void UACEUIElementManager::SaveFloatyLayout() const
{
	if (!GConfig || !SyntheticRoot.IsValid())
	{
		return;
	}
	const TCHAR* Section = TEXT("ACEClient.DatHUD");
	// Bump when floaty placement semantics change (drags are relative to the anchor
	// scheme; stale offsets from an older scheme displace panels / hide tab strips).
	GConfig->SetInt(Section, TEXT("LayoutSchema"), FloatyLayoutSchema, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("UiLocked"), bUiLocked, GGameUserSettingsIni);
	for (const TSharedPtr<FACEUIElement>& Root : SyntheticRoot->Children)
	{
		if (!Root.IsValid())
		{
			continue;
		}
		for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
		{
			if (!Child.IsValid() || !IsSavedLayoutWindow(Child->ElementName))
			{
				continue;
			}
			const FString Key = Child->ElementName;
			GConfig->SetInt(Section, *(Key + TEXT("_DragX")), Child->UserDragX, GGameUserSettingsIni);
			GConfig->SetInt(Section, *(Key + TEXT("_DragY")), Child->UserDragY, GGameUserSettingsIni);
			// Vitals/indicators are fixed-size — do not persist a leftover resize height.
			const int32 ResizeH = IsFixedSizeFloaty(Key) ? 0 : Child->UserResizeH;
			GConfig->SetInt(Section, *(Key + TEXT("_ResizeH")), ResizeH, GGameUserSettingsIni);
			GConfig->SetInt(Section, *(Key + TEXT("_ResizeW")), Child->UserResizeW, GGameUserSettingsIni);
		}
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UACEUIElementManager::LoadFloatyLayout()
{
	if (!GConfig || !SyntheticRoot.IsValid())
	{
		return;
	}
	const TCHAR* Section = TEXT("ACEClient.DatHUD");
	bool bLocked = bUiLocked;
	if (GConfig->GetBool(Section, TEXT("UiLocked"), bLocked, GGameUserSettingsIni))
	{
		// Loading must not save the defaults over the persisted geometry first.
		bUiLocked = bLocked;
		SyncLockedChromeVisibility();
	}
	// Saved drags from an older anchor scheme compose against the wrong base position —
	// discard them (fresh values are written on the next SaveFloatyLayout).
	int32 SavedSchema = 0;
	GConfig->GetInt(Section, TEXT("LayoutSchema"), SavedSchema, GGameUserSettingsIni);
	if (SavedSchema != FloatyLayoutSchema)
	{
		SaveFloatyLayout();
		return;
	}
	for (const TSharedPtr<FACEUIElement>& Root : SyntheticRoot->Children)
	{
		if (!Root.IsValid())
		{
			continue;
		}
		for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
		{
			if (!Child.IsValid() || !IsSavedLayoutWindow(Child->ElementName))
			{
				continue;
			}
			const FString Key = Child->ElementName;
			int32 DragX = 0, DragY = 0, ResizeH = 0;
			GConfig->GetInt(Section, *(Key + TEXT("_DragX")), DragX, GGameUserSettingsIni);
			GConfig->GetInt(Section, *(Key + TEXT("_DragY")), DragY, GGameUserSettingsIni);
			if (!IsFixedSizeFloaty(Key))
			{
				GConfig->GetInt(Section, *(Key + TEXT("_ResizeH")), ResizeH, GGameUserSettingsIni);
			}
			Child->UserDragX = DragX;
			Child->UserDragY = DragY;
			Child->UserResizeH = ResizeH;
			Child->UserResizeW = 0;
			GConfig->GetInt(Section, *(Key + TEXT("_ResizeW")), Child->UserResizeW, GGameUserSettingsIni);
			if (Child->UserResizeW != 0 && Child->AuthoredWidth < 0) Child->AuthoredWidth = Child->Width;
			Child->RecomputeLayoutOffset();
			if (ResizeH != 0 || Child->UserResizeW != 0 || Child->AuthoredHeight >= 0)
			{
				ApplyFloatyResizeLayout(Child);
			}
		}
	}
}

FIntPoint UACEUIElementManager::GetScreenLayoutSize() const
{
	for (const auto& Root : Roots)
		if (Root && Root->ElementName == TEXT("RootGameplay_Field")) return FIntPoint(Root->Width, Root->Height);
	return FIntPoint::ZeroValue;
}

FString UACEUIElementManager::ExportScreenLayout() const
{
	FString Text = TEXT("# AC:Unreal / AC:VR UI geometry in logical canvas pixels.\n");
	for (const auto& Window : ACERetailUILayout::Windows)
		if (const auto Node = FindElementByName(Window.Element))
			Text += FString::Printf(TEXT("<%s> X:%d Y: %d W: %d H: %d\n"), Window.Tag,
				Node->GetDrawX(), Node->GetDrawY(), Node->Width, Node->Height);
	return Text;
}

bool UACEUIElementManager::ImportScreenLayout(const FString& Text, FString& Error)
{
	TArray<ACERetailUILayout::FRect> Rects;
	if (!ACERetailUILayout::Parse(Text, Rects, Error)) return false;
	const FIntPoint Size = GetScreenLayoutSize();
	if (Size.X <= 0 || Size.Y <= 0) { Error = TEXT("UI layouts can only be loaded while playing a character."); return false; }
	CancelPointerCapture();
	ApplyEdgeAnchoredLayout(Size.X, Size.Y);
	for (const auto& Rect : Rects)
	{
		// Unreal always renders the world to the full viewport, particularly in VR.
		// Accept retail's SBOX record without clipping/repositioning the 3D view.
		if (Rect.Tag == TEXT("SBOX")) continue;
		for (const auto& Window : ACERetailUILayout::Windows)
		{
			if (Rect.Tag != Window.Tag) continue;
			const auto Node = FindElementByName(Window.Element);
			if (!Node) break;
			const int32 W = FMath::Clamp(Rect.W, Node->MinWidth, FMath::Max(Node->MinWidth, FMath::Min(Node->MaxWidth, Size.X)));
			const int32 H = IsFixedSizeFloaty(Node->ElementName) ? Node->Height
				: FMath::Clamp(Rect.H, Node->MinHeight, FMath::Max(Node->MinHeight, FMath::Min(Node->MaxHeight, Size.Y)));
			if (Node->AuthoredWidth < 0) Node->AuthoredWidth = Node->LayoutAuthoredW >= 0 ? Node->LayoutAuthoredW : Node->Width;
			if (Node->AuthoredHeight < 0) Node->AuthoredHeight = Node->LayoutAuthoredH >= 0 ? Node->LayoutAuthoredH : Node->Height;
			Node->UserResizeW = W - Node->AuthoredWidth;
			Node->UserResizeH = H - Node->AuthoredHeight;
			ApplyFloatyResizeLayout(Node);
			// Clamp hidden windows too, so opening one later cannot strand it offscreen.
			const int32 X = FMath::Clamp(Rect.X, 0, FMath::Max(0, Size.X - Node->Width));
			const int32 Y = FMath::Clamp(Rect.Y, 0, FMath::Max(0, Size.Y - Node->Height));
			Node->UserDragX += X - Node->GetDrawX();
			Node->UserDragY += Y - Node->GetDrawY();
			Node->RecomputeLayoutOffset();
			break;
		}
	}
	SaveFloatyLayout();
	return true;
}

void UACEUIElementManager::RegisterElementFactory(uint32 Type, FElementFactory Factory)
{
	Factories.Add(Type, MoveTemp(Factory));
}

TSharedPtr<FACEUIElement> UACEUIElementManager::CreateElementByType(
	uint32 Type, uint32 ElementId, const FString& DebugName)
{
	if (const FElementFactory* Found = Factories.Find(Type))
	{
		TSharedPtr<FACEUIElement> El = (*Found)(ElementId, Type);
		if (El.IsValid() && !DebugName.IsEmpty())
		{
			El->DebugName = DebugName;
		}
		return El;
	}
	if (const FElementFactory* Field = Factories.Find(ACEUI::ElementType::Field))
	{
		TSharedPtr<FACEUIElement> El = (*Field)(ElementId, Type);
		if (El.IsValid())
		{
			El->Type = Type;
			El->DebugName = DebugName.IsEmpty()
				? FString::Printf(TEXT("Unregistered_0x%08X"), Type)
				: DebugName;
		}
		return El;
	}
	return nullptr;
}
