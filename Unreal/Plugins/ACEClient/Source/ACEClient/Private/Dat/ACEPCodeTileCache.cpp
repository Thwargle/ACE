#include "Dat/ACEPCodeTileCache.h"
#include "Dat/ACEDiskTileCache.h"
#include "HAL/FileManager.h"
#include "Dat/ACETerrainBlendCache.h"
#include "Misc/Crc.h"
#include "Misc/Guid.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
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

	bool SavePrepared(uint32 PCode, uint64 DatFingerprint, const FACETerrainBlend& Blend)
	{
		if (!Blend.IsValid() || Blend.UploadMips.IsEmpty() || Blend.Width>2048 || Blend.Height>2048) return false;
		TArray<uint8> Bytes;
		WritePod(Bytes, Magic); WritePod(Bytes, SchemaVersion); WritePod(Bytes, PCode); WritePod(Bytes, DatFingerprint);
		WritePod(Bytes, Blend.Width); WritePod(Bytes, Blend.Height); WritePod(Bytes, Blend.UploadMips.Num());
		for (const auto& Mip : Blend.UploadMips)
		{
			WritePod(Bytes, Mip.RawBytes); WritePod(Bytes, uint8(Mip.bPacked)); WritePod(Bytes, Mip.Data.Num());
			Bytes.Append(Mip.Data);
		}
		const uint32 CRC=FCrc::MemCrc32(Bytes.GetData(),Bytes.Num()); WritePod(Bytes,CRC);
		const FString Path=MakeCacheFilePath(PCode,DatFingerprint);
		const FString Temp=Path+FGuid::NewGuid().ToString()+TEXT(".tmp");
		if (!FFileHelper::SaveArrayToFile(Bytes,*Temp)) return false;
		IFileManager& FM=IFileManager::Get();
		const bool Saved=FM.Move(*Path,*Temp,true,true,false,true);
		if (!Saved) FM.Delete(*Temp,false,true);
		return Saved;
	}

	bool LoadPrepared(uint32 PCode, uint64 DatFingerprint, FACETerrainBlend& OutBlend)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ACE_LoadPreparedLandMips);
		const FString Path=MakeCacheFilePath(PCode,DatFingerprint);
		// Validate sizes before allocating. A corrupt cache is a cache miss, never
		// a partially published texture or an unbounded allocation during login.
		const int64 FileSize=IFileManager::Get().FileSize(*Path);
		if (FileSize<36 || FileSize>24*1024*1024) return false;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes,*Path) || Bytes.Num()!=FileSize) return false;
		uint32 CRC=0; FMemory::Memcpy(&CRC,Bytes.GetData()+Bytes.Num()-4,4);
		if (CRC!=FCrc::MemCrc32(Bytes.GetData(),Bytes.Num()-4)) return false;
		FMemoryReader Ar(Bytes,true);
		uint32 FileMagic=0,Version=0,Code=0; uint64 Fingerprint=0; int32 Count=0;
		FACETerrainBlend Loaded;
		if (!ReadPod(Ar,FileMagic) || FileMagic!=Magic || !ReadPod(Ar,Version) || Version!=SchemaVersion
			|| !ReadPod(Ar,Code) || Code!=PCode || !ReadPod(Ar,Fingerprint) || Fingerprint!=DatFingerprint
			|| !ReadPod(Ar,Loaded.Width) || !ReadPod(Ar,Loaded.Height) || !ReadPod(Ar,Count)
			|| Loaded.Width<=0 || Loaded.Height<=0 || Loaded.Width>2048 || Loaded.Height>2048
			|| Count!=FMath::FloorLog2(FMath::Max(Loaded.Width,Loaded.Height))+1) return false;
		int32 W=Loaded.Width,H=Loaded.Height;
		for (int32 I=0;I<Count;++I)
		{
			auto& Mip=Loaded.UploadMips.AddDefaulted_GetRef(); uint8 Packed=0; int32 Size=0;
			if (!ReadPod(Ar,Mip.RawBytes) || Mip.RawBytes!=W*H*4 || !ReadPod(Ar,Packed) || Packed>1
				|| !ReadPod(Ar,Size) || Size<=0 || Size>Mip.RawBytes || (!Packed && Size!=Mip.RawBytes)
				|| Size>Ar.TotalSize()-Ar.Tell()-4) return false;
			Mip.bPacked=Packed!=0; Mip.Data.SetNumUninitialized(Size); Ar.Serialize(Mip.Data.GetData(),Size);
			W=FMath::Max(1,W/2); H=FMath::Max(1,H/2);
		}
		if (Ar.IsError() || Ar.Tell()!=Ar.TotalSize()-4) return false;
		OutBlend=MoveTemp(Loaded); ACEDiskTileCache::Touch(Path); return true;
	}

	bool Save(uint32 PCode, uint64 DatFingerprint, int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		if (Width<=0 || Height<=0 || Width>2048 || Height>2048 || Pixels.Num()!=Width*Height) return false;
		FACETerrainBlend Blend; Blend.Width=Width; Blend.Height=Height; Blend.Pixels=Pixels;
		return Blend.PrepareUploadMips() && SavePrepared(PCode,DatFingerprint,Blend);
	}

	bool Load(uint32 PCode, uint64 DatFingerprint, int32& OutWidth, int32& OutHeight, TArray<FColor>& OutPixels)
	{
		FACETerrainBlend Blend;
		if (!LoadPrepared(PCode,DatFingerprint,Blend)) return false;
		auto Pixels=Blend.CopyBasePixels(); if (Pixels.Num()!=Blend.Width*Blend.Height) return false;
		OutWidth=Blend.Width; OutHeight=Blend.Height; OutPixels=MoveTemp(Pixels); return true;
	}
}
