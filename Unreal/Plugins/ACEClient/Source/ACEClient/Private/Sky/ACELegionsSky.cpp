#include "Sky/ACELegionsSky.h"
#include "Algo/BinarySearch.h"
#include "Algo/Find.h"
#include "Sky/ACELegionsEnvironment.h"
#include "ACERuntimeOptions.h"
#include "Dat/ACEDatTextureResolver.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <shobjidl.h>
#endif

namespace ACELegionsSky
{
namespace
{
// AC2 Legions' clear-day record. AC1's server still owns the clock and weather.
constexpr uint32 ClearDay = 0x1A000002;
constexpr int32 MaxRecord = 8 * 1024 * 1024;
constexpr int32 MaxPack = 40 * 1024 * 1024;
constexpr uint32 LegacyPack = 0x314B534C; // LSK1 did not import the terrain cloud mask.
constexpr uint32 CloudPack = 0x324B534C; // LSK2 added the cloud mask.
constexpr uint32 EnvironmentPack = 0x344B534C; // LSK4 also carries mapped outdoor artwork.
constexpr uint32 GrassPack = 0x354B534C; // LSK5 carries the meadow scene rules, mesh and atlas.
constexpr uint32 TreePack = 0x364B534C; // LSK6 adds the Yaraq scenery meshes and their material atlases.
constexpr uint32 DerethPack = 0x374B534C; // LSK7 includes mapped trees throughout Dereth.
constexpr uint32 GroundDetailPack = 0x384B534C; // LSK8 includes the native landscape detail stage.
constexpr uint32 SceneryPack = 0x394B534C; // LSK9 adds boulder surfaces and the small cut stump.
bool HasDetail(uint32 Version) { return Version == GroundDetailPack || Version == SceneryPack; }
bool HasDereth(uint32 Version) { return Version == DerethPack || HasDetail(Version); }
bool HasYaraq(uint32 Version) { return Version == TreePack || HasDereth(Version); }
bool HasMeadow(uint32 Version) { return Version == GrassPack || HasYaraq(Version); }
bool HasOutdoorArt(uint32 Version) { return Version == EnvironmentPack || HasMeadow(Version); }
constexpr int32 MaxRecords = 640;
constexpr uint32 PackVersion = 0x334B534C; // LSK3 includes the region's rain schedules.
TAtomic<int32> ImportRevision{0};
FCriticalSection CacheMutex;
FCriticalSection ImportMutex;
// Increment when a fresh import selects different records from the same DATs.
constexpr int32 ArtworkRecipe = 2;
#if PLATFORM_WINDOWS || PLATFORM_LINUX
FString ImportLockName(const FString& Destination)
{
	return FString::Printf(TEXT("ACUnrealLegions%08X"), FCrc::StrCrc32(*FPaths::ConvertRelativePathToFull(Destination)));
}
#endif
using FRecords = TMap<uint32, TArray<uint8>>;

// Sticky bounds checks, including nested record counts. Little-endian AC2 DAT.
struct FReader
{
	TConstArrayView<uint8> Data;
	int32 Pos = 0;
	bool Ok = true;
	TConstArrayView<uint8> Take(int32 N)
	{
		if (!Ok || N < 0 || N > Data.Num() - Pos)
		{
			Ok = false;
			return {};
		}
		auto Result = Data.Slice(Pos, N);
		Pos += N;
		return Result;
	}
	template <typename T> T Scalar()
	{
		T Value{};
		auto Bytes = Take(sizeof(T));
		if (Ok)
			FMemory::Memcpy(&Value, Bytes.GetData(), sizeof(T));
		return Value;
	}
	uint32 Word()
	{
		return Scalar<uint32>();
	}
	float Float()
	{
		const float V = Scalar<float>();
		Ok &= FMath::IsFinite(V);
		return Ok ? V : 0.f;
	}
	int32 Count(int32 Max)
	{
		const uint32 N = Word();
		Ok &= N <= uint32(Max);
		return Ok ? int32(N) : 0;
	}
	FVector Vector()
	{
		const float X = Float(), Y = Float(), Z = Float();
		return FVector(X, Y, Z);
	}
};

struct FArchiveFile
{
	TUniquePtr<FArchive> File;
	uint32 Sector = 0, Root = 0;
	bool Open(const FString& Path)
	{
		File.Reset(IFileManager::Get().CreateFileReader(*Path, FILEREAD_AllowWrite));
		if (!File || File->TotalSize() < 380)
			return false;
		uint32 H[9]{};
		File->Seek(300);
		File->Serialize(H, sizeof(H));
		Sector = H[1];
		Root = H[8];
		return !File->IsError() && H[0] == 0x5442 && H[3] == 1 && H[2] == File->TotalSize() && Sector >= 256 &&
		       Sector <= 65536 && FMath::IsPowerOfTwo(Sector) && ValidSector(Root);
	}
	bool ValidSector(uint32 At) const
	{
		return At >= 1024 && At % Sector == 0 && int64(At) + Sector <= File->TotalSize();
	}
	bool Chain(uint32 At, uint32 Size, TArray<uint8>& Out)
	{
		if (!Size || Size > MaxRecord)
			return false;
		Out.SetNumUninitialized(Size);
		TSet<uint32> Visited;
		for (uint32 Written = 0; Written < Size;)
		{
			if (!ValidSector(At) || Visited.Contains(At))
				return false;
			Visited.Add(At);
			File->Seek(At);
			const uint32 Count = FMath::Min(Size - Written, Sector - 4);
			File->Serialize(&At, 4);
			File->Serialize(Out.GetData() + Written, Count);
			if (File->IsError())
				return false;
			Written += Count;
		}
		return At == 0;
	}
	bool Read(uint32 Id, TArray<uint8>& Out)
	{
		uint32 At = Root;
		for (int32 Depth = 0; At && Depth < 64; ++Depth)
		{
			TArray<uint8> Node;
			if (!Chain(At, 1228, Node))
				return false;
			FReader R{Node};
			R.Pos = 248;
			const int32 N = R.Count(61);
			if (!R.Ok)
				return false;
			int32 I = 0;
			for (; I < N; ++I)
			{
				const uint32 Key = R.Word(), Address = R.Word(), Size = R.Word();
				R.Word();
				if (Key == Id)
					return Chain(Address, Size, Out);
				if (Key > Id)
					break;
			}
			R.Pos = 0;
			if (!R.Word())
				break;
			R.Pos = I * 4;
			At = R.Word();
		}
		return false;
	}
};

struct FSource
{
	FArchiveFile Portal, High;
	bool HasPortal = false, HasHigh = false;
	FRecords Records;
	int32 Total = 0;
	int32 DecodedPixels = 0;
	FReader Get(uint32 Id, int32 MaxDimension = 0)
	{
		auto WithinLimit = [MaxDimension](const TArray<uint8>& Bytes)
		{
			if (!MaxDimension) return true;
			FReader Surface{Bytes};
			Surface.Take(8);
			const int32 W = Surface.Count(MaxDimension), H = Surface.Count(MaxDimension);
			return Surface.Ok && W > 0 && H > 0;
		};
		// Existing packs can contain higher mips for a mesh that previously
		// shared a full-size atlas. Select the same mip as a fresh DAT import.
		if (auto* Bytes = Records.Find(Id))
			return WithinLimit(*Bytes) ? FReader{*Bytes} : FReader{{},0,false};
		TArray<uint8> B;
		if (!HasPortal || Records.Num() >= MaxRecords ||
		    !((HasHigh && (Id >> 24) == 0x41 && High.Read(Id, B)) || Portal.Read(Id, B)) ||
		    B.Num() > MaxPack - Total)
			return {{}, 0, false};
		if (!WithinLimit(B)) return {{},0,false};
		Total += B.Num();
		return {Records.Add(Id, MoveTemp(B))};
	}
};

FLinearColor ReadColor(FReader& Value)
{
	const float Red = Value.Float(), Green = Value.Float(), Blue = Value.Float(), Alpha = Value.Float();
	return FLinearColor(Red, Green, Blue, Alpha);
}

bool ReadTexture(FSource& Src, uint32 Id, TMap<uint32, FImage>& Images, bool World = false, bool CpuPixels = false, bool CompactAtlas = false)
{
	if (Images.Contains(Id))
		return true;
	if (Images.Num() >= (World ? 40 : 16) || (Id >> 24) != 0x40)
		return false;
	FReader R = Src.Get(Id);
	if (R.Word() != Id)
		return false;
	R.Word();
	const int32 Levels = R.Count(32);
	FImage Image;
	int32 NextWidth = 0, NextHeight = 0;
	// Retain the installed high-resolution ground and canopy mips; compact tree atlases share a smaller budget.
	const int32 MaxDimension = !World ? 0 : (CompactAtlas ? 512 : 1024);
	for (int32 I = 0; I < Levels; ++I)
	{
		const uint32 Surface = R.Word();
		if ((Surface >> 24) != 0x41)
			return false;
		FReader S = Src.Get(Surface, MaxDimension);
		if (!S.Ok)
			continue; // Highest installed mip, highres.dat is optional.
		if (S.Word() != Surface)
			return false;
		S.Word();
		FACEDatTexture T;
		T.Width = S.Count(2048);
		T.Height = S.Count(2048);
		T.Format = EACESurfacePixelFormat(S.Word());
		const int32 Bytes = S.Count(MaxRecord);
		const auto Payload = S.Take(Bytes);
		if (!S.Ok || !T.Width || !T.Height || S.Pos != S.Data.Num())
			return false;
		if (MaxDimension && FMath::Max(T.Width,T.Height) > MaxDimension) return false;
		if (!Image.Mips.IsEmpty() && (T.Width != NextWidth || T.Height != NextHeight))
			return false;
		Src.DecodedPixels += T.Width * T.Height;
		if (Src.DecodedPixels > (World ? 40 : 16) * 1024 * 1024)
			return false;
		// Validate sizes before handing a record to the shared AC texture decoder.
		int32 Expected = 0;
		EPixelFormat BlockFormat = PF_Unknown;
		switch (T.Format)
		{
		case EACESurfacePixelFormat::DXT1:
			BlockFormat = PF_DXT1;
			Expected = ((T.Width + 3) / 4) * ((T.Height + 3) / 4) * 8;
			break;
		case EACESurfacePixelFormat::DXT3:
		case EACESurfacePixelFormat::DXT5:
			BlockFormat = T.Format == EACESurfacePixelFormat::DXT3 ? PF_DXT3 : PF_DXT5;
			Expected = ((T.Width + 3) / 4) * ((T.Height + 3) / 4) * 16;
			break;
		case EACESurfacePixelFormat::R8G8B8:
			Expected = T.Width * T.Height * 3;
			break;
		case EACESurfacePixelFormat::A8R8G8B8:
			Expected = T.Width * T.Height * 4;
			break;
		default:
			return false;
		}
		if (Bytes != Expected)
			return false;
		if (Image.Mips.IsEmpty())
		{
			Image.Width = T.Width;
			Image.Height = T.Height;
			if (!CpuPixels && Id != 0x400003e7 && BlockFormat != PF_Unknown && GPixelFormats[BlockFormat].Supported &&
			    T.Width % 4 == 0 && T.Height % 4 == 0)
				Image.Format = BlockFormat;
		}
		if (Image.Format != PF_B8G8R8A8)
		{
			if (Image.Format != BlockFormat) return false;
			Image.Mips.Emplace(Payload);
		}
		else
		{
			T.SourceData.Append(Payload.GetData(), Bytes);
			FACEDatTextureResolver Decoder(nullptr, nullptr);
			FACEDatDecodedSurface Decoded;
			if (!Decoder.DecodeTextureForUi(T, Decoded) || Decoded.Pixels.Num() != T.Width*T.Height)
				return false;
			Image.Mips.AddDefaulted_GetRef().Append(reinterpret_cast<const uint8*>(Decoded.Pixels.GetData()), Decoded.Pixels.Num()*sizeof(FColor));
		}
		NextWidth = FMath::Max<int32>(1, T.Width / 2);
		NextHeight = FMath::Max<int32>(1, T.Height / 2);
	}
	if (!R.Ok || Image.Mips.IsEmpty())
		return false;
	// This matte encodes a vertical opacity ramp as dithered white DXT3
	// blocks. Recover the row coverage so magnification doesn't expose the grid.
	if (Id == 0x400003e7)
	{
		for (int32 Level = 0; Level < Image.Mips.Num(); ++Level)
		{
			auto& Bytes = Image.Mips[Level];
			FColor* Pixels = reinterpret_cast<FColor*>(Bytes.GetData());
			const int32 Width = FMath::Max(1, Image.Width >> Level), Height = FMath::Max(1, Image.Height >> Level);
			TArray<int64> RowSums;
			RowSums.Add(0);
			for (int32 Y = 0; Y < Height; ++Y)
			{
				int64 Sum = RowSums.Last();
				for (int32 X = 0; X < Width; ++X)
					Sum += Pixels[Y * Width + X].A;
				RowSums.Add(Sum);
			}
			const int32 Radius = FMath::Max(1, Height / 256);
			for (int32 Y = 0; Y < Height; ++Y)
			{
				const int32 First = FMath::Max(0, Y - Radius), End = FMath::Min(Height, Y + Radius + 1);
				const int32 Samples = (End - First) * Width;
				Pixels[Y] = FColor(255, 255, 255, (RowSums[End] - RowSums[First] + Samples / 2) / Samples);
			}
			Bytes.SetNum(Height*sizeof(FColor));
		}
		Image.Width = 1;
	}
	Images.Add(Id, MoveTemp(Image));
	return true;
}

// Stage-zero diffuse maps verified against the Legions SurfaceDesc and the
// AC1 TexMerge artwork. Zero preserves the AC1 material and its authored tiling.
constexpr uint32 TerrainTextures[] = {
    0x40000525, 0x40000241, 0x400006db, 0,          // barren rock, grassland, cracked ice, lush grass
    0, 0x40000257, 0x4000024a, 0x4000042d,          // sparse marsh, mud, obsidian, packed dirt
    0, 0, 0x40000256, 0,                            // patchy dirt/grass, rippled sand, grey sand
    0, 0, 0x40000244, 0x40000251,                   // rocky sand, sedimentary rock, semi-barren rock, snow
    0, 0, 0, 0, 0, 0, 0, 0,                         // water, forest floor, faux water and sea slime
    0, 0x400011ac, 0, 0, 0, 0x40000252, 0x4000025a, 0, // argila, lava, ash, blue ice, mosses, Olthoi, desolate
    0x40000b0f                                      // dressed cobblestone road
};
uint8 TerrainGroup(uint32 Type)
{
	return Type == 1 || Type == 3 || Type == 9 ? Grass : Ground;
}

// Native Legions scenery. Record IDs identify the material's base
// texture and whether its cards need cutouts; several meshes share bark and leaves.
struct FTreeMaterial { uint32 Material, Texture; bool Masked; };
constexpr FTreeMaterial TreeMaterials[] = {
	{0x31000206,0x400002cb,true}, {0x31000205,0x400002c8,false}, // legacy broadleaf
	{0x310006b2,0x400002c8,false}, {0x310006b3,0x400002cb,true}, // compact broadleaf
	{0x31000359,0x400004a3,true}, {0x3100035a,0x400004a3,true}, // conifer atlas
	{0x310004b2,0x4000074a,true}, {0x310004d1,0x4000078c,true}, {0x310004d2,0x4000078c,true},
	{0x3100080d,0x40000dc5,true}, {0x3100080e,0x40000dc6,false},
	{0x310001c5,0x4000027e,true}, {0x310001c6,0x4000027f,false}, {0x310001c7,0x40000280,true},
	{0x310004be,0x4000076a,true}, {0x310004bd,0x40000767,false}, {0x310004bf,0x4000076c,true},
	{0x310000e1,0x4000013a,true},
	{0x31000010,0x40000015,false},
	{0x310002f3,0x4000042a,false},
	{0x310002f1,0x40000428,true},
	{0x3100044c,0x40000696,true},
	{0x31000014,0x4000001a,false},
	{0x31000015,0x4000001c,true},
	{0x31000016,0x4000001d,true},
	{0x3100048d,0x40000712,true},
	{0x3100048a,0x4000070c,true},
	{0x3100080f,0x40000dc7,true},
	{0x31000810,0x40000125,false},
	{0x310006b6,0x40000b08,false},
	{0x3100072b,0x40000bf0,false}, // cut stump bark and end grain
};
constexpr uint32 SmallStumpSetup = 0x020003df, LegionsStumpMesh = 0x06000e26;
constexpr uint32 OriginalTreeMesh = 0x060001ff;
struct FTreeMapping { uint32 Setup, Mesh; };
constexpr FTreeMapping LegacyTreeMappings[] = {
	{0x02000258,OriginalTreeMesh}, {0x020003ee,OriginalTreeMesh},
	{0x02000322,0x060007d9}, // Yaraq town: rounded fruit tree, high-detail crown of Legions 04000423
	{0x02000257,0x060003f2}, // conifer, 040001F8
	{0x020003ea,0x060007dc} // mature leaning tree, 04000424
};
// Render setups observed in the AC1 cell and region DATs. Mixed rock/grove
// setups, low-crowned scrub and scripted scenery keep their AC1 appearance.
// Keep desert deadwood and small palms in their native form. The Legions
// dead tree carries hanging moss and the mature palms have exposed trunks.
// Reserve the red-fruited Legions atlas for the fruit-bearing AC1 setup 0322.
// The three field trees at 7D66 are 3.8–6.5 m tall. The comparable AC2
// broadleaf meshes are 21–28 m tall; the small AC2 bushes lack their trunks.
// Leave these saplings and their 1003 variant in their authored form.
constexpr FTreeMapping DerethTreeMappings[] = {
	{0x01000d37,0x060006c7}, // Straight fan palm
	{0x0200009d,0x0600063c}, // Autumn canopy
	{0x02000214,0x060006c7}, // Straight fan palm
	{0x02000251,0x06000734}, // Spreading broadleaf
	{0x02000256,0x060003f2}, // Tapered evergreen
	{0x02000257,0x060003f2}, // Tapered evergreen
	{0x02000258,0x060003f2}, // Tapered evergreen
	{0x020002d3,0x060003f2}, // Tapered evergreen
	{0x020002d7,0x06000734}, // Broad crown, from the 28 m spreading tree
	{0x020002d8,0x060003f2}, // Tapered evergreen
	{0x020002db,0x060003f2}, // Tapered evergreen
	{0x020002dc,0x06000734}, // Broad crown, from the 28 m spreading tree
	{0x02000322,0x060007d9}, // Rounded orchard crown
	{0x02000325,0x06000734}, // Spreading broadleaf
	{0x020003a1,0x060003f2}, // Tapered evergreen
	{0x020003a2,0x06000e07}, // Frosted conifer
	{0x020003a3,0x06000e07}, // Frosted conifer
	{0x020003a4,0x06000e07}, // Frosted conifer
	{0x020003a7,0x06000e07}, // Frosted conifer
	{0x020003ea,0x06000734}, // Spreading mature broadleaf
	{0x020003eb,0x06000734}, // Spreading mature broadleaf
	{0x020003ed,0x06000734}, // Spreading broadleaf
	{0x020003ee,0x06000734}, // Broad crown, from the 28 m spreading tree
	{0x020003ef,0x06000734}, // Broad crown, from the 28 m spreading tree
	{0x020004b1,0x060006c7}, // Straight fan palm
	{0x0200062b,0x0600063c}, // Autumn canopy
	{0x0200062c,0x06000015}, // Forked summer tree
	{0x0200071f,0x0600063c}, // Autumn canopy
	{0x020007bc,0x0600063c}, // Autumn canopy
	{0x020007be,0x060006c7}, // Straight fan palm
	{0x020007bf,0x060006c2}, // Leaning slender palm
	{0x020007d4,0x06000734}, // Broad crown, from the 28 m spreading tree
	{0x020007d9,0x06000734}, // Spreading mature broadleaf
	{0x020007da,0x060006c7}, // Straight 13 m palm
	{0x020007db,0x060006c2}, // Leaning slender palm
	{0x02000847,0x0600063c}, // Autumn canopy
	{0x02000a35,0x06000d3b}, // Stocky 26 m broadleaf
	{0x02000a67,0x06000d3b}, // Stocky 26 m broadleaf
	{0x02000e72,0x060006c2}, // Leaning slender palm
	{0x02000f06,0x060003f2}, // Tapered evergreen
	{0x0200129a,0x06000a92}, // Tall open pine
	{0x0200129b,0x06000a92}, // Tall open pine
	{0x020012fb,0x06000e07}, // Frosted conifer
	{0x020012fc,0x0600063c}, // Autumn canopy
	{0x020012fd,0x0600063c}, // Autumn canopy
	{0x020012fe,0x06000d3d}, // Dead snag
	{0x020012ff,0x06000d3b}, // Open olive crown
};
uint32 TreeMeshId(uint32 Setup, bool Expanded = true)
{
	if (Expanded)
	{
		if (Setup == SmallStumpSetup) return LegionsStumpMesh;
		const int32 Index = Algo::LowerBoundBy(DerethTreeMappings, Setup, &FTreeMapping::Setup);
		return Index < UE_ARRAY_COUNT(DerethTreeMappings) && DerethTreeMappings[Index].Setup == Setup ? DerethTreeMappings[Index].Mesh : 0;
	}
	for (const auto& Mapping : LegacyTreeMappings)
		if (Mapping.Setup == Setup) return Mapping.Mesh;
	return 0;
}

bool DecodeTree(FSource& Src, FEnvironment& Env, uint32 Mesh, bool CompactAtlas = false)
{
	if (Env.TreeModels.Contains(Mesh)) return true;
	FReader R = Src.Get(Mesh);
	if (R.Word() != Mesh || R.Word() != 2) return false;
	const int32 Slots = R.Count(3);
	FTree Tree;
	for (int32 Slot = 0; Slot < Slots; ++Slot)
	{
		const uint32 Material = R.Word();
		const auto* Binding = Algo::FindBy(TreeMaterials, Material, &FTreeMaterial::Material);
		if (!Binding) return false;
		auto& Section = Tree.Sections.AddDefaulted_GetRef();
		Section.Texture = Binding->Texture;
		Section.Masked = Binding->Masked;
	}
	const uint32 Format = R.Word();
	const int32 Matrices = (Format >> 16) & 255;
	if (!Slots || (Format & 0xff00ffff) != 0x300002d2 || Matrices > 4) return false;
	const int32 NV = R.Count(4096);
	for (int32 I = 0; I < NV; ++I)
	{
		// The bind-pose positions precede the packed matrix indices and weights.
		FReader V{R.Take(72 + 5 * Matrices)};
		FVector P = V.Vector(), N = V.Vector();
		P.Y = -P.Y;
		N.Y = -N.Y;
		V.Pos = 56;
		const float U = V.Float(), TexV = V.Float();
		if (!V.Ok || P.GetAbsMax() > 1000.f || N.GetAbsMax() > 2.f) return false;
		Tree.Vertices.Add(P);
		Tree.Normals.Add(N.GetSafeNormal());
		Tree.UVs.Emplace(U, TexV);
	}
	R.Take((4 - (R.Pos & 3)) & 3);
	R.Take(40);
	const int32 Levels = R.Count(64);
	if (!Levels || !NV) return false;
	const auto MaterialSlots = Tree.Sections;
	int32 PreviousCount = MAX_int32;
	uint32 FullSlots = 0;
	for (int32 Level = 0; Level < Levels; ++Level)
	{
		R.Take(8);
		const int32 Fragments = R.Count(Slots);
		auto Sections = MaterialSlots;
		uint32 UsedSlots = 0;
		int32 TotalIndices = 0;
		for (int32 I = 0; I < Fragments; ++I)
		{
			const int32 Slot = R.Count(Slots - 1), Count = R.Count(50000);
			if (!R.Ok || Count % 3) return false;
			auto& Indices = Sections[Slot].Indices;
			Indices.Reserve(Indices.Num() + Count);
			for (int32 K = 0; K < Count; ++K)
			{
				const int32 Index = R.Scalar<uint16>();
				if (!R.Ok || Index >= NV) return false;
				Indices.Add(Index);
				if (Level == 0) Tree.Bounds += Tree.Vertices[Index];
			}
			if (Count) UsedSlots |= 1u << Slot;
			TotalIndices += Count;
			R.Take((4 - (R.Pos & 3)) & 3);
		}
		Sections.RemoveAll([](const FTreeSection& S){ return S.Indices.IsEmpty(); });
		if (Level == 0)
		{
			Tree.Sections = MoveTemp(Sections);
			FullSlots = UsedSlots;
		}
		else
		{
			// Some final levels are billboard impostors with a different atlas or no trunk.
			if (UsedSlots != FullSlots || TotalIndices >= PreviousCount) continue;
			Tree.DistanceLevels.Add(MoveTemp(Sections));
		}
		PreviousCount = TotalIndices;
	}
	// Limit upload and build cost to one intermediate model and the lightest usable one.
	if (Tree.DistanceLevels.Num() > 2)
	{
		auto Middle = MoveTemp(Tree.DistanceLevels[(Tree.DistanceLevels.Num() - 1) / 2]);
		auto Far = MoveTemp(Tree.DistanceLevels.Last());
		Tree.DistanceLevels.Reset(2);
		Tree.DistanceLevels.Add(MoveTemp(Middle));
		Tree.DistanceLevels.Add(MoveTemp(Far));
	}
	if (!R.Ok || Tree.Sections.IsEmpty() || Tree.Bounds.GetSize().GetMin() <= UE_SMALL_NUMBER) return false;
	for (const auto& Section : Tree.Sections)
		if (!ReadTexture(Src,Section.Texture,Env.SceneryTextures,true,false,CompactAtlas)) return false;
	Env.TreeModels.Add(Mesh,MoveTemp(Tree));
	return true;
}

bool DecodeYaraqTrees(FSource& Src, FEnvironment& Env)
{
	for (const auto& Mapping : LegacyTreeMappings)
		if (!DecodeTree(Src,Env,Mapping.Mesh)) return false;
	return true;
}

bool DecodeDerethTrees(FSource& Src, FEnvironment& Env)
{
	for (const auto& Mapping : DerethTreeMappings)
		if (!DecodeTree(Src,Env,Mapping.Mesh,true)) return false;
	Env.bDerethTrees = true;
	return true;
}

// Single-material AC1 scenery verified against the Dereth placement records.
// Retain the AC1 mesh. The Legions boulder meshes have different buried extents.
struct FBoulderSurface { uint32 Setup, Texture; };
constexpr FBoulderSurface BoulderSurfaces[] = {
	{0x020005c9, 0x400005be}, // pale weathered stone
	{0x020005ce, 0x40000bf3}, // iron-brown boulder
	{0x020005d0, 0x400005be},
	{0x020006cf, 0x40000010}, // ochre boulder
	{0x020014b5, 0x40000bf4}, // dark layered stone
};

bool DecodeBoulders(FSource& Src, FEnvironment& Env)
{
	for (const auto& Surface : BoulderSurfaces)
		if (!ReadTexture(Src, Surface.Texture, Env.SceneryTextures, true, false, true)) return false;
	return true;
}

bool DecodeEnvironment(FSource& Src, uint8 Selected, FEnvironment& Env)
{
	auto ReadSurface = [&](uint32 Id)
	{
		TMap<uint32, FImage> Decoded;
		if (!ReadTexture(Src, Id, Decoded, true, true)) return false;
		const FImage& Image = Decoded.FindChecked(Id);
		auto& Levels = Env.Surfaces.Add(Id);
		for (int32 I = 0; I < Image.Mips.Num(); ++I)
		{
			auto& Surface = Levels.AddDefaulted_GetRef();
			Surface.Width = FMath::Max(1, Image.Width >> I);
			Surface.Height = FMath::Max(1, Image.Height >> I);
			Surface.SurfaceId = Id;
			Surface.bHasPixels = true;
			Surface.Pixels.SetNumUninitialized(Surface.Width * Surface.Height);
			FMemory::Memcpy(Surface.Pixels.GetData(), Image.Mips[I].GetData(), Image.Mips[I].Num());
		}
		return true;
	};
	for (uint32 Type = 0; Type < UE_ARRAY_COUNT(TerrainTextures); ++Type)
	{
		const uint32 Id = TerrainTextures[Type];
		// Previously imported packs may lack an individual counterpart.
		if (Id && (Selected & TerrainGroup(Type)) && !Env.Surfaces.Contains(Id) && Src.Get(Id).Ok && !ReadSurface(Id))
			return false;
	}
	// Packs imported before the detail stage contain the diffuse records only.
	if ((Selected & (Ground | Grass)) && Src.Get(FEnvironment::DetailTextureId).Ok)
	{
		TMap<uint32,FImage> Decoded;
		if (!ReadTexture(Src,FEnvironment::DetailTextureId,Decoded,true,true)) return false;
		Env.GroundDetail = MoveTemp(Decoded.FindChecked(FEnvironment::DetailTextureId));
		int64 Sum = 0;
		for (auto& Mip : Env.GroundDetail.Mips)
		{
			// Alpha is linear in an sRGB texture, preserving the DAT's detail
			// weights independently of the renderer's colour transfer function.
			for (int32 I=0; I<Mip.Num(); I+=4) Mip[I+3] = Mip[I+2];
		}
		const auto& Top = Env.GroundDetail.Mips[0];
		for (int32 I=3; I<Top.Num(); I+=4) Sum += Top[I];
		Env.DetailMean = float(Sum) / (255.f * Env.GroundDetail.Width * Env.GroundDetail.Height);
	}
	if ((Selected & Trees) && !DecodeTree(Src, Env, OriginalTreeMesh)) return false;
	Env.Enabled = Selected;
	return true;
}

// Only import the grass branch of Legions' grassland meadow. Other objects in
// this scene are flowers and props with separate materials and appearances.
bool DecodeGrassCards(FSource& Src, FEnvironment& Env)
{
	FGrassMeadow Meadow;
	// TerrainDesc: grassland (1), meadow scene variant (1).
	FReader Terrain = Src.Get(0x12000000);
	Terrain.Take(8);
	auto ReadTerrain = [&](uint32& MeadowType)
	{
		uint32 Length = Terrain.Scalar<uint16>();
		if (Length == 65535) Length = Terrain.Word();
		if (Length > 4096) { Terrain.Ok = false; return; }
		Terrain.Take(Length);
		Terrain.Take((-Terrain.Pos) & 3);
		Terrain.Word();
		const uint32 Type = Terrain.Word();
		Terrain.Float();
		if (Terrain.Word()) Terrain.Take(8);
		const int32 Variants = Terrain.Count(256);
		for (int32 I = 0; I < Variants; ++I)
		{
			const uint32 Scene = Terrain.Word(), Local = Terrain.Word();
			if (Type == 1 && Local == 1) MeadowType = Scene;
		}
	};
	uint32 Type = 0;
	if (Terrain.Word()) ReadTerrain(Type);
	const int32 Types = Terrain.Count(1024);
	for (int32 I = 0; I < Types; ++I) ReadTerrain(Type);
	if (!Terrain.Ok || !Type) return false;

	FReader Scenes = Src.Get(0x11000000);
	Scenes.Take(8);
	const int32 NumScenes = Scenes.Count(16384);
	for (int32 I = 0; I < NumScenes; ++I)
	{
		const uint32 SceneType = Scenes.Word();
		Scenes.Word();
		const int32 Entries = Scenes.Count(1024);
		for (int32 J = 0; J < Entries; ++J)
		{
			const uint32 Id = Scenes.Word();
			const float Weight = Scenes.Float();
			Scenes.Word();
			if (SceneType != Type) continue;
			if (Id == 0x020001a9) { Meadow.DirectScene = Id; Meadow.DirectWeight = Weight; }
			else if (Id == 0x0200025d) { Meadow.NestedScene = Id; Meadow.NestedWeight = Weight; }
			else return false;
		}
	}
	if (!Scenes.Ok || Meadow.DirectWeight <= 0 || Meadow.NestedWeight <= 0) return false;

	// Both weighted alternatives refer to the same grass subscene. The second
	// first tests a wrapper frequency. Keep its seed path when it is selected.
	auto ReadGroup = [](FReader& R)
	{
		FFrillRule Rule{};
		Rule.MinOffset = R.Vector(); Rule.MaxOffset = R.Vector();
		Rule.MinRotation = R.Float(); Rule.MaxRotation = R.Float();
		Rule.Frequency = R.Float();
		Rule.MinScale = R.Vector(); Rule.MaxScale = R.Vector();
		R.Float();
		Rule.MaxNormalZ = R.Float(); Rule.MinNormalZ = R.Float();
		R.Take(13*4); // alignment flags, height bands and clearance radius
		Rule.CheckRadius = R.Float();
		R.Word();
		Rule.MaxCheckZ = R.Float(); Rule.MinCheckZ = R.Float();
		R.Take(19*4);
		return Rule;
	};
	auto FirstLevel = [](FReader& R)
	{
		if (R.Word() != 1) return 0;
		R.Take(12);
		return R.Count(256);
	};
	FReader Wrapper = Src.Get(Meadow.NestedScene);
	Wrapper.Take(12);
	if (Wrapper.Word() || FirstLevel(Wrapper) != 2 || Wrapper.Word() != 2) return false;
	Meadow.NestedFrequency = ReadGroup(Wrapper).Frequency;
	if (Wrapper.Count(1) != 1 || Wrapper.Word() != 3 || Wrapper.Word() != Meadow.DirectScene || !Wrapper.Ok) return false;
	FReader Direct = Src.Get(Meadow.DirectScene);
	Direct.Take(12);
	if (Direct.Word() != 1) return false;
	ReadGroup(Direct);
	if (Direct.Count(1) != 1 || Direct.Word() != 3) return false;
	Meadow.FrillScene = Direct.Word();
	if (!Direct.Ok || Meadow.FrillScene != 0x020000b6) return false;

	FReader Frill = Src.Get(Meadow.FrillScene);
	Frill.Take(12);
	if (Frill.Word()) return false;
	const int32 Nodes = FirstLevel(Frill);
	for (int32 I = 0; I < Nodes; ++I)
	{
		if (Frill.Word() != 2) return false;
		FFrillRule Rule = ReadGroup(Frill);
		Rule.ChildIndex = I;
		if (Frill.Count(1) != 1 || Frill.Word() != 1) return false;
		if (Frill.Word() != 0x47000006) continue;
		if (Rule.MinOffset.GetAbsMax() > 5.01 || Rule.MaxOffset.GetAbsMax() > 5.01 ||
		    Rule.MinScale.GetMin() <= 0 || Rule.MaxScale.GetMax() > 4 || Rule.Frequency < 0 || Rule.Frequency > 1) return false;
		Meadow.Rules.Add(Rule);
	}
	if (!Frill.Ok || Meadow.Rules.IsEmpty()) return false;

	// Native skinned frill 47000006 -> entity 1F000004 -> setup 04000004.
	// The stored draw vertices of its single mesh are already in bind space.
	FReader Obj = Src.Get(0x47000006);
	Obj.Take(12);
	const uint32 Entity = Obj.Word();
	if (Entity != 0x1f000004) return false;
	FReader Ent = Src.Get(Entity);
	Ent.Take(28);
	const uint32 Setup = Ent.Word();
	if (Setup != 0x04000004) return false;
	FReader Part = Src.Get(Setup);
	Part.Take(8);
	if (Part.Count(1) != 1) return false;
	const uint32 Mesh = Part.Word();
	FReader R = Src.Get(Mesh);
	if (Mesh != 0x06000004 || R.Word() != Mesh || R.Word() != 2 || R.Count(1) != 1 ||
		R.Word() != 0x31000006 || R.Word() != 0x300102d2) return false;
	const int32 NV = R.Count(4096);
	for (int32 I = 0; I < NV; ++I)
	{
		FReader V{R.Take(77)};
		FVector P = V.Vector();
		V.Pos = 56;
		const float U = V.Float(), TexV = V.Float();
		if (!V.Ok || P.GetAbsMax() > 16) return false;
		Meadow.Vertices.Add(P); Meadow.UVs.Emplace(U, TexV); Meadow.Bounds += P;
	}
	R.Take((-R.Pos) & 3);
	R.Take(40);
	if (!R.Count(64)) return false;
	R.Take(8);
	if (R.Count(1) != 1 || R.Word() != 0) return false;
	const int32 NI = R.Count(65535);
	if (!NI || NI % 6) return false;
	for (int32 I = 0; I < NI; ++I)
	{
		const int32 Index = R.Scalar<uint16>();
		if (Index >= NV) return false;
		Meadow.Indices.Add(Index);
	}
	if (!R.Ok || Meadow.Bounds.GetSize().Z <= UE_SMALL_NUMBER) return false;
	TMap<uint32, FImage> Cards;
	if (!ReadTexture(Src, 0x40000008, Cards, true)) return false;
	Env.GrassCards = MoveTemp(Cards.FindChecked(0x40000008));
	Env.Meadow = MoveTemp(Meadow);
	return true;
}

bool ReadMaterial(FSource& Src, uint32 Id, FSection& Section, FBackdrop& Sky)
{
	FReader M = Src.Get(Id);
	if ((Id >> 24) != 0x31 || M.Word() != Id)
		return false;
	const uint32 Template = M.Word();
	// The clear sky uses the template's first fixed-function layer. Its blend
	// factors distinguish glowing celestial cards from alpha-blended clouds.
	FReader Base = Src.Get(Template);
	if ((Template >> 24) != 0x2b || Base.Word() != Template)
		return false;
	Base.Word();
	const int32 Defaults = Base.Count(256);
	for (int32 I = 0; I < Defaults; ++I)
	{
		const uint32 Name = Base.Word();
		Base.Word();
		FReader Value{Base.Take(Base.Count(4096))};
		if (Name == 2000)
		{
			Section.Tint = ReadColor(Value);
			if (!Value.Ok)
				return false;
		}
		Base.Take(Base.Count(256) * 16);
	}
	if (!Base.Count(64))
		return false;
	Base.Take(8);
	const int32 Stages = Base.Count(2);
	for (int32 I = 0; I < Stages; ++I)
	{
		Base.Word();
		const uint32 ColorOp = Base.Word(), ColorA = Base.Word(), ColorB = Base.Word();
		const uint32 AlphaOp = Base.Word(), AlphaA = Base.Word(), AlphaB = Base.Word();
		if (ColorA != 2 || AlphaA != 2 || ColorB != (I ? 1u : 3u) || AlphaB != ColorB)
			return false;
		if (!I)
		{
			if ((ColorOp != 4 && ColorOp != 5) || (AlphaOp != 3 && AlphaOp != 4))
				return false;
			Section.StageOps.R = ColorOp == 5 ? 2.f : 1.f;
			Section.StageOps.G = AlphaOp == 4 ? 1.f : 0.f;
		}
		else
		{
			if ((ColorOp != 3 && ColorOp != 8) || (AlphaOp != 3 && AlphaOp != 5))
				return false;
			Section.StageOps.B = ColorOp == 8 ? 1.f : 0.f;
			Section.StageOps.A = AlphaOp == 5 ? 1.f : 0.f;
		}
		Base.Take(28);
	}
	const int32 Modifiers = Base.Count(256);
	for (int32 I = 0; I < Modifiers; ++I)
	{
		const uint32 Kind = Base.Word();
		Base.Word();
		switch (Kind)
		{
		case 4:
		case 6:
			Base.Take(88);
			break;
		case 5:
			Base.Take(44);
			break;
		case 7:
			Base.Take(64);
			break;
		default:
			return false;
		}
	}
	const uint32 SourceBlend = Base.Word(), DestBlend = Base.Word();
	if (!Base.Ok || SourceBlend != 5 || (DestBlend != 2 && DestBlend != 6))
		return false;
	Section.Additive = DestBlend == 2;
	M.Word();
	const int32 Mods = M.Count(8);
	uint32 SecondaryTexture = 0;
	for (int32 I = 0; I < Mods; ++I)
	{
		const uint32 Mod = M.Word();
		FReader R = Src.Get(Mod);
		if ((Mod >> 24) != 0x30 || R.Word() != Mod)
			return false;
		const int32 Props = R.Count(64);
		for (int32 J = 0; J < Props; ++J)
		{
			const uint32 Name = R.Word();
			R.Word();
			const int32 Size = R.Count(4096);
			FReader Value{R.Take(Size)};
			if (Name == 3000 && Size == 4)
				Section.Texture = Value.Word();
			if (Name == 3001 && Size == 4)
				SecondaryTexture = Value.Word();
			if (Name == 2000 && Size == 16)
				Section.Tint = ReadColor(Value);
			// Material UV panners use elapsed seconds. The clear sky uses linear phase waves.
			if (Name >= 1017 && Name <= 1020 && Size == 44 && Value.Word() == 2)
			{
				const int32 Stage = (Name - 1017) / 2, Axis = (Name - 1017) % 2;
				const float BaseValue = Value.Float(), BaseSpeed = Value.Float();
				Value.Take(8);
				const float Phase = Value.Float(), PhaseSpeed = Value.Float(), Frequency = Value.Float();
				Section.Offset[Stage][Axis] = BaseValue + Phase * UE_PI;
				Section.Velocity[Stage][Axis] = BaseSpeed + PhaseSpeed + Frequency;
			}
			if (!Value.Ok)
				return false;
			R.Take(R.Count(64) * 16);
		}
		if (!R.Ok || R.Pos != R.Data.Num())
			return false;
	}
	return M.Ok && Stages > 0 && (Stages == 1 || SecondaryTexture == Section.Texture) &&
	       ReadTexture(Src, Section.Texture, Sky.Images);
}

bool ReadGeometry(FSource& Src, uint32 Setup, FLayer& Layer, FBackdrop& Sky)
{
	FReader S = Src.Get(Setup);
	if ((Setup >> 24) != 4 || S.Word() != Setup)
		return false;
	S.Word();
	if (S.Word() != 1)
		return false;
	const uint32 Mesh = S.Word();
	// All native clear-day setups are single rigid meshes, with one placement frame.
	if (S.Word() != 1 || S.Word() != MAX_uint32)
		return false;
	S.Word();
	if (S.Count(32) < 1)
		return false;
	S.Word();
	if (S.Word() != 1)
		return false;
	const FVector Origin = S.Vector(), Scale = S.Vector();
	const float W = S.Float(), X = S.Float(), Y = S.Float(), Z = S.Float();
	const FQuat Rotation(X, Y, Z, W);
	if (!S.Ok || !Rotation.IsNormalized())
		return false;
	FReader R = Src.Get(Mesh);
	if ((Mesh >> 24) != 6 || R.Word() != Mesh)
		return false;
	R.Word();
	const int32 Materials = R.Count(8);
	TArray<uint32> MaterialIds;
	for (int32 I = 0; I < Materials; ++I)
		MaterialIds.Add(R.Word());
	const uint32 FVF = R.Word();
	// Position, normal, diffuse, specular (two-UV variants include tangents).
	if (FVF != 0x1d2 && FVF != 0x300002d2)
		return false;
	const int32 NV = R.Count(4096), Stride = FVF == 0x1d2 ? 40 : 72;
	const int32 UVOffset = FVF == 0x1d2 ? 32 : 56;
	double Nearest = DBL_MAX;
	for (int32 I = 0; I < NV; ++I)
	{
		FReader V{R.Take(Stride)};
		const FVector P = Rotation.RotateVector(Scale * V.Vector()) + Origin;
		V.Pos = UVOffset;
		const float U = V.Float(), TexV = V.Float();
		if (!V.Ok || P.ContainsNaN() || P.GetAbsMax() > 1.e6)
			return false;
		Layer.Vertices.Add(P);
		Layer.UVs.Emplace(U, TexV);
		if (P.Size() > 0.01)
			Nearest = FMath::Min(Nearest, P.Size());
	}
	if (Nearest == DBL_MAX)
		return false;
	for (auto& P : Layer.Vertices)
		P *= 1.e7 / Nearest;
	Layer.Center = FBox(Layer.Vertices).GetCenter();
	R.Take(40);
	const int32 Draws = R.Count(32);
	bool Preferred = false;
	for (int32 I = 0; I < Draws; ++I)
	{
		R.Word();
		const uint32 Flags = R.Word();
		const int32 NF = R.Count(16);
		TArray<FSection> Sections;
		const bool Keep = I == 0 || (!Preferred && (Flags & 4));
		for (int32 J = 0; J < NF; ++J)
		{
			const int32 Slot = R.Count(8), NI = R.Count(50000);
			if (!MaterialIds.IsValidIndex(Slot) || NI % 3)
				return false;
			FReader Indices{R.Take(2 * NI)};
			R.Take((4 - (R.Pos & 3)) & 3);
			if (!Keep)
				continue;
			FSection& Section = Sections.AddDefaulted_GetRef();
			for (int32 K = 0; K < NI; K += 3)
			{
				const int32 A = Indices.Scalar<uint16>(), B = Indices.Scalar<uint16>(),
				            C = Indices.Scalar<uint16>();
				if (FMath::Max3(A, B, C) >= NV)
					return false;
				Section.Indices.Append({A, C, B});
			}
			if (!Indices.Ok || !ReadMaterial(Src, MaterialIds[Slot], Section, Sky))
				return false;
		}
		if (Keep)
		{
			Layer.Sections = MoveTemp(Sections);
			Preferred = (Flags & 4) != 0;
		}
	}
	return R.Ok && !Layer.Sections.IsEmpty();
}

bool ReadDayKeys(FReader& R, uint32 DayId, TArray<FKey>& Keys)
{
	R.Word();
	if (R.Word() != DayId)
		return false;
	const int32 NK = R.Count(64);
	for (int32 K = 0; K < NK; ++K)
	{
		const float Time = R.Float();
		if (Time < 0 || Time > 1 || (K && Time <= Keys.Last().Time))
			return false;
		auto& Key = Keys.AddDefaulted_GetRef();
		Key.Time = Time;
		Key.FogMin = R.Float();
		Key.FogMax = R.Float();
		const float Haze = R.Float();
		Key.Haze = FColor(R.Word()).ReinterpretAsLinear();
		Key.Haze.A = FMath::Clamp(Haze, 0.f, 1.f);
		Key.Direct = R.Float();
		Key.Heading = R.Float();
		Key.Pitch = R.Float();
		Key.DirectColor = FColor(R.Word()).ReinterpretAsLinear();
		Key.Ambient = R.Float();
		Key.AmbientColor = FColor(R.Word()).ReinterpretAsLinear();
		R.Word();
		const int32 N = R.Count(16);
		for (int32 I = 0; I < N; ++I)
		{
			const uint32 Name = R.Word();
			if (R.Word() != 0)
				return false; // This preset tints and rotates its own graphics.
			const float Pitch = R.Float(), Heading = R.Float();
			Key.Objects.Add(Name, {Pitch, Heading, FColor(R.Word()).ReinterpretAsLinear()});
		}
		Key.Rain = R.Float();
		R.Take(8); // wind vector
	}
	return R.Ok && NK >= 2 && Keys[0].Time == 0 && Keys.Last().Time == 1;
}

bool Decode(FSource& Src, FBackdrop& Sky, bool WithCloudMask, bool WithWeather)
{
	if (WithCloudMask)
	{
		FReader Surface = Src.Get(0x13000000);
		Surface.Word();
		if (Surface.Word() != 0x13000000)
			return false;
		Surface.Take(12);
		Sky.CloudMask = Surface.Word();
		const float Scale = Surface.Float(), U = Surface.Float(), V = Surface.Float();
		Sky.CloudMotion = FLinearColor(Scale, U, V, 0);
		if (!Surface.Ok || !ReadTexture(Src, Sky.CloudMask, Sky.Images))
			return false;
	}
	FReader R = Src.Get(ClearDay);
	if (!ReadDayKeys(R, ClearDay, Sky.Keys))
		return false;
	if (WithWeather)
	{
		// Legions SkyDesc lists the early shower, daytime shower and clear day.
		// Ignore unreferenced test days in portal.dat.
		FReader Desc = Src.Get(0x15000000);
		Desc.Word();
		if (Desc.Word() != 0x15000000)
			return false;
		Desc.Take(36);
		const int32 Count = Desc.Count(16);
		const auto Objects = R.Data.RightChop(R.Pos);
		for (int32 I = 0; I < Count; ++I)
		{
			const uint32 DayId = Desc.Word();
			const float Weight = Desc.Float();
			if (DayId == ClearDay || Weight <= 0)
				continue;
			auto& Day = Sky.RainDays.AddDefaulted_GetRef();
			FReader Rain = Src.Get(DayId);
			// These days share one object list. Do not silently apply a schedule
			// to incompatible geometry if a different archive is selected.
			if (!ReadDayKeys(Rain, DayId, Day.Keys) || Rain.Data.Num() - Rain.Pos != Objects.Num() ||
			    FMemory::Memcmp(Rain.Data.GetData() + Rain.Pos, Objects.GetData(), Objects.Num()) != 0)
				return false;
			for (int32 K = 1; K < Day.Keys.Num(); ++K)
				if (Day.Keys[K].Rain > Day.Keys[Day.Wet].Rain)
					Day.Wet = K;
			if (Day.Keys[Day.Wet].Rain <= 0)
				return false;
			Day.Cloudy = Day.Wet;
			while (Day.Cloudy > 0 && Day.Keys[Day.Cloudy].Rain > 0)
				--Day.Cloudy;
		}
		if (!Desc.Ok || Desc.Pos != Desc.Data.Num() || Sky.RainDays.IsEmpty())
			return false;
	}
	const int32 NO = R.Count(16);
	for (int32 I = 0; I < NO; ++I)
	{
		FLayer& L = Sky.Layers.AddDefaulted_GetRef();
		L.Name = R.Word();
		L.Setup = R.Word();
		L.Begin = R.Float();
		L.End = R.Float();
		L.BeginPitch = R.Float();
		L.EndPitch = R.Float();
		L.BeginHeading = R.Float();
		L.EndHeading = R.Float();
		R.Take(24);
		L.West = R.Word() != 0;
		R.Float();
		R.Take(R.Count(64) * 12);
		if (!R.Ok || !ReadGeometry(Src, L.Setup, L, Sky))
			return false;
	}
	return R.Ok && R.Pos == R.Data.Num() && NO > 0;
}
}

const FACEDatDecodedSurface* FEnvironment::Terrain(uint32 Type, int32 MaxSize) const
{
	if (Type >= UE_ARRAY_COUNT(TerrainTextures) || !(Enabled & TerrainGroup(Type))) return nullptr;
	const auto* Levels = Surfaces.Find(TerrainTextures[Type]);
	if (!Levels || Levels->IsEmpty() || FMath::Min((*Levels)[0].Width, (*Levels)[0].Height) < 512) return nullptr;
	for (const auto& Level : *Levels)
		if (Level.Width <= MaxSize && Level.Height <= MaxSize) return &Level;
	return &Levels->Last();
}

bool IsMappedTree(uint32 SetupId)
{
	return TreeMeshId(SetupId) != 0;
}

const FTree* FEnvironment::FindTree(uint32 SetupId) const
{
	return (Enabled & Trees) ? TreeModels.Find(TreeMeshId(SetupId,bDerethTrees)) : nullptr;
}

float FEnvironment::UniformTerrainRepeats(uint32 PCode)
{
	if (IsFullRoad(PCode)) return 120.f / TerrainRepeatMetres(TerrainTextures[32]);
	const uint32 Type = PCode & 31;
	if (((PCode >> 20) & 255) || Type >= UE_ARRAY_COUNT(TerrainTextures) || !TerrainTextures[Type]) return 1.f;
	for (int32 Shift : {5,10,15})
		if (((PCode >> Shift) & 31) != Type) return 1.f;
	return 120.f / TerrainRepeatMetres(TerrainTextures[Type]);
}

int32 FEnvironment::TerrainTileCells(uint32 PCode) const
{
	if (!(Enabled & (Ground | Grass))) return 1;
	bool Imported = false;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		Imported |= Terrain((PCode >> (Corner*5)) & 31) != nullptr;
	}
	if (IsFullRoad(PCode)) Imported = false;
	if ((PCode & 0x0ff00000) && Terrain(32)) Imported = true;
	return Imported ? TerrainCellsAcross : 1;
}

