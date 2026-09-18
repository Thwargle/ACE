#include "UI/ACERetailTextEntry.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	static const FLinearColor CompGold(0.95f, 0.82f, 0.35f, 1.f);
	static const FLinearColor CompWhite(0.92f, 0.92f, 0.88f, 1.f);
	static const FLinearColor CompDim(0.60f, 0.60f, 0.56f, 1.f);
	/** ComponentEntryTemplate in classic_spellcomponent (0x21000033) is 284x32 with a 32px icon. */
	constexpr int32 ComponentRowHeight = 32;
	constexpr int32 ComponentIconSize = 32;
	constexpr int32 ComponentZ = 730;

	/** SpellComponentsTable.Type → retail section heading. */
	const TCHAR* ComponentTypeName(uint32 Type)
	{
		switch (Type)
		{
		case 1: return TEXT("Scarabs");
		case 2: return TEXT("Herbs");
		case 3: return TEXT("Powders");
		case 4: return TEXT("Potions");
		case 5: return TEXT("Talismans");
		case 6: return TEXT("Tapers");
		case 7: return TEXT("Peas");
		default: return TEXT("Other Components");
		}
	}
}

void UACEUIGameplayBinder::BuildComponentRowList(TArray<FACEComponentRow>& OutRows) const
{
	OutRows.Reset();
	UACEDatSubsystem* Dat = nullptr;
	if (PlayerController)
	{
		if (UGameInstance* GI = PlayerController->GetGameInstance())
		{
			Dat = GI->GetSubsystem<UACEDatSubsystem>();
		}
	}
	if (!Dat || !Client)
	{
		return;
	}
	// Carried counts per weenie class, summed across the main pack and every side pack.
	TMap<int32, int32> Carried;
	auto Tally = [&Carried](const TArray<FACEWorldObject>& Items)
	{
		for (const FACEWorldObject& Item : Items)
		{
			if (Item.WeenieClassId == 0)
			{
				continue;
			}
			Carried.FindOrAdd(Item.WeenieClassId) += FMath::Max(1, Item.StackSize);
		}
	};
	Tally(Client->GetPackItems(Client->GetPlayerGuid()));
	for (const FACEWorldObject& Pack : Client->GetPlayerPacks())
	{
		Tally(Client->GetPackItems(Pack.Guid));
	}
	const TMap<int32, int32> Desired = Client->GetDesiredComponents();

	uint32 LastType = MAX_uint32;
	for (const FACESpellComponentInfo& Info : Dat->GetSpellComponents())
	{
		const int32 Wcid = static_cast<int32>(Info.Wcid);
		const int32 Count = Carried.FindRef(Wcid);
		const int32 Want = Desired.FindRef(Wcid);
		if (bComponentsShowCarriedOnly && Count == 0 && Want == 0)
		{
			continue;
		}
		if (Info.Type != LastType)
		{
			LastType = Info.Type;
			FACEComponentRow Header;
			Header.bHeader = true;
			Header.Name = ComponentTypeName(Info.Type);
			OutRows.Add(MoveTemp(Header));
		}
		FACEComponentRow Row;
		Row.Wcid = Wcid;
		Row.Name = Info.Name;
		Row.IconDid = static_cast<int32>(Info.IconDid);
		Row.Carried = Count;
		Row.Desired = Want;
		OutRows.Add(MoveTemp(Row));
	}
}

void UACEUIGameplayBinder::HideComponentOverlays()
{
	for (UTextBlock* T : ComponentNameLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : ComponentCountLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UACERetailTextEntry* T : ComponentDesiredEntries) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UBorder* B : ComponentIcons) { if (B) { B->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UBorder* B : ComponentBackgrounds) if (B) B->SetVisibility(ESlateVisibility::Collapsed);
	for (UBorder* B : ComponentEntryBackgrounds) if (B) B->SetVisibility(ESlateVisibility::Collapsed);
	ComponentRowWcids.Reset();
}

