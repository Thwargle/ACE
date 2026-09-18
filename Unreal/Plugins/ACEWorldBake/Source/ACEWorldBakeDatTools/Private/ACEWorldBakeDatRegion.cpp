#include "ACEWorldBakeDatRegion.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

static const uint32_t kRegionVersion = 3;

FACEWorldBakePortalRegionHourBlock::FACEWorldBakePortalRegionHourBlock()
: StartTime(0.0f)
, IsNightTime(0)
{
}

FACEWorldBakePortalRegionSeason::FACEWorldBakePortalRegionSeason()
: StartDay(0)
{
}

FACEWorldBakePortalRegionWeatherColorUnknown::FACEWorldBakePortalRegionWeatherColorUnknown()
: Unk1(0)
{
	FMemory::Memzero(Unk2);
}

FACEWorldBakePortalRegionWeatherColor::FACEWorldBakePortalRegionWeatherColor()
: ColorA(0)
, Unk2(0.0f)
, ColorB(0)
, Color3(0)
, Unk4(0)
, NumWeatherColorUnknowns(0)
{
	FMemory::Memzero(Unk1);
	FMemory::Memzero(Unk3);
}

FACEWorldBakePortalRegionWeatherResource::FACEWorldBakePortalRegionWeatherResource()
: ResourceId(0)
, ResourceId2(0)
, Unk2(0)
{
	FMemory::Memzero(Unk1);
}

FACEWorldBakePortalRegionWeather::FACEWorldBakePortalRegionWeather()
: Percentage(0.0f)
, NumWeatherResources(0)
, NumWeatherColors(0)
{
}

FACEWorldBakePortalRegionUnknownB::FACEWorldBakePortalRegionUnknownB()
: Unk1(0)
{
	FMemory::Memzero(Unk2);
}

FACEWorldBakePortalRegionUnknownA::FACEWorldBakePortalRegionUnknownA()
: Unk1(0)
, NumUnknownBs(0)
{
}

FACEWorldBakePortalRegionSceneType::FACEWorldBakePortalRegionSceneType()
: SceneTypeUnk(0)
, SceneCount(0)
{
}

FACEWorldBakePortalRegionTerrainType::FACEWorldBakePortalRegionTerrainType()
: TerrainColor(0)
, NumTerrainSceneTypes(0)
{
}

FACEWorldBakePortalRegionAlphaTexture::FACEWorldBakePortalRegionAlphaTexture()
: TexCode(0)
, TexId(0)
{
}

FACEWorldBakePortalRegionAlphaTextures::FACEWorldBakePortalRegionAlphaTextures()
: NumAlphaTex(0)
{
}

FACEWorldBakePortalRegionTerrainTexture::FACEWorldBakePortalRegionTerrainTexture()
: TerrainTexNum(0)
, TerrainTexResourceId(0)
, TexTiling(0)
, MaxVertBright(0)
, MinVertBright(0)
, MaxVertSaturate(0)
, MinVertSaturate(0)
, MaxVertHue(0)
, MinVertHue(0)
, DetailTexTiling(0)
, DetailTexId(0)
{
}

FACEWorldBakePortalRegion::FACEWorldBakePortalRegion()
{
}

