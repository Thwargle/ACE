#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACETypes.h"
#include "ACECharacterCreation.h"
#include "Types/SlateEnums.h"
#include "ACEUICharSelectBinder.generated.h"

class UACEClientSubsystem;
class UACEUIElementManager;
class UACEUICanvasWidget;
class AACEPlayerController;
class UBorder;
class UACERetailTextBlock;
class UACERetailTextEntry;
struct FACEUIElement;

/**
 * Binds retail charactermanagement (0x21000004) ElementNames to character list,
 * WorldName, and EnterGame / Exit buttons. Labels use portal DAT bitmap fonts.
 */
UCLASS()
class ACECLIENT_API UACEUICharSelectBinder : public UObject
{
	GENERATED_BODY()
	friend class FACERetailScreenTest;
	friend class FACECharacterManagementTest;

public:
	void Initialize(UACEClientSubsystem* InClient, UACEUIElementManager* InManager,
		UACEUICanvasWidget* InCanvas, AACEPlayerController* InPC,
		const TArray<FACECharacterInfo>& Characters, const FString& ServerName);
	void Shutdown();

	void TickRefresh(float DeltaSeconds = 0.f);
	bool KeyDown(const FKeyEvent& Event);
	/** Layout-space click (800×600), not viewport pixels. */
	bool TryHandleOverlayClick(FVector2D LayoutPos);

private:
	void OnElementActivated(TSharedPtr<FACEUIElement> Element);
	void EnsureOverlays();
	void RefreshOverlays();
	void SyncButtonStates();
	void PlaceScaled(class UWidget* Widget, float LayoutX, float LayoutY, float LayoutW, float LayoutH, int32 ZOrder);
	void SelectCharacterIndex(int32 Index);
	void EnterSelectedCharacter();
	void ShowDeleteConfirmation();
	void ConfirmDelete();
	void CloseDialog();
	void RefreshDialog();
	void SetCreditsVisible(bool bShow);
	void TickCredits(float DeltaSeconds);
	void ShutdownActions();
	void ActionLabel(const TSharedPtr<FACEUIElement>& Element, const FString& Text);
	UFUNCTION() void ConfirmationCommitted(const FText& Text, ETextCommit::Type Method);
	FACECharacterCreation CharacterStrings;
	TSharedPtr<FACEUIElement> Dialog, CreditsRoot, CreditsText;
	TArray<TSharedPtr<FACEUIElement>> CreditPictures;
	UPROPERTY() TMap<uint32, TObjectPtr<UACERetailTextBlock>> ActionLabels;
	UPROPERTY() TObjectPtr<UACERetailTextEntry> ConfirmationEntry;
	int32 DeleteCandidateId = 0;
	FString DialogMessage, ConfirmationWord, LastManagementError, CreditText;
	bool bCredits = false;
	float CreditsElapsed = 0.f, CreditsSpeed = 0.f;
	int32 NextCreditPicture = 0;
	int32 GetCharacterRowHeight() const;
	void PlaceSlotChrome(int32 SlotIndex, float LayoutX, float LayoutY, float LayoutW, bool bSelected);
	void PlaceListFrameChrome(float LayoutX, float LayoutY, float LayoutW, float LayoutH);
	UBorder* EnsureTextImage(TObjectPtr<UBorder>& Slot);
	void PlaceDatText(UBorder* Border, uint32 FontId, const FString& Text, FColor Color,
		float BoxX, float BoxY, float BoxW, float BoxH, int32 ZOrder, bool bOutline,
		FString& CacheKey, TObjectPtr<UTexture2D>& CacheTex, int32& CacheW, int32& CacheH,
		bool bCenter = true, float LayoutYNudge = 0.f);

	UPROPERTY()
	TObjectPtr<UACEClientSubsystem> Client = nullptr;
	UPROPERTY()
	TObjectPtr<UACEUIElementManager> Manager = nullptr;
	UPROPERTY()
	TObjectPtr<UACEUICanvasWidget> Canvas = nullptr;
	UPROPERTY()
	TObjectPtr<AACEPlayerController> PlayerController = nullptr;

	TArray<FACECharacterInfo> Characters;
	FString ServerName;
	int32 SelectedIndex = INDEX_NONE;
	int32 SelectedCharacterId = 0;

	UPROPERTY()
	TObjectPtr<UBorder> LetterboxFill = nullptr;
	UPROPERTY()
	TObjectPtr<UBorder> WorldNameImage = nullptr;
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> CharNameImages;
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> CharSlotTops;
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> CharSlotMiddles;
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> CharSlotBottoms;
	/** Box_GoldBorder 9-slice around CharacterListBoxFrame (TL/TR/BL/BR + edges). */
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> ListFrameParts;
	UPROPERTY()
	TMap<FString, TObjectPtr<UACERetailTextBlock>> ButtonTextImages;

	/** Avoid re-rasterizing DAT fonts every tick. */
	FString WorldNameCacheKey;
	UPROPERTY()
	TObjectPtr<UTexture2D> WorldNameCacheTex = nullptr;
	int32 WorldNameCacheW = 0;
	int32 WorldNameCacheH = 0;
	TArray<FString> CharNameCacheKeys;
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> CharNameCacheTex;
	TArray<int32> CharNameCacheW;
	TArray<int32> CharNameCacheH;
	TMap<FString, FString> ButtonTextCacheKeys;
	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> ButtonTextCacheTex;
	TMap<FString, int32> ButtonTextCacheW;
	TMap<FString, int32> ButtonTextCacheH;

	FDelegateHandle ActivatedHandle;
	bool bBound = false;
	double LastClickTime = 0.0;
	int32 LastClickIndex = INDEX_NONE;
};
