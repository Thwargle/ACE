#include "Personal/PersonalFarTerrainDetail.h"
#include "Personal/PersonalFarTerrainBake.h"
#include "ACERuntimeOptions.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	constexpr uint32 FarDetailCacheMagic = 0x44464150; // "PAFD"
	constexpr uint32 FarDetailCacheVersion = 2;
	constexpr int32 FarDetailBlockCells = FPersonalFarTerrainBake::CellsPerLandblock;
	/** Pixels across one landblock at mip 0. */
	constexpr int32 FarDetailBlockPixels = FarDetailBlockCells * PersonalFarTerrainDetail::CellPixels;

	int32 FarDetailMod(int32 Value, int32 Step) { return ((Value % Step) + Step) % Step; }

	/** DAT handles of its own, like the world bake: the game's are not public. */
	struct FFarDetailDatSource
	{
		FString Directory;
		ACELegionsSky::FEnvironmentPtr Artwork;
		FACEDatDatabase Portal, Cell, HighRes;
		TUniquePtr<FACEDatTextureResolver> Textures;
		TUniquePtr<FACELandblockMeshBuilder> Builder;
		bool bTried = false;

		bool Bake(uint32 PalCode, TArray<FColor>& OutPixels, int32& OutSize, int32& OutCellsAcross)
		{
			if (!bTried)
			{
				bTried = true;
				if (Portal.Open(Directory / TEXT("client_portal.dat")) && Cell.Open(Directory / TEXT("client_cell_1.dat")))
				{
					const bool HasHighRes = HighRes.Open(Directory / TEXT("client_highres.dat"));
					Textures = MakeUnique<FACEDatTextureResolver>(&Portal, HasHighRes ? &HighRes : nullptr);
					Builder = MakeUnique<FACELandblockMeshBuilder>(&Portal, &Cell, Textures.Get(), nullptr, Artwork);
					if (!Builder->EnsureTexMerge())
					{
						Builder.Reset();
					}
				}
			}
			int32 Height = 0;
			OutCellsAcross = Artwork->TerrainTileCells(PalCode);
			return Builder && Builder->BakePCode(PalCode, OutPixels, OutSize, Height,OutCellsAcross) && Height == OutSize;
		}
	};
}

namespace PersonalFarTerrainDetail
{
	bool Enabled()
	{
		return ACERuntimeOptions::Get(TEXT("PersonalFarTerrainTextures")) > 0.5f;
	}

	FIntRect MeshCells(FIntPoint TextureCenter)
	{
		return FIntRect((TextureCenter - FIntPoint(MeshRings, MeshRings)) * FarDetailBlockCells,
			(TextureCenter + FIntPoint(MeshRings + 1, MeshRings + 1)) * FarDetailBlockCells);
	}

	bool Covers(FIntPoint TextureCenter, FIntPoint MeshCenter)
	{
		return FMath::Abs(TextureCenter.X - MeshCenter.X) <= 1 && FMath::Abs(TextureCenter.Y - MeshCenter.Y) <= 1;
	}

	FPersonalFarTerrainDetail::FBakeTile MakeDatTileSource(const FString& DatDirectory)
	{
		TSharedRef<FFarDetailDatSource, ESPMode::ThreadSafe> Source = MakeShared<FFarDetailDatSource, ESPMode::ThreadSafe>();
		Source->Directory = DatDirectory;
		Source->Artwork = ACELegionsSky::ActiveEnvironment();
		return [Source](uint32 PalCode, TArray<FColor>& OutPixels, int32& OutSize, int32& OutCellsAcross)
		{
			return Source->Bake(PalCode, OutPixels, OutSize,OutCellsAcross);
		};
	}
}

