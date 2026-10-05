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
	void SetFontOverride(uint32 FontId) { FontOverride = FontId; }
	/** Color just the signed modifier in a stat footer, preserving bitmap advances. */
	void SetModifierSuffix(int32 Begin, FLinearColor Color) { ModifierBegin=Begin; ModifierColor=Color; InvalidateLayoutAndVolatility(); }
	int32 GetModifierBegin() const { return ModifierBegin; }
	FLinearColor GetModifierColor() const { return ModifierColor; }
	// Character offsets survive bitmap wrapping; inspection can color individual stats.
	void SetTextColors(const TArray<FLinearColor>& Colors) { TextColors = Colors; InvalidateLayoutAndVolatility(); }
	FLinearColor GetGlyphColor(int32 Index, FLinearColor Default) const
	{
		if (IsLinkAt(Index)) return FLinearColor(.18f,.72f,1.f);
		if (TextColors.IsValidIndex(Index)) return TextColors[Index];
		return ModifierBegin >= 0 && Index >= ModifierBegin ? ModifierColor : Default;
	}
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
	// Chat links use character offsets so wrapping and drag-to-copy remain intact.
	TArray<FIntPoint> TextLinks;
	TFunction<bool()> AreLinksEnabled;
	TFunction<bool(int32)> OnLinkClicked;
	bool IsLinkAt(int32 Index) const
	{
		if (TextLinks.IsEmpty()) return false;
		if (AreLinksEnabled && !AreLinksEnabled()) return false;
		for (const auto& Range:TextLinks) if(Index>=Range.X && Index<Range.Y)return true;
		return false;
	}
	TFunction<FString()> GetCopyAllText;
	/** Ordered rows of one chat window; selection can span wrapped messages. */
	TFunction<TArray<UACERetailTextBlock*>()> GetSelectionPeers;
	TSharedPtr<STextBlock> GetSelectionWidget() const { return MyTextBlock; }

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
	uint32 FontOverride = 0;
	TArray<FLinearColor> TextColors;
};
