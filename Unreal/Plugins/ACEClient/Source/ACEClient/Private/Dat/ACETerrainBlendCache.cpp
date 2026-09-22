#include "Dat/ACETerrainBlendCache.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Misc/Compression.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FACETerrainBlend::IsValid() const
{
    return Width > 0 && Height > 0 && (Pixels.Num() == int64(Width)*Height
        || (!UploadMips.IsEmpty() && UploadMips[0].RawBytes == int64(Width)*Height*sizeof(FColor)));
}

bool FACETerrainBlend::PrepareUploadMips()
{
    if (!UploadMips.IsEmpty()) return IsValid();
    if (!IsValid()) return false;
    TRACE_CPUPROFILER_EVENT_SCOPE(ACE_PrepareLandMips);
    TArray<FColor> Current = MoveTemp(Pixels);
    int32 W = Width, H = Height;
    for (;;)
    {
        FMip& Mip = UploadMips.AddDefaulted_GetRef();
        Mip.RawBytes = Current.Num()*sizeof(FColor);
        // Group channels before LZ4: interleaved noisy RGB hides repeated alpha
        // and color runs. This is a reversible byte shuffle, not quantization.
        TArray<uint8> Planar;
        Planar.SetNumUninitialized(Mip.RawBytes);
        const uint8* Interleaved = reinterpret_cast<const uint8*>(Current.GetData());
        const int32 Count = Current.Num();
        for (int32 I = 0; I < Count; ++I)
            for (int32 C = 0; C < 4; ++C) Planar[C*Count + I] = Interleaved[4*I + C];
        int32 PackedBytes = FCompression::CompressMemoryBound(NAME_LZ4, Mip.RawBytes);
        Mip.Data.SetNumUninitialized(PackedBytes);
        Mip.bPacked = FCompression::CompressMemory(NAME_LZ4, Mip.Data.GetData(), PackedBytes,
            Planar.GetData(), Mip.RawBytes) && PackedBytes < Mip.RawBytes;
        if (Mip.bPacked) Mip.Data.SetNum(PackedBytes);
        else
        {
            Mip.Data.SetNumUninitialized(Mip.RawBytes);
            FMemory::Memcpy(Mip.Data.GetData(), Current.GetData(), Mip.RawBytes);
        }
        Mip.Data.Shrink();
        if (W == 1 && H == 1) break;
        TArray<FColor> Next;
        FACEDatTextureResolver::BuildOpaqueMip(Current, W, H, Next);
        Current = MoveTemp(Next);
        W = FMath::Max(1, W/2); H = FMath::Max(1, H/2);
    }
    return true;
}

bool FACETerrainBlend::CopyMip(int32 Index, void* Destination, int64 Bytes) const
{
    if (!Destination || !UploadMips.IsValidIndex(Index)) return false;
    const FMip& Mip = UploadMips[Index];
    if (Bytes != Mip.RawBytes) return false;
    if (Mip.bPacked)
    {
        TArray<uint8> Planar;
        Planar.SetNumUninitialized(Mip.RawBytes);
        if (!FCompression::UncompressMemory(NAME_LZ4, Planar.GetData(), Bytes, Mip.Data.GetData(), Mip.Data.Num())) return false;
        uint8* Interleaved = static_cast<uint8*>(Destination);
        const int32 Count = Mip.RawBytes/sizeof(FColor);
        for (int32 I = 0; I < Count; ++I)
            for (int32 C = 0; C < 4; ++C) Interleaved[4*I + C] = Planar[C*Count + I];
        return true;
    }
    FMemory::Memcpy(Destination, Mip.Data.GetData(), Bytes);
    return true;
}

TArray<FColor> FACETerrainBlend::CopyBasePixels() const
{
    if (!Pixels.IsEmpty()) return Pixels;
    TArray<FColor> Result;
    if (IsValid())
    {
        Result.SetNumUninitialized(Width*Height);
        if (!CopyMip(0, Result.GetData(), int64(Result.Num())*sizeof(FColor))) Result.Empty();
    }
    return Result;
}

uint64 FACETerrainBlend::GetAllocatedSize() const
{
    uint64 Bytes = Pixels.GetAllocatedSize() + UploadMips.GetAllocatedSize();
    for (const FMip& Mip : UploadMips) Bytes += Mip.Data.GetAllocatedSize();
    return Bytes;
}

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
    if (!Built || !Built->IsValid()) return nullptr;
    FScopeLock Lock(&Mutex);
    FEntry& Entry = Entries.FindOrAdd(Key);
    Entry.Live = Built;
    Entry.Retained = Built;
    Entry.LastUse = ++Clock;
    RetainedBytes += Built->GetAllocatedSize();
    while (RetainedBytes > BudgetBytes)
    {
        FEntry* Oldest = nullptr;
        for (auto& Pair : Entries)
            if (Pair.Value.Retained && (!Oldest || Pair.Value.LastUse < Oldest->LastUse)) Oldest = &Pair.Value;
        if (!Oldest) break;
        RetainedBytes -= Oldest->Retained->GetAllocatedSize();
        Oldest->Retained.Reset();
    }
    // Visible meshes own their mip sources. Weak entries reuse those even after the
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
