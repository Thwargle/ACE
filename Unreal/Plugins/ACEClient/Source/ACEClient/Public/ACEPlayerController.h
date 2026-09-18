#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ACETypes.h"
#include "ACEPlayerController.generated.h"

class UACEClientSubsystem;
class UACEMovementComponent;
class UUserWidget;
class UACELoginWidget;
class UACEHoverTooltipWidget;
class UACEGameHUDWidget;
class UACEUICanvasWidget;
class UACEMouseCursorWidget;
class UTexture2D;
class UACECharacterAppearanceComponent;
class AACELoadingScreenActor;
class AACEWorldEntityActor;
class USpringArmComponent;
class UCapsuleComponent;
class UCameraComponent;

/**
 * Sample controller: shows login UI, enters world, WASD → ACE movement, applies local character appearance to the pawn.
 */
UCLASS()
class ACECLIENT_API AACEPlayerController : public APlayerController
{
	GENERATED_BODY()
	friend class UACEVRComponent;
	friend class FACEVRRigTest;
	friend class FACEVRInteriorTest;
	friend class FACEVRWallContactTest;
	friend class FACEVRStairCeilingTest;
	friend class FACEVRInteriorNetworkTest;
	friend class FACERetailWorldEntryTest;
	friend class FACELedgeStairsTest;
	friend class FACEMissingDatLoginTest;
	friend class FACECameraEdgeTest;
	friend class FACERetailScreenTest;
    friend class UACEUICharSelectBinder;
    friend class UACEUICharGenBinder;
    friend class FACERetailCharacterCreationScreenTest;

public:
	AACEPlayerController();
	bool IsVRActive() const;
	class UACEVRComponent* GetVRComponent() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** Diagnostic: called immediately and one tick later (via zero-delay timer) so the widget shows even if
	 *  the LocalPlayer/viewport wasn't ready yet in BeginPlay (common with "Play In New Window"). */
	void DeferredShowLoginUI();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|UI")
	TSubclassOf<UACELoginWidget> LoginWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	float WorldScale = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bSyncPossessedPawn = true;

	/** Apply ObjectCreate / ObjDesc for the local player onto the possessed pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bApplyLocalAppearance = true;

	/** Show retail-style portal-space tunnel until DAT + terrain + self mesh are ready. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|UI")
	bool bUseEnterWorldLoadScreen = true;

	/** Also run the portal-space transition whenever TeleportSeq changes in-world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|UI")
	bool bUsePortalTransitionOnTeleport = true;

	bool IsWorldTransitionActive() const { return bEnterWorldLoading || bWorldRevealActive; }

	/** Enables the loud per-tick / on-screen diagnostic messages used while bringing up the
	 *  login UI. Off by default — the UMG login widget is the primary UI now, so this debug
	 *  spam just clutters the screen. Flip on if you need to re-diagnose viewport/widget
	 *  attach issues. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Debug")
	bool bShowACEDebug = false;

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void ShowLoginUI();

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void HideLoginUI();

	/** True only once the login widget has actually been composited into the viewport
	 *  (i.e. AddToPlayerScreen succeeded) — NOT just that the UObject was created.
	 *  GameMode polls this to know whether it needs to keep retrying ShowLoginUI(). */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	bool IsLoginUIVisible() const;

	/** True while there's no active/attempted connection yet (i.e. the user hasn't gotten past
	 *  the login screen in any way). Used by AACELoginHUD as the primary signal for whether it
	 *  should keep drawing its guaranteed-visible backup chrome — deliberately independent of
	 *  IsLoginUIVisible()'s CachedGeometry heuristic, which can be true even when the UMG login
	 *  widget's actual content is invisible (e.g. before the RebuildWidget/NativeConstruct
	 *  ordering fix — the widget slot had real geometry while its content was still an empty
	 *  SSpacer, painting nothing). */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	bool IsSessionDisconnected() const;

	/** Stop Holding Use retries (e.g. after ApproachVendor opens the vendor UI). */
	void EndUseApproach();
	/** GameEvent UseDone — release local approach so idle StopMovement cannot cancel the chain. */
	void HandleUseDone(uint32 Error);
	void HandleMoveToFailed(uint32 Error);

	/** Retail-style object name blurb under the cursor (UMG, high Z-order). */
	UFUNCTION(BlueprintPure, Category = "ACE|UI")
	bool GetHoverTooltip(FString& OutText, FVector2D& OutCursorPixels) const;

