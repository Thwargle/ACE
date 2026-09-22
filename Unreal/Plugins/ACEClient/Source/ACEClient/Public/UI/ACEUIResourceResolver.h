#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Dat/ACEDatFontRenderer.h"
#include "ACEUIResourceResolver.generated.h"

class UTexture2D;
class UACEDatSubsystem;

/**
 * Maps retail UI resource DIDs to Unreal assets using Docs/UI/UIAssetManifest naming:
 * AC_UI_Texture_<ID>, AC_UI_Icon_<ID>, AC_UI_Font_<ID>, AC_UI_Cursor_<ID>.
 * Until bulk extraction runs, resolves via portal DAT textures when possible.
 */
UCLASS()
class ACECLIENT_API UACEUIResourceResolver : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UACEDatSubsystem* InDat);
	void Shutdown();
	UACEDatSubsystem* GetDatSubsystem() const { return Dat.Get(); }

	UTexture2D* ResolveTexture(uint32 ResourceId, uint32 AlphaResourceId = 0);
	/** Item / spell icons — Clamp, no mips (Wrap+mip chrome fringes white on cutouts). */
	UTexture2D* ResolveIconTexture(uint32 ResourceId);
	uint32 ResolveItemBackgroundId(uint32 ItemType);
	uint32 ResolveTargetIndicatorId(uint32 ImageEnum);
	UTexture2D* ResolveRadarBlip(int32 Shape, bool bSelected);
	/** Retail EnumMapper::GetString via the client enum-to-resource map. */
	FString ResolveEnumString(uint32 EnumId, uint32 Value);
	/** gmPaperDollUI's authored pixel selection mask, in DAT coordinates. */
	uint32 ResolvePaperDollSelectionMask(FIntPoint Point);
	uint32 ResolvePaperDollAnimation(int32 Heritage);
	UTexture2D* ResolveItemBackground(uint32 ItemType, uint32 UnderlayId);
	UTexture2D* ResolveItemForeground(uint32 IconId, uint32 OverlayId, uint32 Effects, bool bForSale = false);
	/** Retail power background, spell mask, beneficial/harmful effect and target overlay. */
	UTexture2D* ResolveSpellIcon(uint32 SpellId);
	UTexture2D* ResolveFloatingHealthTexture(uint32 ResourceId);
	bool ResolveFont(uint32 FontId, FACEDatFont& OutFont);
	UTexture2D* ResolveFontAtlas(uint32 TextureId, bool& bOutTint);
	/**
	 * Render retail DAT bitmap font text (0x40…). OutW/OutH receive pixel size.
	 * bOutline uses the font's background atlas when present (fancy Enter label).
	 */
	UTexture2D* RenderDatText(uint32 FontId, const FString& Text, FColor Color,
		bool bOutline = false, FColor OutlineColor = FColor(20, 12, 4, 255),
		int32* OutWidth = nullptr, int32* OutHeight = nullptr,
		int32 BoxW = 0, int32 BoxH = 0);
	FString GetSuggestedAssetName(uint32 ResourceId, FName Kind) const;

	struct FCursorDesc
	{
		uint32 ImageId = 0;
		uint32 HotX = 0;
		uint32 HotY = 0;
	};
	bool TryGetCursor(uint32 CursorImageId, FCursorDesc& Out) const;

private:
	UPROPERTY()
	TObjectPtr<UACEDatSubsystem> Dat;

	UPROPERTY()
	TMap<uint32, TObjectPtr<UTexture2D>> Cache;

	UPROPERTY()
	TMap<uint32, TObjectPtr<UTexture2D>> IconCache;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UTexture2D>> RadarMasks;

	UPROPERTY()
	TMap<uint64, TObjectPtr<UTexture2D>> CompositeCache;

	TUniquePtr<FACEDatFontRenderer> FontRenderer;
	UPROPERTY()
	TMap<uint32, TObjectPtr<UTexture2D>> FontAtlasCache;
	TMap<uint32, bool> FontAtlasTint;
	TMap<uint32, TMap<uint32, uint32>> DidMaps;
	TMap<uint32, TMap<uint32, FString>> EnumStrings;
	TMap<uint32, uint32> BaseEnumMaps;
	TArray<FColor> PaperDollClickPixels;
	FIntPoint PaperDollClickSize = FIntPoint::ZeroValue;
	uint32 ResolveMappedDid(uint32 MapperId, uint32 Key, uint32 FallbackKey);
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> ItemCompositeCache;
	UPROPERTY() TMap<uint32, TObjectPtr<UTexture2D>> SpellIconCache;
};
