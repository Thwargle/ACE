#include "ACECharacterCreation.h"
#include "Dat/ACEDatDatabase.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Protocol/ACEWindows1252.h"
#include "Misc/Paths.h"

namespace
{
// The chargen SmartArrays use Turbine compressed counts; names use .NET 7-bit
// UTF-8 lengths. Neither is a network String16L. Keep failure sticky at all nesting levels.
struct FCGReader
{
    const TArray<uint8>& B; int32 P=0; bool Ok=true;
    explicit FCGReader(const TArray<uint8>& In):B(In){}
    bool Need(int32 N) { Ok=Ok && N>=0 && N<=B.Num()-P; return Ok; }
    uint8 U8() { return Need(1)?B[P++]:0; }
    uint16 U16() { const uint16 A=U8(); return A | (uint16(U8())<<8); }
    uint32 U32() { const uint32 A=U16(); return A | (uint32(U16())<<16); }
    void Skip(int32 N) { if(Need(N)) P+=N; }
    void Align() { Skip((4-P%4)%4); }
    uint32 Packed(uint32 Type) { uint32 A=U16(); if(A&0x8000) A=((A&0x7fff)<<16)|U16(); return Type|A; }
    uint32 Count(bool Smart=true) { uint32 N=0; if(!Smart) N=U32(); else { uint32 A=U8(); N=A; if(A&0x80) { uint32 C=U8(); N=((A&0x7f)<<8)|C; if(A&0x40) N=(((A&0x3f)<<8)|C)<<16 | U16(); }} if(N>8192) {Ok=false;return 0;} return Ok?N:0; }
    FString String(bool PString=false)
    {
        uint32 N=0; if(PString) N=U16(); else for(int I=0;I<5;++I) { uint32 C=U8(); N|=(C&127)<<(I*7); if(!(C&128)) break; if(I==4)Ok=false; }
        if(N>65536 || !Need(int32(N))) {Ok=false;return {};}
        FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(B.GetData()+P),N); FString S(Text.Length(),Text.Get()); P+=N; if(PString)Align(); return S;
    }
    TArray<uint32> Array() { TArray<uint32> A; uint32 N=Count(); for(uint32 I=0;I<N&&Ok;++I) A.Add(U32()); return A; }
    FString Unicode() { FString S; uint32 N=Count(); for(uint32 I=0;I<N&&Ok;++I)S.AppendChar(U16());return S; }
    FACEObjDesc Desc()
    {
        FACEObjDesc D; Align(); if(U8()!=0x11)Ok=false; uint8 NP=U8(),NT=U8(),NA=U8();
        if(NP)D.PaletteBaseId=Packed(0x04000000);
        for(int I=0;I<NP&&Ok;++I) { FACEObjDescSubPalette V; V.SubPaletteId=Packed(0x04000000); V.Offset=U8()*8; uint8 L=U8(); V.NumColors=(L?L:256)*8; D.SubPalettes.Add(V); }
        for(int I=0;I<NT&&Ok;++I) { FACEObjDescTextureChange V; V.PartIndex=U8(); V.OldTexture=Packed(0x05000000); V.NewTexture=Packed(0x05000000); D.TextureChanges.Add(V); }
        for(int I=0;I<NA&&Ok;++I) { FACEObjDescAnimPartChange V; V.PartIndex=U8(); V.PartId=Packed(0x01000000); D.AnimPartChanges.Add(V); } Align(); return D;
    }
};
void Merge(FACEObjDesc& Out,const FACEObjDesc& In)
{
    if(In.PaletteBaseId)Out.PaletteBaseId=In.PaletteBaseId;
    Out.SubPalettes.Append(In.SubPalettes);
    for(const auto& T:In.TextureChanges) { Out.TextureChanges.RemoveAll([&](const auto& V){return V.PartIndex==T.PartIndex && V.OldTexture==T.OldTexture;}); Out.TextureChanges.Add(T); }
    for(const auto& T:In.AnimPartChanges) { Out.AnimPartChanges.RemoveAll([&](const auto& V){return V.PartIndex==T.PartIndex;}); Out.AnimPartChanges.Add(T); }
}
}

