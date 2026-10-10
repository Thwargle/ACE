#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
#include "ACEInventoryRules.h"
#include "ACESession.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"

bool UACEUIGameplayBinder::CanMoveInventoryAmountToContainer(int32 Guid, int32 Container, int32 Amount, int32 MergeTarget) const
{
	FACEWorldObject Item, Destination;
	if (!Client || !Guid || !Container || Guid == Client->GetPlayerGuid() || Guid == Container
		|| !Client->GetWorldObject(Guid, Item) || !Client->GetWorldObject(Container, Destination)
		|| Client->GetTradeSelfItems().Contains(Guid) || Client->GetTradeSelfItems().Contains(Container)) return false;
	Amount = Amount > 0 ? FMath::Min(Amount, FMath::Max(1, Item.StackSize)) : FMath::Max(1, Item.StackSize);
	const bool bPack = ACEInventoryRules::IsContainer(Item) || (Item.ItemType & ACEItemType::Container)
		|| (Item.ObjectDescriptionFlags & ACEObjectDescFlag::RequiresPackSlot);
	// Retail's side-pack list is separate from ordinary inventory slots.
	if (bPack && Container != Client->GetPlayerGuid()) return false;
	if (MergeTarget)
	{
		FACEWorldObject Target;
		if (Client->GetWorldObject(MergeTarget, Target) && Target.ContainerId == Container
			&& !Client->GetTradeSelfItems().Contains(MergeTarget)
			&& ACEInventoryRules::MergeAmount(Item, Target) > 0) return true;
	}
	// A full move within the same pack simply reorders its existing slot.
	if (Item.ContainerId == Container && Amount == FMath::Max(1, Item.StackSize)) return true;
	const int32 Capacity = bPack ? Destination.ContainersCapacity : Destination.ItemsCapacity;
	if (Capacity == -1) return true;
	return Capacity > (bPack ? Client->GetPlayerPacks().Num() : Client->GetPackItems(Container).Num());
}

void UACEUIGameplayBinder::UpdateInventoryContainerDropFeedback(FVector2D CanvasLocalPos)
{
	if (!Client || !Canvas || !Manager || !Canvas->WidgetTree) return;
	const FVector2D Absolute = Canvas->GetCachedGeometry().LocalToAbsolute(CanvasLocalPos);
	const int32 Amount = InvDragAmount > 0 ? InvDragAmount : GetSelectedItemAmount(InvDragGuid);
	auto Mark = [&](UBorder* Cell, bool Accept, bool DropIn, const TCHAR* Owner)
	{
		const auto* CellSlot = Cell ? Cast<UCanvasPanelSlot>(Cell->Slot) : nullptr;
		if (!CellSlot) return;
		if (!PaperDollDragTargetIcon)
		{
			PaperDollDragTargetIcon = Canvas->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			PaperDollDragTargetIcon->SetPadding(FMargin(0));
		}
		// ItemSlot_DragOver_DropIn / Accept / Reject, from retail layout 0x21000037.
		SetIconDid(PaperDollDragTargetIcon, Accept ? (DropIn ? 0x060011F7 : 0x060011F9) : 0x060011F8);
		PaperDollDragTargetIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (PaperDollDragTargetIcon->GetParent() != Canvas->GetElementLayer())
			Canvas->GetElementLayer()->AddChild(PaperDollDragTargetIcon);
		if (auto* Slot = Cast<UCanvasPanelSlot>(PaperDollDragTargetIcon->Slot))
		{
			Slot->SetAnchors(FAnchors(0,0)); Slot->SetAutoSize(false);
			Slot->SetPosition(CellSlot->GetPosition()); Slot->SetSize(CellSlot->GetSize());
			Canvas->SetOverlayOrder(PaperDollDragTargetIcon, Manager->FindElementByName(Owner), 120004);
		}
	};
	for (int32 I=0; I<PackSlotBgs.Num() && I<PackSlotGuids.Num(); ++I)
	{
		auto* Cell=PackSlotBgs[I].Get();
		if (!Cell || !Cell->IsVisible() || !Canvas->IsWidgetExposedAt(Cell,Absolute)) continue;
		const bool Reorder = InvDragPackSlotIndex != INDEX_NONE && InvDragGuid != Client->GetPlayerGuid();
		const int32 Container = Reorder ? Client->GetPlayerGuid() : PackSlotGuids[I];
		Mark(Cell, CanMoveInventoryAmountToContainer(InvDragGuid,Container,Amount), !Reorder, TEXT("InventoryPanel_Field"));
		return;
	}
	int32 Target=0, Index=INDEX_NONE;
	if (HitTestInventorySlot(Absolute,Target,Index))
	{
		const int32 Visible=Index-InventoryScrollOffset;
		FACEWorldObject Item;
		const bool Ordinary=Client->GetWorldObject(InvDragGuid,Item) && !ACEInventoryRules::IsContainer(Item)
			&& !(Item.ItemType & ACEItemType::Container) && !(Item.ObjectDescriptionFlags & ACEObjectDescFlag::RequiresPackSlot);
		if (InventorySlotBgs.IsValidIndex(Visible))
			Mark(InventorySlotBgs[Visible], Ordinary && CanMoveInventoryAmountToContainer(InvDragGuid,
				SelectedPackGuid ? SelectedPackGuid : Client->GetPlayerGuid(),Amount,Target), false, TEXT("InventoryPanel_Field"));
	}
}

