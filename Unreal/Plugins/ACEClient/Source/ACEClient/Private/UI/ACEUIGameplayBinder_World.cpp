#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACERetailMap.h"
#include "UI/ACEUIResourceResolver.h"
#include "Dat/ACEDatTextLayout.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatFileTypes.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"

namespace
{
	static const FLinearColor WorldGold(0.95f, 0.82f, 0.35f, 1.f);
	static const FLinearColor WorldInk(0.16f, 0.12f, 0.06f, 1.f);
	static const FLinearColor WorldDim(0.38f, 0.30f, 0.18f, 1.f);
	constexpr int32 WorldTabHeight = 25;
	constexpr int32 HouseRowHeight = 15;
	constexpr int32 WorldZ = 100000;
	/**
	 * The Dereth map art covers the full 255-landblock grid: 204 map units of
	 * 240m each (255 * 192m = 48960m), so a global metre position maps linearly
	 * onto the Map element rect.
	 */
	constexpr float DerethExtentMeters = 48960.f;
	/** Retail maintenance period (House.RentInterval), tripled for apartments. */
	constexpr int64 RentIntervalSeconds = 30 * 24 * 60 * 60;

	FVector2D GlobalMetersFrom(const FACEPosition& Pos)
	{
		const uint32 Cell = static_cast<uint32>(Pos.CellId);
		return FVector2D(
			((Cell >> 24) & 0xFF) * 192.f + Pos.Location.X,
			((Cell >> 16) & 0xFF) * 192.f + Pos.Location.Y);
	}

	bool IsOutdoorCell(const FACEPosition& Pos)
	{
		return Pos.IsValid() && (static_cast<uint32>(Pos.CellId) & 0xFFFF) < 0x100;
	}

	FString FormatUnixDate(int64 UnixSeconds)
	{
		if (UnixSeconds <= 0)
		{
			return TEXT("unknown");
		}
		const FDateTime Time = FDateTime::FromUnixTimestamp(UnixSeconds);
		return FText::AsDate(Time, EDateTimeStyle::Medium, FText::GetInvariantTimeZone()).ToString();
	}
}

FString UACEUIGameplayBinder::FormatMapCoords(const FACEPosition& Pos)
{
	if (!IsOutdoorCell(Pos))
	{
		return FString();
	}
	// PositionExtensions.GetMapCoords: global metres / 240, centred on 102 map units.
	const FVector2D Global = GlobalMetersFrom(Pos);
	const float NS = Global.Y / 240.f - 102.f;
	const float EW = Global.X / 240.f - 102.f;
	return FString::Printf(TEXT("%.1f%s, %.1f%s"),
		FMath::Abs(NS) - 0.05f, NS >= 0.f ? TEXT("N") : TEXT("S"),
		FMath::Abs(EW) - 0.05f, EW >= 0.f ? TEXT("E") : TEXT("W"));
}

FString UACEUIGameplayBinder::FormatDerethDateTime(double GameTicks) const
{
	if (GameTicks <= 0.0)
	{
		return FString();
	}
	// Align hour with GetSky: Fmod(Ticks - ZeroTimeOfYear, DayLength).
	// Darktide = hour 0 at fraction 0 (not the tick-0 Morntide-and-Half calendar epoch).
	double DayLength = 7620.0;
	double ZeroTimeOfYear = 3600.0;
	if (Client)
	{
		if (UWorld* World = Client->GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
				{
					if (const FACEDatRegionSky* Sky = Dat->GetRegionSkyInfo())
					{
						if (Sky->DayLengthSeconds > 1.f)
						{
							DayLength = static_cast<double>(Sky->DayLengthSeconds);
						}
						ZeroTimeOfYear = Sky->ZeroTimeOfYear;
					}
				}
			}
		}
	}
	static const TCHAR* MonthNames[] = {
		TEXT("Morningthaw"), TEXT("Solclaim"), TEXT("Seedsow"), TEXT("Leafdawning"),
		TEXT("Verdantine"), TEXT("Thistledown"), TEXT("Harvestgain"), TEXT("Leafcull"),
		TEXT("Frostfell"), TEXT("Snowreap"), TEXT("Coldeve"), TEXT("Wintersebb"),
	};
	static const TCHAR* HourNames[] = {
		TEXT("Darktide"), TEXT("Darktide-and-Half"), TEXT("Foredawn"), TEXT("Foredawn-and-Half"),
		TEXT("Dawnsong"), TEXT("Dawnsong-and-Half"), TEXT("Morntide"), TEXT("Morntide-and-Half"),
		TEXT("Midsong"), TEXT("Midsong-and-Half"), TEXT("Warmtide"), TEXT("Warmtide-and-Half"),
		TEXT("Evensong"), TEXT("Evensong-and-Half"), TEXT("Gloaming"), TEXT("Gloaming-and-Half"),
	};
	double Phase = FMath::Fmod(GameTicks - ZeroTimeOfYear, DayLength);
	if (Phase < 0.0)
	{
		Phase += DayLength;
	}
	const double HourTicks = DayLength / 16.0;
	const int32 HourIndex = FMath::Clamp(static_cast<int32>(Phase / HourTicks), 0, 15);
	// Date still advances from tick 0 = Morningthaw 1, 10 P.Y. (DerethDateTime epoch).
	const int64 HoursElapsed = static_cast<int64>(GameTicks / HourTicks);
	const int64 AbsHour = 7 + HoursElapsed;
	const int64 DayOffset = AbsHour / 16;
	const int32 Day = static_cast<int32>(DayOffset % 30) + 1;
	const int64 MonthsElapsed = DayOffset / 30;
	const int32 Month = static_cast<int32>(MonthsElapsed % 12);
	const int32 Year = 10 + static_cast<int32>(MonthsElapsed / 12);
	return FString::Printf(TEXT("%s %d, %d P.Y.  %s"),
		MonthNames[Month], Day, Year, HourNames[HourIndex]);
}