bool FACECharacterCreation::LoadBytes(const TArray<uint8>& Data,FString& Error)
{
    Heritages.Reset(); StartAreas.Reset(); FCGReader R(Data);
    if(R.U32()!=0x0e000002)R.Ok=false; R.U32();
    uint32 N=R.Count(); for(uint32 I=0;I<N&&R.Ok;++I) { StartAreas.Add(R.String()); uint32 L=R.Count(); R.Skip(L*32); }
    R.U8(); N=R.Count();
    for(uint32 I=0;I<N&&R.Ok;++I)
    {
        uint32 Id=R.U32(); FACECGHeritage H; H.Name=R.String(); H.Icon=R.U32(); H.Setup=R.U32(); H.Environment=R.U32(); H.AttributeCredits=R.U32(); H.SkillCredits=R.U32();
        H.PrimaryStarts=R.Array(); H.SecondaryStarts=R.Array();
        uint32 C=R.Count(); for(uint32 J=0;J<C&&R.Ok;++J) { uint32 K=R.U32(); int32 A=R.U32(),B=R.U32(); H.SkillCosts.Add(K,FIntPoint(A,B)); }
        C=R.Count(); for(uint32 J=0;J<C&&R.Ok;++J) { FACECGProfession T; T.Name=R.String(); T.Icon=R.U32(); T.Title=R.U32(); for(auto& A:T.Attributes) A=R.U32(); T.Trained=R.Array(); T.Specialized=R.Array(); H.Professions.Add(MoveTemp(T)); }
        R.U8(); C=R.Count(); for(uint32 J=0;J<C&&R.Ok;++J)
        {
            uint32 SexId=R.U32(); FACECGSex S; S.Name=R.String(); S.Scale=R.U32(); S.Setup=R.U32(); S.Sound=R.U32(); S.Icon=R.U32(); S.BasePalette=R.U32(); S.SkinPalSet=R.U32(); S.Physics=R.U32(); S.Motion=R.U32(); S.Combat=R.U32(); S.Base=R.Desc(); S.HairColors=R.Array();
            uint32 K=R.Count(); for(uint32 L=0;L<K&&R.Ok;++L) { FACECGFace F; F.Icon=R.U32(); F.Bald=R.U8()!=0; F.AlternateSetup=R.U32(); F.Desc=R.Desc(); S.Hair.Add(MoveTemp(F)); }
            S.EyeColors=R.Array(); K=R.Count(); for(uint32 L=0;L<K&&R.Ok;++L) { FACECGFace F; F.Icon=R.U32(); F.BaldIcon=R.U32(); F.Desc=R.Desc(); F.BaldDesc=R.Desc(); S.Eyes.Add(MoveTemp(F)); }
            for(auto* Faces:{&S.Nose,&S.Mouth}) { K=R.Count(); for(uint32 L=0;L<K&&R.Ok;++L) { FACECGFace F; F.Icon=R.U32(); F.Desc=R.Desc(); Faces->Add(MoveTemp(F)); }}
            for(auto& Gear:S.Gear) { K=R.Count(); for(uint32 L=0;L<K&&R.Ok;++L) { FACECGGear G; G.Name=R.String(); G.ClothingTable=R.U32(); G.Weenie=R.U32(); Gear.Add(MoveTemp(G)); }}
            S.ClothingColors=R.Array(); H.Sexes.Add(SexId,MoveTemp(S));
        }
        Heritages.Add(Id,MoveTemp(H));
    }
    if(!R.Ok || R.P!=Data.Num() || Heritages.IsEmpty()) { Error=FString::Printf(TEXT("Invalid character creation DAT at byte %d of %d."),R.P,Data.Num()); Heritages.Reset(); return false; }
    return true;
}
bool FACECharacterCreation::Load(const FACEDatDatabase& Dat,FString& Error)
{
    Database=&Dat; TArray<uint8> B; if(!Dat.ReadFile(0x0e000002,B) || !LoadBytes(B,Error))return false;
    if(!Dat.ReadFile(0x0e000004,B)) {Error=TEXT("Missing retail skill table.");return false;}
    Skills.Reset(); FCGReader R(B); R.U32(); uint16 N=R.U16(); R.U16();
    for(int32 I=0;I<N&&R.Ok;++I) { uint32 Id=R.U32(); FACECGSkill S; S.Description=R.String(true); S.Name=R.String(true); S.Icon=R.U32(); S.Trained=R.U32(); S.Specialized=R.U32(); S.Category=R.U32(); S.Chargen=R.U32(); S.MinLevel=R.U32(); for(auto& F:S.Formula)F=R.U32(); R.Skip(24); Skills.Add(Id,MoveTemp(S)); }
    if(!R.Ok) {Error=TEXT("Invalid retail skill table.");return false;} return SelectHeritage(1);
}
bool FACECharacterCreation::LoadStrings(const FString& Directory)
{
    FACEDatDatabase Lang;TArray<uint8>B;if(!Lang.Open(FPaths::Combine(Directory,TEXT("client_local_English.dat")))||!Lang.ReadFile(0x23000002,B))return false;
    FCGReader R(B);R.U32();R.U32();R.U8();uint32 N=R.Count();Strings.Reset();
    for(uint32 I=0;I<N&&R.Ok;++I){uint32 Id=R.U32();for(int J=0;J<2;++J){uint16 C=R.U16();for(int K=0;K<C&&R.Ok;++K)R.Unicode();}uint32 C=R.Count(false);FString Text;for(uint32 K=0;K<C&&R.Ok;++K){auto S=R.Unicode();if(K==0)Text=S;}C=R.Count(false);R.Skip(C*4);R.U8();Text.ReplaceInline(TEXT("\\n"),TEXT("\n"));Text.ReplaceInline(TEXT("\\t"),TEXT("\t"));Strings.Add(Id,Text);}return R.Ok;
}
FString FACECharacterCreation::Text(const FString& Key) const
{
    uint32 Hash=0;for(TCHAR C:Key){Hash=uint8(C)+(Hash<<4);if(Hash&0xf0000000)Hash=(Hash^((Hash&0xf0000000)>>24))&0x0fffffff;}return Strings.FindRef(Hash);
}
bool FACECharacterCreation::SelectHeritage(uint32 Id)
{
    if(!Heritages.Contains(Id))return false;
    int32 Colors[4];for(int I=0;I<4;++I)Colors[I]=GearColors(I).IndexOfByKey(Selection.GearColor[I]);
    Selection.Heritage=Id;Selection.ClassId=MappedAsset(0x25000015,Id==12?0x10000090:Id==13?0x10000091:0x10000003);
    const auto H=Heritage();Selection.StartArea=H->PrimaryStarts.IsEmpty()?0:H->PrimaryStarts[FMath::RandRange(0,H->PrimaryStarts.Num()-1)];
    if(!H->Sexes.Contains(Selection.Sex))Selection.Sex=1;
    ConstrainAppearance(Colors);
    SelectProfession(Id>=12?1:FMath::Clamp(Selection.Profession,0,H->Professions.Num()-1));return true;
}
bool FACECharacterCreation::SelectSex(uint32 Id)
{
    if(!Heritage() || !Heritage()->Sexes.Contains(Id))return false;
    int32 Colors[4];for(int I=0;I<4;++I)Colors[I]=GearColors(I).IndexOfByKey(Selection.GearColor[I]);
    Selection.Sex=Id;ConstrainAppearance(Colors);return true;
}
void FACECharacterCreation::ConstrainAppearance(const int32 (&ColorIndices)[4])
{
    const auto S=Sex();if(!S)return;
    const auto Clamp=[](uint32& Index,int32 Count){Index=FMath::Min(Index,uint32(FMath::Max(0,Count-1)));};
    Clamp(Selection.Eyes,S->Eyes.Num());Clamp(Selection.Nose,S->Nose.Num());Clamp(Selection.Mouth,S->Mouth.Num());
    Clamp(Selection.HairStyle,S->Hair.Num());Clamp(Selection.HairColor,S->HairColors.Num());Clamp(Selection.EyeColor,S->EyeColors.Num());
    for(int I=0;I<4;++I)
    {
        if(I!=0||Selection.GearStyle[I]!=MAX_uint32)Clamp(Selection.GearStyle[I],S->Gear[I].Num());
        auto Colors=GearColors(I);Selection.GearColor[I]=Colors.IsEmpty()?0:Colors[FMath::Clamp(ColorIndices[I],0,Colors.Num()-1)];
    }
}
void FACECharacterCreation::SelectProfession(int32 Index)
{
    if(!Heritage() || !Heritage()->Professions.IsValidIndex(Index))return;
    Selection.Profession=Index; const auto& T=Heritage()->Professions[Index];
    for(int I=0;I<6;++I) {Selection.Attributes[I]=T.Attributes[I];Selection.Locked[I]=false;}
    ResetSkills();
    for(uint32 S:T.Trained) if(Selection.Skills.IsValidIndex(S))Selection.Skills[S]=2;
    for(uint32 S:T.Specialized) if(Selection.Skills.IsValidIndex(S))Selection.Skills[S]=3;
}
void FACECharacterCreation::ResetSkills()
{
    uint32 Max=0;for(const auto& S:Skills)Max=FMath::Max(Max,S.Key);Selection.Skills.Init(0,Max+1);
    for(const auto& S:Skills){auto C=SkillCost(S.Key);Selection.Skills[S.Key]=C.X==0?(C.Y==0?3:2):1;}
}
void FACECharacterCreation::RandomizeProfession()
{
    const auto H=Heritage();if(!H)return;
    if(Selection.Heritage>=12){SelectProfession(1);return;}
    TArray<int32> Choices;for(int I=1;I<H->Professions.Num();++I)if(I!=Selection.Profession)Choices.Add(I);
    if(!Choices.IsEmpty())SelectProfession(Choices[FMath::RandRange(0,Choices.Num()-1)]);
}
void FACECharacterCreation::RandomizeCharacter()
{
    const uint32 Slot=Selection.Slot;Selection=FACECGSelection();Selection.Slot=Slot;
    SelectHeritage(FMath::RandRange(1,4));SelectSex(FMath::RandRange(1,2));RandomizeAppearance();RandomizeProfession();
}
void FACECharacterCreation::RandomizeSkills()
{
    ResetSkills();
    for(int I=0;I<100&&SkillCredits()>0;++I)
    {
        const uint32 Id=FMath::RandRange(0,Selection.Skills.Num()-1),Level=Selection.Skills[Id];
        if(Level==2||(Level==1&&(I&1)))SetSkill(Id,Level+1);
    }
    for(int I=0;I<Selection.Skills.Num()&&SkillCredits()>0;++I)if(Selection.Skills[I]==1||Selection.Skills[I]==2)SetSkill(I,Selection.Skills[I]+1);
    FitProfession();
}
void FACECharacterCreation::FitProfession()
{
    const auto H=Heritage();if(!H)return;double Best=-1;int BestIndex=0;
    for(int I=1;I<H->Professions.Num();++I)
    {
        const auto& T=H->Professions[I];int Sum=0,Match=0,Trained=0,Specialized=0;
        for(int A=0;A<6;++A){Sum+=T.Attributes[A];Match+=FMath::Min(Selection.Attributes[A],T.Attributes[A]);}
        for(uint32 Id:T.Trained)if(Selection.Skills.IsValidIndex(Id)&&Selection.Skills[Id]>=2)++Trained;
        for(uint32 Id:T.Specialized)if(Selection.Skills.IsValidIndex(Id))Specialized+=Selection.Skills[Id]==3?2:Selection.Skills[Id]==2?1:0;
        const double Score=(Sum>0?FMath::Pow(double(Match)/Sum,2.5)/3:.25)+
            (T.Trained.IsEmpty()?.3:FMath::Pow(double(Trained)/T.Trained.Num(),3.)/3)+
            (T.Specialized.IsEmpty()?.45:FMath::Pow(double(Specialized)/(2*T.Specialized.Num()),3.5)/3);
        if(Score>Best){Best=Score;BestIndex=I;}
    }
    Selection.Profession=Best<.75?0:BestIndex;
}
int32 FACECharacterCreation::AttributeCredits() const { int32 C=Heritage()?Heritage()->AttributeCredits:0; for(int A:Selection.Attributes)C-=A; return C; }
FIntPoint FACECharacterCreation::SkillCost(uint32 Id) const { if(Heritage())if(auto C=Heritage()->SkillCosts.Find(Id))return *C; if(auto S=Skills.Find(Id))return FIntPoint(S->Trained,S->Specialized); return FIntPoint(-1,-1); }
int32 FACECharacterCreation::SkillCredits() const { int32 C=Heritage()?Heritage()->SkillCredits:0; for(int I=0;I<Selection.Skills.Num();++I) {auto Cost=SkillCost(I); if(Selection.Skills[I]==2)C-=Cost.X; if(Selection.Skills[I]==3)C-=Cost.Y;} return C; }
bool FACECharacterCreation::SetSkill(uint32 Id,uint32 Level)
{
    if(!Selection.Skills.IsValidIndex(Id) || !Skills.Contains(Id) || Level<1 || Level>3)return false;
    auto Cost=SkillCost(Id); if((Level==1&&Cost.X==0)||(Level==2&&(Cost.X<0||(Cost.X==0&&Cost.Y==0)))||(Level==3&&Cost.Y<0))return false;
    uint32 Old=Selection.Skills[Id]; Selection.Skills[Id]=Level; if(SkillCredits()<0){Selection.Skills[Id]=Old;return false;} FitProfession();return true;
}
void FACECharacterCreation::SetAttribute(int32 Index,int32 Value)
{
    if(Index<0 || Index>=6 || !Heritage() || Selection.Locked[Index])return;
    // A slider may borrow points from the other unlocked attributes, as CharGenState does.
    int32 Available=AttributeCredits()+Selection.Attributes[Index];
    for(int I=0;I<6;++I)if(I!=Index&&!Selection.Locked[I])Available+=Selection.Attributes[I]-10;
    Selection.Attributes[Index]=FMath::Clamp(Value,10,FMath::Min(100,Available));
    while(AttributeCredits()<0)
    {
        int I=BalanceStart;BalanceStart=(BalanceStart+1)%6;
        if(I!=Index&&!Selection.Locked[I]&&Selection.Attributes[I]>10)--Selection.Attributes[I];
    }
    FitProfession();
}
int32 FACECharacterCreation::SkillValue(uint32 Id) const
{
    auto S=Skills.Find(Id); if(!S||!Selection.Skills.IsValidIndex(Id)||Selection.Skills[Id]<S->MinLevel)return 0;
    const auto Attr=[&](uint32 A){static const int Map[]={-1,0,1,3,2,4,5};return A<7&&Map[A]>=0?Selection.Attributes[Map[A]]:0;};
    uint32 Z=S->Formula[3]; return (Z?FMath::RoundToInt(double(S->Formula[0]+S->Formula[1]*Attr(S->Formula[4])+S->Formula[2]*Attr(S->Formula[5]))/Z):0)+(Selection.Skills[Id]==3?10:Selection.Skills[Id]==2?5:0);
}
uint32 FACECharacterCreation::Palette(uint32 Set,double Shade) const
{
    TArray<uint8>B; if(!Database||!Database->ReadFile(Set,B))return 0; FCGReader R(B);R.U32();uint32 N=R.Count(false); if(!N)return 0;
    int32 Index=FMath::Clamp(int32((N-.000001)*FMath::Clamp(Shade,0.,1.)),0,int32(N)-1); R.Skip(Index*4); return R.U32();
}
bool FACECharacterCreation::ApplyClothing(uint32 Table,uint32 Setup,uint32 Color,double Shade,FACEObjDesc& Desc,TArray<uint32>* Colors) const
{
    if(Table==0)return true; TArray<uint8>B; if(!Database||!Database->ReadFile(Table,B))return false; FCGReader R(B);R.U32(); uint16 N=R.U16();R.U16();
    FACEObjDesc Added;
    for(int I=0;I<N&&R.Ok;++I) {uint32 Key=R.U32(),C=R.Count(false);for(uint32 J=0;J<C&&R.Ok;++J){uint32 Part=R.U32(),Model=R.U32(); if(Key==Setup){FACEObjDescAnimPartChange A;A.PartIndex=Part;A.PartId=Model;Added.AnimPartChanges.Add(A);}uint32 T=R.Count(false);for(uint32 K=0;K<T&&R.Ok;++K){uint32 Old=R.U32(),New=R.U32();if(Key==Setup){FACEObjDescTextureChange A;A.PartIndex=Part;A.OldTexture=Old;A.NewTexture=New;Added.TextureChanges.Add(A);}}}}
    N=R.U16();R.U16();for(int I=0;I<N&&R.Ok;++I){uint32 Key=R.U32();R.U32();if(Colors)Colors->Add(Key);uint32 C=R.Count(false);for(uint32 J=0;J<C&&R.Ok;++J){TArray<FIntPoint> Ranges;uint32 T=R.Count(false);for(uint32 K=0;K<T&&R.Ok;++K){int32 A=R.U32(),L=R.U32();Ranges.Add(FIntPoint(A,L));}uint32 Set=R.U32();if(!ClothingPalSets.FindOrAdd(Table).Contains(Key))ClothingPalSets[Table].Add(Key,Set);if(Key==Color&&!Colors){uint32 Pal=Palette(Set,Shade);for(auto Range:Ranges){FACEObjDescSubPalette A;A.SubPaletteId=Pal;A.Offset=Range.X;A.NumColors=Range.Y;Added.SubPalettes.Add(A);}}}}
    if(R.Ok&&!Colors)Merge(Desc,Added);return R.Ok;
}
TArray<uint32> FACECharacterCreation::GearColors(int32 Slot) const
{
    TArray<uint32> Found,Result; const auto S=Sex();if(!S||Slot<0||Slot>=4||!S->Gear[Slot].IsValidIndex(Selection.GearStyle[Slot]))return Result;
    uint32 Table=S->Gear[Slot][Selection.GearStyle[Slot]].ClothingTable;
    if(auto Cached=ClothingColorCache.Find(Table))Found=*Cached;
    else {FACEObjDesc D;ApplyClothing(Table,S->Setup,0,0,D,&Found);ClothingColorCache.Add(Table,Found);}
    for(uint32 C:S->ClothingColors)if(Found.Contains(C))Result.Add(C);return Result;
}
bool FACECharacterCreation::BuildAppearance(FACEWorldObject& Object,bool bHideHeadgear) const
{
    auto S=Sex();if(!S)return false;Object=FACEWorldObject();Object.Guid=1;Object.SetupId=S->Setup;Object.Scale=float(S->Scale)*.01f;Object.MotionTableId=S->Motion;Object.SoundTableId=S->Sound;Object.Appearance=S->Base;Object.Appearance.PaletteBaseId=S->BasePalette;
    bool Bald=false;if(S->Hair.IsValidIndex(Selection.HairStyle)){auto& H=S->Hair[Selection.HairStyle];Merge(Object.Appearance,H.Desc);Bald=H.Bald;if(H.AlternateSetup)Object.SetupId=H.AlternateSetup;}
    // Retail composes hat, trousers, shirt, footwear before the facial strips.
    // A shirt/robe covering the trousers must win overlapping part replacements.
    for(int I:{0,2,1,3})if(!(I==0&&bHideHeadgear)&&S->Gear[I].IsValidIndex(Selection.GearStyle[I]))ApplyClothing(S->Gear[I][Selection.GearStyle[I]].ClothingTable,Object.SetupId,Selection.GearColor[I],Selection.GearHue[I],Object.Appearance);
    if(S->Eyes.IsValidIndex(Selection.Eyes))Merge(Object.Appearance,Bald?S->Eyes[Selection.Eyes].BaldDesc:S->Eyes[Selection.Eyes].Desc);
    if(S->Nose.IsValidIndex(Selection.Nose))Merge(Object.Appearance,S->Nose[Selection.Nose].Desc);
    if(S->Mouth.IsValidIndex(Selection.Mouth))Merge(Object.Appearance,S->Mouth[Selection.Mouth].Desc);
    auto Pal=[&](uint32 Id,int32 Offset,int32 Count){if(Id){FACEObjDescSubPalette P;P.SubPaletteId=Id;P.Offset=Offset;P.NumColors=Count;Object.Appearance.SubPalettes.Add(P);}};
    Pal(Palette(S->SkinPalSet,Selection.SkinHue),0,192);if(S->HairColors.IsValidIndex(Selection.HairColor))Pal(Palette(S->HairColors[Selection.HairColor],Selection.HairHue),192,64);if(S->EyeColors.IsValidIndex(Selection.EyeColor))Pal(S->EyeColors[Selection.EyeColor],256,64);
    return true;
}
void FACECharacterCreation::RandomizeAppearance()
{
    auto S=Sex();if(!S)return;auto Pick=[](int N)->uint32{return N>0?FMath::RandRange(0,N-1):0;};Selection.Eyes=Pick(S->Eyes.Num());Selection.Nose=Pick(S->Nose.Num());Selection.Mouth=Pick(S->Mouth.Num());Selection.HairStyle=Pick(S->Hair.Num());Selection.EyeColor=Pick(S->EyeColors.Num());Selection.HairColor=Pick(S->HairColors.Num());Selection.HairHue=FMath::FRand();Selection.SkinHue=FMath::FRand();for(int I=0;I<4;++I){Selection.GearStyle[I]=I==0?Pick(S->Gear[I].Num()+1)-1:Pick(S->Gear[I].Num());auto C=GearColors(I);Selection.GearColor[I]=C.IsEmpty()?0:C[Pick(C.Num())];Selection.GearHue[I]=FMath::FRand();}
}
bool FACECharacterCreation::IsNameCharacter(TCHAR C)
{
    return (C>='a'&&C<='z')||(C>='A'&&C<='Z')||C=='\''||C==' '||C=='-';
}
FString FACECharacterCreation::FormatName(const FString& Name)
{
    // ACCharGenData::FormatName: filter punctuation in its original context, then
    // apply the retail casing rules (preserve case after separators/name prefixes).
    FString Input=Name;Input.TrimEndInline();Input=Input.Left(32);FString Result;
    const auto Letter=[](TCHAR C){return (C>='a'&&C<='z')||(C>='A'&&C<='Z')||C>=128;};
    for(int I=0;I<Input.Len();++I)
    {
        const TCHAR C=Input[I],Before=I?Input[I-1]:0,After=I+1<Input.Len()?Input[I+1]:0;
        bool Keep=IsNameCharacter(C);
        if(C==' ')Keep=Before&&Before!=' '&&After;
        if(C=='\'')Keep=Before&&Before!='\''&&(Letter(Before)||Letter(After));
        if(C=='-')Keep=Before&&Before!='-'&&Letter(Before)&&(Letter(After)||After=='-');
        if(Keep)Result.AppendChar(C);
    }
    TArray<int32> Cases;Cases.Init(1,Result.Len());if(Cases.IsEmpty())return Result;Cases[0]=0;
    for(int I=0;I<Result.Len();++I)if(I==0||Result[I-1]==' '||Result[I-1]=='-'||Result[I-1]=='\'')
    {
        if(I>0)Cases[I]=2;
        for(const TCHAR* Prefix:{TEXT("De"),TEXT("Di"),TEXT("Du"),TEXT("Fitz"),TEXT("Le"),TEXT("La"),TEXT("Mac"),TEXT("Mc"),TEXT("Von"),TEXT("Van")})
        {
            const int N=FCString::Strlen(Prefix);if(I+N<Result.Len()&&Result.Mid(I,N).Equals(Prefix,ESearchCase::IgnoreCase))Cases[I+N]=2;
        }
    }
    for(int I=0;I<Result.Len();)
    {
        if(!Letter(Result[I])){++I;continue;}
        const int Begin=I;bool Roman=true;
        while(I<Result.Len()&&Letter(Result[I])){const TCHAR C=Result[I++];Roman&=C=='I'||C=='V'||C=='X';}
        if(Roman)for(int J=Begin;J<I;++J)Cases[J]=0;
    }
    for(int I=0;I<Result.Len();++I)
    {
        if(Cases[I]==0&&Result[I]>='a'&&Result[I]<='z')Result[I]-=32;
        if(Cases[I]==1&&Result[I]>='A'&&Result[I]<='Z')Result[I]+=32;
    }
    return Result;
}
bool FACECharacterCreation::Validate(FString& Error) const
{
    auto H=Heritage();auto S=Sex();if(!H||!S){Error=TEXT("Choose a heritage and gender.");return false;}
    if(Selection.Name.TrimStartAndEnd().IsEmpty()||Selection.Name.Len()>32){Error=TEXT("Enter a character name of 1 to 32 characters.");return false;}
    for(TCHAR C:Selection.Name)if(!ACEWindows1252::IsPrintable(C)){Error=TEXT("The name contains unsupported characters.");return false;}
    auto ValidHue=[](double V){return FMath::IsFinite(V)&&V>=0&&V<=1;};
    if(!ValidHue(Selection.SkinHue)||!ValidHue(Selection.HairHue)){Error=TEXT("Invalid appearance shade.");return false;}
    for(double V:Selection.GearHue)if(!ValidHue(V)){Error=TEXT("Invalid clothing shade.");return false;}
    uint32 MaxSkill=0;for(const auto& Entry:Skills)MaxSkill=FMath::Max(MaxSkill,Entry.Key);
    if(Selection.Skills.Num()!=int32(MaxSkill+1)){Error=TEXT("Invalid skill list.");return false;}
    if(AttributeCredits()<0||SkillCredits()<0){Error=TEXT("You have spent more credits than are available.");return false;}
    for(int A:Selection.Attributes)if(A<10||A>100){Error=TEXT("Starting attributes must be between 10 and 100.");return false;}
    if(!H->PrimaryStarts.Contains(Selection.StartArea)&&!H->SecondaryStarts.Contains(Selection.StartArea)){Error=TEXT("Choose a starting town available to your heritage.");return false;}
    if(!H->Professions.IsValidIndex(Selection.Profession)){Error=TEXT("Invalid profession.");return false;}
    auto Valid=[](uint32 I,int32 N){return N?I<uint32(N):I==0;};
    if(!Valid(Selection.Eyes,S->Eyes.Num())||!Valid(Selection.Nose,S->Nose.Num())||!Valid(Selection.Mouth,S->Mouth.Num())||!Valid(Selection.HairStyle,S->Hair.Num())||!Valid(Selection.HairColor,S->HairColors.Num())||!Valid(Selection.EyeColor,S->EyeColors.Num())){Error=TEXT("Invalid appearance selection.");return false;}
    for(int I=0;I<4;++I){auto C=GearColors(I);if(!(I==0&&Selection.GearStyle[I]==MAX_uint32)&&(!Valid(Selection.GearStyle[I],S->Gear[I].Num())||(!C.IsEmpty()&&!C.Contains(Selection.GearColor[I])))){Error=TEXT("Invalid clothing selection.");return false;}}
    for(int I=0;I<Selection.Skills.Num();++I){auto C=SkillCost(I);uint32 L=Selection.Skills[I];if((Skills.Contains(I)&&L<1)||(!Skills.Contains(I)&&L!=0)||L>3||(L==2&&C.X<0)||(L==3&&C.Y<0)||(Skills.Contains(I)&&C.X==0&&L<2)){Error=TEXT("Invalid skill selection.");return false;}}
    Error.Reset();return true;
}
void FACECGSelection::Write(FACEBinaryWriter& W) const
{
    W.WriteUInt32(1);W.WriteUInt32(Heritage);W.WriteUInt32(Sex);
    W.WriteUInt32(Eyes);W.WriteUInt32(Nose);W.WriteUInt32(Mouth);W.WriteUInt32(HairColor);W.WriteUInt32(EyeColor);W.WriteUInt32(HairStyle);
    for(int I=0;I<4;++I){W.WriteUInt32(GearStyle[I]);W.WriteUInt32(GearColor[I]);}
    W.WriteDouble(SkinHue);W.WriteDouble(HairHue);for(double H:GearHue)W.WriteDouble(H);
    W.WriteInt32(Profession);for(int A:Attributes)W.WriteUInt32(A);W.WriteUInt32(Slot);W.WriteUInt32(ClassId);
    W.WriteUInt32(Skills.Num());for(uint32 L:Skills)W.WriteUInt32(L);W.WriteString16L(Name);W.WriteUInt32(StartArea);W.WriteUInt32(0);W.WriteUInt32(0);
}