// A simple AC1/AC2 boundary can keep both source textures at native resolution.
// TexMerge supplies a clamped, cell-local mask while the imported diffuse wraps in world space.
uint32 FEnvironment::TerrainBlendPair(uint32 PCode, uint32& NativeType) const
{
	if (!(Enabled & (Ground | Grass)) || ((PCode >> 20) & 255)) return 0;
	NativeType = MAX_uint32;
	uint32 ImportedType = MAX_uint32;
	for (int Shift : {0,5,10,15})
	{
		const uint32 Type = (PCode >> Shift) & 31;
		uint32& Slot = Terrain(Type) ? ImportedType : NativeType;
		if (Slot != MAX_uint32 && Slot != Type) return 0;
		Slot = Type;
	}
	return ImportedType != MAX_uint32 && NativeType != MAX_uint32 ? Terrain(ImportedType)->SurfaceId : 0;
}

bool IsMappedScenery(uint32 SetupId)
{
	return IsMappedTree(SetupId) || Algo::FindBy(BoulderSurfaces, SetupId, &FBoulderSurface::Setup);
}

bool IsBoulderTexture(uint32 TextureId)
{
	return Algo::FindBy(BoulderSurfaces, TextureId, &FBoulderSurface::Texture) != nullptr;
}

uint32 FEnvironment::BoulderTexture(uint32 SetupId) const
{
	if (Enabled & Boulders)
		if (const auto* Surface = Algo::FindBy(BoulderSurfaces, SetupId, &FBoulderSurface::Setup))
			return Surface->Texture;
	return 0;
}

