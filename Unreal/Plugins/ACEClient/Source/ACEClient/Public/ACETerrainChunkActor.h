#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ACETerrainChunkActor.generated.h"

class UProceduralMeshComponent;
class AACELandblockActor;
class UACEDatSubsystem;
struct FACEBuildingDoorwayClip;

/**
 * Multi-landblock terrain chunk (WorldBuilder-style). Owns one combined ProcMesh for
 * ChunkSize×ChunkSize landblocks and child AACELandblockActor instances for scenery.
 */
UCLASS()
class ACECLIENT_API AACETerrainChunkActor : public AActor
{
	GENERATED_BODY()
	friend class FACERetailInteriorStreamingTest;

public:
	AACETerrainChunkActor();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UProceduralMeshComponent> TerrainMesh;

	/** Landblock high-word of the chunk's SW corner (aligned to ChunkSize). */
	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 ChunkOriginLandblockId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE", meta = (ClampMin = "1", ClampMax = "16"))
	int32 ChunkSizeLandblocks = 4;

	UPROPERTY(BlueprintReadWrite, Category = "ACE")
	float WorldScale = 100.f;

	/**
	 * Builds combined terrain when every landblock mesh in the chunk is Ready.
	 * Spawns/refreshes child landblock actors for scenery (terrain skipped on children).
	 */
	UFUNCTION(BlueprintCallable, Category = "ACE|Terrain")
	bool TryLoadChunk(UACEDatSubsystem* Dat);

	/** Re-punch basement lids after indoor EnvCell floors become available. */
	bool ReapplyOutdoorTerrain(UACEDatSubsystem* Dat);

	UFUNCTION(BlueprintCallable, Category = "ACE|Terrain")
	void SetOutdoorTerrainCollisionEnabled(bool bEnabled);
	void SetOutdoorTerrainHiddenInGame(bool bHideTerrain);
	void ApplyDesiredOutdoorTerrainState();
	void SetLandPortalLookOut(bool bLookOut, UACEDatSubsystem* Dat);
	void SetLandEnvCellFloorPriority(bool bEnable, UACEDatSubsystem* Dat, bool bIndoorLookOut = false);
	/**
	 * bSuppress: hide flora/stabs on every child landblock.
	 * HideBuildingInfoIndices + ShellHideLandblockId: hide those Setup shells on the
	 * matching child (occupied shop). ShellHideLandblockId=0 disables shell hide.
	 */
	void SetIndoorScenerySuppressed(bool bSuppress, const TArray<int32>& HideBuildingInfoIndices = TArray<int32>(),
		uint32 ShellHideLandblockId = 0);
	void SetBuildingShellsBlockPawn(bool bBlockPawn);
	void SetBuildingShellsBlockPawn(bool bBlockPawn, const TArray<int32>& IgnoreBuildingIndices, uint32 IgnoreLandblock = 0);
	void ApplyDegradeCull(float EndCullCm);
	void ApplyOutdoorDoorwayClips(const TArray<FACEBuildingDoorwayClip>& Clips);

	const TMap<int32, TObjectPtr<AACELandblockActor>>& GetChildLandblocks() const { return ChildLandblocks; }

	UFUNCTION(BlueprintPure, Category = "ACE|Terrain")
	bool IsChunkTerrainReady() const;

	UFUNCTION(BlueprintPure, Category = "ACE|Terrain")
	bool IsChunkSceneryComplete() const;

	UFUNCTION(BlueprintPure, Category = "ACE|Terrain")
	bool ContainsLandblock(int32 LandblockId) const;

	AACELandblockActor* FindChildLandblock(int32 LandblockId) const;

protected:
	void ClearChildren();

	UPROPERTY()
	TMap<int32, TObjectPtr<AACELandblockActor>> ChildLandblocks;

	bool bTerrainApplied = false;
	bool bWantTerrainCollision = true;
	bool bWantTerrainHidden = false;
	float AppliedLandDepthBias = -1.f;
	int32 AppliedLandDepthBiasSections = -1;
};
