#include "Dat/ACESceneryTileCache.h"
#include "Dat/ACEDiskTileCache.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"

namespace ACESceneryTileCache
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

		void WriteF32(TArray<uint8>& Buf, double V)
		{
			const float F = static_cast<float>(V);
			WritePod(Buf, F);
		}
	}

	FString MakeCacheFilePath(uint32 LandblockId, float WorldScale, uint64 DatFingerprint)
	{
		const uint32 ScaleBits = *reinterpret_cast<const uint32*>(&WorldScale);
		return ACEDiskTileCache::GetSceneryDir()
			/ FString::Printf(TEXT("%08X_%08X_%016llX.acsc"), LandblockId & 0xFFFF0000u, ScaleBits, DatFingerprint);
	}

	bool Save(const FString& Path, const TArray<FACEDatRegionSceneryItem>& Items, float WorldScale, uint64 DatFingerprint)
	{
		TArray<uint8> Payload;
		WritePod(Payload, static_cast<uint32>(Items.Num()));
		for (const FACEDatRegionSceneryItem& It : Items)
		{
			WritePod(Payload, It.SetupId);
			WriteF32(Payload, It.OriginAc.X);
			WriteF32(Payload, It.OriginAc.Y);
			WriteF32(Payload, It.OriginAc.Z);
			WriteF32(Payload, It.Orientation.X);
			WriteF32(Payload, It.Orientation.Y);
			WriteF32(Payload, It.Orientation.Z);
			WriteF32(Payload, It.Orientation.W);
			WritePod(Payload, It.Scale);
		}

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
		const uint32 ScaleBits = *reinterpret_cast<const uint32*>(&WorldScale);
		WritePod(FileBytes, ScaleBits);
		WritePod(FileBytes, DatFingerprint);
		WritePod(FileBytes, static_cast<uint32>(Payload.Num()));
		WritePod(FileBytes, static_cast<uint32>(Compressed.Num()));
		FileBytes.Append(Compressed);

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

	bool Load(const FString& Path, TArray<FACEDatRegionSceneryItem>& OutItems, float ExpectedWorldScale, uint64 ExpectedFingerprint)
	{
		OutItems.Reset();
		if (Path.IsEmpty() || !IFileManager::Get().FileExists(*Path))
		{
			return false;
		}
		TArray<uint8> FileBytes;
		if (!FFileHelper::LoadFileToArray(FileBytes, *Path) || FileBytes.Num() < 32)
		{
			return false;
		}
		FMemoryReader Ar(FileBytes, true);
		uint32 FileMagic = 0, FileSchema = 0, ScaleBits = 0;
		uint64 Fingerprint = 0;
		uint32 UncompressedSize = 0, CompressedSize = 0;
		if (!ReadPod(Ar, FileMagic) || FileMagic != Magic
			|| !ReadPod(Ar, FileSchema) || FileSchema != SchemaVersion
			|| !ReadPod(Ar, ScaleBits) || !ReadPod(Ar, Fingerprint)
			|| !ReadPod(Ar, UncompressedSize) || !ReadPod(Ar, CompressedSize))
		{
			return false;
		}
		const uint32 ExpectedScaleBits = *reinterpret_cast<const uint32*>(&ExpectedWorldScale);
		if (ScaleBits != ExpectedScaleBits || Fingerprint != ExpectedFingerprint
			|| CompressedSize == 0 || UncompressedSize == 0 || UncompressedSize > 32u * 1024u * 1024u)
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
		uint32 Count = 0;
		if (!ReadPod(PayloadAr, Count) || Count > 100000u)
		{
			return false;
		}
		OutItems.SetNum(static_cast<int32>(Count));
		for (FACEDatRegionSceneryItem& It : OutItems)
		{
			float Ox = 0, Oy = 0, Oz = 0, Qx = 0, Qy = 0, Qz = 0, Qw = 1;
			if (!ReadPod(PayloadAr, It.SetupId)
				|| !ReadPod(PayloadAr, Ox) || !ReadPod(PayloadAr, Oy) || !ReadPod(PayloadAr, Oz)
				|| !ReadPod(PayloadAr, Qx) || !ReadPod(PayloadAr, Qy) || !ReadPod(PayloadAr, Qz) || !ReadPod(PayloadAr, Qw)
				|| !ReadPod(PayloadAr, It.Scale))
			{
				return false;
			}
			It.OriginAc = FVector(Ox, Oy, Oz);
			It.Orientation = FQuat(Qx, Qy, Qz, Qw);
		}
		ACEDiskTileCache::Touch(Path);
		return !PayloadAr.IsError();
	}
}
