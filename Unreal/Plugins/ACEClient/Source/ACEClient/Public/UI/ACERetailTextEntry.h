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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FACERetailEntryChanged, const FText&, Text);

/** An editable DAT glyph field. Display, selection and caret share the same pixel advances. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FACERetailContextCommitted, int32, Context, const FText&, Text, ETextCommit::Type, Method);

UCLASS()
class ACECLIENT_API UACERetailTextEntry : public UWidget
{
    GENERATED_BODY()
    friend class SACERetailTextEntry;
public:
    UACERetailTextEntry();
    void SetText(const FText& Value);
    FText GetText() const { return FText::FromString(Value); }
    void SetRetailElement(UACEUIResourceResolver* Resources, const TSharedPtr<FACEUIElement>& Element);
    void Commit(ETextCommit::Type Method);
    void RequestPlatformKeyboard(uint32 UserIndex);
    float GetScrollOffset() const;
    float GetScrollOffsetOfEnd() const;
    void SetScrollOffset(float Offset);
    bool bMultiline = false;
    bool bDigitsOnly = false;
    bool bSelectAllOnFocus = false;
    // Optional authored padding for compact single-line fields such as stack quantity.
    TOptional<FMargin> ContentMargins;
    int32 MaxLength = 1000;
    int32 ContextId = 0;
    FLinearColor TextColor = FLinearColor::White;
    UPROPERTY() FACERetailEntryCommitted OnTextCommitted;
    UPROPERTY() FACERetailEntryChanged OnTextChanged;
    UPROPERTY() FACERetailContextCommitted OnContextCommitted;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
    FString Value;
    UPROPERTY() TObjectPtr<UACERetailTextBlock> FontLabel;
    TSharedPtr<SACERetailTextEntry> Editor;
};
