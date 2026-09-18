#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ACEScriptComponent.h"
#include "ACELoadingScreenActor.generated.h"

class UACECharacterAppearanceComponent;
class UCameraComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EACEPortalTransitionPhase : uint8
{
	Tunnel,
	Reveal,
	Finished
};

/**
 * Retail portal space (gmSmartBoxUI + CreatureMode) — not a weenie.
 * DIDMap 0x25000010 enum 268435457 → Setup 0x02000306 portalspace_background;
 * enum 268435458 → Animation 0x030005AC portalspace_animation (40 fps, start frame 1).
 * Weenie 14579 W_PORTALPORTALSPACE_CLASS is a world portal into the Invoking dungeon.
 * Camera: SetCameraPosition (0.24, -2.7, 0.88) AC, identity look along AC +Y, then
 * SetCameraDirection_Degrees(0, angle, 0) rolls around that look axis (Frame::grotate Y).
 */
UCLASS()
class ACECLIENT_API AACELoadingScreenActor : public AActor
{
	GENERATED_BODY()
	friend class FACERetailPortalSpaceTest;

public:
	AACELoadingScreenActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Build the two Setup parts (retries until DAT is ready). */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	bool TryBuildMesh();

	/** Enter/keep tunnel phase in a void below the map. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	void BeginTunnel();

	/** One-second tunnel stretch; controller follows with the world stretch. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	bool TickReveal(float DeltaTime, const FVector& PawnLocation);

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void HideAllVisuals();

	UFUNCTION(BlueprintPure, Category = "ACE")
	EACEPortalTransitionPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "ACE")
	bool IsRevealFinished() const { return Phase == EACEPortalTransitionPhase::Finished; }

	/** Portal-space ambient: loop once per tunnel (SoundTweaked @ anim frame 2). */
	bool TryStartPortalAmbient(uint32 WaveId, float Volume);

	/** Keep the HMD upright and reproduce the retail camera roll by rotating the tunnel. */
	void SetTrackedView(bool bEnabled);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<USceneComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACEScriptComponent> Scripts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACECharacterAppearanceComponent> Appearance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	int32 SetupId = 0x02000306;

	/** Portalspace Animation — 120 frames @ 40 fps, SoundTweaked 0x0A000316 @ frame 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	int32 AnimationId = 0x030005AC;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	float WorldScale = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	bool bMeshReady = false;

private:
	void ApplyPortalCamera();
	/** Snap boom to the retail CreatureMode eye after the Setup cooks. */
	void ApplyPortalCameraFromMesh();
	void HideTunnelVisuals();
	void PickNextTeleportYaw();

	EACEPortalTransitionPhase Phase = EACEPortalTransitionPhase::Tunnel;
	float MeshRetrySeconds = 0.f;
	float RevealElapsed = 0.f;
	bool bPortalAmbientStarted = false;
	bool bTrackedView = false;
	/** gmSmartBoxUI teleportRotationCurAngle (degrees, roll about AC +Y look). */
	float TeleportYaw = 0.f;
	float TeleportYawStart = 0.f;
	float TeleportYawEnd = 0.f;
	float TeleportYawDuration = 1.2f;
	float TeleportYawElapsed = 0.f;
};
