#pragma once
#include "ACETypes.h"

namespace ACEVTObjectClass
{
// Decal's class is derived from the wire category/description flags, not WCID.
inline int Classify(const FACEWorldObject& O)
{
    int Result=0;const uint32 Type=uint32(O.ItemType),Flags=uint32(O.ObjectDescriptionFlags);
    const TPair<uint32,int> Categories[]={{1,1},{2,2},{4,3},{8,4},{16,5},{32,6},{64,7},{128,8},{256,9},{512,10},{1024,32},{2048,11},{4096,12},{16384,13},{32768,31},{65536,14},{0x40000,15},{0x80000,16},{0x100000,41},{0x200000,17},{0x400000,18},{0x800000,19},{0x1000000,20},{0x2000000,21},{0x4000000,22},{0x8000000,23},{0x20000000,40},{0x40000000,39}};
    for(const auto& Pair:Categories)if(Type&Pair.Key){Result=Pair.Value;break;}
    const TPair<uint32,int> Overrides[]={{8,24},{0x200,25},{0x1000,26},{0x2000,27},{0x4000,28},{0x8000,6},{0x10000,29},{0x20000,30},{0x40000,14},{0x800000,38},{1,10}};
    for(const auto& Pair:Overrides)if(Flags&Pair.Key){Result=Pair.Value;break;}
    if((Type&0x2000)&&(Flags&0x100)&&Result==0)Result=(Flags&2)?34:(Flags&4)?35:(Flags&15)?33:0;
    if((Type&0x2000)&&O.SpellDID)Result=42;
    if(Result==5&&!O.IsAttackable())Result=37;
    if(Result==5&&(Flags&0x4000000))Result=43;
    return Result;
}
}
