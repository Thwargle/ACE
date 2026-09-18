#include "ACEWorldBakeDatFile.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "ACEWorldBakeDatFile"

const uint32 kACDatFileHeaderFileOffset = 0x12C;
const uint32 kACDatFileHeaderFileOffset_ToD = 0x140;
const uint32 kACDatFileEntrySizeInBytes = 1020; // 1244;
const uint32 kACDatFileEntrySizeInBytes_ToD = 1740;

static FString StructToString(const FACEWorldBakeDatFileHeader& Header);
static FString StructToString(const FACEWorldBakeDatFileEntry& Entry);
static FString StructToString(const FACEWorldBakeDatFileDirectory& Directory, bool bDumpEntries = false);

FACEWorldBakeDatFileHeader::FACEWorldBakeDatFileHeader()
: Format(0)
, BlockSize(0)
, FileSize(0)
, Version(0)
, Identifier(0)
, FreeBlockHead(0)
, FreeBlockTail(0)
, FreeBlockCount(0)
, RootDirectoryOffset(0)
, NewLRU(0)
, OldLRU(0)
, bUseLRU(0)
, MasterMapID(0)
, EnginePackVersion(0)
, GamePackVersion(0)
, VersionMinor(0)
{
	FMemory::Memzero(VersionMajor);
}

FACEWorldBakeDatFileHeader::FACEWorldBakeDatFileHeader(FACEWorldBakeByteReader& Reader)
: FACEWorldBakeDatFileHeader()
{
	Format = Reader.ReadUint32();
	check(0x00005442 == Format);

	BlockSize = Reader.ReadUint32();
	check(256 == BlockSize || 1024 == BlockSize);

	FileSize = Reader.ReadUint32();
	check(FileSize);

	Version = Reader.ReadUint32();

	const bool bIsToD = (1 == Version) || (2 == Version) || (3 == Version);
	check(bIsToD || 0x00000768 == Version);

	if (bIsToD)
	{
		Identifier = Reader.ReadUint32();
	}

	check(0 == Identifier || 1 == Identifier || 0x69466948 == Identifier);

	FreeBlockHead = Reader.ReadUint32();
	check(FreeBlockHead <= FileSize);

	FreeBlockTail = Reader.ReadUint32();
	check(FreeBlockTail <= FileSize);

	FreeBlockCount = Reader.ReadUint32();

	RootDirectoryOffset = Reader.ReadUint32();
	check(RootDirectoryOffset < FileSize);

	if (bIsToD)
	{
		NewLRU = Reader.ReadUint32();
		check(0 == NewLRU);

		OldLRU = Reader.ReadUint32();
		check(0 == OldLRU);

		bUseLRU = Reader.ReadUint8();
		check(0 == bUseLRU);

		// struct is 4-byte aligned
		{
			uint8 Aligned1 = Reader.ReadUint8();
			uint8 Aligned2 = Reader.ReadUint8();
			uint8 Aligned3 = Reader.ReadUint8();
		}

		MasterMapID = Reader.ReadUint32();
		check(0 == MasterMapID || 0x25000000 == MasterMapID);

		EnginePackVersion = Reader.ReadUint32();
		check(0x0000006e == EnginePackVersion || 0x00000016 == EnginePackVersion); // Portal OR Cell known versions

		GamePackVersion = Reader.ReadUint32();
		check(0 == GamePackVersion);

		const uint8* const VersionMajorBlob = Reader.ReadBlob(16);
		uint8* VersionMajorPtr = VersionMajor;
		FMemory::Memcpy(VersionMajorPtr, VersionMajorBlob, 16);

		VersionMinor = Reader.ReadUint32();
	}
}

FACEWorldBakeDatFile::FACEWorldBakeDatFile()
{
	FMemory::Memzero<FACEWorldBakeDatFileHeader>(Header);
}

FACEWorldBakeDatFile::~FACEWorldBakeDatFile()
{
}

