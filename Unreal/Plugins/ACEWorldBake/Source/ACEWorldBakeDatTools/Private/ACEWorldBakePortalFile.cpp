#include "ACEWorldBakePortalFile.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatEnvironment.h"
#include "ACEWorldBakeDatImageTexture.h"
#include "ACEWorldBakeDatModel.h"
#include "ACEWorldBakeDatPalette.h"
#include "ACEWorldBakeDatRegion.h"
#include "ACEWorldBakeDatScene.h"
#include "ACEWorldBakeDatSetup.h"
#include "ACEWorldBakeDatSurface.h"

namespace EACEWorldBakePixelFormat_Native {

FString TypeToString(EACEWorldBakePixelFormat_Native::Type PixelFormat)
{
	FString TypeString(TEXT("UNHANDLED"));

	switch (PixelFormat)
	{
	case kUnknown: TypeString = FString(TEXT("kUnknown")); break;
	case kR8G8B8: TypeString = FString(TEXT("kR8G8B8")); break;
	case kA8R8G8B8: TypeString = FString(TEXT("kA8R8G8B8")); break;
	case kX8R8G8B8: TypeString = FString(TEXT("kX8R8G8B8")); break;
	case kR5G6B5: TypeString = FString(TEXT("kR5G6B5")); break;
	case kX1R5G5B5: TypeString = FString(TEXT("kX1R5G5B5")); break;
	case kA1R5G5B5: TypeString = FString(TEXT("kA1R5G5B5")); break;
	case kA4R4G4B4: TypeString = FString(TEXT("kA4R4G4B4")); break;
	case kR3G3B2: TypeString = FString(TEXT("kR3G3B2")); break;
	case kA8: TypeString = FString(TEXT("kA8")); break;
	case kA8R3G3B2: TypeString = FString(TEXT("kA8R3G3B2")); break;
	case kX4R4G4B4: TypeString = FString(TEXT("kX4R4G4B4")); break;
	case kA2B10G10R10: TypeString = FString(TEXT("kA2B10G10R10")); break;
	case kA8B8G8R8: TypeString = FString(TEXT("kA8B8G8R8")); break;
	case kX8B8G8R8: TypeString = FString(TEXT("kX8B8G8R8")); break;
	case kA2R10G10B10: TypeString = FString(TEXT("kA2R10G10B10")); break;
	case kA8P8: TypeString = FString(TEXT("kA8P8")); break;
	case kP8: TypeString = FString(TEXT("kP8")); break;
	case kL8: TypeString = FString(TEXT("kL8")); break;
	case kA8L8: TypeString = FString(TEXT("kA8L8")); break;
	case kA4L4: TypeString = FString(TEXT("kA4L4")); break;
	case kV8U8: TypeString = FString(TEXT("kV8U8")); break;
	case kL6V5U5: TypeString = FString(TEXT("kL6V5U5")); break;
	case kX8L8V8U8: TypeString = FString(TEXT("kX8L8V8U8")); break;
	case kQ8W8V8U8: TypeString = FString(TEXT("kQ8W8V8U8")); break;
	case kV16U16: TypeString = FString(TEXT("kV16U16")); break;
	case kA2W10V10U10: TypeString = FString(TEXT("kA2W10V10U10")); break;
	case kD16_Lockable: TypeString = FString(TEXT("kD16_Lockable")); break;
	case kD32: TypeString = FString(TEXT("kD32")); break;
	case kD15S1: TypeString = FString(TEXT("kD15S1")); break;
	case kD24S8: TypeString = FString(TEXT("kD24S8")); break;
	case kD24X8: TypeString = FString(TEXT("kD24X8")); break;
	case kD24X4S4: TypeString = FString(TEXT("kD24X4S4")); break;
	case kD16: TypeString = FString(TEXT("kD16")); break;
	case kVertexData: TypeString = FString(TEXT("kVertexData")); break;
	case kIndex16: TypeString = FString(TEXT("kIndex16")); break;
	case kIndex32: TypeString = FString(TEXT("kIndex32")); break;
	case kCustom_R8G8B8A8: TypeString = FString(TEXT("kCustom_R8G8B8A8")); break;
	case kCustom_A8B8G8R8: TypeString = FString(TEXT("kCustom_A8B8G8R8")); break;
	case kCustom_B8G8R8: TypeString = FString(TEXT("kCustom_B8G8R8")); break;
	case kCustomLscapeR8G8B8: TypeString = FString(TEXT("kCustomLscapeR8G8B8")); break;
	case kCustomLscapeAlpha: TypeString = FString(TEXT("kCustomLscapeAlpha")); break;
	case kCustomRawJPEG: TypeString = FString(TEXT("kCustomRawJPEG")); break;
	case kYUY2: TypeString = FString(TEXT("kYUY2")); break;
	case kUYVY: TypeString = FString(TEXT("kUYVY")); break;
	case kG8R8_G8B8: TypeString = FString(TEXT("kG8R8_G8B8")); break;
	case kR8G8_B8G8: TypeString = FString(TEXT("kR8G8_B8G8")); break;
	case kDXT1: TypeString = FString(TEXT("kDXT1")); break;
	case kDXT2: TypeString = FString(TEXT("kDXT2")); break;
	case kDXT3: TypeString = FString(TEXT("kDXT3")); break;
	case kDXT4: TypeString = FString(TEXT("kDXT4")); break;
	case kDXT5: TypeString = FString(TEXT("kDXT5")); break;
	default: TypeString = FString::Printf(TEXT("UNHANDLED(0x%02X)"), (int32)PixelFormat); break;
	}

	return TypeString;
}

}

FACEWorldBakePortalImage::FACEWorldBakePortalImage()
: ResourceId(0)
, Type(0)
, Width(0)
, Height(0)
, Format(EACEWorldBakePixelFormat_Native::kUnknown)
, Length(0)
, Pixels(nullptr)
{

}

FACEWorldBakePortalImage::FACEWorldBakePortalImage(FACEWorldBakeByteReader& Reader)
: ResourceId(Reader.ReadUint32())
, Type(Reader.ReadUint32())
, Width(Reader.ReadUint32())
, Height(Reader.ReadUint32())
, Format(static_cast<EACEWorldBakePixelFormat_Native::Type>(Reader.ReadUint32()))
, Length(Reader.ReadUint32())
, Pixels(nullptr)
{
	if (0 < Length)
	{
		Pixels = Reader.ReadBlob(Length);
	}

	if (Format == EACEWorldBakePixelFormat_Native::kIndex16 || Format == EACEWorldBakePixelFormat_Native::kP8)
	{
		PaletteId = Reader.ReadUint32();
	}
}

FACEWorldBakePortalFile::FACEWorldBakePortalFile(FACEWorldBakeDatFilePtr datFile)
: DatFile(datFile)
{
}
