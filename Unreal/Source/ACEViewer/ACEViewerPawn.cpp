#include "ACEViewerPawn.h"
#include "ACECameraRetail.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Scene.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEMovementComponent.h"
#include "ACEScriptComponent.h"
#include "VR/ACEVRComponent.h"

AACEViewerPawn::AACEViewerPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	// Defaults: shoulder-width radius; half-height ≈ Setup.Height/2 at WorldScale 100.
	Capsule->InitCapsuleSize(34.f, 96.f);
	// Query-only: ACE prediction teleports the pawn and snaps Z from a ground trace.
	// Physics blocking against procedural landblock collision embedded the capsule and
	// made swept moves fail (run-in-place).
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	Capsule->SetHiddenInGame(true);
	Capsule->SetVisibility(false);
	Capsule->SetCastShadow(false);
	SetRootComponent(Capsule);
	VR = CreateDefaultSubobject<UACEVRComponent>(TEXT("VR"));

	Appearance = CreateDefaultSubobject<UACECharacterAppearanceComponent>(TEXT("Appearance"));
	Appearance->bUseWorldLighting = true;
	Appearance->bHideOwnerMeshes = true;
	Appearance->bAlignMeshToCapsuleBottom = true;

	ScriptComponent = CreateDefaultSubobject<UACEScriptComponent>(TEXT("ScriptComponent"));

	ACEMovement = CreateDefaultSubobject<UACEMovementComponent>(TEXT("ACEMovement"));
	// PlayerController owns prediction + MoveToState; keep this component from fighting it.
	ACEMovement->bSyncActorFromServer = false;
	ACEMovement->SetComponentTickEnabled(false);

	// Retail CameraSet::SetDefaultOffsets at WorldScale 100 / scale 1:
	// pivot 1.5 AC above feet, viewer_offset (0, -2.5, 0.75) AC. Spring arm sits on the
	// pivot; yaw +90 because AC facing is pawn +Y and the arm defaults to −X.
	constexpr float DefaultWorldScale = 100.f;
	const float CapsuleHalf = 96.f;
	const float PivotZ = ACECameraRetail::DefaultPivotZAc * DefaultWorldScale;
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Capsule);
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, PivotZ - CapsuleHalf));
	CameraBoom->SetRelativeRotation(FRotator(
		ACECameraRetail::DefaultPitchDegrees(),
		ACECameraRetail::BoomYawToFaceAcForward,
		0.f));
	CameraBoom->TargetArmLength = ACECameraRetail::DefaultArmLengthCm(DefaultWorldScale);
	CameraBoom->SocketOffset = FVector::ZeroVector;
	CameraBoom->TargetOffset = FVector::ZeroVector;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = false;
	// Retract against terrain/architecture so low orbit angles can never put the camera
	// beneath the environment.
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 22.f;
	CameraBoom->ProbeChannel = ECC_Camera;
	// Retail translation stiffness lets the view fall behind while running,
	// then catch up when stopped. Keep manual zoom independent of this offset.
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = ACECameraRetail::TranslationLagSpeed;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 10.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetConstraintAspectRatio(false);
	FollowCamera->FieldOfView = ACECameraRetail::HorizontalFovDegrees(16.f / 9.f);
	FollowCamera->PostProcessBlendWeight = 1.f;
	FollowCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	FollowCamera->PostProcessSettings.MotionBlurAmount = 0.f;
	FollowCamera->PostProcessSettings.bOverride_MotionBlurMax = true;
	FollowCamera->PostProcessSettings.MotionBlurMax = 0.f;
	// Engine OpenWorld PPV enables Lumen. Camera weight 1 must pin GI/reflections off
	// or DefaultLit LScape gets a sky mirror and never shows sun CSM.
	FollowCamera->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	FollowCamera->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	FollowCamera->PostProcessSettings.bOverride_ReflectionMethod = true;
	FollowCamera->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
	FollowCamera->PostProcessSettings.bOverride_bMegaLights = true;
	FollowCamera->PostProcessSettings.bMegaLights = false;
}
