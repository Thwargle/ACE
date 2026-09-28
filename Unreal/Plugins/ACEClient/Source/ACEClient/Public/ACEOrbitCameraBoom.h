#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "ACEOrbitCameraBoom.generated.h"

/** Desktop orbit camera: smooth heading and pitch without rolling through the pole. */
UCLASS()
class ACECLIENT_API UACEOrbitCameraBoom : public USpringArmComponent
{
	GENERATED_BODY()

public:
	virtual FRotator GetDesiredRotation() const override;

protected:
	virtual void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime) override;

private:
	bool bApplyingOrbitRotation = false;
	FRotator OrbitRotation = FRotator::ZeroRotator;
};