void UACEUIGameplayBinder::SyncWorldPanelTab(const FString& PageName)
{
	if (!Manager)
	{
		return;
	}
	ActiveWorldTab = PageName;
	HouseScrollOffset = 0;
	static const TCHAR* Pages[] = { TEXT("MapPage"), TEXT("HousePage") };
	for (const TCHAR* Page : Pages)
	{
		if (TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(TEXT("WorldPanel_Field"), Page))
		{
			El->bVisible = (PageName == Page);
		}
	}
	ApplyPanelTabChrome(TEXT("MapTab"), PageName == TEXT("MapPage"));
	ApplyPanelTabChrome(TEXT("HouseTab"), PageName == TEXT("HousePage"));
	if (PageName == TEXT("HousePage") && Client)
	{
		// Housing state is push-only after login; ask again whenever the tab opens.
		Client->SendHouseQuery();
		LastHouseQueryAt = FPlatformTime::Seconds();
	}
	RefreshWorldOverlays();
}

void UACEUIGameplayBinder::HideWorldOverlays()
{
	for (UTextBlock* T : WorldTabLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : HouseTextRows) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	if (MapCoordinateLabel) { MapCoordinateLabel->SetVisibility(ESlateVisibility::Collapsed); }
	if (MapDateTimeLabel) { MapDateTimeLabel->SetVisibility(ESlateVisibility::Collapsed); }
}

void UACEUIGameplayBinder::RefreshWorldOverlays()
{
	if (ActivePanelPage != TEXT("WorldPanel_Field") || !Manager || !Canvas || !Canvas->WidgetTree
		|| !Client)
	{
		HideWorldOverlays();
		return;
	}
	TSharedPtr<FACEUIElement> Panel = Manager->FindElementByName(TEXT("WorldPanel_Field"));
	if (!Panel.IsValid() || !Panel->bVisible)
	{
		HideWorldOverlays();
		return;
	}
	if (ActiveWorldTab.IsEmpty())
	{
		ActiveWorldTab = TEXT("MapPage");
	}

	const int32 PanelW = FMath::Max(160, Panel->Width);
	if (TSharedPtr<FACEUIElement> Close = Manager->FindElementUnder(TEXT("WorldPanel_Field"),
		TEXT("CloseWorldPanelButton")))
	{
		Close->X = FMath::Max(0, PanelW - Close->Width);
	}

	// Tab captions — the DAT tab art carries no text layer.
	static const TPair<const TCHAR*, const TCHAR*> Tabs[] = {
		{ TEXT("MapTab"), TEXT("Map") },
		{ TEXT("HouseTab"), TEXT("Housing") },
	};
	static const TCHAR* TabPages[] = { TEXT("MapPage"), TEXT("HousePage") };
	while (WorldTabLabels.Num() < UE_ARRAY_COUNT(Tabs))
	{
		UTextBlock* L = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		L->SetJustification(ETextJustify::Center);
		WorldTabLabels.Add(L);
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(Tabs); ++i)
	{
		if (UTextBlock* Label = WorldTabLabels[i])
		{
			PlaceTextOnElement(Label, Tabs[i].Key, Tabs[i].Value, 8,
				ActiveWorldTab == TabPages[i] ? WorldGold : FLinearColor(0.92f, 0.92f, 0.88f, 1.f),
				WorldZ, true);
		}
	}

	TSharedPtr<FACEUIElement> Page = Manager->FindElementUnder(TEXT("WorldPanel_Field"), ActiveWorldTab);
	if (!Page.IsValid())
	{
		HideWorldOverlays();
		return;
	}

	if (ActiveWorldTab == TEXT("MapPage"))
	{
		for (UTextBlock* T : HouseTextRows) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		RefreshWorldMapPage(Page);
		return;
	}
	if (MapCoordinateLabel) { MapCoordinateLabel->SetVisibility(ESlateVisibility::Collapsed); }
	if (MapDateTimeLabel) { MapDateTimeLabel->SetVisibility(ESlateVisibility::Collapsed); }
	RefreshWorldHousePage(Page);
}

