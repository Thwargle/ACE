#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "Dat/ACEDatFileTypes.h"
#include "UI/ACEUIElement.h"
#include "ACERetailTextBlock.generated.h"

class UACEUIResourceResolver;
class UTexture2D;

/** UTextBlock-compatible label using the retail glyph atlas and pixel advances. */
UCLASS()
class ACECLIENT_API UACERetailTextBlock : public UTextBlock
{
	GENERATED_BODY()
public:
	void SetRetailElement(UACEUIResourceResolver* Resources, const TSharedPtr<FACEUIElement>& Element,
		FVector2D Scale, float NativeWidth, bool bFlattenedOnCanvas = true);
	/** Color just the signed modifier in a stat footer, preserving bitmap advances. */
	void SetModifierSuffix(int32 Begin, FLinearColor Color) { ModifierBegin=Begin; ModifierColor=Color; InvalidateLayoutAndVolatility(); }
	int32 GetModifierBegin() const { return ModifierBegin; }
	FLinearColor GetModifierColor() const { return ModifierColor; }
	bool UsesDatAncestorClipping() const { return bApplyDatAncestorClip; }
	const FACEDatFont* GetBitmapFont() const { return ForegroundAtlas ? &BitmapFont : nullptr; }
	UTexture2D* GetGlyphAtlas(bool bBackground) const;
	bool ShouldTintAtlas(bool bBackground) const { return bBackground ? bTintBackground : bTintForeground; }
	FVector2D GetBitmapScale() const { return BitmapScale; }
	float GetNativeWidth() const { return NativeWidth; }
	TSharedPtr<FACEUIElement> GetRetailElement() const { return RetailElement.Pin(); }
	ETextJustify::Type GetTextJustification() const { return Justification; }
	/** Chat-only selection; normal DAT labels remain noninteractive. */
	void SetSelectable(bool Value) { bSelectable = Value; }
	bool IsSelectable() const { return bSelectable; }
	FSimpleDelegate OnTextClicked;
	TFunction<FString()> GetCopyAllText;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY() TObjectPtr<UACEUIResourceResolver> Resources;
	UPROPERTY() TObjectPtr<UTexture2D> ForegroundAtlas;
	UPROPERTY() TObjectPtr<UTexture2D> BackgroundAtlas;
	FACEDatFont BitmapFont;
	TWeakPtr<FACEUIElement> RetailElement;
	FVector2D BitmapScale = FVector2D(1, 1);
	float NativeWidth = 0;
	bool bTintForeground = true;
	bool bSelectable = false;
	bool bApplyDatAncestorClip = true;
	bool bTintBackground = true;
	int32 ModifierBegin = -1;
	FLinearColor ModifierColor = FLinearColor::White;
	uint32 LastPaintState = MAX_uint32;
};
