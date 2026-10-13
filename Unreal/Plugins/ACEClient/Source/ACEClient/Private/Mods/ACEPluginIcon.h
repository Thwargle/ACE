#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/** Original plugin emblems, authored on a square canvas, independent of DAT artwork. */
class SACEPluginIcon : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SACEPluginIcon) {} SLATE_ARGUMENT(FString, Id) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(26,26); }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
    static FSlateLayoutTransform Fit(const FVector2D& Size);
private:
    struct FPath { TArray<FVector2D> Points; FLinearColor Color; bool Filled=false; };
    TArray<FPath> Paths;
};
