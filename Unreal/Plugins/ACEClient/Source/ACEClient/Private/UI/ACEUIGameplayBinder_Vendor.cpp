#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"

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

	// Creation, containment and the source's size change can arrive separately.
	// Require both sides of the split, ignoring pre-existing identical stacks.
	FACEWorldObject Source;
	if (!Client->GetWorldObject(VendorSellSplit.SourceGuid, Source)
		|| Source.ContainerId != VendorSellSplit.ContainerGuid
		|| Source.StackSize != VendorSellSplit.OriginalSize - VendorSellSplit.Amount) return;
	int32 NewGuid = 0;
	for (const auto& Item : Client->GetPackItems(VendorSellSplit.ContainerGuid))
	{
		if (VendorSellSplit.ExistingGuids.Contains(Item.Guid) || Item.WeenieClassId != VendorSellSplit.Wcid
			|| FMath::Max(1, Item.StackSize) != VendorSellSplit.Amount
			|| !Client->IsOwnedInventoryItem(Item) || Item.WielderId || Item.CurrentWieldedLocation) continue;
		if (NewGuid)
		{
			// The wire protocol has no split correlation ID. Never guess between two
			// matching new objects (e.g. simultaneous inventory activity).
			VendorSellSplit = {};
			PostInventorySystemMessage(TEXT("The stack split completed. Select the new stack and add it to the sell list."));
			return;
		}
		NewGuid = Item.Guid;
	}
	if (!NewGuid) return;
	const int32 Amount = VendorSellSplit.Amount;
	const bool bSellWhenReady = VendorSellSplit.bSellWhenReady;
	VendorSellSplit = {};
	SelectInventoryGuid(NewGuid);
	AddInventoryGuidToVendorSellCart(NewGuid, Amount);
	if (bSellWhenReady) SellVendorCart(true);
}
