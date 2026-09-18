#pragma once

#include "ACEWorldBakeDatFile.h"

namespace EACEWorldBakePixelFormat_Native
{
	enum Type
	{
		kUnknown = 0x0000, // used
		kR8G8B8 = 0x0014, // used
		kA8R8G8B8 = 0x0015, // used
		kX8R8G8B8 = 0x0016,
		kR5G6B5 = 0x0017, // used
		kX1R5G5B5 = 0x0018,
		kA1R5G5B5 = 0x0019,
		kA4R4G4B4 = 0x001a, // used
		kR3G3B2 = 0x001b,
		kA8 = 0x001c, // used
		kA8R3G3B2 = 0x001d,
		kX4R4G4B4 = 0x001e,
		kA2B10G10R10 = 0x001f,
		kA8B8G8R8 = 0x0020,
		kX8B8G8R8 = 0x0021,
		kA2R10G10B10 = 0x0023,
		kA8P8 = 0x0028,
		kP8 = 0x0029, // used
		kL8 = 0x0032,
		kA8L8 = 0x0033,
		kA4L4 = 0x0034,
		kV8U8 = 0x003c,
		kL6V5U5 = 0x003d,
		kX8L8V8U8 = 0x003e,
		kQ8W8V8U8 = 0x003f,
		kV16U16 = 0x0040,
		kA2W10V10U10 = 0x0043,
		kD16_Lockable = 0x0046,
		kD32 = 0x0047,
		kD15S1 = 0x0049,
		kD24S8 = 0x004b,
		kD24X8 = 0x004d,
		kD24X4S4 = 0x004f,
		kD16 = 0x0050,
		kVertexData = 0x0064,
		kIndex16 = 0x0065, // used
		kIndex32 = 0x0066,
		kCustom_R8G8B8A8 = 0x00f0,
		kCustom_A8B8G8R8 = 0x00f1,
		kCustom_B8G8R8 = 0x00f2,
		kCustomLscapeR8G8B8 = 0x00f3, // used
		kCustomLscapeAlpha = 0x00f4, // used
		kCustomRawJPEG = 0x01f4, // used
		kYUY2 = 0x32595559,
		kUYVY = 0x59565955,
		kG8R8_G8B8 = 0x42475247,
		kR8G8_B8G8 = 0x47424752,
		kDXT1 = 0x31545844, // used
		kDXT2 = 0x32545844,
		kDXT3 = 0x33545844, // used
		kDXT4 = 0x34545844,
		kDXT5 = 0x35545844 // used
	};

	extern FString TypeToString(EACEWorldBakePixelFormat_Native::Type PixelFormat);
}

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalImage
{
	FACEWorldBakePortalImage();
	FACEWorldBakePortalImage(FACEWorldBakeByteReader& Reader);
	//
	uint32 ResourceId;
	//
	uint32 Type;
	//
	uint32 Width;
	//
	uint32 Height;
	//
	EACEWorldBakePixelFormat_Native::Type Format;
	//
	uint32 Length;
	//
	uint8* Pixels;
	//
	uint32 PaletteId;

	bool HasAlphaChannel() const
	{
		switch (Format)
		{
		case EACEWorldBakePixelFormat_Native::kA8R8G8B8:
		case EACEWorldBakePixelFormat_Native::kA4R4G4B4:
		case EACEWorldBakePixelFormat_Native::kA8:
		case EACEWorldBakePixelFormat_Native::kP8:
		case EACEWorldBakePixelFormat_Native::kCustomLscapeAlpha:
		case EACEWorldBakePixelFormat_Native::kDXT3:
		case EACEWorldBakePixelFormat_Native::kDXT5:
			return true;
		}

		return false;
	}

	bool IsIndexed() const
	{
		switch (Format)
		{
		case EACEWorldBakePixelFormat_Native::kIndex16:
		case EACEWorldBakePixelFormat_Native::kIndex32:
			return true;
		}

		return false;
	}
};

class ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalFile
{
public:
	FACEWorldBakePortalFile(FACEWorldBakeDatFilePtr datFile);

	template <typename T>
	bool GetResource(uint32 identifier, T& OutResource) const
	{
		return DatFile->GetResource(identifier, OutResource);
	}

private:
	FACEWorldBakeDatFilePtr DatFile;
};
