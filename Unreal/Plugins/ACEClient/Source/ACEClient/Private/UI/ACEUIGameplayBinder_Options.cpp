#include "UI/ACERetailTextBlock.h"
#include "UI/ACERetailKeySelector.h"
#include "UI/ACEVideoSettingsWidget.h"
#include "ACEInputBindings.h"
#include "ACERetailChat.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Slider.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIElement.h"
#include "ACECharacterOptions.h"
#include "ACEClientSubsystem.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"

namespace
{
	static const FLinearColor OptGold(0.95f, 0.82f, 0.35f, 1.f);
	static const FLinearColor OptWhite(0.92f, 0.92f, 0.88f, 1.f);
	/** SmallRoundCheckBox off / on (same glyphs the spellbook filters use). */
	constexpr int32 DidOptCheckOff = 0x06004D15;
	constexpr int32 DidOptCheckOn = 0x06004D18;
	constexpr int32 OptionRowHeight = 16;
	constexpr int32 OptionCheckSize = 13;
	/** Height reserved at the bottom of a list page for Apply / Reset / Default. */
	constexpr int32 OptionsFooterHeight = 40;
	constexpr int32 OptionsTabHeight = 25;
	constexpr int32 ChatFilterOptionBase = 1000;
	constexpr int32 ChatOpacityOptionBase = 998;
	constexpr int32 ChatGroupCount = UE_ARRAY_COUNT(ACERetailChat::FilterGroups);
	struct FOptionsRow { int32 Option; FString Label; };
	TArray<FOptionsRow> OptionsRows(int32 Page)
	{
		TArray<FOptionsRow> Rows;
		TArray<const FACECharacterOptionDesc*> Options;
		ACECharacterOptions::GetPageOptions(static_cast<uint8>(Page), Options);
		if (Page == ACECharacterOptions::PageChat)
		{
			Rows.Add({INDEX_NONE, TEXT("General options")});
			Rows.Add({ChatOpacityOptionBase, TEXT("Inactive opacity")});
			Rows.Add({ChatOpacityOptionBase+1, TEXT("Active opacity")});
		}
		for (const auto* Option : Options) Rows.Add({Option->Option, Option->Label});
		if (Page == ACECharacterOptions::PageChat)
		{
			for (int32 Window = 0; Window <= 4; ++Window)
			{
				Rows.Add({INDEX_NONE, Window ? FString::Printf(TEXT("Chat Window %d"),Window) : FString(TEXT("Main chat window"))});
				for (int32 Group = Window ? 0 : 1; Group < ChatGroupCount; ++Group)
					Rows.Add({ChatFilterOptionBase + Window * ChatGroupCount + Group, ACERetailChat::FilterGroups[Group].Label});
			}
		}
		return Rows;
	}
}

int32 UACEUIGameplayBinder::GetOptionsRowCount() const
{
	return OptionsRows(ActiveOptionsPage()).Num();
}

int32 UACEUIGameplayBinder::ActiveOptionsPage() const
{
	if (ActiveOptionsTab == TEXT("CharacterSettingsPage")) { return ACECharacterOptions::PageCharacter; }
	if (ActiveOptionsTab == TEXT("ChatPage")) { return ACECharacterOptions::PageChat; }
	if (ActiveOptionsTab == TEXT("ConfigPage")) { return ACECharacterOptions::PageConfig; }
	return INDEX_NONE;
}

FString UACEUIGameplayBinder::ActiveOptionsListName() const
{
	if (ActiveOptionsTab == TEXT("CharacterSettingsPage")) { return TEXT("CharacterOptionsListBox"); }
	if (ActiveOptionsTab == TEXT("ChatPage")) { return TEXT("ChatOptionsListBox"); }
	if (ActiveOptionsTab == TEXT("ConfigPage")) { return TEXT("OptionsListBox"); }
	return FString();
}

