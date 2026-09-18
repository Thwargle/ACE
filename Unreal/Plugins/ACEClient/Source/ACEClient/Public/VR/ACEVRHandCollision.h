#pragma once
#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class UWorld;

struct FACEVRContactShape
{
	FTransform Local = FTransform::Identity;
	FVector Extent = FVector(6.f);
};

struct FACEVRContactState
{
	FTransform Pose = FTransform::Identity;
	bool bValid = false;
	bool bContact = false;
	void Reset() { bValid = bContact = false; }
};

struct FACEVRContactTriangle
{
	FVector A, B, C;
	FBox Bounds;
	int32 ObjectGuid = 0;
};

/** Kinematic visual contact only. Never applies forces to the pawn or tracking origin. */
namespace ACEVRHandCollision
{
	ACECLIENT_API bool Overlaps(UWorld* World, const FTransform& Pose,
		const TArray<FACEVRContactShape>& Shapes, const FCollisionQueryParams& Query, const TArray<FACEVRContactTriangle>* Mesh = nullptr);
	ACECLIENT_API void Move(UWorld* World, FACEVRContactState& State, const FTransform& Desired,
		const FVector& SafeOrigin, const TArray<FACEVRContactShape>& Shapes, const FCollisionQueryParams& Query, const TArray<FACEVRContactTriangle>* Mesh = nullptr);
}