uint64 FEnvironment::SceneryCacheKey() const
{
	const uint8 Mask = Enabled & (Trees | Boulders);
	return Mask ? (uint64(ContentCrc) << 8) | Mask : 0;
}

uint64 FEnvironment::TerrainCacheKey(uint64 Original) const
{
	const uint8 TerrainMask = Enabled & (Grass | Ground);
	return TerrainMask ? Original ^ (uint64(ContentCrc) << 16) ^ (0x4c53000700000000ull | TerrainMask) : Original;
}

FString CachePath()
{
	return FPaths::ProjectSavedDir() / TEXT("Skies/Legions.sky");
}
bool HasCache(bool* NeedsWeather, bool* HasEnvironment, bool* HasGrassCards, bool* HasDerethTrees, bool* HasGroundDetail, bool* HasBoulders)
{
	FScopeLock Lock(&CacheMutex);
	const FString Path = CachePath();
	auto& Files = IFileManager::Get();
	const FString Actual = Files.FileExists(*Path) ? Path : Path + TEXT(".previous");
	if (NeedsWeather || HasEnvironment || HasGrassCards || HasDerethTrees || HasGroundDetail || HasBoulders)
	{
		if (NeedsWeather) *NeedsWeather = false;
		if (HasEnvironment) *HasEnvironment = false;
		if (HasGrassCards) *HasGrassCards = false;
		if (HasGroundDetail) *HasGroundDetail = false;
		if (HasBoulders) *HasBoulders = false;
		if (HasDerethTrees) *HasDerethTrees = false;
		TUniquePtr<FArchive> Reader(Files.CreateFileReader(*Actual));
		uint32 Version = 0;
		if (!Reader || Reader->TotalSize()<sizeof(Version)) return false;
		*Reader << Version;
		if (HasEnvironment) *HasEnvironment = HasOutdoorArt(Version);
		if (HasGrassCards) *HasGrassCards = HasMeadow(Version);
		if (HasDerethTrees) *HasDerethTrees = HasDereth(Version);
		if (HasGroundDetail) *HasGroundDetail = HasDetail(Version);
		if (HasBoulders) *HasBoulders = Version == SceneryPack;
		if (NeedsWeather) *NeedsWeather = Version == LegacyPack || Version == CloudPack;
		return !Reader->IsError() && (HasOutdoorArt(Version) || Version == PackVersion || Version == CloudPack || Version == LegacyPack);
	}
	return Files.FileExists(*Actual);
}
int32 Revision()
{
	return ImportRevision.Load();
}

