#include "UI/ACEUICanvasWidget.h"
#include "ACEPlayerController.h"
#include "ACEInputBindings.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICharSelectBinder.h"
#include "UI/ACEUICharGenBinder.h"
#include "UI/ACEUITypes.h"
#include "Components/Border.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Slate stores canvas Z order as float: keep adjacent layers below 2^24.
    constexpr int32 WindowPaintStride = 262144;
    constexpr int32 WindowContentOffset = 131072;
    constexpr int32 GlobalOverlayBase = 8000000;

    TSharedPtr<FACEUIElement> RetailWindow(const TSharedPtr<FACEUIElement>& Element)
    {
        for (auto Node = Element; Node; Node = Node->Parent.Pin())
        {
            const auto Parent = Node->Parent.Pin();
            if (Parent && Parent->ElementName == TEXT("RootGameplay_Field")) return Node;
        }
        return nullptr;
    }

    int32 WindowPaintBase(const TSharedPtr<FACEUIElement>& Window)
    {
        if (!Window || Window->ElementName == TEXT("RootGameplay_SmartBox_Field")) return 0;
        const auto Parent = Window->Parent.Pin();
        const int32 Index = Parent ? Parent->Children.IndexOfByKey(Window) : INDEX_NONE;
        return Index == INDEX_NONE ? 0 : (Index + 1) * WindowPaintStride;
    }

	bool IsMeterFillLayer(const TSharedPtr<FACEUIElement>& Element)
	{
		if (!Element.IsValid())
		{
			return false;
		}
		TSharedPtr<FACEUIElement> Parent = Element->Parent.Pin();
		if (!Parent.IsValid() || Parent->Type != ACEUI::ElementType::Meter)
		{
			return false;
		}
		if (Element->ElementName == TEXT("meter_background"))
		{
			return false;
		}
		if (Element->ElementName.Contains(TEXT("Label"))
			|| Element->ElementName == TEXT("Powerbar_Text")
			|| Element->Type == ACEUI::ElementType::Text)
		{
			return false;
		}
		// Retail: unnamed (or non-background) sibling holds the fill left/middle/right art.
		return true;
	}

	float ResolveMeterFillFraction(const TSharedPtr<FACEUIElement>& Element)
	{
		for (TSharedPtr<FACEUIElement> P = Element; P.IsValid(); P = P->Parent.Pin())
		{
			if (P->Type == ACEUI::ElementType::Meter && P->MeterFillFraction >= 0.f)
			{
				return FMath::Clamp(P->MeterFillFraction, 0.f, 1.f);
			}
		}
		return -1.f;
	}
}

void UACEUICanvasWidget::InitializeCanvas(UACEUIElementManager* InManager)
{
	Manager = InManager;
}

void UACEUICanvasWidget::SetResourceResolver(UACEUIResourceResolver* InResolver)
{
	ResourceResolver = InResolver;
}

void UACEUICanvasWidget::SetGameplayBinder(UACEUIGameplayBinder* InBinder)
{
	GameplayBinder = InBinder;
}

void UACEUICanvasWidget::SetCharSelectBinder(UACEUICharSelectBinder* InBinder)
{
	CharSelectBinder = InBinder;
}

TSharedRef<SWidget> UACEUICanvasWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		return SNullWidget::NullWidget;
	}
	// A world-space widget can recreate Slate after leaving the viewport. Keep the
	// UObject tree: binders own labels and brushes attached to this element layer.
	if (RootCanvas && ElementLayer && WidgetTree->RootWidget == RootCanvas)
		return Super::RebuildWidget();
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ACEUIRoot"));
	WidgetTree->RootWidget = RootCanvas;
	CharacterBackdrop=WidgetTree->ConstructWidget<UBorder>();
	CharacterBackdrop->SetBrush(FSlateColorBrush(FLinearColor::Black));
	CharacterBackdrop->SetVisibility(ESlateVisibility::Collapsed);
	RootCanvas->AddChild(CharacterBackdrop);
	if (auto* BackdropSlot=Cast<UCanvasPanelSlot>(CharacterBackdrop->Slot))
	{
		BackdropSlot->SetAnchors(FAnchors(0,0,1,1)); BackdropSlot->SetOffsets(FMargin(0)); BackdropSlot->SetZOrder(0);
	}

	ElementLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ACEUIElementLayer"));
	ElementLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	RootCanvas->AddChild(ElementLayer);
	if (UCanvasPanelSlot* LayerSlot = Cast<UCanvasPanelSlot>(ElementLayer->Slot))
	{
		LayerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		LayerSlot->SetOffsets(FMargin(0.f));
		LayerSlot->SetZOrder(1);
	}

	ImageWidgets.Reset();
	PaintStates.Reset();
	return Super::RebuildWidget();
}

void UACEUICanvasWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// Visible so empty regions hit this widget and forward to the element manager.
	// HitTestInvisible image chrome alone would let all clicks fall through to the world.
	SetVisibility(ESlateVisibility::Visible);
	// Character creation handles name editing on this canvas. In the world, keep
	// focus on the game viewport; chat focuses its EditableTextBox directly.
	SetIsFocusable(CharGenBinder != nullptr);
	UpdateCanvasLayout();
	SyncElementWidgets();
}

void UACEUICanvasWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateCanvasLayout();
    if(CharGenBinder) CharGenBinder->Tick(InDeltaTime);
	if (GameplayBinder)
	{
		GameplayBinder->TickRefresh();
	}
	if (CharSelectBinder)
	{
		CharSelectBinder->TickRefresh();
	}
	SyncElementWidgets();
	RefreshOverlayOrder();
}

FVector2D UACEUICanvasWidget::ViewportToLayout(FVector2D ViewportLocal) const
{
	const float SX = FMath::Max(KINDA_SMALL_NUMBER, LastScaleX);
	const float SY = FMath::Max(KINDA_SMALL_NUMBER, LastScaleY);
	return FVector2D(ViewportLocal.X / SX, ViewportLocal.Y / SY);
}

FVector2D UACEUICanvasWidget::LayoutToViewport(FVector2D LayoutPos) const
{
	return FVector2D(LayoutPos.X * LastScaleX, LayoutPos.Y * LastScaleY);
}

void UACEUICanvasWidget::UpdateCanvasLayout()
{
	if (CharacterBackdrop) CharacterBackdrop->SetVisibility(CharSelectBinder || CharGenBinder
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!ElementLayer || !Manager)
	{
		return;
	}
	const FVector2D Size = GetCachedGeometry().GetLocalSize();
	if (Size.X < 1.f || Size.Y < 1.f)
	{
		return;
	}
	// Character select: uniform scale so the 800×600 panel + fonts grow with resolution.
	// Gameplay stays 1:1 with edge-anchored floaties (non-uniform stretch warps chrome).
	const FVector2D Scale = Manager->GetCanvasScale(Size);
	LastScaleX = Scale.X;
	LastScaleY = Scale.Y;
	Manager->ApplyEdgeAnchoredLayout(FMath::FloorToInt(Size.X), FMath::FloorToInt(Size.Y));
	if (UCanvasPanelSlot* LayerSlot = Cast<UCanvasPanelSlot>(ElementLayer->Slot))
	{
		LayerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		LayerSlot->SetOffsets(FMargin(0.f));
	}
}

void UACEUICanvasWidget::PlaceWidgetAtElement(UWidget* Widget, const TSharedPtr<FACEUIElement>& Element,
	int32 ZOrder, const FMargin& Inset)
{
	if (!Widget || !Element.IsValid() || !ElementLayer)
	{
		return;
	}
	if (Widget->GetParent() != ElementLayer)
	{
		ElementLayer->AddChild(Widget);
	}
	const FIntPoint Origin = Element->GetScreenOrigin();
	if (UACERetailTextBlock* Text = Cast<UACERetailTextBlock>(Widget))
		Text->SetRetailElement(ResourceResolver, Element, FVector2D(LastScaleX, LastScaleY),
			FMath::Max(1.f, Element->Width - Inset.Left - Inset.Right));
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
		CanvasSlot->SetAlignment(FVector2D(0.f, 0.f));
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetPosition(FVector2D(
			(static_cast<float>(Origin.X) + Inset.Left) * LastScaleX,
			(static_cast<float>(Origin.Y) + Inset.Top) * LastScaleY));
		CanvasSlot->SetSize(FVector2D(
			FMath::Max(1.f, (static_cast<float>(Element->Width) - Inset.Left - Inset.Right) * LastScaleX),
			FMath::Max(1.f, (static_cast<float>(Element->Height) - Inset.Top - Inset.Bottom) * LastScaleY)));
		SetOverlayOrder(Widget, Element, ZOrder);
	}
}

void UACEUICanvasWidget::SetOverlayOrder(UWidget* Widget,
    const TSharedPtr<FACEUIElement>& OwnerElement, int32 LocalOrder)
{
    if (!Widget) return;
    if (auto* Text = Cast<UACERetailTextBlock>(Widget); Text && !Text->GetRetailElement())
        Text->SetRetailElement(ResourceResolver, nullptr, FVector2D(LastScaleX, LastScaleY), 0, false);
    FOverlayOrder& Order = OverlayOrders.FindOrAdd(Widget);
    const auto Window = RetailWindow(OwnerElement);
    Order.Owner = Window ? Window : OwnerElement;
    Order.bGlobal = !OwnerElement;
    Order.LocalOrder = LocalOrder;
    const int32 Base = WindowPaintBase(Window);
    const int32 Z = (Order.bGlobal ? GlobalOverlayBase : Base + (Base ? WindowContentOffset : 0)) + LocalOrder;
    if (auto* OverlaySlot = Cast<UCanvasPanelSlot>(Widget->Slot))
        if (OverlaySlot->GetZOrder() != Z) OverlaySlot->SetZOrder(Z);
}

bool UACEUICanvasWidget::IsWidgetExposedAt(const UWidget* Widget, FVector2D Absolute) const
{
    if (!Widget || !Widget->GetCachedGeometry().IsUnderLocation(Absolute)) return false;
    for (const UWidget* Node = Widget; Node; Node = Node->GetParent())
        if (!Node->IsVisible()) return false; // Collapsed overlays retain stale geometry.
    const auto* Order = OverlayOrders.Find(const_cast<UWidget*>(Widget));
    if (!Order || Order->bGlobal || !Manager) return true;
    return IsElementExposedAt(Order->Owner.Pin(), GetCachedGeometry().AbsoluteToLocal(Absolute));
}

