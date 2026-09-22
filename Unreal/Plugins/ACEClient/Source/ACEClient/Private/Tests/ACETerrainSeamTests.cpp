#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACETerrainSeamTest, "ACE.RetailParity.TerrainSeams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACETerrainSeamTest::RunTest(const FString& Parameters)
{
    FACEDatDatabase Portal, Cell, HighRes;
    FString DatDir = TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), DatDir);
    if (!TestTrue(TEXT("Open portal DAT"), Portal.Open(DatDir / TEXT("client_portal.dat"))) ||
        !TestTrue(TEXT("Open cell DAT"), Cell.Open(DatDir / TEXT("client_cell_1.dat"))) ||
        !TestTrue(TEXT("Open high-resolution DAT"), HighRes.Open(DatDir / TEXT("client_highres.dat")))) return false;
    FACEDatTextureResolver Textures(&Portal, &HighRes);
    FACELandblockMeshBuilder Builder(&Portal, &Cell, &Textures);
    constexpr int32 TilePixels = 64, MapSize = 24 * TilePixels;
    TArray<FColor> Map; Map.Init(FColor::Magenta, MapSize * MapSize);
    constexpr int32 DetailSize = 2048;
    TArray<FColor> Detail; Detail.Init(FColor::Magenta, DetailSize * DetailSize);
    struct FEdge { FVector Position, Normal; uint32 Block; };
    TMap<FIntPoint, FEdge> Edges;
    double MaxHeightError = 0, MinNormalDot = 1;
    for (int32 BX = 0; BX < 3; ++BX) for (int32 BY = 0; BY < 3; ++BY)
    {
        const uint32 Block = (uint32(0x7C + BX) << 24) | (uint32(0x63 + BY) << 16);
        FACEBuiltLandblockMesh Mesh;
        if (!TestTrue(TEXT("Yaraq terrain builds"), Builder.BuildLandblock(Block, 100, Mesh))) continue;
        for (const auto& Section : Mesh.Sections)
        {
            const TArray<FColor> SectionPixels = Section.GetBakedPixels();
            for (int32 V = 0; V < Section.Vertices.Num(); ++V)
            {
                const FVector P = Section.Vertices[V];
                const int32 X = FMath::RoundToInt(-P.X / 2400), Y = FMath::RoundToInt(P.Y / 2400);
                if (X != 0 && X != 8 && Y != 0 && Y != 8) continue;
                const FIntPoint Key(BX * 8 + X, BY * 8 + Y);
                if (const FEdge* Existing = Edges.Find(Key); Existing && Existing->Block != Block)
                {
                    MaxHeightError = FMath::Max(MaxHeightError, FMath::Abs(Existing->Position.Z - P.Z));
                    MinNormalDot = FMath::Min(MinNormalDot, FVector::DotProduct(Existing->Normal, Section.Normals[V]));
                }
                else Edges.Add(Key, { P, Section.Normals[V], Block });
            }
            TestTrue(TEXT("Terrain retains full-resolution composites"), Section.BakeWidth == 1024 && Section.BakeHeight == 1024);
            if (!TestTrue(TEXT("Every terrain vertex has a texture coordinate"), Section.UVs.Num() == Section.Vertices.Num()) ||
                !TestTrue(TEXT("Composite has all authored texels"), SectionPixels.Num() == 1024*1024)) return false;
            for (int32 V = 0; V + 3 < Section.Vertices.Num(); V += 4)
            {
                const int32 CX = FMath::RoundToInt(-Section.Vertices[V].X / 2400);
                const int32 CY = FMath::RoundToInt(Section.Vertices[V].Y / 2400);
                for (int32 Corner = 0; Corner < 4; ++Corner)
                {
                    const FVector P = Section.Vertices[V+Corner];
                    const FVector2D Expected(1.0-(-P.X/2400.0-CX), 1.0-(P.Y/2400.0-CY));
                    TestTrue(TEXT("Adjacent cells retain identical paving density and orientation"), Section.UVs[V+Corner].Equals(Expected));
                }
                if (BX == 1 && BY == 1 && CX >= 2 && CX <= 3 && CY >= 3 && CY <= 4)
                {
                    AddInfo(FString::Printf(TEXT("Road tile %d,%d PCode=%08X UVs=%s %s %s %s"), CX, CY, Section.PCode,
                        *Section.UVs[V].ToString(), *Section.UVs[V+1].ToString(), *Section.UVs[V+2].ToString(), *Section.UVs[V+3].ToString()));
                    for (int32 Y = 0; Y < 1024; ++Y) for (int32 X = 0; X < 1024; ++X)
                        Detail[((4-CY)*1024+Y)*DetailSize+(CX-2)*1024+X] = SectionPixels[Y*1024+1023-X];
                }
                for (int32 Y = 0; Y < TilePixels; ++Y) for (int32 X = 0; X < TilePixels; ++X)
                {
                    const int32 PX = Section.BakeWidth - 1 - ((2*X+1)*Section.BakeWidth/(2*TilePixels));
                    const int32 PY = (2*Y+1)*Section.BakeHeight/(2*TilePixels);
                    const int32 MX = (BX*8+CX)*TilePixels+X;
                    const int32 MY = (23-(BY*8+CY))*TilePixels+Y;
                    Map[MY*MapSize+MX] = SectionPixels[PY*Section.BakeWidth+PX];
                }
            }
        }
    }
    TestEqual(TEXT("Shared landblock edges have matching elevations"), MaxHeightError, 0.);
    AddInfo(FString::Printf(TEXT("Shared terrain edges: maxHeightErrorCm=%.5f minimumNormalDot=%.6f"), MaxHeightError, MinNormalDot));
    const FString Directory = FPaths::ProjectSavedDir()/TEXT("Automation/TerrainSeams");
    IFileManager::Get().MakeDirectory(*Directory, true);
    TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(MapSize, MapSize, Map, PNG);
    FFileHelper::SaveArrayToFile(PNG, *(Directory/TEXT("yaraq-textures.png")));
    PNG.Reset(); FImageUtils::PNGCompressImageArray(DetailSize, DetailSize, Detail, PNG);
    FFileHelper::SaveArrayToFile(PNG, *(Directory/TEXT("yaraq-road-native.png")));

    // Straight Yaraq road: the two adjacent cells meet down its center. Compare
    // both sides with the original source image, independent of the bake sampler.
    FACEDatDecodedSurface Road;
    if (!TestTrue(TEXT("Read original retail paving image"), Textures.DecodeSurfaceTexture(0x05001458, Road))) return false;
    if (!TestTrue(TEXT("Paving source is the authored 512x512 image"), Road.Width == 512 && Road.Height == 512)) return false;
    TArray<FColor> West, East; int32 W = 0, H = 0;
    if (!Builder.BakePCode(0x11408421, West, W, H) || !Builder.BakePCode(0x14108421, East, W, H)) return false;
    int32 IncorrectRoadTexels = 0;
    for (int32 Y = 0; Y < 1024; ++Y) for (int32 X = 0; X < 8; ++X)
    {
        IncorrectRoadTexels += West[Y*1024+X] != Road.Pixels[(Y%512)*512+511-X];
        IncorrectRoadTexels += East[Y*1024+1023-X] != Road.Pixels[(Y%512)*512+X];
    }
    TestEqual(TEXT("Road center join preserves source paving phase and size on both sides"), IncorrectRoadTexels, 0);
    FFileHelper::SaveArrayToFile(TArrayView<const uint8>(reinterpret_cast<const uint8*>(West.GetData()), West.Num()*sizeof(FColor)),
        *(Directory/TEXT("road-11408421.bgra")));
    return !HasAnyErrors();
}
#endif
