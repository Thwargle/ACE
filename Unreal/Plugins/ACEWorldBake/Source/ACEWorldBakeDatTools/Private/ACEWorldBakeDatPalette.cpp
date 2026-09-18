#include "ACEWorldBakeDatPalette.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

FACEWorldBakePortalPalette::FACEWorldBakePortalPalette()
: ResourceId(0)
, NumColors(0)
{
}

FACEWorldBakePortalPalette::FACEWorldBakePortalPalette(FACEWorldBakeByteReader& Reader)
: ResourceId(Reader.ReadUint32())
, NumColors(Reader.ReadUint32())
{
	check(NumColors == 2048);
	Colors.AddZeroed(NumColors);

	for (FColor& Color : Colors)
	{
		Color.B = Reader.ReadUint8();
		Color.G = Reader.ReadUint8();
		Color.R = Reader.ReadUint8();
		Color.A = Reader.ReadUint8();
	}

	check(Reader.BytesLeft() == 0);
}