FACEWorldBakePortalRegion::FACEWorldBakePortalRegion(FACEWorldBakeByteReader& Reader)
{
	check(Reader.TotalSize() == 48844 || Reader.TotalSize() == 58956);
	const bool bReadFACEWorldBakePortalRegionWeatherResourceUnk2 = (Reader.TotalSize() == 58956);

	ResourceId = Reader.ReadUint32();

	RegionNumber = Reader.ReadUint32();
	check(RegionNumber == 1);

	RegionVersion = Reader.ReadUint32();
	check(RegionVersion == kRegionVersion);

	RegionName = Reader.ReadString();

	NumRows = Reader.ReadUint32();
	check(NumRows == 0xFF);

	NumColumns = Reader.ReadUint32();
	check(NumColumns == 0xFF);

	SquareLength = Reader.ReadFloat();		//square_length
	heightsPerCell = Reader.ReadUint32();
	VertexPerCell = Reader.ReadUint32();	//vert_per_cell
	Unk1 = Reader.ReadFloat();				//max_obj_height
	Unk2 = Reader.ReadFloat();				//sky_height
	RoadWidth = Reader.ReadFloat();			//road_width

	for (int32 HeightIndex = 0; HeightIndex < 256; ++HeightIndex)
	{
		HeightValues.Add(Reader.ReadFloat());
	}

	Unk4 = Reader.ReadUint32();
	Unk5 = Reader.ReadFloat();
	Unk6 = Reader.ReadUint32();
	Unk7 = Reader.ReadFloat();
	daysPerYear = Reader.ReadUint32();
	yearUnitName = Reader.ReadString();

	NumHours = Reader.ReadUint32();

	for (uint32 i = 0; i < NumHours; i++)
	{
		FACEWorldBakePortalRegionHourBlock HourBlock;

		HourBlock.StartTime = Reader.ReadFloat();
		check(HourBlock.StartTime >= 0.0 && HourBlock.StartTime <= 1.0);

		HourBlock.IsNightTime = Reader.ReadUint32();
		check(HourBlock.IsNightTime == 0 || HourBlock.IsNightTime == 1);

		HourBlock.HourName = Reader.ReadString();

		HourBlocks.Add(HourBlock);
	}

	NumHolidays = Reader.ReadUint32();

	for (uint32 i = 0; i < NumHolidays; i++)
	{
		HolidayNames.Add(Reader.ReadString());
	}

	NumSeasons = Reader.ReadUint32();

	for (uint32 i = 0; i < NumSeasons; i++)
	{
		FACEWorldBakePortalRegionSeason Season;

		Season.StartDay = Reader.ReadUint32();
		check(Season.StartDay <= 365);

		Season.SeasonName = Reader.ReadString();

		Seasons.Add(Season);
	}

	unknown8 = Reader.ReadUint32();
	unknown9 = Reader.ReadUint32();
	unknown10 = Reader.ReadFloat();
	unknown11 = Reader.ReadFloat();
	unknown12 = Reader.ReadFloat();

	NumWeather = Reader.ReadUint32();

	for (uint32 i = 0; i < NumWeather; i++)
	{
		FACEWorldBakePortalRegionWeather Weather;

		Weather.Percentage = Reader.ReadFloat();
		Weather.Name = Reader.ReadString();

		Weather.NumWeatherResources = Reader.ReadUint32();

		for (uint32 j = 0; j < Weather.NumWeatherResources; j++)
		{
			FACEWorldBakePortalRegionWeatherResource WeatherData;

			for (uint32 k = 0; k < 6; k++)
			{
				WeatherData.Unk1[k] = Reader.ReadFloat();
			}

			WeatherData.ResourceId = Reader.ReadUint32();
			WeatherData.ResourceId2 = Reader.ReadUint32();
			if (bReadFACEWorldBakePortalRegionWeatherResourceUnk2)
			{
				WeatherData.Unk2 = Reader.ReadUint32();
			}

			Weather.WeatherResources.Add(WeatherData);
		}

		Weather.NumWeatherColors = Reader.ReadUint32();

		for (uint32 j = 0; j < Weather.NumWeatherColors; j++)
		{
			FACEWorldBakePortalRegionWeatherColor WeatherColor;

			for (uint32 k = 0; k < 4; k++)
			{
				WeatherColor.Unk1[k] = Reader.ReadFloat();
			}

			WeatherColor.ColorA = Reader.ReadUint32();

			WeatherColor.Unk2 = Reader.ReadFloat();

			WeatherColor.ColorB = Reader.ReadUint32();

			for (uint32 k = 0; k < 2; k++)
			{
				WeatherColor.Unk3[k] = Reader.ReadFloat();
			}

			WeatherColor.Color3 = Reader.ReadUint32();

			WeatherColor.Unk4 = Reader.ReadUint32();

			WeatherColor.NumWeatherColorUnknowns = Reader.ReadUint32();

			for (uint32 k = 0; k < WeatherColor.NumWeatherColorUnknowns; k++)
			{
				FACEWorldBakePortalRegionWeatherColorUnknown WeatherColorUnknown;

				WeatherColorUnknown.Unk1 = Reader.ReadUint32();

				for (uint32 l = 0; l < 5; l++)
				{
					WeatherColorUnknown.Unk2[l] = Reader.ReadFloat();
				}

				WeatherColor.WeatherColorUnknowns.Add(WeatherColorUnknown);
			}

			Weather.WeatherColors.Add(WeatherColor);
		}

		Weathers.Add(Weather);
	}

	NumUnknownA = Reader.ReadUint32();

	for (uint32 i = 0; i < NumUnknownA; i++)
	{
		FACEWorldBakePortalRegionUnknownA UnknownA;

		UnknownA.Unk1 = Reader.ReadUint32();

		UnknownA.NumUnknownBs = Reader.ReadUint32();

		for (uint32 j = 0; j < UnknownA.NumUnknownBs; j++)
		{
			FACEWorldBakePortalRegionUnknownB UnknownB;

			UnknownB.Unk1 = Reader.ReadUint32();

			for (uint32 k = 0; k < 4; k++)
			{
				UnknownB.Unk2[k] = Reader.ReadFloat();
			}

			UnknownA.UnknownBs.Add(UnknownB);
		}

		UnknownAs.Add(UnknownA);
	}

	NumSceneTypes = Reader.ReadUint32();
	for (uint32 i = 0; i < NumSceneTypes; i++)
	{
		FACEWorldBakePortalRegionSceneType SceneType;

		SceneType.SceneTypeUnk = Reader.ReadUint32();

		SceneType.SceneCount = Reader.ReadUint32();
		for (uint32 j = 0; j < SceneType.SceneCount; j++)
		{
			SceneType.SceneIds.Add(Reader.ReadUint32());
			check((SceneType.SceneIds.Last() & 0xFF000000) == static_cast<uint32_t>(EACEWorldBakeResource_Native::kScene));
		}

		SceneTypes.Add(SceneType);
	}

	NumTerrainTypes = Reader.ReadUint32();
	for (uint32 i = 0; i < NumTerrainTypes; i++)
	{
		FACEWorldBakePortalRegionTerrainType TerrainType;

		TerrainType.TerrainName = Reader.ReadString();
		TerrainType.TerrainColor = Reader.ReadUint32();

		TerrainType.NumTerrainSceneTypes = Reader.ReadUint32();
		for (uint32 j = 0; j < TerrainType.NumTerrainSceneTypes; j++)
		{
			TerrainType.TerrainSceneTypes.Add(Reader.ReadUint32());
		}

		TerrainTypes.Add(TerrainType);
	}

	Unk9 = Reader.ReadUint32();
	check(Unk9 == 0);

	Unk10 = Reader.ReadUint32();
	check(Unk10 == 0x400);

	FACEWorldBakePortalRegionAlphaTextures* Alphas[] = { &AlphaTexture1, &AlphaTexture2, &AlphaTexture3 };
	for (uint32 i = 0; i < 3; i++)
	{
		Alphas[i]->NumAlphaTex = Reader.ReadUint32();

		for (uint32 j = 0; j < Alphas[i]->NumAlphaTex; j++)
		{
			FACEWorldBakePortalRegionAlphaTexture AlphaTexture;

			AlphaTexture.TexCode = Reader.ReadUint32();
			check(AlphaTexture.TexCode == 8 || AlphaTexture.TexCode == 9 || AlphaTexture.TexCode == 10);

			AlphaTexture.TexId = Reader.ReadUint32();
			check((AlphaTexture.TexId & 0xFF000000) == static_cast<uint32>(EACEWorldBakeResource_Native::kImgTex));

			Alphas[i]->AlphaTextures.Add(AlphaTexture);
		}
	}

	NumTerrainTex = Reader.ReadUint32();

	for (uint32 i = 0; i < NumTerrainTex; i++)
	{
		FACEWorldBakePortalRegionTerrainTexture TerrainTexture;

		TerrainTexture.TerrainTexNum = Reader.ReadUint32();
		check(TerrainTexture.TerrainTexNum == i);

		TerrainTexture.TerrainTexResourceId = Reader.ReadUint32();
		check((TerrainTexture.TerrainTexResourceId & 0xFF000000) == static_cast<uint32>(EACEWorldBakeResource_Native::kImgTex));

		TerrainTexture.TexTiling = Reader.ReadUint32();

		TerrainTexture.MaxVertBright = Reader.ReadUint32();
		TerrainTexture.MinVertBright = Reader.ReadUint32();
		check(TerrainTexture.MaxVertBright >= TerrainTexture.MinVertBright);

		TerrainTexture.MaxVertSaturate = Reader.ReadUint32();
		TerrainTexture.MinVertSaturate = Reader.ReadUint32();
		check(TerrainTexture.MaxVertSaturate >= TerrainTexture.MinVertSaturate);

		TerrainTexture.MaxVertHue = Reader.ReadUint32();
		TerrainTexture.MinVertHue = Reader.ReadUint32();
		check(TerrainTexture.MaxVertHue >= TerrainTexture.MinVertHue);

		TerrainTexture.DetailTexTiling = Reader.ReadUint32();

		TerrainTexture.DetailTexId = Reader.ReadUint32();
		check((TerrainTexture.DetailTexId & 0xFF000000) == static_cast<uint32_t>(EACEWorldBakeResource_Native::kImgTex));

		TerrainTextures.Add(TerrainTexture);
	}

	Unk11 = Reader.ReadUint32();
	check(Unk11 == 1);

	SmallMap = Reader.ReadUint32();
	LargeMap = Reader.ReadUint32();

	Unk12 = Reader.ReadBlob(12);

	check(0 == Reader.BytesLeft());
}