	/** Retail NumPad +/- / mouse wheel — zoom third-person camera in or out. */
	void ApplyCameraWheelZoom(float WheelDelta);

	/** Predicted CellId when local prediction is active; otherwise session CellId. */
	UFUNCTION(BlueprintPure, Category = "ACE")
	int32 GetEffectiveCellId() const;
	/** Local collision result for presentation; false during pending server placement. */
	bool TryGetLocallyPredictedPosition(FACEPosition& OutPosition) const;

	/**
	 * Client-side walk-into-UseRadius after hand/double-click Use (server also MoveToChains).
	 * bFireActionWhenInRange=false for Give / pure server MoveTo — approach & AutoPos only,
	 * do not SendUseItem (that interrupts NPC hand-offs).
	 */
	void BeginUseApproach(int32 TargetGuid, float DistanceAc, bool bPickupIntoInventory = false,
		bool bFireActionWhenInRange = true);
	/** Retail F / hand icon: Use selected, or PutItemInContainer for ground loot. */
	void InteractWithSelectedObject();
	void InteractWithObject(int32 ObjectGuid);

	/** Yaw the local pawn to face a world object (targeted spells / combat). */
	void FaceWorldTarget(int32 TargetGuid);

	/** Exact server Position.CylinderDistance using Setup radius/height (returns edge distance in cm). */
	float GetUseCylinderDistanceCm(const FACEWorldObject& Target, const FVector& SelfOriginCm,
		const FVector& TargetOriginCm) const;

	/** World entity under the cursor (Visibility channel). Used for inventory drag→Give. */
	AACEWorldEntityActor* PickWorldEntityUnderCursor() const;

	/** Same as PickWorldEntityUnderCursor but using explicit viewport pixel coords (drag capture). */
	AACEWorldEntityActor* PickWorldEntityAtScreenPosition(float ScreenX, float ScreenY) const;

	/** True while a ground-loot PutItemInContainer approach is in flight. */
	bool IsPickupApproachActive() const
	{
		return bServerMoveToActive && bApproachPickupIntoInventory;
	}

	/** Dual-use targeting (mana stone / key) — keep interactable cursor until cancelled. */
	void SetPendingUseTargeting(bool bPending);

protected:
	UPROPERTY()
	TObjectPtr<UACELoginWidget> LoginWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UACEHoverTooltipWidget> HoverTooltipWidget = nullptr;
	UPROPERTY()
	TObjectPtr<UACEMouseCursorWidget> MouseCursorWidget = nullptr;
	bool bRetailCursorInstalled = false;
	bool bRetailCursorInteractable = false;
	bool bPendingUseTargeting = false;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> RetailCursorDefaultTex = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> RetailCursorInteractableTex = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> RetailCursorTargetTex = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> RetailCursorTargetValidTex = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> RetailCursorTargetInvalidTex = nullptr;

	/** In-world gameplay HUD (vitals, radar, chat, toolbar). Shown once entering the world. */
	UPROPERTY()
	TObjectPtr<UACEGameHUDWidget> GameHUDWidget = nullptr;

	/** DAT-driven retail layout canvas (LayoutDesc resolved JSON + textures). */
	UPROPERTY()
	TObjectPtr<UACEUICanvasWidget> DatCanvasWidget = nullptr;

	/** Binds DAT ElementNames to networking + overlays for classic_gameplay. */
	UPROPERTY()
	TObjectPtr<class UACEUIGameplayBinder> DatGameplayBinder = nullptr;

	/** Binds DAT charactermanagement (0x21000004) for character select. */
	UPROPERTY()
	TObjectPtr<class UACEUICharSelectBinder> DatCharSelectBinder = nullptr;
	UPROPERTY() TObjectPtr<class UACEUICharGenBinder> DatCharGenBinder = nullptr;
	void ShowCharacterCreationUI();