namespace
{
bool PublishCache(const FString& Incoming, const FString& Destination, FString& Error)
{
	FScopeLock Lock(&CacheMutex);
	auto& Files = IFileManager::Get();
	const FString Previous = Destination + TEXT(".previous");
	if ((Files.FileExists(*Destination) && !Files.Move(*Previous, *Destination, true)) ||
	    !Files.Move(*Destination, *Incoming, false))
	{
		if (!Files.FileExists(*Destination) && Files.FileExists(*Previous))
			Files.Move(*Destination, *Previous, false);
		Error = TEXT("The imported sky could not be saved.");
		return false;
	}
	Files.Delete(*Previous, false);
	++ImportRevision;
	Error.Reset();
	return true;
}

bool ImportArchives(const FString& Portal, const FString& HighRes, const FString& Destination, FString& Error, uint32 MinVersion = 0, uint32* PublishedCrc = nullptr)
{
	FSource Src;
	Src.HasPortal = Src.Portal.Open(Portal);
	if (!Src.HasPortal)
	{
		Error = TEXT("Choose an AC2 Legions folder containing portal.dat.");
		return false;
	}
	Src.HasHigh = !HighRes.IsEmpty() && Src.High.Open(HighRes);
	FBackdrop Sky;
	if (!Decode(Src, Sky, true, true))
	{
		Error = TEXT("These DATs do not contain a supported Legions sky and weather.");
		return false;
	}

	TArray<uint32> SkyRecords;
	Src.Records.GenerateKeyArray(SkyRecords);
	Sky = {};
	Src.DecodedPixels = 0;
	FEnvironment Environment;
	const bool WithEnvironment = DecodeEnvironment(Src, AllEnvironment, Environment);
	if (!WithEnvironment)
	{
		// An archive with the supported sky can still be used with the original
		// feature. Exclude any incomplete outdoor art from that legacy-format pack.
		TSet<uint32> Keep(SkyRecords);
		for (auto It = Src.Records.CreateIterator(); It; ++It)
			if (!Keep.Contains(It.Key())) It.RemoveCurrent();
	}
	TArray<uint8> Pack;
	FMemoryWriter Writer(Pack);
	const bool WithGrass = WithEnvironment && DecodeGrassCards(Src, Environment);
	TArray<uint32> ExistingRecords;
	if (WithGrass) Src.Records.GenerateKeyArray(ExistingRecords);
	const bool WithTrees = WithGrass && DecodeYaraqTrees(Src, Environment);
	if (WithGrass && !WithTrees)
	{
		// Keep a working meadow pack when an installation lacks these tree assets.
		const TSet<uint32> Keep(ExistingRecords);
		for (auto It = Src.Records.CreateIterator(); It; ++It)
			if (!Keep.Contains(It.Key())) It.RemoveCurrent();
		UE_LOG(LogTemp, Warning, TEXT("AC2 artwork: Yaraq tree records unavailable; using the original tree set."));
	}
	if (WithTrees) Src.Records.GenerateKeyArray(ExistingRecords);
	const bool WithDereth = WithTrees && DecodeDerethTrees(Src, Environment);
	if (WithTrees && !WithDereth)
	{
		const TSet<uint32> Keep(ExistingRecords);
		for (auto It = Src.Records.CreateIterator(); It; ++It)
			if (!Keep.Contains(It.Key())) It.RemoveCurrent();
		UE_LOG(LogTemp, Warning, TEXT("AC2 artwork: some Dereth tree records unavailable; retaining the Yaraq tree pack."));
	}
	const bool WithDetail = WithDereth && !Environment.GroundDetail.Mips.IsEmpty();
	if (WithDetail) Src.Records.GenerateKeyArray(ExistingRecords);
	const bool WithScenery = WithDetail && DecodeBoulders(Src, Environment) && DecodeTree(Src, Environment, LegionsStumpMesh, true);
	if (WithDetail && !WithScenery)
	{
		const TSet<uint32> Keep(ExistingRecords);
		for (auto It = Src.Records.CreateIterator(); It; ++It)
			if (!Keep.Contains(It.Key())) It.RemoveCurrent();
	}
	uint32 Version = WithScenery ? SceneryPack : WithDetail ? GroundDetailPack : WithDereth ? DerethPack : WithTrees ? TreePack : WithGrass ? GrassPack : WithEnvironment ? EnvironmentPack : PackVersion;
	if (Version < MinVersion)
	{
		Error = TEXT("The available DATs provide less artwork than the installed import.");
		return false;
	}
	uint32 Count = Src.Records.Num();
	Writer << Version << Count;
	for (auto& Pair : Src.Records)
	{
		uint32 Id = Pair.Key;
		int32 Size = Pair.Value.Num();
		Writer << Id << Size;
		Writer.Serialize(Pair.Value.GetData(), Size);
	}
	uint32 Crc = FCrc::MemCrc32(Pack.GetData(), Pack.Num());
	Writer << Crc;
	auto& Files = IFileManager::Get();
	const FString Dir = FPaths::GetPath(Destination);
	Files.MakeDirectory(*Dir, true);
	const FString Temp = FPaths::CreateTempFilename(*Dir, TEXT("legions-"), TEXT(".tmp"));
	if (FFileHelper::SaveArrayToFile(Pack, *Temp) && PublishCache(Temp, Destination, Error))
	{
		if (PublishedCrc) *PublishedCrc = Crc;
		return true;
	}
	Files.Delete(*Temp, false, true);
	Error = TEXT("The imported sky could not be saved.");
	return false;
}
// Keep the local path outside the transferable .sky file. Bind the receipt
// to its pack so restoring/copying a different pack invalidates the stamp.
bool ImportFolder(const FString& Folder, const FString& Destination, FString& Error, uint32 MinVersion = 0)
{
	uint32 Crc = 0;
	if (!ImportArchives(Folder / TEXT("portal.dat"), Folder / TEXT("highres.dat"), Destination, Error, MinVersion, &Crc))
		return false;
	const FString Receipt = FString::Printf(TEXT("%d\n%08X\n%s"), ArtworkRecipe, Crc, *FPaths::ConvertRelativePathToFull(Folder));
	const FString Temp = FPaths::CreateTempFilename(*FPaths::GetPath(Destination), TEXT("source-"));
	auto& Files = IFileManager::Get();
	if (!FFileHelper::SaveStringToFile(Receipt, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
		!Files.Move(*(Destination + TEXT(".import")), *Temp, true))
	{
		Files.Delete(*Temp, false);
		UE_LOG(LogTemp, Warning, TEXT("AC2 artwork imported, but its source folder could not be saved."));
	}
	return true;
}
}

bool Import(const FString& DirectoryOrFile, const FString& Destination, FString& Error)
{
	FString Folder = DirectoryOrFile.TrimStartAndEnd().TrimQuotes();
	FPaths::NormalizeDirectoryName(Folder);
	if (FPaths::GetCleanFilename(Folder).Equals(TEXT("portal.dat"), ESearchCase::IgnoreCase))
		Folder = FPaths::GetPath(Folder);
	if (Folder.IsEmpty())
	{
		Error = TEXT("Choose an AC2 Legions folder containing portal.dat.");
		return false;
	}
	FScopeLock Lock(&ImportMutex);
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	FSystemWideCriticalSection FileLock(ImportLockName(Destination), FTimespan::FromSeconds(30));
	if (!FileLock.IsValid()) { Error = TEXT("Another client is importing artwork. Try again shortly."); return false; }
#endif
	return ImportFolder(Folder, Destination, Error);
}

FString StagedPath(const FString& Destination)
{
	return FPaths::GetPath(Destination) / TEXT("Import/Legions.source");
}

bool ImportStaged(const FString& Destination, FString& Error)
{
	FScopeLock Lock(&ImportMutex);
	auto& Files = IFileManager::Get();
	const FString Source = StagedPath(Destination);
	uint32 Version = 0;
	{
		TUniquePtr<FArchive> Reader(Files.CreateFileReader(*Source));
		if (!Reader || Reader->TotalSize() < sizeof(Version))
		{
			Error = TEXT("Transfer your AC2 sky file from the PC again.");
			return false;
		}
		*Reader << Version;
	}
	if (HasOutdoorArt(Version) || Version == PackVersion || Version == CloudPack || Version == LegacyPack)
	{
		FBackdrop Sky;
		FEnvironment Environment;
		if (!Load(Source, Sky, Error) || (HasOutdoorArt(Version) && !LoadEnvironment(Source, AllEnvironment, Environment)))
		{
			Error = TEXT("Invalid sky cache. Import your AC2 portal.dat or transfer a fresh Legions.sky.");
			return false;
		}
		return PublishCache(Source, Destination, Error);
	}
	if (!ImportArchives(Source, FString(), Destination, Error))
		return false;
	if (!Files.Delete(*Source))
		UE_LOG(LogTemp, Warning, TEXT("AC2 sky installed, but its transfer file could not be removed."));
	return true;
}

static bool ReadPack(const FString& Filename, FSource& Src, uint32& Version, uint32& Crc)
{
	TArray<uint8> Pack;
	{
		FScopeLock Lock(&CacheMutex);
		const FString Path = IFileManager::Get().FileExists(*Filename) ? Filename : Filename + TEXT(".previous");
		const int64 Size = IFileManager::Get().FileSize(*Path);
		if (Size < 12 || Size > MaxPack + MaxRecords * 8 + 12 || !FFileHelper::LoadFileToArray(Pack, *Path) || Pack.Num() != Size)
			return false;
	}
	FReader R{Pack};
	R.Pos = Pack.Num() - 4;
	Crc = FCrc::MemCrc32(Pack.GetData(), R.Pos);
	if (Crc != R.Word())
		return false;
	R.Pos = 0;
	Version = R.Word();
	if (!HasOutdoorArt(Version) && Version != PackVersion && Version != CloudPack && Version != LegacyPack)
		return false;
	const int32 N = R.Count(MaxRecords);
	for (int32 I = 0; I < N; ++I)
	{
		const uint32 Id = R.Word();
		const auto Bytes = R.Take(R.Count(MaxRecord));
		if (!R.Ok || Src.Records.Contains(Id))
			return false;
		Src.Records.Add(Id, TArray<uint8>(Bytes));
	}
	return R.Ok && R.Pos == Pack.Num() - 4;
}

bool RefreshImport(const FString& Destination, const FString& RetailFolder)
{
	FScopeLock Lock(&ImportMutex);
	auto& Files = IFileManager::Get();
	if (!Files.FileExists(*Destination) && !Files.FileExists(*(Destination + TEXT(".previous")))) return false;
	const FDateTime InitialStamp = Files.GetTimeStamp(*Destination);
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	FSystemWideCriticalSection FileLock(ImportLockName(Destination), FTimespan::FromSeconds(30));
	if (!FileLock.IsValid()) return false;
#endif
	const FString Actual = Files.FileExists(*Destination) ? Destination : Destination + TEXT(".previous");
	TArray<FString> Receipt;
	FFileHelper::LoadFileToStringArray(Receipt, *(Destination + TEXT(".import")));
	if (Receipt.Num() == 3 && Receipt[0] == LexToString(ArtworkRecipe))
	{
		TUniquePtr<FArchive> Reader(Files.CreateFileReader(*Actual));
		if (Reader && Reader->TotalSize() >= 12)
		{
			uint32 Crc = 0;
			Reader->Seek(Reader->TotalSize()-4);
			*Reader << Crc;
			if (!Reader->IsError() && Receipt[1] == FString::Printf(TEXT("%08X"), Crc))
			{
				if (Files.GetTimeStamp(*Destination) == InitialStamp) return false;
				// A second client completed the refresh while this one waited.
				++ImportRevision;
				return true;
			}
		}
	}
	FSource Old;
	uint32 Version, Crc;
	if (!ReadPack(Destination, Old, Version, Crc) || Old.Records.IsEmpty()) return false;

	TArray<FString> Candidates;
	auto Add = [&](FString Folder)
	{
		FPaths::NormalizeDirectoryName(Folder);
		if (!Folder.IsEmpty() && Files.FileExists(*(Folder / TEXT("portal.dat")))) Candidates.AddUnique(Folder);
	};
	if (Receipt.Num() == 3) Add(Receipt[2]);
#if PLATFORM_WINDOWS
	// The old Windows picker used the application's default dialog state.
	// Ask that picker for its startup folder without opening a window.
	const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	IFileOpenDialog* Dialog = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
	{
		IShellItem* Item = nullptr;
		if (SUCCEEDED(Dialog->GetFolder(&Item)))
		{
			PWSTR Path = nullptr;
			if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &Path))) { Add(Path); CoTaskMemFree(Path); }
			Item->Release();
		}
		Dialog->Release();
	}
	if (SUCCEEDED(Init)) CoUninitialize();