bool UACEUIGameplayBinder::TryDropInExternalContainer(int32 Guid, int32 Amount, FVector2D Absolute)
{
	if (!Client || !Canvas || !Manager || !OpenLootContainerGuid) return false;
	for (int32 I = 0; I < ExtPackSlots.Num() && I < ExtPackGuids.Num(); ++I)
		if (ExtPackSlots[I] && ExtPackGuids[I] && ExtPackSlots[I]->IsVisible() && Canvas->IsWidgetExposedAt(ExtPackSlots[I], Absolute))
		{
			MoveInventoryAmount(Guid, ExtPackGuids[I], 0, Amount);
			return true;
		}
	const int32 Container = OpenLootSelectedPackGuid ? OpenLootSelectedPackGuid : OpenLootContainerGuid;
	for (int32 I = 0; I < ExtItemSlots.Num() && I < ExtItemGuids.Num(); ++I)
		if (ExtItemSlots[I] && ExtItemSlots[I]->IsVisible() && Canvas->IsWidgetExposedAt(ExtItemSlots[I], Absolute))
		{
			if (ExtItemGuids[I] == Guid) return true;
			if (!MergeInventoryAmount(Guid, ExtItemGuids[I], Amount))
			{
				FACEWorldObject Source;
				int32 Place = I + ExtItemScrollOffset;
				if (Client->GetWorldObject(Guid, Source) && Source.ContainerId == Container
					&& Source.PlacementPosition >= 0 && Source.PlacementPosition < Place && Amount >= Source.StackSize) --Place;
				MoveInventoryAmount(Guid, Container, Place, Amount);
			}
			return true;
		}
	const auto List = Manager->FindElementUnder(TEXT("ExternalContainer"), TEXT("Ext_Container_ItemList"));
	if (!List || !Canvas->IsElementExposedAt(List, Canvas->GetCachedGeometry().AbsoluteToLocal(Absolute))) return false;
	MoveInventoryAmount(Guid, Container, Client->GetPackItems(Container).Num(), Amount);
	return true;
}

int32 UACEUIGameplayBinder::GetSelectedItemAmount(int32 Guid) const
{
	FACEWorldObject Item;
	if (!Client || !Client->GetWorldObject(Guid, Item)) return 0;
	const int32 Size = FMath::Max(1, Item.StackSize);
	// ItemHolder::GetObjectSplitSize: only the selected object uses the slider.
	return LastSelection.Guid == Guid ? FMath::Clamp(SelectedStackAmount, 1, Size) : Size;
}

void UACEUIGameplayBinder::PickupInventoryAmount(int32 Guid, int32 Amount)
{
	if (!Client) return;
	if (Amount <= 0) Amount = GetSelectedItemAmount(Guid);
	MoveInventoryAmount(Guid, Client->ResolvePickupContainer(Guid, SelectedPackGuid, Amount), 0, Amount);
}