	/** Prefer UACEUICanvasWidget + UIFlow; fall back to hardcoded UACEGameHUDWidget when layout load fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|UI")
	bool bUseDatDrivenHud = true;

	/** Class used for the gameplay HUD; falls back to the native UACEGameHUDWidget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|UI")
	TSubclassOf<UACEGameHUDWidget> GameHUDWidgetClass;

	void ShowGameHUD();
	/** Soft-hide (Collapsed) — preserves widget state across portal transitions. */
	void HideGameHUD();
	/** Remove and destroy the HUD widget (logout / leave world). */
	void DestroyGameHUD();
	/** Retail UIFlow Intro (0x21000001 splash) under the credentials overlay. */
	void EnsureDatIntroCanvas();
	void ShowCharacterSelectUI(const TArray<FACECharacterInfo>& Characters, const FString& ServerName);
	void DestroyCharacterSelectUI();
	/** Hold login until char-select DAT chrome textures resolve. */
	void TickPendingCharacterSelect();
	bool TryPrefetchCharacterSelectAssets();
	/** Warm ClassicGameplay textures while still in portal space. */
	bool TryPrefetchGameplayHudAssets();
	static void CollectLayoutTextureIds(const TSharedPtr<struct FACEUIElement>& Node, TArray<uint32>& OutIds);

	UPROPERTY()
	TObjectPtr<UACEClientSubsystem> Client = nullptr;

	float ForwardAxis = 0.f;
	float RightAxis = 0.f;
	float TurnAxis = 0.f;
	float ForwardSent = 0.f;
	float RightSent = 0.f;
	float TurnSent = 0.f;
	bool bRunning = true; // AC default: run; Shift holds walk
	bool bRunningSent = true;
	/** Retail-style auto-run (NumLock / Mouse4). Cleared by W/S. */
	bool bAutoRun = false;
	bool bNumLockWasDown = false;
	bool bMouse4WasDown = false;
	bool bWasMoving = false;
	/** ControlRotation frozen for portal tunnel so exit view isn't tilted. */
	FRotator PortalSavedControlRotation = FRotator::ZeroRotator;
	bool bHavePortalSavedControlRotation = false;
	/** Jump charge: Space held fills 0→1 over ~1s (retail), release sends JumpPack. */
	bool bJumpCharging = false;
	bool bJumpChargeSent = false;
	bool bJumpAirborne = false;
	bool bJumpAirborneSent = false;
	/**
	 * Charge began with no loco axes — lock movement for the charge + arc (retail StandingLongJump).
	 * Run-into-jump keeps loco through charge and launch XY velocity.
	 */
	bool bStandingJumpLocked = false;
	/** W/S/Q/E held during a standing charge — applied as walk-scale leave-ground velocity. */
	float StandingJumpAimF = 0.f;
	float StandingJumpAimR = 0.f;
	/** Frozen facing for the airborne arc (A/D must not rotate the pawn mid-jump). */
	FQuat JumpLaunchAcQuat = FQuat::Identity;
	bool bHaveJumpLaunchFacing = false;
	float JumpChargeExtent = 0.f;
	/** JumpPack local ACE velocity (sent to server). */
	FVector JumpLocalAceVelocity = FVector::ZeroVector;
	/** Locked world-ACE launch velocity for the full ballistic arc (no air steer). */
	FVector JumpWorldAceVelocity = FVector::ZeroVector;
	float JumpAirborneSeconds = 0.f;
	bool bSpaceWasDown = false;
	/** After teleport, force the next MoveToState even if held keys are unchanged. */
	bool bForceMovementResend = false;
	bool bLocalPredicting = false;
	bool bHavePredictedPose = false;
	FACEPosition PredictedPose;
	/** Server-directed MoveToObject after Use (GameAction 0x36). */
	bool bServerMoveToActive = false;
	int32 ServerMoveToTargetGuid = 0;
	float ServerMoveToDistance = 0.6f;
	/** MoveTo FailDistance in AC units (melee charge default 15). 0 = no local fail check. */
	float ServerMoveToFailDistance = 0.f;
	FACEPosition ApproachStartPose;
	bool bHaveApproachStartPose = false;
	/** Face door first, then walk, then hold in UseRadius while AutoPos reports. */
	enum class EACEUseApproachPhase : uint8 { None, Turning, Walking, Holding };
	EACEUseApproachPhase UseApproachPhase = EACEUseApproachPhase::None;
	FVector ApproachTargetDir = FVector::ZeroVector;
	/** After navigate-in-range, re-send Use/PutInContainer so ActOnUse / pickup runs. */
	bool bResendUseWhenInRange = false;
	/** World time of last Approach Use/PutInContainer send (throttle retries while Holding). */
	double LastApproachUseTime = 0.0;
	/** True when approach should PutItemInContainer into the player (ground loot). */
	bool bApproachPickupIntoInventory = false;
	/** True when this approach must never send Use (give / server MoveOnly hand-off). */
	bool bApproachMoveOnly = false;
	/** Sent Use then local-walk; keep AutoPos until UseDone (idle StopMovement cancels CreateMoveToChain). */
	bool bAwaitingUseDone = false;
	/** Use sends made for this approach. Retail sends Use once per interaction. */
	int32 ApproachUseSendCount = 0;
	/** Seconds held after a vendor Use while waiting for ApproachVendor to arrive. */
	float VendorOpenWaitSeconds = 0.f;
	/** Seconds spent nearly stationary while Walking toward a Use target (door mesh block). */
	float ApproachStallSeconds = 0.f;
	FVector ApproachStallLastLoc = FVector::ZeroVector;
	float ApproachTurnSeconds = 0.f;
	double LastLeftClickTime = 0.0;
	int32 LastLeftClickGuid = 0;
	bool bHoverTooltipVisible = false;
	FString HoverTooltipText;
	FVector2D HoverCursorPixels = FVector2D::ZeroVector;
	/** Keep stepped feet Z briefly so outdoor heightfield cannot yank into the building void. */
	float StepHoldSeconds = 0.f;
	float StepHoldMinFeetZ = 0.f;
	/** Cached Setup cylinder (AC units) for Use-radius math — mirrors PhysicsObj.GetRadius/Height. */
	float PlayerSetupRadiusAc = 0.5f;
	float PlayerSetupHeightAc = 2.f;
	/** Last TeleportSeq applied from server UpdatePosition — forces snap on portal transitions. */
	uint16 LastAppliedTeleportSeq = 0;
	uint16 LastAppliedForcePositionSeq = 0;
	/** Authoritative F748 pose — soft-blended into prediction instead of hard resets. */
	FACEPosition LastServerPose;
	bool bHaveLastServerPose = false;
	/** Exp blend rate (1/s) pulling predicted XY toward LastServerPose while running. */
	float ServerReconcileRate = 2.0f;
	/** Ignore server XY error inside this deadzone (cm) so small lag never fights prediction. */
	float ServerReconcileDeadzoneCm = 150.f;
	/** Hard-snap only beyond this XY error (cm), or on teleport/indoor cell changes. */
	float ServerSnapErrorCm = 2500.f;

