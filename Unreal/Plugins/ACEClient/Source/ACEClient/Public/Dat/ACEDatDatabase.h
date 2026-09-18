#pragma once

#include "CoreMinimal.h"
#include "Dat/ACEDatCursor.h"

struct FACEDatFileEntry
{
	uint32 ObjectId = 0;
	uint32 FileOffset = 0;
	uint32 FileSize = 0;
	uint32 Date = 0;
	uint32 Iteration = 0;
};

/**
 * Opens a Turbine .dat (portal / cell / highres), indexes the B-tree, and
 * reconstructs file blobs across sector chains — same layout as ACE.DatLoader.DatDatabase.
 */
class ACECLIENT_API FACEDatDatabase
{
public:
	static constexpr uint32 HeaderOffset = 0x140;

	bool Open(const FString& FilePath);
	void Close();

	bool IsOpen() const { return bOpen; }
	const FString& GetFilePath() const { return FilePath; }
	uint32 GetBlockSize() const { return BlockSize; }
	int32 GetFileCount() const { return Files.Num(); }

	bool Contains(uint32 ObjectId) const { return Files.Contains(ObjectId); }
	bool ReadFile(uint32 ObjectId, TArray<uint8>& OutBuffer) const;

	/**
	 * Stable fingerprint of this DAT on disk (path + size + mtime). Used by the
	 * landblock mesh disk cache so a swapped DAT invalidates baked geometry.
	 */
	uint64 GetSourceFingerprint() const;

private:
	/** Serializes KeptHandle Seek/Read — concurrent ReadFile from workers is otherwise racy. */
	mutable FCriticalSection IoMutex;
	bool ReadHeader(IFileHandle* Handle);
	bool ReadDirectoryTree(IFileHandle* Handle, uint32 RootSectorOffset);
	bool ReadSectorChain(IFileHandle* Handle, uint32 Offset, uint32 Size, TArray<uint8>& OutBuffer) const;
	static uint32 ReadNextAddress(IFileHandle* Handle);

	FString FilePath;
	mutable TUniquePtr<IFileHandle> KeptHandle;
	bool bOpen = false;
	bool bKeepOpen = true;

	uint32 BlockSize = 0;
	uint32 BTreeRoot = 0;
	uint32 DatFileSize = 0;

	TMap<uint32, FACEDatFileEntry> Files;
};
