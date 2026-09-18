#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ACETypes.h"
#include "ACEWorldPresenterComponent.generated.h"

class UACEClientSubsystem;
class AACEWorldEntityActor;
class UACEScriptComponent;

/**
 * Spawns / updates AACEWorldEntityActor instances from ACE ObjectCreate / UpdatePosition / Delete.
 * Attach to the GameMode, PlayerController, or a persistent world actor.
 *
 * IMPORTANT: Enter-world floods dozens/hundreds of ObjectCreates in one PollSockets drain.
 * Building DAT setup meshes synchronously for each one freezes the game thread before terrain
 * can stream. Creates are queued and drained with a per-tick budget; DAT appearance is optional
 * and also budgeted.
 */
UCLASS(ClassGroup = (ACE), meta = (BlueprintSpawnableComponent))
class ACECLIENT_API UACEWorldPresenterComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class FACERetailInteriorStreamingTest;
	friend class FACERetailParticleTimingTest;
	friend class FACEMovementReviewTest;
	friend class FACEProjectileLifetimeTest;

public:
	UACEWorldPresenterComponent();
	void RefreshCellVisibility();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	float WorldScale = 100.f;

	/** Skip spawning a second actor for the local player (appearance goes on the possessed pawn via ACEPlayerController). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bSkipSelf = true;

	/** Only spawn objects that have a world position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bRequirePosition = true;

	/**
	 * When true, queue DAT setup-mesh appearance after the placeholder capsule is spawned.
	 * Keep false if enter-world still feels heavy; capsules alone are enough for debugging
	 * landblock streaming.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bApplyDatAppearance = true;

	/** Max new entities to spawn from the ObjectCreate queue per tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE", meta = (ClampMin = "1", ClampMax = "32"))
	int32 SpawnsPerTick = 8;

	/** Max DAT appearance builds per tick (expensive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE", meta = (ClampMin = "0", ClampMax = "24"))
	int32 DatAppearancesPerTick = 6;

	/** After portal exit, temporarily raise appearance budget to drain NPC mesh backlog. */
	void BeginPostPortalAppearanceBoost(int32 ExtraPerTick = 12, float Seconds = 4.f);

	/**
	 * Destroy weenies (NPCs, mobs, ground objects) whose landblock left the streaming
	 * ring, and re-queue session objects that are back inside it. TerrainPresenter
	 * calls this whenever the keep ring slides.
	 */
	void SyncEntitiesToKeepRing(const TSet<int32>& KeepLbs);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	TSubclassOf<AACEWorldEntityActor> EntityClass;

private:
    struct FProjectileFlight { double ExpiresAt=0; bool bExpired=false; };
    TMap<int32,FProjectileFlight> ProjectileFlights;
    void ExpireProjectileVisuals(double Now);
	int32 AppearanceBoostExtra = 0;
	double AppearanceBoostUntil = 0.0;
	UPROPERTY()
	TObjectPtr<UACEClientSubsystem> Client = nullptr;

	UPROPERTY()
	TMap<int32, TObjectPtr<AACEWorldEntityActor>> Spawned;

	/** Latest server motion for entities whose actor/appearance may still be queued. */
	TMap<int32, FACEObjectMotionState> LatestMotion;

	/** ObjectCreates waiting to be turned into actors (processed off the packet path). */
	TArray<FACEWorldObject> PendingSpawns;

	/** Guids that still need a DAT appearance pass. */
	TArray<int32> PendingDatAppearance;

	UFUNCTION()
	void HandleObjectCreated(const FACEWorldObject& Object);

	UFUNCTION()
	void HandleObjectDeleted(int32 ObjectGuid);

	UFUNCTION()
	void HandlePositionUpdate(int32 ObjectGuid, const FACEPosition& Position);

	UFUNCTION()
	void HandleMotionUpdate(int32 ObjectGuid, const FACEObjectMotionState& Motion);

	UFUNCTION()
	void HandleVectorUpdate(int32 ObjectGuid, FVector AceVelocity, FVector AceOmega);

	UFUNCTION()
	void HandlePhysicsStateUpdate(int32 ObjectGuid, int32 PhysicsState);
	UFUNCTION()
	void HandlePkStatusUpdated(int32 ObjectGuid, int32 Status);

	UFUNCTION()
	void HandlePlayScriptId(int32 ObjectGuid, int32 PhysicsScriptId, float Intensity);

	UFUNCTION()
	void HandlePlayEffect(int32 ObjectGuid, int32 ScriptType, float Intensity);

	UFUNCTION()
	void HandleSound(int32 ObjectGuid, int32 SoundType, float Volume);

	UFUNCTION()
	void HandleLogout(EACESessionState NewState);

	UFUNCTION()
	void HandleEnteredWorld(int32 PlayerGuid, const FACEPosition& SpawnPos);

	void ClearSpawned();
	bool ShouldStreamObject(const FACEWorldObject& Object) const;
	TSet<int32> LastEntityKeepLbs;
	void DestroySpawnedGuid(int32 Guid);
	void DrainPendingSpawns();
	void DrainPendingDatAppearance();
	void DrainPendingAttachments();
	void SpawnOrUpdateEntity(const FACEWorldObject& Object, bool bImmediateDatAppearance);
	bool TryAttachToParent(AACEWorldEntityActor* Child, int32 ParentGuid, int32 ParentLocation);
	bool ShouldSkipSelf(const FACEWorldObject& Object) const;
	void DestroySpawnedSelf(int32 PlayerGuid);

