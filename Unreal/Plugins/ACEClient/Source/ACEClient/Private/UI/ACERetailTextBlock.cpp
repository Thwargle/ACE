#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIFontStyles.h"
#include "ACEDatSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Dat/ACEDatTextLayout.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Text/STextBlock.h"

class SACERetailTextBlock : public STextBlock
{
public:
	SLATE_BEGIN_ARGS(SACERetailTextBlock) {} SLATE_END_ARGS()
	void Construct(const FArguments&, UACERetailTextBlock* InOwner)
	{
		Owner = InOwner;
		STextBlock::Construct(STextBlock::FArguments());
		SetClipping(EWidgetClipping::ClipToBounds);
	}

	virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override
	{
		STextBlock::Tick(Geometry, Time, DeltaTime);
		const auto* Label = Owner.Get();
		if (!Label || !Label->GetBitmapFont() || Label->GetNativeWidth() > 0) return;
		const int32 Width = FMath::Max(1, FMath::FloorToInt(Geometry.GetLocalSize().X / Label->GetBitmapScale().X));
		if (AllocatedNativeWidth != Width)
		{
			// ScrollBox children have no authored width. Keep the allotted width for
			// the next prepass, so wrapped lines reserve height instead of being clipped.
			AllocatedNativeWidth = Width;
			Invalidate(EInvalidateWidgetReason::Layout);
		}
	}

