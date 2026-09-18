#include "ACELoadingScreenActor.h"
#include "ACECameraRetail.h"
#include "ACERetailPortalAnimation.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEDatSubsystem.h"
#include "ACETypes.h"
#include "Camera/CameraComponent.h"
#include "Math/RotationMatrix.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Far below Dereth so land/sky never fill the portal-space frustum. */
	const FVector PortalSpaceWorldOrigin(0.f, 0.f, -250000.f);
}

AACELoadingScreenActor::AACELoadingScreenActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	CameraBoom = CreateDefaultSubobject<USceneComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Root);
	CameraBoom->SetMobility(EComponentMobility::Movable);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom);
	Camera->SetMobility(EComponentMobility::Movable);
	Camera->bUsePawnControlRotation = false;
	Camera->SetConstraintAspectRatio(false);
	ApplyPortalCamera();

	Scripts = CreateDefaultSubobject<UACEScriptComponent>(TEXT("ACEPortalScripts"));
	Appearance = CreateDefaultSubobject<UACECharacterAppearanceComponent>(TEXT("ACEPortalAppearance"));
	if (Appearance)
	{
		Appearance->PrimaryComponentTick.bTickEvenWhenPaused = true;
		Appearance->PrimaryComponentTick.TickGroup = TG_PrePhysics;
	}
}

void AACELoadingScreenActor::PickNextTeleportYaw()
{
	TeleportYawStart = TeleportYaw;
	TeleportYawEnd = FMath::FRandRange(0.f, 360.f);
	TeleportYawDuration = FMath::FRandRange(0.6f, 1.8f);
	TeleportYawElapsed = 0.f;
}

void AACELoadingScreenActor::ApplyPortalCamera()
{
	// gmSmartBoxUI::PostInit: CreatureMode::SetCameraPosition(0.24, -2.7, 0.88) AC units.
	// Identity creature_view_frame looks along AC +Y. SetCameraDirection_Degrees(0, angle, 0)
	// euler_set_rotate(0,0,0) then Frame::rotate/grotate around world Y — roll about look,
	// not a heading yaw of the look vector (that spun the tunnel left/right).
	const FVector EyeRel = FACEPosition::AceVectorToUnreal(
		FVector(0.24f, -2.7f, 0.88f), WorldScale);
	const FVector LookUe = FACEPosition::AceVectorToUnreal(FVector(0.f, 1.f, 0.f), 1.f);
	// Frame::grotate around AC +Y sends +Z toward +X. Convert the rotated basis
	// after rotation: the AC -> UE X reflection reverses the roll's handedness.
	const float Angle = TeleportYaw * 0.017453292f;
	const FVector UpUe = FACEPosition::AceVectorToUnreal(FVector(FMath::Sin(Angle), 0.f, FMath::Cos(Angle)), 1.f);
	const FRotator CamRot = FRotationMatrix::MakeFromXZ(LookUe, UpUe).Rotator();
	if (Appearance && Appearance->GetMeshRoot())
	{
		// A rolled camera and an inversely rolled scene have the same view.
		// Rotate about the authored eye, not the tunnel origin, to retain its framing.
		const FQuat Unrolled = FRotationMatrix::MakeFromXZ(LookUe, FVector::UpVector).ToQuat();
		const FQuat SceneRoll = bTrackedView ? (CamRot.Quaternion() * Unrolled.Inverse()).Inverse() : FQuat::Identity;
		Appearance->GetMeshRoot()->SetRelativeLocationAndRotation(
			EyeRel - SceneRoll.RotateVector(EyeRel), SceneRoll);
	}
	if (CameraBoom)
	{
		CameraBoom->SetRelativeLocation(EyeRel);
		CameraBoom->SetRelativeRotation(CamRot);
	}
	if (Camera)
	{
		Camera->bUsePawnControlRotation = false;
		Camera->SetRelativeLocation(FVector::ZeroVector);
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
		int32 SizeX = 1280, SizeY = 720;
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->GetViewportSize(SizeX, SizeY);
			}
		}
		ACECameraRetail::ApplyFovToCamera(Camera, SizeX, SizeY);
		// gmSmartBoxUI changes SmartBox's world projection on exit, not the
		// separate CreatureMode portal camera. Widening this view exposes the
		// finite tunnel rim as a disk surrounded by black.
		Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
		Camera->PostProcessSettings.AutoExposureMinBrightness = 1.f;
		Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
		Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.f;
		Camera->PostProcessBlendWeight = 1.f;
	}
}

void AACELoadingScreenActor::SetTrackedView(bool bEnabled)
{
	if (bTrackedView == bEnabled) return;
	bTrackedView = bEnabled;
	ApplyPortalCamera();
}

void AACELoadingScreenActor::ApplyPortalCameraFromMesh()
{
	ApplyPortalCamera();
	const FVector Eye = CameraBoom ? CameraBoom->GetRelativeLocation() : FVector::ZeroVector;
	UE_LOG(LogTemp, Log,
		TEXT("ACE: portal camera CreatureMode eyeRel=(%.0f,%.0f,%.0f) roll=%.0f (retail 0.24,-2.7,0.88 AC, grotate Y)"),
		Eye.X, Eye.Y, Eye.Z, TeleportYaw);
}

void AACELoadingScreenActor::BeginPlay()
{
	Super::BeginPlay();
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetActorTickEnabled(true);
}

