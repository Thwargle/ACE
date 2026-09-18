#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ACEViewerPawn.generated.h"

class UCapsuleComponent;
class UACECharacterAppearanceComponent;
class UACEMovementComponent;
class UACEScriptComponent;
class USpringArmComponent;
class UCameraComponent;
class UACEVRComponent;

/**
 * Simple pawn: capsule collision + ACE appearance (filled in by ACEPlayerController on login).
 * Camera is a SpringArm+Camera on the retail CameraSet pivot (1.5 AC above feet) so it always
 * follows the capsule's yaw (ACE heading / turn input) — CameraSet third-person, not FPS.
 */
UCLASS()
class ACEVIEWER_API AACEViewerPawn : public APawn
{
	GENERATED_BODY()

public:
	AACEViewerPawn();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE|VR")
	TObjectPtr<UACEVRComponent> VR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACECharacterAppearanceComponent> Appearance;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACEScriptComponent> ScriptComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACEMovementComponent> ACEMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UCameraComponent> FollowCamera;
};