	virtual FVector2D ComputeDesiredSize(float Scale) const override
	{
		const UACERetailTextBlock* Label = Owner.Get();
		// Slate can finish a prepass after UMG releases/rebuilds the owning label.
		// The inherited text style contains pointers into that UObject (StrikeBrush).
		if (!Label) return FVector2D::ZeroVector;
		const FACEDatFont* DatFont = Label->GetBitmapFont();
		if (!DatFont) return STextBlock::ComputeDesiredSize(Scale);
		UpdateLines(*Label, *DatFont, AllocatedNativeWidth);
		int32 Width = 0;
		for (const FACEBitmapTextLine& Line : Lines) Width = FMath::Max(Width, Line.Width);
		const FMargin TextInsets = GetMargin();
		return FVector2D(Width, Lines.Num() * DatFont->MaxCharHeight) * Label->GetBitmapScale()
			+ FVector2D(TextInsets.Left + TextInsets.Right, TextInsets.Top + TextInsets.Bottom);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const UACERetailTextBlock* Label = Owner.Get();
		if (!Label) return Layer;
		const FACEDatFont* DatFont = Label->GetBitmapFont();
		if (!DatFont) return STextBlock::OnPaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled);
		const int32 Width = FMath::Max(1, FMath::FloorToInt(Geometry.GetLocalSize().X / Label->GetBitmapScale().X));
		if (Label->GetNativeWidth() <= 0 && AllocatedNativeWidth != Width)
		{
			AllocatedNativeWidth = Width;
			const_cast<SACERetailTextBlock*>(this)->Invalidate(EInvalidateWidgetReason::Layout);
		}
		UpdateLines(*Label, *DatFont, Geometry.GetLocalSize().X / Label->GetBitmapScale().X);
		const TSharedPtr<FACEUIElement> Element = Label->GetRetailElement();
		// The canvas flattens DAT children into one Slate layer. Reapply ancestor
		// clipping here so a scrolled label cannot paint over the panel's frame.
		FSlateRect Clip = Geometry.GetLayoutBoundingRect().IntersectionWith(CullingRect);
		if (Element && Label->UsesDatAncestorClipping())
		{
			const FIntPoint Origin = Element->GetScreenOrigin();
			for (auto Parent = Element->Parent.Pin(); Parent; Parent = Parent->Parent.Pin())
			{
				// SyntheticRoot is an ownership node, not an authored clipping region.
				if (!Parent->Parent.IsValid()) continue;
				const FIntPoint P = Parent->GetScreenOrigin();
				if (Parent->Width <= 0 || Parent->Height <= 0) continue;
				const FVector2D Min = Geometry.LocalToAbsolute(FVector2D(P-Origin)*Label->GetBitmapScale());
				const FVector2D Max = Geometry.LocalToAbsolute(FVector2D(P-Origin+FIntPoint(Parent->Width,Parent->Height))*Label->GetBitmapScale());
				Clip = Clip.IntersectionWith(FSlateRect(Min.X,Min.Y,Max.X,Max.Y));
			}
		}
		if (Clip.Right <= Clip.Left || Clip.Bottom <= Clip.Top) return Layer;
		Elements.PushClip(FSlateClippingZone(Clip));
		const FACEUIStateMedia* State = Element ? Element->States.Find(Element->PaintState) : nullptr;
		const bool bOutline = State && State->bTextOutline.IsSet() ? State->bTextOutline.GetValue()
			: Element && Element->bTextOutline;
		const FLinearColor TextColor = State && State->TextColor.IsSet() ? State->TextColor.GetValue()
			: GetColorAndOpacity().GetColor(Style);
		const FLinearColor OutlineColor = Element ? Element->TextOutlineColor : FLinearColor::Black;
		const FVector2D Scale = Label->GetBitmapScale();
		const FMargin TextInsets = GetMargin();
		const float AvailableWidth = FMath::Max(0.f, Geometry.GetLocalSize().X - TextInsets.Left - TextInsets.Right);
		float Top = TextInsets.Top;
		const float SpareHeight = FMath::Max(0.f, Geometry.GetLocalSize().Y - TextInsets.Top - TextInsets.Bottom
			- Lines.Num() * DatFont->MaxCharHeight * Scale.Y);
		if (Element && Element->TextVerticalJustification == 1) Top += FMath::FloorToFloat(SpareHeight * .5f);
		else if (Element && Element->TextVerticalJustification == 5) Top += SpareHeight;
		const ESlateDrawEffect Effects = (ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect)
			| (Scale.Equals(FVector2D(1,1)) ? ESlateDrawEffect::None : ESlateDrawEffect::NoPixelSnapping);
		auto DrawPass = [&](bool bBackground, FVector2D Offset)
		{
			UTexture2D* Atlas = Label->GetGlyphAtlas(bBackground);
			const bool bHasBackgroundAtlas = Atlas != nullptr;
			if (!Atlas) Atlas = Label->GetGlyphAtlas(false);
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.SetResourceObject(Atlas);
			const bool bTint = Label->ShouldTintAtlas(bBackground && bHasBackgroundAtlas);
			FLinearColor Tint = bTint ? (bBackground ? OutlineColor : TextColor) : FLinearColor::White;
			Tint *= Style.GetColorAndOpacityTint();
			const int32 BorderX = bBackground && bHasBackgroundAtlas ? DatFont->NumHorizontalBorderPixels : 0;
			const int32 BorderY = bBackground && bHasBackgroundAtlas ? DatFont->NumVerticalBorderPixels : 0;
			for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
			{
				const FACEBitmapTextLine& Line = Lines[LineIndex];
				const float Spare = FMath::Max(0.f, AvailableWidth - Line.Width * Scale.X);
				float X = TextInsets.Left;
				if (Label->GetTextJustification() == ETextJustify::Center) X += FMath::FloorToFloat(Spare * .5f);
				else if (Label->GetTextJustification() == ETextJustify::Right) X += Spare;
				const float Y = Top + LineIndex * DatFont->MaxCharHeight * Scale.Y;
				for (int32 I = Line.Begin; I < Line.End; ++I)
				{
					if (CachedText[I] == '\n') continue;
					const FACEDatFontChar* Ch = ACEDatText::FindChar(*DatFont, CachedText[I]);
					if (!Ch) continue;
					X += static_cast<int8>(Ch->HorizontalOffsetBefore) * Scale.X;
					if (Ch->Width > 0 && Ch->Height > 0)
					{
						const FVector2D PixelSize(Ch->Width + 2 * BorderX, Ch->Height + 2 * BorderY);
						const FVector2D AtlasSize(Atlas->GetSizeX(), Atlas->GetSizeY());
						const FVector2D UV(Ch->OffsetX - BorderX, Ch->OffsetY - BorderY);
						Brush.SetUVRegion(FBox2f(FVector2f(UV / AtlasSize), FVector2f((UV + PixelSize) / AtlasSize)));
						const FVector2D Pos = FVector2D(X, Y + static_cast<int8>(Ch->VerticalOffsetBefore) * Scale.Y)
							+ (Offset - FVector2D(BorderX, BorderY)) * Scale;
						FSlateDrawElement::MakeBox(Elements, Layer + (bBackground ? 0 : 1),
							Geometry.ToPaintGeometry(PixelSize * Scale, FSlateLayoutTransform(Pos)), &Brush, Effects,
								!bBackground && bTint && Label->GetModifierBegin() >= 0 && I >= Label->GetModifierBegin()
								? Label->GetModifierColor() * Style.GetColorAndOpacityTint() : Tint);
					}
					X += (Ch->Width + static_cast<int8>(Ch->HorizontalOffsetAfter)) * Scale.X;
				}
			}
		};
		if (bOutline)
		{
			if (Label->GetGlyphAtlas(true)) DrawPass(true, FVector2D::ZeroVector);
			else for (int32 Y = -1; Y <= 1; ++Y) for (int32 X = -1; X <= 1; ++X)
				if (X || Y) DrawPass(true, FVector2D(X, Y));
		}
		DrawPass(false, FVector2D::ZeroVector);
		Elements.PopClip();
		return Layer + 1;
	}

