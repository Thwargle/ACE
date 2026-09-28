#include "ACEOrbitCameraBoom.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Misc/ScopeExit.h"

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