FACEWorldBakeDatFilePtr FACEWorldBakeDatFile::LoadFromFile(const FString& Path)
{
	FACEWorldBakeDatFilePtr LoadedFile;

	FArchive* RileReader = IFileManager::Get().CreateFileReader(*Path, FILEREAD_Silent);
	if (RileReader)
	{
		LoadedFile = MakeShareable(new FACEWorldBakeDatFile());

		const bool bDatIsToD = Path.EndsWith(TEXT("client_cell_1.dat")) || Path.EndsWith(TEXT("client_highres.dat")) || Path.EndsWith(TEXT("client_local_English.dat")) || Path.EndsWith(TEXT("client_portal.dat"));

		const int32 WorkItems_ReadDatFile = 3;
		FText ReadDatFileText = FText::Format(LOCTEXT("ReadDatFile", "Reading {0}"), FText::FromString(FPaths::GetCleanFilename(Path)));
		FScopedSlowTask SlowTask_ReadDatFile(static_cast<float>(WorkItems_ReadDatFile), ReadDatFileText);
		SlowTask_ReadDatFile.MakeDialog();

		// read the header
		SlowTask_ReadDatFile.EnterProgressFrame();
		LoadedFile->FileEntrySizeInBytes = bDatIsToD ? kACDatFileEntrySizeInBytes_ToD : kACDatFileEntrySizeInBytes;
		LoadedFile->ReadHeader(RileReader, bDatIsToD ? kACDatFileHeaderFileOffset_ToD : kACDatFileHeaderFileOffset);

		// read directories, starting at the root offset
		SlowTask_ReadDatFile.EnterProgressFrame();
		LoadedFile->ReadDirectory(RileReader, LoadedFile->Header.RootDirectoryOffset);

		SlowTask_ReadDatFile.EnterProgressFrame();
		const bool Ok = RileReader->Close();

		delete RileReader;
	}
	else
	{
		UE_LOG_ACEWORLDBAKEDATTOOLS(Warning, TEXT("Failed to read file '%s' error."), *Path);
	}

	return LoadedFile;
}

const FACEWorldBakeDatFileHeader& FACEWorldBakeDatFile::GetHeader() const
{
	return Header;
}

TArray<uint32> FACEWorldBakeDatFile::MatchIdentifiers(const uint32 IdentifierMask) const
{
	TArray<uint32> MatchingIdentifiers;

	TArray<uint32> AllIdentifiers;
	ResourceID_FileData.GetKeys(AllIdentifiers);

	for (uint32 Identifier : AllIdentifiers)
	{
		const bool MatchingIdentifier = (Identifier & EACEWorldBakeResource_Native::kMask) == (IdentifierMask & EACEWorldBakeResource_Native::kMask);
		const uint32 IdentifierOnly = Identifier & ~EACEWorldBakeResource_Native::kMask;
		const bool MatchingMask = (0 == IdentifierOnly) || (0 != (IdentifierOnly & (IdentifierMask & ~EACEWorldBakeResource_Native::kMask)));
		if (MatchingIdentifier && MatchingMask)
		{
			MatchingIdentifiers.Add(Identifier);
		}
	}

	return MatchingIdentifiers;
}

bool FACEWorldBakeDatFile::WalkAndFindFileData(FArchive* FileReader, uint32 Offset, uint32 Identifier, TByteArray& OutFileDataCopy) const
{
	TByteArrayPtr DirectoryBuffer = GetDataFromBlocks(FileReader, Offset, FileEntrySizeInBytes);
	check(0 < DirectoryBuffer->Num());

	const FACEWorldBakeDatFileDirectory* const AsFileDirectory = reinterpret_cast<const FACEWorldBakeDatFileDirectory*>(DirectoryBuffer->GetData());

	uint32 Index = 0;
	for (; Index < AsFileDirectory->EntryCount; ++Index)
	{
		if (Identifier <= AsFileDirectory->Entries[Index].Identifier)
		{
			break;
		}
	}

	if (Index < AsFileDirectory->EntryCount && Identifier == AsFileDirectory->Entries[Index].Identifier)
	{
		TByteArrayPtr FileDataBuffer = GetDataFromBlocks(FileReader, AsFileDirectory->Entries[Index].Offset, AsFileDirectory->Entries[Index].Size);
		OutFileDataCopy.Append(*FileDataBuffer);
		return true;
	}

	if (0 != AsFileDirectory->Offsets[0])
	{
		return WalkAndFindFileData(FileReader, AsFileDirectory->Offsets[Index], Identifier, OutFileDataCopy);
	}

	return false;
}

