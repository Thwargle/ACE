#pragma once

class FACEWorldBakeByteReader;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalSurface
{
	FACEWorldBakePortalSurface();
	FACEWorldBakePortalSurface(FACEWorldBakeByteReader& Reader);
	//
	uint32 Flags;
	//
	uint32 BGRA;
	//
	uint32 TextureID;
	//
	uint32 PaletteID;
	//
	float Translucency;
	//
	float Luminosity;
	//
	float Diffuse;
};
