#include "ACEOrbitCameraBoom.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Misc/ScopeExit.h"
#include "ACEPlayerController.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACECellTransit.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"

FVector UACEOrbitCameraBoom::BlendLocations(const FVector& Desired, const FVector& HitLocation, bool bHit, float DeltaTime)
{
	const auto* Pawn = Cast<APawn>(GetOwner());
	const auto* PC = Pawn ? Cast<AACEPlayerController>(Pawn->GetController()) : nullptr;
	auto* Dat = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!bHit || !PC || !Dat || !ACECellTransit::IsIndoorCell(PC->GetEffectiveCellId()))
		return Super::BlendLocations(Desired, HitLocation, bHit, DeltaTime);

	// Retail's viewer sphere collides with its cells, not the outdoor heightfield
	// through an indoor floor. Check short segments so a boom exiting a doorway
	// still collides with outdoor ground and every segment still checks walls.
	const FVector Origin = GetComponentLocation() + TargetOffset;
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(FVector::Distance(Origin, Desired) / 50.f), 1, 128);
	uint32 Cell = PC->GetEffectiveCellId();
	FVector Start = Origin;
	for (int32 I=1; I<=Steps; ++I)
	{
		const FVector End = FMath::Lerp(Origin, Desired, double(I)/Steps);
		TArray<uint32> Cells;
		ACECellTransit::FindCellList(*Dat, Cell, (Start+End)*.5, ProbeSize+FVector::Distance(Start,End)*.5, PC->WorldScale, Cells);
		const bool IndoorOnly = !Cells.IsEmpty() && !Cells.ContainsByPredicate([](uint32 C){ return !ACECellTransit::IsIndoorCell(C); });
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ACEIndoorCamera), false, GetOwner());
		FHitResult Contact;
		while (GetWorld()->SweepSingleByChannel(Contact, Start, End, FQuat::Identity, ProbeChannel,
			FCollisionShape::MakeSphere(ProbeSize), Query))
		{
			const auto* Component = Contact.GetComponent();
			if (!Component || !Component->ComponentTags.Contains(TEXT("ACEOutdoorTerrain"))) return Contact.Location;
			// A portal intersecting the probe's broad-phase envelope adds an
			// outdoor cell even though the actual terrain contact is inside the
			// room. Test that contact against the room BSP before shortening the
			// boom. Outside contacts and building walls still block normally.
			const bool ContactInside = IndoorOnly || Cells.ContainsByPredicate([&](uint32 C)
			{
				return ACECellTransit::IsIndoorCell(C)
					&& Dat->IsPointInsideEnvCell(C, Contact.Location, PC->WorldScale, 0.f)
					&& Dat->IsPointInsideEnvCell(C, Contact.ImpactPoint, PC->WorldScale, 0.f);
			});
			if (!ContactInside) return Contact.Location;
			Query.AddIgnoredComponent(Component);
		}
		uint32 NextCell=Cell;
		ACECellTransit::ResolveViewerCellId(*Dat, Cell, Start, End, PC->WorldScale, NextCell);
		Cell=NextCell; Start=End;
	}
	return Desired;
}

FRotator UACEOrbitCameraBoom::GetDesiredRotation() const
{
	return bApplyingOrbitRotation ? OrbitRotation : Super::GetDesiredRotation();
}

void UACEOrbitCameraBoom::UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime)
{
	// Resolve the pawn/control/inheritance rules before smoothing in world space.
	const FRotator Target = GetTargetRotation();
	OrbitRotation = FRotator(Target.Pitch, Target.Yaw, 0.f);
	if (bDoRotationLag && CameraRotationLagSpeed > 0.f)
	{
		float Dt = FMath::Max(0.f, DeltaTime);
		if (bClampToMaxPhysicsDeltaTime) Dt = FMath::Min(Dt, UPhysicsSettings::Get()->MaxPhysicsDeltaTime);
		const float Alpha = 1.f - FMath::Exp(-CameraRotationLagSpeed * Dt);
		// Retail CameraManager::UpdateCamera -> Frame::set_vector_heading builds
		// an upright heading/pitch frame. Quaternion interpolation between two
		// upright endpoints can introduce severe roll near the downward pole.
		OrbitRotation.Pitch = FMath::Lerp(PreviousDesiredRot.Pitch, Target.Pitch, Alpha);
		OrbitRotation.Yaw = PreviousDesiredRot.Yaw
			+ FMath::FindDeltaAngleDegrees(PreviousDesiredRot.Yaw, Target.Yaw) * Alpha;
	}
	OrbitRotation.Normalize();

	// SpringArm's GetTargetRotation is not virtual. Supply the resolved rotation
	// through GetDesiredRotation and bypass its second inheritance/control pass
	// for this call only. Leave translation lag, obstruction sweeps, socket
	// offsets and child transforms to the engine's existing implementation.
	const bool bSavedControl = bUsePawnControlRotation;
	const bool bSavedPitch = bInheritPitch, bSavedYaw = bInheritYaw, bSavedRoll = bInheritRoll;
	ON_SCOPE_EXIT
	{
		bApplyingOrbitRotation = false;
		bUsePawnControlRotation = bSavedControl;
		bInheritPitch = bSavedPitch; bInheritYaw = bSavedYaw; bInheritRoll = bSavedRoll;
	};
	bApplyingOrbitRotation = true;
	bUsePawnControlRotation = false;
	bInheritPitch = bInheritYaw = bInheritRoll = true;
	Super::UpdateDesiredArmLocation(bDoTrace, bDoLocationLag, false, DeltaTime);
}
