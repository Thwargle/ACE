#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ACEHoverTooltipWidget.generated.h"

class UOverlay;
class UImage;
class UACERetailTextBlock;
class UACEUIResourceResolver;
struct FACEUIElement;

/** Shared retail layout 0x21000041 for object, control, and map tooltips. */
UCLASS()
class ACECLIENT_API UACEHoverTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	static constexpr uint32 ObjectTemplate = 0x10000395;
	static constexpr uint32 OptionsTemplate = 0x10000397;
	static constexpr uint32 MapTemplate = 0x10000398;
	static void SetWidgetTooltip(UWidget* Widget, const FText& Text, uint32 TemplateId = OptionsTemplate);
	void SetResources(UACEUIResourceResolver* InResources);
	void SetTooltipText(const FString& Text, uint32 TemplateId = ObjectTemplate);
	void SetTooltipScreenPosition(const FVector2D& CursorPixels);

protected:
	void EnsureDefaultLayout();
	void ApplyRetailStyle();

	UPROPERTY()
	TObjectPtr<UOverlay> Frame = nullptr;

	UPROPERTY()
	TObjectPtr<UImage> Fill = nullptr;

	UPROPERTY()
	TObjectPtr<UACERetailTextBlock> Label = nullptr;
	UPROPERTY() TArray<TObjectPtr<UImage>> Edges;
	UPROPERTY() TObjectPtr<UACEUIResourceResolver> Resources;
	TSharedPtr<FACEUIElement> Template;
	uint32 CurrentTemplate = ObjectTemplate;
};
