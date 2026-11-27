#pragma once
#include "CoreMinimal.h"

struct ACECLIENT_API FACETerrainBlend
{
    int32 CellsAcross = 1; // Runtime projection for this palette and artwork selection.
    uint32 OverlaySurfaceId = 0; // A separate world-space diffuse over this cell-local RGB and coverage mask.
    struct FLayer
    {
        uint32 SurfaceId = 0;
        // AC2 repeats per world tile (positive) or AC1 repeats per cell (negative).
        float Repeat = 0.f;
    };
    FLayer Layers[5] = {}; // Base, up to three terrain transitions, then road.
    bool IsLayerMask() const { return Layers[0].SurfaceId != 0; }
    TArray<FColor> Pixels;
    int32 Width = 0;
    int32 Height = 0;

    // Prepared on the terrain worker, before publishing this immutable blend.
    // Lossless CPU backup only: the GPU still receives full-resolution BGRA8.
    bool PrepareUploadMips();
    bool IsValid() const;
    bool CopyMip(int32 Index, void* Destination, int64 Bytes) const;
    TArray<FColor> CopyBasePixels() const;
    uint64 GetAllocatedSize() const;
    struct FMip
    {
        TArray<uint8> Data;
        int32 RawBytes = 0;
        bool bPacked = false;
    };
    TArray<FMip> UploadMips;
};

/** Shared immutable pixels for the current DAT session, including background builders. */
class ACECLIENT_API FACETerrainBlendCache
{
public:
    using FBlendPtr = TSharedPtr<const FACETerrainBlend, ESPMode::ThreadSafe>;
    explicit FACETerrainBlendCache(uint64 InBudgetBytes = 128ull << 20) : BudgetBytes(InBudgetBytes) {}
    FBlendPtr GetOrBuild(uint32 PCode, uint64 Fingerprint, TFunctionRef<FBlendPtr()> Build);
    uint64 GetRetainedBytes() const;
private:
    struct FKey
    {
        uint32 Code;
        uint64 Fingerprint;
        bool operator==(const FKey& Other) const { return Code == Other.Code && Fingerprint == Other.Fingerprint; }
        friend uint32 GetTypeHash(const FKey& Key) { return HashCombine(GetTypeHash(Key.Code), GetTypeHash(Key.Fingerprint)); }
    };
    struct FEntry
    {
        TWeakPtr<const FACETerrainBlend, ESPMode::ThreadSafe> Live;
        FBlendPtr Retained;
        uint64 LastUse = 0;
    };
    FBlendPtr Find(const FKey& Key);
    mutable FCriticalSection Mutex;
    FCriticalSection BuildMutexes[16];
    TMap<FKey, FEntry> Entries;
    const uint64 BudgetBytes;
    uint64 RetainedBytes = 0;
    uint64 Clock = 0;
};