void AACELoadingScreenActor::BeginTunnel()
{
	Phase = EACEPortalTransitionPhase::Tunnel;
	bTrackedView = false;
	RevealElapsed = 0.f;
	bPortalAmbientStarted = false;
	bMeshReady = false;
	TeleportYaw = 0.f;
	PickNextTeleportYaw();
	SetActorLocation(PortalSpaceWorldOrigin);
	SetActorRotation(FRotator::ZeroRotator);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(false);
	SetActorTickEnabled(true);
	ApplyPortalCamera();
	if (Appearance)
	{
		Appearance->ResetDefaultAnimClock();
		Appearance->SetAppearanceVisible(true);
		// The tunnel actor owns the animation clock, including subsequent teleports.
		Appearance->SetComponentTickEnabled(false);
	}
	if (Root)
	{
		Root->SetVisibility(true, true);
		Root->SetHiddenInGame(false, true);
	}
}

bool AACELoadingScreenActor::TryStartPortalAmbient(uint32 WaveId, float Volume)
{
	if (bPortalAmbientStarted || WaveId == 0 || !Scripts)
	{
		return false;
	}
	Scripts->PlayCenteredWave(static_cast<int32>(WaveId), Volume, /*bLoop*/ true);
	bPortalAmbientStarted = true;
	UE_LOG(LogTemp, Log, TEXT("ACE: portal-space ambient from anim hook (wave=0x%08X vol=%.2f)"), WaveId, Volume);
	return true;
}

bool AACELoadingScreenActor::TryBuildMesh()
{
	if (bMeshReady && Appearance && Appearance->HasAppearance())
	{
		return true;
	}
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat || !Appearance)
	{
		return false;
	}
	if (!Dat->IsDatReady())
	{
		Dat->BeginBackgroundLoad();
		return false;
	}
	if (!Dat->PrefetchPortalSpaceSetup(SetupId, WorldScale))
	{
		return false;
	}

	float IgnoredStep = 0.f, IgnoredHeight = 0.f, IgnoredRadius = 0.f;
	uint32 SetupDefaultAnim = 0;
	if (Dat->TryGetSetupPhysics(static_cast<uint32>(SetupId), IgnoredStep, IgnoredHeight, IgnoredRadius, SetupDefaultAnim)
		&& SetupDefaultAnim != 0)
	{
		AnimationId = static_cast<int32>(SetupDefaultAnim);
	}

	// Ring already lies in XY after AceVectorToUnreal. Extra yaw hid the tunnel.
	Appearance->MeshFacingYawDegrees = 0.f;

	FACEWorldObject PortalObj;
	PortalObj.SetupId = SetupId;
	PortalObj.DefaultAnimationId = AnimationId;
	PortalObj.Scale = 1.f;
	if (!Appearance->ApplyWorldObject(PortalObj, WorldScale, /*bEnablePartCollision*/ false))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE: portal tunnel Setup 0x%08X appearance build failed"), SetupId);
		return false;
	}
	// Actor Tick drives appearance (paused portal); avoid double-advance from engine tick.
	Appearance->SetComponentTickEnabled(false);

	if (Scripts)
	{
		Scripts->InitializeFromObject(PortalObj, WorldScale);
		Scripts->NotifyAppearanceReady(/*bIsDoor*/ false);
		Scripts->SetComponentTickEnabled(true);
	}

	bMeshReady = Appearance->HasAppearance();
	if (bMeshReady)
	{
		ApplyPortalCameraFromMesh();
		UE_LOG(LogTemp, Log,
			TEXT("ACE: portal tunnel Setup 0x%08X anim=0x%08X ready (appearance)"),
			SetupId, AnimationId);
	}
	return bMeshReady;
}

void AACELoadingScreenActor::HideTunnelVisuals()
{
	if (Appearance)
	{
		Appearance->SetAppearanceVisible(false);
	}
}

void AACELoadingScreenActor::HideAllVisuals()
{
	HideTunnelVisuals();
	SetActorHiddenInGame(true);
}

bool AACELoadingScreenActor::TickReveal(float DeltaTime, const FVector& PawnLocation)
{
	(void)PawnLocation;
	if (Phase == EACEPortalTransitionPhase::Finished)
	{
		return true;
	}
	Phase = EACEPortalTransitionPhase::Reveal;
	RevealElapsed += FMath::Max(0.f, DeltaTime);
	ApplyPortalCamera();
	if (RevealElapsed < 1.f) return false;
	Phase = EACEPortalTransitionPhase::Finished;
	HideTunnelVisuals();
	return true;
}

void AACELoadingScreenActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Phase == EACEPortalTransitionPhase::Finished)
	{
		return;
	}
	if (!bMeshReady)
	{
		MeshRetrySeconds += DeltaTime;
		if (MeshRetrySeconds >= 0.25f)
		{
			MeshRetrySeconds = 0.f;
			TryBuildMesh();
		}
	}
	else if (Appearance && Appearance->IsRegistered())
	{
		Appearance->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
	}

	TeleportYawElapsed += DeltaTime;
	if (TeleportYawElapsed >= TeleportYawDuration)
	{
		TeleportYaw = TeleportYawEnd;
		PickNextTeleportYaw();
	}
	else
	{
		const float T = FMath::Clamp(TeleportYawElapsed / FMath::Max(TeleportYawDuration, 0.01f), 0.f, 1.f);
		// Retail interpolates the numeric angles directly, including turns > 180 degrees.
		TeleportYaw = TeleportYawStart + (TeleportYawEnd - TeleportYawStart)
			* (ACERetailPortalAnimation::GetAnimLevel(T) / 1024.f);
	}
	ApplyPortalCamera();
}