bool UACEUICanvasWidget::IsElementExposedAt(const TSharedPtr<FACEUIElement>& Element, FVector2D CanvasLocal) const
{
    if (!Element || !Manager) return false;
    for (auto Node=Element; Node; Node=Node->Parent.Pin())
        if (!Node->bVisible) return false;
    const auto Window=RetailWindow(Element);
    if (!Window) return true;
    const FVector2D Scale=Manager->GetCanvasScale(GetCachedGeometry().GetLocalSize());
    const FVector2D P=CanvasLocal / Scale;
    return Manager->FindWindowAtCanvas(FMath::FloorToInt(P.X),FMath::FloorToInt(P.Y)) == Window;
}

void UACEUICanvasWidget::RefreshOverlayOrder()
{
    // Inventory and list contents may skip refresh while unchanged. Window raises
    // still reorder every child together, as UIRegion::DrawChildren does.
    for (auto It = OverlayOrders.CreateIterator(); It; ++It)
    {
        UWidget* Widget = It.Key().Get();
        const auto Owner = It.Value().Owner.Pin();
        if (!Widget || (!Owner && !It.Value().bGlobal)) { It.RemoveCurrent(); continue; }
        if (auto* OverlaySlot = Cast<UCanvasPanelSlot>(Widget->Slot))
        {
            const int32 Base = WindowPaintBase(RetailWindow(Owner));
            const int32 Z = (It.Value().bGlobal ? GlobalOverlayBase : Base + (Base ? WindowContentOffset : 0))
                + It.Value().LocalOrder;
            if (OverlaySlot->GetZOrder() != Z) OverlaySlot->SetZOrder(Z);
        }
    }
}

void UACEUICanvasWidget::SyncElementWidgets()
{
	if (!ElementLayer || !Manager || !ResourceResolver)
	{
		return;
	}
	const FVector2D Size = GetCachedGeometry().GetLocalSize();
	if (Size.X < 1.f || Size.Y < 1.f)
	{
		return;
	}

	TSet<uint32>& UsedIds = UsedIdsScratch;
	UsedIds.Reset();
	int32 ZOrder = 10;
	const int32 ViewW = FMath::Max(1, FMath::FloorToInt(Size.X));
	const int32 ViewH = FMath::Max(1, FMath::FloorToInt(Size.Y));
	const TSharedPtr<FACEUIElement> Synthetic = Manager->GetSyntheticRoot();
	if (Synthetic.IsValid())
	{
		for (const TSharedPtr<FACEUIElement>& Root : Synthetic->Children)
		{
			SyncElementRecursive(Root, INDEX_NONE, INDEX_NONE, 0, 0, ViewW, ViewH, ZOrder, UsedIds);
		}
	}

	for (auto It = ImageWidgets.CreateIterator(); It; ++It)
	{
		if (!UsedIds.Contains(It.Key()))
		{
			if (It.Value())
			{
				if (UWidget* ClipPanel = It.Value()->GetParent()) ClipPanel->RemoveFromParent();
			}
			PaintStates.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void UACEUICanvasWidget::SetCachedVisibility(UBorder* Border, uint32 InstanceId,
	ESlateVisibility InVisibility)
{
	if (!Border)
	{
		return;
	}
	FACEUIPaintState& State = PaintStates.FindOrAdd(InstanceId);
	if (!State.bVisibilitySet || State.Visibility != InVisibility)
	{
		State.Visibility = InVisibility;
		State.bVisibilitySet = true;
		Border->SetVisibility(InVisibility);
	}
}

void UACEUICanvasWidget::ApplyPaintState(UBorder* Border, uint32 InstanceId, UTexture2D* Texture,
	const FLinearColor& Color, int32 X, int32 Y, int32 W, int32 H, int32 Z,
	ESlateVisibility InVisibility, ESlateBrushTileType::Type TileType, FVector2D BrushImageSize, FIntRect ImageRect)
{
	if (!Border)
	{
		return;
	}
	FACEUIPaintState& State = PaintStates.FindOrAdd(InstanceId);

	// Brush carries an explicit ImageSize, so a resize also needs a re-set.
	const bool bTextureChanged = State.Texture.Get() != Texture;
	const bool bSizeChanged = State.W != W || State.H != H || State.ImageRect != ImageRect;
	const bool bTilingChanged = State.Tiling != static_cast<uint8>(TileType);
	if (bTextureChanged || bSizeChanged || bTilingChanged || !State.bHasBrush)
	{
		if (Texture)
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = (TileType != ESlateBrushTileType::NoTile
				&& BrushImageSize.X >= 1.f && BrushImageSize.Y >= 1.f)
				? BrushImageSize
				: FVector2D(ImageRect.Width(), ImageRect.Height());
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.Tiling = TileType;
			Border->SetBrush(Brush);
		}
		else
		{
			// Failed DAT resolve must not leave UBorder's default dark slab over the panel.
			FSlateBrush Clear;
			Clear.DrawAs = ESlateBrushDrawType::NoDrawType;
			Border->SetBrush(Clear);
		}
		State.Texture = Texture;
		State.Tiling = static_cast<uint8>(TileType);
		State.bHasBrush = true;
		// Force the colour push below; SetBrush resets the resolved tint.
		State.Color = FLinearColor(-1.f, -1.f, -1.f, -1.f);
	}
	if (!State.Color.Equals(Color))
	{
		State.Color = Color;
		Border->SetBrushColor(Color);
	}
	if (State.X != X || State.Y != Y || bSizeChanged || State.Z != Z)
	{
		// Clip the full image through a parent viewport. Resizing the image to the
		// visible rectangle stretches cropped art and changes tiled-border phase.
		UWidget* ClipPanel = Border->GetParent();
		if (UCanvasPanelSlot* ClipSlot = ClipPanel ? Cast<UCanvasPanelSlot>(ClipPanel->Slot) : nullptr)
		{
			ClipSlot->SetPosition(FVector2D(X, Y));
			ClipSlot->SetSize(FVector2D(W, H));
			ClipSlot->SetZOrder(Z);
		}
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Border->Slot))
		{
			CanvasSlot->SetPosition(FVector2D(ImageRect.Min.X - X, ImageRect.Min.Y - Y));
			CanvasSlot->SetSize(FVector2D(ImageRect.Width(), ImageRect.Height()));
		}
		State.ImageRect = ImageRect;
		State.X = X;
		State.Y = Y;
		State.W = W;
		State.H = H;
		State.Z = Z;
	}
	if (!State.bVisibilitySet || State.Visibility != InVisibility)
	{
		State.Visibility = InVisibility;
		State.bVisibilitySet = true;
		Border->SetVisibility(InVisibility);
	}
}

