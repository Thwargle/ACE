#include "ACEWorldBakeBlueprintFunctionLibrary.h"
#include "LevelUtils.h"

const float kACUnitsToUnreal = 100.0f; // AC units to Unreal units (meters to centimeters)
const float kUnrealUnitsToAC = 0.01f; // Unreal units to AC units (centimeters to meters)

const int32 kACLandblockCount = 256; // landblocks in a row/column of the AC world
const int32 kACLandblockCountSquared = kACLandblockCount * kACLandblockCount; // landblocks in a grid of the AC world
const int32 kACLandblockVertexCount = 9; // vertices in a row/column of an AC landblock
const int32 kACLandblockVertexCountSquared = kACLandblockVertexCount * kACLandblockVertexCount; // vertices in a grid of an AC landblock
const float kACLandblockVertexSpacing = 24.0f; // distance in AC units between landblock vertices
const float kACLandblockSize = kACLandblockVertexSpacing * (kACLandblockVertexCount - 1); // distance in AC units between poles of landblock vertices
const float kACWorldSize = kACLandblockSize * kACLandblockCount; // size in AC units of the entire landblock grid
const int32 kACWorldVertexCount = kACLandblockCount * (kACLandblockVertexCount - 1) + 1; // 2049

const int32 kLandscapeTileCount = 8; // landscape tiles in a row/column
const int32 kLandscapeTileCountSquared = kLandscapeTileCount * kLandscapeTileCount; // landscape tiles in a grid
const int32 kLandscapeComponentCount = 17; // landscape components in a row/column of each tile
const int32 kLandscapeComponentCountSquared = kLandscapeComponentCount * kLandscapeComponentCount; // landscape components in a grid per-tile
const int32 kLandscapeQuadCount = 15; // quads in a row/column of each component
const int32 kLandscapeQuadCountSquared = kLandscapeQuadCount * kLandscapeQuadCount; // quads in a grid per-component
const int32 kLandscapeQuadCountPerTile = kLandscapeComponentCountSquared * kLandscapeQuadCountSquared; // quads in a grid per-tile
const float kLandscapeTileSize = kACLandblockVertexSpacing * kACUnitsToUnreal * kLandscapeComponentCount * kLandscapeQuadCount; // size of x/y dimension per-tile
const float kLandscapeWorldSize = kLandscapeTileSize * kLandscapeTileCount; // size of width/height of the entire landscape grid
const int32 kLandscapeVertexCount = kLandscapeTileCount * kLandscapeComponentCount * kLandscapeQuadCount + 1; // 2041

const int32 kACLandblocksPerUnrealTile = kACLandblockCount / kLandscapeTileCount; // Number of AC landblocks in a row/column per-tile [unreal]
const int32 kACLandblocksPerTileSquared = kACLandblocksPerUnrealTile * kACLandblocksPerUnrealTile; // Number of AC landblocks in a grid [unreal]

UACEWorldBakeFunctionLibrary::UACEWorldBakeFunctionLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

AWorldSettings* UACEWorldBakeFunctionLibrary::GetWorldSettings(UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	return World ? World->GetWorldSettings() : nullptr;
}

ULevelStreaming* UACEWorldBakeFunctionLibrary::FindStreamingLevel(const ULevel* Level)
{
	return FLevelUtils::FindStreamingLevel(Level);
}

void UACEWorldBakeFunctionLibrary::GetACEWorldBakeWorldSize(int32& OutLandscapeTileDimension, float& OutLandscapeTileSize, float& OutLandscapeSize)
{
	OutLandscapeTileDimension = kLandscapeTileCount;
	OutLandscapeTileSize = kLandscapeTileSize;
	OutLandscapeSize = kLandscapeWorldSize;
}

FVector3f UACEWorldBakeFunctionLibrary::ACVectorToUnreal(FVector3f ACVector)
{
	return FVector3f(ACVector.X, -ACVector.Y, ACVector.Z) * kACUnitsToUnreal;
}

FVector3f UACEWorldBakeFunctionLibrary::UnrealVectorToAC(FVector3f Vector)
{
	return FVector3f(Vector.X, -Vector.Y, Vector.Z) * kUnrealUnitsToAC;
}

FQuat4f UACEWorldBakeFunctionLibrary::ACHeadingToUnreal(FVector4f ACHeading)
{
	return FQuat4f(-ACHeading.X, ACHeading.Y, -ACHeading.Z, ACHeading.W);
}

void UACEWorldBakeFunctionLibrary::UnrealVectorToACLandblock(FVector3f Vector, int32& OutLandblockX, int32& OutLandblockY)
{
	const FVector3f ACVector = UnrealVectorToAC(Vector);

	const float ToLandblockX = ACVector.X / kACLandblockSize;
	const float ToLandblockY = ACVector.Y / kACLandblockSize;
	const float RoundedX = ToLandblockX < 0.0f ? FMath::RoundFromZero(ToLandblockX) : FMath::RoundToZero(ToLandblockX);
	const float RoundedY = ToLandblockY < 0.0f ? FMath::RoundFromZero(ToLandblockY) : FMath::RoundToZero(ToLandblockY);

	OutLandblockX = static_cast<int32>(RoundedX) + (kACLandblockCount / 2);
	OutLandblockY = static_cast<int32>(RoundedY) + (kACLandblockCount / 2);
}

void UACEWorldBakeFunctionLibrary::UnrealVectorToTile(FVector3f Vector, int32& OutTileX, int32& OutTileY)
{
	const float ToTileX = Vector.X / kLandscapeTileSize;
	const float ToTileY = Vector.Y / kLandscapeTileSize;
	const float RoundedX = ToTileX < 0.0f ? FMath::RoundFromZero(ToTileX) : FMath::RoundToZero(ToTileX);
	const float RoundedY = ToTileY < 0.0f ? FMath::RoundFromZero(ToTileY) : FMath::RoundToZero(ToTileY);
	
	OutTileX = static_cast<int32>(RoundedX) + (kLandscapeTileCount / 2);
	OutTileY = static_cast<int32>(RoundedY) + (kLandscapeTileCount / 2);
}

void UACEWorldBakeFunctionLibrary::ACLandblockToUnrealTile(int32 LandblockX, int32 LandblockY, int32& OutTileX, int32& OutTileY)
{
	OutTileX = LandblockX / kACLandblocksPerUnrealTile;
}

void UACEWorldBakeFunctionLibrary::UnrealTileToACLandblock(int32 TileX, int32 TileY, int32& OutFirstLandblockX, int32& OutLastLandblockX, int32& OutFirstLandblockY, int32& OutLastLandblockY)
{
	OutFirstLandblockX = TileX * kACLandblocksPerUnrealTile;
	OutLastLandblockX = ((TileX + 1) * kACLandblocksPerUnrealTile) - 1;
	OutFirstLandblockY = TileY * kACLandblocksPerUnrealTile;
	OutLastLandblockY = ((TileY + 1) * kACLandblocksPerUnrealTile) - 1;
}
