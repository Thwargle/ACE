#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Text/STextBlock.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACEDatTextLayout.h"

class UACEPluginSubsystem;
class UACEUIResourceResolver;
class UTexture2D;
struct FACESupplyEntry;

struct FACEPluginFont
{
    FACEDatFont Font;
    TObjectPtr<UTexture2D> Atlas=nullptr;
    bool Tint=false;
};

/** Shared retail artwork and atlas, retained as long as any plugin widget uses them. */
class FACEPluginStyle : public FGCObject
{
public:
    explicit FACEPluginStyle(UACEUIResourceResolver* InResolver);
    static TSharedRef<FACEPluginStyle> Use(UACEPluginSubsystem* Host,const FString& WindowId=FString());
    static TSharedRef<FACEPluginStyle> Current();
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override {return TEXT("ACEPluginStyle");}
    const FSlateBrush* Image(uint32 Id);
    const FSlateBrush* ItemImage(const FACESupplyEntry& Item,bool Foreground);
    void Refresh();
    bool HasDatArtwork() const {return DatArtworkLoaded;}
    bool SetWindowAppearance(float Opacity,bool Frameless);
    TSharedPtr<FACEPluginFont> Typeface(int32 Size);
    FButtonStyle Button,DockButton;
    FSliderStyle Slider;
    FCheckBoxStyle Check;
    FEditableTextBoxStyle Edit;
    FComboButtonStyle Combo;
    FSpinBoxStyle Spin;
    FSlateBrush Frame, Background, TitleBackground, SectionFrame, SectionHeader;
    FACEDatFont Font;
    TObjectPtr<UTexture2D> Atlas=nullptr;
    bool TintAtlas=false;
private:
    void InitializeArtwork();
    TObjectPtr<UACEUIResourceResolver> Resolver=nullptr;
    TMap<uint32,TSharedPtr<FSlateBrush>> Images;
    TMap<FString,TSharedPtr<FSlateBrush>> ItemImages;
    TMap<int32,TSharedPtr<FACEPluginFont>> Fonts;
    FSlateBrush OriginalFrame,OriginalBackground,OriginalTitle,OriginalSectionFrame,OriginalSectionHeader;
    float BackgroundOpacity=-1;
    bool HideFrame=false;
    bool DatArtworkLoaded=false;
};

/** Native DAT bitmap text with cached wrapping; no generated text textures. */
class SACEPluginText : public STextBlock
{
public:
    using FArguments=STextBlock::FArguments;
    void Construct(const FArguments& Args);
    virtual FVector2D ComputeDesiredSize(float) const override;
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
private:
    TSharedPtr<FACEPluginStyle> Retail;
    mutable TSharedPtr<FACEPluginFont> Face;
    mutable int32 FaceSize=INDEX_NONE;
    TAttribute<bool> Wrap;
    TAttribute<float> WrapAt;
    TAttribute<ETextJustify::Type> Justification;
    mutable FString CachedText;
    mutable TArray<FACEBitmapTextLine> Lines;
    mutable int32 CachedWidth=-1;
    mutable float Width=0;
    mutable bool NativeGlyphs=true;
    mutable FString CheckedGlyphs;
    bool CanDrawNative() const;
    float BitmapScale() const;
    void Layout(float Available) const;
};
