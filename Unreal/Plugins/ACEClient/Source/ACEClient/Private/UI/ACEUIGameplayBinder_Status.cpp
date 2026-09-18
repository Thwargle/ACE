#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElement.h"
#include "UI/ACEUIElementManager.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACETypes.h"
#include "ACEPlayerController.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"
#include "Engine/GameInstance.h"

namespace
{
	static const FLinearColor StatusGold(0.95f, 0.82f, 0.35f, 1.f);
	static const FLinearColor StatusWhite(0.92f, 0.92f, 0.88f, 1.f);

	static constexpr float EffectsRowHeight = 24.f;
	/** Above DAT PaintZ (InOutZOrder + floaty ZLevel up to ~9k) so text is never buried. */
	static constexpr int32 StatusOverlayZ = 100000;

	/**
	 * Retail floaty indicators swap ImageFileId lit/dark pairs (PressedOverlay is hover-only
	 * in the canvas and cannot express empty vs active).
	 * Pairs from LayoutDesc 0x21000071 / UIAssetManifest.
	 */
	static void SetIndicatorDid(UACEUIElementManager* Manager, const TCHAR* Name, uint32 Did)
	{
		if (!Manager || Did == 0)
		{
			return;
		}
		if (TSharedPtr<FACEUIElement> El = Manager->FindElementByName(Name))
		{
			El->ImageFileId = Did;
			El->bVisible = true;
			// PressedOverlay is hover chrome only — never leave it stuck visible.
			for (const TSharedPtr<FACEUIElement>& Child : El->Children)
			{
				if (Child.IsValid() && Child->ElementName == TEXT("PressedOverlay"))
				{
					Child->bVisible = true; // canvas gates paint on hover/capture
				}
			}
		}
	}

	static void SetIndicatorLit(UACEUIElementManager* Manager, const TCHAR* Name, bool bLit,
		uint32 DidLit, uint32 DidDark)
	{
		SetIndicatorDid(Manager, Name, bLit ? DidLit : DidDark);
	}

	static float ComputeBurdenRatio(UACEClientSubsystem* Client)
	{
		if (!Client)
		{
			return 0.f;
		}
		FACEWorldObject SelfObj;
		Client->GetWorldObject(Client->GetPlayerGuid(), SelfObj);
		const FACEPlayerVitals Vitals = Client->GetPlayerVitals();
		const int32 Strength = FMath::Max(1, Vitals.bValid ? Vitals.GetBuffedStrength() : 100);
		const int32 NumAugs = Vitals.bValid ? FMath::Clamp(Vitals.CarryingCapacityAugs, 0, 5) : 0;
		const int32 BonusBurden = FMath::Clamp(30 * NumAugs, 0, 150);
		const int32 Capacity = FMath::Max(1, 150 * Strength + Strength * BonusBurden);
		int32 Enc = 0;
		if (!Client->TryGetPlayerEncumbrance(Enc))
		{
			Enc = FMath::Max(0, SelfObj.Burden);
		}
		return static_cast<float>(FMath::Max(0, Enc)) / static_cast<float>(Capacity);
	}
}

void UACEUIGameplayBinder::HandleEnchantmentsChanged()
{
	RefreshEffectsOverlays(true);
	RefreshEffectsOverlays(false);
	RefreshVitaePanelOverlays();
	RefreshStatusIndicators();
}

void UACEUIGameplayBinder::HandleLinkStatusChanged(const FACELinkStatus& Status)
{
	LastLinkStatus = Status;
	RefreshLinkStatusPanelOverlays();
	RefreshStatusIndicators();
}

