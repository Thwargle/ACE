#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ACETypes.h"
#include "ACEWorldEntityActor.generated.h"

class UStaticMeshComponent;
class UCapsuleComponent;
class UTextRenderComponent;
class UACECharacterAppearanceComponent;
class UACEScriptComponent;

/** Visual for an ACE world object — Setup + ObjDesc appearance when DAT files are present. */
UCLASS()
class ACECLIENT_API AACEWorldEntityActor : public AActor
{
	GENERATED_BODY()
	friend class FACEMovementReviewTest;

public:
	AACEWorldEntityActor();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void InitializeFromObject(const FACEWorldObject& Object, float InWorldScale = 100.f, bool bApplyDatAppearance = true);

	/** Build DAT setup mesh after a lightweight InitializeFromObject(..., bApplyDatAppearance=false). */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	bool ApplyDatAppearanceFromObject(const FACEWorldObject& Object);

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void ApplyACEPosition(const FACEPosition& Position);

	/** World-space point for spatial SFX (capsule center, not stale root). */
	FVector GetSoundEmitLocation() const;

	/** Apply server UpdateMotion locomotion to this entity's DAT animation. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	void ApplyMotionState(const FACEObjectMotionState& Motion);

	/** Apply ACE-space linear/angular velocity (ObjectCreate / VectorUpdate) for projectiles. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	void ApplyPhysicsVelocity(const FVector& AceVelocity, const FVector& AceOmega = FVector::ZeroVector);

	float GetMeleeBodyRadius() const { return MovementRadius * GetActorScale3D().Z; }
	float GetMeleeBodyHeight() const { return MeleeBodyHeight * GetActorScale3D().Z; }
	bool FindMeleeContact(const FVector& A, const FVector& B, float& Along) const;
	bool FindProjectileContact(const FVector& A, const FVector& B, float RadiusCm, float& Along) const;
	FBox GetProjectileContactBounds(float RadiusCm) const;
	void SetSelectionHighlight(float Strength);
	float MeleeBodyHeight = 200.f;
	FVector GetAcePhysicsOmega() const { return AcePhysicsOmega; }

	/** Apply PhysicsState from ObjectCreate / SetState (Ethereal open doors, etc.). */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	void ApplyPhysicsState(int32 InPhysicsState);
	bool IsMeshSuppressed() const;

	UFUNCTION(BlueprintPure, Category = "ACE")
	bool IsDoor() const { return (ObjectDescriptionFlags & ACEObjectDescFlag::Door) != 0; }
	bool IsOpenable() const { return (ObjectDescriptionFlags & ACEObjectDescFlag::Openable) != 0; }
	/** Doors / chests — locked chests lose Openable; Container+MT still uses On/Off. */
	bool UsesOnOffMotion() const
	{
		if (IsCorpse()) return false;
		if (IsDoor() || IsOpenable())
		{
			return true;
		}
		if (WeenieClassId == 16919)
		{
			return true;
		}
		if ((ItemType & ACEItemType::Container) != 0 && MotionTableId != 0)
		{
			return true;
		}
		return InitialMotionCommand == ACEMotion::OnCommandU16
			|| InitialMotionCommand == ACEMotion::OffCommandU16
			|| InitialMotionCommand == static_cast<int32>(ACEMotion::On)
			|| InitialMotionCommand == static_cast<int32>(ACEMotion::Off);
	}
	bool IsCorpse() const { return (ObjectDescriptionFlags & ACEObjectDescFlag::Corpse) != 0; }
	/** PK or PK Lite — pawn-pawn collision; NPK / Free walk through other players. */
	bool IsPkOrPkLite() const { return ACEPlayerKillerStatus::FlagsBlockPlayers(ObjectDescriptionFlags); }

	UFUNCTION(BlueprintPure, Category = "ACE")
	bool IsEthereal() const { return (PhysicsState & ACEPhysicsState::Ethereal) != 0; }

