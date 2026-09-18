#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ACETypes.h"
#include "ACEMovementComponent.generated.h"

class UACEClientSubsystem;

/**
 * Attach to a pawn/character. Translates Unreal movement input into ACE MoveToState
 * and optionally syncs the actor transform from ACE position updates.
 */
UCLASS(ClassGroup = (ACE), meta = (BlueprintSpawnableComponent))
class ACECLIENT_API UACEMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UACEMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** If true, Apply ACE UpdatePosition for our player guid onto the owner actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bSyncActorFromServer = false;

	/** Scale ACE units → Unreal centimeters (default 100 = 1 AC unit → 1 meter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	float WorldScale = 100.f;

	/** Hold Shift / sprint as ACE run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bRunning = false;

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void SetMoveInput(float Forward, float Strafe, float Turn);

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void SetRunning(bool bInRunning) { bRunning = bInRunning; }

private:
	UPROPERTY()
	TObjectPtr<UACEClientSubsystem> Client = nullptr;

	float ForwardInput = 0.f;
	float StrafeInput = 0.f;
	float TurnInput = 0.f;
	float ForwardSent = 0.f;
	float StrafeSent = 0.f;
	float TurnSent = 0.f;
	bool bWasMoving = false;

	UFUNCTION()
	void HandlePositionUpdate(int32 ObjectGuid, const FACEPosition& Position);

	UFUNCTION()
	void HandleEnteredWorld(int32 PlayerGuid, const FACEPosition& SpawnPosition);

	void ApplyPositionToOwner(const FACEPosition& Position);
};