FString UACEUIGameplayBinder::FormatEnchantmentRemaining(const FACEActiveEnchantment& E)
{
	if (E.Duration < 0.f)
	{
		return TEXT("Permanent");
	}
	// ACE StartTime is typically negative elapsed seconds since apply.
	const float Elapsed = (E.StartTime < 0.f) ? -E.StartTime : E.StartTime;
	const float Remaining = FMath::Max(0.f, E.Duration - Elapsed);
	const int32 TotalSec = FMath::RoundToInt(Remaining);
	const int32 Min = TotalSec / 60;
	const int32 Sec = TotalSec % 60;
	if (Min >= 60)
	{
		return FString::Printf(TEXT("%dh %02dm"), Min / 60, Min % 60);
	}
	return FString::Printf(TEXT("%d:%02d"), Min, Sec);
}

int32 UACEUIGameplayBinder::VitaeXpRemaining(float VitaeMul, int32 Level, int32 CpPool)
{
	const int32 L = FMath::Max(1, Level);
	const double Threshold = (FMath::Pow(static_cast<double>(L), 2.5) * 2.5 + 20.0)
		* FMath::Pow(static_cast<double>(FMath::Clamp(VitaeMul, 0.01f, 1.f)), 5.0) + 0.5;
	return FMath::Max(0, static_cast<int32>(Threshold) - FMath::Max(0, CpPool));
}

