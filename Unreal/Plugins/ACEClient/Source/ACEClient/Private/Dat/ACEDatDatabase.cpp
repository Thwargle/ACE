#include "Dat/ACEDatDatabase.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"

namespace
{
	void CloseDatabaseUnlocked(TUniquePtr<IFileHandle>& KeptHandle, TMap<uint32, FACEDatFileEntry>& Files,
		bool& bOpen, uint32& BlockSize, uint32& BTreeRoot, uint32& DatFileSize, FString& FilePath)
	{
		KeptHandle.Reset();
		Files.Reset();
		bOpen = false;
		BlockSize = 0;
		BTreeRoot = 0;
		DatFileSize = 0;
		FilePath.Reset();
	}
}

bool FACEDatDatabase::Open(const FString& InFilePath)
{
	FScopeLock Lock(&IoMutex);
	CloseDatabaseUnlocked(KeptHandle, Files, bOpen, BlockSize, BTreeRoot, DatFileSize, FilePath);

	FilePath = InFilePath;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("ACEDat: file not found: %s"), *FilePath);
		return false;
	}

	// Retail holds DAT handles with write access. Our read-only handle must permit
	// that sharing, otherwise running the two clients together prevents all asset loads.
	TUniquePtr<IFileHandle> Handle;
	// Short retries only — long Sleep() on the game thread looked like a hard hang (low CPU).
	// Background load can afford a few brief waits; callers also retry via EnsureLoaded.
	constexpr int32 MaxOpenAttempts = 4;
	for (int32 Attempt = 0; Attempt < MaxOpenAttempts; ++Attempt)
	{
		Handle = TUniquePtr<IFileHandle>(PlatformFile.OpenRead(*FilePath, /*bAllowWrite*/ true));
		if (Handle)
		{
			break;
		}
		if (Attempt + 1 < MaxOpenAttempts)
		{
			FPlatformProcess::Sleep(0.05f);
		}
	}
	if (!Handle)
	{
		UE_LOG(LogTemp, Error, TEXT("ACEDat: failed to open (locked/AV-scanned?) after %d attempts: %s"), MaxOpenAttempts, *FilePath);
		return false;
	}

	if (!ReadHeader(Handle.Get()))
	{
		return false;
	}

	if (!ReadDirectoryTree(Handle.Get(), BTreeRoot))
	{
		UE_LOG(LogTemp, Error, TEXT("ACEDat: failed to index B-tree in %s"), *FilePath);
		return false;
	}

	if (bKeepOpen)
	{
		KeptHandle = MoveTemp(Handle);
	}

	bOpen = true;
	UE_LOG(LogTemp, Log, TEXT("ACEDat: opened %s (%d files, blockSize=%u)"), *FilePath, Files.Num(), BlockSize);
	return true;
}

void FACEDatDatabase::Close()
{
	FScopeLock Lock(&IoMutex);
	CloseDatabaseUnlocked(KeptHandle, Files, bOpen, BlockSize, BTreeRoot, DatFileSize, FilePath);
}

uint64 FACEDatDatabase::GetSourceFingerprint() const
{
	FScopeLock Lock(&IoMutex);
	if (FilePath.IsEmpty())
	{
		return 0;
	}
	const FFileStatData Stat = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*FilePath);
	uint32 Hash = FCrc::StrCrc32(*FilePath);
	Hash = HashCombine(Hash, GetTypeHash(Stat.FileSize));
	Hash = HashCombine(Hash, GetTypeHash(Stat.ModificationTime.ToUnixTimestamp()));
	Hash = HashCombine(Hash, DatFileSize);
	Hash = HashCombine(Hash, static_cast<uint32>(Files.Num()));
	return (static_cast<uint64>(Hash) << 32) ^ static_cast<uint64>(DatFileSize);
}

bool FACEDatDatabase::ReadHeader(IFileHandle* Handle)
{
	if (!Handle || !Handle->Seek(HeaderOffset))
	{
		return false;
	}

	uint32 FileType = 0;
	uint32 DataSet = 0;
	uint32 DataSubset = 0;
	uint32 FreeHead = 0, FreeTail = 0, FreeCount = 0;
	uint32 NewLRU = 0, OldLRU = 0, UseLRU = 0;
	uint32 MasterMapID = 0;
	uint32 EnginePackVersion = 0, GamePackVersion = 0;
	uint8 VersionMajor[16];
	uint32 VersionMinor = 0;

	auto ReadU32 = [Handle](uint32& Out) -> bool
	{
		return Handle->Read(reinterpret_cast<uint8*>(&Out), sizeof(uint32));
	};

	if (!ReadU32(FileType) || !ReadU32(BlockSize) || !ReadU32(DatFileSize) || !ReadU32(DataSet) || !ReadU32(DataSubset))
	{
		return false;
	}
	if (!ReadU32(FreeHead) || !ReadU32(FreeTail) || !ReadU32(FreeCount) || !ReadU32(BTreeRoot))
	{
		return false;
	}
	if (!ReadU32(NewLRU) || !ReadU32(OldLRU) || !ReadU32(UseLRU) || !ReadU32(MasterMapID))
	{
		return false;
	}
	if (!ReadU32(EnginePackVersion) || !ReadU32(GamePackVersion))
	{
		return false;
	}
	if (!Handle->Read(VersionMajor, 16) || !ReadU32(VersionMinor))
	{
		return false;
	}

	if (BlockSize == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("ACEDat: invalid block size in %s"), *FilePath);
		return false;
	}

	return true;
}

