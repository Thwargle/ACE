#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "ACEParticleBatchComponent.generated.h"

/** One draw per authored surface, with per-particle transforms and vertex fades. */
UCLASS()
class ACECLIENT_API UACEParticleBatchComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()
public:
	void InitializeParticles(int32 Capacity);
	int32 AddParticle(const FTransform& WorldTransform, float Opacity);
	void RemoveParticle(int32 Index);
	void SetParticleVisual(int32 Index, const FTransform& WorldTransform, float Opacity);
	void ClearParticles();
	int32 GetParticleCount() const { return Transforms.Num(); }
	void FlushParticles();
private:
	struct FSection
	{
		TArray<FProcMeshVertex> Source;
		TArray<FVector> Positions, Normals;
		TArray<FVector2D> UVs;
		TArray<FColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		int32 IndicesPerParticle = 0;
	};
	TArray<FSection> Sections;
	TArray<FTransform> Transforms;
	TArray<float> Opacities;
	int32 MaxParticles = 0;
	int32 LastFlushedParticleCount = 0;
	bool bDirty = false;
};