void UACEUICanvasWidget::SyncElementRecursive(
	const TSharedPtr<FACEUIElement>& Element, int32 ClipMaxWidth, int32 ClipMaxHeight,
	int32 ClipX0, int32 ClipY0, int32 ClipX1, int32 ClipY1,
	int32& InOutZOrder, TSet<uint32>& UsedIds)
{
	if (!Element.IsValid())
	{
		return;
	}

	const FACEUIStateMedia* RetailState = Element->ResolvePaintState(
		Manager && Manager->GetCaptureElement() == Element,
		Manager && Manager->GetHoverElement() == Element,
		Manager && Manager->GetFocusElement() == Element);
	if (!Element->IsPaintVisible()) return;
    if(Element->ElementName==TEXT("RootCharGenDialog"))InOutZOrder=2000;
    if(Element->ElementName==TEXT("RootCharGenTooltip"))InOutZOrder=3000;
    if (const auto Window = RetailWindow(Element); Window == Element)
        InOutZOrder = WindowPaintBase(Window);

	const bool bStateMedia = RetailState && RetailState->bHasMedia;
	const uint32 PaintImage = bStateMedia ? RetailState->ImageFileId : Element->ImageFileId;
	const uint32 PaintAlpha = bStateMedia ? RetailState->AlphaFileId : Element->AlphaFileId;
	const uint32 PaintMode = bStateMedia ? RetailState->DrawMode : Element->DrawMode;

	const FIntPoint OriginLogical = Element->GetScreenOrigin();
	const int32 ElX0 = FMath::RoundToInt(static_cast<float>(OriginLogical.X) * LastScaleX);
	const int32 ElY0 = FMath::RoundToInt(static_cast<float>(OriginLogical.Y) * LastScaleY);
	const int32 ElX1 = FMath::RoundToInt(static_cast<float>(OriginLogical.X + Element->Width) * LastScaleX);
	const int32 ElY1 = FMath::RoundToInt(static_cast<float>(OriginLogical.Y + Element->Height) * LastScaleY);

	// LayoutDesc merges sometimes place children outside their parent (e.g. ChatLog_RightEndCap
	// at x=397 under a 306-wide ChatPanelTextEntry). Skip painting those orphans.
	if (TSharedPtr<FACEUIElement> Parent = Element->Parent.Pin())
	{
		if (Element->Width > 0 && Element->Height > 0
			&& (Element->X >= Parent->Width || Element->Y >= Parent->Height
				|| Element->X + Element->Width <= 0 || Element->Y + Element->Height <= 0))
		{
			return;
		}
	}

	// Retail UIRegion clips children to the parent box (PanelPages, floaty frames, chat log).
	int32 NextClipX0 = ClipX0;
	int32 NextClipY0 = ClipY0;
	int32 NextClipX1 = ClipX1;
	int32 NextClipY1 = ClipY1;
	const bool bTightenClip = Element->ElementName.StartsWith(TEXT("RootGameplay_"))
		|| Element->ElementName == TEXT("PanelPages")
		|| Element->ElementName.EndsWith(TEXT("Panel_Field"))
		|| Element->ElementName.EndsWith(TEXT("ExamineUI"))
		|| Element->ElementName == TEXT("ChatLogField")
		|| Element->ElementName == TEXT("ChatEntryField")
		|| Element->ElementName == TEXT("ChatPanelTextEntry");
	if (bTightenClip && Element->Width > 0 && Element->Height > 0)
	{
		NextClipX0 = FMath::Max(ClipX0, ElX0);
		NextClipY0 = FMath::Max(ClipY0, ElY0);
		NextClipX1 = FMath::Min(ClipX1, ElX1);
		NextClipY1 = FMath::Min(ClipY1, ElY1);
		if (NextClipX0 >= NextClipX1 || NextClipY0 >= NextClipY1)
		{
			return;
		}
	}

	int32 OwnClipW = ClipMaxWidth;
	int32 OwnClipH = ClipMaxHeight;
	int32 ChildClipW = ClipMaxWidth;
	int32 ChildClipH = ClipMaxHeight;
	int32 FillClipW = INDEX_NONE;
	int32 FillClipH = INDEX_NONE;
	const bool bMeterFillParent = Element->Type == ACEUI::ElementType::Meter
		&& Element->MeterFillFraction >= 0.f;
	if (bMeterFillParent)
	{
		const float Frac = FMath::Clamp(Element->MeterFillFraction, 0.f, 1.f);
		const bool bVertical = Element->Height > Element->Width * 1.5f;
		if (bVertical)
		{
			FillClipH = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Element->Height) * Frac));
		}
		else
		{
			FillClipW = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Element->Width) * Frac));
		}
		// Empty vial (meter_background) and toolbar empty frames on the Meter node itself
		// must stay full size. Only fill children receive FillClip.
	}
	else if (IsMeterFillLayer(Element))
	{
		const float Frac = ResolveMeterFillFraction(Element);
		if (Frac >= 0.f)
		{
			const bool bVertical = Element->Height > Element->Width * 1.5f;
			if (bVertical)
			{
				ChildClipH = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Element->Height) * Frac));
				ChildClipW = INDEX_NONE;
			}
			else
			{
				ChildClipW = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Element->Width) * Frac));
				ChildClipH = INDEX_NONE;
			}
			OwnClipW = ChildClipW;
			OwnClipH = ChildClipH;
		}
	}

	bool bChildrenDrawn = false;
	auto RecurseChildren = [&]()
	{
		if (bChildrenDrawn) return;
		bChildrenDrawn = true;
		for (const TSharedPtr<FACEUIElement>& Child : Element->Children)
		{
			int32 PassW = ChildClipW;
			int32 PassH = ChildClipH;
			if (bMeterFillParent && Child.IsValid())
			{
				if (IsMeterFillLayer(Child))
				{
					PassW = FillClipW;
					PassH = FillClipH;
				}
				else
				{
					PassW = INDEX_NONE;
					PassH = INDEX_NONE;
				}
			}
			SyncElementRecursive(Child, PassW, PassH, NextClipX0, NextClipY0, NextClipX1, NextClipY1,
				InOutZOrder, UsedIds);
		}
	};

	if (Element->bDrawAfterChildren) RecurseChildren();
	if (PaintImage != 0 && Element->Width > 0 && Element->Height > 0
		&& Element->InstanceId != 0)
	{
		// Gameplay root is a transparent world viewport — skip residual full-screen imageFile.
		// CharacterManagementField (and other fullscreen modes) must paint their backdrop.
		if (Element->ElementName.Contains(TEXT("RootGameplay"))
			&& Element->Width >= ACEUI::ReferenceWidth && Element->Height >= ACEUI::ReferenceHeight)
		{
			RecurseChildren();
			return;
		}

		int32 DrawX = ElX0;
		int32 DrawY = ElY0;
		int32 DrawW = FMath::Max(1, ElX1 - ElX0);
		int32 DrawH = FMath::Max(1, ElY1 - ElY0);

		// Clip meter fill: horizontal left→right, vertical (burden) bottom→top.
		// OwnClip is fill-layer only — empty vial frames are not clipped.
		if (OwnClipW != INDEX_NONE || OwnClipH != INDEX_NONE)
		{
			// Meter box origin: Meter itself, or nearest Meter ancestor for fill children.
			TSharedPtr<FACEUIElement> MeterRoot = Element;
			if (Element->Type != ACEUI::ElementType::Meter)
			{
				for (TSharedPtr<FACEUIElement> P = Element->Parent.Pin(); P.IsValid(); P = P->Parent.Pin())
				{
					if (P->Type == ACEUI::ElementType::Meter)
					{
						MeterRoot = P;
						break;
					}
				}
			}
			const FIntPoint FillOrigin = MeterRoot.IsValid() ? MeterRoot->GetScreenOrigin() : OriginLogical;
			const int32 RootH = MeterRoot.IsValid() ? MeterRoot->Height : Element->Height;
			if (OwnClipW != INDEX_NONE)
			{
				const int32 LocalX = OriginLogical.X - FillOrigin.X;
				if (LocalX >= OwnClipW)
				{
					RecurseChildren();
					return;
				}
				DrawW = FMath::Min(DrawW, FMath::RoundToInt(static_cast<float>(OwnClipW - LocalX) * LastScaleX));
				if (DrawW <= 0)
				{
					RecurseChildren();
					return;
				}
			}
			if (OwnClipH != INDEX_NONE)
			{
				const int32 LocalY = OriginLogical.Y - FillOrigin.Y;
				const int32 HiddenTop = FMath::Max(0, RootH - OwnClipH);
				if (LocalY + Element->Height <= HiddenTop)
				{
					RecurseChildren();
					return;
				}
				if (LocalY < HiddenTop)
				{
					const int32 Cut = FMath::RoundToInt(static_cast<float>(HiddenTop - LocalY) * LastScaleY);
					DrawY += Cut;
					DrawH -= Cut;
				}
				if (DrawH <= 0)
				{
					RecurseChildren();
					return;
				}
			}
		}

		// Intersect with UIRegion clip rect.
		const int32 VisX0 = FMath::Max(DrawX, ClipX0);
		const int32 VisY0 = FMath::Max(DrawY, ClipY0);
		const int32 VisX1 = FMath::Min(DrawX + DrawW, ClipX1);
		const int32 VisY1 = FMath::Min(DrawY + DrawH, ClipY1);
		if (VisX0 >= VisX1 || VisY0 >= VisY1)
		{
			RecurseChildren();
			return;
		}
		DrawX = VisX0;
		DrawY = VisY0;
		DrawW = VisX1 - VisX0;
		DrawH = VisY1 - VisY0;

		UsedIds.Add(Element->InstanceId);

		UBorder* Border = ImageWidgets.FindRef(Element->InstanceId);
		if (!Border)
		{
			Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Border->SetPadding(FMargin(0.f));
			UCanvasPanel* ClipPanel = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
			ClipPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
			ClipPanel->SetClipping(EWidgetClipping::ClipToBoundsAlways);
			ElementLayer->AddChild(ClipPanel);
			ClipPanel->AddChild(Border);
			ImageWidgets.Add(Element->InstanceId, Border);
			PaintStates.Remove(Element->InstanceId);
		}

		UTexture2D* Tex = Element->bFloatingHealthArtwork ? ResourceResolver->ResolveFloatingHealthTexture(PaintImage)
			: ResourceResolver->ResolveTexture(PaintImage, PaintAlpha);
		FLinearColor Tint = Tex ? Element->ImageTint : FLinearColor::Transparent;
		// Retail ChatInterface fades its background object. Applying that opacity
		// separately to every child lets the gold track show through the blue thumb.
		Tint.A *= Element->ImageOpacity;
		if (Element->ElementName == TEXT("ChatLogField"))
			for (auto Parent=Element->Parent.Pin(); Parent; Parent=Parent->Parent.Pin())
				Tint.A *= Parent->ImageOpacity;
		// Sibling ZLevel is already sorted; adding it again breaks parent/child paint order.
		const int32 PaintZ = InOutZOrder;

		// LayoutDesc drawMode 1 repeats the source art across the element — panel slabs
		// (0x06004CC2 over 300x600), window borders, bevels and edge strips are all small
		// tiles in retail. drawMode 3 stretches, which a plain Image brush already does.
		ESlateBrushTileType::Type TileType = ESlateBrushTileType::NoTile;
		FVector2D TileSize = FVector2D::ZeroVector;
		if (PaintMode == 1 && Tex)
		{
			const float TexW = FMath::Max(1.f, static_cast<float>(Tex->GetSizeX()) * LastScaleX);
			const float TexH = FMath::Max(1.f, static_cast<float>(Tex->GetSizeY()) * LastScaleY);
			const bool bTileX = static_cast<float>(ElX1 - ElX0) > TexW;
			const bool bTileY = static_cast<float>(ElY1 - ElY0) > TexH;
			if (bTileX && bTileY)
			{
				TileType = ESlateBrushTileType::Both;
			}
			else if (bTileX)
			{
				TileType = ESlateBrushTileType::Horizontal;
			}
			else if (bTileY)
			{
				TileType = ESlateBrushTileType::Vertical;
			}
			TileSize = FVector2D(TexW, TexH);
		}
		ApplyPaintState(Border, Element->InstanceId, Tex, Tint, DrawX, DrawY, DrawW, DrawH, PaintZ,
			ESlateVisibility::HitTestInvisible,
			TileType, TileSize, FIntRect(ElX0, ElY0, ElX1, ElY1));

		++InOutZOrder;
	}

	RecurseChildren();
}