private:
	void UpdateLines(const UACERetailTextBlock& Label, const FACEDatFont& DatFont, float AllocatedWidth = 0) const
	{
		const FString Text = GetText().ToString();
		const TSharedPtr<FACEUIElement> Element = Label.GetRetailElement();
		const bool bOneLine = Element ? Element->bTextOneLine : !Label.GetAutoWrapText();
		const FMargin TextInsets = GetMargin();
		const int32 Width = FMath::Max(1, FMath::FloorToInt((Label.GetNativeWidth() > 0 ? Label.GetNativeWidth() : AllocatedWidth > 0 ? AllocatedWidth : 10000.f)
			- (TextInsets.Left + TextInsets.Right) / Label.GetBitmapScale().X));
		if (CachedText != Text || CachedFont != DatFont.Id || CachedWidth != Width || bCachedOneLine != bOneLine)
		{
			CachedText = Text;
			CachedFont = DatFont.Id;
			CachedWidth = Width;
			bCachedOneLine = bOneLine;
			Lines = ACEDatText::Layout(DatFont, Text, Width, bOneLine);
		}
	}
	TWeakObjectPtr<UACERetailTextBlock> Owner;
	mutable int32 AllocatedNativeWidth = 0;
	mutable FString CachedText;
	mutable uint32 CachedFont = 0;
	mutable int32 CachedWidth = -1;
	mutable bool bCachedOneLine = false;
	mutable TArray<FACEBitmapTextLine> Lines;
};

void UACERetailTextBlock::SetRetailElement(UACEUIResourceResolver* InResources,
	const TSharedPtr<FACEUIElement>& Element, FVector2D Scale, float InNativeWidth, bool bFlattenedOnCanvas)
{
	const FACEUIStateMedia* State = Element ? Element->States.Find(Element->PaintState) : nullptr;
	uint32 FontId = State && State->FontId ? State->FontId : Element ? Element->FontId : 0;
	if (!FontId) FontId = ACEUIFontStyles::ResolveForElement(Element, GetFont().Size).FontId;
	if (!FontId) FontId = GetFont().Size <= 8 ? 0x40000002u : 0x40000000u;
	bool bChanged = Resources != InResources || BitmapFont.Id != FontId || !ForegroundAtlas;
	if (bChanged)
	{
		Resources = InResources;
		BitmapFont = FACEDatFont();
		ForegroundAtlas = nullptr;
		BackgroundAtlas = nullptr;
		if (Resources && Resources->ResolveFont(FontId, BitmapFont))
		{
			ForegroundAtlas = Resources->ResolveFontAtlas(BitmapFont.ForegroundSurfaceDataID, bTintForeground);
			BackgroundAtlas = Resources->ResolveFontAtlas(BitmapFont.BackgroundSurfaceDataID, bTintBackground);
		}
	}
	bChanged |= BitmapScale != Scale || NativeWidth != InNativeWidth || RetailElement.Pin() != Element;
	bChanged |= Element && LastPaintState != Element->PaintState;
	bChanged |= bApplyDatAncestorClip != bFlattenedOnCanvas;
	// A real ScrollBox already clips its descendants. Projecting DAT ancestors
	// from its translated child geometry would move the clip with the text.
	bApplyDatAncestorClip = bFlattenedOnCanvas;
	LastPaintState = Element ? Element->PaintState : 0;
	RetailElement = Element;
	BitmapScale = Scale.ComponentMax(FVector2D(KINDA_SMALL_NUMBER, KINDA_SMALL_NUMBER));
	NativeWidth = InNativeWidth;
	if (bChanged) InvalidateLayoutAndVolatility();
}

UTexture2D* UACERetailTextBlock::GetGlyphAtlas(bool bBackground) const
{
	return bBackground ? BackgroundAtlas.Get() : ForegroundAtlas.Get();
}

TSharedRef<SWidget> UACERetailTextBlock::RebuildWidget()
{
	if (!ForegroundAtlas)
	{
		if (auto* Canvas = GetTypedOuter<UACEUICanvasWidget>())
			SetRetailElement(Canvas->GetResourceResolver(), RetailElement.Pin(), BitmapScale, NativeWidth, false);
		else if (const UWorld* World = GetWorld())
			if (const UGameInstance* GI = World->GetGameInstance())
				if (auto* Dat = GI->GetSubsystem<UACEDatSubsystem>())
					SetRetailElement(Dat->GetUiResources(), RetailElement.Pin(), BitmapScale, NativeWidth, false);
	}
	MyTextBlock = SNew(SACERetailTextBlock, this);
	return MyTextBlock.ToSharedRef();
}