void UACEUIGameplayBinder::RefreshWorldMapPage(const TSharedPtr<FACEUIElement>& Page)
{
	const auto Map = Manager->FindElementUnder(TEXT("MapPage"), TEXT("Map"));
	if (!Map) return;
	// Retail keeps the map at its native size and centers its complete area.
	// The parent-size reflow also anchors the paper and labels.
	const FACEPosition Self = Client->GetPlayerPosition();
	// Player pin — retail hides it when the character is off the surface map.
	if (TSharedPtr<FACEUIElement> Pin = Manager->FindElementUnder(TEXT("MapPage"),
		TEXT("Map_PlayerPosition_Icon")))
	{
		if (IsOutdoorCell(Self))
		{
			const FVector2D G = GlobalMetersFrom(Self);
			Pin->bVisible = true;
			Pin->X = FMath::RoundToInt((6.f + G.X / DerethExtentMeters * 242.f) * Map->Width / 257.f) - Pin->Width / 2;
			Pin->Y = FMath::RoundToInt((8.f + (1.f - G.Y / DerethExtentMeters) * 251.f) * Map->Height / 267.f) - Pin->Height / 2;
		}
		else
		{
			Pin->bVisible = false;
		}
	}
	const FACEHouseInfo HouseInfo = Client->GetHouseInfo();
	if (TSharedPtr<FACEUIElement> Pin = Manager->FindElementUnder(TEXT("MapPage"),
		TEXT("Map_HousePosition_Icon")))
	{
		if (HouseInfo.bOwned && IsOutdoorCell(HouseInfo.Position))
		{
			const FVector2D G = GlobalMetersFrom(HouseInfo.Position);
			Pin->bVisible = true;
			Pin->X = FMath::RoundToInt((6.f + G.X / DerethExtentMeters * 242.f) * Map->Width / 257.f) - Pin->Width / 2;
			Pin->Y = FMath::RoundToInt((8.f + (1.f - G.Y / DerethExtentMeters) * 251.f) * Map->Height / 267.f) - Pin->Height / 2;
		}
		else
		{
			Pin->bVisible = false;
		}
	}

	if (!MapDateTimeLabel)
	{
		MapDateTimeLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		MapDateTimeLabel->SetJustification(ETextJustify::Center);
	}
	if (!MapCoordinateLabel)
	{
		MapCoordinateLabel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		MapCoordinateLabel->SetJustification(ETextJustify::Center);
	}
	const FString DateLine = FormatDerethDateTime(Client->GetGameTimeTicks());
	PlaceTextOnElement(MapDateTimeLabel,
		Manager->FindElementUnder(TEXT("MapPage"), TEXT("Map_DateTimeLabel")),
		DateLine.IsEmpty() ? TEXT("Dereth") : DateLine, 8, WorldInk, WorldZ + 2, true);
	const FString Coords = FormatMapCoords(Self);
	PlaceTextOnElement(MapCoordinateLabel,
		Manager->FindElementUnder(TEXT("MapPage"), TEXT("Map_CoordinateLabel")),
		Coords.IsEmpty() ? TEXT("Location unknown") : Coords, 9,
		Coords.IsEmpty() ? WorldDim : WorldInk, WorldZ + 2, true);
}

