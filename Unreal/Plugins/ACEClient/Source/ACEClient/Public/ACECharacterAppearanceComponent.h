#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ACETypes.h"
#include "ACEOpcodes.h"
#include "ACECharacterAppearanceComponent.generated.h"

class UMaterialInstanceDynamic;

class UProceduralMeshComponent;
class UPrimitiveComponent;
struct FACEDatAnimationHook;

/**
 * Builds the character / creature mesh the server specified (Setup + ObjDesc clothing/face)
 * and plays MotionTable idle / walk / run. Attach to the possessed pawn for the local player,
 * or used by AACEWorldEntityActor for other objects.
 */
UCLASS(ClassGroup = (ACE), meta = (BlueprintSpawnableComponent))
class ACECLIENT_API UACECharacterAppearanceComponent : public UActorComponent
{
	GENERATED_BODY()

	friend class FACERetailRuntimeRegressionTest;
	friend class FACEMovementReviewTest;
	friend class FACEAvatarMotionTest;
	friend class FACERetailScreenTest;
    friend class FACERetailWorldEntryTest;
	friend class FACERetailPortalSpaceTest;

public:
	/** World bodies receive scene lights; UI and portal-space previews keep their own lighting. */
	bool bUseWorldLighting = false;
	bool HasAuthoredPhysicsGeometry() const;
	void UpdateCellLighting(uint32 CellId);
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> WorldLightingInstances;
	bool bWorldLightingInterior = false;
	UACECharacterAppearanceComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Reset DefaultAnimLoop clock (portal tunnel re-entry). */
	void ResetDefaultAnimClock();

	/** Retail examination viewport sequence: independent of world distance/placement. */
	void SetPreviewAnimation(uint32 AnimationId, bool bAnimate, int32 StartFrame = 0);
	/** UI captures animate relative to their own camera, outside the playable scene. */
	void SetPreviewCapture(bool bEnabled) { bPreviewCapture = bEnabled; }

	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	bool ApplyWorldObject(const FACEWorldObject& Object, float InWorldScale = 100.f, bool bEnablePartCollision = false);

	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void ClearAppearance();

	UFUNCTION(BlueprintPure, Category = "ACE|Appearance")
	bool HasAppearance() const { return bHasMesh; }

	/** Fingerprint of the last successfully applied ObjDesc (0 if none). */
	uint64 GetAppliedAppearanceHash() const { return AppliedAppearanceHash; }
	uint64 GetAppearanceRevision() const { return AppearanceRevision; }

	UFUNCTION(BlueprintPure, Category = "ACE|Appearance")
	USceneComponent* GetMeshRoot() const { return MeshRoot; }

	UFUNCTION(BlueprintPure, Category = "ACE|Appearance")
	int32 GetSetupId() const { return SetupId; }

	UFUNCTION(BlueprintPure, Category = "ACE|Appearance")
	bool GetPartBindTransform(int32 PartIndex, FTransform& OutTransform) const;
	int32 GetPartCount() const { return PartMeshes.Num(); }
	/** Tracked VR body; lower-body gait follows actual world travel, independent of stance. */
	bool bVRPoseControlled = false;

	/** Current animated/bind pose of a Setup part (relative to MeshRoot). */
	UFUNCTION(BlueprintPure, Category = "ACE|Appearance")
	bool GetPartCurrentTransform(int32 PartIndex, FTransform& OutTransform) const;
	void StabilizeVRPelvis();
	void UpdateVRLowerBody(float Dt);
	/** Cosmetic chest bend, shared by the owner and replicated tracked avatars. Returns the shoulder frame. */
	FTransform UpdateVRUpperBody(const FTransform& Head, const FTransform& LeftGrip, const FTransform& RightGrip,
		bool bLeftTracked, bool bRightTracked, float Dt);
	void ResetVRLowerBody() { bVRLowerBodyReady = false; VRGaitTime = VRGaitBlend = 0.f; VRTorsoAngles = FVector::ZeroVector; }
private:
	FVector VRTorsoAngles = FVector::ZeroVector;
	bool bVRLowerBodyReady = false;
	FVector VRPreviousBodyLocation = FVector::ZeroVector;
	float VRGaitTime = 0.f, VRGaitBlend = 0.f;
public:

	/** Part mesh component used as an attach parent for held items (hand / shield / etc.). */
	UFUNCTION(BlueprintPure, Category = "ACE|Appearance")
	USceneComponent* GetPartMesh(int32 PartIndex) const;

	/** Replace one Setup part's GfxObj geometry (AnimationHook ReplaceObject). */
	bool ReplacePartGfxObj(int32 PartIndex, uint32 GfxObjId);

	/** Show/hide Setup geometry without hiding separately managed script particle components. */
	void SetAppearanceVisible(bool bVisible);

	/** Toggle CSM casting on Setup parts. Inset is for creatures/players only — scenery weenies
	 *  go through whole-scene cache (per-object inset on every DAT mesh was the <10 FPS path). */
	void SetPartsCastShadow(bool bCast, bool bInset = false);
	bool GetPartsCastShadow() const;