TByteArrayPtr FACEWorldBakeDatFile::GetFileData(uint32 Identifier) const
{
	const TByteArrayPtr* const FoundData = ResourceID_FileData.Find(Identifier);
	return FoundData ? *FoundData : nullptr;
}

void FACEWorldBakeDatFile::DumpStatsToLog(bool bDumpFileEntries) const
{
	for (auto& Directory : Directories)
	{
		const FString DirectoryDump = StructToString(Directory, bDumpFileEntries);
		UE_LOG_ACEWORLDBAKEDATTOOLS(Warning, TEXT("%s"), *DirectoryDump);
	}
}

void FACEWorldBakeDatFile::ReadHeader(FArchive* FileReader, uint32 Offset)
{
	TByteArrayPtr HeaderData = MakeShareable(new TByteArray());
	HeaderData->Reset();
	HeaderData->AddUninitialized(sizeof(FACEWorldBakeDatFileHeader));

	FileReader->Seek(Offset);
	FileReader->Serialize(HeaderData->GetData(), sizeof(FACEWorldBakeDatFileHeader));

	FACEWorldBakeByteReader ByteReader(HeaderData);
	Header = FACEWorldBakeDatFileHeader(ByteReader);

	if (FileReader->TotalSize() != Header.FileSize)
	{
		UE_LOG_ACEWORLDBAKEDATTOOLS(Warning, TEXT("FACEWorldBakeDatFile::ReadHeader(fileReader:%s) - Header.FileSize is %d but file size is %d"), *FileReader->GetArchiveName(), Header.FileSize, FileReader->TotalSize());
	}

	UE_LOG_ACEWORLDBAKEDATTOOLS(Log, TEXT("FACEWorldBakeDatFile::ReadHeader(fileReader:%s) - Header - Format:0x%08X, Version:%d, Identifier:0x%08X"), *FileReader->GetArchiveName(), Header.Format, Header.Version, Header.Identifier);
}

void FACEWorldBakeDatFile::ReadDirectory(FArchive* FileReader, uint32 Offset)
{
	TByteArrayPtr DirectoryBuffer = GetDataFromBlocks(FileReader, Offset, FileEntrySizeInBytes);
	check(0 < DirectoryBuffer->Num());

	const FACEWorldBakeDatFileDirectory* const AsFileDirectory = reinterpret_cast<const FACEWorldBakeDatFileDirectory*>(DirectoryBuffer->GetData());
	Directories.Add(*AsFileDirectory);

	if (0 != AsFileDirectory->Offsets[0]) // no directories if first offset is 0
	{
		for (uint32 OffsetIndex = 0; OffsetIndex <= AsFileDirectory->EntryCount; ++OffsetIndex) // number of subdirectories is up to mEntryCount + 1
		{
			check((0 != AsFileDirectory->Offsets[OffsetIndex]) && (0xcdcdcdcd != AsFileDirectory->Offsets[OffsetIndex]));
			check((AsFileDirectory->Offsets[OffsetIndex] + FileEntrySizeInBytes) < FileReader->TotalSize());
			ReadDirectory(FileReader, AsFileDirectory->Offsets[OffsetIndex]);
		}
	}

	for (uint32 FileEntryIndex = 0; FileEntryIndex < AsFileDirectory->EntryCount; ++FileEntryIndex)
	{
		const FACEWorldBakeDatFileEntry& FileEntry = AsFileDirectory->Entries[FileEntryIndex];
		if ((FileEntry.Offset + FileEntry.Size) < FileReader->TotalSize())
		{
			TByteArrayPtr fileBuffer = GetDataFromBlocks(FileReader, FileEntry.Offset, FileEntry.Size);
			check(!ResourceID_FileData.Contains(FileEntry.Identifier));
			ResourceID_FileData.Add(FileEntry.Identifier, fileBuffer);
		}
		else
		{
			UE_LOG_ACEWORLDBAKEDATTOOLS(Warning, TEXT("FACEWorldBakeDatFile::ReadDirectory(fileReader:%s) - File entry for identifier 0x%08X offset+size is larger than the dat file, resource ignored"), *FileReader->GetArchiveName(), FileEntry.Identifier);
		}
	}
}

