#pragma once

class FACEWorldBakeByteReader;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeSetupLight
{
	FACEWorldBakeSetupLight();
	//
	int32 LightIndex;
	//
	FACEWorldBakeResourceLocation Location;
	//
	int32 Color;
	//
	float Intensity;
	//
	float Falloff;
	//
	float ConeAngle;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalSetup
{
	FACEWorldBakePortalSetup();
	FACEWorldBakePortalSetup(FACEWorldBakeByteReader& Reader);
	//
	uint32 ResourceId;
	//
	uint32 Flags;
	//
	TArray<int32> ModelResourceIds;
	//
	TArray<int32> ModelParents;
	//
	TArray<FVector3f> ModelScales;
	//
	TMap<int32, FACEWorldBakeResourceInfo> HoldingLocations;
	//
	TMap<int32, FACEWorldBakeResourceInfo> ConnectionPoints;
	//
	TArray<int32> OrderedPlacementKeys;
	//
	TMap<int32, TArray<FACEWorldBakeResourceLocation>> PlacementFrames;
	//
	int32 LightCount;
	//
	TArray<FACEWorldBakeSetupLight> Lights;
	//
	int32 DefaultAnimId;
	//
	int32 DefaultPhysScriptId;
	//
	int32 DefaultMotionTableId;
	//
	int32 DefaultSoundTableId;
	//
	int32 DefaultPhysScriptTableId;
};
