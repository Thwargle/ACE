#pragma once
#include "CoreMinimal.h"

struct FACEVRPose
{
	uint32 Sequence = 0, Cell = 0, Teleport = 0, Flags = 0;
	float EyeHeight = 1.65f, Draw = 0;
	FTransform Poses[3]; // meters in Unreal world axes, relative to feet
	double ReceivedAt = 0;
};

struct FACERemoteVRPose
{
	FACEVRPose Previous, Current;
};