	/** Attach to a wielder (equipped weapon/shield). Disables world placement. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	void AttachToParentActor(AActor* ParentActor, int32 ParentLocation);

	/** Clear attach state so Presenter can re-home this item onto the real pawn. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	void ClearAttachedState();

	/** Force every primitive on this actor to NoCollision (weapons must never block the pawn). */
	void DisableAllCollision();

	/** Query-only Visibility on wielded weapon meshes so they can be selected / inspected. */
	void ConfigureAttachedPickCollision();

	/** Configure blocking collision for freestanding world objects (chests, lifestones, NPCs). */
	void ConfigureWorldCollision(bool bEnable);

	UFUNCTION(BlueprintPure, Category = "ACE")
	bool IsAttachedToParent() const { return GetAttachParentActor() != nullptr; }

	UFUNCTION(BlueprintPure, Category = "ACE")
	int32 GetACEGuid() const { return ACEGuid; }

	UFUNCTION(BlueprintPure, Category = "ACE")
	int32 GetParentGuid() const { return ParentGuid; }
	int32 GetLastAceCellId() const { return LastAceCellId; }
	void SetCellVisible(bool bVisible);
	bool IsCellVisible() const { return bCellVisible; }
	bool bCellVisible = true;
	bool bReceivedDeathMotion = false;
	int32 GetWielderId() const { return WielderId; }

	/** Standing creature/player (not a parented wielded mesh). */
	bool IsStandingCreatureOrPlayer() const
	{
		if (ParentGuid != 0 || bAttachedToParent || WielderId != 0 || CurrentWieldedLocation != 0)
		{
			return false;
		}
		return bIsPlayer || (ItemType & ACEItemType::Creature) != 0;
	}