void UACEUIGameplayBinder::RefreshEffectsOverlays(bool bPositive)
{
	const FString PageName = bPositive
		? TEXT("PositiveEffectsPanel_Field")
		: TEXT("NegativeEffectsPanel_Field");
	auto HideAll = [this]()
	{
		for (UTextBlock* R : EffectsListRows)
		{
			if (R) { R->SetVisibility(ESlateVisibility::Collapsed); }
		}
		for (UBorder* I : EffectsListIcons)
		{
			if (I) { I->SetVisibility(ESlateVisibility::Collapsed); }
		}
		if (EffectsInfoLabel) { EffectsInfoLabel->SetVisibility(ESlateVisibility::Collapsed); }
		if (EffectsTitleLabel) { EffectsTitleLabel->SetVisibility(ESlateVisibility::Collapsed); }
	};

	if (!Client || !Manager || !Canvas || ActivePanelPage != PageName)
	{
		if (ActivePanelPage != TEXT("PositiveEffectsPanel_Field")
			&& ActivePanelPage != TEXT("NegativeEffectsPanel_Field"))
		{
			HideAll();
		}
		return;
	}

	EnsureOverlays();
	UACEDatSubsystem* Dat = nullptr;
	if (PlayerController)
	{
		if (UGameInstance* GI = PlayerController->GetGameInstance())
		{
			Dat = GI->GetSubsystem<UACEDatSubsystem>();
		}
	}

	TArray<FACEActiveEnchantment> All = Client->GetActiveEnchantments();
	TArray<FACEActiveEnchantment> Filtered;
	Filtered.Reserve(All.Num());
	for (const FACEActiveEnchantment& E : All)
	{
		if (E.bVitae || E.SpellId == 666)
		{
			continue;
		}
		const bool bBen = E.bBeneficial;
		if (bPositive == bBen)
		{
			Filtered.Add(E);
		}
	}

	if (!EffectsTitleLabel && Canvas->WidgetTree)
	{
		EffectsTitleLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10);
		EffectsTitleLabel->SetFont(Font);
	}
	PlaceTextUnder(EffectsTitleLabel, PageName, TEXT("Effects_TitleText"),
		bPositive ? TEXT("Spells in Effect") : TEXT("Harmful Spells in Effect"),
		10, StatusGold, StatusOverlayZ);
	EffectsTitleLabel->SetJustification(ETextJustify::Center);

	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(PageName, TEXT("Effects_SpellList"));
	if (!ListEl.IsValid())
	{
		// Layout merge must provide Effects_SpellList; place empty-state on the page body.
		PlaceTextUnder(EffectsInfoLabel, PageName, TEXT("Effects_TitleText"),
			TEXT("(Effects list missing from layout — restart after UI regen.)"), 8, StatusWhite, StatusOverlayZ);
		return;
	}
	if (TSharedPtr<FACEUIElement> InfoBg = Manager->FindElementUnder(PageName, TEXT("InfoBackground")))
	{
		if (InfoBg->ImageFileId == 0)
		{
			InfoBg->ImageFileId = 0x06001395u;
			InfoBg->DrawMode = 1;
		}
	}

	const int32 EffectsVisibleRows = ListEl.IsValid()
		? FMath::Max(1, ListEl->Height / FMath::RoundToInt(EffectsRowHeight))
		: 10;
	const int32 MaxOffset = FMath::Max(0, Filtered.Num() - EffectsVisibleRows);
	EffectsScrollOffset = FMath::Clamp(EffectsScrollOffset, 0, MaxOffset);
	EffectsListSpellIds.SetNum(EffectsVisibleRows);

	bool bHaveSelected = false;
	for (const FACEActiveEnchantment& E : Filtered)
	{
		if (E.SpellId == SelectedEffectsSpellId)
		{
			bHaveSelected = true;
			break;
		}
	}
	if (!bHaveSelected)
	{
		SelectedEffectsSpellId = Filtered.Num() > 0 ? Filtered[0].SpellId : 0;
	}

	FACEActiveEnchantment Selected;
	bool bFoundSelected = false;

	for (int32 Row = 0; Row < EffectsVisibleRows; ++Row)
	{
		const int32 Idx = EffectsScrollOffset + Row;
		UTextBlock* Label = nullptr;
		if (EffectsListRows.IsValidIndex(Row))
		{
			Label = EffectsListRows[Row];
		}
		if (!Label && Canvas->WidgetTree)
		{
			Label = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
			FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9);
			Label->SetFont(Font);
			if (EffectsListRows.Num() <= Row)
			{
				EffectsListRows.SetNum(Row + 1);
			}
			EffectsListRows[Row] = Label;
		}
		UBorder* Icon = EnsureIconBorder(EffectsListIcons, Row);
		if (Idx >= Filtered.Num())
		{
			EffectsListSpellIds[Row] = 0;
			if (Label) { Label->SetVisibility(ESlateVisibility::Collapsed); }
			if (Icon) { Icon->SetVisibility(ESlateVisibility::Collapsed); }
			continue;
		}

		const FACEActiveEnchantment& E = Filtered[Idx];
		EffectsListSpellIds[Row] = E.SpellId;
		if (E.SpellId == SelectedEffectsSpellId)
		{
			Selected = E;
			bFoundSelected = true;
		}

		FString SpellName = FString::Printf(TEXT("Spell %d"), E.SpellId);
		uint32 IconDid = 0;
		if (Dat)
		{
			Dat->TryGetSpellInfo(static_cast<uint32>(E.SpellId), SpellName, IconDid);
		}
		const FString Line = Client && Client->IsCharacterOptionSet(0x15)
			? FString::Printf(TEXT("%s  %s"), *SpellName, *FormatEnchantmentRemaining(E))
			: SpellName;
		const bool bSel = (E.SpellId == SelectedEffectsSpellId);
		const float RowTop = 2.f + Row * EffectsRowHeight;
		const float RowBottom = FMath::Max(2.f,
			static_cast<float>(ListEl->Height) - (RowTop + EffectsRowHeight));
		if (Label)
		{
			Label->SetVisibility(ESlateVisibility::Visible);
			Label->SetText(FText::FromString(Line));
			Label->SetColorAndOpacity(FSlateColor(bSel ? StatusGold : StatusWhite));
			Canvas->PlaceWidgetAtElement(Label, ListEl, StatusOverlayZ + Row,
				FMargin(28.f, RowTop, 8.f, RowBottom));
		}
		if (Icon)
		{
			SetSpellIcon(Icon, E.SpellId);
			Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
			Canvas->PlaceWidgetAtElement(Icon, ListEl, StatusOverlayZ + 50 + Row,
				FMargin(4.f, RowTop,
					static_cast<float>(ListEl->Width) - 24.f,
					RowBottom));
		}
	}

	if (!EffectsInfoLabel && Canvas->WidgetTree)
	{
		EffectsInfoLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8);
		EffectsInfoLabel->SetFont(Font);
	}
	FString Info;
	if (bFoundSelected)
	{
		FString SpellName = FString::Printf(TEXT("Spell %d"), Selected.SpellId);
		uint32 IconDid = 0;
		if (Dat)
		{
			Dat->TryGetSpellInfo(static_cast<uint32>(Selected.SpellId), SpellName, IconDid);
		}
		if (Client && Client->IsCharacterOptionSet(0x15))
		{
			Info = FString::Printf(TEXT("%s\nTime remaining: %s"),
				*SpellName, *FormatEnchantmentRemaining(Selected));
		}
		else
		{
			Info = SpellName;
		}
		if (Selected.StatModValue != 0.f)
		{
			Info += FString::Printf(TEXT("\nMagnitude: %.2f"), Selected.StatModValue);
		}
	}
	else if (Filtered.Num() == 0)
	{
		Info = bPositive
			? TEXT("You have no positive enchantments.")
			: TEXT("You have no negative enchantments.");
	}
	PlaceTextUnder(EffectsInfoLabel, PageName, TEXT("Effects_InfoText"), Info, 8, StatusWhite, StatusOverlayZ + 100);

	if (TSharedPtr<FACEUIElement> Bar = Manager->FindElementUnder(PageName, TEXT("Effects_SpellList_Scrollbar")))
	{
		const float VisFrac = Filtered.Num() > 0
			? FMath::Clamp(static_cast<float>(EffectsVisibleRows) / static_cast<float>(Filtered.Num()), 0.1f, 1.f)
			: 1.f;
		const float Frac = MaxOffset > 0
			? static_cast<float>(EffectsScrollOffset) / static_cast<float>(MaxOffset)
			: 0.f;
		SyncDatScrollbar(Bar, Frac, VisFrac);
	}
}

