#include "ACEWorldBakeDatSurface.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

enum SurfaceType
{
	kBase1Solid = 0x00000001,
	kBase1Image = 0x00000002,
	kBase1Clipmap = 0x00000004,
	kTranslucent = 0x00000010,
	kDiffuse = 0x00000020,
	kLuminous = 0x00000040,
	kAlpha = 0x00000100,
	kInvAlpha = 0x00000200,
	kAdditive = 0x00010000,
	kDetail = 0x00020000,
	kGouraud = 0x10000000,
	kStippled = 0x40000000,
	kPerspective = 0x80000000
};

FACEWorldBakePortalSurface::FACEWorldBakePortalSurface()
: Flags(0)
, BGRA(0)
, TextureID(0)
, PaletteID(0)
, Translucency(0.0f)
, Luminosity(0.0f)
, Diffuse(0.0f)
{
}

FACEWorldBakePortalSurface::FACEWorldBakePortalSurface(FACEWorldBakeByteReader& Reader)
: Flags(Reader.ReadUint32())
, BGRA(0)
, TextureID(0)
, PaletteID(0)
, Translucency(0.0f)
, Luminosity(0.0f)
, Diffuse(0.0f)
{
	if (Flags & kBase1Solid)
	{
		BGRA = Reader.ReadUint32();
	}
	else if (Flags & (kBase1Image | kBase1Clipmap))
	{
		TextureID = Reader.ReadUint32();
		PaletteID = Reader.ReadUint32();
		check(PaletteID == 0);
	}
	else
	{
		check(false);
	}

	Translucency = Reader.ReadFloat();
	Luminosity = Reader.ReadFloat();
	Diffuse = Reader.ReadFloat();

	check(Reader.BytesLeft() == 0);
}
