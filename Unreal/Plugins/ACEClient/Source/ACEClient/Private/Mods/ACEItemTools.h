#pragma once
#include "ACEClientSubsystem.h"
#include "ACEInventoryRules.h"
#include "ACESession.h"

namespace ACEItemTools
{
inline bool Transferable(UACEClientSubsystem* C, FACEWorldObject Item)
{
    if(!C||!C->IsOwnedInventoryItem(Item)
        ||(Item.ObjectDescriptionFlags&ACEObjectDescFlag::Retained))return false;
    // Empty packs may be given individually, never their hidden contents.
    if(ACEInventoryRules::IsContainer(Item))
    {
        auto Session=C->GetSession();if(!Session)return false;
        if(const auto* Contents=Session->GetContainerContents(Item.Guid);Contents&&!Contents->IsEmpty())return false;
        for(const auto& Pair:Session->GetWorldObjects())if(Pair.Value.ContainerId==Item.Guid)return false;
    }
    const auto Offered=C->GetTradeSelfItems();
    for(int32 Depth=0;Depth<32;++Depth)
    {
        if(Item.WielderId||Item.ParentGuid||Item.CurrentWieldedLocation||Offered.Contains(Item.Guid))return false;
        if(Item.ContainerId==C->GetPlayerGuid())return true;
        if(!C->GetWorldObject(Item.ContainerId,Item))return false;
    }
    return false;
}
inline bool UsesPackSlot(const FACEWorldObject& Item)
{
    return ACEInventoryRules::IsContainer(Item)||(Item.ItemType&ACEItemType::Container)||(Item.ObjectDescriptionFlags&ACEObjectDescFlag::RequiresPackSlot);
}
struct FVendorSpace
{
    int32 ItemSlots=0,PackSlots=0,CoinStack=25000;

    int32 PurchaseRoom(const FACEWorldObject& Stock) const
    {
        // ACE checks new stacks before charging the player or merging anything.
        return (UsesPackSlot(Stock)?PackSlots:ItemSlots)>0?ACEInventoryRules::VendorPurchaseLimit(Stock):0;
    }
    int64 PayoutRoom(bool Split) const
    {
        return int64(FMath::Max(0,ItemSlots-(Split?1:0)))*CoinStack;
    }
};
inline FVendorSpace VendorSpace(UACEClientSubsystem* C)
{
    FVendorSpace Space;
    auto Session=C->GetSession();if(!Session)return Space;
    const auto Offered=C->GetTradeSelfItems();
    TMap<int32,int32> Counts;
    const int32 Player=C->GetPlayerGuid();
    if(!Player)return Space;
    Counts.Add(Player,0);
    for(const auto& Pack:C->GetPlayerPacks())
        if(ACEInventoryRules::IsContainer(Pack)&&!Offered.Contains(Pack.Guid))Counts.Add(Pack.Guid,0);
    Session->GetPackItemCounts(Counts);
    for(const auto& Pair:Counts)
    {
        const auto* Pack=Session->GetWorldObjects().Find(Pair.Key);
        const int32 Capacity=ACEInventoryRules::SlotCapacity(Pack,Pair.Key==Player,false);
        Space.ItemSlots+=Capacity==-1?MAX_uint16:FMath::Max(0,Capacity-Pair.Value);
        if(Pair.Key==Player)
        {
            const int32 PackCapacity=ACEInventoryRules::SlotCapacity(Pack,true,true);
            Space.PackSlots=PackCapacity==-1?MAX_uint16:FMath::Max(0,PackCapacity-Session->CountContainerSlots(Player,true));
        }
    }
    for(const auto& Pair:Session->GetWorldObjects())
    {
        const auto& Item=Pair.Value;
        if(Item.WeenieClassId==273&&Item.MaxStackSize>0&&Counts.Contains(Item.ContainerId))
        {Space.CoinStack=Item.MaxStackSize;break;}
    }
    return Space;
}
inline int32 PurchaseRoom(UACEClientSubsystem* C,const FACEWorldObject& Stock)
{
    return VendorSpace(C).PurchaseRoom(Stock);
}
inline bool IsMMD(const FACEWorldObject& Stock)
{
    return Stock.WeenieClassId==20630&&Stock.ItemType==ACEItemType::PromissoryNote&&ACEInventoryRules::VendorPurchaseLimit(Stock)>0;
}
inline bool IsPea(const FACEWorldObject& Item)
{
    const int32 Id=Item.WeenieClassId;
    return (Id>=8284&&Id<=8357)||Id==20963||Id==25729||Id==36642||Id==36675||(Id>=36729&&Id<=36735)||Id==39329
        ||(Item.ItemType==ACEItemType::SpellComponents&&Item.Name.EndsWith(TEXT(" Pea")));
}
inline bool HasMMDVendor(UACEClientSubsystem* C)
{
    if(!C)return false;
    const auto Session=C->GetSession();
    return Session&&Session->GetOpenVendorGuid()&&Session->VendorUsesPyreals()&&Session->GetVendorMerchandise().ContainsByPredicate(IsMMD);
}
inline double Number(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double Default=0)
{double V;return O&&O->TryGetNumberField(K,V)&&FMath::IsFinite(V)?V:Default;}
inline FString String(const TSharedPtr<FJsonObject>& O,const TCHAR* K)
{FString V;if(O)O->TryGetStringField(K,V);return V;}
inline int32 Guid(const TSharedPtr<FJsonObject>& O,const TCHAR* K)
{const double V=Number(O,K);return V>0&&V<=MAX_uint32&&FMath::FloorToDouble(V)==V?int32(uint32(V)):0;}
}
