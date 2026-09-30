#include "ACEHoverTooltipWidget.h"
#include "ACEDatSubsystem.h"
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

void UACEHoverTooltipWidget::SetTooltipScreenPosition(const FVector2D& CursorPixels)
{
	EnsureDefaultLayout();
	const float Dpi = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
	// UIElementManager::StartTooltip places the tip 32 pixels from the cursor
	// on both axes, then clamps the complete frame to the display bounds.
	SetAlignmentInViewport(FVector2D(0.f, 0.f));
	FVector2D PosPx = CursorPixels + FVector2D(32.f, 32.f) * Dpi;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D SizePx;
		GEngine->GameViewport->GetViewportSize(SizePx);
		ForceLayoutPrepass();
		const FVector2D TipPx = GetDesiredSize() * Dpi;
		PosPx.X = FMath::Clamp(PosPx.X, 0.f, FMath::Max(0.f, SizePx.X - TipPx.X));
		PosPx.Y = FMath::Clamp(PosPx.Y, 0.f, FMath::Max(0.f, SizePx.Y - TipPx.Y));
	}
	SetPositionInViewport(PosPx, true);
}