void UACEUIGameplayBinder::SyncOptionsPanelTab(const FString& PageName)
{
	if (!Manager)
	{
		return;
	}
	ActiveOptionsTab = PageName;
	OptionsScrollOffset = 0;
	static const TCHAR* Pages[] = {
		TEXT("GameplayOptionsPage"), TEXT("CharacterSettingsPage"), TEXT("ChatPage"), TEXT("ConfigPage")
	};
	for (const TCHAR* Page : Pages)
	{
		if (TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(TEXT("OptionsPanel_Field"), Page))
		{
			El->bVisible = (PageName == Page);
		}
	}
	ApplyPanelTabChrome(TEXT("GameplayOptionsTab"), PageName == TEXT("GameplayOptionsPage"));
	ApplyPanelTabChrome(TEXT("CharacterSettingsTab"), PageName == TEXT("CharacterSettingsPage"));
	ApplyPanelTabChrome(TEXT("ChatTab"), PageName == TEXT("ChatPage"));
	ApplyPanelTabChrome(TEXT("ConfigTab"), PageName == TEXT("ConfigPage"));
	RefreshOptionsOverlays();
}

void UACEUIGameplayBinder::HideOptionsOverlays()
{
	for (USlider* Slider : ChatOpacitySliders) if (Slider) Slider->SetVisibility(ESlateVisibility::Collapsed);
	if (VideoSettings) VideoSettings->SetVisibility(ESlateVisibility::Collapsed);
	for (UTextBlock* T : OptionRowLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UBorder* B : OptionRowCheckboxes) { if (B) { B->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : OptionsTabLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : OptionsButtonLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	OptionRowOptions.Reset();
}

void UACEUIGameplayBinder::RefreshOptionsOverlays()
{
	for (USlider* Slider : ChatOpacitySliders) if (Slider) Slider->SetVisibility(ESlateVisibility::Collapsed);
	// Labels live on the flattened canvas, so changing DAT page visibility does
	// not hide them. Reset the shared pool before showing this page's buttons.
	for (UTextBlock* T : OptionsButtonLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	// Collapsing the active Config widget for even part of a refresh dismisses
	// Slate's dropdown popup and clears its capture. Keep it continuously visible.
	if (VideoSettings && ActiveOptionsTab != TEXT("ConfigPage")) VideoSettings->SetVisibility(ESlateVisibility::Collapsed);
	if (ActivePanelPage != TEXT("OptionsPanel_Field") || !Manager || !Canvas || !Canvas->WidgetTree
		|| !Client)
	{
		HideOptionsOverlays();
		return;
	}
	TSharedPtr<FACEUIElement> Panel = Manager->FindElementByName(TEXT("OptionsPanel_Field"));
	if (!Panel.IsValid() || !Panel->bVisible)
	{
		HideOptionsOverlays();
		return;
	}

	// Grow the active page (authored 298x575) into the live floaty and keep the close
	// button pinned to the tab strip's right edge.
	const int32 PanelW = FMath::Max(120, Panel->Width);
	const int32 PanelH = FMath::Max(120, Panel->Height);
	if (TSharedPtr<FACEUIElement> Close = Manager->FindElementUnder(TEXT("OptionsPanel_Field"),
		TEXT("CloseOptionsPanelButton")))
	{
		Close->X = FMath::Max(0, PanelW - Close->Width);
	}
	if (TSharedPtr<FACEUIElement> Edge = Manager->FindElementUnder(TEXT("OptionsPanel_Field"), TEXT("left_edge")))
	{
		Edge->Height = FMath::Max(0, PanelH - OptionsTabHeight);
	}
	TSharedPtr<FACEUIElement> Page = Manager->FindElementUnder(TEXT("OptionsPanel_Field"), ActiveOptionsTab);
	if (!Page.IsValid())
	{
		HideOptionsOverlays();
		return;
	}
	Page->Y = OptionsTabHeight;
	Page->Width = FMath::Max(64, PanelW - Page->X);
	Page->Height = FMath::Max(64, PanelH - OptionsTabHeight);

	// Tab labels.
	static const TPair<const TCHAR*, const TCHAR*> Tabs[] = {
		{ TEXT("GameplayOptionsTab"), TEXT("Game Play") },
		{ TEXT("CharacterSettingsTab"), TEXT("Character") },
		{ TEXT("ChatTab"), TEXT("Chat") },
		{ TEXT("ConfigTab"), TEXT("Config") },
	};
	static const TCHAR* TabPages[] = {
		TEXT("GameplayOptionsPage"), TEXT("CharacterSettingsPage"), TEXT("ChatPage"), TEXT("ConfigPage")
	};
	while (OptionsTabLabels.Num() < UE_ARRAY_COUNT(Tabs))
	{
		UTextBlock* L = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		L->SetJustification(ETextJustify::Center);
		OptionsTabLabels.Add(L);
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(Tabs); ++i)
	{
		UTextBlock* Label = OptionsTabLabels[i];
		if (!Label)
		{
			continue;
		}
		const bool bActive = ActiveOptionsTab == TabPages[i];
		TSharedPtr<FACEUIElement> TabEl = Manager->FindElementUnder(
			TEXT("OptionsPanel_Field"), Tabs[i].Key);
		if (!TabEl.IsValid())
		{
			Label->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		Label->SetText(FText::FromString(Tabs[i].Value));
		Label->SetColorAndOpacity(FSlateColor(bActive ? OptGold : OptWhite));
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(bActive ? TEXT("Bold") : TEXT("Regular"), 8));
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		// DAT paint climbs past 20k — keep captions above tab chrome (SkillManagement pattern).
		Canvas->PlaceWidgetAtElement(Label, TabEl, 100000, FMargin(8.f, 4.f, 6.f, 4.f));
	}

	if (ActiveOptionsTab == TEXT("GameplayOptionsPage"))
	{
		for (UTextBlock* T : OptionRowLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		for (UBorder* B : OptionRowCheckboxes) { if (B) { B->SetVisibility(ESlateVisibility::Collapsed); } }
		OptionRowOptions.Reset();

		static const TPair<const TCHAR*, const TCHAR*> Buttons[] = {
			{ TEXT("GameplayOptions_LeaveWorld_Button"), TEXT("Leave World") },
			{ TEXT("GameplayOptions_Quit_Button"), TEXT("Quit") },
			{ TEXT("GameplayOptions_Keyboard_Button"), TEXT("Keyboard Layout") },
			{ TEXT("GameplayOptions_MouseTurning_Button"), TEXT("Mouse Turning") },
			{ TEXT("GameplayOptions_Help_Button"), TEXT("Help") },
			{ TEXT("GameplayOptions_UrgentAssistance_Button"), TEXT("Urgent Assistance") },
			{ TEXT("GameplayOptions_ReportAbuse_Button"), TEXT("Report Abuse") },
		};
		while (OptionsButtonLabels.Num() < UE_ARRAY_COUNT(Buttons))
		{
			UTextBlock* L = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
			L->SetJustification(ETextJustify::Center);
			OptionsButtonLabels.Add(L);
		}
		for (int32 i = 0; i < UE_ARRAY_COUNT(Buttons); ++i)
		{
			UTextBlock* Label = OptionsButtonLabels[i];
			if (!Label)
			{
				continue;
			}
			FString Text = Buttons[i].Value;
			if (FCString::Strcmp(Buttons[i].Key, TEXT("GameplayOptions_MouseTurning_Button")) == 0)
			{
				Text += Client->IsCharacterOptionSet(0x31) ? TEXT(": On") : TEXT(": Off");
			}
			PlaceTextOnElement(Label, Manager->FindElementUnder(ActiveOptionsTab, Buttons[i].Key),
				Text, 8, OptWhite, 100010, true);
		}
		SyncOptionsScrollbar();
		return;
	}

	// Option list pages: stretch the list + scrollbar, park the footer buttons at the bottom.
	const FString ListName = ActiveOptionsListName();
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(ActiveOptionsTab, ListName);
	if (!ListEl.IsValid())
	{
		HideOptionsOverlays();
		return;
	}
	const bool bConfig = ActiveOptionsTab == TEXT("ConfigPage");
	const int32 VideoHeight = 0;
	if (bConfig)
	{
		if (!VideoSettings)
		{
			auto* Video = NewObject<UACEVideoSettingsWidget>(Canvas);
			Video->Initialize();
			VideoSettings = Video;
		}
		if (VideoSettings)
		{
			VideoSettings->SetVisibility(ESlateVisibility::Visible);
			Canvas->PlaceWidgetAtElement(VideoSettings, Page, 100040, FMargin(4, 0, 20, OptionsFooterHeight));
		}
	}
	const int32 ListH = FMath::Max(OptionRowHeight, Page->Height - OptionsFooterHeight - VideoHeight);
	const int32 ListW = FMath::Max(32, Page->Width - 20);
	ListEl->X = 0;
	ListEl->Y = VideoHeight;
	ListEl->Width = ListW;
	ListEl->Height = ListH;
	const TCHAR* BarName = (ActiveOptionsTab == TEXT("CharacterSettingsPage"))
		? TEXT("CharacterOptionsListBoxScrollbar") : TEXT("OptionsListBoxScrollbar");
	if (TSharedPtr<FACEUIElement> Bar = Manager->FindElementUnder(ActiveOptionsTab, BarName))
	{
		Bar->X = ListW;
		Bar->Y = VideoHeight;
		Bar->Width = 16;
		Bar->Height = ListH;
	}
	static const TPair<const TCHAR*, const TCHAR*> Footer[] = {
		{ TEXT("ApplyButton"), TEXT("Apply") },
		{ TEXT("ResetButton"), TEXT("Reset") },
		{ TEXT("DefaultButton"), TEXT("Defaults") },
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Footer); ++i)
	{
		if (TSharedPtr<FACEUIElement> Btn = Manager->FindElementUnder(ActiveOptionsTab, Footer[i].Key))
		{
			Btn->Y = ListH + VideoHeight + 4;
			Btn->X = 16 + i * 90;
		}
	}
	while (OptionsButtonLabels.Num() < 10)
	{
		UTextBlock* L = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		L->SetJustification(ETextJustify::Center);
		OptionsButtonLabels.Add(L);
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(Footer); ++i)
	{
		// Footer labels live at the tail of the shared label pool (7 gameplay buttons first).
		UTextBlock* Label = OptionsButtonLabels.IsValidIndex(7 + i) ? OptionsButtonLabels[7 + i] : nullptr;
		if (!Label)
		{
			continue;
		}
		TSharedPtr<FACEUIElement> Btn = Manager->FindElementUnder(ActiveOptionsTab, Footer[i].Key);
		if (!Btn.IsValid())
		{
			Label->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		// Keep the full button box: the DAT font is 18 pixels high and the
		// authored text layout supplies its margins and vertical centering.
		PlaceTextOnElement(Label, Btn, Footer[i].Value, 9, OptWhite, 100011, true);
	}

	if (bConfig)
	{
		for (UTextBlock* L : OptionRowLabels) if (L) L->SetVisibility(ESlateVisibility::Collapsed);
		for (UBorder* B : OptionRowCheckboxes) if (B) B->SetVisibility(ESlateVisibility::Collapsed);
		OptionRowOptions.Reset();
		SyncOptionsScrollbar();
		return;
	}
	const auto PageOptions = OptionsRows(ActiveOptionsPage());
	const int32 VisibleRows = FMath::Max(1, ListH / OptionRowHeight);
	const int32 MaxOffset = FMath::Max(0, PageOptions.Num() - VisibleRows);
	OptionsScrollOffset = FMath::Clamp(OptionsScrollOffset, 0, MaxOffset);

	const FIntPoint ListOrigin = ListEl->GetScreenOrigin();
	OptionRowOptions.SetNum(VisibleRows);
	while (OptionRowLabels.Num() < VisibleRows)
	{
		UTextBlock* L = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		L->SetJustification(ETextJustify::Left);
		L->SetVisibility(ESlateVisibility::Collapsed);
		OptionRowLabels.Add(L);
	}
	for (int32 Row = 0; Row < OptionRowLabels.Num(); ++Row)
	{
		UTextBlock* Label = OptionRowLabels[Row];
		UBorder* Check = EnsureIconBorder(OptionRowCheckboxes, Row);
		const int32 Index = OptionsScrollOffset + Row;
		if (Row >= VisibleRows || !PageOptions.IsValidIndex(Index))
		{
			if (Label) { Label->SetVisibility(ESlateVisibility::Collapsed); }
			if (Check) { Check->SetVisibility(ESlateVisibility::Collapsed); }
			if (OptionRowOptions.IsValidIndex(Row)) { OptionRowOptions[Row] = INDEX_NONE; }
			continue;
		}
		const auto& Desc = PageOptions[Index];
		OptionRowOptions[Row] = Desc.Option;
		const bool bHeader = Desc.Option == INDEX_NONE;
		const bool bOpacity = Desc.Option >= ChatOpacityOptionBase && Desc.Option < ChatFilterOptionBase;
		bool bChecked = false;
		if (Desc.Option >= ChatFilterOptionBase)
		{
			const int32 Filter = Desc.Option - ChatFilterOptionBase;
			const int32 Window = Filter / ChatGroupCount;
			const uint64 Mask = Window == 0 ? MainChatTypeFilter : FloatyChatFilters[Window-1];
			const uint64 Group = ACERetailChat::FilterGroups[Filter % ChatGroupCount].Mask;
			bChecked = (Mask & Group) == Group;
		}
		else if (!bHeader && !bOpacity) bChecked = Client->IsCharacterOptionSet(Desc.Option);
		const float RowY = static_cast<float>(ListOrigin.Y + Row * OptionRowHeight);
		if (bOpacity)
		{
			if (ChatOpacitySliders.IsEmpty())
			{
				auto Brush=[&](uint32 Id,FVector2D Size) {
					FSlateBrush B; B.DrawAs=ESlateBrushDrawType::Image; B.ImageSize=Size;
					B.SetResourceObject(Canvas->GetResourceResolver()->ResolveIconTexture(Id)); return B;
				};
				FSliderStyle Style;
				Style.SetNormalBarImage(Brush(0x06001285,FVector2D(120,12))).SetHoveredBarImage(Style.NormalBarImage).SetDisabledBarImage(Style.NormalBarImage)
					.SetNormalThumbImage(Brush(0x06001286,FVector2D(7,12))).SetHoveredThumbImage(Style.NormalThumbImage).SetDisabledThumbImage(Style.NormalThumbImage).SetBarThickness(4);
				for (int32 I=0;I<2;++I)
				{
					auto* Slider=Canvas->WidgetTree->ConstructWidget<USlider>();
					Slider->SetWidgetStyle(Style); Slider->SetMinValue(0); Slider->SetMaxValue(1); Slider->SetStepSize(.01f);
					Canvas->GetElementLayer()->AddChild(Slider); ChatOpacitySliders.Add(Slider);
				}
				ChatOpacitySliders[0]->OnValueChanged.AddDynamic(this,&UACEUIGameplayBinder::ChangeInactiveChatOpacity);
				ChatOpacitySliders[1]->OnValueChanged.AddDynamic(this,&UACEUIGameplayBinder::ChangeActiveChatOpacity);
			}
			auto* Slider=ChatOpacitySliders[Desc.Option-ChatOpacityOptionBase].Get();
			Slider->SetValue(Desc.Option==ChatOpacityOptionBase ? ChatInactiveOpacity : ChatActiveOpacity);
			Slider->SetVisibility(ESlateVisibility::Visible);
			if (auto* Slot=Cast<UCanvasPanelSlot>(Slider->Slot))
			{
				Slot->SetPosition(FVector2D(ListOrigin.X+ListW-120,RowY)); Slot->SetSize(FVector2D(116,16));
				Canvas->SetOverlayOrder(Slider,Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")),100080+Row);
			}
		}
		if (Check)
		{
			SetIconDid(Check, bChecked ? DidOptCheckOn : DidOptCheckOff);
			Check->SetVisibility(bHeader || bOpacity ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
			if (Check->GetParent() != Canvas->GetElementLayer())
			{
				Canvas->GetElementLayer()->AddChild(Check);
			}
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Check->Slot))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetPosition(FVector2D(static_cast<float>(ListOrigin.X + 3),
					RowY + (OptionRowHeight - OptionCheckSize) * 0.5f));
				Slot->SetSize(FVector2D(OptionCheckSize, OptionCheckSize));
				Canvas->SetOverlayOrder(Check, Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")), 100020 + Row);
			}
		}
		if (Label)
		{
			Label->SetText(FText::FromString(Desc.Label));
			Label->SetColorAndOpacity(FSlateColor(bHeader ? OptGold : OptWhite));
			Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8));
			Label->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (Label->GetParent() != Canvas->GetElementLayer())
			{
				Canvas->GetElementLayer()->AddChild(Label);
			}
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Label->Slot))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetPosition(FVector2D(static_cast<float>(ListOrigin.X + (bHeader || bOpacity ? 3 : 20)), RowY + 1.f));
				Slot->SetSize(FVector2D(static_cast<float>(ListW - (bOpacity ? 125 : 24)), OptionRowHeight));
				Canvas->SetOverlayOrder(Label, Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")), 100020 + Row);
			}
		}
	}
	SyncOptionsScrollbar();
}

