#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElement.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "Dat/ACEDatTextLayout.h"
#include "Components/ScrollBox.h"
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

	static constexpr float EffectsRowHeight = 32.f; // DAT 2100001B / 10000128.
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
		return FString(); // EffectInfoRegion::Update leaves permanent values blank.
	}
	const double SinceReceipt = E.ReceivedAt > 0.0 ? FMath::Max(0.0, FPlatformTime::Seconds()-E.ReceivedAt) : 0.0;
	const double Remaining = E.Duration + E.StartTime - SinceReceipt;
	if (!FMath::IsFinite(Remaining) || Remaining < 0.0) return FString();
	const int32 TotalSec = FMath::FloorToInt(FMath::Min(Remaining, double(MAX_int32)));
	const int32 Min = TotalSec / 60;
	const int32 Sec = TotalSec % 60;
	if (Min >= 60)
	{
		return FString::Printf(TEXT("%d:%02d:%02d"), Min / 60, Min % 60, Sec);
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
		for (UTextBlock* R : EffectsListDurations) if (R) R->SetVisibility(ESlateVisibility::Collapsed);
		for (const auto& Row : EffectsRowElements) Row->bVisible = false;
		if (EffectsInfoScroll) EffectsInfoScroll->SetVisibility(ESlateVisibility::Collapsed);
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
	UACEDatSubsystem* Dat = Canvas->GetResourceResolver() ? Canvas->GetResourceResolver()->GetDatSubsystem() : nullptr;
	if (PlayerController)
	{
		if (UGameInstance* GI = PlayerController->GetGameInstance())
		{
			Dat = GI->GetSubsystem<UACEDatSubsystem>();
		}
	}

	TArray<FACEActiveEnchantment> All = Client->GetActiveEnchantments();
	// CEnchantmentRegistry::Duel: only the strongest effect per category is in
	// effect; equal power prefers the more recently applied enchantment.
	TMap<int32, FACEActiveEnchantment> Winners;
	for (const FACEActiveEnchantment& E : All)
	{
		if (E.bVitae || E.SpellId == 666 || E.bCooldown || E.SpellId >= 0x8000)
		{
			continue;
		}
		auto* Existing = Winners.Find(E.SpellCategory);
		if (!Existing || E.PowerLevel > Existing->PowerLevel
			|| (E.PowerLevel == Existing->PowerLevel && E.ReceivedAt+E.StartTime >= Existing->ReceivedAt+Existing->StartTime))
			Winners.Add(E.SpellCategory,E);
	}
	TArray<FACEActiveEnchantment> Filtered;
	TMap<int32,FString> Names;
	for (const auto& Pair : Winners) if (Pair.Value.bBeneficial == bPositive)
	{
		const auto& E=Pair.Value;
		FString Name=FString::Printf(TEXT("Spell %d"),E.SpellId); uint32 Icon=0;
		if (Dat) Dat->TryGetSpellInfo(E.SpellId,Name,Icon);
		if (Name.IsEmpty()) Name=FString::Printf(TEXT("Spell %d"),E.SpellId);
		Names.Add(E.SpellId,Name); Filtered.Add(E);
	}
	Filtered.Sort([&](const auto& A,const auto& B){return Names[A.SpellId].Compare(Names[B.SpellId],ESearchCase::CaseSensitive)<0;});
	EffectsContentCount=Filtered.Num();

	if (!EffectsTitleLabel && Canvas->WidgetTree)
	{
		EffectsTitleLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10);
		EffectsTitleLabel->SetFont(Font);
	}
	PlaceTextUnder(EffectsTitleLabel, PageName, TEXT("Effects_TitleText"),
		bPositive ? TEXT("Spells in Effect") : TEXT("Harmful Spells in Effect"),
		10, FLinearColor::White, StatusOverlayZ);
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
		SelectedEffectsSpellId = 0;
	}

	for (int32 Row = 0; Row < FMath::Max(EffectsVisibleRows,EffectsRowElements.Num()); ++Row)
	{
		if (Row >= EffectsRowElements.Num())
		{
			auto Entry=UACEUILayoutResolver::LoadTemplate(0x2100001B,0x10000128);
			if (!Entry) break;
			Entry->SetElementName(FString::Printf(TEXT("ActiveEffectRow_%d"),Row));
			Entry->bUseExplicitState=true;
			EffectsRowElements.Add(Entry);
			EffectsListRows.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
			EffectsListDurations.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
		}
		const auto Entry=EffectsRowElements[Row];
		if (Entry->Parent.Pin()!=ListEl)
		{
			if (auto Old=Entry->Parent.Pin()) Old->RemoveChild(Entry);
			ListEl->AddChild(Entry); Manager->InvalidateNameLookupIndex();
		}
		auto* Label=EffectsListRows[Row].Get();
		auto* Duration=EffectsListDurations[Row].Get();
		auto* Icon=EnsureIconBorder(EffectsListIcons,Row);
		const int32 Idx=EffectsScrollOffset+Row;
		Entry->bVisible=Row<EffectsVisibleRows && Filtered.IsValidIndex(Idx);
		if (!Entry->bVisible)
		{
			UWidget* Widgets[]={Label,Duration,Icon};
			for (UWidget* W : Widgets)
				if (W) W->SetVisibility(ESlateVisibility::Collapsed);
			if (EffectsListSpellIds.IsValidIndex(Row)) EffectsListSpellIds[Row]=0;
			continue;
		}
		const auto& E=Filtered[Idx];
		EffectsListSpellIds[Row]=E.SpellId;
		Entry->Y=Row*EffectsRowHeight; Entry->Width=ListEl->Width;
		Entry->DefaultState=E.SpellId==SelectedEffectsSpellId ? 6 : 1;
		for (const auto& Child : Entry->Children)
		{
			if (Child->ElementName==TEXT("InfoRegion_Label"))
			{
				Child->Width=FMath::Max(1,ListEl->Width-112); // 37px icon/name gap, 50px timer, 25px scrollbar.
				FString Name=Names[E.SpellId];
				PlaceTextOnElement(Label,Child,Name,10,FLinearColor::White,StatusOverlayZ+2);
				// The DAT template requests one-line truncation, not wrapping into
				// another row or painting under the duration/scrollbar.
				if (const auto* Font=Cast<UACERetailTextBlock>(Label)->GetBitmapFont())
				{
					int32 Width=0; for (TCHAR C:Name) Width+=ACEDatText::Advance(*Font,C);
					if (Width>Child->Width)
					{
						const int32 Dots=3*ACEDatText::Advance(*Font,'.');
						while (!Name.IsEmpty() && Width+Dots>Child->Width)
						{Width-=ACEDatText::Advance(*Font,Name[Name.Len()-1]);Name.LeftChopInline(1);}
						Name+=TEXT("..."); Label->SetText(FText::FromString(Name));
					}
				}
				SetRetailTooltip(Label,FText::FromString(Names[E.SpellId]));
			}
			else if (Child->ElementName==TEXT("InfoRegion_Value"))
			{
				Child->X=ListEl->Width-75;
				PlaceTextOnElement(Duration,Child,Client->IsCharacterOptionSet(0x15) ? FormatEnchantmentRemaining(E) : FString(),
					10,FLinearColor::White,StatusOverlayZ+2);
			}
			else if (Child->ElementName==TEXT("InfoRegion_Icon"))
			{
				SetSpellIcon(Icon,E.SpellId); Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
				Canvas->PlaceWidgetAtElement(Icon,Child,StatusOverlayZ+1);
			}
		}
	}

	if (!EffectsInfoLabel) EffectsInfoLabel=Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
	FString Info=Filtered.IsEmpty() ? TEXT("There are no spells in effect.") : TEXT("Select a spell to see its description.");
	if (SelectedEffectsSpellId && Names.Contains(SelectedEffectsSpellId))
	{
		FString Description;
		if (Dat) Dat->TryGetSpellDescription(SelectedEffectsSpellId,Description);
		Info=Names[SelectedEffectsSpellId]+TEXT("\n\n")+Description;
	}
	const auto InfoEl=Manager->FindElementUnder(PageName,TEXT("Effects_InfoText"));
	if (InfoEl)
	{
		if (!EffectsInfoScroll)
		{
			EffectsInfoScroll=Canvas->WidgetTree->ConstructWidget<UScrollBox>();
			EffectsInfoScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
			EffectsInfoScroll->SetClipping(EWidgetClipping::ClipToBounds);
			EffectsInfoScroll->SetAnimateWheelScrolling(false);
			EffectsInfoScroll->AddChild(EffectsInfoLabel);
		}
		if (EffectsInfoSpellId!=SelectedEffectsSpellId)
		{EffectsInfoScroll->SetScrollOffset(0);EffectsInfoSpellId=SelectedEffectsSpellId;}
		EffectsInfoLabel->SetText(FText::FromString(Info));
		EffectsInfoLabel->SetJustification(ETextJustify::Center);
		EffectsInfoLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		EffectsInfoLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
		Cast<UACERetailTextBlock>(EffectsInfoLabel)->SetRetailElement(Canvas->GetResourceResolver(),InfoEl,Canvas->GetLastScale2D(),InfoEl->Width,false);
		EffectsInfoScroll->SetVisibility(ESlateVisibility::Visible);
		Canvas->PlaceWidgetAtElement(EffectsInfoScroll,InfoEl,StatusOverlayZ+100);
		const float Max=EffectsInfoScroll->GetScrollOffsetOfEnd(),Height=InfoEl->Height*Canvas->GetLastScale2D().Y;
		SyncDatScrollbar(Manager->FindElementUnder(PageName,TEXT("Effects_InfoText_Scrollbar")),
			Max>0 ? EffectsInfoScroll->GetScrollOffset()/Max : 0,Height/FMath::Max(1.f,Height+Max));
	}

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
		if (Canvas->IsWidgetExposedAt(Label, Absolute)
			|| (EffectsListDurations.IsValidIndex(i) && Canvas->IsWidgetExposedAt(EffectsListDurations[i],Absolute))
			|| (EffectsListIcons.IsValidIndex(i) && Canvas->IsWidgetExposedAt(EffectsListIcons[i],Absolute)))
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
		TEXT("Link Status"), 10, StatusWhite, StatusOverlayZ);

	LastLinkStatus = Client->GetLinkStatus();
	FString PingStr = TEXT("????");
	if (LastLinkStatus.bHasPing && LastLinkStatus.RoundTripSeconds > 0.f)
	{
		PingStr = FString::Printf(TEXT("%.0f"), LastLinkStatus.RoundTripSeconds * 1000.f);
	}
	if (!bLoadedLinkStatusStrings)
	{
		bLoadedLinkStatusStrings = true;
		if (const auto* Resources = Canvas->GetResourceResolver())
			if (const auto* Dat = Resources->GetDatSubsystem())
				LinkStatusStrings.LoadStrings(Dat->GetDatDirectory(), 0x23000001);
	}
	const FString Body = LinkStatusStrings.Text(TEXT("ID_LinkStatus_Info"))
		+ LinkStatusStrings.Text(TEXT("ID_LinkStatus_Colors"))
		+ LinkStatusStrings.Text(TEXT("ID_LinkStatus_Disconnect"))
		+ LinkStatusStrings.FormatText(TEXT("ID_LinkStatus_PacketLoss"),
			{{TEXT("PACKET_LOSS"), FString::Printf(TEXT("%.2f"), LastLinkStatus.PacketLossPercent)}})
		+ LinkStatusStrings.FormatText(TEXT("ID_LinkStatus_Ping"), {{TEXT("PING"), PingStr}});
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
		if (E.bVitae || E.SpellId == 666 || E.bCooldown || E.SpellId >= 0x8000)
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
	uint32 LinkDid = DidLinkGood;
	// Retail colors reflect time without server packets, not internet latency.
	if (!LastLinkStatus.bConnected || LastLinkStatus.SecondsSinceLastPacket >= 20.f)
	{
		LinkDid = DidLinkPoor;
	}
	else if (LastLinkStatus.SecondsSinceLastPacket >= 5.f)
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