	/** Pull PredictedPose XY toward Server in continuous Unreal space; leave Z to ground snap. */
	void SoftReconcilePredictedTowardServer(const FACEPosition& Server, float DeltaTime, bool bAllowHeading);

	/**
	 * Client prediction runs at GetLocomotionSpeed(), but that assumes a default Run skill
	 * (~200). A real low-skill character moves far slower on the server, so prediction races
	 * ahead of every ~1 Hz anchor and the reconcile drags it back — the heavy rubber-band.
	 * We measure the server's actual ground speed from consecutive self UpdatePosition packets
	 * while running straight and fold it into PredictionSpeedScale so prediction tracks reality.
	 */
	void CalibratePredictionSpeedFromServer(const FACEPosition& ServerPose);
	float PredictionSpeedScale = 1.f;
	bool bHaveServerSpeedSample = false;
	double LastServerSpeedSampleTime = 0.0;
	FVector LastServerSpeedSamplePosUe = FVector::ZeroVector;

	/** Numpad-driven orbit camera state. Values are captured from the pawn's spring arm. */
	bool bCameraDefaultsCaptured = false;
	FRotator DefaultCameraRotation = FRotator::ZeroRotator;
	float DefaultCameraDistance = 261.f;
	/** Player-chosen third-person arm length (wheel / numpad). SpringArm collision shortens the eye without changing this. */
	float UserCameraArmLength = -1.f;
	bool bNumPadZeroWasDown = false;
	bool bNumPadFiveWasDown = false;
	bool bNumPadThreeWasDown = false;
	bool bEnterWasDown = false;
	bool bCameraInHead = false;
	bool bCameraLookDown = false;
	bool bMouseLookActive = false;
	bool bMouseLookUsesCapture = false;
	float MouseLookTravelPixels = 0.f;
	bool bRightMouseWasDown = false;
	bool bLeftMouseWasDown = false;
	bool bLeftOrbitEligible = false;
	void CycleNearbyTarget(bool bEnemies, int32 Direction);
	bool bRightClickEligible = false;
	bool bMouseLookMovedPlayer = false;
	bool bMouseForwardActive = false;
	float MouseLookPressX = 0.f;
	float MouseLookPressY = 0.f;
	/** Character yaw from RMB mouselook when PlayerOption UseMouseTurning (0x31) is on. */
	float PendingMouseTurnDegrees = 0.f;
	double LastMouseTurnServerResendSec = 0.0;
	FRotator LookDownSavedRotation = FRotator::ZeroRotator;
	float LookDownSavedArm = 0.f;
	bool bLookDownHasSaved = false;

