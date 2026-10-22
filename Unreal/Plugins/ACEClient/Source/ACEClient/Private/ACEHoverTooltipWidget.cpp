#include "ACEHoverTooltipWidget.h"
#include "ACEDatSubsystem.h"
#include "ACERuntimeOptions.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"

void UACEHoverTooltipWidget::SetWidgetTooltip(UWidget* Widget, const FText& Text, uint32 TemplateId)
{
	if (!Widget) return;
	auto* Tip = Cast<UACEHoverTooltipWidget>(Widget->GetToolTip());
	if (Widget->GetToolTipText().EqualTo(Text) && Tip && Tip->CurrentTemplate == TemplateId) return;
	Widget->SetToolTipText(Text);
	if (Text.IsEmpty()) { Widget->SetToolTip(nullptr); return; }
	if (!Tip)
	{
		Tip = NewObject<UACEHoverTooltipWidget>(Widget);
		Tip->Initialize();
		if (auto* Canvas = Widget->GetTypedOuter<UACEUICanvasWidget>())
			Tip->SetResources(Canvas->GetResourceResolver());
	}
	Tip->SetTooltipText(Text.ToString(), TemplateId);
	// SetToolTipText replaces the cached Slate tooltip, even when UMG still
	// remembers our custom widget. Reattach after every changed description.
	Widget->SetToolTip(Tip);
	Tip->ProtectOwnerFromCursorOverlap();
}

void UACEHoverTooltipWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// SetWidgetTooltip also runs before the owner's Slate widget exists.
	// UMG constructs its tooltip during owner property synchronization.
	ProtectOwnerFromCursorOverlap();
}

FSlateRect UACEHoverTooltipWidget::GetCursorExclusionRect(const FVector2D& CursorPixels)
{
	// Software cursor widgets are centered on the pointer, with the retail
	// artwork offset inside them to preserve its hotspot. Reserve their complete
	// bounds rather than just the OS pointer tip (Slate's default tooltip offset).
	FVector2D Extent(32.f * ACERuntimeOptions::Get(TEXT("CursorScale")));
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetIsUsingSoftwareCursorWidgets())
	{
		for (auto Cursor : {EMouseCursor::Default, EMouseCursor::CardinalCross, EMouseCursor::ResizeUpDown,
			EMouseCursor::ResizeLeftRight, EMouseCursor::ResizeSouthEast, EMouseCursor::ResizeSouthWest})
		{
			if (auto Widget = GEngine->GameViewport->GetSoftwareCursorWidget(Cursor))
			{
				const FVector2D HalfSize = Widget->GetDesiredSize() * .5f;
				Extent.X = FMath::Max(Extent.X, HalfSize.X);
				Extent.Y = FMath::Max(Extent.Y, HalfSize.Y);
			}
		}
	}
	return FSlateRect(CursorPixels - Extent, CursorPixels + Extent);
}

void UACEHoverTooltipWidget::ProtectOwnerFromCursorOverlap()
{
	if (auto* Owner = Cast<UWidget>(GetOuter()))
		if (auto SlateOwner = Owner->GetCachedWidget())
		{
			SlateOwner->EnableToolTipForceField(true);
			SlateOwner->SetToolTipForceFieldExpansion(TAttribute<TOptional<FSlateRect>>::CreateLambda([]
			{
				if (!FSlateApplication::IsInitialized()) return TOptional<FSlateRect>();
				return TOptional<FSlateRect>(GetCursorExclusionRect(FSlateApplication::Get().GetCursorPos()));
			}));
		}
}

TSharedRef<SWidget> UACEHoverTooltipWidget::RebuildWidget()
{
	EnsureDefaultLayout();
	ApplyRetailStyle();
	auto Slate = Super::RebuildWidget();
	// UImage's desired-size override is Slate-only: set it after the images
	// are built, keeping native texture tiling independent of layout thickness.
	Fill->SetDesiredSizeOverride(FVector2D::ZeroVector);
	for (const auto& Edge : Edges) Edge->SetDesiredSizeOverride(FVector2D(1, 1));
	return Slate;
}

void UACEHoverTooltipWidget::EnsureDefaultLayout()
{
	if (Frame || !WidgetTree) return;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Frame = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HoverFrame"));
	WidgetTree->RootWidget = Frame;
	Fill = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HoverFill"));
	auto* FillSlot = Frame->AddChildToOverlay(Fill);
	FillSlot->SetHorizontalAlignment(HAlign_Fill);
	FillSlot->SetVerticalAlignment(VAlign_Fill);
	Label = WidgetTree->ConstructWidget<UACERetailTextBlock>(UACERetailTextBlock::StaticClass(), TEXT("HoverLabel"));
	Label->SetAutoWrapText(true);
	Frame->AddChildToOverlay(Label);
	// Draw the four one-pixel edges over the fill. Painting a solid gold border
	// behind the translucent fill would change the retail background's color.
	for (int32 I = 0; I < 4; ++I)
	{
		auto* Edge = WidgetTree->ConstructWidget<UImage>();
		auto* EdgeSlot = Frame->AddChildToOverlay(Edge);
		EdgeSlot->SetHorizontalAlignment(HAlign_Fill);
		EdgeSlot->SetVerticalAlignment(VAlign_Fill);
		if (I == 0) EdgeSlot->SetVerticalAlignment(VAlign_Top);
		if (I == 1) EdgeSlot->SetHorizontalAlignment(HAlign_Left);
		if (I == 2) EdgeSlot->SetHorizontalAlignment(HAlign_Right);
		if (I == 3) EdgeSlot->SetVerticalAlignment(VAlign_Bottom);
		Edges.Add(Edge);
	}
}

