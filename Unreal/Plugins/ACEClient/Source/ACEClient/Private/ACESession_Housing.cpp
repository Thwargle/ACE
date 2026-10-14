#include "ACESession.h"

namespace
{
bool ReadHouseString(FACEBinaryReader& R, FString& Text)
{
	if (!R.CanRead(2)) return false;
	const int32 Start=R.Tell(), Length=R.ReadUInt16();
	R.Seek(Start);
	if (!R.CanRead(Align(Length+2,4))) return false;
	Text=R.ReadString16L(); return true;
}
bool ReadHousePayments(FACEBinaryReader& R,TArray<FACEHousePayment>& Payments)
{
	if (!R.CanRead(4)) return false;
	const uint32 Count=R.ReadUInt32();
	if (Count>256 || Count>uint32(R.Remaining()/20)) return false;
	for(uint32 I=0;I<Count;++I)
	{
		if (!R.CanRead(12)) return false;
		FACEHousePayment P; P.Required=R.ReadInt32(); P.Paid=R.ReadInt32(); P.Wcid=R.ReadInt32();
		if (P.Required<0 || P.Paid<0 || !ReadHouseString(R,P.Name) || !ReadHouseString(R,P.PluralName)) return false;
		Payments.Add(MoveTemp(P));
	}
	return true;
}
}

void FACESession::HandleHouseProfile(FACEBinaryReader& R)
{
	// CM_House::RecvNotice_HouseProfile / HouseProfile::UnPack. Publish atomically.
	if (!R.CanRead(40)) return;
	FACEHouseProfile P;
	P.LordGuid=R.ReadInt32(); P.DwellingId=R.ReadInt32(); P.OwnerGuid=R.ReadInt32(); P.Flags=R.ReadUInt32();
	P.MinLevel=R.ReadInt32(); P.MaxLevel=R.ReadInt32(); P.MinRank=R.ReadInt32(); P.MaxRank=R.ReadInt32();
	P.bMaintenanceFree=R.ReadUInt32()!=0; P.Type=R.ReadInt32();
	if (!P.LordGuid || !ReadHouseString(R,P.OwnerName) || !ReadHousePayments(R,P.Buy) || !ReadHousePayments(R,P.Rent)) return;
	HouseOffer=MoveTemp(P); HouseBuyItems.Reset(); HouseRentItems.Reset(); ++HouseOfferRevision;
}

void FACESession::CloseHouseProfile()
{
	HouseOffer={}; HouseBuyItems.Reset(); HouseRentItems.Reset(); ++HouseOfferRevision;
}

bool FACESession::IsHousePaymentOwned(int32 Guid) const
{
	const auto* Item=WorldObjects.Find(Guid);
	if (!Item || Item->WielderId || Item->CurrentWieldedLocation || TradeSelfItems.Contains(Guid)) return false;
	TSet<int32> Seen;
	int32 Owner=Item->ContainerId;
	while(Owner && Owner!=PlayerGuid && !Seen.Contains(Owner))
	{
		Seen.Add(Owner); const auto* Parent=WorldObjects.Find(Owner); Owner=Parent?Parent->ContainerId:0;
	}
	return PlayerGuid && Owner==PlayerGuid;
}

int64 FACESession::GetHousePaymentAmount(int32 Wcid,bool bRent) const
{
	int64 Total=0;
	for(int32 Guid:GetHousePaymentItems(bRent)) if(IsHousePaymentOwned(Guid))
	{
		const auto& Item=WorldObjects.FindChecked(Guid);
		// ACE HousePayment accepts promissory notes at their full stack value.
		if(Wcid==273 && (Item.ItemType&ACEItemType::PromissoryNote)) Total+=FMath::Max(0,Item.Value);
		else if(Item.WeenieClassId==Wcid) Total+=FMath::Max(1,Item.StackSize);
	}
	return Total;
}

