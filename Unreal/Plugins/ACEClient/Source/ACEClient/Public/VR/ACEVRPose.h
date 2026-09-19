#pragma once
#include "CoreMinimal.h"

struct FACEVRPose
{
	uint32 Sequence = 0, Cell = 0, Teleport = 0, Flags = 0;
	uint32 Version = 1;
	int32 Weapon = 0, Ammo = 0;
	FVector Root = FVector::ZeroVector; // authoritative server feet, world meters
	float EyeHeight = 1.65f, Draw = 0;
	FTransform Poses[5] = {FTransform::Identity,FTransform::Identity,FTransform::Identity,FTransform::Identity,FTransform::Identity};
	double ReceivedAt = 0;
};

struct FACERemoteVRPose
{
	FACEVRPose Previous, Current;
};