	void UpdateOrbitCamera(float DeltaTime);
	void ApplyRetailCameraFov();
	void ApplyRetailCameraPivot(USpringArmComponent* Boom, UCapsuleComponent* Capsule) const;
	void ApplyCameraOffset(USpringArmComponent* Boom, float OffsetYAc, float OffsetZAc, float PitchDegrees, bool bKeepYaw);
	void SetCameraInHead(USpringArmComponent* Boom, bool bInHead);
	void SetCameraLookDown(USpringArmComponent* Boom, bool bLookDown);
	void ResetCameraToRetailDefaults(USpringArmComponent* Boom);
	void UpdateMouseLook(float DeltaTime, USpringArmComponent* Boom);
	bool UpdateMouseButtons(bool bRightDown, bool bLeftDown, bool bInputFocused, bool bOverUI);
	void ApplyMouseLookDelta(float DeltaX, float DeltaY, USpringArmComponent* Boom);
	void UpdateCombatTargetCameraAssist(float DeltaTime, USpringArmComponent* Boom);
	void SyncUserCameraArmLength(USpringArmComponent* Boom);
	void AdjustMouseCameraDistance(float WheelDelta);
	void SetMouseLookActive(bool bActive);
	bool IsUseMouseTurning() const;
	void IdentifyAtScreenPosition(float MouseX, float MouseY);
	float GetLocalCreatureScale() const;
	float GetCameraScaleCm() const;

	FTimerHandle DeferredLoginUITimerHandle;

	/** AddToPlayerScreen fails silently (returns false) if World->GetGameViewport() isn't
	 *  ready yet — common right after BeginPlay in "Play Standalone" while the OS window is
	 *  still being created. We keep retrying on a short timer until it actually succeeds. */
	FTimerHandle ShowLoginUIRetryTimerHandle;
	int32 ShowLoginUIRetryAttempts = 0;

	/** The real pixel size of the game viewport is often still (0,0) for a frame or two right
	 *  after AddToPlayerScreen succeeds (the window/backbuffer isn't fully initialized yet even
	 *  though GetGameViewport() is already non-null). If we only try once, the login widget's
	 *  viewport slot gets permanently stuck at a zero-sized rect — technically IsInViewport()
	 *  but with DesiredSize/allocated geometry of (0,0), i.e. invisible. We keep retrying on a
	 *  short timer until we observe a real, non-zero viewport size. */
	FTimerHandle DesiredSizeRetryTimerHandle;
	int32 DesiredSizeRetryAttempts = 0;

	bool AttachLoginWidgetToScreen();
	void ApplyLoginUIInputMode();
	void ScheduleShowLoginUIRetry();
	void ApplyViewportSizeToLoginWidget();
	void ScheduleDesiredSizeRetry();

	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void JumpPressed();
	void JumpReleased();
	void BeginJumpCharge();
	void ReleaseJump(float Forward, float Right);
	void SprintPressed();
	void SprintReleased();
	void ApplyInWorldInputMode();
	void PollObjectClick();
	void PollObjectHover();
	/** True when the cursor is over interactive UMG (inventory, hotbar, panels) — skip world pick. */
	bool IsMouseOverBlockingUI() const;
	void EnsureHoverTooltipWidget();
	void EnsureRetailMouseCursor();
	void EnableGameplayViewportShadows();
	void ApplyRetailMouseCursor(bool bInteractable, bool bCompatible = true);
	void BeginServerMoveTo(const FACEObjectMotionState& Motion);
	void ClearServerMoveTo();
	float GetStepUpHeightCm() const;
	/** Retail Setup.StepDownHeight * Scale * WorldScale — max snap/walk-off drop (CheckWalkable). */
	float GetStepDownHeightCm() const;
	void ApplyPlayerCapsuleFromSetup(int32 SetupId);
	/** NPK walks through players; PK / PK Lite block ECC_Pawn. */
	void ApplyLocalPawnPkCollision();

	UACECharacterAppearanceComponent* EnsurePawnAppearance();

	UFUNCTION()
	void HandleEnteredWorld(int32 PlayerGuid, const FACEPosition& SpawnPosition);