void UACEUIGameplayBinder::SyncOptionsScrollbar()
{
	if (!Manager || ActivePanelPage != TEXT("OptionsPanel_Field"))
	{
		return;
	}
	const int32 Page = ActiveOptionsPage();
	if (Page == INDEX_NONE)
	{
		return;
	}
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(ActiveOptionsTab, ActiveOptionsListName());
	const TCHAR* BarName = (ActiveOptionsTab == TEXT("CharacterSettingsPage"))
		? TEXT("CharacterOptionsListBoxScrollbar") : TEXT("OptionsListBoxScrollbar");
	TSharedPtr<FACEUIElement> Bar = Manager->FindElementUnder(ActiveOptionsTab, BarName);
	if (!ListEl.IsValid() || !Bar.IsValid())
	{
		return;
	}
	if (Page == ACECharacterOptions::PageConfig)
	{
		if (auto* Video = Cast<UACEVideoSettingsWidget>(VideoSettings))
		{
			const float End = Video->GetScrollEnd();
			SyncDatScrollbar(Bar, End > 0 ? Video->GetScrollOffset()/End : 0,
				ListEl->Height / FMath::Max(1.f, End + ListEl->Height));
		}
		return;
	}
	const auto PageOptions = OptionsRows(Page);
	const int32 VisibleRows = FMath::Max(1, ListEl->Height / OptionRowHeight);
	const int32 MaxOffset = FMath::Max(0, PageOptions.Num() - VisibleRows);
	const float Frac = MaxOffset > 0
		? static_cast<float>(OptionsScrollOffset) / static_cast<float>(MaxOffset) : 0.f;
	const float VisibleFrac = PageOptions.Num() > 0
		? FMath::Clamp(static_cast<float>(VisibleRows) / static_cast<float>(PageOptions.Num()), 0.05f, 1.f)
		: 1.f;
	SyncDatScrollbar(Bar, Frac, VisibleFrac);
}