TArray<int32> FACESession::CollectHousePaymentItems(int32 Guid,bool bRent) const
{
	TArray<int32> Items;
	if(State!=EACESessionState::InWorld || !HouseOffer.LordGuid || (bRent ? !HouseOffer.OwnerGuid : HouseOffer.OwnerGuid!=0)) return Items;
	TMap<int32,int64> Remaining;
	for(const auto& Cost : bRent ? HouseOffer.Rent : HouseOffer.Buy)
		Remaining.Add(Cost.Wcid,int64(Cost.Required)-Cost.Paid-GetHousePaymentAmount(Cost.Wcid,bRent));
	const auto& Cart=GetHousePaymentItems(bRent);
	// Share validation between the drag marker and the drop. Recursively expand
	// packs without staging duplicate, equipped, foreign or already-paid items.
	TSet<int32> Visited;
	TFunction<void(int32)> Visit=[&](int32 Id)
	{
		if(Visited.Contains(Id)) return;
		Visited.Add(Id);
		if(Id!=PlayerGuid && !IsHousePaymentOwned(Id)) return;
		const auto* Item=WorldObjects.Find(Id);
		if(!Item) return;
		TArray<int32> Contents;
		if(Id==PlayerGuid || Item->ItemsCapacity!=0 || Item->ContainersCapacity!=0
			|| (Item->ItemType&ACEItemType::Container) || ContainerContents.Contains(Id))
		{
			// Use the inventory's reconciled view, including ObjectCreate-only bags
			// and newly received main-pack items. ViewContents is not always sent for
			// owned bags; requiring that cache made a visibly full bag look empty here.
			GetPackItemGuids(Id,Contents);
			if(Id==PlayerGuid)
			{
				TArray<int32> Packs; GetPlayerPackGuids(Packs); Contents.Append(Packs);
			}
			else if(const auto* Refs=ContainerContents.Find(Id))
				for(const auto& Ref:*Refs) Contents.AddUnique(Ref.ItemGuid);
		}
		if(!Contents.IsEmpty())
		{
			for(int32 Child:Contents) Visit(Child);
			return; // gmSlumlordUI::AddItem never offers a nonempty container itself.
		}
		if(Id==PlayerGuid || Cart.Contains(Id)) return;
		const bool Note=(Item->ItemType&ACEItemType::PromissoryNote)!=0;
		const int32 Wcid=Note?273:Item->WeenieClassId;
		auto* Needed=Remaining.Find(Wcid);
		const int64 Amount=Note?FMath::Max(0,Item->Value):FMath::Max(1,Item->StackSize);
		if(Needed && *Needed>0 && Amount>0)
		{ Items.Add(Id); *Needed-=Amount; }
	};
	Visit(Guid);
	return Items;
}

bool FACESession::CanStageHousePayment(int32 Guid,bool bRent) const
{
	return !CollectHousePaymentItems(Guid,bRent).IsEmpty();
}

bool FACESession::StageHousePayment(int32 Guid,bool bRent)
{
	const auto Items=CollectHousePaymentItems(Guid,bRent);
	(bRent?HouseRentItems:HouseBuyItems).Append(Items);
	return !Items.IsEmpty();
}

void FACESession::PruneHousePaymentItems()
{
	for(auto* Cart : {&HouseBuyItems,&HouseRentItems})
		Cart->RemoveAll([this](int32 Guid){ return !IsHousePaymentOwned(Guid); });
}
void FACESession::RemoveHousePayment(int32 Guid,bool bRent)
{
	(bRent?HouseRentItems:HouseBuyItems).Remove(Guid);
}

bool FACESession::CanPayHouse(bool bRent) const
{
	if(State!=EACESessionState::InWorld || !HouseOffer.LordGuid || (bRent ? !HouseOffer.OwnerGuid : HouseOffer.OwnerGuid!=0)) return false;
	const auto& Cart=GetHousePaymentItems(bRent);
	// Retail accepts partial maintenance; a purchase needs every requirement covered.
	if(bRent) return Cart.ContainsByPredicate([this](int32 Guid){return IsHousePaymentOwned(Guid);});
	for(const auto& P:HouseOffer.Buy)
		if(int64(P.Required)>P.Paid+GetHousePaymentAmount(P.Wcid,false)) return false;
	return true;
}

bool FACESession::SendHousePayment(bool bRent)
{
	if(!CanPayHouse(bRent)) return false;
	TArray<int32> Items=GetHousePaymentItems(bRent);
	Items.RemoveAll([this](int32 Guid){return !IsHousePaymentOwned(Guid);});
	FACEBinaryWriter W; W.WriteUInt32(HouseOffer.LordGuid); W.WriteUInt32(Items.Num());
	for(int32 Guid:Items) W.WriteUInt32(Guid);
	SendGameAction(bRent?0x0221u:0x021Cu,W.GetData(),ACEQueue::WeenieQueue);
	// Inventory and paid quantities remain server-owned. Only the staged offer clears.
	HouseBuyItems.Reset(); HouseRentItems.Reset();
	return true;
}
