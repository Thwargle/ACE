#include "Dat/ACEPCodeTileCache.h"
#include "Dat/ACEDiskTileCache.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"

namespace ACEPCodeTileCache
{
	namespace
	{
		template <typename T>
		void WritePod(TArray<uint8>& Buf, const T& Value)
		{
			const int32 At = Buf.AddUninitialized(sizeof(T));
			FMemory::Memcpy(Buf.GetData() + At, &Value, sizeof(T));
		}

		template <typename T>
		bool ReadPod(FMemoryReader& Ar, T& Out)
		{
			if (Ar.IsError() || Ar.TotalSize() - Ar.Tell() < static_cast<int64>(sizeof(T)))
			{
				return false;
			}
			Ar.Serialize(&Out, sizeof(T));
			return !Ar.IsError();
		}
	}

	FString MakeCacheFilePath(uint32 PCode, uint64 DatFingerprint)
	{
		return ACEDiskTileCache::GetPCodesDir()
			/ FString::Printf(TEXT("%08X_%016llX.acpx"), PCode, DatFingerprint);
	}

	bool Save(uint32 PCode, uint64 DatFingerprint, int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
		{
			return false;
		}
		TArray<uint8> Payload;
		WritePod(Payload, Width);
		WritePod(Payload, Height);
		const int32 At = Payload.AddUninitialized(Pixels.Num() * sizeof(FColor));
		FMemory::Memcpy(Payload.GetData() + At, Pixels.GetData(), Pixels.Num() * sizeof(FColor));

		TArray<uint8> Compressed;
		Compressed.AddUninitialized(FCompression::CompressMemoryBound(NAME_Zlib, Payload.Num()));
		int32 CompressedSize = Compressed.Num();
		if (!FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Payload.GetData(), Payload.Num()))
		{
			return false;
		}
		Compressed.SetNum(CompressedSize);

		TArray<uint8> FileBytes;
		WritePod(FileBytes, Magic);
		WritePod(FileBytes, SchemaVersion);
		WritePod(FileBytes, PCode);
		WritePod(FileBytes, DatFingerprint);
		WritePod(FileBytes, static_cast<uint32>(Payload.Num()));
		WritePod(FileBytes, static_cast<uint32>(Compressed.Num()));
		FileBytes.Append(Compressed);

		const FString Path = MakeCacheFilePath(PCode, DatFingerprint);
		const FString TempPath = FString::Printf(TEXT("%s.%u.tmp"), *Path, FPlatformProcess::GetCurrentProcessId());
		if (!FFileHelper::SaveArrayToFile(FileBytes, *TempPath))
		{
			return false;
		}
		IFileManager& FM = IFileManager::Get();
		if (!FM.Move(*Path, *TempPath, true, true, false, true))
		{
			FM.Delete(*TempPath, false, true);
			return FM.FileExists(*Path);
		}
		return true;
	}

	bool Load(uint32 PCode, uint64 DatFingerprint, int32& OutWidth, int32& OutHeight, TArray<FColor>& OutPixels)
	{
		const FString Path = MakeCacheFilePath(PCode, DatFingerprint);
		if (!IFileManager::Get().FileExists(*Path))
		{
			return false;
		}
		TArray<uint8> FileBytes;
		if (!FFileHelper::LoadFileToArray(FileBytes, *Path) || FileBytes.Num() < 32)
		{
			return false;
		}
		FMemoryReader Ar(FileBytes, true);
		uint32 FileMagic = 0, FileSchema = 0, FilePCode = 0;
		uint64 FileFp = 0;
		uint32 UncompressedSize = 0, CompressedSize = 0;
		if (!ReadPod(Ar, FileMagic) || FileMagic != Magic
			|| !ReadPod(Ar, FileSchema) || FileSchema != SchemaVersion
			|| !ReadPod(Ar, FilePCode) || FilePCode != PCode
			|| !ReadPod(Ar, FileFp) || FileFp != DatFingerprint
			|| !ReadPod(Ar, UncompressedSize) || !ReadPod(Ar, CompressedSize)
			|| CompressedSize == 0 || UncompressedSize == 0
			|| UncompressedSize > 16u * 1024u * 1024u)
		{
			return false;
		}
		TArray<uint8> Compressed;
		Compressed.SetNumUninitialized(static_cast<int32>(CompressedSize));
		Ar.Serialize(Compressed.GetData(), CompressedSize);
		if (Ar.IsError())
		{
			return false;
		}
		TArray<uint8> Payload;
		Payload.SetNumUninitialized(static_cast<int32>(UncompressedSize));
		if (!FCompression::UncompressMemory(NAME_Zlib, Payload.GetData(), UncompressedSize, Compressed.GetData(), CompressedSize))
		{
			return false;
		}
		FMemoryReader PayloadAr(Payload, true);
		int32 W = 0, H = 0;
		if (!ReadPod(PayloadAr, W) || !ReadPod(PayloadAr, H) || W <= 0 || H <= 0 || W > 2048 || H > 2048)
		{
			return false;
		}
		const int32 Count = W * H;
		OutPixels.SetNumUninitialized(Count);
		PayloadAr.Serialize(OutPixels.GetData(), Count * sizeof(FColor));
		if (PayloadAr.IsError())
		{
			return false;
		}
		OutWidth = W;
		OutHeight = H;
		ACEDiskTileCache::Touch(Path);
		return true;
	}
}