	/** Create MeshRoot if missing — needed so equipped items can attach before DAT appearance loads. */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void EnsureMeshRoot();

	/** Height of the rendered Setup parts in world centimeters (feet to top of head). */
	float GetVisualHeightCm() const;

	/** World-space AABB of visible parts — for door/chest pick capsules. */
	bool GetVisualWorldBounds(FBox& OutBox) const;

	/** Reapply feet-at-capsule-bottom alignment after the capsule is resized. */
	void RefreshMeshAlignment() { EnsureMeshRoot(); }

	/** Local / remote locomotion. PlayRate scales walk/run cycle speed (run uses GetRunRate). */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void SetLocomotionInput(float Forward, float Strafe, bool bRunning, float PlayRate = 1.f);

	/**
	 * Persist MotionStance from server UpdateMotion CurrentStyle (NonCombat / HandCombat / Magic…).
	 * Used for idle and locomotion cycles so combat mode does not snap back to peace idle.
	 */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void SetPreferredStyle(int32 Style);

	/** Server On/Off door transition via MotionTable Links (one-shot, holds final pose). */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void PlayDoorMotion(int32 ToCommandU16);

	/** Clear Jumpup/Falling / other held one-shots and resume locomotion. */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void ClearActionMotion();
	/** Respawn keeps the pawn; release only its death pose, leaving other actions alone. */
	void ClearDeathMotion();

	/** Seed held door pose without replaying (ObjectCreate already On/Off). */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void SetDoorHeldCommand(int32 CommandU16);

	/** True while held/animating toward Motion::On (door/chest open). */
	bool IsDoorHeldOpen() const { return DoorToCommand == ACEMotion::On; }
	/** True after On/Off reached its last frame (not mid-transition). */
	bool IsDoorHoldFinal() const { return bDoorHoldFinal; }

	/**
	 * One-shot combat/emote action from UpdateMotion Commands (Ready→Attack under CurrentStyle).
	 * Style is a full MotionStance (HandCombat etc.); 0 uses MotionTable fallbacks.
	 * Dead holds its final frame instead of returning to locomotion.
	 */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void PlayActionMotion(int32 ActionCommand, float PlayRate = 1.f, int32 Style = 0, bool bHoldFinalPose = false);

	/** Queue a one-shot to run after the current ActionOneShot finishes (cast after PowerUp). */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void QueueActionMotion(int32 ActionCommand, float PlayRate = 1.f, int32 Style = 0, bool bHoldFinalPose = false);

	/**
	 * After the current ActionOneShot finishes, hold this command instead of returning to
	 * Locomotion Ready — Jumpup → Falling without an idle blend.
	 */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void QueueHeldActionAfterCurrent(int32 ActionCommand, int32 Style = 0);

	/** Suppress idle↔walk pose blends while jump owns the pose. */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void SetSuppressLocoIdleBlend(bool bSuppress);

	/** Clear Jumpup / Falling / JumpCharging holds so loco Ready/run can resume. */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void ClearJumpMotionIfAny();

	/** Cancel a held (or hold-pending) chat-pose emote — retail clears it on move/jump. */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void CancelHeldActionMotion();

	/** Seed held action pose without replaying (ObjectCreate corpse already Dead). */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void SetHeldActionMotion(int32 ActionCommand, int32 Style = 0);

	/**
	 * Part-mesh physics for standing on / blocking. Visibility pick uses the actor capsule
	 * (ConfigureWorldCollision) — parts always ignore ECC_Visibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "ACE|Appearance")
	void ConfigurePartCollision(bool bBlocking, bool bQueryVisibilityOnly = false, bool bSelectOnMesh = false);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	float WorldScale = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bPlayIdleMotion = true;

	/**
	 * AC GfxObj/Setup art faces local +Y (same as AC facing). Actor rotation is the raw AC
	 * quat; leave at 0 so mesh +Y matches GetAceForwardVector().
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	float MeshFacingYawDegrees = 0.f;

	/** Hide other mesh/skeletal components on the owner (e.g. default pawn capsule mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bHideOwnerMeshes = true;

	/**
	 * When true, shift MeshRoot down by the owner's capsule half-height so AC feet (mesh origin)
	 * sit on the capsule bottom. Required for the local pawn (capsule center = actor location);
	 * leave false for world entities whose actor location is already feet height.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bAlignMeshToCapsuleBottom = false;

protected:
	void EnsurePartMeshes(int32 Count);
	void HideOwnerPrimitiveMeshes();
	void ApplyPartTransform(int32 PartIndex, const FTransform& AnimOrBind);
	/** Retail Position.Frame.Rotate(Omega) for HasDefaultAnim static props. */
	void TickObjectAnimFrame(float DeltaTime);
	void ApplyDefaultAnimPartTransforms(const TArray<FTransform>& Animated, int32 AnimatedCount);
	/** Refresh complex collision after door part transforms change. */
	void RecookPartPhysics();
	void DispatchCrossedHooks(const TArray<FACEDatAnimationHook>& Hooks);
	const float* GetPreviousHookTime(uint64 TrackKey, float CurrentEvalTime);
	void CommitHookTime(uint64 TrackKey, float CurrentEvalTime);
	void ResetHookTracking();