void UACEUIGameplayBinder::RefreshComponentOverlays()
{
	if (!Client || !Manager || !Canvas || !Canvas->WidgetTree
		|| ActivePanelPage != TEXT("SpellManagementPanel_Field")
		|| ActiveSpellPanelTab != TEXT("SpellComponentPage"))
	{
		HideComponentOverlays();
		return;
	}
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(
		TEXT("SpellComponentPage"), TEXT("SpellComponents_ComponentList"));
	if (!ListEl.IsValid() || !ListEl->bVisible)
	{
		HideComponentOverlays();
		return;
	}
	// Grow the authored 284x600 list into whatever the floaty was resized to.
	if (TSharedPtr<FACEUIElement> Page = Manager->FindElementUnder(
		TEXT("SpellManagementPanel_Field"), TEXT("SpellComponentPage")))
	{
		if (const auto Panel = Manager->FindElementByName(TEXT("SpellManagementPanel_Field")))
		{
			Page->Y = 25;
			Page->Width = Panel->Width;
			Page->Height = FMath::Max(ComponentRowHeight, Panel->Height - Page->Y);
		}
		const int32 ListW = FMath::Max(120, Page->Width - 16);
		ListEl->X = 0;
		ListEl->Y = 0;
		ListEl->Width = ListW;
		ListEl->Height = FMath::Max(ComponentRowHeight, Page->Height);
		if (TSharedPtr<FACEUIElement> Bar = Manager->FindElementUnder(
			TEXT("SpellComponentPage"), TEXT("SpellComponents_ComponentList_Scrollbar")))
		{
			Bar->X = ListW;
			Bar->Y = 0;
			Bar->Width = 16;
			Bar->Height = ListEl->Height;
		}
	}

	TArray<FACEComponentRow> Rows;
	BuildComponentRowList(Rows);
	const int32 VisibleRows = FMath::Max(1, ListEl->Height / ComponentRowHeight);
	const int32 MaxOffset = FMath::Max(0, Rows.Num() - VisibleRows);
	ComponentScrollOffset = FMath::Clamp(ComponentScrollOffset, 0, MaxOffset);

	const FIntPoint Origin = ListEl->GetScreenOrigin();
	const int32 ListWidth = FMath::Max(64, ListEl->Width);
	ComponentRowWcids.SetNum(VisibleRows);
	auto MakeLabel = [this](TArray<TObjectPtr<UTextBlock>>& Pool, int32 Index,
		ETextJustify::Type Justify) -> UTextBlock*
	{
		while (Pool.Num() <= Index)
		{
			UTextBlock* L = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
			L->SetVisibility(ESlateVisibility::Collapsed);
			L->SetJustification(Justify);
			Pool.Add(L);
		}
		return Pool[Index];
	};
	auto Place = [this](UWidget* W, float X, float Y, float SizeX, float SizeY, int32 Z)
	{
		if (W->GetParent() != Canvas->GetElementLayer())
		{
			Canvas->GetElementLayer()->AddChild(W);
		}
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(W->Slot))
		{
			Slot->SetAnchors(FAnchors(0.f, 0.f));
			Slot->SetPosition(FVector2D(X, Y));
			Slot->SetSize(FVector2D(SizeX, SizeY));
			Canvas->SetOverlayOrder(W, Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")), Z);
		}
	};

	// Use the authored component templates, including the separate numeric edit box.
	static const auto HeaderTemplate = UACEUILayoutResolver::LoadTemplate(0x21000033, 0x10000466);
	static const auto ItemTemplate = UACEUILayoutResolver::LoadTemplate(0x21000033, 0x10000467);
	auto Child = [](const TSharedPtr<FACEUIElement>& Root, const TCHAR* Name) -> TSharedPtr<FACEUIElement>
	{
		if (Root) for (const auto& C : Root->Children) if (C->ElementName == Name) return C;
		return nullptr;
	};
	const auto NameTemplate = Child(ItemTemplate,TEXT("ComponentName"));
	const auto CountTemplate = Child(ItemTemplate,TEXT("ComponentLevel"));
	const auto EntryTemplate = Child(ItemTemplate,TEXT("ComponentLevelEntry"));
	auto Font = [this](UTextBlock* Label, const TSharedPtr<FACEUIElement>& El, int32 Width)
	{
		if (auto* Retail = Cast<UACERetailTextBlock>(Label); Retail && El)
			Retail->SetRetailElement(Canvas->GetResourceResolver(),El,FVector2D(1,1),Width,false);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	};
	for (int32 Row = 0; Row < FMath::Max(VisibleRows, ComponentNameLabels.Num()); ++Row)
	{
		UTextBlock* NameLabel = MakeLabel(ComponentNameLabels, Row, ETextJustify::Left);
		UTextBlock* CountLabel = MakeLabel(ComponentCountLabels, Row, ETextJustify::Right);
		while (ComponentDesiredEntries.Num() <= Row)
		{
			auto* Entry = Canvas->WidgetTree->ConstructWidget<UACERetailTextEntry>();
			Entry->bDigitsOnly = true;
			Entry->MaxLength = 5;
			Entry->OnContextCommitted.AddDynamic(this, &UACEUIGameplayBinder::HandleComponentCommitted);
			ComponentDesiredEntries.Add(Entry);
		}
		auto* Entry = ComponentDesiredEntries[Row].Get();
		UBorder* Icon = EnsureIconBorder(ComponentIcons, Row);
		UBorder* Background = EnsureIconBorder(ComponentBackgrounds, Row);
		UBorder* EntryBackground = EnsureIconBorder(ComponentEntryBackgrounds, Row);
		const int32 Index = ComponentScrollOffset + Row;
		if (Row >= VisibleRows || !Rows.IsValidIndex(Index))
		{
			UWidget* Widgets[] = {NameLabel,CountLabel,Entry,Icon,Background,EntryBackground};
			for (UWidget* W : Widgets)
				if (W) W->SetVisibility(ESlateVisibility::Collapsed);
			if (ComponentRowWcids.IsValidIndex(Row)) ComponentRowWcids[Row] = 0;
			continue;
		}
		const FACEComponentRow& Data = Rows[Index];
		if (Entry->HasKeyboardFocus() && (Data.bHeader || Entry->ContextId != Data.Wcid))
			FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
		const float RowY = Origin.Y + Row * ComponentRowHeight;
		ComponentRowWcids[Row] = Data.bHeader ? 0 : Data.Wcid;
		SetIconDid(Background, Data.bHeader ? 0x06001392 : Data.Wcid == SelectedComponentWcid ? 0x06005E22 : 0x06005E21);
		Place(Background, Origin.X, RowY, ListWidth, ComponentRowHeight, ComponentZ);
		Background->SetVisibility(ESlateVisibility::HitTestInvisible);
		NameLabel->SetText(FText::FromString(Data.bHeader ? Data.Name.ToUpper() : Data.Name));
		NameLabel->SetJustification(Data.bHeader ? ETextJustify::Center : ETextJustify::Left);
		Font(NameLabel,Data.bHeader ? HeaderTemplate : NameTemplate,Data.bHeader ? ListWidth : ListWidth-104);
		Place(NameLabel, Origin.X+(Data.bHeader ? 0 : 42), RowY, Data.bHeader ? ListWidth : ListWidth-104,32,ComponentZ+2);
		NameLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (Data.bHeader)
		{
			Icon->SetVisibility(ESlateVisibility::Collapsed);
			CountLabel->SetVisibility(ESlateVisibility::Collapsed);
			Entry->SetVisibility(ESlateVisibility::Collapsed);
			EntryBackground->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		SetIconDid(Icon,Data.IconDid);
		Place(Icon,Origin.X,RowY,32,32,ComponentZ+1);
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
		Font(CountLabel,CountTemplate,60);
		CountLabel->SetText(FText::AsNumber(Data.Carried));
		Place(CountLabel,Origin.X+ListWidth-60,RowY,60,15,ComponentZ+2);
		CountLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
		SetIconDid(EntryBackground,0x06004CC2);
		Place(EntryBackground,Origin.X+ListWidth-60,RowY+15,60,15,ComponentZ+1);
		EntryBackground->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (!Entry->HasKeyboardFocus() || Entry->ContextId != Data.Wcid) Entry->SetText(FText::AsNumber(Data.Desired,&FNumberFormattingOptions::DefaultNoGrouping()));
		Entry->ContextId = Data.Wcid;
		Entry->SetRetailElement(Canvas->GetResourceResolver(),EntryTemplate);
		Place(Entry,Origin.X+ListWidth-60,RowY+15,60,15,ComponentZ+3);
		Entry->SetVisibility(ESlateVisibility::Visible);
	}

	if (TSharedPtr<FACEUIElement> Bar = Manager->FindElementUnder(
		TEXT("SpellComponentPage"), TEXT("SpellComponents_ComponentList_Scrollbar")))
	{
		const float Frac = MaxOffset > 0
			? static_cast<float>(ComponentScrollOffset) / static_cast<float>(MaxOffset) : 0.f;
		const float VisibleFrac = Rows.Num() > 0
			? FMath::Clamp(static_cast<float>(VisibleRows) / static_cast<float>(Rows.Num()), 0.05f, 1.f)
			: 1.f;
		SyncDatScrollbar(Bar, Frac, VisibleFrac);
	}
}

bool UACEUIGameplayBinder::IsPointerOverQuestList(FVector2D CanvasLocalPos) const
{
	if (!Manager || ActivePanelPage != TEXT("QuestManagementPanel_Field"))
	{
		return false;
	}
	if (ActiveQuestTab == TEXT("JournalPage")) return false;
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(ActiveQuestTab,
		ActiveQuestTab == TEXT("PageListPage") ? TEXT("PageListBox") : TEXT("ContractsBox"));
	if (!ListEl.IsValid() || !ListEl->bVisible)
	{
		return false;
	}
	const FIntPoint O = ListEl->GetScreenOrigin();
	return CanvasLocalPos.X >= O.X && CanvasLocalPos.X < O.X + ListEl->Width
		&& CanvasLocalPos.Y >= O.Y && CanvasLocalPos.Y < O.Y + ListEl->Height;
}

bool UACEUIGameplayBinder::ScrollQuestList(float WheelDelta)
{
	if (ActivePanelPage != TEXT("QuestManagementPanel_Field"))
	{
		return false;
	}
	const int32 Dir = WheelDelta > 0.f ? -1 : 1;
	int32& Offset = ActiveQuestTab == TEXT("PageListPage") ? JournalScrollOffset : QuestScrollOffset;
	Offset = FMath::Max(0, Offset + Dir * 3);
	RefreshQuestOverlays();
	return true;
}

bool UACEUIGameplayBinder::IsPointerOverComponentList(FVector2D CanvasLocalPos) const
{
	if (!Manager || ActivePanelPage != TEXT("SpellManagementPanel_Field")
		|| ActiveSpellPanelTab != TEXT("SpellComponentPage"))
	{
		return false;
	}
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(
		TEXT("SpellComponentPage"), TEXT("SpellComponents_ComponentList"));
	if (!ListEl.IsValid() || !ListEl->bVisible)
	{
		return false;
	}
	const FIntPoint O = ListEl->GetScreenOrigin();
	return CanvasLocalPos.X >= O.X && CanvasLocalPos.X < O.X + ListEl->Width
		&& CanvasLocalPos.Y >= O.Y && CanvasLocalPos.Y < O.Y + ListEl->Height;
}

bool UACEUIGameplayBinder::ScrollComponentList(float WheelDelta, FVector2D CanvasLocalPos)
{
	if (!IsPointerOverComponentList(CanvasLocalPos))
	{
		return false;
	}
	const int32 Dir = WheelDelta > 0.f ? -1 : 1;
	ComponentScrollOffset = FMath::Max(0, ComponentScrollOffset + Dir * 3);
	RefreshComponentOverlays();
	return true;
}

bool UACEUIGameplayBinder::TryHandleComponentListClick(FVector2D CanvasLocalPos, bool bRightClick)
{
	if (!IsPointerOverComponentList(CanvasLocalPos) || !Manager || !Client)
	{
		return false;
	}
	TSharedPtr<FACEUIElement> ListEl = Manager->FindElementUnder(
		TEXT("SpellComponentPage"), TEXT("SpellComponents_ComponentList"));
	if (!ListEl.IsValid())
	{
		return false;
	}
	const FIntPoint O = ListEl->GetScreenOrigin();
	const int32 Row = static_cast<int32>((CanvasLocalPos.Y - O.Y) / ComponentRowHeight);
	if (!ComponentRowWcids.IsValidIndex(Row) || ComponentRowWcids[Row] == 0)
	{
		return true;
	}
	const int32 Wcid = ComponentRowWcids[Row];
	SelectedComponentWcid = Wcid;
	RefreshComponentOverlays();
	return true;
}

void UACEUIGameplayBinder::HandleComponentCommitted(int32 Wcid, const FText& Text, ETextCommit::Type Method)
{
	if (!Client || !Wcid || Method == ETextCommit::OnCleared) return;
	const int32 Amount = FMath::Clamp(FCString::Atoi(*Text.ToString()),0,5000);
	if (Client->GetDesiredComponents().FindRef(Wcid) != Amount) Client->SendSetDesiredComponentLevel(Wcid,Amount);
	for (const auto& Entry : ComponentDesiredEntries) if (Entry && Entry->ContextId == Wcid)
		Entry->SetText(FText::AsNumber(Amount,&FNumberFormattingOptions::DefaultNoGrouping()));
}
