#include "Dat/ACEDiskTileCache.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

namespace ACEDiskTileCache
{
	namespace
	{
		FString EnsureDir(const FString& Rel)
		{
			const FString Dir = GetRootDir() / Rel;
			IFileManager::Get().MakeDirectory(*Dir, true);
			return Dir;
		}

		bool FilenameHasFingerprint(const FString& Filename, uint64 Fingerprint)
		{
			const FString Needle = FString::Printf(TEXT("%016llX"), Fingerprint);
			return Filename.Contains(Needle, ESearchCase::IgnoreCase);
		}
	}

	FString GetRootDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("ACEClient") / TEXT("MeshCache");
	}

	FString GetLandblocksDir() { return EnsureDir(TEXT("Landblocks")); }
	FString GetEnvCellsDir() { return EnsureDir(TEXT("EnvCells")); }
	FString GetPCodesDir() { return EnsureDir(TEXT("PCodes")); }
	FString GetSceneryDir() { return EnsureDir(TEXT("Scenery")); }
	int32 PurgeStaleFingerprint(const FString& Dir, uint64 CurrentFingerprint)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*")), true, false);
		int32 Removed = 0;
		int64 Freed = 0;
		for (const FString& Name : Files)
		{
			const FString Path = Dir / Name;
			const bool bTmp = Name.EndsWith(TEXT(".tmp"), ESearchCase::IgnoreCase)
				|| Name.Contains(TEXT(".tmp."), ESearchCase::IgnoreCase);
			const bool bStale = !bTmp && CurrentFingerprint != 0
				&& !FilenameHasFingerprint(Name, CurrentFingerprint);
			if (!bTmp && !bStale)
			{
				continue;
			}
			const int64 Sz = IFileManager::Get().FileSize(*Path);
			if (IFileManager::Get().Delete(*Path, false, true))
			{
				++Removed;
				if (Sz > 0)
				{
					Freed += Sz;
				}
			}
		}
		if (Removed > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("ACEDiskTileCache: purged %d stale/tmp files from %s (freed %.1f MB, keep fp=%016llX)"),
				Removed, *Dir, Freed / (1024.0 * 1024.0), CurrentFingerprint);
		}
		return Removed;
	}

	int32 EnforceBudget(const FString& Dir, uint64 MaxBytes)
	{
		if (MaxBytes == 0)
		{
			return 0;
		}

		struct FEntry
		{
			FString Path;
			int64 Size = 0;
			FDateTime MTime;
		};
		TArray<FEntry> Entries;
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*")), true, false);
		uint64 Total = 0;
		for (const FString& Name : Files)
		{
			if (Name.EndsWith(TEXT(".tmp"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			FEntry E;
			E.Path = Dir / Name;
			E.Size = IFileManager::Get().FileSize(*E.Path);
			if (E.Size <= 0)
			{
				continue;
			}
			E.MTime = IFileManager::Get().GetTimeStamp(*E.Path);
			Total += static_cast<uint64>(E.Size);
			Entries.Add(MoveTemp(E));
		}
		if (Total <= MaxBytes)
		{
			return 0;
		}

		Entries.Sort([](const FEntry& A, const FEntry& B)
		{
			return A.MTime < B.MTime;
		});

		int32 Removed = 0;
		uint64 Freed = 0;
		for (const FEntry& E : Entries)
		{
			if (Total <= MaxBytes)
			{
				break;
			}
			if (IFileManager::Get().Delete(*E.Path, false, true))
			{
				Total -= static_cast<uint64>(E.Size);
				Freed += static_cast<uint64>(E.Size);
				++Removed;
			}
		}
		if (Removed > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("ACEDiskTileCache: budget trim %s — removed %d files (%.1f MB), now ~%.1f MB (cap %.1f MB)"),
				*Dir, Removed, Freed / (1024.0 * 1024.0),
				Total / (1024.0 * 1024.0), MaxBytes / (1024.0 * 1024.0));
		}
		return Removed;
	}

	void Touch(const FString& Path)
	{
		if (Path.IsEmpty() || !IFileManager::Get().FileExists(*Path))
		{
			return;
		}
		IFileManager::Get().SetTimeStamp(*Path, FDateTime::UtcNow());
	}

	void MaintainAfterDatLoad(uint64 CurrentFingerprint)
	{
		PurgeStaleFingerprint(GetLandblocksDir(), CurrentFingerprint);
		PurgeStaleFingerprint(GetEnvCellsDir(), CurrentFingerprint);
		PurgeStaleFingerprint(GetPCodesDir(), CurrentFingerprint);
		PurgeStaleFingerprint(GetSceneryDir(), CurrentFingerprint);

		EnforceBudget(GetLandblocksDir(), LandblockBudgetBytes);
		EnforceBudget(GetEnvCellsDir(), EnvCellBudgetBytes);
		EnforceBudget(GetPCodesDir(), PCodeBudgetBytes);
		EnforceBudget(GetSceneryDir(), SceneryBudgetBytes);
	}
}