void UACEUIGameplayBinder::RefreshWorldHousePage(const TSharedPtr<FACEUIElement>& Page)
{
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(TEXT("HousePage"),
		TEXT("HouseTextListBox"));
	if (!ListEl.IsValid())
	{
		for (UTextBlock* T : HouseTextRows) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		return;
	}
	ListEl->X = 5;
	ListEl->Y = 5;
	ListEl->Width = FMath::Max(80, Page->Width - 10);
	ListEl->Height = FMath::Max(HouseRowHeight, Page->Height - 10);

	// Re-poll slowly while the tab stays open so rent payments show up as they land.
	const double Now = FPlatformTime::Seconds();
	if (Client && Now - LastHouseQueryAt > 10.0)
	{
		Client->SendHouseQuery();
		LastHouseQueryAt = Now;
	}

	const FACEHouseInfo Info = Client->GetHouseInfo();
	const FLinearColor HouseInk(0.92f,0.92f,0.88f);
	const FLinearColor HouseDim(0.65f,0.65f,0.60f);
	TArray<TPair<FString, FLinearColor>> Lines;
	FACEDatFont Font;
	Canvas->GetResourceResolver()->ResolveFont(0x40000002, Font);
	auto Add = [&](const FString& Text, const FLinearColor& Color)
	{
		const FLinearColor Ink = Color == WorldInk ? HouseInk : Color == WorldDim ? HouseDim : Color;
		if (Text.IsEmpty() || !Font.Id) { Lines.Emplace(Text, Ink); return; }
		for (const auto& Line : ACEDatText::Layout(Font, Text, ListEl->Width - 12, false))
			Lines.Emplace(Text.Mid(Line.Begin, Line.End - Line.Begin), Ink);
	};
	if (!Info.bQueried)
	{
		Add(TEXT("Querying dwelling status..."), WorldDim);
	}
	else if (!Info.bOwned)
	{
		Add(TEXT("You do not currently own a house."), WorldInk);
	}
	else
	{
        if (!Info.Buy.IsEmpty())
        {
            Add(TEXT("The purchase price for this dwelling is:"), WorldInk);
            TArray<FString> Payments;
            for (const FACEHousePayment& Pay : Info.Buy)
                Payments.Add(FString::Printf(TEXT("%d %s"), Pay.Required,
                    *(Pay.Required != 1 && !Pay.PluralName.IsEmpty() ? Pay.PluralName : Pay.Name)));
            Add(FString::Join(Payments, TEXT(", ")), WorldInk);
            Add(FString(), WorldInk);
        }
        if (!Info.Rent.IsEmpty())
        {
            Add(TEXT("Rent:"), WorldInk);
            TArray<FString> Payments;
            for (const FACEHousePayment& Pay : Info.Rent)
                Payments.Add(FString::Printf(TEXT("%d/%d %s"), Pay.Paid, Pay.Required,
                    *(Pay.Required != 1 && !Pay.PluralName.IsEmpty() ? Pay.PluralName : Pay.Name)));
            Add(FString::Join(Payments, TEXT(", ")), WorldInk);
            Add(FString(), WorldInk);
        }
        Add(FString::Printf(TEXT("Bought: %s"), *FormatUnixDate(Info.BuyTime)), WorldInk);
        const int64 Interval = Info.HouseType == 4 ? RentIntervalSeconds * 3 : RentIntervalSeconds;
        const bool bPaid = Info.bMaintenanceFree || !Info.Rent.ContainsByPredicate(
            [](const FACEHousePayment& Pay) { return Pay.Paid < Pay.Required; });
        Add(FString::Printf(TEXT("This maintenance period ends: %s"),
            *FormatUnixDate(Info.RentTime + Interval)), WorldInk);
        Add(FString::Printf(TEXT("Maintenance is next due: %s"),
            *FormatUnixDate(Info.RentTime + Interval * (bPaid ? 2 : 1))), WorldInk);
        const FString Coords = FormatMapCoords(Info.Position);
        if (Info.HouseType != 4 && !Coords.IsEmpty())
            Add(FString::Printf(TEXT("Location: %s"), *Coords), WorldInk);
        Add(FString(), WorldInk);
        Add(bPaid
            ? TEXT("The maintenance has already been paid for this period. You may not prepay next period's maintenance.")
            : TEXT("You have not paid your maintenance costs for this period."),
            bPaid ? FLinearColor(.45f,1.f,.45f) : FLinearColor(1.f,.35f,.25f));
	}

	const int32 VisibleRows = FMath::Max(1, ListEl->Height / HouseRowHeight);
	HouseScrollOffset = FMath::Clamp(HouseScrollOffset, 0, FMath::Max(0, Lines.Num() - VisibleRows));
	const FIntPoint Origin = ListEl->GetScreenOrigin();
	while (HouseTextRows.Num() < VisibleRows)
	{
		UTextBlock* L = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		L->SetJustification(ETextJustify::Left);
		L->SetVisibility(ESlateVisibility::Collapsed);
		HouseTextRows.Add(L);
	}
	for (int32 Row = 0; Row < HouseTextRows.Num(); ++Row)
	{
		UTextBlock* Label = HouseTextRows[Row];
		if (!Label)
		{
			continue;
		}
		const int32 Index = HouseScrollOffset + Row;
		if (Row >= VisibleRows || !Lines.IsValidIndex(Index) || Lines[Index].Key.IsEmpty())
		{
			Label->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		Label->SetText(FText::FromString(Lines[Index].Key));
		Label->SetColorAndOpacity(FSlateColor(Lines[Index].Value));
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8));
		CastChecked<UACERetailTextBlock>(Label)->SetRetailElement(Canvas->GetResourceResolver(), nullptr, FVector2D(1,1), ListEl->Width - 12, false);
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (Label->GetParent() != Canvas->GetElementLayer())
		{
			Canvas->GetElementLayer()->AddChild(Label);
		}
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Label->Slot))
		{
			Slot->SetAnchors(FAnchors(0.f, 0.f));
			Slot->SetPosition(FVector2D(static_cast<float>(Origin.X + 6),
				static_cast<float>(Origin.Y + Row * HouseRowHeight)));
			Slot->SetSize(FVector2D(static_cast<float>(ListEl->Width - 10), HouseRowHeight));
			Canvas->SetOverlayOrder(Label, Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")), WorldZ + 2 + Row);
		}
	}
}

