#pragma once
#include "CoreMinimal.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatDatabase.h"

namespace ACEVTLootPalette
{
inline uint32 ExampleColor(UACEDatSubsystem* Dat,const FACEObjDescSubPalette& Palette)
{
    // VT bb.a samples halfway through the wire palette range. Its zero length
    // byte stays zero for this sample, even though rendering expands it to 256.
    const int32 Index=Palette.Offset+(Palette.NumColors==2048?0:Palette.NumColors/2);
    if(!Dat||!Dat->GetPortalDat()||Index<0||Index>2047)return 0;
    static TWeakObjectPtr<UACEDatSubsystem> Owner;
    static int32 Generation=INDEX_NONE;
    static TMap<uint64,uint32> Colors;
    if(Owner.Get()!=Dat||Generation!=Dat->GetDatGeneration())
    {Owner=Dat;Generation=Dat->GetDatGeneration();Colors.Reset();}
    const uint32 Id=uint32(Palette.SubPaletteId)|0x04000000u;
    const uint64 Key=(uint64(Id)<<32)|uint32(Index);
    if(const uint32* Color=Colors.Find(Key))return *Color;
    TArray<uint8> Bytes;uint32 Color=0;
    if(Dat->GetPortalDat()->ReadFile(Id,Bytes)&&Bytes.Num()>=8+(Index+1)*4)
        FMemory::Memcpy(&Color,Bytes.GetData()+8+Index*4,4);
    // A bounded color cache avoids DAT reads on every inventory snapshot.
    if(Colors.Num()>=4096)Colors.Reset();
    Colors.Add(Key,Color&0xffffffu);return Color&0xffffffu;
}
}