void UACEUIGameplayBinder::MoveInventoryAmount(int32 Guid, int32 Container, int32 Placement, int32 Amount)
{
	FACEWorldObject Item;
	if (!Client || !Container || Amount <= 0 || !Client->GetWorldObject(Guid, Item)) return;
	if (Client->GetTradeSelfItems().Contains(Guid))
	{
		PostInventorySystemMessage(TEXT("Remove that item from the trade before moving it."));
		return;
	}
	Amount = FMath::Min(Amount, FMath::Max(1, Item.StackSize));
	// UIElement_ItemList only removes the source's old position for a full move.
	// A split inserts a NEW object; the remainder keeps its original slot.
	if (Amount < Item.StackSize)
	{
		// Retail can merge a selected portion during pickup, but an intentional
		// split of an already-owned stack must still create a separate object.
		FACEWorldObject Destination;
		const bool bOwnedDestination = Container == Client->GetPlayerGuid()
			|| (Client->GetWorldObject(Container, Destination) && Client->IsOwnedInventoryItem(Destination));
		if (bOwnedDestination && !Item.WielderId && !Client->IsOwnedInventoryItem(Item))
		{
			TArray<int32> Containers{Container};
			if (Container == Client->GetPlayerGuid())
				for (const auto& Pack : Client->GetPlayerPacks()) Containers.Add(Pack.Guid);
			for (int32 SearchContainer : Containers)
				for (const auto& Target : Client->GetPackItems(SearchContainer))
					if (ACEInventoryRules::MergeAmount(Item, Target) >= Amount
						&& MergeInventoryAmount(Guid, Target.Guid, Amount)) return;
		}
		TrackInventoryStackSplit(InventorySelectionSplit, Item, Amount);
		InventorySelectionSplit.ContainerGuid = Container;
		Client->SendStackableSplitToContainer(Guid, Container, Placement, Amount);
	}
	else Client->SendPutItemInContainer(Guid, Container, Placement);
}

bool UACEUIGameplayBinder::MergeInventoryAmount(int32 Guid, int32 TargetGuid, int32 Amount)
{
	FACEWorldObject Source, Target;
	if (!Client || Amount <= 0 || !Client->GetWorldObject(Guid, Source) || !Client->GetWorldObject(TargetGuid, Target)) return false;
	const auto Offered = Client->GetTradeSelfItems();
	const auto OtherOffers = Client->GetTradePartnerItems();
	if (Offered.Contains(Guid) || Offered.Contains(TargetGuid) || OtherOffers.Contains(Guid) || OtherOffers.Contains(TargetGuid)) return false;
	Amount = FMath::Min(Amount, ACEInventoryRules::MergeAmount(Source, Target));
	if (Amount <= 0) return false;
	Client->SendStackableMerge(Guid, TargetGuid, Amount);
	SelectInventoryGuid(TargetGuid); // ItemHolder::AttemptMerge selects the surviving stack.
	return true;
}

void UACEUIGameplayBinder::TrackInventoryStackSplit(FInventoryStackSplit& Split, const FACEWorldObject& Source, int32 Amount)
{
	Split = {};
	Split.SourceGuid = Source.Guid;
	Split.SourceContainerGuid = Source.ContainerId;
	Split.ContainerGuid = Source.ContainerId;
	Split.Wcid = Source.WeenieClassId;
	Split.Amount = Amount;
	Split.OriginalSize = Source.StackSize;
	Split.Deadline = FPlatformTime::Seconds() + 15.0;
	Split.ExistingGuids.Reset();
	for (const auto& Existing : Client->GetWorldObjects()) Split.ExistingGuids.Add(Existing.Guid);
}

int32 UACEUIGameplayBinder::FindCompletedInventoryStackSplit(const FInventoryStackSplit& Split, bool bRequireOwned) const
{
	// Object creation, containment and source count updates may arrive in any order.
	FACEWorldObject Source;
	if (!Client || !Client->GetWorldObject(Split.SourceGuid, Source)
		|| Source.ContainerId != Split.SourceContainerGuid || Source.StackSize != Split.OriginalSize - Split.Amount) return 0;
	int32 NewGuid = 0;
	const auto Candidates = Split.bToWorld || Split.WieldLocation ? Client->GetWorldObjects() : Client->GetPackItems(Split.ContainerGuid);
	for (const auto& Item : Candidates)
	{
		if (Split.ExistingGuids.Contains(Item.Guid) || Item.WeenieClassId != Split.Wcid
			|| FMath::Max(1, Item.StackSize) != Split.Amount) continue;
		if (Split.bToWorld)
		{
			if (!Item.bHasPosition || Item.ContainerId || Item.WielderId || Item.ParentGuid) continue;
		}
		else if (Split.WieldLocation)
		{
			if (Item.WielderId != Client->GetPlayerGuid() || Item.CurrentWieldedLocation != Split.WieldLocation) continue;
		}
		else if (Item.ContainerId != Split.ContainerGuid || Item.WielderId || Item.CurrentWieldedLocation
			|| (bRequireOwned && !Client->IsOwnedInventoryItem(Item))) continue;
		// No transaction ID exists on the wire. Never guess between matching new objects.
		if (NewGuid) return INDEX_NONE;
		NewGuid = Item.Guid;
	}
	return NewGuid;
}