	bool IsWieldedWorldItem() const
	{
		return ParentGuid != 0 || bAttachedToParent || WielderId != 0 || CurrentWieldedLocation != 0;
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Invisible simple capsule — wall sweeps use this (not DAT procedural meshes). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UCapsuleComponent> CollisionProxy;
	/** Authored blocking volumes, independent of the larger mouse selection volume. */
	UPROPERTY() TArray<TObjectPtr<UPrimitiveComponent>> AuthoredCollision;
	uint32 CollisionSetupId = 0;
	float CollisionWorldScale = 0.f;
	bool bSetupHasPhysicsBSP = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UTextRenderComponent> NameLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACECharacterAppearanceComponent> Appearance;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACEScriptComponent> ScriptComponent;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 ACEGuid = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	FString ACEName;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 SetupId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 WeenieClassId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 MotionTableId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 ItemType = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 InitialMotionCommand = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 ObjectDescriptionFlags = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 PhysicsState = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	float UseRadius = 0.6f;

	UPROPERTY(BlueprintReadWrite, Category = "ACE")
	float WorldScale = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	bool bIsSelf = false;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	bool bIsPlayer = false;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	bool bUsingDatMesh = false;

	/** An effect-only setup has no fallback body, even after a physics update. */
	bool bParticleOnlyAppearance = false;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 ParentGuid = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 ParentLocation = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int32 WielderId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	int64 CurrentWieldedLocation = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	bool bAttachedToParent = false;

	/** Exponential correction rate toward the extrapolated remote pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Movement", meta = (ClampMin = "1.0", ClampMax = "40.0"))
	float RemotePositionSmoothing = 10.f;

	/** Corrections larger than this many AC units snap (portals/teleports) instead of blending. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Movement", meta = (ClampMin = "5.0", ClampMax = "192.0"))
	float RemoteSnapDistance = 40.f;

	/** Max seconds of free integration away from the last F748 before clamping drift. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Movement", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RemoteMaxExtrapolate = 2.f;

	/** Degrees/sec at TurnSpeed=1 — matches local ACEPlayerController prediction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Movement", meta = (ClampMin = "45.0", ClampMax = "360.0"))
	float RemoteTurnRateDegrees = 180.f;
	float RemoteWalkSpeedAc = 3.1199999f;
	float RemoteRunSpeedAc = 4.f;
	uint32 ApproachTargetSetup = 0;
	float ApproachTargetRadiusAc = 0.f, ApproachTargetHeightAc = 0.f;

	/**
	 * Keep the extrapolated remote pose on the world surface between the ~1 Hz F748 packets.
	 * Retail only broadcasts sparse positions, so a running creature integrated in a straight
	 * line dives through hills / floats over dips until the next correction. A short vertical
	 * ground trace pins feet to the terrain so they stop clipping the environment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Movement")
	bool bClampToGround = true;

	/** Retry grounding until terrain/env-cell collision is streamed (portals / static props). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Movement")
	bool bRetryGroundClamp = true;

	/** Only re-seat Z when the surface is within this band (AC units) of the extrapolated feet —
	 *  larger gaps (jumps, flying casters, dungeon floors) are left to the authoritative F748. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Movement", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float GroundClampBandAc = 2.5f;

protected:
	/** Local player position is live; its world-object network echo can be stale. */
	bool ResolveFacingTarget(int32 TargetGuid, FVector& Location) const;
	/** Collision-constrained extrapolation; authoritative F748 teleports stay authoritative. */
	FVector ResolvePredictedMovement(const FVector& From, const FVector& Destination) const;
	float MovementRadius = 25.f;
	float MovementHalfHeight = 90.f;

	/** Vertical trace (world static, then outdoor heightfield) for the ground Z under a point. */
	bool TraceGroundZ(const FVector& AtLocation, float& OutGroundZ) const;

	/** True when LastAceCellId is indoor, or the point sits inside a building EnvCell. */
	bool ResolveIndoorOccupancy(const FVector& AtLocation, uint32& OutEnvCellId) const;

	/** Pin InOutLocation.Z onto the traced surface within GroundClampBandAc (prevents clipping).
	 *  Returns true when a ground sample was found (even if Z was left unchanged outside the band). */
	bool ClampLocationToGround(FVector& InOutLocation, const FQuat* Rotation = nullptr) const;
	void SupportDroppedItem(FVector& Location, const FQuat& Rotation) const;
	mutable TArray<FVector> GroundSupportPoints;
	mutable uint64 GroundSupportRevision = MAX_uint64;

	/** Static props don't tick. Creatures, missiles, and held items do. */
	void RefreshActorTickEnabled();

	/** Refresh relative pose from the parent's current animated hold part. */
	void UpdateHeldAttachmentPose();

	/** Setup HoldingLocations part index on the parent (hand / shield / etc.). */
	int32 HeldParentPartIndex = INDEX_NONE;

	/** Holding frame relative to that parent part (Unreal space). */
	FTransform HeldLocalFrame = FTransform::Identity;

	bool bHaveHeldLocalFrame = false;
	/** Weenie DefaultScale — restored after attach so we do not inherit the hand part's scale. */
	float HeldWeenieScale = 1.f;
	bool bHaveRemotePredict = false;
	int32 ProjectileAmmoType = 0;
	/** Last F748 / ObjectCreate cell — indoor cells must not fall back to outdoor heightfield. */
	int32 LastAceCellId = 0;
	/** True until a successful outdoor/indoor ground sample seats the actor. */
	bool bPendingGroundClamp = false;
	float GroundClampRetrySeconds = 0.f;
	/** Continuously integrated pose (UpdateMotion turn/move + F748 corrections). */
	FVector RemotePredictLocation = FVector::ZeroVector;
	FQuat RemotePredictRotation = FQuat::Identity;
	/** Last authoritative F748 location — used to clamp runaway drift. */
	FVector RemoteAnchorLocation = FVector::ZeroVector;
	FACEObjectMotionState RemoteMotion;

	/** Frozen Unreal XY face direction for TurnToObject / TurnToHeading (set once per packet). */
	FVector RemoteTurnFaceDir = FVector::ZeroVector;
	bool bHaveRemoteTurnFace = false;

	/** ACE-space velocity / omega for missiles & spell projectiles (units/sec). */
	FVector AcePhysicsVelocity = FVector::ZeroVector;
	FVector AcePhysicsOmega = FVector::ZeroVector;
	bool bHavePhysicsVelocity = false;
	bool bPartsCastShadow = true;
	bool bPartsCastInset = true;
	bool bPlayedDeathSound = false;
};
