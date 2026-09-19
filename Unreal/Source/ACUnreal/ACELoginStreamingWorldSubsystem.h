#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ACELoginStreamingWorldSubsystem.generated.h"

/**
 * Applies login-screen streaming holds as early as possible (world subsystem Initialize),
 * before BringUpLevel / RouteActorInitialize sync-loads World Partition cells or WC tiles.
 */
UCLASS()
class ACUNREAL_API UACELoginStreamingWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	void ApplyLoginStreamingHold();
	void MaintainLoginStreamingHold();
	void RestoreWorldStreamingIfAllowed();

	bool IsLoginStreamingHoldActive() const { return bLoginHoldActive; }

	/** Force-load WC landscape tiles overlapping the spawn neighborhood (ring = tile count per axis). */
	void RequestWcTerrainAround(const FVector& WorldLocation, int32 RingTiles = 1);

	/** Landscapes in the ring are loaded, visible, collision-enabled, and shadow-ready. */
	bool IsWcTerrainReadyAround(const FVector& WorldLocation, int32 RingTiles = 1) const;

	/** Visibility/collision/shadow flags for landscapes that streamed in after login restore. */
	void MaintainWcLandscapePresentation();

private:
	bool bLoginHoldActive = false;
	bool bWpStreamingRestored = true;
	bool bWcDistanceStreamingRestored = false;
	bool bWcFlagsHoldApplied = false;
};
