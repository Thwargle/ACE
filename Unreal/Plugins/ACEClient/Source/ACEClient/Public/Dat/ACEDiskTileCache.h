#pragma once

#include "CoreMinimal.h"

/**
 * Disk tile caches under Saved/ACEClient/MeshCache.
 * Stale DAT fingerprints are purged and writable tiles have bounded disk budgets.
 */
namespace ACEDiskTileCache
{
	/** Soft budgets (bytes). Overages delete oldest files by mtime. */
	static constexpr uint64 LandblockBudgetBytes = 4ull << 30; // 4 GiB
	static constexpr uint64 EnvCellBudgetBytes = 2ull << 30;   // 2 GiB
	static constexpr uint64 PCodeBudgetBytes = 512ull << 20;   // 512 MiB
	static constexpr uint64 SceneryBudgetBytes = 128ull << 20; // 128 MiB

	FString GetRootDir();
	FString GetLandblocksDir();
	FString GetEnvCellsDir();
	FString GetPCodesDir();
	FString GetSceneryDir();
	/** Delete *.tmp leftovers and files whose name does not contain CurrentFingerprint. */
	int32 PurgeStaleFingerprint(const FString& Dir, uint64 CurrentFingerprint);

	/** Delete oldest files until total size <= MaxBytes. Returns files removed. */
	int32 EnforceBudget(const FString& Dir, uint64 MaxBytes);

	/** Bump mtime so LRU budget keeps recently used tiles. */
	void Touch(const FString& Path);

	/** Purge stale fingerprints and enforce disk budgets after DAT indexing. */
	void MaintainAfterDatLoad(uint64 CurrentFingerprint);
}