void UACEUIGameplayBinder::ToggleCharacterOption(int32 Option)
{
	if (Option >= ChatOpacityOptionBase && Option < ChatFilterOptionBase) return;
	if (Option >= ChatFilterOptionBase && Option < ChatFilterOptionBase + 5 * ChatGroupCount)
	{
		const int32 Filter = Option - ChatFilterOptionBase;
		const int32 Window = Filter / ChatGroupCount;
		uint64& Mask = Window == 0 ? MainChatTypeFilter : FloatyChatFilters[Window-1];
		const uint64 Group = ACERetailChat::FilterGroups[Filter % ChatGroupCount].Mask;
		Mask = (Mask & Group) == Group ? Mask & ~Group : Mask | Group;
		SaveFloatyChatSettings();
		RefreshOptionsOverlays();
		return;
	}
	if (!Client)
	{
		return;
	}
	const bool bNew = !Client->IsCharacterOptionSet(Option);
	Client->SendSetSingleCharacterOption(Option, bNew);
	RefreshOptionsOverlays();
}

bool UACEUIGameplayBinder::TryHandleOptionsListClick(FVector2D CanvasLocalPos)
{
	if (ActiveOptionsTab == TEXT("ConfigPage")) return false;
	if (ActivePanelPage != TEXT("OptionsPanel_Field") || !Manager
		|| ActiveOptionsPage() == INDEX_NONE)
	{
		return false;
	}
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(ActiveOptionsTab, ActiveOptionsListName());
	if (!ListEl.IsValid() || !ListEl->bVisible)
	{
		return false;
	}
	const FIntPoint O = ListEl->GetScreenOrigin();
	if (CanvasLocalPos.X < O.X || CanvasLocalPos.X >= O.X + ListEl->Width
		|| CanvasLocalPos.Y < O.Y || CanvasLocalPos.Y >= O.Y + ListEl->Height)
	{
		return false;
	}
	const int32 Row = static_cast<int32>((CanvasLocalPos.Y - O.Y) / OptionRowHeight);
	if (!OptionRowOptions.IsValidIndex(Row) || OptionRowOptions[Row] == INDEX_NONE)
	{
		return true;
	}
	ToggleCharacterOption(OptionRowOptions[Row]);
	return true;
}

