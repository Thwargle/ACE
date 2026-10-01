#pragma once
#include "Blueprint/UserWidget.h"
#include "ACEVRChat.generated.h"

class UACEVRComponent;
class UACEUIGameplayBinder;
class UACEChatEntry;

/** Native chat presentation; the gameplay binder owns formatting, filtering and send commands. */
UCLASS()
class ACECLIENT_API UACEVRChat : public UUserWidget
{
    GENERATED_BODY()
    friend class FACEVRChatTest;
public:
    void InitializeChat(UACEVRComponent* InRig, UACEUIGameplayBinder* InBinder);
    bool Refresh();
    UWidget* GetEntry() const;
    bool IsBoundTo(const UACEUIGameplayBinder* Other) const { return Binder==Other; }
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool Children) override;
private:
    UPROPERTY(Transient) TObjectPtr<UACEVRComponent> Rig;
    UPROPERTY(Transient) TObjectPtr<UACEUIGameplayBinder> Binder;
    UPROPERTY(Transient) TObjectPtr<UACEChatEntry> Entry;
    TSharedPtr<class SScrollBox> Log;
    TSharedPtr<class SVerticalBox> Lines;
    TSharedPtr<class SACEVRPanelControls> Controls;
    TSharedPtr<class STextBlock> FilterLabel;
    TSharedPtr<class STextBlock> AnchorLabel;
    TArray<TSharedPtr<int32>> Channels;
    uint64 Revision = MAX_uint64;
    TArray<uint64> DisplayedLines;
    int32 DisplayedFilterMode = INDEX_NONE;
    int32 DisplayedAnchorMode = INDEX_NONE;
    bool bStickToBottom = true;
    UFUNCTION() void Committed(const FText& Text, ETextCommit::Type Method);
    void Send();
    FString ChannelLabel(int32 Channel) const;
};