bool UACEUIGameplayBinder::IsPointerOverHouseList(FVector2D CanvasLocalPos) const
{
	if (ActivePanelPage != TEXT("WorldPanel_Field") || ActiveWorldTab != TEXT("HousePage") || !Manager)
	{
		return false;
	}
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(TEXT("HousePage"),
		TEXT("HouseTextListBox"));
	if (!ListEl.IsValid() || !ListEl->bVisible)
	{
		return false;
	}
	const FIntPoint O = ListEl->GetScreenOrigin();
	return CanvasLocalPos.X >= O.X && CanvasLocalPos.X < O.X + ListEl->Width
		&& CanvasLocalPos.Y >= O.Y && CanvasLocalPos.Y < O.Y + ListEl->Height;
}

bool UACEUIGameplayBinder::ScrollHouseList(float WheelDelta)
{
	if (ActivePanelPage != TEXT("WorldPanel_Field") || ActiveWorldTab != TEXT("HousePage"))
	{
		return false;
	}
	HouseScrollOffset = FMath::Max(0, HouseScrollOffset + (WheelDelta > 0.f ? -3 : 3));
	RefreshWorldOverlays();
	return true;
}

bool UACEUIGameplayBinder::HandleWorldNamedClick(const FString& Name)
{
	if (Name == TEXT("MapTab") && ActivePanelPage == TEXT("WorldPanel_Field"))
	{
		SyncWorldPanelTab(TEXT("MapPage"));
		return true;
	}
	if (Name == TEXT("HouseTab") && ActivePanelPage == TEXT("WorldPanel_Field"))
	{
		SyncWorldPanelTab(TEXT("HousePage"));
		return true;
	}
	if (ActivePanelPage != TEXT("WorldPanel_Field"))
	{
		return false;
	}
	if (Name == TEXT("CloseWorldPanelButton"))
	{
		HidePanel();
		return true;
	}
	return false;
}

bool UACEUIGameplayBinder::GetMapTooltipAt(FVector2D Absolute, FString& OutText) const
{
    if (!Canvas || !Manager || ActivePanelPage != TEXT("WorldPanel_Field") || ActiveWorldTab != TEXT("MapPage")) return false;
    const auto Map = Manager->FindElementUnder(TEXT("MapPage"), TEXT("Map"));
    const FVector2D Local = Canvas->GetCachedGeometry().AbsoluteToLocal(Absolute);
    if (!Map || !Canvas->IsElementExposedAt(Map, Local) || Map->Width <= 0 || Map->Height <= 0) return false;
    const FVector2D P = Local / Canvas->GetLastScale2D() - FVector2D(Map->GetScreenOrigin());
    const FVector2D Native(P.X * 257.f / Map->Width, P.Y * 267.f / Map->Height);
    for (const auto& Town : ACERetailMap::Locations)
        if (Native.X >= Town.X && Native.X < Town.X + Town.Width && Native.Y >= Town.Y && Native.Y < Town.Y + Town.Height)
        { OutText = Town.Name; return true; }
    return false;
}