void UACEUICanvasWidget::SetVRPointerFeedback(FVector2D Point, bool Pressed)
{
	if (Pressed != bVRPointerDown) VRClickFlashUntil = FPlatformTime::Seconds() + .2;
	bVRPointerDown = Pressed;
	bVRPointerVisible = GameplayBinder && GameplayBinder->GetVRPointerFeedback(Point, VRPointerBounds, VRPointerLabel, VRPointerHint, VRPointerGuid);
	InvalidateLayoutAndVolatility();
}

int32 UACEUICanvasWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool Enabled) const
{
	Layer = Super::NativePaint(Args, Geometry, Culling, Elements, Layer, Style, Enabled);
	if (!bVRPointerVisible) return Layer;
	const FVector2D A = VRPointerBounds.Min, B = VRPointerBounds.Max;
	// Small controls/items receive a strong outline, without outlining an entire
	// 1280px canvas when the ray is simply over an empty background.
	if (B.X - A.X > 500 || B.Y - A.Y > 300) return Layer;
	const FLinearColor Color = bVRPointerDown || FPlatformTime::Seconds() < VRClickFlashUntil
		? FLinearColor(1.f, .65f, .05f) : FLinearColor(.05f, .85f, 1.f);
	TArray<FVector2D> Points = {A, FVector2D(B.X, A.Y), B, FVector2D(A.X, B.Y), A};
	FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 3.f);
	const float Remaining = GameplayBinder ? GameplayBinder->GetVRSecondClickFraction(VRPointerGuid) : 0;
	if (Remaining > 0)
	{
		FSlateDrawElement::MakeBox(Elements, ++Layer,
			Geometry.ToPaintGeometry(FVector2D((B.X - A.X) * Remaining, 4), FSlateLayoutTransform(FVector2D(A.X, B.Y - 4))),
			FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, FLinearColor(.25f, 1.f, .35f));
	}
	return Layer;
}

void UACEUICanvasWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	Super::NativeOnMouseCaptureLost(Event);
	CancelPointerGestures();
}

void UACEUICanvasWidget::CancelPointerGestures()
{
	ResetPointerOwnership(); bVRPointerDown = false;
	if (GameplayBinder) GameplayBinder->CancelPointerGestures();
	if (CharGenBinder) CharGenBinder->MouseUp();
	if (Manager) Manager->CancelPointerCapture();
}

FReply UACEUICanvasWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (PressedPointer != INDEX_NONE && (PressedPointer != InMouseEvent.GetPointerIndex() || PressedUser != InMouseEvent.GetUserIndex())) return FReply::Unhandled();
	const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D ViewportSize = InGeometry.GetLocalSize();
	if(CharGenBinder) CharGenBinder->MouseMove(ViewportToLayout(Local));
	if (GameplayBinder)
	{
		GameplayBinder->UpdateInventoryDrag(Local);
		GameplayBinder->UpdateSpellDrag(Local);
		GameplayBinder->UpdateCombatPowerDrag(Local);
		GameplayBinder->UpdateScrollbarDrag(Local);
	}
	if (Manager)
	{
		// Viewport pixels — ViewportToCanvas applies char-select uniform scale.
		Manager->NotifyMouseMove(Local, ViewportSize);
	}
	return FReply::Unhandled();
}

