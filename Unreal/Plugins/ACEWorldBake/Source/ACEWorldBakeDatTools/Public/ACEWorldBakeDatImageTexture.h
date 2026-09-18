#pragma once

class FACEWorldBakeByteReader;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalImageTexture
{
	FACEWorldBakePortalImageTexture();
	FACEWorldBakePortalImageTexture(FACEWorldBakeByteReader& Reader);
	//
	uint32 ResourceId;
	//
	uint32 AlwaysZero;
	//
	uint8 AlwaysTwo;
	//
	uint32 NumImgColors;
	//
	TArray<uint32> ImageColorIDs;
};
