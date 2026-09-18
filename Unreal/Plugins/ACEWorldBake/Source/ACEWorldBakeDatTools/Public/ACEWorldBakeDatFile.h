#pragma once

#include "ACEWorldBakeByteReader.h"

namespace EACEWorldBakeResource_Native
{
	enum Type : uint32
	{
		kNone = 0x00000000,
		kModel = 0x01000000,
		kSetup = 0x02000000,
		kAnimation = 0x03000000,
		kPalette = 0x04000000,
		kImgTex = 0x05000000,
		kImgColor = 0x06000000,
		kSurface = 0x08000000,
		kMotionTable = 0x09000000,
		kSound = 0x0A000000,
		kEnvironment = 0x0D000000,
		kScene = 0x12000000,
		kRegion = 0x13000000,
		kSoundTable = 0x20000000,
		kEnumMapper = 0x22000000,
		kParticleEmitter = 0x32000000,
		kPhysicsScript = 0x33000000,
		kPhysicsScriptTable = 0x34000000,
		kMask = 0xFF000000
	};
}

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeDatFileHeader
{
	//
	FACEWorldBakeDatFileHeader();
	//
	FACEWorldBakeDatFileHeader(FACEWorldBakeByteReader& Reader);
	//
	uint32 Format;
	//
	uint32 BlockSize;
	//
	uint32 FileSize;
	//
	uint32 Version;
	//
	uint32 Identifier;
	//
	uint32 FreeBlockHead;
	//
	uint32 FreeBlockTail;
	//
	uint32 FreeBlockCount;
	//
	uint32 RootDirectoryOffset;
	//
	uint32 NewLRU;
	//
	uint32 OldLRU;
	//
	uint8 bUseLRU;
	//
	uint32 MasterMapID;
	//
	uint32 EnginePackVersion;
	//
	uint32 GamePackVersion;
	//
	uint8 VersionMajor[16];
	//
	uint32 VersionMinor;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeDatFileBlock
{
	//
	uint32 NextBlockOffset;
	//
	const uint8* Data;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeDatFileEntry
{
	//
	uint32 Flags; // ToD
	//
	uint32 Identifier;
	//
	uint32 Offset;
	//
	uint32 Size;
	//
	uint32 Time; // ToD
	//
	uint32 Version; // ToD
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeDatFileDirectory
{
	//
	static const uint32 kMaxEntryCount = 62;
	//
	uint32 Offsets[kMaxEntryCount];
	//
	uint32 EntryCount;
	//
	FACEWorldBakeDatFileEntry Entries[kMaxEntryCount];
};

class ACEWORLDBAKEDATTOOLS_API FACEWorldBakeDatFile
{
public:
	FACEWorldBakeDatFile();

	virtual ~FACEWorldBakeDatFile();

	static FACEWorldBakeDatFilePtr LoadFromFile(const FString& Path);

	const FACEWorldBakeDatFileHeader& GetHeader() const;

	TArray<uint32> MatchIdentifiers(const uint32 IdentifierMask) const;

	bool WalkAndFindFileData(FArchive* FileReader, uint32 Offset, uint32 Identifier, TByteArray& OutFileDataCopy) const;

	TByteArrayPtr GetFileData(uint32 Identifier) const;

	template <typename T>
	bool GetResource(uint32 Identifier, T& OutResource) const;

	void DumpStatsToLog(bool bDumpFileEntries) const;

protected:
	void ReadHeader(FArchive* FileReader, uint32 Offset);

	void ReadDirectory(FArchive* FileReader, uint32 Offset);

	TByteArrayPtr GetDataFromBlocks(FArchive* FileReader, const uint32 Offset, const uint32 Size) const;

private:
	//
	FACEWorldBakeDatFileHeader Header;
	//
	uint32 FileEntrySizeInBytes;
	//
	TArray<FACEWorldBakeDatFileDirectory> Directories;
	//
	TMap<uint32, TByteArrayPtr> ResourceID_FileData;
};

template<typename T>
bool FACEWorldBakeDatFile::GetResource(uint32 identifier, T& OutResource) const
{
	TByteArrayPtr FileData = GetFileData(identifier);
	if (FileData.IsValid())
	{
		FACEWorldBakeByteReader Reader(FileData);
		OutResource = T(Reader);
		return true;
	}

	return false;
}
