#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ACEMouseCursorWidget.generated.h"

class UImage;
class UCanvasPanel;
class USizeBox;

/** Software cursor textured from portal.dat retail pointer surfaces (0x06004D6x). */
UCLASS()
class ACECLIENT_API UACEMouseCursorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Tip / click point is the top-left of the 15x28 retail pointer texture. */
	void SetCursorTexture(UTexture2D* Texture, int32 HotX, int32 HotY);

	/** Default silver/bronze metal pointer (portal.dat). */
	static constexpr uint32 DefaultCursorDid = 0x06004D68u;
	/** Yellow-outline interactable pointer when hovering a clickable object. */
	static constexpr uint32 InteractableCursorDid = 0x06004D69u;
	// ClientUISystem::UpdateCursorState, DidMapper 2500000F enums 39/40/41.
	static constexpr uint32 TargetCursorDid = 0x06004D73u;
	static constexpr uint32 TargetValidCursorDid = 0x06005E6Bu;
	static constexpr uint32 TargetInvalidCursorDid = 0x06005E6Au;
	// classic_gameplay's Dragbar/Resizebar MD_Data_Cursor records (hotspot 16,16).
	static constexpr uint32 MoveCursorDid = 0x06006119u;
	static constexpr uint32 ResizeVerticalCursorDid = 0x06005E66u;
	static constexpr uint32 ResizeHorizontalCursorDid = 0x06006128u;
	static constexpr uint32 ResizeNWSECursorDid = 0x06006126u;
	static constexpr uint32 ResizeNESWCursorDid = 0x06006127u;
	static constexpr int32 WindowHotspot = 16;
	static constexpr int32 TargetHotspot = 14;
	/** Hotspot = texture (0,0) so world picks match the visible tip. */
	static constexpr int32 DefaultHotspotX = 0;
	static constexpr int32 DefaultHotspotY = 0;
	static constexpr int32 InteractableHotspotX = 0;
	static constexpr int32 InteractableHotspotY = 0;

private:
	void EnsureLayout();

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Root;
	UPROPERTY(Transient) TObjectPtr<USizeBox> CursorSize;
	UPROPERTY(Transient) TObjectPtr<UImage> CursorImage;
	int32 HotspotX = DefaultHotspotX;
	int32 HotspotY = DefaultHotspotY;
};
