#include "Dat/ACETerrainBlendCache.h"

FACETerrainBlendCache::FBlendPtr FACETerrainBlendCache::Find(const FKey& Key)
{
    FScopeLock Lock(&Mutex);
    if (FEntry* Entry = Entries.Find(Key))
    {
        Entry->LastUse = ++Clock;
        return Entry->Live.Pin();
    }
    return nullptr;
}

FACETerrainBlendCache::FBlendPtr FACETerrainBlendCache::GetOrBuild(uint32 PCode, uint64 Fingerprint,
    TFunctionRef<FBlendPtr()> Build)
{
    const FKey Key{PCode, Fingerprint};
    if (FBlendPtr Found = Find(Key)) return Found;
    // Builders requesting the same blend share one bake/write. Different blends
    // can still build in parallel; never hold the map lock during DAT or disk I/O.
    FScopeLock BuildLock(&BuildMutexes[GetTypeHash(Key) % UE_ARRAY_COUNT(BuildMutexes)]);
    if (FBlendPtr Found = Find(Key)) return Found;
    FBlendPtr Built = Build();
    if (!Built || Built->Width <= 0 || Built->Height <= 0
        || int64(Built->Pixels.Num()) != int64(Built->Width) * Built->Height) return nullptr;
    FScopeLock Lock(&Mutex);
    FEntry& Entry = Entries.FindOrAdd(Key);
    Entry.Live = Built;
    Entry.Retained = Built;
    Entry.LastUse = ++Clock;
    RetainedBytes += Built->Pixels.GetAllocatedSize();
    while (RetainedBytes > BudgetBytes)
    {
        FEntry* Oldest = nullptr;
        for (auto& Pair : Entries)
            if (Pair.Value.Retained && (!Oldest || Pair.Value.LastUse < Oldest->LastUse)) Oldest = &Pair.Value;
        if (!Oldest) break;
        RetainedBytes -= Oldest->Retained->Pixels.GetAllocatedSize();
        Oldest->Retained.Reset();
    }
    // Visible meshes own their pixels. Weak entries reuse those even after the
    // optional 128 MiB recent-use retention has evicted its strong reference.
    if (Entries.Num() > 4096)
        for (auto It = Entries.CreateIterator(); It; ++It)
            if (!It.Value().Live.IsValid()) It.RemoveCurrent();
    return Built;
}

uint64 FACETerrainBlendCache::GetRetainedBytes() const
{
    FScopeLock Lock(&Mutex);
    return RetainedBytes;
}
