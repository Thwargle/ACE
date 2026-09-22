#pragma once
#include "Widgets/SLeafWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"

/** Vector health artwork, deliberately free of text and low-resolution DAT masks. */
class SACEEnemyHealthBar : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SACEEnemyHealthBar) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        SetVisibility(EVisibility::HitTestInvisible);
    }
    void SetFraction(float Value)
    {
        const float NewFraction=FMath::Clamp(Value,0.f,1.f);
        if(Fraction!=NewFraction){Fraction=NewFraction;Invalidate(EInvalidateWidgetReason::Paint);}
    }
    float GetFraction() const { return Fraction; }
private:
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(512,40); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        static const FSlateRoundedBoxBrush Frame(FLinearColor(.018f,.024f,.035f,.96f),12.f,FLinearColor(.8f,.65f,.34f),2.f);
        static const FSlateRoundedBoxBrush Fill(FLinearColor(.82f,.035f,.025f),7.f);
        FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),&Frame,
            ESlateDrawEffect::None,Style.GetColorAndOpacityTint()*Frame.GetTint(Style));
        // Paint the inset at its actual width. SProgressBar's rectangular padding
        // clip cuts off rounded corners drawn at the full widget bounds.
        const FVector2f Size=FVector2f(Geometry.GetLocalSize())-FVector2f(10,10);
        if(Fraction>0.f && Size.X>0.f && Size.Y>0.f)
        {
            FSlateDrawElement::MakeBox(Elements,Layer+1,
                Geometry.ToPaintGeometry(FVector2f(Size.X*Fraction,Size.Y),FSlateLayoutTransform(FVector2f(5,5))),
                &Fill,ESlateDrawEffect::None,Style.GetColorAndOpacityTint()*Fill.GetTint(Style));
        }
        return Layer+1;
    }
    float Fraction=1.f;
};