bool UACEUIGameplayBinder::IsPointerOverOptionsList(FVector2D CanvasLocalPos) const
{
	if (ActivePanelPage != TEXT("OptionsPanel_Field") || !Manager
		|| ActiveOptionsTab == TEXT("GameplayOptionsPage"))
	{
		return false;
	}
	FString ListName;
	if (ActiveOptionsTab == TEXT("CharacterSettingsPage")) { ListName = TEXT("CharacterOptionsListBox"); }
	else if (ActiveOptionsTab == TEXT("ChatPage")) { ListName = TEXT("ChatOptionsListBox"); }
	else if (ActiveOptionsTab == TEXT("ConfigPage")) { ListName = TEXT("OptionsListBox"); }
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(ActiveOptionsTab, ListName);
	if (!ListEl.IsValid() || !ListEl->bVisible)
	{
		return false;
	}
	const FIntPoint O = ListEl->GetScreenOrigin();
	return CanvasLocalPos.X >= O.X && CanvasLocalPos.X < O.X + ListEl->Width
		&& CanvasLocalPos.Y >= O.Y && CanvasLocalPos.Y < O.Y + ListEl->Height;
}

bool UACEUIGameplayBinder::ScrollOptionsList(float WheelDelta)
{
	if (ActivePanelPage != TEXT("OptionsPanel_Field") || ActiveOptionsPage() == INDEX_NONE)
	{
		return false;
	}
	if (ActiveOptionsTab == TEXT("ConfigPage"))
	{
		if (auto* Video = Cast<UACEVideoSettingsWidget>(VideoSettings)) Video->SetScrollOffset(Video->GetScrollOffset() - WheelDelta*48.f);
		SyncOptionsScrollbar();
		return true;
	}
	const int32 Dir = WheelDelta > 0.f ? -1 : 1;
	OptionsScrollOffset = FMath::Max(0, OptionsScrollOffset + Dir * 3);
	RefreshOptionsOverlays();
	return true;
}