void UACEUIGameplayBinder::UpdateInventorySplitSelection()
{
	if (!InventorySelectionSplit.SourceGuid) return;
	if (!Client || !Client->GetSession() || Client->GetSession()->GetState() != EACESessionState::InWorld
		|| FPlatformTime::Seconds() >= InventorySelectionSplit.Deadline)
	{
		InventorySelectionSplit = {};
		return;
	}
	const int32 NewGuid = FindCompletedInventoryStackSplit(InventorySelectionSplit, false);
	if (!NewGuid) return;
	InventorySelectionSplit = {};
	if (NewGuid != INDEX_NONE) SelectInventoryGuid(NewGuid);
}

void UACEUIGameplayBinder::AddInventoryGuidToTrade(int32 Guid, int32 Amount)
{
	if (!Client || !bTradeOpen || !TradePartnerGuid || Client->GetTradePartnerGuid() != TradePartnerGuid) return;
	if (TradeStackSplit.SourceGuid || VendorSellSplit.SourceGuid)
	{
		PostInventorySystemMessage(TEXT("Please wait for the stack to finish splitting."));
		return;
	}
	FACEWorldObject Item;
	if (Amount <= 0 || !Client->GetWorldObject(Guid, Item) || !Client->IsOwnedInventoryItem(Item)
		|| Client->GetTradeSelfItems().Contains(Guid)) return;
	if (Item.Attuned != 0)
	{
		PostInventorySystemMessage(TEXT("That item cannot be traded."));
		return;
	}
	Amount = FMath::Min(Amount, FMath::Max(1, Item.StackSize));
	if (Amount < Item.StackSize)
	{
		// gmSecureTradeUI::AcceptDragObject splits first, then offers the created GUID.
		TradeStackSplit.PartnerGuid = TradePartnerGuid;
		TrackInventoryStackSplit(TradeStackSplit, Item, Amount);
		Client->SendStackableSplitToContainer(Guid, Item.ContainerId, 0, Amount);
		PostInventorySystemMessage(TEXT("Splitting the selected amount before trading it."));
	}
	else Client->SendAddToTrade(Guid, 0);
}

void UACEUIGameplayBinder::UpdateTradeStackSplit()
{
	if (PendingTradeItemGuid && (!Client || !Client->GetSession()
		|| Client->GetSession()->GetState() != EACESessionState::InWorld
		|| FPlatformTime::Seconds() >= PendingTradeItemDeadline))
		PendingTradeItemGuid = PendingTradeItemAmount = PendingTradeItemPartner = 0;
	if (!TradeStackSplit.SourceGuid) return;
	if (!Client || !bTradeOpen || TradePartnerGuid != TradeStackSplit.PartnerGuid
		|| Client->GetTradePartnerGuid() != TradeStackSplit.PartnerGuid
		|| !Client->GetSession() || Client->GetSession()->GetState() != EACESessionState::InWorld)
	{
		TradeStackSplit = {};
		return;
	}
	if (FPlatformTime::Seconds() >= TradeStackSplit.Deadline)
	{
		TradeStackSplit = {};
		PostInventorySystemMessage(TEXT("The stack split was not confirmed. Nothing was offered; select the item and try again."));
		return;
	}
	const int32 NewGuid = FindCompletedInventoryStackSplit(TradeStackSplit);
	if (!NewGuid) return;
	const int32 Amount = TradeStackSplit.Amount;
	TradeStackSplit = {};
	if (NewGuid == INDEX_NONE)
	{
		PostInventorySystemMessage(TEXT("The stack split completed. Select the new stack and add it to the trade."));
		return;
	}
	SelectInventoryGuid(NewGuid);
	AddInventoryGuidToTrade(NewGuid, Amount);
}

bool UACEUIGameplayBinder::TryOpenTradeForDraggedItem(const FACEWorldObject& Target, int32 Guid, int32 Amount)
{
	if (!Client || !Target.bIsPlayer || Target.Guid == Client->GetPlayerGuid()
		|| !(Client->GetCharacterOptions1() & 0x04000000u)) return false;
	if (bTradeOpen && TradePartnerGuid == Target.Guid)
		AddInventoryGuidToTrade(Guid, Amount);
	else
	{
		PendingTradeItemGuid = Guid;
		PendingTradeItemAmount = Amount;
		PendingTradeItemPartner = Target.Guid;
		PendingTradeItemDeadline = FPlatformTime::Seconds() + 15.0;
		Client->SendOpenTrade(Target.Guid);
	}
	return true;
}