	/** Parent for all Setup parts. AC GfxObj art faces +Y; MeshRoot yaws -90° so visual
	 *  forward matches actor +X (movement + camera). Do NOT also yaw individual parts. */
	UPROPERTY()
	TObjectPtr<USceneComponent> MeshRoot;

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> PartMeshes;

	TArray<FTransform> BindTransforms;
	/** Object physics frame (omega tumble) combined with default-anim part quats. */
	FTransform ObjectAnimFrame = FTransform::Identity;
	int32 SetupId = 0;
	int32 MotionTableId = 0;
	int32 DefaultAnimationId = 0;
	FACEObjDesc Appearance;
	float AnimTime = 0.f;
	float DeferredPoseDeltaTime = 0.f;
	bool bPreviewAnimation = false;
	bool bPreviewCapture = false;
	int32 PreviewStartFrame = 0;
	bool bHasMesh = false;
	float LocomotionForward = 0.f;
	float LocomotionStrafe = 0.f;
	bool bLocomotionRunning = false;
	/** Motion playback rate (1 = authored cycle speed; run often > 1). */
	float LocomotionPlayRate = 1.f;
	/** Cross-fade weight between walk (0) and run (1) while moving forward. */
	float RunBlend = 0.f;
	/** Last frame moving state — triggers idle↔walk pose blend. */
	bool bWasLocomotionMoving = false;
	/** +1 forward / −1 back. Reverse after a run-jump must restart the cycle or it freezes. */
	int8 LastLocomotionFwdSign = 0;

	/** Cross-fade when PreferredStyle (combat stance) or loco↔action changes. */
	TArray<FTransform> StanceBlendFrom;
	float StanceBlendAlpha = 1.f;
	static constexpr float StanceBlendDuration = 0.28f;
	float PoseBlendDuration = StanceBlendDuration;

	/** Capture current part poses into StanceBlendFrom and start a cross-fade. */
	void BeginPoseBlendFromCurrent(float Duration = StanceBlendDuration);
	/** Apply evaluated part transforms, optionally cross-fading from StanceBlendFrom. */
	void ApplyAnimatedPartsWithBlend(const TArray<FTransform>& Animated, int32 AnimatedCount, float DeltaTime);

	/** Last applied mesh fingerprint — skip ObjDesc/wield rebuild spam. */
	int32 AppliedPlacementId = 0;
	uint64 AppliedAppearanceHash = 0;
	float AppliedTranslucencyKey = -1.f;
	float AppliedWorldScale = 0.f;
	uint64 AppearanceRevision = 0;
	bool AppliedPartCollision = false;

	enum class EACEAnimMode : uint8
	{
		Locomotion,
		DoorTransition,
		DefaultAnimLoop,
		ActionOneShot
	};
	EACEAnimMode AnimMode = EACEAnimMode::Locomotion;
	uint32 DoorFromCommand = ACEMotion::Off;
	uint32 DoorToCommand = ACEMotion::Off;
	bool bDoorHoldFinal = false;
	/** Weenie 16919 doorpedestal — Off clip last frame is empty/NoDraw. */
	void ApplyBindPartTransforms();
	bool bSuppressNextHookDispatch = false;
	uint64 HookTrackKey = 0;
	float PreviousHookTime = 0.f;
	bool bHookTrackValid = false;

	/** Active combat/emote one-shot (full MotionCommand). */
	uint32 ActionCommand = 0;
	uint32 ActionStyle = 0;
	float ActionPlayRate = 1.f;
	/** Dead (and similar) stay on the final action frame instead of returning to idle. */
	bool bHoldActionFinal = false;
	/** Chat/emote: after one-shot finishes, latch final frame until loco input. */
	bool bHoldActionFinalAfterFinish = false;
	/** True once the current ActionOneShot evaluated successfully at least once. */
	bool bActionEverEvaluated = false;
	/** After ActionOneShot completes, hold this command (0 = return to Locomotion). */
	uint32 QueuedHoldAction = 0;
	uint32 QueuedHoldStyle = 0;
	/** Bindstone / lifestone: resume Setup DefaultAnimation after Twitch1. */
	bool bResumeDefaultAnimAfterAction = false;
	/** Held weapons: loop DefaultAnimation hooks but keep Placement part frames. */
	bool bHeldKeepPlacementPose = false;
	/** Cast / next PowerUp waiting for current MagicPowerUp to finish (do not interrupt). */
	TArray<uint32> PendingActionCommands;
	TArray<uint32> PendingActionStyles;
	TArray<float> PendingActionPlayRates;
	TArray<bool> PendingActionHolds;
	/** While true, idle↔walk pose blends are skipped (jump arc). */
	bool bSuppressLocoIdleBlend = false;

	/** Last server MotionStance for idle / loco Cycles (persists across one-shots). */
	uint32 PreferredStyle = 0;

	/** Unused: camera-facing thin world meshes was reverted (doors / pedestals). */
	bool bBillboardSpriteParts = false;
	void BillboardSpriteParts();
};
