#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Camera/CameraComponent.h"
#include "MotionControllerComponent.h"

namespace
{
	bool ProjectToViewPlane(const FTransform& View, const UMotionControllerComponent* Aim, float Distance, FVector2D& Point)
	{
		if (!Aim || !Aim->IsTracked()) return false;
		const FVector Origin = View.InverseTransformPosition(Aim->GetComponentLocation());
		const FVector Direction = View.InverseTransformVectorNoScale(Aim->GetForwardVector());
		if (Direction.X <= .05f) return false;
		const float T = (Distance - Origin.X) / Direction.X;
		if (T < 0.f || T > 500.f) return false;
		const FVector Hit = Origin + Direction * T;
		Point = FVector2D(Hit.Y, Hit.Z);
		return true;
	}
}

FTransform UACEVRComponent::GetVitalsAnchorTransform() const
{
	return Settings->VitalsAnchorMode == 0 ? Head->GetComponentTransform() : VitalsAnchorFrame;
}

void UACEVRComponent::UpdateVitalsAnchor(float Dt)
{
	const FTransform Target(FRotator(0, Head->GetComponentRotation().Yaw, 0), Head->GetComponentLocation());
	const FVector OwnerLocation = GetOwner()->GetActorLocation();
	const FVector OwnerDelta = bVitalsAnchorReady ? OwnerLocation - VitalsOwnerLocation : FVector::ZeroVector;
	VitalsOwnerLocation = OwnerLocation;
	if (!bVitalsAnchorReady) { VitalsAnchorFrame = Target; bVitalsAnchorReady = true; }
	if (Settings->VitalsAnchorMode != 1 || VitalsDragHand != INDEX_NONE) return;
	// Translate with locomotion exactly. Smooth only room-scale/head motion;
	// smoothing world travel lets the HUD fall behind a running player.
	const FVector Transported = VitalsAnchorFrame.GetLocation() + OwnerDelta;
	const FVector Smoothed = FMath::VInterpTo(Transported, Target.GetLocation(), Dt, 6.f);
	VitalsAnchorFrame.SetLocation(Target.GetLocation() + (Smoothed - Target.GetLocation()).GetClampedToMaxSize(12.f));
	const float Difference = FMath::FindDeltaAngleDegrees(VitalsAnchorFrame.Rotator().Yaw, Target.Rotator().Yaw);
	if (FMath::Abs(Difference) > 25.f)
	{
		const float Yaw = VitalsAnchorFrame.Rotator().Yaw + (Difference - FMath::Sign(Difference)*25.f) * (1.f-FMath::Exp(-Dt*3.f));
		VitalsAnchorFrame.SetRotation(FRotator(0,Yaw,0).Quaternion());
	}
}

void UACEVRComponent::ToggleVitalsLock()
{
	EndVitalsDrag(false);
	Settings->bVitalsLocked = !Settings->bVitalsLocked;
	Settings->Persist();
}

void UACEVRComponent::BeginVitalsDrag(bool bLeft)
{
	if (!bTracking || Settings->bVitalsLocked || VitalsDragHand != INDEX_NONE) return;
	FVector2D Hit;
	if (!ProjectToViewPlane(GetVitalsAnchorTransform(), bLeft ? LeftAim.Get() : RightAim.Get(), Settings->VitalsViewOffset.X, Hit)) return;
	VitalsGrabOffset = Hit - FVector2D(Settings->VitalsViewOffset.Y, Settings->VitalsViewOffset.Z);
	VitalsDragHand = bLeft ? 0 : 1;
}

void UACEVRComponent::UpdateVitalsDrag()
{
	if (VitalsDragHand == INDEX_NONE) return;
	if (!bTracking || Settings->bVitalsLocked) { EndVitalsDrag(); return; }
	FVector2D Hit;
	if (!ProjectToViewPlane(GetVitalsAnchorTransform(), VitalsDragHand == 0 ? LeftAim.Get() : RightAim.Get(), Settings->VitalsViewOffset.X, Hit)) return;
	Hit -= VitalsGrabOffset;
	Settings->VitalsViewOffset.Y = Hit.X;
	Settings->VitalsViewOffset.Z = Hit.Y;
	Settings->Sanitize();
}

void UACEVRComponent::EndVitalsDrag(bool bSave)
{
	if (VitalsDragHand == INDEX_NONE) return;
	VitalsDragHand = INDEX_NONE;
	if (bSave) Settings->Persist();
}