	UFUNCTION()
	void HandlePositionUpdate(int32 ObjectGuid, const FACEPosition& Position);

	/** Retail 0xF751 cue: enter portal space before the destination position arrives. */
	UFUNCTION()
	void HandlePlayerTeleportStarted();

	UFUNCTION()
	void HandleMotionUpdate(int32 ObjectGuid, const FACEObjectMotionState& Motion);

	UFUNCTION()
	void HandlePhysicsStateUpdate(int32 ObjectGuid, int32 PhysicsState);

	UFUNCTION()
	void HandleCharacterList(const TArray<FACECharacterInfo>& Characters, const FString& ServerName);

	UFUNCTION()
	void HandleSessionStateChanged(EACESessionState NewState);

	UFUNCTION()
	void HandleObjectCreated(const FACEWorldObject& Object);

	UFUNCTION()
	void HandleVitalsUpdated(const FACEPlayerVitals& Vitals);

	void ApplyPendingSelfAppearance();
	int32 PendingSelfAppearanceGuid = 0;

	void BeginWorldTransition(const TCHAR* Reason);
	void TickWorldTransition();
	void BeginWorldReveal();
	void FinishWorldTransition();
	bool IsWorldTransitionReady(FString* OutBlockingGate = nullptr, bool bIgnoreServerUnhide = false) const;
	/** Validate real blocking collision and supporting ground before enabling the pawn. */
	bool FindWorldEntryPlacement(FVector& OutCapsuleCenter) const;
	/** One server-authoritative recall per failed arrival; never reveal an unsafe spawn. */
	bool TickWorldEntryRecovery(float DeltaTime, const FString& BlockingGate);
	void ResetWorldEntryDestination();
	void FailWorldEntry(const FString& Reason);
	/** Send LoginComplete once per transition so the server clears Teleporting. */
	void NotifyPortalArrivalIfNeeded();
	/** Reset cached MoveToState axes and require a fresh send after TeleportSeq changes. */
	void InvalidateMovementAfterTeleport();
	/** Rain/weather PES must not composite over the portal tunnel camera. */
	void SetSkyWeatherEnabled(bool bEnabled);

	/** Legacy names kept for call sites / readability. */
	void BeginEnterWorldLoadScreen() { BeginWorldTransition(TEXT("enter-world")); }
	void TickEnterWorldLoadScreen() { TickWorldTransition(); }
	void EndEnterWorldLoadScreen() { BeginWorldReveal(); }
	bool IsEnterWorldLoadReady() const { return IsWorldTransitionReady(nullptr); }

	UPROPERTY()
	TObjectPtr<AACELoadingScreenActor> LoadingScreenActor = nullptr;

	bool bEnterWorldLoading = false;
	bool bWorldRevealActive = false;
	float PortalWorldRevealElapsed = -1.f;
	float EnterWorldLoadElapsed = 0.f;
	float WcTerrainStreamAccum = 0.f;
	int32 WcLastStreamTileX = INT32_MIN;
	int32 WcLastStreamTileY = INT32_MIN;
	/** Time the textured portal tunnel has been on-camera (starts after mesh ready). */
	float TunnelVisibleElapsed = 0.f;
	/** Minimum time in portal space so the tunnel is visible even on fast machines. */
	float EnterWorldLoadMinSeconds = 1.5f;
	/** Visual waits may expire; unsafe physical destinations request one lifestone recovery. */
	float WorldTransitionMaxSeconds = 45.f;
	bool bLoggedTransitionTimeout = false;
	bool bWaitingForServerUnhide = false;
	/** True after LoginComplete was sent for the current portal/enter-world transition. */
	bool bPortalExitNotified = false;
	bool bWorldEntryRecoveryAttempted = false;
	bool bAwaitingRecoveryDestination = false;
	float WorldEntryRecoveryElapsed = 0.f;
	FString WorldEntryFailureReason;
	FDelegateHandle EditorThrottleDelegate;
	/** ClassicGameplay layout textures resolved while still in the portal tunnel. */
	bool bGameplayUiAssetsReady = false;
	/**
	 * CharacterList arrived but char-select DAT art is not ready yet — keep login up
	 * until TryPrefetchCharacterSelectAssets succeeds.
	 */
	bool bPendingCharSelect = false;
	FString CharacterSelectLoadError;
	TArray<FACECharacterInfo> PendingCharSelectCharacters;
	FString PendingCharSelectServerName;
};
