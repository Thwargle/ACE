#pragma once

class FACEWorldBakeByteReader;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalPalette
{
	FACEWorldBakePortalPalette();
	FACEWorldBakePortalPalette(FACEWorldBakeByteReader& Reader);
	//
	uint32 ResourceId;
	//
	uint32 NumColors;
	//
	TArray<FColor> Colors;
};
