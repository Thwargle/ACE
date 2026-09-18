#include "UI/ACECaptureImage.h"
#include "Widgets/Images/SImage.h"
#include "Rendering/DrawElements.h"

namespace
{
    class SACECaptureImage : public SImage
    {
        virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
            FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
        {
            const FSlateBrush* Brush = GetImageAttribute().Get();
            if (!Brush || Brush->DrawAs == ESlateBrushDrawType::NoDrawType) return Layer;
            auto Effects = ESlateDrawEffect::InvertAlpha | ESlateDrawEffect::PreMultipliedAlpha;
            if (!ShouldBeEnabled(bParentEnabled)) Effects |= ESlateDrawEffect::DisabledEffect;
            FLinearColor Tint = Style.GetColorAndOpacityTint()
                * GetColorAndOpacityAttribute().Get().GetColor(Style) * Brush->GetTint(Style);
            // RGB already contains the scene's coverage. Premultiply only widget tint.
            Tint.R *= Tint.A; Tint.G *= Tint.A; Tint.B *= Tint.A;
            FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(), Brush, Effects, Tint);
            return Layer;
        }
    };
}

TSharedRef<SWidget> UACECaptureImage::RebuildWidget()
{
    MyImage = SNew(SACECaptureImage);
    return MyImage.ToSharedRef();
}
