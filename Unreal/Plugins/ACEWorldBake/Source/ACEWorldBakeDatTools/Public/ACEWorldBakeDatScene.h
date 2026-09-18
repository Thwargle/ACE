#pragma once

class FACEWorldBakeByteReader;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalSceneObject
{
	//
	FACEWorldBakeResourceInfo ResourceInfo;
	//
	float Frequency;
	//
	float DisplaceX;
	//
	float DisplaceY;
	//
	float MinScale;
	//
	float MaxScale;
	//
	float MaxRotation;
	//
	float MinSlope;
	//
	float MaxSlope;
	//
	uint32 IntAlign;
	//
	uint32 IntOrient;
	//
	uint32 IntIsWeenieObj;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakePortalScene
{
	FACEWorldBakePortalScene();
	FACEWorldBakePortalScene(FACEWorldBakeByteReader& Reader);

	//
	uint32 ResourceId;
	//
	uint32 NumObects;
	//
	TArray<FACEWorldBakePortalSceneObject> Objects;
};