void UACEHoverTooltipWidget::SetResources(UACEUIResourceResolver* InResources)
{
	Resources = InResources;
	EnsureDefaultLayout();
	ApplyRetailStyle();
}

void UACEHoverTooltipWidget::ApplyRetailStyle()
{
	if (!Label) return;
	// Templates are immutable here; share them rather than parsing JSON for
	// each inventory icon or every frame of a world-object hover.
	static TMap<uint32, TSharedPtr<FACEUIElement>> Templates;
	Template = Templates.FindRef(CurrentTemplate);
	if (!Template)
	{
		Template = UACEUILayoutResolver::LoadTemplate(0x21000041, CurrentTemplate);
		if (Template) Templates.Add(CurrentTemplate, Template);
	}
	if (!Template) return;
	if (!Resources)
		if (auto* World = GetWorld())
			if (auto* GI = World->GetGameInstance())
				if (auto* Dat = GI->GetSubsystem<UACEDatSubsystem>()) Resources = Dat->GetUiResources();
	auto SetImage = [&](UImage* Image, uint32 ImageId)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::Both;
		auto* Texture = Resources ? Resources->ResolveTexture(ImageId) : nullptr;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Texture ? FVector2D(Texture->GetSizeX(), Texture->GetSizeY()) : FVector2D(1, 1);
		Brush.TintColor = Texture ? FLinearColor::White : FLinearColor::Transparent;
		Image->SetBrush(Brush);
	};
	SetImage(Fill, Template->ImageFileId);
	int32 EdgeIndex = 0;
	for (const auto& Child : Template->Children)
	{
		if (Child->ElementId != 0x10000396)
		{
			if (Edges.IsValidIndex(EdgeIndex)) SetImage(Edges[EdgeIndex++], Child->ImageFileId);
			continue;
		}
		Label->SetRetailElement(Resources, Child, FVector2D(1, 1), Child->MaxWidth, false);
		Label->SetColorAndOpacity(Child->TextColor.Get(FLinearColor::White));
		Label->SetMargin(Child->TextMargins);
		Label->SetWrapTextAt(Child->MaxWidth);
		// UIElementManager::StartTooltip sizes the root by the measured text plus
		// the authored root-to-text offsets; margins belong inside the text region.
		CastChecked<UOverlaySlot>(Label->Slot)->SetPadding(FMargin(Child->X, Child->Y,
			Template->Width - Child->X - Child->Width, Template->Height - Child->Y - Child->Height));
	}
}

void UACEHoverTooltipWidget::SetTooltipText(const FString& Text, uint32 TemplateId)
{
	EnsureDefaultLayout();
	if (!Template || CurrentTemplate != TemplateId)
	{
		CurrentTemplate = TemplateId;
		ApplyRetailStyle();
	}
	if (Label && Label->GetText().ToString() != Text) Label->SetText(FText::FromString(Text));
}

FVector2D UACEHoverTooltipWidget::CalculateTooltipPosition(const FVector2D& CursorPixels,
	const FVector2D& TipPx, const FVector2D& SizePx, float Dpi)
{
	// Retain retail's 32-pixel offset, but clear the scaled software cursor too.
	const FSlateRect CursorRect = GetCursorExclusionRect(CursorPixels);
	const float Gap = 4.f * Dpi;
	FVector2D PosPx(FMath::Max(CursorPixels.X + 32.f * Dpi, CursorRect.Right + Gap),
		FMath::Max(CursorPixels.Y + 32.f * Dpi, CursorRect.Bottom + Gap));
	PosPx.X = FMath::Clamp(PosPx.X, 0.f, FMath::Max(0.f, SizePx.X - TipPx.X));
	PosPx.Y = FMath::Clamp(PosPx.Y, 0.f, FMath::Max(0.f, SizePx.Y - TipPx.Y));
	// Clamping near the lower/right edge must not push text under the cursor.
	if (FSlateRect::DoRectanglesIntersect(CursorRect, FSlateRect(PosPx, PosPx + TipPx)))
	{
		if (CursorRect.Top - Gap >= TipPx.Y) PosPx.Y = CursorRect.Top - Gap - TipPx.Y;
		else if (CursorRect.Left - Gap >= TipPx.X) PosPx.X = CursorRect.Left - Gap - TipPx.X;
	}
	return PosPx;
}

void UACEHoverTooltipWidget::SetTooltipScreenPosition(const FVector2D& CursorPixels)
{
	EnsureDefaultLayout();
	const float Dpi = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
	FVector2D SizePx = UWidgetLayoutLibrary::GetViewportSize(this);
	ForceLayoutPrepass();
	const FVector2D PosPx = CalculateTooltipPosition(CursorPixels, GetDesiredSize() * Dpi, SizePx, Dpi);
	SetAlignmentInViewport(FVector2D::ZeroVector);
	SetPositionInViewport(PosPx, true);
}