uint32 FACECharacterCreation::MappedAsset(uint32 Mapper,uint32 Key) const
{
    if(!AssetMaps.Contains(Mapper))
    {
        TArray<uint8>B;if(!Database||!Database->ReadFile(Mapper,B))return 0;
        FCGReader R(B);R.U32();R.U8();uint32 N=R.Count();TMap<uint32,uint32> Map;
        for(uint32 I=0;I<N&&R.Ok;++I){uint32 K=R.U32(),V=R.U32();Map.Add(K,V);}
        if(!R.Ok)return 0;AssetMaps.Add(Mapper,MoveTemp(Map));
    }
    return AssetMaps[Mapper].FindRef(Key);
}
FColor FACECharacterCreation::SamplePaletteSet(uint32 Id,int32 Sample,bool bSet) const
{
    uint64 Key=(uint64(Id)<<32)|uint32(Sample);if(auto C=SampleCache.Find(Key))return *C;
    TArray<uint32> Palettes;TArray<uint8>B;
    if(bSet){if(!Database||!Database->ReadFile(Id,B))return FColor::Black;FCGReader R(B);R.U32();uint32 N=R.Count(false);for(uint32 I=0;I<N&&R.Ok;++I)Palettes.Add(R.U32());}
    else Palettes.Add(Id);
    uint32 Red=0,Green=0,Blue=0,Count=0;
    for(uint32 Pal:Palettes)if(Database&&Database->ReadFile(Pal,B)){FCGReader R(B);R.U32();uint32 N=R.Count(false);if(Sample<int32(N)){R.Skip(Sample*4);uint32 C=R.U32();if(R.Ok){Red+=(C>>16)&255;Green+=(C>>8)&255;Blue+=C&255;++Count;}}}
    FColor C=Count?FColor(Red/Count,Green/Count,Blue/Count):FColor::Black;SampleCache.Add(Key,C);return C;
}
TArray<FColor> FACECharacterCreation::ColorSamples(int32 Field) const
{
    TArray<FColor> Result;auto S=Sex();if(!S)return Result;
    if(Field==0)for(uint32 Set:S->HairColors)Result.Add(SamplePaletteSet(Set,208,true));
    else if(Field==1)for(uint32 Pal:S->EyeColors)Result.Add(SamplePaletteSet(Pal,259,false));
    else if(Field>=2&&Field<=4)Result.Add(SamplePaletteSet(S->SkinPalSet,176,true));
    else if(Field>=5&&Field<=8){int Slot=Field-5;auto Colors=GearColors(Slot);if(S->Gear[Slot].IsValidIndex(Selection.GearStyle[Slot])){uint32 Table=S->Gear[Slot][Selection.GearStyle[Slot]].ClothingTable;for(uint32 C:Colors)Result.Add(SamplePaletteSet(ClothingPalSets.FindRef(Table).FindRef(C),1312,true));}}
    return Result;
}
