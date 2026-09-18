#include "ACEWorldBakeLandscape.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeCellFile.h"
#include "ACEWorldBakeDatRegion.h"
#include "Engine/Texture2D.h"
#include "Exporters/Exporter.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "LandscapeDataAccess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "ACEWorldBakeLandscape"

const FString kLandblocksSaveDirectory = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("/Landblocks/"));
const uint32 kDimensionLessOne = FACEWorldBakeCellLandblock::kDimension - 1;
const uint32 kLandscapeSize = 2048;
const uint32 kLandscapeTilePoles = 256;
const uint32 kLandscapeTilePolesMinus1 = kLandscapeTilePoles - 1;
const uint32 kLandscapeComponentPoles = 16;
const uint32 kLandscapeComponentPolesMinus1 = kLandscapeComponentPoles - 1;
const uint32 kLandscapeMaterialSamplers = 3;
const uint32 kLandscapeMaterialSamplerChannels = 4;
const uint32 kLandscapeMaterialChannels = kLandscapeMaterialSamplers * kLandscapeMaterialSamplerChannels;
const uint32 kLandscapeMaterialSamplersMax = 10;

uint32 GetHash(const TArray<uint8>& Elements)
{
	uint32_t Hash = 0;

	for (int32 i = 0; i < Elements.Num(); ++i)
	{
		Hash += Elements[i] + 1;
		Hash += (Hash << 10);
		Hash ^= (Hash >> 6);
	}

	Hash += (Hash << 3);
	Hash ^= (Hash >> 11);
	Hash += (Hash << 15);

	return Hash;
}

uint32 GetUniqueHash(const TArray<uint8>& Elements)
{
	TSet<uint8> UniqueElements;
	UniqueElements.Append(Elements);
	return GetHash(UniqueElements.Array());
}

FString FACEWorldBakeLandscapeWriter::ExportLandscapeImages()
{
	return FString();
}

void SaveHeightmapToImage(const TArray<uint16>& Raw, float MaxHeight, uint32 Width, uint32 Height, const FString& FileName)
{
	TArray<FColor> pixels;
	pixels.AddUninitialized(Width * Height);

	for (int32 pixelIndex = 0; pixelIndex < pixels.Num(); ++pixelIndex)
	{
		const uint16* const rawHeightPtr = &Raw[pixelIndex];
		uint16 heightValue = *rawHeightPtr;
		double f = static_cast<double>(heightValue) / MaxHeight;

		FColor pixel = FColor::Black;
		pixel.R = pixel.G = pixel.B = (f * MAX_uint8 * 1.0);
		pixel.A = 255;

		pixels[pixelIndex] = pixel;
	}

	FCreateTexture2DParameters texParams;
	texParams.bUseAlpha = false;
	texParams.CompressionSettings = TC_Grayscale;
	texParams.bDeferCompression = false;
	texParams.bSRGB = false;

	const FString FilePath = FString::Printf(TEXT("%s/%s_All.tga"), *kLandblocksSaveDirectory, *FileName);
	UTexture2D* texture = FImageUtils::CreateTexture2D(Width, Height, pixels, GetTransientPackage(), FilePath, RF_Public | RF_Standalone, texParams);
	UExporter::ExportToFile(texture, NULL, *FilePath, false);

	TArray<uint8> UnrealTileRaw;
	UnrealTileRaw.AddUninitialized(Raw.Num() * 2);
	FMemory::Memcpy(UnrealTileRaw.GetData(), Raw.GetData(), UnrealTileRaw.Num());

	const FString HeightFilePath = FString::Printf(TEXT("%s/%s_All.r16"), *kLandblocksSaveDirectory, *FileName);
	FFileHelper::SaveArrayToFile(UnrealTileRaw, *HeightFilePath);
}

