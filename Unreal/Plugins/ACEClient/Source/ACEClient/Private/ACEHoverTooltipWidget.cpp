#include "ACEHoverTooltipWidget.h"
#include "UI/ACERetailTextBlock.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/GameViewportClient.h"
#include "Styling/SlateBrush.h"

TSharedRef<SWidget> UACEHoverTooltipWidget::RebuildWidget()
{
	EnsureDefaultLayout();
	return Super::RebuildWidget();
}

void UACEHoverTooltipWidget::EnsureDefaultLayout()
{
	if (OuterBorder || !WidgetTree)
	{
		return;
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);

	OuterBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HoverGoldBorder"));
	WidgetTree->RootWidget = OuterBorder;
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(FLinearColor(0.82f, 0.55f, 0.12f, 0.95f));
		OuterBorder->SetBrush(Brush);
		OuterBorder->SetPadding(FMargin(1.5f));
		OuterBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	InnerFill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HoverFill"));
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f));
		InnerFill->SetBrush(Brush);
		InnerFill->SetPadding(FMargin(3.f, 1.f));
		InnerFill->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	Label = WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass(), TEXT("HoverLabel"));
	Label->SetText(FText::GetEmpty());
	Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.96f, 0.96f, 1.f)));
	Label->SetVisibility(ESlateVisibility::HitTestInvisible);
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 12;
	Label->SetFont(Font);

	InnerFill->SetContent(Label);
	OuterBorder->SetContent(InnerFill);
}

void UACEHoverTooltipWidget::SetTooltipText(const FString& Text)
{
	EnsureDefaultLayout();
	if (Label)
	{
		Label->SetText(FText::FromString(Text));
	}
}

void UACEHoverTooltipWidget::SetTooltipScreenPosition(const FVector2D& CursorPixels)
{
	EnsureDefaultLayout();
	// GetMousePosition is viewport pixels. SetPositionInViewport needs bRemoveDPIScale=true
	// so Unreal converts pixels → Slate units; otherwise DPI>100% parks the tip far SE of the cursor.
	// Retail 15×28 arrow tip is the hotspot; BR of the glyph is ~(+14,+26) from the tip.
	const float Dpi = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
	const FVector2D OffsetPx(14.f, 26.f);
	SetAlignmentInViewport(FVector2D(0.f, 0.f));

	FVector2D PosPx = CursorPixels + OffsetPx;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D SizePx;
		GEngine->GameViewport->GetViewportSize(SizePx);
		ForceLayoutPrepass();
		const FVector2D TipSize = GetDesiredSize();
		if (TipSize.X > 1.f && TipSize.Y > 1.f && SizePx.X > 1.f && SizePx.Y > 1.f)
		{
			const FVector2D TipPx(TipSize.X * Dpi, TipSize.Y * Dpi);
			PosPx.X = FMath::Clamp(PosPx.X, 0.f, FMath::Max(0.f, SizePx.X - TipPx.X - 2.f));
			PosPx.Y = FMath::Clamp(PosPx.Y, 0.f, FMath::Max(0.f, SizePx.Y - TipPx.Y - 2.f));
		}
	}
	SetPositionInViewport(PosPx, /*bRemoveDPIScale*/ true);
}
