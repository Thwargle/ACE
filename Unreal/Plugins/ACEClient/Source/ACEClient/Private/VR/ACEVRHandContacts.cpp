#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "MotionControllerComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"

FTransform UACEVRComponent::GetPhysicalGrip(bool Left) const
{
	const auto& Contact = ContactHands[Left ? 0 : 1];
	return Contact.bAvailable ? Contact.State.Pose : (Left ? LeftGrip.Get() : RightGrip.Get())->GetComponentTransform();
}

FTransform UACEVRComponent::GetPhysicalAim(bool Left) const
{
	const auto* Aim = Left ? LeftAim.Get() : RightAim.Get();
	const auto* Grip = Left ? LeftGrip.Get() : RightGrip.Get();
	return Aim->GetComponentTransform().GetRelativeTransform(Grip->GetComponentTransform()) * GetPhysicalGrip(Left);
}

void UACEVRComponent::ResetHandContacts()
{
	for (auto& Hand : ContactHands) { Hand.State.Reset(); Hand.bAvailable = false; Hand.ShapeKey = 0; }
	bPreviousContactBlade = false;
}

void UACEVRComponent::UpdateHandContacts(float Dt)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ACEVRHandContact);
	if (!bTracking || !Client || Client->GetSessionState() != EACESessionState::InWorld
		|| PC->bEnterWorldLoading || PC->bWorldRevealActive)
	{ ResetHandContacts(); return; }
	TArray<AActor*> Attached; GetOwner()->GetAttachedActors(Attached, true, true);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VRHandContact), false, GetOwner());
	Query.bFindInitialOverlaps = true;
	for (auto* Actor : Attached) Query.AddIgnoredActor(Actor);
	if (PresentationActor) Query.AddIgnoredActor(PresentationActor);
	if (AmmoActor) Query.AddIgnoredActor(AmmoActor);
	TArray<AACEWorldEntityActor*> Held[2];
	const auto Launcher = EquippedMissileWeapon();
	for (auto* Actor : Attached)
	{
		auto* Item = Cast<AACEWorldEntityActor>(Actor);
		if (!Item || !Item->Appearance || !Item->Appearance->HasAppearance()) continue;
		FACEWorldObject Obj;
		if (!Client->GetWorldObject(Item->GetACEGuid(), Obj) || (Obj.CurrentWieldedLocation & ACEEquipMask::MissileAmmo)) continue;
		bool Left;
		if (Item->GetACEGuid() == Launcher.Guid && IsAmmoLauncher()) Left = !Settings->bLeftHanded;
		else if ((Obj.CurrentWieldedLocation & ACEEquipMask::TwoHanded) || Obj.ParentLocation == 1) Left = Settings->bLeftHanded;
		else if (Obj.ParentLocation == 2 || Obj.ParentLocation == 3 || Obj.ParentLocation == 8 || Obj.ParentLocation == 9) Left = !Settings->bLeftHanded;
		else continue;
		Held[Left ? 0 : 1].Add(Item);
	}
	const auto* Capsule = GetOwner()->FindComponentByClass<UCapsuleComponent>();
	const FVector SafeOrigin = GetOwner()->GetActorLocation() - FVector(0,0,Capsule->GetScaledCapsuleHalfHeight())
		+ FVector(0,0,FMath::Min(AvatarEyeHeight*.65f,120.f));
	// Locomotion uses generous retail cylinders. Hands instead contact visible
	// NPC/creature/item triangles, including animated rigid body parts. Keep
	// world BSP collision for buildings and terrain. Broad phase is local only.
	ContactTriangles.Reset();
	FBox HandReach(ForceInit);
	for (int32 Side=0;Side<2;++Side)
	{
		const auto* Grip=Side==0 ? LeftGrip.Get() : RightGrip.Get();
		if (!Grip->IsTracked()) continue;
		const auto& Hand=ContactHands[Side];
		float Radius=Hand.Shapes.IsEmpty() ? 220.f : 20.f;
		for (const auto& Shape:Hand.Shapes) Radius=FMath::Max(Radius,float(Shape.Local.GetLocation().Size()+Shape.Extent.Size()+20.f));
		FBox SweepBounds(ForceInit); SweepBounds+=Grip->GetComponentLocation();
		SweepBounds+=Hand.bAvailable ? Hand.State.Pose.GetLocation() : SafeOrigin;
		// Reacquisition/recovery can start at the torso. Keep that entire path.
		SweepBounds+=SafeOrigin;
		HandReach+=SweepBounds.ExpandBy(Radius);
	}
	TArray<FOverlapResult> Nearby;
	GetWorld()->OverlapMultiByObjectType(Nearby,SafeOrigin,FQuat::Identity,
		FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeSphere(350.f),Query);
	TSet<AActor*> Seen;
	const bool InCombat = GetCombatMode() != ACECombatMode::NonCombat;
	auto IsCombatBody = [&](const AACEWorldEntityActor* Entity)
	{
		if (Entity->IsStandingCreatureOrPlayer()) return true;
		FACEWorldObject Object;
		return Client->GetWorldObject(Entity->GetACEGuid(), Object) && Object.IsGiveOrCreatureTarget();
	};
	auto PassThroughInCombat = [&](const AACEWorldEntityActor* Entity)
	{
		if (!InCombat) return false;
		for (const AActor* Parent=Entity; Parent; Parent=Parent->GetAttachParentActor())
			if (const auto* Body=Cast<AACEWorldEntityActor>(Parent); Body && IsCombatBody(Body)) return true;
		// Equipment can receive its profile before its visual attachment resolves.
		FACEWorldObject Parent;
		return (Client->GetWorldObject(Entity->GetWielderId(), Parent) && Parent.IsGiveOrCreatureTarget())
			|| (Client->GetWorldObject(Entity->GetParentGuid(), Parent) && Parent.IsGiveOrCreatureTarget());
	};
	for(const auto& Hit:Nearby)
	{
		auto* Entity=Cast<AACEWorldEntityActor>(Hit.GetActor());
		if(!Entity || Seen.Contains(Entity)) continue;
		Seen.Add(Entity);
		if (PassThroughInCombat(Entity))
		{
			// Only the visual hand constraint ignores combat bodies. Keep their
			// collision enabled for locomotion, selection and tracked strike tests.
			Query.AddIgnoredActor(Entity);
			TArray<AActor*> Equipment; Entity->GetAttachedActors(Equipment, true, true);
			for (auto* Item : Equipment) { Query.AddIgnoredActor(Item); Seen.Add(Item); }
			continue;
		}
		if(Entity->IsHidden() || !Entity->Appearance) continue;
		bool HasGeometry=false;
		for(int32 P=0;P<Entity->Appearance->GetPartCount();++P)
		{
			auto* Part=Cast<UProceduralMeshComponent>(Entity->Appearance->GetPartMesh(P));
			if(!Part || !Part->IsVisible()) continue;
			const FTransform Transform=Part->GetComponentTransform();
			const FBox LocalReach=HandReach.TransformBy(Transform.Inverse());
			for(int32 S=0;S<Part->GetNumSections();++S)
			{
				const auto* Section=Part->GetProcMeshSection(S); if(!Section || !Section->bSectionVisible) continue;
				HasGeometry |= Section->ProcIndexBuffer.Num()>=3;
				if (!LocalReach.Intersect(Section->SectionLocalBox)) continue;
				for(int32 I=0;I+2<Section->ProcIndexBuffer.Num();I+=3)
				{
					const FVector A=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I]].Position;
					const FVector B=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+1]].Position;
					const FVector C=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+2]].Position;
					FBox LocalBounds(A,A); LocalBounds+=B; LocalBounds+=C;
					if (!LocalReach.Intersect(LocalBounds)) continue;
					FACEVRContactTriangle T;
					T.ObjectGuid=Entity->GetACEGuid();
					T.A=Transform.TransformPosition(A);
					T.B=Transform.TransformPosition(B);
					T.C=Transform.TransformPosition(C);
					T.Bounds=FBox(T.A,T.A);T.Bounds+=T.B;T.Bounds+=T.C;
					ContactTriangles.Add(T);
				}
			}
		}
		if(HasGeometry) Query.AddIgnoredActor(Entity);
	}
	for (int32 Side=0; Side<2; ++Side)
	{
		const bool Left = Side == 0;
		auto& Hand = ContactHands[Side];
		const auto* Grip = Left ? LeftGrip.Get() : RightGrip.Get();
		if (!Grip->IsTracked()) { Hand.State.Reset(); Hand.bAvailable = false; continue; }
		const FTransform Reference = GetPhysicalGrip(Left);
		uint32 Key = HashCombine(GetTypeHash(Settings->HandPitch), GetTypeHash(Settings->bLeftHanded));
		Key = HashCombine(Key, GetTypeHash(GetCombatMode()));
		for (auto* Item : Held[Side])
			Key = HashCombine(Key, HashCombine(GetTypeHash(Item->GetACEGuid()), GetTypeHash(Item->Appearance->GetAppearanceRevision())));
		if (Hand.Shapes.IsEmpty() || Hand.ShapeKey != Key)
		{
			Hand.ShapeKey = Key; Hand.Shapes.Reset();
			FACEVRContactShape Palm;
			Palm.Local = FTransform(FRotator(Settings->HandPitch,0,0), FVector(0,0,-3));
			Palm.Extent = FVector(5,6,8); Hand.Shapes.Add(Palm);
			for (auto* Item : Held[Side])
				for (int32 P=0; P<Item->Appearance->GetPartCount() && Hand.Shapes.Num()<13; ++P)
				{
					auto* Part = Cast<UProceduralMeshComponent>(Item->Appearance->GetPartMesh(P));
					if (!Part) continue;
					FBox Bounds(ForceInit);
					// Only rendered geometry: DAT collision shells and particle bounds
					// must never turn a small held item into an enormous contact box.
					for (int32 S=0; S<Part->GetNumSections(); ++S)
						if (const auto* Section=Part->GetProcMeshSection(S); Section && Section->bSectionVisible)
							Bounds += Section->SectionLocalBox;
					if (!Bounds.IsValid) continue;
					const FVector Center = Part->GetComponentTransform().TransformPosition(Bounds.GetCenter());
					FACEVRContactShape Shape;
					Shape.Local = FTransform(Part->GetComponentQuat(), Center).GetRelativeTransform(Reference);
					Shape.Local.SetScale3D(FVector::OneVector);
					Shape.Extent = (Bounds.GetExtent() * Part->GetComponentScale().GetAbs()).ComponentMax(FVector(.5f)) + FVector(.75f);
					if (Shape.Local.GetLocation().Size()<220.f && Shape.Extent.GetMax()<180.f) Hand.Shapes.Add(Shape);
				}
		}
		const bool WasContact = Hand.State.bContact;
		ACEVRHandCollision::Move(GetWorld(), Hand.State, Grip->GetComponentTransform(), SafeOrigin, Hand.Shapes, Query, &ContactTriangles);
		Hand.bAvailable = true;
		const double Now = GetWorld()->GetTimeSeconds();
		const float Pressure = FVector::Distance(Hand.State.Pose.GetLocation(), Grip->GetComponentLocation());
		if (Hand.State.bContact && (!WasContact || Now-Hand.LastPulse>.18))
		{
			Pulse(Left,FMath::Clamp(.15f+Pressure*.015f,.15f,.65f)); Hand.LastPulse=Now;
		}
	}
}