void FACEWorldBakeLandscapeWriter::GenerateLandscapeArrays(FScopedSlowTask& SlowTask, const FACEWorldBakeCellFile& CellFile, const FACEWorldBakePortalRegion& Region, TArray<uint16>& OutHeights, TArray<FColor>& OutColors, TArray<uint8>& OutRegions, TArray<uint8>& OutRoads)
{
	OutHeights.Empty();
	OutHeights.AddUninitialized(kLandscapeSize * kLandscapeSize);
	FMemory::Memset(OutHeights.GetData(), 0, kLandscapeSize * kLandscapeSize * sizeof(uint16));

	OutColors.Empty();
	OutColors.AddUninitialized(kLandscapeSize * kLandscapeSize);
	FMemory::Memset(OutColors.GetData(), 0x00, kLandscapeSize * kLandscapeSize * sizeof(FColor));

	OutRegions.Empty();
	OutRegions.AddUninitialized(kLandscapeSize * kLandscapeSize);
	FMemory::Memset(OutRegions.GetData(), 0xFF, kLandscapeSize * kLandscapeSize * sizeof(uint8));

	OutRoads.Empty();
	OutRoads.AddUninitialized(kLandscapeSize * kLandscapeSize);
	FMemory::Memset(OutRoads.GetData(), 0x00, kLandscapeSize * kLandscapeSize * sizeof(uint8));

	for (uint32 LandblockY = 0; LandblockY < FACEWorldBakeCellFile::kHeightInLandblocks; ++LandblockY)
	{
		for (uint32 LandblockX = 0; LandblockX < FACEWorldBakeCellFile::kWidthInLandblocks; ++LandblockX)
		{
			SlowTask.EnterProgressFrame();

			if (const FACEWorldBakeCellLandblock* const LandBlock = CellFile.GetLandblock(LandblockX, LandblockY))
			{
				TArray<uint8> LandblockUniqueRegionTerrains;
				if (false)
				{
					LandblockUniqueRegionTerrains = LandBlock->GetUniqueRegionTerrains().Array();

					FString UnqiueRegionTerrainsString;
					ArrayToString(LandblockUniqueRegionTerrains, UnqiueRegionTerrainsString);
					UE_LOG_ACEWORLDBAKEDATTOOLS(Log, TEXT("Landblock %d, %d unique terrains: %s"), LandblockX, LandblockY, *UnqiueRegionTerrainsString);
				}

				for (int32 Y = 0; Y < FACEWorldBakeCellLandblock::kDimension; ++Y)
				{
					const uint32 firstIndexToBlock = ((254 - LandblockY) * kLandscapeSize * kDimensionLessOne) + (LandblockX * kDimensionLessOne);
					const uint32 indexToFirstPixelInRow = firstIndexToBlock + (Y * kLandscapeSize);

					for (int32 X = 0; X < FACEWorldBakeCellLandblock::kDimension; ++X)
					{
						float RealHeightValue = LandBlock->GetHeightAt(X, kDimensionLessOne - Y, Region.HeightValues);
						check(0.0f <= RealHeightValue);
						float RealHeightValueWhole = 0.0f;
						check(0.0 == FMath::Modf(RealHeightValue, &RealHeightValueWhole)); // ensure we are not truncating data
						const uint16 heightValue = static_cast<uint16>(RealHeightValue);

						const uint32 pixelIndex = indexToFirstPixelInRow + X;
						uint16* const rawHeightPtr = &OutHeights[pixelIndex];
						*rawHeightPtr = heightValue;

						const uint8 RegionTerrain = LandBlock->GetRegionTerrain(X, kDimensionLessOne - Y);
						check(RegionTerrain < Region.TerrainTypes.Num());

						OutRegions[pixelIndex] = RegionTerrain;
						OutRoads[pixelIndex] = LandBlock->GetRoad(X, Y);

						FColor TerrainColor;
						TerrainColor.A = (Region.TerrainTypes[RegionTerrain].TerrainColor & 0xFF000000) >> 24;
						TerrainColor.R = (Region.TerrainTypes[RegionTerrain].TerrainColor & 0x00FF0000) >> 16;
						TerrainColor.G = (Region.TerrainTypes[RegionTerrain].TerrainColor & 0x0000FF00) >> 8;
						TerrainColor.B = (Region.TerrainTypes[RegionTerrain].TerrainColor & 0x000000FF) >> 0;
						OutColors[pixelIndex] = TerrainColor;
					}
				}
			}
			else
			{
				UE_LOG_ACEWORLDBAKEDATTOOLS(Warning, TEXT("SaveLandblockHeightmapsToImage() failed to get landblock %d, %d"), LandblockX, LandblockY);
			}
		}
	}
}

static void SortRegionsByChannel(TArray<uint8>& OccurenceSortedUniqueRegions, int32 ComponentX, int32 ComponentY)
{
	// start with max samplers of invalid (unused) regions
	TArray<uint8> SortedRegions;
	for (int32 Index = 0; Index < kLandscapeMaterialChannels; ++Index)
	{
		SortedRegions.Add(0xFF);
	}

	int32 HighestIndex = 0;

	TArray<uint8> UnsortedRegions; // regions that were not able to be sorted due to an occupied channel

	// first-come, first-serve reservation of channels
	for (uint8 Region : OccurenceSortedUniqueRegions)
	{
		const int32 Index = Region % kLandscapeMaterialChannels;
		if (SortedRegions[Index] == 0xFF)
		{
			SortedRegions[Index] = Region;
			HighestIndex = FMath::Max(HighestIndex, Index);
		}
		else
		{
			UnsortedRegions.Add(Region);
		}
	}

	// fill empty slots with unsorted regions
	if (0 < UnsortedRegions.Num())
	{
		for (uint8 Region : UnsortedRegions)
		{
			// see if the same channel on either sampler is available
			const int32 ChannelAlignedIndex0 = Region % kLandscapeMaterialChannels;
			if (SortedRegions.IsValidIndex(ChannelAlignedIndex0) && SortedRegions[ChannelAlignedIndex0] == 0xFF)
			{
				SortedRegions[ChannelAlignedIndex0] = Region;
				HighestIndex = FMath::Max(HighestIndex, ChannelAlignedIndex0);
				continue;
			}

			const int32 ChannelAlignedIndex1 = kLandscapeMaterialSamplerChannels * 1 + (Region % kLandscapeMaterialSamplerChannels);
			if (SortedRegions.IsValidIndex(ChannelAlignedIndex1) && SortedRegions[ChannelAlignedIndex1] == 0xFF)
			{
				SortedRegions[ChannelAlignedIndex1] = Region;
				HighestIndex = FMath::Max(HighestIndex, ChannelAlignedIndex1);
				continue;
			}

			const int32 ChannelAlignedIndex2 = kLandscapeMaterialSamplerChannels * 2 + (Region % kLandscapeMaterialSamplerChannels);
			if (SortedRegions.IsValidIndex(ChannelAlignedIndex2) && SortedRegions[ChannelAlignedIndex2] == 0xFF)
			{
				SortedRegions[ChannelAlignedIndex2] = Region;
				HighestIndex = FMath::Max(HighestIndex, ChannelAlignedIndex2);
				continue;
			}

			// fallback to first found
			for (int32 Index = 0; Index < kLandscapeMaterialChannels; ++Index)
			{
				if (SortedRegions[Index] == 0xFF)
				{
					SortedRegions[Index] = Region;
					HighestIndex = FMath::Max(HighestIndex, Index);
					break;
				}
			}
		}

		UE_LOG_ACEWORLDBAKEDATTOOLS(Verbose, TEXT("SortRegionSet() component %d, %d placed %d regions outside strict sort order (expected for some tiles)"), ComponentX, ComponentY, UnsortedRegions.Num());
	}

	OccurenceSortedUniqueRegions = SortedRegions;
}

