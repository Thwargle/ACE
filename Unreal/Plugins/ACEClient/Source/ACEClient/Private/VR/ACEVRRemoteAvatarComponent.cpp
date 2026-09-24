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
#include "ACECombatStance.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarRemoteBowStrings(TEXT("ace.VR.RemoteBowStrings"),1,TEXT("Render one lightweight bow string mesh for nearby VR archers."));

UACEVRRemoteAvatarComponent::UACEVRRemoteAvatarComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UACEVRRemoteAvatarComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Dt, Type, Tick);
	for(auto It=EquipmentRevisions.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
	if (BowString) BowString->SetVisibility(false);
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
	bApplied = true; PoseFlags = Pose.Flags; App->bVRPoseControlled = true; VisualPose=Pose;
	// Settled world actors suspend their ordinary movement tick. Tracking still
	// updates at render cadence, so apply the sampled root here with the limbs,
	// including when no new retail motion packet has woken the actor yet.
	if(Pose.Version==2 && !Entity->GetActorLocation().Equals(Pose.Root*Scale,.001f))
		Entity->SetActorLocation(Pose.Root*Scale);
	FTransform Tracked[3];
	for (int32 I = 0; I < 3; ++I)
		Tracked[I] = FTransform(Pose.Poses[I].GetRotation(), Entity->GetActorLocation() + Pose.Poses[I].GetLocation() * Scale);
	const FTransform& Head = Tracked[0];
	const FVector EyeLocal(0, -.08f * Scale, .17f * Scale);
	const FVector EyeBind = HeadBind.TransformPosition(EyeLocal);
	const float BodyScale = FMath::Clamp(Pose.EyeHeight * Scale / FMath::Max(50.f, float(EyeBind.Z)), .6f, 1.5f);
	FTransform Frame = ACEVRMath::BodyFromHead(Head, HeadBind, EyeLocal, BodyScale);
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
	Frame = App->UpdateVRUpperBody(Head, Tracked[1], Tracked[2], (Pose.Flags & 1u)!=0, (Pose.Flags & 2u)!=0, Dt);
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
		App->GetPartMesh(HandIndex-2)->SetWorldTransform(FTransform(Delta * Frame.GetRotation() * Upper.GetRotation(), Shoulder, FVector(BodyScale * Reach)));
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
	FACEWorldObject Weapon;
	auto* Viewer=GetWorld()->GetFirstPlayerController();
	if (CVarRemoteBowStrings.GetValueOnGameThread() && Pose.Version==2 && Pose.Ammo && Viewer && Viewer->PlayerCameraManager
		&& Client->GetWorldObject(Pose.Weapon,Weapon) && ACECombatStance::InferCombatStyle(Weapon)==0x10
		&& FVector::DistSquared(Viewer->PlayerCameraManager->GetCameraLocation(),Entity->GetActorLocation())<FMath::Square(1500.f))
	{
		if (!BowString)
		{
			BowString=NewObject<UProceduralMeshComponent>(GetOwner());GetOwner()->AddInstanceComponent(BowString);
			BowString->SetupAttachment(GetOwner()->GetRootComponent());BowString->SetUsingAbsoluteRotation(true);
			BowString->SetCollisionEnabled(ECollisionEnabled::NoCollision);BowString->SetCastShadow(false);BowString->RegisterComponent();
			BowString->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ACE/RuntimeMaterials/M_ACEVertexColor_v1.M_ACEVertexColor_v1")));
		}
		TArray<FVector> V,N;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
		const FVector Center=Pose.Poses[3].GetLocation()*Scale;
		const FVector Nock=(Pose.Flags&16u) ? Pose.Poses[(Pose.Flags&8u) ? 1 : 2].GetLocation()*Scale
			: Center+Pose.Poses[3].GetUnitAxis(EAxis::Z)*12.f;
		const FVector View=Viewer->PlayerCameraManager->GetCameraLocation()-Entity->GetActorLocation();
		for (float Sign:{-1.f,1.f})
		{
			const FVector Tip=Center+Pose.Poses[3].GetUnitAxis(EAxis::Y)*48.f*Sign;
			const FVector Side=FVector::CrossProduct(Nock-Tip,View-Tip).GetSafeNormal()*.15f;
			const int32 I=V.Num();V.Append({Tip-Side,Tip+Side,Nock+Side,Nock-Side});Indices.Append({I,I+1,I+2,I,I+2,I+3});
			for(int32 J=0;J<4;++J){N.Add(FVector::UpVector);UV.Add(FVector2D::ZeroVector);Colors.Add(FLinearColor(.7f,.65f,.5f));}
		}
		if (!BowString->GetProcMeshSection(0)) BowString->CreateMeshSection_LinearColor(0,V,Indices,N,UV,Colors,Tangents,false);
		else BowString->UpdateMeshSection_LinearColor(0,V,N,UV,Colors,Tangents);
		BowString->SetVisibility(!Entity->IsHidden());
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

bool UACEVRRemoteAvatarComponent::UpdateMissileAttachment(AACEWorldEntityActor* Item)
{
	if (!Item || !Item->Appearance) return false;
	auto* Client=GetWorld()->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
	FACEWorldObject Object;
	if (!Client || !Client->GetWorldObject(Item->GetACEGuid(),Object)) return false;
	const bool Ammo=(Object.CurrentWieldedLocation & ACEEquipMask::MissileAmmo)!=0;
	if (!bApplied || VisualPose.Version!=2)
	{
		if (EquipmentRevisions.Remove(Item)) { Item->Appearance->ApplyWorldObject(Object,Item->Appearance->WorldScale,false); Item->SetActorHiddenInGame(false); }
		return false;
	}
	const int32 Slot=Item->GetACEGuid()==VisualPose.Weapon ? 3 : Item->GetACEGuid()==VisualPose.Ammo ? 4 : -1;
	if (Slot<0)
	{
		if (Ammo) { Item->SetActorHiddenInGame(true); EquipmentRevisions.FindOrAdd(Item)=0; return true; }
		if (EquipmentRevisions.Remove(Item)) Item->Appearance->ApplyWorldObject(Object,Item->Appearance->WorldScale,false);
		return false;
	}
	const int32 Style=ACECombatStance::InferCombatStyle(Object);
	if (!EquipmentRevisions.Contains(Item) || EquipmentRevisions[Item]!=Item->Appearance->GetAppearanceRevision())
	{
		FACEWorldObject Copy=Object;
		if (Ammo || Style==0x10 || Style==0x20)
		{
			Copy.ParentGuid=Copy.ParentLocation=0;
			Copy.PlacementId=Ammo ? 52 : Style==0x20 ? 3 : 0;
			Copy.MotionTableId=Copy.DefaultAnimationId=0;
		}
		Item->Appearance->ApplyWorldObject(Copy,Item->Appearance->WorldScale,false);
		EquipmentRevisions.FindOrAdd(Item)=Item->Appearance->GetAppearanceRevision();
	}
	const FTransform& T=VisualPose.Poses[Slot];
	Item->SetActorLocationAndRotation(GetOwner()->GetActorLocation()+T.GetLocation()*Item->Appearance->WorldScale,T.GetRotation());
	Item->SetActorHiddenInGame(GetOwner()->IsHidden());
	return true;
}
