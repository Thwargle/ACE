#pragma once
#include "Components/EditableTextBox.h"
#include "ACEChatEntry.generated.h"

class UACEUIGameplayBinder;

/** Retail chat editing, shared by mouse, hardware keys and VR text entry. */
UCLASS()
class ACECLIENT_API UACEChatEntry : public UEditableTextBox
{
    GENERATED_BODY()
public:
    void InitializeChat(UACEUIGameplayBinder* InBinder);
    void RememberSubmitted(const FString& Message);
    void NavigateHistory(bool Previous);
    void SetChatText(const FString& Value);
    void ApplyNativeText(const FText& Value, bool RestoreDraft);
    FReply HandleChatKey(const FGeometry& Geometry, const FKeyEvent& Event);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UFUNCTION() void HandleChanged(const FText& Value);
    TWeakObjectPtr<UACEUIGameplayBinder> Binder;
    TArray<FString> History;
    int32 HistoryPosition = INDEX_NONE;
    bool bSettingText = false;
    // Native keyboards can continue sending their original unexpanded buffer.
    // Keep its reply recipient fixed for this edit, even if another tell arrives.
    FString ShortcutPrefix, ExpandedPrefix;
};