static void ToUniqueRegions(const TArray<uint8>& Regions, const TMap<uint8, int32>& LandscapeComponentRegionCount, int32 ComponentX, int32 ComponentY, TArray<uint8>& UniqueRegionArray, TArray<uint8>& TruncatedRegions)
{
	// count the number of times each region appears
	TMap<uint8, int32> RegionCount;
	for (int32 Y = 0; Y < kLandscapeComponentPolesMinus1; ++Y)
	{
		for (int32 X = 0; X < kLandscapeComponentPolesMinus1; ++X)
		{
			const int32 Index = Y * kLandscapeComponentPolesMinus1 + X;
			RegionCount.FindOrAdd(Regions[Index])++;
		}
	}

	RegionCount.GetKeys(UniqueRegionArray);
	UniqueRegionArray.Remove(0xFF);

	// populate UniqueRegionArray with max supported regions
	{
		UniqueRegionArray.Sort([RegionCount](const uint8& Left, const uint8& Right)
		{
			return RegionCount.FindChecked(Left) > RegionCount.FindChecked(Right);
		});

		if (UniqueRegionArray.Num() > kLandscapeMaterialSamplersMax) // unsupported component, truncate the first N
		{
			for (int32 Index = kLandscapeMaterialSamplersMax; Index < UniqueRegionArray.Num(); ++Index)
			{
				TruncatedRegions.Add(UniqueRegionArray[Index]);
			}

			UniqueRegionArray.SetNum(kLandscapeMaterialSamplersMax, EAllowShrinking::Yes);

			UE_LOG_ACEWORLDBAKEDATTOOLS(Warning, TEXT("Landscape component X: %d, Y: %d, truncated %d regions"), ComponentX, ComponentY, TruncatedRegions.Num());
		}
	}

	UniqueRegionArray.Sort([LandscapeComponentRegionCount](const uint8& Left, const uint8& Right)
	{
		return LandscapeComponentRegionCount.FindChecked(Left) > LandscapeComponentRegionCount.FindChecked(Right);
	});

	SortRegionsByChannel(UniqueRegionArray, ComponentX, ComponentY);
}

void FACEWorldBakeLandscapeWriter::GenerateLandscapeComponentRegionSets(FScopedSlowTask& SlowTask, const TArray<uint8>& LandscapeRegions, TMap<uint8, int32>& OutLandscapeComponentRegionCount, TMap<uint32, TArray<uint8>>& OutLandscapeComponentRegionSets)
{
	OutLandscapeComponentRegionCount.Empty();
	OutLandscapeComponentRegionSets.Empty();

	TArray<uint8> LandscapeComponentRegions;
	LandscapeComponentRegions.AddUninitialized(kLandscapeComponentPolesMinus1 * kLandscapeComponentPolesMinus1);

	// produce a count of all regions for use with sorting
	{
		for (uint32 LandscapeComponentY = 0; LandscapeComponentY < 136; ++LandscapeComponentY)
		{
			const int32 IndexToFirstPixelInComponentRawColumn = LandscapeComponentY * kLandscapeSize * kLandscapeComponentPolesMinus1;

			for (uint32 LandscapeComponentX = 0; LandscapeComponentX < 136; ++LandscapeComponentX)
			{
				const int32 IndexToFirstPixelInComponentRawRow = IndexToFirstPixelInComponentRawColumn + (LandscapeComponentX * kLandscapeComponentPolesMinus1);

				for (int32 Y = 0; Y < kLandscapeComponentPolesMinus1; ++Y)
				{
					const int32 IndexToFirstPixelInPoleColumn = IndexToFirstPixelInComponentRawRow + (Y * kLandscapeSize);

					for (int32 X = 0; X < kLandscapeComponentPolesMinus1; ++X)
					{
						int32 LandscapeIndex = (IndexToFirstPixelInPoleColumn + X);
						check(LandscapeIndex < LandscapeRegions.Num());
						const uint8 RegionTerrain = LandscapeRegions[LandscapeIndex];
						OutLandscapeComponentRegionCount.FindOrAdd(RegionTerrain) += 1;
					}
				}
			}
		}
	}

	for (uint32 LandscapeComponentY = 0; LandscapeComponentY < 136; ++LandscapeComponentY)
	{
		const int32 IndexToFirstPixelInComponentRawColumn = LandscapeComponentY * kLandscapeSize * kLandscapeComponentPolesMinus1;
		const int32 IndexToFirstPixelInComponentColumn = LandscapeComponentY * kLandscapeTilePolesMinus1 * kLandscapeComponentPolesMinus1;

		for (uint32 LandscapeComponentX = 0; LandscapeComponentX < 136; ++LandscapeComponentX)
		{
			const int32 IndexToFirstPixelInComponentRawRow = IndexToFirstPixelInComponentRawColumn + (LandscapeComponentX * kLandscapeComponentPolesMinus1);
			const int32 IndexToFirstPixelInComponentRow = IndexToFirstPixelInComponentColumn + LandscapeComponentX * kLandscapeComponentPolesMinus1;

			for (int32 Y = 0; Y < kLandscapeComponentPolesMinus1; ++Y)
			{
				const int32 IndexToFirstPixelInPoleColumn = IndexToFirstPixelInComponentRawRow + (Y * kLandscapeSize);

				for (int32 X = 0; X < kLandscapeComponentPolesMinus1; ++X)
				{
					int32 LandscapeIndex = (IndexToFirstPixelInPoleColumn + X);
					check(LandscapeIndex < LandscapeRegions.Num());

					check(LandscapeIndex < LandscapeRegions.Num());
					const uint8 RegionTerrain = LandscapeRegions[LandscapeIndex];
					check((Y * kLandscapeComponentPolesMinus1 + X) < static_cast<uint32>(LandscapeComponentRegions.Num()));
					LandscapeComponentRegions[(Y * kLandscapeComponentPolesMinus1 + X)] = RegionTerrain;
				}
			}

			TArray<uint8> UniqueComponentRegionsArray;
			TArray<uint8> TruncatedRegions;
			ToUniqueRegions(LandscapeComponentRegions, OutLandscapeComponentRegionCount, LandscapeComponentX, LandscapeComponentY, UniqueComponentRegionsArray, TruncatedRegions);

			const uint32 UniqueComponentHash = GetHash(UniqueComponentRegionsArray);
			check(!OutLandscapeComponentRegionSets.Contains(UniqueComponentHash) || (TSet<uint8>(UniqueComponentRegionsArray).Difference(TSet<uint8>(OutLandscapeComponentRegionSets.FindChecked(UniqueComponentHash))).Num() == 0)); // ensure we do not have hashing collision
			OutLandscapeComponentRegionSets.Add(UniqueComponentHash, UniqueComponentRegionsArray);
		}
	}
}

