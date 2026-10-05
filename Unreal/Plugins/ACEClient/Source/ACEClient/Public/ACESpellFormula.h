#pragma once
#include "CoreMinimal.h"

namespace ACESpellFormula
{
// CSpellBase::InqCustomizedSpellFormula / SpellFormula::RandomizeVersion1..3.
inline TArray<uint32> Customize(TArray<uint32> C, uint32 Version, const FString& Account)
{
    uint32 Key=0;
    for(TCHAR Ch:Account)
    {
        Key=(Key<<4)+int8(uint8(Ch));
        const uint32 High=Key&0xF0000000u;
        if(High)Key=(Key^(High>>24))&0x0FFFFFFFu;
    }
    const uint32 Seed=Key%0x13D573;
    if(Version==1 && C.Num()>=5)
    {
        const int H=C.Num()>5?2:1, P=H+1+(C.Num()>6), T=P+2+(C.Num()>7);
        const uint32 Scarab=C[0],Herb=C[H],Powder=C[P],Potion=C[P+1],Talisman=C[T];
        if(C.Num()>5)C[1]=(Powder+2*Herb+Potion+Talisman+Scarab)%12+63;
        if(C.Num()>6 && Scarab+Powder+Potion)C[3]=(Scarab+Herb+Talisman+2*(Powder+Potion))*(Seed/(Scarab+Powder+Potion))%12+63;
        if(C.Num()>7 && Talisman+Scarab)C[6]=(Powder+2*Talisman+Potion+Herb+Scarab)*(Seed/(Talisman+Scarab))%12+63;
    }
    else if(Version==2 && C.Num()>=8)
    {
        const uint32 P=C[0],D=C[4],X=C[5],A=C[7];
        C[3]=(A+3*P+2*D*X+C[2]+C[1])%12+63;
        if(C[1]*A+2*D)C[6]=(A+3*P*C[2]+2*X+D)*(Seed/(C[1]*A+2*D))%12+63;
    }
    else if(Version==3 && C.Num()>=7)
    {
        const uint32 A=(Seed+C[0])%12,B=(Key%0x4AEFD+C[1])%12,D=(Key%0x96A7F+C[2])%12;
        const uint32 E=(Key%0x100A03+C[4])%12,F=(Key%0xEB2EF+C[5])%12,G=(Key%0x121E7D+(C.Num()>7?C[7]:0))%12;
        C[3]=(A+B+D+E+F+D*F+A*B+G*(E+1))%12+63;
        C[6]=(A+B+D+E+Key%0x65039%12+G*(E*(A*B*D*F+7)+1)+F+5*A*B+11*D*F)%12+63;
    }
    return C;
}
inline TArray<uint32> WithFocus(const TArray<uint32>& Formula, uint32 Power)
{
    TArray<uint32> Result;
    for(uint32 C:Formula)if((C>=1 && C<=6)||C==110||C==111||C==112||C==192||C==193)Result.Add(C);
    const int Count=Power==1?1:Power==2?2:(Power==3||Power==4||Power==7)?3:Power>=5&&Power<=10?4:0;
    for(int I=0;I<Count;++I)Result.Add(188);
    return Result;
}
}
