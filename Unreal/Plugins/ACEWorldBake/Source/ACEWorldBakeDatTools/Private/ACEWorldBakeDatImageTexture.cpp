#include "ACEWorldBakeDatImageTexture.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

FACEWorldBakePortalImageTexture::FACEWorldBakePortalImageTexture()
: ResourceId(0)
, AlwaysZero(0)
, AlwaysTwo(0)
, NumImgColors(0)
{
}

FACEWorldBakePortalImageTexture::FACEWorldBakePortalImageTexture(FACEWorldBakeByteReader& Reader)
: ResourceId(Reader.ReadUint32())
, AlwaysZero(Reader.ReadUint32())
, AlwaysTwo(Reader.ReadUint8())
, NumImgColors(Reader.ReadUint32())
{
	check(0 == AlwaysZero);
	check(2 == AlwaysTwo);
	check(0 < NumImgColors);

	for (uint32_t i = 0; i < NumImgColors; i++)
	{
		ImageColorIDs.Add(Reader.ReadUint32());
	}

	check(Reader.BytesLeft() == 0);
}
