#pragma once

#include "CoreMinimal.h"

/** MonoGame/ACE.DatLoader DxtUtil — decompress to RGBA8 (R,G,B,A per pixel). */
namespace ACEDxtUtil
{
	void DecompressDxt1(const TArray<uint8>& ImageData, int32 Width, int32 Height, TArray<uint8>& OutRGBA);
	void DecompressDxt3(const TArray<uint8>& ImageData, int32 Width, int32 Height, TArray<uint8>& OutRGBA);
	void DecompressDxt5(const TArray<uint8>& ImageData, int32 Width, int32 Height, TArray<uint8>& OutRGBA);

	/** Runtime block compress of FColor (BGRA) mip0+ — width/height padded to 4. */
	void CompressDxt1(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& Out);
	void CompressDxt5(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& Out);
}