void FACEWorldBakeLandscapeWriter::ExtractTileArrays(uint32 LandscapeTileX, uint32 LandscapeTileY, const TArray<uint16>& LandscapeHeights, const TArray<FColor>& LandscapeColors, const TArray<uint8>& LandscapeRegions, const TArray<uint8>& LandscapeRoads, TArray<uint8>& OutLandscapeTileHeights, TArray<FColor>& OutLandscapeTileColors, TArray<uint8>& OutLandscapeTileRegions, TArray<uint8>& OutLandscapeTileRoads)
{
	const int32 kLandscapeTileHeightCount = kLandscapeTilePoles * kLandscapeTilePoles * 2;
	if (OutLandscapeTileHeights.Num() != kLandscapeTileHeightCount)
	{
		OutLandscapeTileHeights.Empty();
		OutLandscapeTileHeights.AddUninitialized(kLandscapeTileHeightCount);
	}

	const int32 kLandscapeTileColorCount = kLandscapeTilePoles * kLandscapeTilePoles;
	if (OutLandscapeTileColors.Num() != kLandscapeTileColorCount)
	{
		OutLandscapeTileColors.Empty();
		OutLandscapeTileColors.AddUninitialized(kLandscapeTileColorCount);
	}

	const int32 kLandscapeTileRegionCount = kLandscapeTilePoles * kLandscapeTilePoles;
	if (OutLandscapeTileRegions.Num() != kLandscapeTileRegionCount)
	{
		OutLandscapeTileRegions.Empty();
		OutLandscapeTileRegions.AddUninitialized(kLandscapeTileRegionCount);
	}

	const int32 kLandscapeTileRoadCount = kLandscapeTilePoles * kLandscapeTilePoles;
	if (OutLandscapeTileRoads.Num() != kLandscapeTileRoadCount)
	{
		OutLandscapeTileRoads.Empty();
		OutLandscapeTileRoads.AddUninitialized(kLandscapeTileRoadCount);
	}

	const int32 IndexToFirstPixelInColumn = LandscapeTileY * kLandscapeSize * kLandscapeTilePolesMinus1;
	const int32 IndexToFirstPixelInRow = IndexToFirstPixelInColumn + (LandscapeTileX * kLandscapeTilePolesMinus1);

	for (int32 Y = 0; Y < kLandscapeTilePoles; ++Y)
	{
		const int32 IndexToFirstPixelInTileRow = IndexToFirstPixelInRow + (Y * kLandscapeSize);

		for (int32 X = 0; X < kLandscapeTilePoles; ++X)
		{
			int32 LandscapeIndex = (IndexToFirstPixelInTileRow + X);
			check(LandscapeIndex < LandscapeRegions.Num());
			uint16 LandscapeHeight = LandscapeHeights[LandscapeIndex];

			int32 LandscapeTileIndex = (Y * kLandscapeTilePoles + X) * 2;
			check(LandscapeTileIndex < OutLandscapeTileHeights.Num());
			uint16* const LandscapeTileHeightPtr = reinterpret_cast<uint16*>(&OutLandscapeTileHeights[LandscapeTileIndex]);

			(*LandscapeTileHeightPtr) = LandscapeHeight + static_cast<uint16>(LandscapeDataAccess::MidValue); // sea-level is at 0 on the import data

			check(LandscapeIndex < LandscapeColors.Num());
			OutLandscapeTileColors[(Y * kLandscapeTilePoles + X)] = LandscapeColors[LandscapeIndex];

			check(LandscapeIndex < LandscapeRegions.Num());
			const uint8 RegionTerrain = LandscapeRegions[LandscapeIndex];
			OutLandscapeTileRegions[(Y * kLandscapeTilePoles + X)] = RegionTerrain;
			const uint8 RegionRoad = LandscapeRoads[LandscapeIndex];
			OutLandscapeTileRoads[(Y * kLandscapeTilePoles + X)] = RegionRoad;
		}
	}
}