FReply UACEUICanvasWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (PressedPointer != INDEX_NONE && (PressedPointer != InMouseEvent.GetPointerIndex() || PressedUser != InMouseEvent.GetUserIndex())) return FReply::Handled();
	PressedPointer = InMouseEvent.GetPointerIndex(); PressedUser = InMouseEvent.GetUserIndex();
	const bool bLeft = InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
	const bool bRight = InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
	const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	if (GameplayBinder && bLeft && GameplayBinder->TryHandleModalPopupClick(InMouseEvent.GetScreenSpacePosition()))
		return FReply::Handled();

	if (GameplayBinder && bLeft)
	{
		if (GameplayBinder->TryBeginScrollbarDrag(Local))
		{
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
		if (GameplayBinder->TryBeginCombatPowerDrag(Local))
		{
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
		if (GameplayBinder->TryBeginSpellDrag(Local))
		{
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
		// Inventory / doll: pending drag (activate after move); click/double-click on release.
		if (GameplayBinder->TryBeginInventoryDrag(Local))
		{
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	if (GameplayBinder && (bLeft || bRight))
	{
		if (GameplayBinder->TryHandleOverlayClick(Local, bRight))
		{
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	const FVector2D LayoutPos = ViewportToLayout(Local);
	if(CharGenBinder && bLeft && CharGenBinder->MouseDown(LayoutPos)) return FReply::Handled().SetUserFocus(TakeWidget()).CaptureMouse(TakeWidget());
	const FVector2D ViewportSize = InGeometry.GetLocalSize();
	if (CharSelectBinder && bLeft)
	{
		if (CharSelectBinder->TryHandleOverlayClick(LayoutPos))
		{
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	if (Manager)
	{
		Manager->NotifyMouseDown(Local, ViewportSize, InMouseEvent.GetEffectingButton());
		if (CharGenBinder)
		{
			// Navigation and ordinary DAT buttons must retain name-entry keyboard focus.
			return FReply::Handled().SetUserFocus(TakeWidget()).CaptureMouse(TakeWidget());
		}
		if (Manager->GetActiveElement().IsValid() || Manager->GetCaptureElement().IsValid())
		{
			// Keep chat focus only when clicking the entry chrome itself.
			bool bChatChrome = false;
			for (TSharedPtr<FACEUIElement> Cur = Manager->GetActiveElement(); Cur.IsValid(); Cur = Cur->Parent.Pin())
			{
				if (Cur->ElementName == TEXT("ChatPanelTextEntry")
					|| Cur->ElementName == TEXT("ChatEntryField")
					|| Cur->ElementName == TEXT("ChatPanelTextEntryField"))
				{
					bChatChrome = true;
					break;
				}
			}
			if (GameplayBinder && !bChatChrome)
			{
				GameplayBinder->ClearChatEntryFocus();
			}
			else if (GameplayBinder && bChatChrome)
			{
				// Keep chat focus; do not restore viewport.
			}
			else if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			}
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	if (GameplayBinder)
	{
		GameplayBinder->ClearChatEntryFocus();
	}
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
	return FReply::Unhandled();
}

FReply UACEUICanvasWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Slate sends DoubleClick instead of the second MouseButtonDown. The item
	// interaction state machine needs that press to complete use on release.
	return NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UACEUICanvasWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (PressedPointer != INDEX_NONE && (PressedPointer != InMouseEvent.GetPointerIndex() || PressedUser != InMouseEvent.GetUserIndex())) return FReply::Unhandled();
	PressedPointer = PressedUser = INDEX_NONE;
	if(CharGenBinder) CharGenBinder->MouseUp();
	const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	if (GameplayBinder && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (GameplayBinder->TryFinishScrollbarDrag())
		{
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (GameplayBinder->TryFinishCombatPowerDrag(Local))
		{
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (GameplayBinder->TryFinishSpellDrag(Local))
		{
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (GameplayBinder->TryFinishInventoryDrag(Local))
		{
			return FReply::Handled().ReleaseMouseCapture();
		}
	}
	const FVector2D ViewportSize = InGeometry.GetLocalSize();
	if (Manager)
	{
		Manager->NotifyMouseUp(Local, ViewportSize, InMouseEvent.GetEffectingButton(), CharGenBinder != nullptr);
		if (TakeWidget()->HasMouseCaptureByUser(InMouseEvent.GetUserIndex(), InMouseEvent.GetPointerIndex()))
		{
			return FReply::Handled().ReleaseMouseCapture();
		}
	}
	return FReply::Unhandled();
}

FReply UACEUICanvasWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if(CharGenBinder && CharGenBinder->MouseWheel(ViewportToLayout(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())),InMouseEvent.GetWheelDelta())) return FReply::Handled();
	if (GameplayBinder)
	{
		const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const int32 ChatWindow=GameplayBinder->ChatWindowAtPointer(Local);
		if (ChatWindow!=INDEX_NONE)
		{
			// Slate wheel delta: positive = scroll up content.
			GameplayBinder->ScrollChatLog(-InMouseEvent.GetWheelDelta() * 28.f,ChatWindow);
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverShortcutBar(Local))
		{
			GameplayBinder->ScrollShortcutBar(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverInventoryGrid(Local))
		{
			GameplayBinder->ScrollInventoryGrid(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverPackList(Local))
		{
			GameplayBinder->ScrollPackList(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverStatList(Local))
		{
			GameplayBinder->ScrollStatList(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverSpellbookList(Local))
		{
			GameplayBinder->ScrollSpellbook(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverOptionsList(Local))
		{
			GameplayBinder->ScrollOptionsList(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverComponentList(Local))
		{
			GameplayBinder->ScrollComponentList(InMouseEvent.GetWheelDelta(), Local);
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverQuestList(Local))
		{
			GameplayBinder->ScrollQuestList(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverHouseList(Local))
		{
			GameplayBinder->ScrollHouseList(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverEffectsList(Local))
		{
			GameplayBinder->ScrollEffectsList(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
		if (GameplayBinder->IsPointerOverExternalContainer(Local))
		{
			GameplayBinder->ScrollExternalContainer(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (AACEPlayerController* AcePc = Cast<AACEPlayerController>(PC))
		{
			AcePc->ApplyCameraWheelZoom(InMouseEvent.GetWheelDelta());
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply UACEUICanvasWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Buttons consume Enter before the viewport sees it. Route it to chat during
	// the preview phase, while leaving text commits and key binding edits alone.
	const auto Focused = FSlateApplication::Get().GetUserFocusedWidget(InKeyEvent.GetUserIndex());
	const bool bEditingText = Focused && (Focused->GetTypeAsString().Contains(TEXT("EditableText"))
		|| Focused->GetTypeAsString().Contains(TEXT("RetailTextEntry")));
	if (GameplayBinder && InKeyEvent.GetKey() == EKeys::Escape && !InKeyEvent.IsRepeat()
		&& !bEditingText && !ACEInputBindings::IsEditing())
	{
		GameplayBinder->HandleEscape();
		return FReply::Handled();
	}
	if (GameplayBinder && (InKeyEvent.GetKey() == EKeys::Enter || InKeyEvent.GetKey() == EKeys::Slash)
		&& !InKeyEvent.IsRepeat() && !InKeyEvent.IsAltDown() && !InKeyEvent.IsControlDown()
		&& !GameplayBinder->IsChatEntryFocused() && !bEditingText && !ACEInputBindings::IsEditing())
	{
		GameplayBinder->FocusChatEntry();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UACEUICanvasWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if(CharGenBinder && CharGenBinder->KeyDown(InKeyEvent)) return FReply::Handled();
	if (GameplayBinder && (InKeyEvent.GetKey() == EKeys::Enter || InKeyEvent.GetKey() == EKeys::Slash)
		&& !InKeyEvent.IsRepeat() && !InKeyEvent.IsAltDown() && !InKeyEvent.IsControlDown()
		&& !ACEInputBindings::IsEditing())
	{
		// When the entry already has focus, EditableTextBox OnTextCommitted sends the line.
		if (GameplayBinder->IsChatEntryFocused())
		{
			return FReply::Unhandled();
		}
		GameplayBinder->FocusChatEntry();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UACEUICanvasWidget::NativeOnKeyChar(const FGeometry& Geometry,const FCharacterEvent& Event)
{
    if(CharGenBinder && CharGenBinder->KeyChar(Event))return FReply::Handled();
    return Super::NativeOnKeyChar(Geometry,Event);
}
