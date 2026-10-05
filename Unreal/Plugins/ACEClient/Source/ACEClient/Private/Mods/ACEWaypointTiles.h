#pragma once
#include "CoreMinimal.h"
#include "Brushes/SlateImageBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Texture2D.h"

// Independent DAT readers and serial worker: no world actors, collision meshes,
// game-thread terrain bakes or unbounded texture residency for the atlas.
class FACEWaypointTiles
{
public:
    FACEWaypointTiles();
    ~FACEWaypointTiles();
    void Update(const FString& Directory,const FBox2D& Visible,double PixelsPerCoordinate);
    struct FTile
    {
        FIntVector Key; // landblock x/y and number of landblocks across
        TStrongObjectPtr<UTexture2D> Texture;
        FSlateBrush Brush;
        uint64 Used=0;
        int32 Resolution=256;
    };
    const TArray<TSharedPtr<FTile>>& GetTiles() const{return Tiles;}
    static FBox2D Bounds(FIntVector Key);
    static FIntPoint Detail(double PixelsPerCoordinate);
    bool IsLoading() const{return Loading;}
private:
    struct FWorker;
    TSharedPtr<FWorker,ESPMode::ThreadSafe> Worker;
    TArray<TSharedPtr<FTile>> Tiles;
    FString Source;
    uint64 Frame=0;
    bool Loading=false;
};
