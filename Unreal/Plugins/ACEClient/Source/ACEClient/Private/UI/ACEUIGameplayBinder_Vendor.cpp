#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"

bool UACEUIGameplayBinder::CanStageVendorSellItem(int32 Guid) const
{
	FACEWorldObject Item;
	if (!Client || !OpenVendorGuid || !Client->GetSession()
		|| VendorSellSplit.SourceGuid || TradeStackSplit.SourceGuid
		|| !Client->GetWorldObject(Guid, Item) || Client->GetTradeSelfItems().Contains(Guid)) return false;
	const bool bMainPack = Guid == Client->GetPlayerGuid();
	if (!bMainPack && !Client->IsOwnedInventoryItem(Item)) return false;
	// VendorSellUI::DragItemAcceptable accepts a carried pack as a bulk offer.
	// Its contents are filtered individually; the pack itself is never sold.
	if (bMainPack || (Item.ItemType & ACEItemType::Container) || Item.ItemsCapacity > 0)
		return !Client->GetPackItems(Guid).IsEmpty();
	return Item.Attuned == 0 && Client->GetSession()->CanVendorBuyItem(Item);
}

void UACEUIGameplayBinder::UpdateVendorSellSplit()
{
	if (!VendorSellSplit.SourceGuid) return;
	if (!Client || OpenVendorGuid != VendorSellSplit.VendorGuid
		|| Client->GetOpenVendorGuid() != VendorSellSplit.VendorGuid
		|| !Client->GetSession() || Client->GetSession()->GetState() != EACESessionState::InWorld)
	{
		VendorSellSplit = {};
		return;
	}
	if (FPlatformTime::Seconds() >= VendorSellSplit.Deadline)
	{
		VendorSellSplit = {};
		PostInventorySystemMessage(TEXT("The stack split was not confirmed. Nothing was sold; select the item and try again."));
		return;
	}

	const int32 NewGuid = FindCompletedInventoryStackSplit(VendorSellSplit);
	if (NewGuid == INDEX_NONE)
	{
		VendorSellSplit = {};
		PostInventorySystemMessage(TEXT("The stack split completed. Select the new stack and add it to the sell list."));
		return;
	}
	if (!NewGuid) return;
	const int32 Amount = VendorSellSplit.Amount;
	const bool bSellWhenReady = VendorSellSplit.bSellWhenReady;
	VendorSellSplit = {};
	SelectInventoryGuid(NewGuid);
	AddInventoryGuidToVendorSellCart(NewGuid, Amount);
	if (bSellWhenReady) SellVendorCart(true);
}