void FACEWorldBakeLandscapeWriter::ExtractTileHashesAndBlendMask(uint32 LandscapeTileX, uint32 LandscapeTileY, const TArray<uint8>& LandscapeRegions, const TMap<uint8, int32>& LandscapeComponentRegion_Count, const TMap<uint32, TArray<uint8>>& LandscapeComponentRegionSets, TArray<uint32>& OutLandscapeTileHashes, TArray<FColor>& OutLandscapeTileMasks0, TArray<FColor>& OutLandscapeTileMasks1, TArray<FColor>& OutLandscapeTileMasks2)
{
	OutLandscapeTileHashes.Empty();

	const int32 kLandscapeTileMaskCount = kLandscapeTilePolesMinus1 * kLandscapeTilePolesMinus1;
	if (OutLandscapeTileMasks0.Num() != kLandscapeTileMaskCount)
	{
		OutLandscapeTileMasks0.Empty();
		OutLandscapeTileMasks0.AddUninitialized(kLandscapeTileMaskCount);
	}

	if (OutLandscapeTileMasks1.Num() != kLandscapeTileMaskCount)
	{
		OutLandscapeTileMasks1.Empty();
		OutLandscapeTileMasks1.AddUninitialized(kLandscapeTileMaskCount);
	}

	if (OutLandscapeTileMasks2.Num() != kLandscapeTileMaskCount)
	{
		OutLandscapeTileMasks2.Empty();
		OutLandscapeTileMasks2.AddUninitialized(kLandscapeTileMaskCount);
	}

	const FColor kEmpty(0, 0, 0, 255);

	TArray<uint8> LandscapeComponentRegions;
	LandscapeComponentRegions.AddUninitialized(kLandscapeComponentPolesMinus1 * kLandscapeComponentPolesMinus1);

	for (uint32 LandscapeComponentY = 0; LandscapeComponentY < 17; ++LandscapeComponentY)
	{
		const int32 IndexToFirstPixelInComponentRawColumn = (LandscapeTileY * 17 + LandscapeComponentY) * kLandscapeSize * kLandscapeComponentPolesMinus1;
		const int32 IndexToFirstPixelInComponentColumn = LandscapeComponentY * kLandscapeTilePolesMinus1 * kLandscapeComponentPolesMinus1;

		for (uint32 LandscapeComponentX = 0; LandscapeComponentX < 17; ++LandscapeComponentX)
		{
			const int32 IndexToFirstPixelInComponentRawRow = IndexToFirstPixelInComponentRawColumn + ((LandscapeTileX * 17 + LandscapeComponentX) * kLandscapeComponentPolesMinus1);
			const int32 IndexToFirstPixelInComponentRow = IndexToFirstPixelInComponentColumn + LandscapeComponentX * kLandscapeComponentPolesMinus1;

			for (int32 Y = 0; Y < kLandscapeComponentPolesMinus1; ++Y)
			{
				const int32 IndexToFirstPixelInPoleColumn = IndexToFirstPixelInComponentRawRow + (Y * kLandscapeSize);

				for (int32 X = 0; X < kLandscapeComponentPolesMinus1; ++X)
				{
					int32 LandscapeIndex = (IndexToFirstPixelInPoleColumn + X);
					check(LandscapeIndex < LandscapeRegions.Num());

					check(LandscapeIndex < LandscapeRegions.Num());
					const uint8 RegionTerrain = LandscapeRegions[LandscapeIndex];
					check((Y * kLandscapeComponentPolesMinus1 + X) < static_cast<uint32>(LandscapeComponentRegions.Num()));
					LandscapeComponentRegions[(Y * kLandscapeComponentPolesMinus1 + X)] = RegionTerrain;
				}
			}

			TArray<uint8> UniqueComponentRegionsArray;
			TArray<uint8> TruncatedRegions;
			ToUniqueRegions(LandscapeComponentRegions, LandscapeComponentRegion_Count, (LandscapeTileX * 17 + LandscapeComponentX), (LandscapeTileY * 17 + LandscapeComponentY), UniqueComponentRegionsArray, TruncatedRegions);

			const uint32 UniqueComponentHash = GetHash(UniqueComponentRegionsArray);
			check(!LandscapeComponentRegionSets.Contains(UniqueComponentHash) || (TSet<uint8>(UniqueComponentRegionsArray).Difference(TSet<uint8>(LandscapeComponentRegionSets.FindChecked(UniqueComponentHash))).Num() == 0)); // ensure we do not have hashing collision
			OutLandscapeTileHashes.Add(UniqueComponentHash);

			const TArray<uint8> RegionArray = LandscapeComponentRegionSets.FindChecked(UniqueComponentHash);

			for (uint32 Y = 0; Y < kLandscapeComponentPolesMinus1; ++Y)
			{
				const int32 IndexToFirstPixelInPoleRawColumn = IndexToFirstPixelInComponentRawRow + (Y * kLandscapeSize);
				const int32 IndexToFirstPixelInPoleColumn = IndexToFirstPixelInComponentRow + (Y * kLandscapeTilePolesMinus1);

				for (uint32 X = 0; X < kLandscapeComponentPolesMinus1; ++X)
				{
					int32 TileIndex = (IndexToFirstPixelInPoleColumn + X);
					check((TileIndex < OutLandscapeTileMasks0.Num()) && (TileIndex < OutLandscapeTileMasks1.Num()) && (TileIndex < OutLandscapeTileMasks2.Num()));

					int32 LandscapeIndex = (IndexToFirstPixelInPoleRawColumn + X);
					check(LandscapeIndex < LandscapeRegions.Num());

					const uint8 RegionTerrain = LandscapeRegions[LandscapeIndex];
					const bool bTruncated = TruncatedRegions.Contains(RegionTerrain);
					const int32 RegionIndex = bTruncated ? 0 : RegionArray.Find(RegionTerrain);

					check(RegionArray.Contains(RegionTerrain) || bTruncated);

					FColor ColorForIndex(0, 0, 0, 0);
					switch (RegionIndex % 4) // RGB255-A
					{
					case -1: ColorForIndex = kEmpty; break;
					case 0: ColorForIndex = FColor(0xFF, 0, 0, 0xFF); break;
					case 1: ColorForIndex = FColor(0, 0xFF, 0, 0xFF); break;
					case 2: ColorForIndex = FColor(0, 0, 0xFF, 0xFF); break;
					case 3: ColorForIndex = FColor(0, 0, 0, 0); break;
					}

					if (RegionIndex < 4)
					{
						OutLandscapeTileMasks0[TileIndex] = ColorForIndex;
						OutLandscapeTileMasks1[TileIndex] = kEmpty;
						OutLandscapeTileMasks2[TileIndex] = kEmpty;
					}
					else if (RegionIndex < 8)
					{
						OutLandscapeTileMasks0[TileIndex] = kEmpty;
						OutLandscapeTileMasks1[TileIndex] = ColorForIndex;
						OutLandscapeTileMasks2[TileIndex] = kEmpty;
					}
					else if (RegionIndex < 12)
					{
						OutLandscapeTileMasks0[TileIndex] = kEmpty;
						OutLandscapeTileMasks1[TileIndex] = kEmpty;
						OutLandscapeTileMasks2[TileIndex] = ColorForIndex;
					}
				}
			}
		}
	}
}

