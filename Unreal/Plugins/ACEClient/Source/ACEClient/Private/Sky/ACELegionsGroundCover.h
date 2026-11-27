#pragma once
#include "CoreMinimal.h"

class UACEDatSubsystem;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

// The imported scene uses a ten-metre world grid across AC1's 24 m cells.
class FACELegionsGroundCover
{
	friend class FACEYaraqArtworkTest;
public:
	void Update(UACEDatSubsystem& Dat, AActor& Owner, FVector Feet, float BodyRadius, float BodyHeight,
		FVector Viewer, float WorldScale, float DeltaTime);
	void Clear();
private:
	struct FPatch
	{
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;
		int32 TuftCount = 0;
	};
	bool BuildPatch(UACEDatSubsystem& Dat, AActor& Owner, FIntPoint Key, float Scale, FPatch& Out);
	TMap<FIntPoint, FPatch> Patches;
	TWeakObjectPtr<AActor> MeshOwner;
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	TOptional<FIntPoint> StreamCenter;
	TArray<FIntPoint> Pending, Unloaded;
	TOptional<FVector> PreviousFeet;
	FVector Trail = FVector::ZeroVector;
	double NextRetry = 0;
	float Elapsed = 0;
	uint32 ContentCrc = 0;
};
