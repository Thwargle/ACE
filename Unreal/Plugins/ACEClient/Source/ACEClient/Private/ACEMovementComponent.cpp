#include "ACEMovementComponent.h"
#include "ACEClientSubsystem.h"
#include "GameFramework/Actor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UACEMovementComponent::UACEMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UACEMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			Client = GI->GetSubsystem<UACEClientSubsystem>();
			if (Client)
			{
				Client->OnPositionUpdate.AddDynamic(this, &UACEMovementComponent::HandlePositionUpdate);
				Client->OnEnteredWorld.AddDynamic(this, &UACEMovementComponent::HandleEnteredWorld);
			}
		}
	}
}

void UACEMovementComponent::SetMoveInput(float Forward, float Strafe, float Turn)
{
	ForwardInput = FMath::Clamp(Forward, -1.f, 1.f);
	StrafeInput = FMath::Clamp(Strafe, -1.f, 1.f);
	TurnInput = FMath::Clamp(Turn, -1.f, 1.f);
}

void UACEMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Client || Client->GetSessionState() != EACESessionState::InWorld)
	{
		return;
	}

	const bool bMoving = !FMath::IsNearlyZero(ForwardInput) || !FMath::IsNearlyZero(StrafeInput) || !FMath::IsNearlyZero(TurnInput);
	const bool bChanged =
		!FMath::IsNearlyEqual(ForwardInput, ForwardSent) ||
		!FMath::IsNearlyEqual(StrafeInput, StrafeSent) ||
		!FMath::IsNearlyEqual(TurnInput, TurnSent) ||
		(bMoving != bWasMoving);

	if (bChanged)
	{
		if (bMoving)
		{
			Client->SendMovement(ForwardInput, StrafeInput, TurnInput, bRunning);
		}
		else
		{
			Client->StopMovement();
		}
		ForwardSent = ForwardInput;
		StrafeSent = StrafeInput;
		TurnSent = TurnInput;
		bWasMoving = bMoving;
	}

	// Client-side prediction: nudge local origin while moving so AutonomousPosition stays fresh.
	if (bMoving)
	{
		FACEPosition Pos = Client->GetPlayerPosition();
		if (Pos.IsValid())
		{
			const float Speed = Client->GetLocomotionSpeed(bRunning);
			// Location is AC-space; GetAce*InAcSpace for prediction.
			const float BackFactor = (ForwardInput < 0.f) ? 0.65f : 1.f;
			Pos.Location += (Pos.GetAceForwardInAcSpace() * (ForwardInput * BackFactor) + Pos.GetAceRightInAcSpace() * StrafeInput) * Speed * DeltaTime;
			Pos.NormalizeOutdoorLandblock();
			Client->SetReportedPosition(Pos);
			if (bSyncActorFromServer)
			{
				ApplyPositionToOwner(Pos);
			}
		}
	}
}

void UACEMovementComponent::HandleEnteredWorld(int32 /*PlayerGuid*/, const FACEPosition& SpawnPosition)
{
	if (bSyncActorFromServer)
	{
		ApplyPositionToOwner(SpawnPosition);
	}
}

void UACEMovementComponent::HandlePositionUpdate(int32 ObjectGuid, const FACEPosition& Position)
{
	if (!bSyncActorFromServer || !Client)
	{
		return;
	}
	if (ObjectGuid != Client->GetPlayerGuid())
	{
		return;
	}
	Client->SetReportedPosition(Position);
	ApplyPositionToOwner(Position);
}

void UACEMovementComponent::ApplyPositionToOwner(const FACEPosition& Position)
{
	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorLocation(Position.ToUnrealLocation(WorldScale));
		Owner->SetActorRotation(Position.ToUnrealQuat());
	}
}