void FACEWorldBakeLandscapeWriter::ExtrudeBlendMaskEdges(TArray<FColor>& LandscapeTileMasks0, TArray<FColor>& LandscapeTileMasks1, TArray<FColor>& LandscapeTileMasks2)
{
	/*
	for (uint32 LandscapeTilePoleY = 0; LandscapeTilePoleY < kLandscapeTilePolesMinus1; ++LandscapeTilePoleY)
	{
		for (uint32 LandscapeTilePoleX = 0; LandscapeTilePoleX < kLandscapeTilePolesMinus1; ++LandscapeTilePoleX)
		{
			int32 TileIndex = LandscapeTilePoleY * kLandscapeTilePolesMinus1 + LandscapeTilePoleX;
			check(LandscapeTileMasks0.IsValidIndex(TileIndex) && LandscapeTileMasks1.IsValidIndex(TileIndex) && LandscapeTileMasks2.IsValidIndex(TileIndex));

			const FColor& Mask0 = LandscapeTileMasks0[TileIndex];
			const uint8* MaskPtr0 = reinterpret_cast<const uint8*>(&Mask0);
			if (Mask0.B == 0xFF)
			{
				for (int32 SampleY = -1; SampleY < 2; ++SampleY)
				{
					const bool bOutOfBoundsY = ((0 == LandscapeTilePoleY) && (-1 == SampleY)) || (((kLandscapeTilePolesMinus1 - 1) == LandscapeTilePoleY) && (1 == SampleY));
					if (bOutOfBoundsY)
						continue;

					for (int32 SampleX = -1; SampleX < 2; ++SampleX)
					{
						const bool bIsCenter = (0 == SampleY) && (0 == SampleX);
						const bool bOutOfBoundsX = ((0 == LandscapeTilePoleX) && (-1 == SampleX)) || (((kLandscapeTilePolesMinus1 - 1) == LandscapeTilePoleX) && (1 == SampleX));
						if (bOutOfBoundsX || bIsCenter)
							continue;

						int32 TileIndexAdjacent = TileIndex + SampleY * kLandscapeTilePolesMinus1 + SampleX;
						check(LandscapeTileMasks0.IsValidIndex(TileIndexAdjacent) && LandscapeTileMasks1.IsValidIndex(TileIndexAdjacent) && LandscapeTileMasks2.IsValidIndex(TileIndexAdjacent));
						if (0 == LandscapeTileMasks0[TileIndexAdjacent].B)
						{
							LandscapeTileMasks0[TileIndexAdjacent].B = 0x7F;
						}
					}
				}
			}
		}
	}
	*/

	for (int32 MaskArray = 0; MaskArray < 3; ++MaskArray) // each mask array passed in
	{
		TArray<FColor>& CurrentMask = (0 == MaskArray) ? LandscapeTileMasks0 : (1 == MaskArray) ? LandscapeTileMasks1 : LandscapeTileMasks2;

		// extrude the mask channels
		for (uint32 LandscapeTilePoleY = 0; LandscapeTilePoleY < kLandscapeTilePolesMinus1; ++ LandscapeTilePoleY) // pixels of each mask
		{
			for (uint32 LandscapeTilePoleX = 0; LandscapeTilePoleX < kLandscapeTilePolesMinus1; ++LandscapeTilePoleX) // pixels of each mask
			{
				int32 TileIndex = LandscapeTilePoleY * kLandscapeTilePolesMinus1 + LandscapeTilePoleX;
				check(LandscapeTileMasks0.IsValidIndex(TileIndex) && LandscapeTileMasks1.IsValidIndex(TileIndex) && LandscapeTileMasks2.IsValidIndex(TileIndex));

				const FColor& PixelMask = CurrentMask[TileIndex];
				const uint8* PixelMaskPtr = reinterpret_cast<const uint8*>(&PixelMask);
				for (int32 PixelMasktPtrIndex = 0; PixelMasktPtrIndex < 4; ++PixelMasktPtrIndex) // color channels for this mask (read)
				{
					const uint8 EmptyValue = (3 == PixelMasktPtrIndex) ? 0xFF : 0;
					const uint8 FullValue = (3 == PixelMasktPtrIndex) ? 0 : 0xFF;
					if (FullValue == PixelMaskPtr[PixelMasktPtrIndex])
					{
						for (int32 SampleY = -1; SampleY < 2; ++SampleY) // adjacent pixels for this channel
						{
							const bool bOutOfBoundsY = ((0 == LandscapeTilePoleY) && (-1 == SampleY)) || (((kLandscapeTilePolesMinus1 - 1) == LandscapeTilePoleY) && (1 == SampleY));
							if (bOutOfBoundsY)
								continue;

							for (int32 SampleX = -1; SampleX < 2; ++SampleX) // adjacent pixels for this channel
							{
								const bool bIsCenter = (0 == SampleY) && (0 == SampleX);
								const bool bOutOfBoundsX = ((0 == LandscapeTilePoleX) && (-1 == SampleX)) || (((kLandscapeTilePolesMinus1 - 1) == LandscapeTilePoleX) && (1 == SampleX));
								if (bOutOfBoundsX || bIsCenter)
									continue;

								int32 TileIndexAdjacent = TileIndex + SampleY * kLandscapeTilePolesMinus1 + SampleX;
								check(LandscapeTileMasks0.IsValidIndex(TileIndexAdjacent) && LandscapeTileMasks1.IsValidIndex(TileIndexAdjacent) && LandscapeTileMasks2.IsValidIndex(TileIndexAdjacent));

								FColor& MaskAdjacent0 = CurrentMask[TileIndexAdjacent];
								uint8* MaskAdjacentPtr0 = reinterpret_cast<uint8*>(&MaskAdjacent0);
								
								if (EmptyValue == MaskAdjacentPtr0[PixelMasktPtrIndex]) // write empty adjacent pixels with a half-value for this channel
								{
									MaskAdjacentPtr0[PixelMasktPtrIndex] = 0x7F;
								}
							}
						}
					}
				}
			}
		}
	}

	for (uint32 LandscapeTilePoleY = 0; LandscapeTilePoleY < kLandscapeTilePolesMinus1; ++LandscapeTilePoleY) // pixels of each mask
	{
		for (uint32 LandscapeTilePoleX = 0; LandscapeTilePoleX < kLandscapeTilePolesMinus1; ++LandscapeTilePoleX) // pixels of each mask
		{
			int32 TileIndex = LandscapeTilePoleY * kLandscapeTilePolesMinus1 + LandscapeTilePoleX;
			check(LandscapeTileMasks0.IsValidIndex(TileIndex) && LandscapeTileMasks1.IsValidIndex(TileIndex) && LandscapeTileMasks2.IsValidIndex(TileIndex));

			FColor& PixelMask0 = LandscapeTileMasks0[TileIndex];
			uint8* PixelMaskPtr0 = reinterpret_cast<uint8*>(&PixelMask0);

			FColor& PixelMask1 = LandscapeTileMasks1[TileIndex];
			uint8* PixelMaskPtr1 = reinterpret_cast<uint8*>(&PixelMask1);

			FColor& PixelMask2 = LandscapeTileMasks2[TileIndex];
			uint8* PixelMaskPtr2 = reinterpret_cast<uint8*>(&PixelMask2);

			int32 NonEmptyPixels = 0;
			for (int32 PixelMasktPtrIndex = 0; PixelMasktPtrIndex < 4; ++PixelMasktPtrIndex)
			{
				const uint8 EmptyValue = (3 == PixelMasktPtrIndex) ? 0xFF : 0;
				const uint8 FullValue = (3 == PixelMasktPtrIndex) ? 0 : 0xFF;

				if (PixelMaskPtr0[PixelMasktPtrIndex] != EmptyValue)
				{
					NonEmptyPixels++;
				}

				if (PixelMaskPtr1[PixelMasktPtrIndex] != EmptyValue)
				{
					NonEmptyPixels++;
				}

				if (PixelMaskPtr2[PixelMasktPtrIndex] != EmptyValue)
				{
					NonEmptyPixels++;
				}
			}

			if (1 < NonEmptyPixels)
			{
				const uint8 SplitValue = 0xFF / NonEmptyPixels;
				for (int32 PixelMasktPtrIndex = 0; PixelMasktPtrIndex < 4; ++PixelMasktPtrIndex)
				{
					const uint8 EmptyValue = (3 == PixelMasktPtrIndex) ? 0xFF : 0;
					const uint8 FullValue = (3 == PixelMasktPtrIndex) ? SplitValue : (0xFF - SplitValue);

					if (PixelMaskPtr0[PixelMasktPtrIndex] != EmptyValue)
					{
						PixelMaskPtr0[PixelMasktPtrIndex] = FullValue;
					}

					if (PixelMaskPtr1[PixelMasktPtrIndex] != EmptyValue)
					{
						PixelMaskPtr1[PixelMasktPtrIndex] = FullValue;
					}

					if (PixelMaskPtr2[PixelMasktPtrIndex] != EmptyValue)
					{
						PixelMaskPtr2[PixelMasktPtrIndex] = FullValue;
					}
				}
			}
		}
	}
}