public:
	/** After local pawn appearance is ready, re-attach any equipped items still orphaned. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	void RehomePlayerChildren(int32 PlayerGuid);

	/** Flush PlayEffect / sound that arrived before the local pawn ScriptComponent existed. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	void FlushPendingEffectsForGuid(int32 ObjectGuid);

	/** Drop queued Launch / body-spell one-shots so portal reveal does not replay them. */
	void DropPendingOneShotEffectsForGuid(int32 ObjectGuid);

	/**
	 * True when nearby freestanding world objects (same landblock, or within radius) have
	 * finished their pending spawn/DAT appearance drain. Distant queued objects are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "ACE")
	/**
	 * True when nearby ObjectCreates have at least spawned placeholders (not full DAT meshes).
	 */
	bool IsAreaObjectsReady(const FACEPosition& AreaCenter, float RadiusAc = 24.f) const;

	/**
	 * True when nearby freestanding world objects have DAT appearance meshes (or gave up).
	 * Portal reveal waits on this so NPCs/items don't pop in after the tunnel.
	 */
	bool IsAreaAppearancesReady(const FACEPosition& AreaCenter, float RadiusAc = 48.f) const;

	/** Children whose parent actor wasn't ready yet. */
	TArray<int32> PendingAttachments;

	/** How many times DAT appearance failed for a guid — retry a few times then give up. */
	TMap<int32, int32> AppearanceRetryCounts;

	struct FPendingPlayEffect
	{
		uint64 ArrivalOrder = 0;
		int32 ObjectGuid = 0;
		int32 ScriptType = 0;
		float Intensity = 1.f;
	};
	struct FPendingPlayScriptId
	{
		uint64 ArrivalOrder = 0;
		int32 ObjectGuid = 0;
		int32 PhysicsScriptId = 0;
		float Intensity = 1.f;
	};
	struct FPendingSound
	{
		uint64 ArrivalOrder = 0;
		int32 ObjectGuid = 0;
		int32 SoundType = 0;
		float Volume = 1.f;
	};

	/** PlayEffect / PlayScriptId / Sound can arrive before ObjectCreate spawn finishes. */
	uint64 NextEffectArrivalOrder = 0;
	TArray<FPendingPlayEffect> PendingEffects;
	TArray<FPendingPlayScriptId> PendingScriptIds;
	TArray<FPendingSound> PendingSounds;

	void FlushPendingEffectsFor(int32 ObjectGuid);
	bool TryPlayEffectOnActor(int32 ObjectGuid, int32 ScriptType, float Intensity);
	bool TryPlayScriptIdOnActor(int32 ObjectGuid, int32 PhysicsScriptId, float Intensity);
	bool TryPlaySoundOnActor(int32 ObjectGuid, int32 SoundType, float Volume);
	UACEScriptComponent* FindLocalPawnScriptComponent() const;
	UACEScriptComponent* FindScriptComponentForGuid(int32 ObjectGuid) const;
};
