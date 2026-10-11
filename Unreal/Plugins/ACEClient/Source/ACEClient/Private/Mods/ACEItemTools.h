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
inline int32 PurchaseRoom(UACEClientSubsystem* C,const FACEWorldObject& Stock)
{
    auto Session=C->GetSession();if(!Session)return 0;
    auto PackSlot=[](const FACEWorldObject& O){return ACEInventoryRules::IsContainer(O)||(O.ItemType&ACEItemType::Container)||(O.ObjectDescriptionFlags&ACEObjectDescFlag::RequiresPackSlot);};
    const bool IsPack=PackSlot(Stock);const auto Offered=C->GetTradeSelfItems();
    TMap<int32,int32> Counts;Counts.Add(C->GetPlayerGuid(),0);
    for(const auto& Pack:C->GetPlayerPacks())if(!Offered.Contains(Pack.Guid))Counts.Add(Pack.Guid,0);
    int64 Room=0;
    for(const auto& Pair:Session->GetWorldObjects())
    {
        const auto& I=Pair.Value;if(I.WielderId||I.ParentGuid||I.CurrentWieldedLocation)continue;
        if(auto* Count=Counts.Find(I.ContainerId);Count&&PackSlot(I)==IsPack)
        {
            ++*Count;
            if(!IsPack&&I.WeenieClassId==Stock.WeenieClassId&&Transferable(C,I))Room+=FMath::Max(0,I.MaxStackSize-FMath::Max(1,I.StackSize));
        }
    }
    for(auto& Pair:Counts)
    {
        if(const auto* Contents=Session->GetContainerContents(Pair.Key))
            for(const auto& Ref:*Contents)if(!Session->GetWorldObjects().Contains(Ref.ItemGuid)&&(Ref.ContainerType!=0)==IsPack)++Pair.Value;
        FACEWorldObject Pack;if(!C->GetWorldObject(Pair.Key,Pack))continue;
        const int32 Capacity=IsPack?Pack.ContainersCapacity:Pack.ItemsCapacity;
        if(Capacity==-1)return ACEInventoryRules::VendorPurchaseLimit(Stock);
        Room+=int64(FMath::Max(0,Capacity-Pair.Value))*FMath::Max(1,Stock.MaxStackSize);
    }
    return int32(FMath::Clamp<int64>(Room,0,ACEInventoryRules::VendorPurchaseLimit(Stock)));
}
inline double Number(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double Default=0)
{double V;return O&&O->TryGetNumberField(K,V)&&FMath::IsFinite(V)?V:Default;}
inline FString String(const TSharedPtr<FJsonObject>& O,const TCHAR* K)
{FString V;if(O)O->TryGetStringField(K,V);return V;}
inline int32 Guid(const TSharedPtr<FJsonObject>& O,const TCHAR* K)
{const double V=Number(O,K);return V>0&&V<=MAX_uint32&&FMath::FloorToDouble(V)==V?int32(uint32(V)):0;}
}
