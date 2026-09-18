#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "ACETypes.h"
#include "ACELoginWidget.generated.h"

class UACEClientSubsystem;
class UEditableTextBox;
class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UBorder;
class UACELoginWidget;

/** A single clickable character-list row. Carries its own list index so a shared OnClicked
 *  handler (dynamic multicast delegates can't carry extra payload data) can tell the owning
 *  UACELoginWidget which character was picked. */
UCLASS()
class UACECharacterRowButton : public UButton
{
	GENERATED_BODY()

public:
	int32 CharacterIndex = 0;

	UPROPERTY()
	TWeakObjectPtr<UACELoginWidget> OwnerWidget;

	UFUNCTION()
	void HandleClicked();
};

/**
 * Minimal C++ login / character-select HUD.
 * Create a Blueprint child to style, or use this class directly (default slate layout via NativeConstruct).
 */
UCLASS()
class ACECLIENT_API UACELoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** True once EnsureDefaultLayout() has actually populated WidgetTree->RootWidget with our
	 *  real Canvas/Border/TextBox layout (as opposed to it still being null / a placeholder). */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	bool IsDefaultLayoutBuilt() const { return HostBox != nullptr; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	FString DefaultHost;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	int32 DefaultPort = 9000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	FString DefaultAccount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	FString DefaultPassword;

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void DoLogin();

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void DoEnterSelectedCharacter();

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void SelectCharacterIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void SetStatus(const FString& Msg);

	/** Hide the opaque login fill so DAT Intro splash (0x21000001) shows behind the card. */
	void SetUseDatIntroBackdrop(bool bUse);

protected:
	friend class FACEMissingDatLoginTest;
	/** CRITICAL: builds WidgetTree->RootWidget BEFORE UUserWidget::RebuildWidget() ever runs.
	 *  UUserWidget::RebuildWidget() captures `WidgetTree->RootWidget ? RootWidget->TakeWidget()
	 *  : SNew(SSpacer)` at the moment it's called, and only calls NativeConstruct() AFTER that
	 *  (see UUserWidget::OnWidgetRebuilt). If EnsureDefaultLayout() were only called from
	 *  NativeConstruct() (too late), the very first build would permanently freeze this widget's
	 *  content as an invisible, zero-sized SSpacer — even though the outer viewport slot still
	 *  gets a real, non-zero allocated geometry (that geometry belongs to the slot, not to the
	 *  SSpacer content). That exactly reproduces "CachedGeometry non-zero, DesiredSize (0,0),
	 *  nothing painted" — so building the layout here, and calling this again defensively from
	 *  NativeConstruct() (where it's a no-op once HostBox is set), guarantees the real widget
	 *  tree is what RebuildWidget() actually captures. */
	virtual TSharedRef<SWidget> RebuildWidget() override;

	void EnsureDefaultLayout();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> HostBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> PortBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> AccountBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> PasswordBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> EnterWorldButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CharacterListLabel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> CharacterListBox;

	UPROPERTY()
	TObjectPtr<class USizeBox> CharacterListSizeBox;

	UPROPERTY()
	TArray<TObjectPtr<UACECharacterRowButton>> CharacterRowButtons;

	UPROPERTY()
	TObjectPtr<UACEClientSubsystem> Client = nullptr;

	int32 SelectedCharacterId = 0;
	int32 SelectedCharacterIndex = INDEX_NONE;
	TArray<FACECharacterInfo> CachedCharacters;

	void RebuildCharacterRows();
	void UpdateCharacterRowHighlight();

	/** Set once NativeTick has fired the one-shot "still zero-sized" safety-net prepass, so we
	 *  don't spam it every frame. */
	bool bForcedZeroSizePrepass = false;
	bool bUseDatIntroBackdrop = false;
	bool bLoginSettingsLoaded = false;
	bool bLoginEntriesDirty = false;
	double LastLoginEditTime = 0.0;
	void SaveLoginEntries();
	UFUNCTION() void OnLoginEntryChanged(const FText& Text);
	UFUNCTION() void OnLoginEntryCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UPROPERTY()
	TObjectPtr<UBorder> BackdropBorder;

	UFUNCTION()
	void OnLoginClicked();

	UFUNCTION()
	void OnEnterWorldClicked();

	UFUNCTION()
	void HandleState(EACESessionState NewState);

	UFUNCTION()
	void HandleCharacters(const TArray<FACECharacterInfo>& Characters, const FString& ServerName);

	UFUNCTION()
	void HandleLog(const FString& Message);
};
