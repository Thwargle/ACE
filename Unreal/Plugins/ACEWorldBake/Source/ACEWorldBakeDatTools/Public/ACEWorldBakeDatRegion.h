#pragma once

class FACEWorldBakeByteReader;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionHourBlock
{
	FACEWorldBakePortalRegionHourBlock();

	float StartTime;

	uint32 IsNightTime;

	FString HourName;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionSeason
{
	FACEWorldBakePortalRegionSeason();

	uint32 StartDay;

	FString SeasonName;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionWeatherColorUnknown
{
	FACEWorldBakePortalRegionWeatherColorUnknown();

	uint32 Unk1;

	float Unk2[5];
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionWeatherColor
{
	FACEWorldBakePortalRegionWeatherColor();

	float Unk1[4];

	uint32 ColorA;

	float Unk2;

	uint32 ColorB;

	float Unk3[2];

	uint32 Color3;

	uint32 Unk4;

	uint32 NumWeatherColorUnknowns;

	TArray<FACEWorldBakePortalRegionWeatherColorUnknown> WeatherColorUnknowns;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionWeatherResource
{
	FACEWorldBakePortalRegionWeatherResource();

	float Unk1[6];

	uint32 ResourceId;

	uint32 ResourceId2;

	uint32 Unk2;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionWeather
{
	FACEWorldBakePortalRegionWeather();

	float Percentage;

	FString Name;

	uint32 NumWeatherResources;

	TArray<FACEWorldBakePortalRegionWeatherResource> WeatherResources;

	uint32 NumWeatherColors;

	TArray<FACEWorldBakePortalRegionWeatherColor> WeatherColors;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionUnknownB
{
	FACEWorldBakePortalRegionUnknownB();

	uint32 Unk1;

	float Unk2[5];
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionUnknownA
{
	FACEWorldBakePortalRegionUnknownA();

	uint32 Unk1;

	uint32 NumUnknownBs;

	TArray<FACEWorldBakePortalRegionUnknownB> UnknownBs;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionSceneType
{
	FACEWorldBakePortalRegionSceneType();

	uint32 SceneTypeUnk;

	uint32 SceneCount;

	TArray<uint32> SceneIds;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionTerrainType
{
	FACEWorldBakePortalRegionTerrainType();

	FString TerrainName;

	uint32 TerrainColor;

	uint32 NumTerrainSceneTypes;

	TArray<uint32> TerrainSceneTypes;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionAlphaTexture
{
	FACEWorldBakePortalRegionAlphaTexture();

	uint32 TexCode;

	uint32 TexId;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionAlphaTextures
{
	FACEWorldBakePortalRegionAlphaTextures();

	uint32 NumAlphaTex;

	TArray<FACEWorldBakePortalRegionAlphaTexture> AlphaTextures;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegionTerrainTexture
{
	FACEWorldBakePortalRegionTerrainTexture();

	uint32 TerrainTexNum;

	uint32 TerrainTexResourceId;

	uint32 TexTiling;

	uint32 MaxVertBright;

	uint32 MinVertBright;

	uint32 MaxVertSaturate;

	uint32 MinVertSaturate;

	uint32 MaxVertHue;

	uint32 MinVertHue;

	uint32 DetailTexTiling;

	uint32 DetailTexId;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalRegion
{
	FACEWorldBakePortalRegion();
	FACEWorldBakePortalRegion(FACEWorldBakeByteReader& Reader);

	uint32 ResourceId;

	uint32 RegionNumber;

	uint32 RegionVersion;

	FString RegionName;

	uint32 NumRows;

	uint32 NumColumns;

	float SquareLength;
	uint32 heightsPerCell;
	uint32 VertexPerCell;
	float Unk1;
	float Unk2;
	float RoadWidth;

	TArray<float> HeightValues;

	uint32 Unk4;
	float Unk5;
	uint32 Unk6;
	float Unk7;
	uint32 daysPerYear;
	FString yearUnitName;

	uint32 NumHours;

	TArray<FACEWorldBakePortalRegionHourBlock> HourBlocks;

	uint32 NumHolidays;

	TArray<FString> HolidayNames;

	uint32 NumSeasons;

	TArray<FACEWorldBakePortalRegionSeason> Seasons;

	uint32 unknown8;
	uint32 unknown9;
	float unknown10;
	float unknown11;
	float unknown12;

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

	FACEWorldBakePortalRegionAlphaTextures AlphaTexture1;

	FACEWorldBakePortalRegionAlphaTextures AlphaTexture2;

	FACEWorldBakePortalRegionAlphaTextures AlphaTexture3;

	uint32 NumTerrainTex;

	TArray<FACEWorldBakePortalRegionTerrainTexture> TerrainTextures;

	uint32 Unk11;

	uint32 SmallMap;

	uint32 LargeMap;

	const uint8* Unk12;
};
