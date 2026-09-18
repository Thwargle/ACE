#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRMath.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "MotionControllerComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

FTransform UACEVRComponent::GetAvatarGrip(bool Left) const
{
	FTransform Pose = GetPhysicalGrip(Left);
	Pose.SetRotation(Pose.GetRotation() * FRotator(Settings->HandPitch,0,0).Quaternion());
	const auto Weapon = EquippedWeapon();
	if (GetCombatMode() == ACECombatMode::Melee && (Weapon.CurrentWieldedLocation & ACEEquipMask::TwoHanded)
		&& Left != Settings->bLeftHanded)
	{
		// Solve the support arm onto the shaft. Its physical controller never
		// rotates the weapon or contributes velocity to the dominant-hand strike.
		const FTransform Dominant = GetPhysicalGrip(Settings->bLeftHanded);
		Pose.SetRotation(Dominant.GetRotation() * FRotator(Settings->HandPitch,0,0).Quaternion());
		Pose.SetLocation(Dominant.GetLocation() + Pose.GetUnitAxis(EAxis::X) * 22.f);
	}
	return Pose;
}

void UACEVRComponent::UpdateArms(float Dt)
{
	auto* App = GetOwner()->FindComponentByClass<UACECharacterAppearanceComponent>();
	auto* Dat = GetWorld()->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
	const bool Visible = bTracking && !PC->bEnterWorldLoading && !PC->bWorldRevealActive;
	const FRotator BodyYaw(0.f, Head->GetComponentRotation().Yaw, 0.f);
	const FTransform BodyFrame(BodyYaw, Head->GetComponentLocation());
	for (UStaticMeshComponent* M : FallbackArms) M->SetVisibility(false);
	// Validate the retail human hand sockets: chest=9, arms=10..15, head=16.
	// The setup also has accessory parts after 16, which must obey body visibility.
	int32 LeftHand = INDEX_NONE, RightHand = INDEX_NONE; FTransform LeftHold, RightHold;
	const bool Human = App && App->HasAppearance() && Dat
		&& Dat->GetHoldingLocation(App->GetSetupId(), 8, LeftHand, LeftHold, PC->WorldScale)
		&& Dat->GetHoldingLocation(App->GetSetupId(), 1, RightHand, RightHold, PC->WorldScale)
		&& LeftHand == 12 && RightHand == 15 && App->GetPartMesh(16);
	if (App) { App->bVRPoseControlled = Human; App->SetPartsCastShadow(true, false); }
	if (Human)
	{
		App->UpdateVRLowerBody(Dt);
		FTransform HeadBind;
		if (App->GetMeshRoot() && App->GetPartBindTransform(16, HeadBind))
		{
			// Setup faces +Y. Fit the authored eye height once per recenter and
			// keep the torso under the HMD even when menus pause network movement.
			// Both retail human setups use head 0100005A: its pivot is near
			// the neck (1.587m), crown +.231m, eyes about +.17m and face -Y.
			// An 8cm world-Z offset put the viewpoint below the face in armor.
			const FVector EyeBind = HeadBind.TransformPosition(FVector(0, -.08f, .17f) * PC->WorldScale);
			const float Scale = FMath::Clamp(AvatarEyeHeight / FMath::Max(50.f, float(EyeBind.Z)), .6f, 1.5f);
			App->GetMeshRoot()->SetWorldTransform(ACEVRMath::BodyFromHead(Head->GetComponentTransform(),
				HeadBind, FVector(0,-.08f,.17f)*PC->WorldScale, Scale));
		}
	}
	if (App)
		for (int32 I = 0; I < App->GetPartCount(); ++I)
		{
			if (auto* Part = Cast<UPrimitiveComponent>(App->GetPartMesh(I)))
			{
				const bool Arm = Human && ((I >= 10 && I <= 15) || I == 27 || I == 28);
				const bool HeadPart = Human && (I == 16 || I == 21 || I == 22);
				Part->SetCastHiddenShadow(true);
				Part->SetOwnerNoSee(HeadPart || (!Settings->bShowBody && !Arm));
				Part->SetVisibility(Visible && Human, false);
				Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				if (Human && I >= 9)
				{
					// Tracked parts receive their final transform below. Resetting them to
					// bind first also moves every attached weapon/light twice per frame.
					const bool LeftArm = (I >= 10 && I <= 12) || I == 27;
					const bool RightArm = (I >= 13 && I <= 15) || I == 28;
					const bool TrackedArm = Visible && ((LeftArm && LeftGrip->IsTracked()) || (RightArm && RightGrip->IsTracked()));
					if (!HeadPart && !TrackedArm)
					{
						FTransform Bind; if (App->GetPartBindTransform(I, Bind)) Part->SetRelativeTransform(Bind);
					}
				}
			}
		}
	if (Human)
	{
		// Hidden head/helmet parts still cast the complete, tracked body shadow.
		FTransform HeadBind;
		if (App->GetPartBindTransform(16, HeadBind))
		{
			const FVector BodyScale = App->GetMeshRoot()->GetComponentScale();
			const FQuat Rotation = Head->GetComponentQuat() * FRotator(0,-90,0).Quaternion() * HeadBind.GetRotation();
			const FVector EyeLocal = FVector(0,-.08f,.17f) * PC->WorldScale * BodyScale;
			const FTransform HeadWorld(Rotation, Head->GetComponentLocation()-Rotation.RotateVector(EyeLocal), BodyScale);
			for (int32 I : {16,21,22})
			{
				FTransform Bind;
				if (auto* Part = App->GetPartMesh(I); Part && App->GetPartBindTransform(I, Bind))
					Part->SetWorldTransform(Bind.GetRelativeTransform(HeadBind) * HeadWorld);
			}
		}
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool Left = Side == 0;
		auto* Grip = Left ? LeftGrip.Get() : RightGrip.Get();
		const FTransform GripPose = GetAvatarGrip(Left);
		const bool Tracked = Visible && Grip->IsTracked();
		const int32 HandPart = Left ? LeftHand : RightHand;
		const FTransform Holding = Left ? LeftHold : RightHold;
		const int32 UpperPart = HandPart - 2, LowerPart = HandPart - 1;
		FTransform UpperBind, LowerBind, HandBind;
		// Human DAT forearm frames sit in the shaft, NOT at the elbow. Both
		// male/female skin meshes run from z=+0.14m (elbow) to -0.17m (wrist).
		// Clothing replaces geometry in this same frame, so use the anatomical
		// landmark rather than treating an armor bounding box as a joint.
		const FVector ElbowInForearm(0, 0, .14f * PC->WorldScale);
		FVector ElbowBind = FVector::ZeroVector;
		const bool HasArm = Human && UpperPart >= 0 && App->GetPartBindTransform(UpperPart, UpperBind)
			&& App->GetPartBindTransform(LowerPart, LowerBind) && App->GetPartBindTransform(HandPart, HandBind);
		if (HasArm)
			for (int32 I : {UpperPart, LowerPart, HandPart}) if (auto* M = App->GetPartMesh(I)) M->SetVisibility(Tracked, false);
		if (!Tracked) continue;
		FVector Shoulder = BodyFrame.TransformPosition(FVector(-8.f, Left ? -18.f : 18.f, -25.f));
		FVector Hand = GripPose.GetLocation();
		FQuat HandRotation = GripPose.GetRotation();
		const FVector Pole = BodyFrame.TransformPosition(FVector(-20.f, Left ? -48.f : 48.f, -65.f));
		float UpperLength = 30.f, LowerLength = 27.f;
		FTransform MeshFrame = FTransform::Identity;
		if (HasArm)
		{
			MeshFrame = App->GetMeshRoot()->GetComponentTransform();
			Shoulder = MeshFrame.TransformPosition(UpperBind.GetLocation());
			// OpenXR grip +X (Unreal) runs through the grasp from little finger
			// to thumb. Align the held item's shaft (+Z), then invert the actual
			// DAT holding socket to recover the wrist instead of guessing Euler offsets.
			// DAT fingers extend along hand -Z. The palm roll must put them toward
			// grip -Z (away from the wearer), while preserving the held shaft axis.
			const FQuat HeldRotation = FRotationMatrix::MakeFromZY(GripPose.GetUnitAxis(EAxis::X), GripPose.GetUnitAxis(EAxis::Z)).ToQuat();
			HandRotation = HeldRotation * Holding.GetRotation().Inverse();
			Hand -= HandRotation.RotateVector(Holding.GetLocation() * MeshFrame.GetScale3D());
			ElbowBind = LowerBind.TransformPosition(ElbowInForearm);
			UpperLength = FVector::Distance(MeshFrame.TransformPosition(UpperBind.GetLocation()), MeshFrame.TransformPosition(ElbowBind));
			LowerLength = FVector::Distance(MeshFrame.TransformPosition(ElbowBind), MeshFrame.TransformPosition(HandBind.GetLocation()));
		}
		FVector Elbow, Wrist;
		const float ReachScale = FMath::Max(1.f, FVector::Distance(Shoulder, Hand) / FMath::Max(1.f, UpperLength + LowerLength - .01f));
		UpperLength *= ReachScale; LowerLength *= ReachScale;
		ACEVRMath::SolveArm(Shoulder, Hand, Pole, UpperLength, LowerLength, Elbow, Wrist);
		if (HasArm)
		{
			auto Limb = [&](int32 Index, const FTransform& Bind, FVector BindEnd, FVector Start, FVector End)
			{
				auto* Part = App->GetPartMesh(Index); if (!Part) return;
				const FVector Original = MeshFrame.TransformVector(BindEnd - Bind.GetLocation());
				const FQuat Delta = FQuat::FindBetweenNormals(Original.GetSafeNormal(), (End - Start).GetSafeNormal());
				Part->SetWorldTransform(FTransform(Delta * MeshFrame.GetRotation() * Bind.GetRotation(),
					Start, MeshFrame.GetScale3D() * ReachScale));
			};
			Limb(UpperPart, UpperBind, ElbowBind, Shoulder, Elbow);
			// Scale both arm segments when tracking exceeds the authored reach, keeping
			// the visible elbow/wrist joined and the palm at the actual controller.
			// Preserve the hand's authored relationship to the forearm, then bend
			// the forearm toward the wrist. A shortest-arc solve from the bind pose
			// alone loses palm roll and leaves the forearm facing backward.
			const FVector LowerAxis = LowerBind.GetRotation().UnrotateVector(HandBind.GetLocation() - ElbowBind).GetSafeNormal();
			const FQuat ForearmRoll = HandRotation * HandBind.GetRotation().Inverse() * LowerBind.GetRotation();
			const FQuat LocalFrame = FRotationMatrix::MakeFromZY(LowerAxis, FVector::RightVector).ToQuat();
			const FQuat WorldFrame = FRotationMatrix::MakeFromZY((Hand - Elbow).GetSafeNormal(), ForearmRoll.RotateVector(FVector::RightVector)).ToQuat();
			const FQuat ForearmRotation = WorldFrame * LocalFrame.Inverse();
			const FVector LowerScale = MeshFrame.GetScale3D() * ReachScale;
			auto* Forearm = App->GetPartMesh(LowerPart);
			Forearm->SetWorldTransform(FTransform(ForearmRotation,
				Elbow - ForearmRotation.RotateVector(ElbowInForearm * LowerScale), LowerScale));
			if (auto* Part = App->GetPartMesh(HandPart))
				Part->SetWorldTransform(FTransform(HandRotation, Hand, HandBind.GetScale3D() * MeshFrame.GetScale3D()));
			const int32 Accessory = Left ? 27 : 28;
			FTransform AccessoryBind;
			if (auto* Part = App->GetPartMesh(Accessory); Part && App->GetPartBindTransform(Accessory, AccessoryBind))
			{
				Part->SetWorldTransform(AccessoryBind.GetRelativeTransform(UpperBind) * App->GetPartMesh(UpperPart)->GetComponentTransform());
				Part->SetVisibility(Tracked, false);
			}
		}
		else
		{
			for (int32 I = 0; I < 2; ++I)
			{
				auto* Limb = FallbackArms[Side * 3 + I].Get();
				const FVector A = I == 0 ? Shoulder : Elbow, B = I == 0 ? Elbow : Hand;
				Limb->SetWorldLocation((A + B) * .5f); Limb->SetWorldRotation(FRotationMatrix::MakeFromZ(B - A).Rotator());
				Limb->SetWorldScale3D(FVector(.065f, .065f, FVector::Dist(A, B) / 100.f)); Limb->SetVisibility(true);
			}
			auto* Palm = FallbackArms[Side * 3 + 2].Get(); Palm->SetWorldLocationAndRotation(Hand, GripPose.GetRotation());
			Palm->SetWorldScale3D(FVector(.12f, .075f, .045f)); Palm->SetVisibility(true);
		}
	}
}
