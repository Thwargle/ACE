#include "VR/ACEVRRemoteAvatarComponent.h"
#include "VR/ACEVRMath.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACESession.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "ProceduralMeshComponent.h"

UACEVRRemoteAvatarComponent::UACEVRRemoteAvatarComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UACEVRRemoteAvatarComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Dt, Type, Tick);
	auto* Entity = Cast<AACEWorldEntityActor>(GetOwner());
	auto* App = GetOwner()->FindComponentByClass<UACECharacterAppearanceComponent>();
	auto* GI = GetWorld()->GetGameInstance();
	auto* Client = GI ? GI->GetSubsystem<UACEClientSubsystem>() : nullptr;
	auto* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Entity || !App || !App->GetMeshRoot() || !Client || !Dat) return;
	FACEVRPose Pose;
	const auto Session = Client->GetSession();
	int32 Left = -1, Right = -1; FTransform LeftHold, RightHold, HeadBind;
	const float Scale = App->WorldScale;
	const bool Valid = Session && Session->GetVRPose(Entity->GetACEGuid(), Pose)
		&& Dat->GetHoldingLocation(App->GetSetupId(), 8, Left, LeftHold, Scale)
		&& Dat->GetHoldingLocation(App->GetSetupId(), 1, Right, RightHold, Scale)
		&& Left == 12 && Right == 15 && App->GetPartBindTransform(16, HeadBind);
	if (!Valid)
	{
		if (bApplied)
		{
			App->bVRPoseControlled = false; App->ResetVRLowerBody();
			App->GetMeshRoot()->SetRelativeTransform(RetailRoot);
			for (int32 I = 0; I < App->GetPartCount(); ++I)
			{ FTransform Bind; if (auto* Part = App->GetPartMesh(I); Part && App->GetPartBindTransform(I, Bind)) Part->SetRelativeTransform(Bind); }
			bApplied = false;
		}
		return;
	}
	if (!bApplied) RetailRoot = App->GetMeshRoot()->GetRelativeTransform();
	bApplied = true; PoseFlags = Pose.Flags; App->bVRPoseControlled = true;
	FTransform Tracked[3];
	for (int32 I = 0; I < 3; ++I)
		Tracked[I] = FTransform(Pose.Poses[I].GetRotation(), Entity->GetActorLocation() + Pose.Poses[I].GetLocation() * Scale);
	const FTransform& Head = Tracked[0];
	const FQuat Facing = FRotator(0, Head.Rotator().Yaw - 90.f, 0).Quaternion();
	const FVector EyeLocal(0, -.08f * Scale, .17f * Scale);
	const FVector EyeBind = HeadBind.TransformPosition(EyeLocal);
	const float BodyScale = FMath::Clamp(Pose.EyeHeight * Scale / FMath::Max(50.f, float(EyeBind.Z)), .6f, 1.5f);
	const FTransform Frame = ACEVRMath::BodyFromHead(Head, HeadBind, EyeLocal, BodyScale);
	App->GetMeshRoot()->SetWorldTransform(Frame);
	App->UpdateVRLowerBody(Dt);
	for (int32 I = 9; I < App->GetPartCount(); ++I)
	{
		const bool HeadPart = I == 16 || I == 21 || I == 22;
		const bool TrackedArm = (((I >= 10 && I <= 12) || I == 27) && (Pose.Flags & 1u))
			|| (((I >= 13 && I <= 15) || I == 28) && (Pose.Flags & 2u));
		if (!HeadPart && !TrackedArm)
		{ FTransform Bind; if (auto* Part = App->GetPartMesh(I); Part && App->GetPartBindTransform(I, Bind)) Part->SetRelativeTransform(Bind); }
	}
	const FQuat HeadRotation = Head.GetRotation() * FRotator(0,-90,0).Quaternion() * HeadBind.GetRotation();
	for (int32 I : {16, 21, 22})
		if (auto* Part = App->GetPartMesh(I))
		{
			FTransform Bind; if (!App->GetPartBindTransform(I, Bind)) continue;
			const FTransform HeadWorld(HeadRotation, Head.GetLocation() - HeadRotation.RotateVector(EyeLocal * BodyScale), FVector(BodyScale));
			Part->SetWorldTransform(Bind.GetRelativeTransform(HeadBind) * HeadWorld);
		}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!(Pose.Flags & (1u << Side))) continue;
		const int32 HandIndex = Side == 0 ? Left : Right;
		const FTransform Holding = Side == 0 ? LeftHold : RightHold;
		const FTransform Grip = Tracked[Side + 1];
		FTransform Upper, Lower, HandBind;
		if (!App->GetPartBindTransform(HandIndex-2, Upper) || !App->GetPartBindTransform(HandIndex-1, Lower)
			|| !App->GetPartBindTransform(HandIndex, HandBind)) continue;
		const FQuat Held = FRotationMatrix::MakeFromZY(Grip.GetUnitAxis(EAxis::X), Grip.GetUnitAxis(EAxis::Z)).ToQuat();
		const FQuat HandRot = Held * Holding.GetRotation().Inverse();
		const FVector Hand = Grip.GetLocation() - HandRot.RotateVector(Holding.GetLocation() * BodyScale);
		const FVector ElbowLocal(0,0,.14f * Scale);
		const FVector ElbowBind = Lower.TransformPosition(ElbowLocal);
		const FVector Shoulder = Frame.TransformPosition(Upper.GetLocation());
		float UpperLength = FVector::Distance(Shoulder, Frame.TransformPosition(ElbowBind));
		float LowerLength = FVector::Distance(Frame.TransformPosition(ElbowBind), Frame.TransformPosition(HandBind.GetLocation()));
		const float Reach = FMath::Max(1.f, FVector::Distance(Shoulder, Hand) / FMath::Max(1.f, UpperLength + LowerLength - .01f));
		UpperLength *= Reach; LowerLength *= Reach;
		const FTransform BodyYaw(FRotator(0,Head.Rotator().Yaw,0), Head.GetLocation());
		const FVector Pole = BodyYaw.TransformPosition(FVector(-20.f, Side == 0 ? -48.f : 48.f, -65.f));
		FVector Elbow, Wrist;
		ACEVRMath::SolveArm(Shoulder, Hand, Pole, UpperLength, LowerLength, Elbow, Wrist);
		const FVector Original = Frame.TransformVector(ElbowBind - Upper.GetLocation());
		const FQuat Delta = FQuat::FindBetweenNormals(Original.GetSafeNormal(), (Elbow-Shoulder).GetSafeNormal());
		App->GetPartMesh(HandIndex-2)->SetWorldTransform(FTransform(Delta * Facing * Upper.GetRotation(), Shoulder, FVector(BodyScale * Reach)));
		const FVector Axis = Lower.GetRotation().UnrotateVector(HandBind.GetLocation()-ElbowBind).GetSafeNormal();
		const FQuat Roll = HandRot * HandBind.GetRotation().Inverse() * Lower.GetRotation();
		const FQuat LowerRot = FRotationMatrix::MakeFromZY((Hand-Elbow).GetSafeNormal(), Roll.RotateVector(FVector::RightVector)).ToQuat()
			* FRotationMatrix::MakeFromZY(Axis, FVector::RightVector).ToQuat().Inverse();
		App->GetPartMesh(HandIndex-1)->SetWorldTransform(FTransform(LowerRot, Elbow-LowerRot.RotateVector(ElbowLocal*BodyScale*Reach), FVector(BodyScale*Reach)));
		App->GetPartMesh(HandIndex)->SetWorldTransform(FTransform(HandRot, Hand, FVector(BodyScale)));
		const int32 Accessory = Side == 0 ? 27 : 28;
		FTransform Bind;
		if (auto* Part=App->GetPartMesh(Accessory); Part && App->GetPartBindTransform(Accessory, Bind))
			Part->SetWorldTransform(Bind.GetRelativeTransform(Upper)*App->GetPartMesh(HandIndex-2)->GetComponentTransform());
	}
}

USceneComponent* UACEVRRemoteAvatarComponent::GetHeldAnchor(int32 ParentLocation, bool TwoHanded) const
{
	if (!bApplied) return nullptr;
	bool Left;
	if (TwoHanded || ParentLocation == 1) Left = (PoseFlags & 8u) != 0;
	else if (ParentLocation == 2 || ParentLocation == 8 || ParentLocation == 9 || ParentLocation == 3) Left = (PoseFlags & 8u) == 0;
	else return nullptr;
	auto* App = GetOwner()->FindComponentByClass<UACECharacterAppearanceComponent>();
	return App ? App->GetPartMesh(Left ? 12 : 15) : nullptr;
}