void FACEWorldBakeLandscapeWriter::SaveLandblockHeightmaps(const FACEWorldBakeCellFile& CellFile, const FACEWorldBakePortalRegion& Region, const FString& FileName)
{
	const int32 WorkItems_SaveLandblockHeightmaps = FACEWorldBakeCellFile::kHeightInLandblocks * FACEWorldBakeCellFile::kWidthInLandblocks + 136 * 136 + 8 * 8;
	FScopedSlowTask SlowTask_SaveLandblockHeightmaps(static_cast<float>(WorkItems_SaveLandblockHeightmaps), LOCTEXT("SaveLandblockHeightmaps", "Saving Landblock Heightmaps"));
	SlowTask_SaveLandblockHeightmaps.MakeDialog();

	TArray<uint32> LandscapeUsedSetHashes; // all set hashes used across the entire landscape

	// generate the full landscape-sized linear arrays
	TArray<uint16> LandscapeHeights;
	TArray<FColor> LandscapeColors;
	TArray<uint8> LandscapeRegions;
	TArray<uint8> LandscapeRoads;
	GenerateLandscapeArrays(SlowTask_SaveLandblockHeightmaps, CellFile, Region, LandscapeHeights, LandscapeColors, LandscapeRegions, LandscapeRoads);

	// generate unique sets of regions used per-component
	TMap<uint8, int32> LandscapeComponentRegionCount;
	TMap<uint32, TArray<uint8>> LandscapeComponentRegionSets;
	GenerateLandscapeComponentRegionSets(SlowTask_SaveLandblockHeightmaps, LandscapeRegions, LandscapeComponentRegionCount, LandscapeComponentRegionSets);

	// save linear heights to image for viewing
	SaveHeightmapToImage(LandscapeHeights, Region.HeightValues.Last(), kLandscapeSize, kLandscapeSize, FileName);

	// enumerate the tiles, extracting all masks
	for (uint32 LandscapeTileY = 0; LandscapeTileY < 8; ++LandscapeTileY)
	{
		for (uint32 LandscapeTileX = 0; LandscapeTileX < 8; ++LandscapeTileX)
		{
			SlowTask_SaveLandblockHeightmaps.EnterProgressFrame();

			// extract to tile
			static TArray<uint8> LandscapeTileHeights;
			static TArray<FColor> LandscapeTileColors;
			static TArray<uint8> LandscapeTileRegions;
			static TArray<uint8> LandscapeTileRoads;
			ExtractTileArrays(LandscapeTileX, LandscapeTileY, LandscapeHeights, LandscapeColors, LandscapeRegions, LandscapeRoads, LandscapeTileHeights, LandscapeTileColors, LandscapeTileRegions, LandscapeTileRoads);

			// fill the blend masks for the tile
			static TArray<uint32> LandscapeTileHashes; // set hashes used across the current tile
			static TArray<FColor> LandscapeTileMasks0;
			static TArray<FColor> LandscapeTileMasks1;
			static TArray<FColor> LandscapeTileMasks2;
			ExtractTileHashesAndBlendMask(LandscapeTileX, LandscapeTileY, LandscapeRegions, LandscapeComponentRegionCount, LandscapeComponentRegionSets, LandscapeTileHashes, LandscapeTileMasks0, LandscapeTileMasks1, LandscapeTileMasks2);

			static TArray<uint32> LandscapeTileSetHashIndices; // set hash indices used across the current tile
			LandscapeTileSetHashIndices.Empty();
			for (uint32 LandscapeTileSetHash : LandscapeTileHashes)
			{
				LandscapeUsedSetHashes.AddUnique(LandscapeTileSetHash);
				LandscapeTileSetHashIndices.Add(LandscapeUsedSetHashes.IndexOfByKey(LandscapeTileSetHash));
			}

			//ExtrudeBlendMaskEdges(LandscapeTileMasks0, LandscapeTileMasks1, LandscapeTileMasks2);

			const FString HeightFilePath = FString::Printf(TEXT("%s/%s_x%d_y%d.r16"), *kLandblocksSaveDirectory, *FileName, LandscapeTileX, LandscapeTileY);
			FFileHelper::SaveArrayToFile(LandscapeTileHeights, *HeightFilePath);

			const FString UnrealTileTerrainColorFileName = FString::Printf(TEXT("%s_x%d_y%d_Color"), *FileName, LandscapeTileX, LandscapeTileY);
			SaveImageToFile(LandscapeTileColors, kLandscapeTilePoles, kLandscapeTilePoles, UnrealTileTerrainColorFileName, TEXT("/Landblocks/"));

			if (0 < kLandscapeMaterialChannels)
			{
				const FString UnrealTileTerrainUniqueIndices0FileName = FString::Printf(TEXT("%s_x%d_y%d_Mask0"), *FileName, LandscapeTileX, LandscapeTileY);
				SaveImageToFile(LandscapeTileMasks0, kLandscapeTilePolesMinus1, kLandscapeTilePolesMinus1, UnrealTileTerrainUniqueIndices0FileName, TEXT("/Landblocks/"));
			}

			if (4 < kLandscapeMaterialChannels)
			{
				const FString UnrealTileTerrainUniqueIndices1FileName = FString::Printf(TEXT("%s_x%d_y%d_Mask1"), *FileName, LandscapeTileX, LandscapeTileY);
				SaveImageToFile(LandscapeTileMasks1, kLandscapeTilePolesMinus1, kLandscapeTilePolesMinus1, UnrealTileTerrainUniqueIndices1FileName, TEXT("/Landblocks/"));
			}

			if (8 < kLandscapeMaterialChannels)
			{
				const FString UnrealTileTerrainUniqueIndices2FileName = FString::Printf(TEXT("%s_x%d_y%d_Mask2"), *FileName, LandscapeTileX, LandscapeTileY);
				SaveImageToFile(LandscapeTileMasks2, kLandscapeTilePolesMinus1, kLandscapeTilePolesMinus1, UnrealTileTerrainUniqueIndices2FileName, TEXT("/Landblocks/"));
			}

			{
				FString UnrealTileRegionsString;
				ArrayToString(LandscapeTileRegions, UnrealTileRegionsString);

				const FString SaveStringPath = FString::Printf(TEXT("%s/%s_x%d_y%d.regions"), *kLandblocksSaveDirectory, *FileName, LandscapeTileX, LandscapeTileY);
				FFileHelper::SaveStringToFile(UnrealTileRegionsString, *SaveStringPath);
			}

			{
				FString UnrealTileRoadsString;
				ArrayToString(LandscapeTileRoads, UnrealTileRoadsString);

				const FString SaveStringPath = FString::Printf(TEXT("%s/%s_x%d_y%d.roads"), *kLandblocksSaveDirectory, *FileName, LandscapeTileX, LandscapeTileY);
				FFileHelper::SaveStringToFile(UnrealTileRoadsString, *SaveStringPath);
			}

			{
				FString UnrealTileGCFsString;
				ArrayToString(LandscapeTileSetHashIndices, UnrealTileGCFsString);

				const FString SaveStringPath = FString::Printf(TEXT("%s/%s_x%d_y%d.gcfs"), *kLandblocksSaveDirectory, *FileName, LandscapeTileX, LandscapeTileY);
				FFileHelper::SaveStringToFile(UnrealTileGCFsString, *SaveStringPath);
			}
		}
	}

	// dump used GCFs map to a text file
	{
		FString UnrealTileUniqueGCFsString;

		for (uint32 UniqueGCF : LandscapeUsedSetHashes)
		{
			const TArray<uint8>& GCFRegionSet = LandscapeComponentRegionSets.FindChecked(UniqueGCF);

			FString GCFRegionSetArrayString;
			ArrayToString(GCFRegionSet, GCFRegionSetArrayString);

			UnrealTileUniqueGCFsString += FString::Printf(TEXT("%d: %s\r\n"), LandscapeUsedSetHashes.IndexOfByKey(UniqueGCF), *GCFRegionSetArrayString);
		}

		const FString SaveStringPath = FString::Printf(TEXT("%s/%s.uniquegcfs"), *kLandblocksSaveDirectory, *FileName);
		FFileHelper::SaveStringToFile(UnrealTileUniqueGCFsString, *SaveStringPath);
	}
}

#undef LOCTEXT_NAMESPACE
