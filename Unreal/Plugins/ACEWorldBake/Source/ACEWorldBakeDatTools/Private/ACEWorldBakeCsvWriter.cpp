#include "ACEWorldBakeCsvWriter.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeDatRegion.h"
#include "ACEWorldBakeDatImageTexture.h"
#include "ACEWorldBakePortalFile.h"
#include "Modules/ModuleManager.h"

FString ToString(const FACEWorldBakePortalRegionAlphaTextures& AlphaTextures)
{
	IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));

	TArray<uint32> ImageIds;
	for (int32 AlphaTextureIndex = 0; AlphaTextureIndex < AlphaTextures.AlphaTextures.Num(); ++AlphaTextureIndex)
	{
		const FACEWorldBakePortalRegionAlphaTexture& AlphaTextureSub = AlphaTextures.AlphaTextures[AlphaTextureIndex];

		FACEWorldBakePortalImageTexture PortalTexture;
		const bool PortalTextureOk = PortalFile.GetResource(AlphaTextureSub.TexId, PortalTexture);
		if (PortalTextureOk)
		{
			ImageIds.Add(PortalTexture.ImageColorIDs[0]);
		}
	}

	FString Result;
	ArrayToHexString(ImageIds, Result);

	return Result;
}

FString FACEWorldBakeCsvWriter::ExportText(const FACEWorldBakePortalRegion& Region)
{
	IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));

	FString OutContent;

	OutContent += FString::Printf(TEXT("ResourceId,0x%08X\r\n"), Region.ResourceId);
	OutContent += FString::Printf(TEXT("RegionNumber,%d\r\n"), Region.RegionNumber);
	OutContent += FString::Printf(TEXT("RegionVersion,%d\r\n"), Region.RegionVersion);
	OutContent += FString::Printf(TEXT("RegionName,%s\r\n"), *(Region.RegionName));
	OutContent += FString::Printf(TEXT("NumRows,%d\r\n"), Region.NumRows);
	OutContent += FString::Printf(TEXT("NumColumns,%d\r\n"), Region.NumColumns);
	OutContent += FString::Printf(TEXT("cellGridSize,%4.2f\r\n"), Region.SquareLength);
	OutContent += FString::Printf(TEXT("heightsPerCell,%d\r\n"), Region.heightsPerCell);
	OutContent += FString::Printf(TEXT("overlapPerCell,%d\r\n"), Region.VertexPerCell);
	OutContent += FString::Printf(TEXT("Unk1,%4.2f\r\n"), Region.Unk1);
	OutContent += FString::Printf(TEXT("Unk2,%4.2f\r\n"), Region.Unk2);
	OutContent += FString::Printf(TEXT("Unk3,%4.2f\r\n"), Region.RoadWidth);
	FString HeightArrayString;
	ArrayToString(Region.HeightValues, HeightArrayString);
	OutContent += FString::Printf(TEXT("HeightValues,[%s]\r\n"), *HeightArrayString);
	OutContent += FString::Printf(TEXT("Unk4,%d\r\n"), Region.Unk4);
	OutContent += FString::Printf(TEXT("Unk5,%4.2f\r\n"), Region.Unk5);
	OutContent += FString::Printf(TEXT("Unk6,%d\r\n"), Region.Unk6);
	OutContent += FString::Printf(TEXT("Unk7,%4.2f\r\n"), Region.Unk7);
	OutContent += FString::Printf(TEXT("daysPerYear,%d\r\n"), Region.daysPerYear);
	OutContent += FString::Printf(TEXT("yearUnitName,%s\r\n"), *(Region.yearUnitName));
	OutContent += FString::Printf(TEXT("NumHours,%d\r\n"), Region.NumHours);
	//TArray<FACEWorldBakePortalRegionHourBlock> HourBlocks;
	OutContent += FString::Printf(TEXT("NumHolidays,%d\r\n"), Region.NumHolidays);
	FString HolidayNamesArrayString;
	ArrayToString(Region.HolidayNames, HolidayNamesArrayString);
	OutContent += FString::Printf(TEXT("HolidayNames,[%s]\r\n"), *(HolidayNamesArrayString));
	OutContent += FString::Printf(TEXT("NumSeasons,%d\r\n"), Region.NumSeasons);
	//TArray<FACEWorldBakePortalRegionSeason> Seasons;
	OutContent += FString::Printf(TEXT("unknown8,%d\r\n"), Region.unknown8);
	OutContent += FString::Printf(TEXT("unknown9,%u\r\n"), Region.unknown9);
	OutContent += FString::Printf(TEXT("unknown10,%4.2f\r\n"), Region.unknown10);
	OutContent += FString::Printf(TEXT("unknown11,%4.2f\r\n"), Region.unknown11);
	OutContent += FString::Printf(TEXT("unknown12,%4.2f\r\n"), Region.unknown12);
	/*
	uint32 NumWeather;
	TArray<FACEWorldBakePortalRegionWeather> Weathers;
	uint32 NumUnknownA;
	TArray<FACEWorldBakePortalRegionUnknownA> UnknownAs;
	uint32 NumSceneTypes;
	TArray<FACEWorldBakePortalRegionSceneType> SceneTypes;
	uint32 NumTerrainTypes;
	TArray<FACEWorldBakePortalRegionTerrainType> TerrainTypes;
	uint32 Unk9;
	uint32 Unk10;
	*/

	OutContent += FString::Printf(TEXT("AlphaTexture1_ImageIds,%s\r\n"), *ToString(Region.AlphaTexture1));
	OutContent += FString::Printf(TEXT("AlphaTexture2_ImageIds,%s\r\n"), *ToString(Region.AlphaTexture2));
	OutContent += FString::Printf(TEXT("AlphaTexture3_ImageIds,%s\r\n"), *ToString(Region.AlphaTexture3));

	for (int32 TerrainTextureIndex = 0; TerrainTextureIndex < Region.TerrainTextures.Num(); ++TerrainTextureIndex)
	{
		const FACEWorldBakePortalRegionTerrainTexture& TerrainTexture = Region.TerrainTextures[TerrainTextureIndex];
		OutContent += FString::Printf(TEXT("TerrainTexture: %d,"), TerrainTextureIndex);

		FACEWorldBakePortalImageTexture TerrainImageTexture;
		const bool TerrainImageTextureOk = PortalFile.GetResource(TerrainTexture.TerrainTexResourceId, TerrainImageTexture);
		if (TerrainImageTextureOk)
		{
			OutContent += FString::Printf(TEXT("terrainTexResourceId (img): 0x%08X,"), TerrainImageTexture.ImageColorIDs[0]);
		}
		else
		{
			OutContent += FString::Printf(TEXT("terrainTexResourceId (img?): 0x%08X,"), TerrainTexture.TerrainTexResourceId);
		}

		OutContent += FString::Printf(TEXT("texTiling:%d "), TerrainTexture.TexTiling);
		OutContent += FString::Printf(TEXT("maxVertBright:%d,"), TerrainTexture.MaxVertBright);
		OutContent += FString::Printf(TEXT("minVertBright:%d,"), TerrainTexture.MinVertBright);
		OutContent += FString::Printf(TEXT("maxVertSaturate:%d,"), TerrainTexture.MaxVertSaturate);
		OutContent += FString::Printf(TEXT("minVertSaturate:%d,"), TerrainTexture.MinVertSaturate);
		OutContent += FString::Printf(TEXT("maxVertHue:%d,"), TerrainTexture.MaxVertHue);
		OutContent += FString::Printf(TEXT("minVertHue:%d,"), TerrainTexture.MinVertHue);
		OutContent += FString::Printf(TEXT("detailTexTiling:%d,"), TerrainTexture.DetailTexTiling);

		FACEWorldBakePortalImageTexture DetailImageTexture;
		const bool DetailImageTextureOk = PortalFile.GetResource(TerrainTexture.DetailTexId, DetailImageTexture);
		if (DetailImageTextureOk)
		{
			OutContent += FString::Printf(TEXT("detailTexId (img):0x%08X\r\n"), DetailImageTexture.ImageColorIDs[0]);
		}
		else
		{
			OutContent += FString::Printf(TEXT("detailTexId (img?):0x%08X\r\n"), TerrainTexture.DetailTexId);
		}
	}

	OutContent += FString::Printf(TEXT("Unk11,0x%08X\r\n"), Region.Unk11);
	OutContent += FString::Printf(TEXT("SmallMap,0x%08X\r\n"), Region.SmallMap);
	OutContent += FString::Printf(TEXT("LargeMap,0x%08X\r\n"), Region.LargeMap);

	return OutContent;
}
