#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "ACECharacterCreation.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "Dat/ACESetupMeshBuilder.h"
#include "Dat/ACEDatCursor.h"
#include "Protocol/ACEObjectCreateParser.h"

namespace
{
void WriteKnown(FACEBinaryWriter& W, uint32 Id)
{
    Id &= 0x00FFFFFF;
    if (Id < 0x8000) W.WriteUInt16(Id);
    else { W.WriteUInt16(0x8000 | (Id >> 16)); W.WriteUInt16(Id & 0xFFFF); }
}

// Deliberately serialize unnormalized lists, like a server composing equipment
// bottom-to-top. The production parser must apply retail ObjDesc insertion rules.
void WriteDesc(FACEBinaryWriter& W, const FACEObjDesc& D)
{
    W.WriteUInt8(0x11); W.WriteUInt8(D.SubPalettes.Num());
    W.WriteUInt8(D.TextureChanges.Num()); W.WriteUInt8(D.AnimPartChanges.Num());
    if (!D.SubPalettes.IsEmpty()) WriteKnown(W, D.PaletteBaseId);
    for (const auto& P : D.SubPalettes)
    { WriteKnown(W,P.SubPaletteId); W.WriteUInt8(P.Offset/8); W.WriteUInt8(P.NumColors/8); }
    for (const auto& T : D.TextureChanges)
    { W.WriteUInt8(T.PartIndex); WriteKnown(W,T.OldTexture); WriteKnown(W,T.NewTexture); }
    for (const auto& P : D.AnimPartChanges)
    { W.WriteUInt8(P.PartIndex); WriteKnown(W,P.PartId); }
    W.Align();
}

FACEObjDesc Layers(const FACEObjDesc& Under, const FACEObjDesc& Over)
{
    FACEObjDesc D=Under;
    D.SubPalettes.Append(Over.SubPalettes);
    D.TextureChanges.Append(Over.TextureChanges);
    D.AnimPartChanges.Append(Over.AnimPartChanges);
    return D;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACETailoringLayersTest,"ACE.RetailParity.TailoringLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACETailoringLayersTest::RunTest(const FString&)
{
    auto Decode=[&](const FACEObjDesc& Wire)
    {
        FACEBinaryWriter W; WriteDesc(W,Wire); FACEBinaryReader R(W.GetData()); FACEObjDesc D;
        TestTrue(TEXT("Retail ObjDesc wire fixture parses"),FACEObjectCreateParser::ParseModelData(R,D));
        TestEqual(TEXT("Duplicate removal still consumes the entire wire description"),R.Tell(),W.Num());
        return D;
    };
    auto Pal=[](int32 Id,int32 Offset,int32 Count)
    { FACEObjDescSubPalette P; P.SubPaletteId=Id; P.Offset=Offset; P.NumColors=Count; return P; };

    // Reference: ObjDesc.cpp 005AB970; Subpalette.cpp 005AD830/005AD870.
    // These are NOT ordinary last-write-wins intervals: a full override also
    // blocks partial ranges that follow it. Identical ranges move to the end.
    FACEObjDesc Wire; Wire.PaletteBaseId=0x0400007E;
    Wire.SubPalettes={Pal(0x0400008E,0,192),Pal(0x0400007E,128,128),Pal(0x0400008E,0,192)};
    FACEObjDesc D=Decode(Wire);
    TestEqual(TEXT("Repeated range replaces earlier range"),D.SubPalettes.Num(),2);
    if (D.SubPalettes.Num()==2)
    {
        TestEqual(TEXT("Different overlapping range retains its position"),D.SubPalettes[0].Offset,128);
        TestEqual(TEXT("Replacement range is applied last"),D.SubPalettes[1].Offset,0);
    }
    Wire.SubPalettes={Pal(0x0400007E,0,192),Pal(0x0400008E,0,2048),Pal(0x0400007E,128,128)};
    D=Decode(Wire);
    TestEqual(TEXT("Complete palette replaces earlier dyes and blocks later partial dyes"),D.SubPalettes.Num(),1);
    if (D.SubPalettes.Num()==1) TestEqual(TEXT("Complete palette stays authoritative"),D.SubPalettes[0].SubPaletteId,0x0400008E);
    Wire.SubPalettes.Add(Pal(0x0400007E,0,2048));
    auto Replaced=Decode(Wire);
    TestEqual(TEXT("A second complete palette replaces the first"),Replaced.SubPalettes.Num(),1);
    if (Replaced.SubPalettes.Num()==1) TestEqual(TEXT("Latest complete palette wins"),Replaced.SubPalettes[0].SubPaletteId,0x0400007E);

    // A texture mapping is scoped to the original texture AND the body part.
    // A -> B followed by B -> C does not turn original A into C.
    FACEObjDesc Mappings;
    auto Tex=[&](uint8 Part,int32 Old,int32 New)
    { FACEObjDescTextureChange T;T.PartIndex=Part;T.OldTexture=Old;T.NewTexture=New;Mappings.TextureChanges.Add(T); };
    Tex(0,0x05000001,0x05000002); Tex(1,0x05000001,0x05000003);
    Tex(0,0x05000002,0x05000004); Tex(0,0x05000001,0x05000005);
    auto Mapped=Decode(Mappings);
    TestEqual(TEXT("Texture replacements deduplicate only matching part and original texture"),Mapped.TextureChanges.Num(),3);
    if (Mapped.TextureChanges.Num()==3)
    {
        TestEqual(TEXT("Other part remains independent"),Mapped.TextureChanges[0].PartIndex,uint8(1));
        TestEqual(TEXT("Replacement is retained at the end"),Mapped.TextureChanges[2].NewTexture,0x05000005);
    }

    FString Directory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    FACEDatDatabase Portal,HighRes;
    if (!TestTrue(TEXT("Open actual retail clothing DAT"),Portal.Open(FPaths::Combine(Directory,TEXT("client_portal.dat"))))) return false;
    HighRes.Open(FPaths::Combine(Directory,TEXT("client_highres.dat")));
    FACEDatTextureResolver Textures(&Portal,&HighRes);
    FACESetupMeshBuilder Builder(&Portal,&HighRes,&Textures);
    TArray<uint8> Blob; Portal.ReadFile(0x0400008E,Blob);
    FACEDatCursor PalCursor(Blob); FACEDatPalette Full;
    if (!TestTrue(TEXT("Read independent expected full palette"),ACEDatUnpack::UnpackPalette(PalCursor,Full))) return false;
    TArray<uint32> ActualColors;
    TestTrue(TEXT("Decoded layered palette resolves"),Textures.BuildWorkingPalette(D,ActualColors));
    TestTrue(TEXT("Every resulting color matches the authoritative full palette"),ActualColors==Full.Colors);

    FACECharacterCreation Creation; FString Error;
    if (!TestTrue(TEXT("Load retail clothing tables"),Creation.Load(Portal,Error))) return false;
    // Standard ACE Asenala (49518): Empyrean / Geomancer, PinkPurple template.
    FACEObjDesc Asenala; Asenala.PaletteBaseId=0x0400007E;
    TestTrue(TEXT("Asenala's actual DAT clothing resolves"),Creation.ApplyClothing(0x100007BD,0x02001A10,82,0,Asenala));
    FACEBuiltSetupMesh AsenalaMesh;
    TestTrue(TEXT("Asenala builds with her server appearance"),Builder.BuildSetupWithAppearance(0x02001A10,Asenala,100.f,AsenalaMesh));
    int32 Recolored=0;
    if (AsenalaMesh.Parts.IsValidIndex(16))
    {
        const auto& Head=AsenalaMesh.Parts[16];
        AddInfo(FString::Printf(TEXT("Asenala head %08X palette %08X"),Head.GfxObjId,Head.OriginalPaletteId));
        for (const auto& Section:Head.Sections)
        {
            FACEDatDecodedSurface Old,Fixed;
            Textures.ResolveSurfaceWithAppearance(Section.SurfaceId,16,&Asenala,Old);
            Textures.ResolveSurfaceWithAppearance(Section.SurfaceId,16,&Asenala,Fixed,Head.OriginalPaletteId);
            if (Old.Pixels!=Fixed.Pixels) ++Recolored;
            if (Section.SurfaceId==0x080009D1)
            {
                for (int32 Pass=0;Pass<2;++Pass)
                {
                    const auto& Image=Pass?Fixed:Old;TArray64<uint8> Png;
                    FImageUtils::PNGCompressImageArray(Image.Width,Image.Height,Image.Pixels,Png);
                    FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Asenala-head-%s.png"),Pass?TEXT("retail-palette"):TEXT("old"))));
                }
            }
            AddInfo(FString::Printf(TEXT("Asenala head surface %08X palette %08X changed=%d"),Section.SurfaceId,Textures.GetOriginalPaletteId(Section.SurfaceId),Old.Pixels!=Fixed.Pixels));
        }
    }
    TestTrue(TEXT("Asenala's secondary headgear surface receives the part palette"),Recolored>0);
    Creation.SelectHeritage(1); Creation.SelectSex(1);
    auto& Sex=Creation.Heritages[1].Sexes[1];
    // Local preview composition must preserve the creature palette just as
    // ObjDesc::operator+= does, even if a facial strip contains a different DID.
    if (!Sex.Eyes.IsEmpty())
    {
        Creation.Selection.Eyes=0;
        Sex.Eyes[0].Desc.PaletteBaseId=Sex.Eyes[0].BaldDesc.PaletteBaseId=0x0400008E;
        FACEWorldObject Preview;
        TestTrue(TEXT("Local appearance composes"),Creation.BuildAppearance(Preview));
        TestEqual(TEXT("Merging a strip preserves the character's base palette"),Preview.Appearance.PaletteBaseId,int32(Sex.BasePalette));
    }
    FACEObjDesc PreviewDyes=D;
    TestTrue(TEXT("Local clothing composition accepts full palette override"),Creation.ApplyClothing(0x100007E2,0x02000001,61,1,PreviewDyes));
    TestEqual(TEXT("Local preview applies the same full-palette precedence as wire descriptions"),PreviewDyes.SubPalettes.Num(),1);
    constexpr uint32 SetupId=0x02000001;
    FACEObjDesc Robe,Hauberk; Robe.PaletteBaseId=Hauberk.PaletteBaseId=0x0400007E;
    TestTrue(TEXT("Decode Hoary Mattekar Over-robe"),Creation.ApplyClothing(0x100007E2,SetupId,61,1,Robe));
    TestTrue(TEXT("Decode Pathwarden Scale Hauberk"),Creation.ApplyClothing(0x100000A6,SetupId,0,0,Hauberk));
    int32 Overlap=0;
    for (const auto& P:Robe.AnimPartChanges)
        if (Hauberk.AnimPartChanges.ContainsByPredicate([&](const auto& H){return H.PartIndex==P.PartIndex && H.PartId!=P.PartId;})) ++Overlap;
    TestTrue(TEXT("Real garment fixture covers the same body parts with different models"),Overlap>0);
    Portal.ReadFile(SetupId,Blob); FACEDatCursor SetupCursor(Blob); FACEDatSetupModel Setup;
    if (!ACEDatUnpack::UnpackSetupModel(SetupCursor,Setup)) return false;

    // Exercise actual F745/F625 replication, not a client-side priority sort.
    // Reverse the server order to model a top/bottom tool, then remove garments.
    FACESession Session; constexpr uint32 Guid=0x70011234;
    auto SendAppearance=[&](const FACEObjDesc& Look,uint16 Sequence)
    {
        FACEBinaryWriter W;
        W.WriteUInt32(Sequence==1 ? ACEOpcode::ObjectCreate : ACEOpcode::ObjDescEvent); W.WriteUInt32(Guid);
        WriteDesc(W,Look);
        if (Sequence==1)
        {
            W.WriteUInt32(1); W.WriteUInt32(0); W.WriteUInt32(SetupId);
            for (int32 I=0;I<ACEPhysicsTimeStamp::Count;++I) W.WriteUInt16(1);
            W.Align(); W.WriteUInt32(0); W.WriteString16L(TEXT("Tailoring layer fixture"));
            WriteKnown(W,1); WriteKnown(W,0x06000001); W.WriteUInt32(ACEItemType::Creature); W.WriteUInt32(0); W.Align();
        }
        else { W.WriteUInt16(1); W.WriteUInt16(Sequence); }
        Session.HandleGameMessage(W.GetData());
    };
    FACEObjDesc Empty;
    uint16 Sequence=0;
    uint64 LastHash=0;
    for (const auto& Look : {Layers(Robe,Hauberk),Layers(Hauberk,Robe),Hauberk,Empty})
    {
        SendAppearance(Look,++Sequence);
        const auto* Object=Session.GetWorldObjects().Find(Guid);
        if (!TestNotNull(TEXT("Appearance reaches the replicated character"),Object)) return false;
        TestTrue(TEXT("Changing layers invalidates the appearance cache"),Object->Appearance.GetContentHash()!=LastHash);
        LastHash=Object->Appearance.GetContentHash();
        FACEBuiltSetupMesh Mesh;
        TestTrue(TEXT("Layered character builds through the shared world/paperdoll mesh builder"),
            Builder.BuildSetupWithAppearance(SetupId,Object->Appearance,100.f,Mesh));
        TestEqual(TEXT("Tailoring replaces parts rather than stacking duplicate meshes"),Mesh.Parts.Num(),Setup.Parts.Num());
        TArray<uint32> Expected=Setup.Parts;
        for (const auto& P:Look.AnimPartChanges) if (Expected.IsValidIndex(P.PartIndex)) Expected[P.PartIndex]=P.PartId;
        for (int32 I=0;I<FMath::Min(Expected.Num(),Mesh.Parts.Num());++I)
            TestEqual(TEXT("Final visible part follows server layer order, including uncovered defaults"),Mesh.Parts[I].GfxObjId,Expected[I]);
        TSet<uint8> Unique;
        for (const auto& P:Object->Appearance.AnimPartChanges)
        { TestFalse(TEXT("No duplicate body-part override survives parsing"),Unique.Contains(P.PartIndex)); Unique.Add(P.PartIndex); }
    }
    return true;
}
#endif