#endif
	auto Neighbours = [&](const FString& Parent)
	{
		if (Parent.IsEmpty()) return;
		TArray<FString> Names;
		Files.FindFiles(Names, *(Parent / TEXT("*")), false, true);
		for (const auto& Name : Names) Add(Parent / Name);
	};
	if (!RetailFolder.IsEmpty())
	{
		const FString Retail = FPaths::ConvertRelativePathToFull(RetailFolder);
		Add(Retail);
		Neighbours(Retail);
		Neighbours(FPaths::GetPath(Retail));
	}
#if PLATFORM_WINDOWS
	Neighbours(TEXT("C:/Turbine"));
	Neighbours(FString(FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramFiles(x86)"))) / TEXT("Turbine"));
#endif
	for (const FString& Folder : Candidates)
	{
		FArchiveFile Portal, High;
		if (!Portal.Open(Folder / TEXT("portal.dat"))) continue;
		const bool HasHigh = High.Open(Folder / TEXT("highres.dat"));
		// Match every previously imported record, including optional high-res
		// mips. A different or incomplete installation cannot replace the pack.
		bool Match = true;
		for (const auto& Record : Old.Records)
		{
			TArray<uint8> Bytes;
			if (HasHigh && (Record.Key >> 24) == 0x41 && High.Read(Record.Key,Bytes) && Bytes == Record.Value) continue;
			if (Portal.Read(Record.Key,Bytes) && Bytes == Record.Value) continue;
			Match = false;
			break;
		}
		if (Match)
		{
			FString Error;
			if (ImportFolder(Folder, Destination, Error, Version))
			{
				UE_LOG(LogTemp, Display, TEXT("Automatically refreshed AC2 artwork from its DATs."));
				return true;
			}
			UE_LOG(LogTemp, Warning, TEXT("AC2 artwork refresh: %s"), *Error);
		}
	}
	return false;
}

bool LoadEnvironment(const FString& Filename, uint8 Selected, FEnvironment& Out)
{
	Out = {};
	FEnvironment Result;
	FSource Src;
	uint32 Version;
	if (!ReadPack(Filename, Src, Version, Result.ContentCrc) || !HasOutdoorArt(Version)) return false;
	if (Version != SceneryPack) Selected &= ~Boulders;
	if (!DecodeEnvironment(Src, Selected, Result) ||
	    ((Selected & Boulders) && !DecodeBoulders(Src, Result)) ||
	    (Version == SceneryPack && (Selected & Trees) && !DecodeTree(Src, Result, LegionsStumpMesh, true)) ||
	    (HasDetail(Version) && (Selected & (Ground | Grass)) && Result.GroundDetail.Mips.IsEmpty()) ||
	    (HasMeadow(Version) && !DecodeGrassCards(Src, Result)) ||
	    (HasYaraq(Version) && (Selected & Trees) && !DecodeYaraqTrees(Src, Result)) ||
	    (HasDereth(Version) && (Selected & Trees) && !DecodeDerethTrees(Src, Result))) return false;
	Out = MoveTemp(Result);
	return true;
}

namespace
{
FCriticalSection EnvironmentMutex;
FEnvironmentPtr CurrentEnvironment = MakeShared<FEnvironment, ESPMode::ThreadSafe>();
}

FEnvironmentPtr ActiveEnvironment()
{
	FScopeLock Lock(&EnvironmentMutex);
	return CurrentEnvironment;
}

void RefreshEnvironment()
{
	check(IsInGameThread());
	const uint8 Selected = (ACERuntimeOptions::Get(TEXT("LegionsGround")) > .5f ? Ground : 0) |
		(ACERuntimeOptions::Get(TEXT("LegionsGrass")) > .5f ? Grass : 0) |
		(ACERuntimeOptions::Get(TEXT("LegionsTrees")) > .5f ? Trees : 0) |
		(ACERuntimeOptions::Get(TEXT("LegionsBoulders")) > .5f ? Boulders : 0);
	const bool WantCards = ACERuntimeOptions::Get(TEXT("LegionsGroundCover")) > .5f;
	const int32 Import = Revision();
	static int32 LastImport = INDEX_NONE, LastSelection = INDEX_NONE;
	const int32 Selection = Selected | (WantCards ? 16 : 0);
	if (LastImport == Import && LastSelection == Selection) return;
	LastImport = Import;
	LastSelection = Selection;
	auto Next = MakeShared<FEnvironment, ESPMode::ThreadSafe>();
	if ((Selected || WantCards) && !LoadEnvironment(CachePath(), Selected, *Next))
		UE_LOG(LogTemp, Warning, TEXT("AC2 outdoor artwork unavailable. Reimport the Legions DAT in Graphics Options."));
	if (!WantCards) Next->GrassCards = {};
	UE_LOG(LogTemp, Display, TEXT("AC2 artwork active: ground=%d grass textures=%d trees=%d boulders=%d ground cover=%dx%d"),
		!!(Next->Enabled & Ground), !!(Next->Enabled & Grass), !!(Next->Enabled & Trees), !!(Next->Enabled & Boulders), Next->GrassCards.Width, Next->GrassCards.Height);
	FScopeLock Lock(&EnvironmentMutex);
	CurrentEnvironment = Next;
}

bool Load(const FString& Filename, FBackdrop& Sky, FString& Error)
{
	Sky = {};
	Error = TEXT("Import the AC2 Legions DAT in Graphics Options.");
	FSource Src;
	uint32 Version, Crc;
	if (!ReadPack(Filename, Src, Version, Crc) ||
	    !Decode(Src, Sky, Version != LegacyPack, Version == PackVersion || HasOutdoorArt(Version)))
		return false;
	Error.Reset();
	return true;
}
}