void UACEVRComponent::UpdateTwoHandUse(float Dt)
{
	if (!bActive || !bTracking || IsMenuOpen() || IsInputBlocked() || bSpellWheelOpen
		|| GetCombatMode()!=ACECombatMode::NonCombat || !LeftGrip->IsTracked() || !RightGrip->IsTracked())
	{
		TouchUseGuid=0; TouchUseHold=0; bTouchUseArmed=false; bTouchUsePrevious=false; return;
	}
	const FVector Raw[2]={LeftGrip->GetComponentLocation(),RightGrip->GetComponentLocation()};
	const FVector Body=GetOwner()->GetActorLocation();
	const bool Still=bTouchUsePrevious && Dt>0 && Dt<.15f && MoveStick.Size()<.2f
		&& FVector::Distance(Body,TouchUsePreviousBody)/Dt<25.f
		&& FVector::Distance(Raw[0],TouchUsePreviousHands[0])/Dt<45.f
		&& FVector::Distance(Raw[1],TouchUsePreviousHands[1])/Dt<45.f;
	TouchUsePreviousBody=Body; TouchUsePreviousHands[0]=Raw[0]; TouchUsePreviousHands[1]=Raw[1]; bTouchUsePrevious=true;
	// Intent comes from the tracked hands, not their collision-constrained
	// visual meshes. Hold both hands forward while looking at a usable object.
	const FVector Mid=(Raw[0]+Raw[1])*.5f;
	const FVector Forward=FVector(Head->GetForwardVector().X,Head->GetForwardVector().Y,0).GetSafeNormal();
	auto Raised=[&](const FVector& P) { const FVector D=P-Head->GetComponentLocation();
		return FVector::DotProduct(D,Forward)>25 && D.Z>-75 && D.Size()<140; };
	const bool InFront=Raised(Raw[0]) && Raised(Raw[1])
		&& FVector::Distance(Raw[0],Raw[1])>12 && FVector::Distance(Raw[0],Raw[1])<90;
	// Withdraw both hands before a second use. Holding or brushing past cannot
	// repeatedly toggle a door, even if its collision moves during opening.
	if (!Raised(Raw[0]) && !Raised(Raw[1])) bTouchUseArmed=true;
	int32 Candidate=0;
	float Best=0.8f;
	if (Still && InFront && bTouchUseArmed && !Client->IsUseBusy())
		for (TActorIterator<AACEWorldEntityActor> It(GetWorld());It;++It)
		{
			FACEWorldObject Object;
			if (!Client->GetWorldObject(It->GetACEGuid(),Object) || Object.ContainerId || Object.WielderId || Object.ParentGuid
				|| Object.bIsPlayer || Object.IsWorldLootable() || (Object.IsAttackable() && !Object.IsVendor())) continue;
			if (!(Object.IsDoor() || Object.IsLifeStone() || Object.IsOpenable() || Object.IsVendor()
				|| ACEItemUseable::IsSourceUsable(Object.ItemUseable))) continue;
			const auto* Capsule=GetOwner()->FindComponentByClass<UCapsuleComponent>();
			const FVector Feet=Body-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
			if (PC->GetUseCylinderDistanceCm(Object,Feet,Object.Position.ToUnrealLocation(PC->WorldScale))
				>Object.UseRadius*PC->WorldScale+5.f) continue;
			FBox Bounds(ForceInit);
			if (!It->Appearance || !It->Appearance->GetVisualWorldBounds(Bounds)) continue;
			const FVector Closest=Bounds.GetClosestPointTo(Mid);
			const FVector Towards=(Bounds.GetCenter()-Head->GetComponentLocation()).GetSafeNormal();
			const float Score=FVector::DotProduct(Towards,Head->GetForwardVector());
			if (Score<=Best) continue;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(VRTwoHandUse),false,GetOwner()); Query.AddIgnoredActor(*It);
			TArray<AActor*> Children;GetOwner()->GetAttachedActors(Children,true,true);for(auto* Child:Children)Query.AddIgnoredActor(Child);
			FHitResult Block;
			if (GetWorld()->LineTraceSingleByChannel(Block,Head->GetComponentLocation(),Closest,ECC_Camera,Query)) continue;
			Candidate=Object.Guid; Best=Score;
		}
	if (!Candidate || Candidate!=TouchUseGuid) TouchUseHold=0;
	TouchUseGuid=Candidate;
	if (Candidate && (TouchUseHold+=Dt)>=.65f)
	{
		bTouchUseArmed=false; TouchUseHold=0;
		// Send the ordinary use action within retail use range. Do not start a desktop
		// automatic approach or change the player's selection/combat target.
		Client->SendUseItem(Candidate); Pulse(true,.45f); Pulse(false,.45f);
	}
}
