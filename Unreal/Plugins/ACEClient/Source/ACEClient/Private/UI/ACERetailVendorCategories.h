#pragma once
#include "CoreMinimal.h"

namespace ACERetailVendorCategories
{
struct FCategory { const TCHAR* Name; uint32 Mask; };
// VendorItemsUI::OpenVendor (0x004C16D0), in retail menu order.
inline constexpr FCategory Categories[] = {
 {TEXT("Armor"),0x2}, {TEXT("Books, Paper"),0x2000}, {TEXT("Clothing"),0x4},
 {TEXT("Containers"),0x200}, {TEXT("Food"),0x20}, {TEXT("Gems"),0x800},
 {TEXT("Jewelry"),0x8}, {TEXT("Keys, Tools"),0x20004000}, {TEXT("Miscellaneous"),0x490},
 {TEXT("Services"),0x100000}, {TEXT("Spell Components"),0x1000}, {TEXT("Trade Notes"),0x40000},
 {TEXT("Weapons"),0x101}, {TEXT("Mana Stones"),0x80000}, {TEXT("Magic Items"),0x8000},
 {TEXT("Alchemical Items"),0x4800000}, {TEXT("Cooking Items"),0x400000}, {TEXT("Fletching Items"),0x9000000}
};
}