TByteArrayPtr FACEWorldBakeDatFile::GetDataFromBlocks(FArchive* FileReader, const uint32 Offset, const uint32 Size) const
{
	TByteArrayPtr BlockData = MakeShareable(new TByteArray());
	BlockData->Reset();

	uint32 NextBlockOffset = Offset;
	// if offset is 0 the chain of blocks has ended
	while (0 != NextBlockOffset)
	{
		// must be a valid address with room for the block we are going to read
		check((NextBlockOffset + Header.BlockSize) < FileReader->TotalSize());

		// seek to the current offset and read the offset for the next block
		FileReader->Seek(NextBlockOffset);
		FileReader->SerializeInt(NextBlockOffset, FileReader->TotalSize());

		// read the data of this block and append it to the buffer
		const uint32 BytesReadSoFar = static_cast<uint32>(BlockData->Num());
		const uint32 BytesToReadForThisBlock = FMath::Min(Header.BlockSize - static_cast<uint32>(sizeof(uint32)), Size - BytesReadSoFar);
		check(0 < BytesToReadForThisBlock);
		BlockData->AddUninitialized(BytesToReadForThisBlock);
		FileReader->Serialize(BlockData->GetData() + BytesReadSoFar, BytesToReadForThisBlock);
	}

	// must have read the exact number of bytes expected
	if (BlockData->Num() != Size)
	{
		UE_LOG_ACEWORLDBAKEDATTOOLS(Fatal, TEXT("FACEWorldBakeDatFile::GetDataFromBlocks(fileReader:%s, offset:%d, size:%d) - Read wrong number of bytes %d for block data"), *FileReader->GetArchiveName(), Offset, Size, BlockData->Num());
	}

	return BlockData;
}

FString StructToString(const FACEWorldBakeDatFileHeader& Header)
{
	return FString::Printf(TEXT(""));
}

FString StructToString(const FACEWorldBakeDatFileEntry& Entry)
{
	return FString::Printf(TEXT("Entry.mFlags(%d), Entry.mIdentifier(%d), Entry.mOffset(%d), Entry.mSize(%d), Entry.mTime(%d), Entry.mVersion(%d)\r\n"), Entry.Flags, Entry.Identifier, Entry.Offset, Entry.Size, Entry.Time, Entry.Version);
}

FString StructToString(const FACEWorldBakeDatFileDirectory& Directory, bool bDumpEntries)
{
	uint32 ValidOffsets = 0;
	for (; 0 != Directory.Offsets[ValidOffsets]; ++ValidOffsets)
	{
		continue;
	}

	FString Dump = FString::Printf(TEXT("mOffsets[%d], mEntryCount(%d)"), ValidOffsets, Directory.EntryCount);

	if (bDumpEntries)
	{
		for (uint32 i = 0; i < Directory.EntryCount; ++i)
		{
			Dump += StructToString(Directory.Entries[i]);
		}
	}

	return Dump;
}

#undef LOCTEXT_NAMESPACE
