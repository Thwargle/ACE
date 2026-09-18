#pragma once
#include "CoreMinimal.h"
#include "ACETypes.h"

class FACEDatDatabase;
class FACEBinaryWriter;

/** Retail 0E000002 records. Indices are preserved: they are also the F656 wire values. */
struct FACECGFace { uint32 Icon=0, BaldIcon=0, AlternateSetup=0; bool Bald=false; FACEObjDesc Desc, BaldDesc; };
struct FACECGGear { FString Name; uint32 ClothingTable=0, Weenie=0; };
struct FACECGSex
{
    FString Name;
    uint32 Scale=100, Setup=0, Sound=0, Icon=0, BasePalette=0, SkinPalSet=0, Physics=0, Motion=0, Combat=0;
    FACEObjDesc Base;
    TArray<uint32> HairColors, EyeColors, ClothingColors;
    TArray<FACECGFace> Hair, Eyes, Nose, Mouth;
    TArray<FACECGGear> Gear[4];
};
struct FACECGProfession { FString Name; uint32 Icon=0, Title=0; int32 Attributes[6]={10,10,10,10,10,10}; TArray<uint32> Trained, Specialized; };
struct FACECGHeritage
{
    FString Name; uint32 Icon=0, Setup=0, Environment=0; int32 AttributeCredits=330, SkillCredits=52;
    TArray<uint32> PrimaryStarts, SecondaryStarts;
    TMap<uint32,FIntPoint> SkillCosts;
    TArray<FACECGProfession> Professions;
    TMap<uint32,FACECGSex> Sexes;
};
struct FACECGSkill
{
    FString Name, Description; uint32 Icon=0, Category=0, Chargen=0, MinLevel=1;
    int32 Trained=-1, Specialized=-1; uint32 Formula[6]={};
};
struct FACECGSelection
{
    uint32 Heritage=1, Sex=1, Eyes=0, Nose=0, Mouth=0, HairColor=0, EyeColor=0, HairStyle=0;
    uint32 GearStyle[4]={MAX_uint32,0,0,0}, GearColor[4]={};
    double SkinHue=.5, HairHue=.5, GearHue[4]={.5,.5,.5,.5};
    int32 Profession=0, Attributes[6]={10,10,10,10,10,10}; // wire order: STR END COORD QUICK FOCUS SELF
    bool Locked[6]={};
    TArray<uint32> Skills;
    FString Name;
    uint32 Slot=0, ClassId=0, StartArea=0;
    void Write(FACEBinaryWriter& Writer) const;
};
class ACECLIENT_API FACECharacterCreation
{
public:
    bool Load(const FACEDatDatabase& Dat, FString& Error);
    bool LoadBytes(const TArray<uint8>& Data, FString& Error);
    TMap<uint32,FACECGHeritage> Heritages;
    TMap<uint32,FACECGSkill> Skills;
    TArray<FString> StartAreas;
    TMap<uint32,FString> Strings;
    bool LoadStrings(const FString& DatDirectory);
    FString Text(const FString& Key) const;
    FACECGSelection Selection;
    const FACECGHeritage* Heritage() const { return Heritages.Find(Selection.Heritage); }
    const FACECGSex* Sex() const { auto H=Heritage(); return H?H->Sexes.Find(Selection.Sex):nullptr; }
    bool SelectHeritage(uint32 Id);
    bool SelectSex(uint32 Id);
    void SelectProfession(int32 Index);
    int32 AttributeCredits() const;
    int32 SkillCredits() const;
    FIntPoint SkillCost(uint32 Id) const;
    bool SetSkill(uint32 Id, uint32 Level);
    void SetAttribute(int32 Index, int32 Value);
    int32 SkillValue(uint32 Id) const;
    TArray<uint32> GearColors(int32 Slot) const;
    bool Validate(FString& Error) const;
    bool BuildAppearance(FACEWorldObject& Object, bool bHideHeadgear=false) const;
    uint32 MappedAsset(uint32 Mapper,uint32 Key) const;
    TArray<FColor> ColorSamples(int32 Field) const;
    void RandomizeAppearance();
    void RandomizeCharacter();
    void RandomizeProfession();
    void RandomizeSkills();
    void FitProfession();
    static FString FormatName(const FString& Name);
    static bool IsNameCharacter(TCHAR C);
private:
    friend class FACERetailCharacterCreationTest;
    friend class FACEAppearancePlacementTest;
    friend class FACERetailWorldEntryTest;
    void ResetSkills();
    void ConstrainAppearance(const int32 (&ColorIndices)[4]);
    const FACEDatDatabase* Database=nullptr;
    int32 BalanceStart=0;
    mutable TMap<uint32,TArray<uint32>> ClothingColorCache;
    mutable TMap<uint32,TMap<uint32,uint32>> ClothingPalSets, AssetMaps;
    mutable TMap<uint64,FColor> SampleCache;
    FColor SamplePaletteSet(uint32 Id,int32 Sample,bool bSet) const;
    uint32 Palette(uint32 Set, double Shade) const;
    bool ApplyClothing(uint32 Table, uint32 Setup, uint32 Color, double Shade, FACEObjDesc& Desc, TArray<uint32>* Colors=nullptr) const;
};
