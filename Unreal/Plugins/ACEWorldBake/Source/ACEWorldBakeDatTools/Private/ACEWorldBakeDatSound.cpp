#include "ACEWorldBakeDatSound.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

FACEWorldBakePortalSoundWave::FACEWorldBakePortalSoundWave()
: ResourceId(0)
{
}

FACEWorldBakePortalSoundWave::FACEWorldBakePortalSoundWave(FACEWorldBakeByteReader& Reader)
: ResourceId(Reader.ReadUint32())
, ChunkSize(Reader.ReadUint32())
, DataSize(Reader.ReadUint32())
, FormatTag(Reader.ReadUint16())
, Channels(Reader.ReadUint16())
, SamplesPerSec(Reader.ReadUint32())
, AvgBytesPerSec(Reader.ReadUint32())
, BlockAlign(Reader.ReadUint16())
, BitsPerSample(Reader.ReadUint16())
, BytesEx(Reader.ReadUint16())
, mp3wID(0)
, mp3fdwFlags(0)
, mp3nBlockSize(0)
, mp3nFramesPerBlock(0)
, mp3nCodecDelay(0)
, WaveData(nullptr)
{
	check(18 == ChunkSize || 30 == ChunkSize);

	WaveData = Reader.ReadBlob(DataSize);

	if (30 == ChunkSize)
	{
		mp3wID = Reader.ReadUint16(); // packed?
		mp3fdwFlags = Reader.ReadUint32();
		mp3nBlockSize = Reader.ReadUint16();
		mp3nFramesPerBlock = Reader.ReadUint16();
		mp3nCodecDelay = Reader.ReadUint16();
	}

	check(Reader.BytesLeft() == 0);
}