bool UACEUIGameplayBinder::TryHandleEffectsListClick(FVector2D Absolute)
{
	if (ActivePanelPage != TEXT("PositiveEffectsPanel_Field")
		&& ActivePanelPage != TEXT("NegativeEffectsPanel_Field"))
	{
		return false;
	}
	for (int32 i = 0; i < EffectsListRows.Num() && i < EffectsListSpellIds.Num(); ++i)
	{
		UTextBlock* Label = EffectsListRows[i];
		if (!Label || Label->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		if (Canvas->IsWidgetExposedAt(Label, Absolute))
		{
			const int32 SpellId = EffectsListSpellIds[i];
			if (SpellId != 0)
			{
				SelectedEffectsSpellId = SpellId;
				RefreshEffectsOverlays(ActivePanelPage == TEXT("PositiveEffectsPanel_Field"));
				return true;
			}
		}
	}
	return false;
}

void UACEUIGameplayBinder::RefreshVitaePanelOverlays()
{
	if (!Client || !Manager || !Canvas || ActivePanelPage != TEXT("VitaePanel_Field"))
	{
		if (VitaePanelLabel)
		{
			VitaePanelLabel->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (EffectsTitleLabel && ActivePanelPage != TEXT("PositiveEffectsPanel_Field")
			&& ActivePanelPage != TEXT("NegativeEffectsPanel_Field"))
		{
			// Title label is shared with effects — leave alone when those pages own it.
		}
		return;
	}
	EnsureOverlays();
	if (!VitaePanelLabel && Canvas->WidgetTree)
	{
		VitaePanelLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
		VitaePanelLabel->SetFont(Font);
	}
	if (!EffectsTitleLabel && Canvas->WidgetTree)
	{
		EffectsTitleLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10);
		EffectsTitleLabel->SetFont(Font);
	}
	PlaceTextUnder(EffectsTitleLabel, TEXT("VitaePanel_Field"), TEXT("TitleText"),
		TEXT("Vitae"), 10, StatusGold, StatusOverlayZ);
	EffectsTitleLabel->SetJustification(ETextJustify::Center);

	float VitaeMul = 1.f;
	const bool bHasVitae = Client->TryGetVitaeMultiplier(VitaeMul);
	FString Body;
	if (!bHasVitae || VitaeMul >= 0.999f)
	{
		Body = TEXT("You are at full strength.\n\nVitae weakens your skills and attributes after death. Earn experience to recover.");
	}
	else
	{
		const int32 PenaltyPct = FMath::Clamp(100 - FMath::RoundToInt(VitaeMul * 100.f), 1, 99);
		const FACEPlayerVitals Vitals = Client->GetPlayerVitals();
		const int32 Level = Client->GetDeathLevel() > 0
			? Client->GetDeathLevel()
			: (Vitals.bValid ? Vitals.Level : 1);
		const int32 XpLeft = VitaeXpRemaining(VitaeMul, Level, Client->GetVitaeCpPool());
		Body = FString::Printf(
			TEXT("Your Vitae is reduced by %d%%.\n\n"
				"Your skills are reduced by %d%%.\n\n"
				"Experience needed to restore Vitae: %d"),
			PenaltyPct, PenaltyPct, XpLeft);
	}
	PlaceTextUnder(VitaePanelLabel, TEXT("VitaePanel_Field"), TEXT("VitaeText"), Body, 10, StatusWhite, StatusOverlayZ + 1);
}

void UACEUIGameplayBinder::RefreshLinkStatusPanelOverlays()
{
	if (!Client || !Manager || !Canvas || ActivePanelPage != TEXT("LinkStatusPanel_Field"))
	{
		if (LinkStatusPanelLabel)
		{
			LinkStatusPanelLabel->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}
	EnsureOverlays();
	if (!LinkStatusPanelLabel && Canvas->WidgetTree)
	{
		LinkStatusPanelLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
		LinkStatusPanelLabel->SetFont(Font);
	}
	if (!EffectsTitleLabel && Canvas->WidgetTree)
	{
		EffectsTitleLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10);
		EffectsTitleLabel->SetFont(Font);
	}
	PlaceTextUnder(EffectsTitleLabel, TEXT("LinkStatusPanel_Field"), TEXT("TitleText"),
		TEXT("Link Status"), 10, StatusGold, StatusOverlayZ);

	LastLinkStatus = Client->GetLinkStatus();
	FString PingStr = TEXT("????");
	if (LastLinkStatus.bHasPing && LastLinkStatus.RoundTripSeconds >= 0.f)
	{
		PingStr = FString::Printf(TEXT("%.0f"), LastLinkStatus.RoundTripSeconds * 1000.f);
	}
	const FString Body = FString::Printf(
		TEXT("Connection status\n\n"
			"Green: good\nYellow: fair\nOrange: poor\nRed: critical\n\n"
			"Packet loss: %.1f%%\n"
			"Ping: %s ms"),
		LastLinkStatus.PacketLossPercent,
		*PingStr);
	PlaceTextUnder(LinkStatusPanelLabel, TEXT("LinkStatusPanel_Field"), TEXT("LinkStatusText"),
		Body, 10, StatusWhite, StatusOverlayZ + 1);
}

void UACEUIGameplayBinder::RefreshStatusIndicators()
{
	if (!Manager || !Client)
	{
		return;
	}

	// Lit / dark DIDs — layout defaults are the lit art; empty states use the paired dark DID.
	constexpr uint32 DidPosLit = 0x0600749Cu, DidPosDark = 0x0600749Du;
	constexpr uint32 DidNegLit = 0x0600749Eu, DidNegDark = 0x0600749Fu;
	constexpr uint32 DidVitaeLit = 0x060074A0u, DidVitaeDark = 0x060074A1u;
	constexpr uint32 DidBurdenEmpty = 0x060074A2u, DidBurdenYellow = 0x060074A3u, DidBurdenRed = 0x060074A4u;
	constexpr uint32 DidChessLit = 0x060074A5u, DidChessDark = 0x060074A6u;
	// Link: 7498 good, 7499 fair, 749A poor (layout default = good).
	constexpr uint32 DidLinkGood = 0x06007498u, DidLinkFair = 0x06007499u, DidLinkPoor = 0x0600749Au;

	const float Burden = ComputeBurdenRatio(Client);
	// Dark when under capacity; yellow from 100%; red when heavily overburdened.
	uint32 BurdenDid = DidBurdenEmpty;
	if (Burden >= 2.f)
	{
		BurdenDid = DidBurdenRed;
	}
	else if (Burden >= 1.f)
	{
		BurdenDid = DidBurdenYellow;
	}
	SetIndicatorDid(Manager, TEXT("BurdenIndicator"), BurdenDid);

	float VitaeMul = 1.f;
	const bool bVitae = Client->TryGetVitaeMultiplier(VitaeMul) && VitaeMul < 0.999f;
	SetIndicatorLit(Manager, TEXT("VitaeIndicator"), bVitae, DidVitaeLit, DidVitaeDark);

	int32 PosCount = 0;
	int32 NegCount = 0;
	for (const FACEActiveEnchantment& E : Client->GetActiveEnchantments())
	{
		if (E.bVitae || E.SpellId == 666)
		{
			continue;
		}
		if (E.bBeneficial) { ++PosCount; }
		else { ++NegCount; }
	}
	SetIndicatorLit(Manager, TEXT("PositiveEffectsIndicator"), PosCount > 0, DidPosLit, DidPosDark);
	SetIndicatorLit(Manager, TEXT("NegativeEffectsIndicator"), NegCount > 0, DidNegLit, DidNegDark);

	// Chess/minigame: lit only while that panel is open (no active-match state wired yet).
	const bool bChess = ActivePanelPage == TEXT("MiniGamePanel_Field");
	SetIndicatorLit(Manager, TEXT("MiniGameIndicator"), bChess, DidChessLit, DidChessDark);

	LastLinkStatus = Client->GetLinkStatus();
	const float RttMs = LastLinkStatus.bHasPing
		? LastLinkStatus.RoundTripSeconds * 1000.f
		: -1.f;
	uint32 LinkDid = DidLinkGood;
	if (!LastLinkStatus.bConnected || (RttMs >= 0.f && RttMs > 200.f)
		|| LastLinkStatus.PacketLossPercent > 10.f)
	{
		LinkDid = DidLinkPoor;
	}
	else if ((RttMs >= 0.f && RttMs > 80.f) || LastLinkStatus.PacketLossPercent > 3.f)
	{
		LinkDid = DidLinkFair;
	}
	SetIndicatorDid(Manager, TEXT("LinkStatusIndicator"), LinkDid);
}

void UACEUIGameplayBinder::TickStatusPanels(float /*DeltaSeconds*/)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastStatusIndicatorRefreshAt >= 1.0)
	{
		LastStatusIndicatorRefreshAt = Now;
		RefreshStatusIndicators();
		if (ActivePanelPage == TEXT("InventoryPanel_Field"))
		{
			RefreshInventoryBurdenOverlays();
		}
	}

	if (ActivePanelPage == TEXT("LinkStatusPanel_Field") && Client)
	{
		if (Now - LastLinkPingRequestAt >= 5.0)
		{
			LastLinkPingRequestAt = Now;
			Client->SendPingRequest();
		}
		RefreshLinkStatusPanelOverlays();
	}

	if (ActivePanelPage == TEXT("PositiveEffectsPanel_Field"))
	{
		RefreshEffectsOverlays(true);
	}
	else if (ActivePanelPage == TEXT("NegativeEffectsPanel_Field"))
	{
		RefreshEffectsOverlays(false);
	}
	else if (ActivePanelPage == TEXT("VitaePanel_Field"))
	{
		RefreshVitaePanelOverlays();
	}
}
