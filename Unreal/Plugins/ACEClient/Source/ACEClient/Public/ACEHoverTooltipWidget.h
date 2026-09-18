#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ACEHoverTooltipWidget.generated.h"

class UBorder;
class UTextBlock;

/** Retail-style object hover blurb: dark fill, gold border, white text. */
UCLASS()
class ACECLIENT_API UACEHoverTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	void SetTooltipText(const FString& Text);
	void SetTooltipScreenPosition(const FVector2D& CursorPixels);

protected:
	void EnsureDefaultLayout();

	UPROPERTY()
	TObjectPtr<UBorder> OuterBorder = nullptr;

	UPROPERTY()
	TObjectPtr<UBorder> InnerFill = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> Label = nullptr;
};
