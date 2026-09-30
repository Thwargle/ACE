#pragma once
#include "ACETypes.h"

namespace ACEVendorPricing
{
inline uint32 VendorSellCost(const FACEWorldObject& Item,int32 Quantity,float SellRate)
{
    // Retail VendorProfile divides stack value first. Trade notes have a fixed premium.
    const double Rate=Item.ItemType==ACEItemType::PromissoryNote?1.15:double(SellRate);
    const int32 UnitValue=FMath::Max(0,Item.Value)/FMath::Max(1,Item.StackSize);
    return uint32(FMath::Max(1,FMath::CeilToInt(FMath::Min(double(MAX_int32),Rate*UnitValue*Quantity-.1))));
}
inline int32 VendorBuyPayout(const FACEWorldObject& Item,int32 Quantity,float BuyRate)
{
    const double Rate=Item.ItemType==ACEItemType::PromissoryNote?1.0:double(BuyRate);
    const int32 UnitValue=FMath::Max(0,Item.Value)/FMath::Max(1,Item.StackSize);
    return FMath::Max(1,FMath::FloorToInt(FMath::Min(double(MAX_int32),Rate*UnitValue*Quantity+.1)));
}
}
