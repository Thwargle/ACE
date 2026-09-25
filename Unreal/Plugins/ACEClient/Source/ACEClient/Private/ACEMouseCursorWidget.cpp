#include "ACEMouseCursorWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"

TSharedRef<SWidget> UACEMouseCursorWidget::RebuildWidget()
{
	EnsureLayout();
	return Super::RebuildWidget();
}

void UACEMouseCursorWidget::EnsureLayout()
{
	if (Root || !WidgetTree)
	{
		return;
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CursorRoot"));
	CursorSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CursorSize"));
	CursorSize->AddChild(Root);
	WidgetTree->RootWidget = CursorSize;
	CursorImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CursorImage"));
	CursorImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	Root->AddChild(CursorImage);
	if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(CursorImage->Slot))
	{
		PanelSlot->SetAutoSize(false);
		PanelSlot->SetAnchors(FAnchors(0.f, 0.f));
		PanelSlot->SetAlignment(FVector2D(0.f, 0.f));
		PanelSlot->SetPosition(FVector2D(static_cast<float>(-HotspotX), static_cast<float>(-HotspotY)));
	}
}

void UACEMouseCursorWidget::SetCursorScale(float Scale)
{
	Scale = FMath::IsFinite(Scale) ? FMath::Clamp(Scale, .5f, 3.f) : 1.f;
	if (FMath::IsNearlyEqual(CursorScale, Scale)) return;
	CursorScale = Scale;
	if (CursorImage) SetCursorTexture(Cast<UTexture2D>(CursorImage->GetBrush().GetResourceObject()), HotspotX, HotspotY);
}

void UACEMouseCursorWidget::SetCursorTexture(UTexture2D* Texture, int32 HotX, int32 HotY)
{
	EnsureLayout();
	HotspotX = FMath::Max(0, HotX);
	HotspotY = FMath::Max(0, HotY);
	if (!CursorImage)
	{
		return;
	}
	if (!Texture)
	{
		return;
	}
	const FVector2D Size = FVector2D(Texture->GetSizeX(), Texture->GetSizeY()) * CursorScale;
	// Slate draws software cursor widgets CENTERED on the pointer (FSlateUser::DrawCursor
	// subtracts DesiredSize * 0.5). Make the widget 2x the texture and park the art in the
	// bottom-right quadrant: the widget center — which lands on the OS pointer — is then
	// exactly the texture's top-left tip (minus the authored hotspot).
	if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(CursorImage->Slot))
	{
		PanelSlot->SetAutoSize(false);
		PanelSlot->SetPosition(Size - FVector2D(HotspotX, HotspotY) * CursorScale);
		PanelSlot->SetSize(Size);
	}
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = Size;
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	CursorImage->SetBrush(Brush);

	SetDesiredSizeInViewport(Size * 2.f);
	// Software cursors are not viewport slots. Their Slate desired size must be
	// explicit too, otherwise the hotspot offset shrinks the canvas's bounds.
	CursorSize->SetWidthOverride(Size.X * 2.f);
	CursorSize->SetHeightOverride(Size.Y * 2.f);
}
