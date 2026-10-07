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
		// ACCWeenieObject::GetHousePayment treats promissory notes as pyreal value.
		if(Wcid==273 && (Item.ItemType&ACEItemType::PromissoryNote)) Total+=FMath::Max(0,Item.Value);
		else if(Item.WeenieClassId==Wcid) Total+=FMath::Max(1,Item.StackSize);
	}
	return Total;
}

bool FACESession::StageHousePayment(int32 Guid,bool bRent)
{
	if(State!=EACESessionState::InWorld || !HouseOffer.LordGuid || (bRent ? !HouseOffer.OwnerGuid : HouseOffer.OwnerGuid!=0)) return false;
	// Retail recursively expands packs, including the main backpack, into accepted items.
	TSet<int32> Visited;
	TFunction<bool(int32)> Add=[&](int32 Id)
	{
		if(Visited.Contains(Id)) return false;
		Visited.Add(Id);
		if(Id!=PlayerGuid && !IsHousePaymentOwned(Id)) return false;
		bool Added=false;
		if(const auto* Contents=ContainerContents.Find(Id))
		{
			for(const auto& Ref:*Contents) Added=Add(Ref.ItemGuid)||Added;
			// gmSlumlordUI::AddItem never offers a nonempty container itself.
			if(!Contents->IsEmpty()) return Added;
		}
		const auto* Item=WorldObjects.Find(Id);
		if(!Item || Id==PlayerGuid) return Added;
		auto& Cart=bRent?HouseRentItems:HouseBuyItems;
		if(Cart.Contains(Id)) return Added;
		const int32 Wcid=(Item->ItemType&ACEItemType::PromissoryNote)?273:Item->WeenieClassId;
		const auto& Requirements=bRent?HouseOffer.Rent:HouseOffer.Buy;
		for(const auto& P:Requirements)
			if(P.Wcid==Wcid && int64(P.Required)>P.Paid+GetHousePaymentAmount(Wcid,bRent))
			{Cart.Add(Id); return true;}
		return Added;
	};
	return Add(Guid);
}

void FACESession::RemoveHousePayment(int32 Guid,bool bRent)
{
	(bRent?HouseRentItems:HouseBuyItems).Remove(Guid);
}

bool FACESession::CanPayHouse(bool bRent) const
{
	if(State!=EACESessionState::InWorld || !HouseOffer.LordGuid || (bRent ? !HouseOffer.OwnerGuid : HouseOffer.OwnerGuid!=0)) return false;
	const auto& Cart=GetHousePaymentItems(bRent);
	if(!Cart.ContainsByPredicate([this](int32 Guid){return IsHousePaymentOwned(Guid);})) return false;
	// Retail accepts partial maintenance; a purchase needs every requirement covered.
	if(bRent) return true;
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