TConstArrayView<FColor> FPersonalFarTerrainDetail::Tile(const FPersonalFarTerrainBake& Bake, int32 CellX, int32 CellY, const FBakeTile& BakeTile)
{
	using namespace PersonalFarTerrainDetail;
	const uint32 PalCode = Bake.PalCodeAt(CellX, CellY);
	auto Phase = [&](const TArray<FColor>& Tile) -> TConstArrayView<FColor>
	{
		const int32 N = ACELegionsSky::FEnvironment::TerrainCellsAcross;
		const int32 Index = Tile.Num() == TilePixels ? 0 : FarDetailMod(CellY,N)*N+FarDetailMod(CellX,N);
		return MakeArrayView(Tile).Slice(Index*TilePixels,TilePixels);
	};
	if (const auto* Found = Tiles.Find(PalCode)) return Phase(*Found);
	TArray<FColor> Pixels;
	int32 Size = 0, Cells = 1;
	if (!BakeTile || !BakeTile(PalCode,Pixels,Size,Cells) ||
		(Cells != 1 && Cells != ACELegionsSky::FEnvironment::TerrainCellsAcross) ||
		Size < CellPixels*Cells || !FMath::IsPowerOfTwo(Size) || Pixels.Num() != int64(Size)*Size)
	{
		Cells = 1;
		Size = CellPixels;
		Pixels.Init(Bake.ColorAt(CellX,CellY),Size*Size);
	}
	TArray<FColor> Smaller;
	while (Size/2 >= CellPixels*Cells)
	{
		FACEDatTextureResolver::BuildOpaqueMip(Pixels,Size,Size,Smaller);
		Pixels = MoveTemp(Smaller);
		Size /= 2;
	}
	TArray<FColor>& Out = Tiles.Add(PalCode);
	Out.Reserve(TilePixels*Cells*Cells);
	// Reduce the shared world-aligned texture once. Keep the 16px patch for
	// each phase so the shell agrees with the loaded cells across block edges.
	for (int32 Y=0; Y<Cells; ++Y)
		for (int32 X=0; X<Cells; ++X)
		{
			TArray<FColor> Patch;
			Patch.Reserve(CellPixels*CellPixels);
			for (int32 Row=0; Row<CellPixels; ++Row)
				for (int32 Col=0; Col<CellPixels; ++Col)
				{
					const int32 SX = (2*(X*CellPixels+Col)+1)*Size/(2*Cells*CellPixels);
					const int32 SY = (2*(Y*CellPixels+Row)+1)*Size/(2*Cells*CellPixels);
					auto Color=Pixels[(Size-1-SY)*Size+Size-1-SX];
					Color.A=255;
					Patch.Add(Color);
				}
			for (int32 Side=CellPixels; ; Side/=2)
			{
				Out.Append(Patch);
				if (Side==1) break;
				FACEDatTextureResolver::BuildOpaqueMip(Patch,Side,Side,Smaller);
				Patch=MoveTemp(Smaller);
			}
		}
	++Unsaved;
	return Phase(Out);
}

void FPersonalFarTerrainDetail::Update(const FPersonalFarTerrainBake& Bake, FIntPoint Center, const FBakeTile& BakeTile, TArray<FUpload>& Out)
{
	using namespace PersonalFarTerrainDetail;
	Out.Reset();
	if (!Bake.IsValid())
	{
		return;
	}
	if (SlotBlock.Num() == 0)
	{
		SlotBlock.Init(FIntPoint(MIN_int32, MIN_int32), BlocksAcross * BlocksAcross);
		LastCellMip.Init(FColor::Black, CellsAcross * CellsAcross);
	}

	const FIntPoint First = Center - FIntPoint(TextureRings, TextureRings);
	TArray<FIntPoint> Changed;
	for (int32 X = 0; X < BlocksAcross; ++X)
	{
		for (int32 Y = 0; Y < BlocksAcross; ++Y)
		{
			const FIntPoint Block = First + FIntPoint(X, Y);
			if (SlotBlock[FarDetailMod(Block.X, BlocksAcross) * BlocksAcross + FarDetailMod(Block.Y, BlocksAcross)] != Block)
			{
				Changed.Add(Block);
			}
		}
	}
	if (Changed.Num() == 0)
	{
		return;
	}
	// A large change is sent as whole mips, so everything is written again.
	const bool bWhole = Changed.Num() > BlocksAcross * BlocksAcross / 4;
	if (bWhole)
	{
		Changed.Reset();
		for (int32 X = 0; X < BlocksAcross; ++X)
		{
			for (int32 Y = 0; Y < BlocksAcross; ++Y) Changed.Add(First + FIntPoint(X, Y));
		}
		Out.SetNum(CellMips);
		for (int32 Mip = 0; Mip < CellMips; ++Mip)
		{
			FUpload& Upload = Out[Mip];
			Upload.Mip = Mip;
			Upload.Width = Upload.Height = TextureSize >> Mip;
			Upload.Pixels.SetNumUninitialized(Upload.Width * Upload.Height);
		}
	}
	else
	{
		Out.Reserve(Changed.Num() * CellMips + MipCount - CellMips);
	}

	for (const FIntPoint Block : Changed)
	{
		const FIntPoint Slot(FarDetailMod(Block.X, BlocksAcross), FarDetailMod(Block.Y, BlocksAcross));
		SlotBlock[Slot.X * BlocksAcross + Slot.Y] = Block;
		// Where this landblock's pixels go, per cell mip.
		FColor* Target[CellMips];
		int32 Stride[CellMips];
		for (int32 Mip = 0; Mip < CellMips; ++Mip)
		{
			const int32 BlockPixels = FarDetailBlockPixels >> Mip;
			if (bWhole)
			{
				Stride[Mip] = Out[Mip].Width;
				Target[Mip] = Out[Mip].Pixels.GetData() + Slot.Y * BlockPixels * Stride[Mip] + Slot.X * BlockPixels;
			}
			else
			{
				FUpload& Upload = Out.AddDefaulted_GetRef();
				Upload.Mip = Mip;
				Upload.X = Slot.X * BlockPixels;
				Upload.Y = Slot.Y * BlockPixels;
				Upload.Width = Upload.Height = BlockPixels;
				Upload.Pixels.SetNumUninitialized(BlockPixels * BlockPixels);
				Stride[Mip] = BlockPixels;
				Target[Mip] = Upload.Pixels.GetData();
			}
		}
		for (int32 CX = 0; CX < FarDetailBlockCells; ++CX)
		{
			for (int32 CY = 0; CY < FarDetailBlockCells; ++CY)
			{
				const FColor* Source = Tile(Bake, Block.X * FarDetailBlockCells + CX, Block.Y * FarDetailBlockCells + CY, BakeTile).GetData();
				for (int32 Mip = 0; Mip < CellMips; ++Mip)
				{
					const int32 Size = CellPixels >> Mip;
					for (int32 Row = 0; Row < Size; ++Row)
					{
						FMemory::Memcpy(Target[Mip] + (CY * Size + Row) * Stride[Mip] + CX * Size, Source + Row * Size, Size * sizeof(FColor));
					}
					Source += Size * Size;
				}
				LastCellMip[(Slot.Y * FarDetailBlockCells + CY) * CellsAcross + Slot.X * FarDetailBlockCells + CX] = Source[-1];
			}
		}
	}

	// The mips smaller than one pixel per cell mix cells: make them again from that mip.
	TArray<FColor> Pixels = LastCellMip, Smaller;
	int32 Size = CellsAcross;
	for (int32 Mip = CellMips; Mip < MipCount && Size > 1; ++Mip)
	{
		FACEDatTextureResolver::BuildOpaqueMip(Pixels, Size, Size, Smaller);
		Pixels = MoveTemp(Smaller);
		Size /= 2;
		FUpload& Upload = Out.AddDefaulted_GetRef();
		Upload.Mip = Mip;
		Upload.Width = Upload.Height = Size;
		Upload.Pixels = Pixels;
	}
}

