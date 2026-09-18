#pragma once

#include "Kismet/KismetSystemLibrary.h"
#include "ACEWorldBakeBlueprintFunctionLibrary.generated.h"

class AWorldSettings;
class ULevelStreaming;

UCLASS()
class UACEWorldBakeFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	// Get the world settings for World, useful for BP access to i.e. origin shifting
	UFUNCTION(BlueprintCallable, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static AWorldSettings* GetWorldSettings(UObject* WorldContextObject);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static ULevelStreaming* FindStreamingLevel(const ULevel* Level);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static void GetACEWorldBakeWorldSize(int32& OutLandscapeTileDimension, float& OutLandscapeTileSize, float& OutLandscapeSize);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static FVector3f ACVectorToUnreal(FVector3f ACVector);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static FVector3f UnrealVectorToAC(FVector3f Vector);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static FQuat4f ACHeadingToUnreal(FVector4f ACHeading);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static void UnrealVectorToACLandblock(FVector3f Vector, int32& OutLandblockX, int32& OutLandblockY);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static void UnrealVectorToTile(FVector3f Vector, int32& OutTileX, int32& OutTileY);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static void ACLandblockToUnrealTile(int32 LandblockX, int32 LandblockY, int32& OutTileX, int32& OutTileY);

	//
	UFUNCTION(BlueprintPure, Category="ACEWorldBake", meta=(WorldContext="WorldContextObject"))
	static void UnrealTileToACLandblock(int32 TileX, int32 TileY, int32& OutFirstLandblockX, int32& OutLastLandblockX, int32& OutFirstLandblockY, int32& OutLastLandblockY);
};