uint32 FACEDatDatabase::ReadNextAddress(IFileHandle* Handle)
{
	uint32 Next = 0;
	Handle->Read(reinterpret_cast<uint8*>(&Next), sizeof(uint32));
	return Next;
}

bool FACEDatDatabase::ReadSectorChain(IFileHandle* Handle, uint32 Offset, uint32 Size, TArray<uint8>& OutBuffer) const
{
	OutBuffer.SetNumUninitialized(static_cast<int32>(Size));
	if (Size == 0)
	{
		return true;
	}

	if (!Handle->Seek(Offset))
	{
		return false;
	}

	uint32 NextAddress = ReadNextAddress(Handle);
	int32 BufferOffset = 0;
	uint32 Remaining = Size;
	const uint32 Chunk = BlockSize > 4 ? (BlockSize - 4) : 0;
	if (Chunk == 0)
	{
		return false;
	}

	// Guard against corrupt next-pointers: Remaining is unsigned, so subtracting Chunk when
	// Remaining < Chunk would wrap to a huge value and spin forever (hard freeze on DAT open).
	constexpr int32 MaxSectors = 1 << 20;
	int32 SectorsRead = 0;
	while (Remaining > 0)
	{
		if (++SectorsRead > MaxSectors)
		{
			UE_LOG(LogTemp, Error, TEXT("ACEDat: sector chain exceeded %d sectors (corrupt?)"), MaxSectors);
			return false;
		}

		if (NextAddress == 0)
		{
			if (!Handle->Read(OutBuffer.GetData() + BufferOffset, static_cast<int64>(Remaining)))
			{
				return false;
			}
			Remaining = 0;
		}
		else
		{
			if (Remaining < Chunk)
			{
				UE_LOG(LogTemp, Error, TEXT("ACEDat: sector chain truncated (Remaining=%u Chunk=%u)"), Remaining, Chunk);
				return false;
			}
			if (!Handle->Read(OutBuffer.GetData() + BufferOffset, static_cast<int32>(Chunk)))
			{
				return false;
			}
			BufferOffset += static_cast<int32>(Chunk);
			if (!Handle->Seek(NextAddress))
			{
				return false;
			}
			NextAddress = ReadNextAddress(Handle);
			Remaining -= Chunk;
		}
	}

	return true;
}

bool FACEDatDatabase::ReadDirectoryTree(IFileHandle* Handle, uint32 RootSectorOffset)
{
	// DatDirectoryHeader.ObjectSize = (uint*0x3E) + uint + (DatFile*0x3D)
	constexpr int32 BranchCount = 0x3E;
	constexpr uint32 DatFileObjectSize = sizeof(uint32) * 6;
	constexpr uint32 DirectoryObjectSize = (sizeof(uint32) * BranchCount) + sizeof(uint32) + (DatFileObjectSize * 0x3D);

	TArray<uint8> HeaderBlob;
	if (!ReadSectorChain(Handle, RootSectorOffset, DirectoryObjectSize, HeaderBlob))
	{
		return false;
	}

	FACEDatCursor Cur(HeaderBlob);
	bool bOk = true;

	uint32 Branches[BranchCount];
	for (int32 i = 0; i < BranchCount; ++i)
	{
		Branches[i] = Cur.ReadU32(bOk);
	}
	const uint32 EntryCount = Cur.ReadU32(bOk);
	if (!bOk || EntryCount > 0x3D)
	{
		return false;
	}

	TArray<FACEDatFileEntry> LocalEntries;
	LocalEntries.Reserve(EntryCount);
	for (uint32 i = 0; i < EntryCount; ++i)
	{
		FACEDatFileEntry E;
		Cur.ReadU32(bOk); // BitFlags unused
		E.ObjectId = Cur.ReadU32(bOk);
		E.FileOffset = Cur.ReadU32(bOk);
		E.FileSize = Cur.ReadU32(bOk);
		E.Date = Cur.ReadU32(bOk);
		E.Iteration = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}
		LocalEntries.Add(E);
	}

	if (Branches[0] != 0)
	{
		for (uint32 i = 0; i < EntryCount + 1; ++i)
		{
			if (!ReadDirectoryTree(Handle, Branches[i]))
			{
				return false;
			}
		}
	}

	for (const FACEDatFileEntry& E : LocalEntries)
	{
		Files.Add(E.ObjectId, E);
	}

	return true;
}

bool FACEDatDatabase::ReadFile(uint32 ObjectId, TArray<uint8>& OutBuffer) const
{
	FScopeLock Lock(&IoMutex);
	OutBuffer.Reset();
	const FACEDatFileEntry* Entry = Files.Find(ObjectId);
	if (!Entry)
	{
		return false;
	}


	IFileHandle* Handle = KeptHandle.Get();
	TUniquePtr<IFileHandle> TempHandle;
	if (!Handle)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		TempHandle.Reset(PlatformFile.OpenRead(*FilePath, true));
		Handle = TempHandle.Get();
	}
	if (!Handle)
	{
		return false;
	}

	return ReadSectorChain(Handle, Entry->FileOffset, Entry->FileSize, OutBuffer);
}
