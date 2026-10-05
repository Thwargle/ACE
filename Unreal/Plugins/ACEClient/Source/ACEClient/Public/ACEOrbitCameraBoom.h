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
	virtual FVector BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime) override;
	virtual void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FACEVRWallContactTest;
#endif
	bool bApplyingOrbitRotation = false;
	FRotator OrbitRotation = FRotator::ZeroRotator;
};
