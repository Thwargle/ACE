#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Types/SlateEnums.h"
#include "UI/ACEUIElement.h"
#include "ACERetailTextEntry.generated.h"

class UACERetailTextBlock;
class UACEUIResourceResolver;
class SACERetailTextEntry;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FACERetailEntryCommitted, const FText&, Text, ETextCommit::Type, Method);

/** An editable DAT glyph field. Display, selection and caret share the same pixel advances. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FACERetailContextCommitted, int32, Context, const FText&, Text, ETextCommit::Type, Method);

UCLASS()
class ACECLIENT_API UACERetailTextEntry : public UWidget
{
    GENERATED_BODY()
    friend class SACERetailTextEntry;
public:
    void SetText(const FText& Value);
    FText GetText() const { return FText::FromString(Value); }
    void SetRetailElement(UACEUIResourceResolver* Resources, const TSharedPtr<FACEUIElement>& Element);
    void Commit(ETextCommit::Type Method);
    void RequestPlatformKeyboard(uint32 UserIndex);
    bool bMultiline = false;
    bool bDigitsOnly = false;
    int32 MaxLength = 1000;
    int32 ContextId = 0;
    FLinearColor TextColor = FLinearColor::White;
    UPROPERTY() FACERetailEntryCommitted OnTextCommitted;
    UPROPERTY() FACERetailContextCommitted OnContextCommitted;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
    FString Value;
    UPROPERTY() TObjectPtr<UACERetailTextBlock> FontLabel;
    TSharedPtr<SACERetailTextEntry> Editor;
};