FString FPersonalFarTerrainDetail::TileCachePath(uint64 Key)
{
	return FPaths::ProjectSavedDir() / TEXT("Personal") / FString::Printf(TEXT("FarTerrainTiles_%016llX.bin"), Key);
}

bool FPersonalFarTerrainDetail::SaveTiles(const FString& Path, uint64 Key)
{
	TArray<uint8> Bytes;
	Bytes.Reserve(32 + Tiles.Num() * (sizeof(uint32) + TilePixels * sizeof(FColor)));
	FMemoryWriter Writer(Bytes);
	uint32 Magic = FarDetailCacheMagic, Version = FarDetailCacheVersion, Pixels = TilePixels, Count = Tiles.Num();
	Writer << Magic << Version << Key << Pixels << Count;
	for (const auto& Pair : Tiles)
	{
		uint32 Code = Pair.Key, Length = Pair.Value.Num();
		Writer << Code << Length;
		Writer.Serialize(const_cast<FColor*>(Pair.Value.GetData()), Length * sizeof(FColor));
	}
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
	{
		return false;
	}
	Unsaved = 0;
	return true;
}

bool FPersonalFarTerrainDetail::LoadTiles(const FString& Path, uint64 Key)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
	{
		return false;
	}
	FMemoryReader Reader(Bytes);
	uint32 Magic = 0, Version = 0, Pixels = 0, Count = 0;
	uint64 StoredKey = 0;
	Reader << Magic << Version << StoredKey << Pixels << Count;
	const int64 MinEntryBytes = (Version==1 ? 1 : 2)*sizeof(uint32) + TilePixels * sizeof(FColor);
	if (Reader.IsError() || Magic != FarDetailCacheMagic || (Version != 1 && Version != FarDetailCacheVersion) || StoredKey != Key
		|| Pixels != TilePixels || Bytes.Num() - Reader.Tell() < int64(Count) * MinEntryBytes)
	{
		return false;
	}
	TMap<uint32,TArray<FColor>> Loaded;
	for (uint32 I = 0; I < Count; ++I)
	{
		uint32 Code = 0, Length = TilePixels;
		Reader << Code;
		if (Version >= 2) Reader << Length;
		constexpr int32 N = ACELegionsSky::FEnvironment::TerrainCellsAcross;
		if (Reader.IsError() || (Length != TilePixels && Length != TilePixels*N*N) ||
			int64(Length)*sizeof(FColor) > uint64(Bytes.Num()-Reader.Tell()) || Loaded.Contains(Code)) return false;
		TArray<FColor>& Tile = Loaded.Add(Code);
		Tile.SetNumUninitialized(Length);
		Reader.Serialize(Tile.GetData(), Length * sizeof(FColor));
	}
	if (Reader.IsError() || Reader.Tell() != Bytes.Num()) return false;
	Tiles=MoveTemp(Loaded);
	Unsaved = 0;
	return true;
}
