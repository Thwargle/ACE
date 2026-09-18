#include "Dat/ACELandblockMeshCache.h"
#include "Dat/ACEDiskTileCache.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ACELandblockMeshCache
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

		void WriteF32Vec3(TArray<uint8>& Buf, const FVector& V)
		{
			const float X = static_cast<float>(V.X);
			const float Y = static_cast<float>(V.Y);
			const float Z = static_cast<float>(V.Z);
			WritePod(Buf, X);
			WritePod(Buf, Y);
			WritePod(Buf, Z);
		}

		bool ReadF32Vec3(FMemoryReader& Ar, FVector& Out)
		{
			float X = 0.f, Y = 0.f, Z = 0.f;
			if (!ReadPod(Ar, X) || !ReadPod(Ar, Y) || !ReadPod(Ar, Z))
			{
				return false;
			}
			Out = FVector(X, Y, Z);
			return true;
		}

		void WriteF32Vec2(TArray<uint8>& Buf, const FVector2D& V)
		{
			const float X = static_cast<float>(V.X);
			const float Y = static_cast<float>(V.Y);
			WritePod(Buf, X);
			WritePod(Buf, Y);
		}

		bool ReadF32Vec2(FMemoryReader& Ar, FVector2D& Out)
		{
			float X = 0.f, Y = 0.f;
			if (!ReadPod(Ar, X) || !ReadPod(Ar, Y))
			{
				return false;
			}
			Out = FVector2D(X, Y);
			return true;
		}

		void WriteLinearColor(TArray<uint8>& Buf, const FLinearColor& C)
		{
			WritePod(Buf, C.R);
			WritePod(Buf, C.G);
			WritePod(Buf, C.B);
			WritePod(Buf, C.A);
		}

		bool ReadLinearColor(FMemoryReader& Ar, FLinearColor& Out)
		{
			return ReadPod(Ar, Out.R) && ReadPod(Ar, Out.G) && ReadPod(Ar, Out.B) && ReadPod(Ar, Out.A);
		}

		bool SerializePayload(TArray<uint8>& Buf, const FACEBuiltLandblockMesh& Mesh)
		{
			WritePod(Buf, Mesh.LandblockId);
			const uint32 NumSections = static_cast<uint32>(Mesh.Sections.Num());
			WritePod(Buf, NumSections);
			for (const FACEBuiltLandblockSection& Sec : Mesh.Sections)
			{
				WritePod(Buf, Sec.PCode);
				WritePod(Buf, static_cast<uint32>(Sec.Vertices.Num()));
				for (const FVector& V : Sec.Vertices)
				{
					WriteF32Vec3(Buf, V);
				}
				WritePod(Buf, static_cast<uint32>(Sec.Triangles.Num()));
				for (int32 T : Sec.Triangles)
				{
					WritePod(Buf, T);
				}
				WritePod(Buf, static_cast<uint32>(Sec.Normals.Num()));
				for (const FVector& N : Sec.Normals)
				{
					WriteF32Vec3(Buf, N);
				}
				WritePod(Buf, static_cast<uint32>(Sec.UVs.Num()));
				for (const FVector2D& UV : Sec.UVs)
				{
					WriteF32Vec2(Buf, UV);
				}
				const uint8 bGpu = Sec.bGpuTexMerge ? 1 : 0;
				WritePod(Buf, bGpu);
				WritePod(Buf, static_cast<uint32>(Sec.UV1.Num()));
				for (const FVector2D& UV : Sec.UV1)
				{
					WriteF32Vec2(Buf, UV);
				}
				WritePod(Buf, static_cast<uint32>(Sec.UV2.Num()));
				for (const FVector2D& UV : Sec.UV2)
				{
					WriteF32Vec2(Buf, UV);
				}
				WritePod(Buf, static_cast<uint32>(Sec.UV3.Num()));
				for (const FVector2D& UV : Sec.UV3)
				{
					WriteF32Vec2(Buf, UV);
				}
				WritePod(Buf, static_cast<uint32>(Sec.VertexColors.Num()));
				for (const FLinearColor& C : Sec.VertexColors)
				{
					WriteLinearColor(Buf, C);
				}
				// v7: never persist BakedPixels (shared PCode tile cache). Keep dims as 0.
				const int32 BakeW = 0;
				const int32 BakeH = 0;
				WritePod(Buf, BakeW);
				WritePod(Buf, BakeH);
				WritePod(Buf, static_cast<uint32>(0));
			}

			const uint8 HasHeights = Mesh.bHasHeights ? 1 : 0;
			const uint8 HasTerrain = Mesh.bHasTerrain ? 1 : 0;
			WritePod(Buf, HasHeights);
			WritePod(Buf, HasTerrain);
			for (int32 i = 0; i < 81; ++i)
			{
				WritePod(Buf, Mesh.HeightsAc[i]);
			}
			for (int32 i = 0; i < 81; ++i)
			{
				WritePod(Buf, Mesh.Terrain[i]);
			}
			const uint8 HasCellWater = Mesh.bHasCellWater ? 1 : 0;
			WritePod(Buf, HasCellWater);
			for (int32 i = 0; i < 64; ++i)
			{
				WritePod(Buf, Mesh.CellWaterType[i]);
			}
			WritePod(Buf, Mesh.BlockWaterType);
			return true;
		}

		bool DeserializePayload(FMemoryReader& Ar, FACEBuiltLandblockMesh& Out)
		{
			Out = FACEBuiltLandblockMesh();
			if (!ReadPod(Ar, Out.LandblockId))
			{
				return false;
			}
			uint32 NumSections = 0;
			if (!ReadPod(Ar, NumSections) || NumSections > 4096)
			{
				return false;
			}
			Out.Sections.SetNum(static_cast<int32>(NumSections));
			for (FACEBuiltLandblockSection& Sec : Out.Sections)
			{
				uint32 N = 0;
				if (!ReadPod(Ar, Sec.PCode) || !ReadPod(Ar, N) || N > 200000)
				{
					return false;
				}
				Sec.Vertices.SetNum(static_cast<int32>(N));
				for (FVector& V : Sec.Vertices)
				{
					if (!ReadF32Vec3(Ar, V))
					{
						return false;
					}
				}
				if (!ReadPod(Ar, N) || N > 600000)
				{
					return false;
				}
				Sec.Triangles.SetNum(static_cast<int32>(N));
				for (int32& T : Sec.Triangles)
				{
					if (!ReadPod(Ar, T))
					{
						return false;
					}
				}
				if (!ReadPod(Ar, N) || N > 200000)
				{
					return false;
				}
				Sec.Normals.SetNum(static_cast<int32>(N));
				for (FVector& Nr : Sec.Normals)
				{
					if (!ReadF32Vec3(Ar, Nr))
					{
						return false;
					}
				}
				if (!ReadPod(Ar, N) || N > 200000)
				{
					return false;
				}
				Sec.UVs.SetNum(static_cast<int32>(N));
				for (FVector2D& UV : Sec.UVs)
				{
					if (!ReadF32Vec2(Ar, UV))
					{
						return false;
					}
				}
				uint8 bGpu = 0;
				if (!ReadPod(Ar, bGpu))
				{
					return false;
				}
				Sec.bGpuTexMerge = bGpu != 0;
				auto ReadUVChannel = [&](TArray<FVector2D>& Dest) -> bool
				{
					uint32 Count = 0;
					if (!ReadPod(Ar, Count) || Count > 200000)
					{
						return false;
					}
					Dest.SetNum(static_cast<int32>(Count));
					for (FVector2D& UV : Dest)
					{
						if (!ReadF32Vec2(Ar, UV))
						{
							return false;
						}
					}
					return true;
				};
				if (!ReadUVChannel(Sec.UV1) || !ReadUVChannel(Sec.UV2) || !ReadUVChannel(Sec.UV3))
				{
					return false;
				}
				if (!ReadPod(Ar, N) || N > 200000)
				{
					return false;
				}
				Sec.VertexColors.SetNum(static_cast<int32>(N));
				for (FLinearColor& C : Sec.VertexColors)
				{
					if (!ReadLinearColor(Ar, C))
					{
						return false;
					}
				}
				if (!ReadPod(Ar, Sec.BakeWidth) || !ReadPod(Ar, Sec.BakeHeight))
				{
					return false;
				}
				if (!ReadPod(Ar, N) || N > 2048u * 2048u)
				{
					return false;
				}
				Sec.BakedPixels.SetNum(static_cast<int32>(N));
				if (N > 0)
				{
					if (Ar.TotalSize() - Ar.Tell() < static_cast<int64>(N * sizeof(FColor)))
					{
						return false;
					}
					Ar.Serialize(Sec.BakedPixels.GetData(), N * sizeof(FColor));
					if (Ar.IsError())
					{
						return false;
					}
				}
			}

			uint8 HasHeights = 0, HasTerrain = 0;
			if (!ReadPod(Ar, HasHeights) || !ReadPod(Ar, HasTerrain))
			{
				return false;
			}
			Out.bHasHeights = HasHeights != 0;
			Out.bHasTerrain = HasTerrain != 0;
			for (int32 i = 0; i < 81; ++i)
			{
				if (!ReadPod(Ar, Out.HeightsAc[i]))
				{
					return false;
				}
			}
			for (int32 i = 0; i < 81; ++i)
			{
				if (!ReadPod(Ar, Out.Terrain[i]))
				{
					return false;
				}
			}
			uint8 HasCellWater = 0;
			if (!ReadPod(Ar, HasCellWater))
			{
				return false;
			}
			Out.bHasCellWater = HasCellWater != 0;
			for (int32 i = 0; i < 64; ++i)
			{
				if (!ReadPod(Ar, Out.CellWaterType[i]))
				{
					return false;
				}
			}
			if (!ReadPod(Ar, Out.BlockWaterType))
			{
				return false;
			}
			return !Ar.IsError();
		}
	}

	FString MakeCacheFilePath(uint32 LandblockId, float WorldScale, uint64 DatFingerprint)
	{
		const uint32 ScaleBits = *reinterpret_cast<const uint32*>(&WorldScale);
		return ACEDiskTileCache::GetLandblocksDir()
			/ FString::Printf(TEXT("%08X_%08X_%016llX.aclb"), LandblockId & 0xFFFF0000u, ScaleBits, DatFingerprint);
	}

	bool Save(const FString& Path, const FACEBuiltLandblockMesh& Mesh, float WorldScale, uint64 DatFingerprint)
	{
		TArray<uint8> Payload;
		Payload.Reserve(256 * 1024);
		if (!SerializePayload(Payload, Mesh))
		{
			return false;
		}

		TArray<uint8> Compressed;
		Compressed.AddUninitialized(FCompression::CompressMemoryBound(NAME_Zlib, Payload.Num()));
		int32 CompressedSize = Compressed.Num();
		if (!FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Payload.GetData(), Payload.Num()))
		{
			UE_LOG(LogTemp, Warning, TEXT("ACELandCache: zlib compress failed for %s"), *Path);
			return false;
		}
		Compressed.SetNum(CompressedSize);

		TArray<uint8> FileBytes;
		WritePod(FileBytes, Magic);
		WritePod(FileBytes, SchemaVersion);
		WritePod(FileBytes, Mesh.LandblockId);
		const uint32 ScaleBits = *reinterpret_cast<const uint32*>(&WorldScale);
		WritePod(FileBytes, ScaleBits);
		WritePod(FileBytes, DatFingerprint);
		WritePod(FileBytes, static_cast<uint32>(Payload.Num()));
		WritePod(FileBytes, static_cast<uint32>(Compressed.Num()));
		FileBytes.Append(Compressed);

		const FString TempPath = FString::Printf(
			TEXT("%s.%u_%u_%08x.tmp"),
			*Path,
			FPlatformProcess::GetCurrentProcessId(),
			FPlatformTLS::GetCurrentThreadId(),
			FPlatformTime::Cycles());
		if (!FFileHelper::SaveArrayToFile(FileBytes, *TempPath))
		{
			return false;
		}

		// Concurrent async+sync builds can race the same cache key. Unique temps avoid
		// clobbering one .tmp; replace without UE's noisy Move retry loop (Error 32/2 spam).
		IFileManager& FM = IFileManager::Get();
		const bool bReplaced = FM.Move(
			*Path,
			*TempPath,
			/*Replace*/ true,
			/*EvenIfReadOnly*/ true,
			/*Attributes*/ false,
			/*bDoNotRetryOrError*/ true);
		if (bReplaced)
		{
			return true;
		}

		// Another writer likely won the race — keep existing cache, drop our temp.
		FM.Delete(*TempPath, false, true);
		return FM.FileExists(*Path);
	}

	bool Load(const FString& Path, FACEBuiltLandblockMesh& OutMesh, float ExpectedWorldScale, uint64 ExpectedFingerprint)
	{
		if (Path.IsEmpty() || !IFileManager::Get().FileExists(*Path))
		{
			return false;
		}
		TArray<uint8> FileBytes;
		if (!FFileHelper::LoadFileToArray(FileBytes, *Path) || FileBytes.Num() < 40)
		{
			return false;
		}

		FMemoryReader Ar(FileBytes, true);
		uint32 FileMagic = 0, FileSchema = 0, LandblockId = 0, ScaleBits = 0;
		uint64 Fingerprint = 0;
		uint32 UncompressedSize = 0, CompressedSize = 0;
		if (!ReadPod(Ar, FileMagic) || FileMagic != Magic)
		{
			return false;
		}
		if (!ReadPod(Ar, FileSchema) || FileSchema != SchemaVersion)
		{
			return false;
		}
		if (!ReadPod(Ar, LandblockId) || !ReadPod(Ar, ScaleBits) || !ReadPod(Ar, Fingerprint))
		{
			return false;
		}
		if (!ReadPod(Ar, UncompressedSize) || !ReadPod(Ar, CompressedSize))
		{
			return false;
		}

		const uint32 ExpectedScaleBits = *reinterpret_cast<const uint32*>(&ExpectedWorldScale);
		if (ScaleBits != ExpectedScaleBits || Fingerprint != ExpectedFingerprint)
		{
			return false;
		}
		if (CompressedSize == 0 || UncompressedSize == 0 || UncompressedSize > 64u * 1024u * 1024u)
		{
			return false;
		}
		if (Ar.TotalSize() - Ar.Tell() < static_cast<int64>(CompressedSize))
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
			UE_LOG(LogTemp, Warning, TEXT("ACELandCache: zlib decompress failed for %s"), *Path);
			return false;
		}

		FMemoryReader PayloadAr(Payload, true);
		if (!DeserializePayload(PayloadAr, OutMesh))
		{
			return false;
		}
		if (OutMesh.LandblockId != (LandblockId & 0xFFFF0000u) && OutMesh.LandblockId != LandblockId)
		{
			return false;
		}
		ACEDiskTileCache::Touch(Path);
		return true;
	}
}