bool UACEUIGameplayBinder::HandleOptionsNamedClick(const FString& Name)
{
	if (HandleKeyboardNamedClick(Name)) return true;
	if (Name == TEXT("GameplayOptionsTab"))
	{
		ShowPanelPage(TEXT("OptionsPanel_Field"));
		SyncOptionsPanelTab(TEXT("GameplayOptionsPage"));
		return true;
	}
	if (Name == TEXT("CharacterSettingsTab"))
	{
		ShowPanelPage(TEXT("OptionsPanel_Field"));
		SyncOptionsPanelTab(TEXT("CharacterSettingsPage"));
		return true;
	}
	if (Name == TEXT("ConfigTab"))
	{
		ShowPanelPage(TEXT("OptionsPanel_Field"));
		SyncOptionsPanelTab(TEXT("ConfigPage"));
		return true;
	}
	// "ChatTab" also exists in the chat floaty; only claim it while options are up.
	if (Name == TEXT("ChatTab") && ActivePanelPage == TEXT("OptionsPanel_Field"))
	{
		SyncOptionsPanelTab(TEXT("ChatPage"));
		return true;
	}
	if (ActivePanelPage != TEXT("OptionsPanel_Field"))
	{
		return false;
	}
	if (Name == TEXT("CloseOptionsPanelButton"))
	{
		HidePanel();
		return true;
	}
	if (Name == TEXT("GameplayOptions_LeaveWorld_Button"))
	{
		PostInventorySystemMessage(TEXT("Returning to character selection..."));
		if (Client)
		{
			Client->Logout();
		}
		return true;
	}
	if (Name == TEXT("GameplayOptions_Quit_Button"))
	{
		if (Client)
		{
			Client->Logout();
		}
		if (UWorld* World = GetWorld())
		{
			UKismetSystemLibrary::QuitGame(World, nullptr, EQuitPreference::Quit, false);
		}
		return true;
	}
	if (Name == TEXT("GameplayOptions_Keyboard_Button"))
	{
		ToggleKeyboardMappingUI();
		return true;
	}
	if (Name == TEXT("KeyboardOKButton") || Name == TEXT("KeyboardCancelButton"))
	{
		if (Manager)
		{
			Manager->SetElementVisibleByName(TEXT("RootGameplay_Keyboard_Field"), false);
			Manager->SetElementVisibleByName(TEXT("RootKeyboard_Field"), false);
			if (TSharedPtr<FACEUIElement> El = Manager->FindElementByName(TEXT("RootGameplay_Keyboard_Field")))
			{
				El->bVisible = false;
			}
			if (TSharedPtr<FACEUIElement> El = Manager->FindElementByName(TEXT("RootKeyboard_Field")))
			{
				El->bVisible = false;
			}
		}
		return true;
	}
	if (Name == TEXT("GameplayOptions_MouseTurning_Button"))
	{
		// Retail PlayerOption UseMouseTurning (0x31).
		ToggleCharacterOption(0x31);
		if (Client)
		{
			PostInventorySystemMessage(Client->IsCharacterOptionSet(0x31)
				? TEXT("Mouse turning enabled.") : TEXT("Mouse turning disabled."));
		}
		return true;
	}
	if (Name == TEXT("GameplayOptions_Help_Button"))
	{
		PostInventorySystemMessage(TEXT("Type @help for client commands, @house_help for housing, ")
			TEXT("@acehelp for server commands. Left-click to select, double-click to use, ")
			TEXT("drag items to move them, right-click an item to examine it."));
		return true;
	}
	if (Name == TEXT("GameplayOptions_UrgentAssistance_Button"))
	{
		ShowPanelPage(TEXT("UrgentAssistancePanel_Field"));
		return true;
	}
	if (Name == TEXT("GameplayOptions_ReportAbuse_Button"))
	{
		ShowPanelPage(TEXT("AbusePanel_Field"));
		return true;
	}
	if (Name == TEXT("ApplyButton"))
	{
		if (ActiveOptionsTab == TEXT("ConfigPage"))
			if (auto* Video = Cast<UACEVideoSettingsWidget>(VideoSettings)) Video->ApplyVideo();
		if (Client)
		{
			// Retail pushes both bitfields on Apply; per-checkbox updates already went out.
			Client->SendCharacterOptions(Client->GetCharacterOptions1(), Client->GetCharacterOptions2());
			OptionsSnapshot1 = Client->GetCharacterOptions1();
			OptionsSnapshot2 = Client->GetCharacterOptions2();
			MainChatFilterSnapshot = MainChatTypeFilter;
			FloatyChatFilterSnapshot = FloatyChatFilters;
			ChatOpacitySnapshot = FVector2D(ChatInactiveOpacity,ChatActiveOpacity);
			PostInventorySystemMessage(TEXT("Options applied."));
		}
		return true;
	}
	if (Name == TEXT("ResetButton"))
	{
		if (ActiveOptionsTab == TEXT("ChatPage"))
		{
			MainChatTypeFilter = MainChatFilterSnapshot;
			ChatInactiveOpacity = ChatOpacitySnapshot.X; ChatActiveOpacity = ChatOpacitySnapshot.Y;
			if (FloatyChatFilterSnapshot.Num() == NumFloatyChats) FloatyChatFilters = FloatyChatFilterSnapshot;
			SaveFloatyChatSettings();
		}
		if (ActiveOptionsTab == TEXT("ConfigPage"))
			if (auto* Video = Cast<UACEVideoSettingsWidget>(VideoSettings)) Video->ResetVideo();
		if (Client)
		{
			Client->SendCharacterOptions(OptionsSnapshot1, OptionsSnapshot2);
			PostInventorySystemMessage(TEXT("Options reset."));
		}
		RefreshOptionsOverlays();
		return true;
	}
	if (Name == TEXT("DefaultButton"))
	{
		if (ActiveOptionsTab == TEXT("ChatPage"))
		{
			MainChatTypeFilter = ACERetailChat::MainWindowDefault;
			ChatInactiveOpacity = .5f; ChatActiveOpacity = 1.f;
			for (int32 W=0; W<NumFloatyChats; ++W) FloatyChatFilters[W] = ACERetailChat::FloatyDefaults[W];
			SaveFloatyChatSettings();
		}
		if (ActiveOptionsTab == TEXT("ConfigPage"))
			if (auto* Video = Cast<UACEVideoSettingsWidget>(VideoSettings)) Video->DefaultsVideo();
		if (Client)
		{
			Client->SendCharacterOptions(ACECharacterOptions::Options1Default,
				ACECharacterOptions::Options2Default | ACECharacterOptions::MouseTurningFlag);
			PostInventorySystemMessage(TEXT("Options restored to defaults."));
		}
		RefreshOptionsOverlays();
		return true;
	}
	if (Name == TEXT("ScrollBar_Up") || Name == TEXT("ScrollBar_Down"))
	{
		if (ActiveOptionsPage() != INDEX_NONE)
		{
			const int32 Dir = (Name == TEXT("ScrollBar_Up")) ? -1 : 1;
			if (ActiveOptionsTab == TEXT("ConfigPage")) return ScrollOptionsList(-Dir / 3.f);
			OptionsScrollOffset = FMath::Max(0, OptionsScrollOffset + Dir);
			RefreshOptionsOverlays();
			return true;
		}
	}
	return false;
}
