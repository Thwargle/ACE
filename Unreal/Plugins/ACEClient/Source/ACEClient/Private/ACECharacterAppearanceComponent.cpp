#include "ACECharacterAppearanceComponent.h"
#include "ACEProfiling.h"
#include "ACELoadingScreenActor.h"
#include "ACERegionSceneryActor.h"
#include "ACEDatSubsystem.h"
#include "ACEScriptComponent.h"
#include "ACEWorldEntityActor.h"
#include "ProceduralMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Misc/App.h"

static bool ACEIsMagicCastCommand(uint32 Command)
{
	return (Command >= 0x4000002Bu && Command <= 0x40000039u)
		|| Command == 0x400000D3u
		|| Command == 0x400000E0u
		|| Command == 0x400000E1u;
}

static void ACEStopCastGestureIfLeaving(AActor* Owner, bool bLeavingCast, bool bStayingInCast)
{
	if (!bLeavingCast || bStayingInCast || !Owner)
	{
		return;
	}
	if (UACEScriptComponent* Scripts = Owner->FindComponentByClass<UACEScriptComponent>())
	{
		Scripts->StopCastGestureEffects();
	}
}

UACECharacterAppearanceComponent::UACECharacterAppearanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UACECharacterAppearanceComponent::EnsureMeshRoot()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (!MeshRoot)
	{
		MeshRoot = NewObject<USceneComponent>(Owner, TEXT("ACEMeshRoot"));
		MeshRoot->SetupAttachment(Owner->GetRootComponent());
		MeshRoot->RegisterComponent();
	}

	FVector FeetOffset = FVector::ZeroVector;
	if (bAlignMeshToCapsuleBottom)
	{
		if (UCapsuleComponent* Cap = Owner->FindComponentByClass<UCapsuleComponent>())
		{
			const FVector FeetWorld = Cap->GetComponentLocation()
				- Cap->GetUpVector() * Cap->GetScaledCapsuleHalfHeight();
			FeetOffset = Owner->GetRootComponent()->GetComponentTransform().InverseTransformPosition(FeetWorld);
		}
	}
	if (bVRPoseControlled) return; // The tracked rig owns this root during appearance refreshes.
	MeshRoot->SetRelativeLocation(FeetOffset);
	MeshRoot->SetRelativeRotation(FRotator(0.f, MeshFacingYawDegrees, 0.f));
}

float UACECharacterAppearanceComponent::GetVisualHeightCm() const
{
	FBox VisualBounds(ForceInit);
	for (const UProceduralMeshComponent* Part : PartMeshes)
	{
		if (Part && Part->IsRegistered() && Part->GetNumSections() > 0)
		{
			VisualBounds += Part->Bounds.GetBox();
		}
	}
	return VisualBounds.IsValid ? VisualBounds.GetSize().Z : 0.f;
}

bool UACECharacterAppearanceComponent::GetVisualWorldBounds(FBox& OutBox) const
{
	OutBox = FBox(ForceInit);
	for (const UProceduralMeshComponent* Part : PartMeshes)
	{
		if (Part && Part->IsRegistered() && Part->GetNumSections() > 0)
		{
			OutBox += Part->Bounds.GetBox();
		}
	}
	return OutBox.IsValid != 0;
}

bool UACECharacterAppearanceComponent::GetPartBindTransform(int32 PartIndex, FTransform& OutTransform) const
{
	if (!BindTransforms.IsValidIndex(PartIndex))
	{
		return false;
	}
	OutTransform = BindTransforms[PartIndex];
	return true;
}

void UACECharacterAppearanceComponent::StabilizeVRPelvis()
{
	FTransform Bind, Animated;
	if (!GetPartBindTransform(0, Bind) || !GetPartCurrentTransform(0, Animated)) return;
	// Stance animations move the abdomen, but VR pins the chest under the
	// headset. Remove that common lower-body shift while keeping leg motion.
	for (int32 I=1; I<9; ++I)
		if (auto* Part=GetPartMesh(I)) Part->SetRelativeTransform(Part->GetRelativeTransform().GetRelativeTransform(Animated) * Bind);
	if (auto* Pelvis=GetPartMesh(0)) Pelvis->SetRelativeTransform(Bind);
}

bool UACECharacterAppearanceComponent::GetPartCurrentTransform(int32 PartIndex, FTransform& OutTransform) const
{
	if (PartMeshes.IsValidIndex(PartIndex) && PartMeshes[PartIndex])
	{
		OutTransform = PartMeshes[PartIndex]->GetRelativeTransform();
		return true;
	}
	return GetPartBindTransform(PartIndex, OutTransform);
}

USceneComponent* UACECharacterAppearanceComponent::GetPartMesh(int32 PartIndex) const
{
	if (PartMeshes.IsValidIndex(PartIndex) && PartMeshes[PartIndex])
	{
		return PartMeshes[PartIndex];
	}
	return nullptr;
}

bool UACECharacterAppearanceComponent::ReplacePartGfxObj(int32 PartIndex, uint32 GfxObjId)
{
	if (!PartMeshes.IsValidIndex(PartIndex) || !PartMeshes[PartIndex] || GfxObjId == 0)
	{
		return false;
	}
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return false;
	}
	// ApplySetupToProceduralMesh accepts bare GfxObj DIDs (0x01) via BuildGfxObjOnly.
	const bool bOk = Dat->ApplySetupToProceduralMesh(
		PartMeshes[PartIndex], static_cast<int32>(GfxObjId), WorldScale, /*bEnableCollision*/ false);
	if (bOk)
	{
		// Keep the part's current placement/anim transform; only geometry/materials change.
		UE_LOG(LogTemp, Verbose, TEXT("ACEAppearance: ReplaceObject part=%d gfx=0x%08X"),
			PartIndex, GfxObjId);
	}
	return bOk;
}

void UACECharacterAppearanceComponent::SetAppearanceVisible(bool bVisible)
{
	// Animation/door restoration cannot override an authoritative visibility flag.
	// Keep this at the part boundary so attachment and appearance rebuilds agree.
	if (const auto* Entity = Cast<AACEWorldEntityActor>(GetOwner()))
		bVisible = bVisible && !Entity->IsMeshSuppressed();
	for (UProceduralMeshComponent* Part : PartMeshes)
	{
		if (Part)
		{
			// Held actors attach under these parts. Their collision proxies and
			// script components own their visibility; never recursively reveal them.
			Part->SetVisibility(bVisible);
			Part->SetHiddenInGame(!bVisible);
		}
	}
}

void UACECharacterAppearanceComponent::SetPartsCastShadow(bool bCast, bool bInset)
{
	const bool bWantInset = bInset && bCast;
	for (UProceduralMeshComponent* Part : PartMeshes)
	{
		if (!Part)
		{
			continue;
		}
		if (bCast && Part->Mobility != EComponentMobility::Movable)
		{
			Part->SetMobility(EComponentMobility::Movable);
		}
		if (static_cast<bool>(Part->CastShadow) != bCast || Part->bCastDynamicShadow != bCast)
		{
			Part->SetCastShadow(bCast);
			Part->bCastDynamicShadow = bCast;
			Part->bCastShadowAsTwoSided = bCast;
			Part->MarkRenderStateDirty();
		}
		if (Part->bCastInsetShadow != bWantInset)
		{
			Part->bCastInsetShadow = bWantInset;
		}
	}
}

bool UACECharacterAppearanceComponent::GetPartsCastShadow() const
{
	for (const UProceduralMeshComponent* Part : PartMeshes)
	{
		if (Part)
		{
			return static_cast<bool>(Part->CastShadow);
		}
	}
	return false;
}

void UACECharacterAppearanceComponent::ApplyPartTransform(int32 PartIndex, const FTransform& AnimOrBind)
{
	if (bVRPoseControlled) return;
	if (!PartMeshes.IsValidIndex(PartIndex) || !PartMeshes[PartIndex])
	{
		return;
	}
	// Facing is on MeshRoot only — parts keep raw Setup/Anim transforms so head/body stay coherent.
	const FTransform& Previous = PartMeshes[PartIndex]->GetRelativeTransform();
	// Centimeter translation tolerance is not a quaternion tolerance: .04 skipped
	// several degrees of rotation, turning slow tunnel/creature motion into steps.
	if (Previous.GetTranslation().Equals(AnimOrBind.GetTranslation(), .04f)
		&& Previous.GetRotation().Equals(AnimOrBind.GetRotation(), .00001f)
		&& Previous.GetScale3D().Equals(AnimOrBind.GetScale3D(), .00001f))
	{
		return;
	}
	PartMeshes[PartIndex]->SetRelativeTransform(AnimOrBind);
}

void UACECharacterAppearanceComponent::TickObjectAnimFrame(float DeltaTime)
{
	FVector OmegaUe = FVector::ZeroVector;
	if (AACEWorldEntityActor* Entity = Cast<AACEWorldEntityActor>(GetOwner()))
	{
		OmegaUe = FACEPosition::AceVectorToUnreal(Entity->GetAcePhysicsOmega(), 1.f);
	}
	else if (AActor* Owner = GetOwner())
	{
		// Portal tunnel: retail animate_static_object rotates Position.Frame by Omega after
		// PartArray.Update — SetOmega hooks land on Scripts, not the world entity.
		if (UACEScriptComponent* Scripts = Owner->FindComponentByClass<UACEScriptComponent>())
		{
			OmegaUe = Scripts->GetRootOmegaRadiansPerSecond();
		}
	}
	if (OmegaUe.IsNearlyZero())
	{
		return;
	}
	const FVector DeltaRad = OmegaUe * DeltaTime;
	const FQuat DeltaQ(FRotator(
		FMath::RadiansToDegrees(-DeltaRad.X),
		FMath::RadiansToDegrees(DeltaRad.Z),
		FMath::RadiansToDegrees(-DeltaRad.Y)));
	ObjectAnimFrame.SetRotation((DeltaQ * ObjectAnimFrame.GetRotation()).GetNormalized());
}

void UACECharacterAppearanceComponent::ApplyDefaultAnimPartTransforms(
	const TArray<FTransform>& Animated, int32 AnimatedCount)
{
	// Retail CPartArray::UpdateParts: Frame::combine(part, object_frame, anim_frame[i], scale).
	// Sequence part frames replace Setup placement while the clip plays. portalspace_background
	// placement is (51,0,0) but anim 0x030005AC frame 0 is (0,0,51) — composing bind rotation
	// onto those frames leaves gaps in the tunnel wall.
	for (int32 i = 0; i < PartMeshes.Num(); ++i)
	{
		if (!bHeldKeepPlacementPose && i < AnimatedCount && Animated.IsValidIndex(i))
		{
			FTransform AnimPart = Animated[i];
			if (BindTransforms.IsValidIndex(i))
				AnimPart.SetScale3D(BindTransforms[i].GetScale3D());
			FTransform PartXform = FACEPosition::CombineAceFrames(ObjectAnimFrame, AnimPart);
			ApplyPartTransform(i, PartXform);
		}
		else if (BindTransforms.IsValidIndex(i))
		{
			ApplyPartTransform(i, BindTransforms[i]);
		}
	}
}

void UACECharacterAppearanceComponent::BillboardSpriteParts()
{
	if (!bBillboardSpriteParts)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
	if (!Camera)
	{
		return;
	}
	const FVector CamFwd = Camera->GetActorForwardVector();
	const FVector CamUp = Camera->GetActorUpVector();
	if (CamFwd.IsNearlyZero() || CamUp.IsNearlyZero())
	{
		return;
	}
	// Match particle XZ sprites: authored facing local -Y toward the camera.
	const FQuat Face = FRotationMatrix::MakeFromYZ(CamFwd, CamUp).ToQuat();
	for (UProceduralMeshComponent* Part : PartMeshes)
	{
		if (Part && Part->GetNumSections() > 0)
		{
			Part->SetWorldRotation(Face);
		}
	}
}

void UACECharacterAppearanceComponent::RecookPartPhysics()
{
	for (UProceduralMeshComponent* Proc : PartMeshes)
	{
		if (!Proc || Proc->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}
		Proc->bUseComplexAsSimpleCollision = true;
		Proc->RecreatePhysicsState();
	}
}

void UACECharacterAppearanceComponent::SetPreviewAnimation(uint32 AnimationId, bool bAnimate, int32 StartFrame)
{
	// gmCG3DView::StartAnimation/StopAnimation use an absolute part-frame sequence,
	// not the setup's Resting placement and not the world locomotion state machine.
	bPreviewAnimation = true;
	PreviewStartFrame = FMath::Max(0,StartFrame);
	DefaultAnimationId = AnimationId;
	AnimMode = EACEAnimMode::DefaultAnimLoop;
	bPlayIdleMotion = true;
	LocomotionPlayRate = bAnimate ? 1.f : 0.f;
	ObjectAnimFrame = FTransform::Identity;
	AnimTime = DeferredPoseDeltaTime = 0.f;
	ResetHookTracking();
	TickComponent(0.f, LEVELTICK_All, nullptr); // Pose before the first capture, including face view.
	SetComponentTickEnabled(bAnimate);
}

void UACECharacterAppearanceComponent::ResetDefaultAnimClock()
{
	AnimTime = 0.f;
	DeferredPoseDeltaTime = 0.f;
	ObjectAnimFrame = FTransform::Identity;
	ResetHookTracking();
}

void UACECharacterAppearanceComponent::DispatchCrossedHooks(const TArray<FACEDatAnimationHook>& Hooks)
{
	if (Hooks.Num() == 0)
	{
		return;
	}
	UACEScriptComponent* Scripts = GetOwner()
		? GetOwner()->FindComponentByClass<UACEScriptComponent>()
		: nullptr;
	if (!IsValid(Scripts))
	{
		return;
	}
	AACELoadingScreenActor* PortalTunnel = Cast<AACELoadingScreenActor>(GetOwner());
	for (const FACEDatAnimationHook& Hook : Hooks)
	{
		if (PortalTunnel && Hook.Type == EACEAnimationHookType::SoundTweaked)
		{
			if (PortalTunnel->TryStartPortalAmbient(Hook.Id, Hook.Volume))
			{
				continue;
			}
		}
		if (GetOwner() && GetOwner()->IsA<AACERegionSceneryActor>()
			&& (Hook.Type == EACEAnimationHookType::Sound
				|| Hook.Type == EACEAnimationHookType::SoundTable
				|| Hook.Type == EACEAnimationHookType::SoundTweaked))
		{
			continue;
		}
		Scripts->DispatchAnimationHook(Hook);
	}
}

const float* UACECharacterAppearanceComponent::GetPreviousHookTime(uint64 TrackKey, float CurrentEvalTime)
{
	if (!bHookTrackValid || HookTrackKey != TrackKey || !FMath::IsFinite(CurrentEvalTime))
	{
		return nullptr;
	}
	return &PreviousHookTime;
}

void UACECharacterAppearanceComponent::CommitHookTime(uint64 TrackKey, float CurrentEvalTime)
{
	HookTrackKey = TrackKey;
	PreviousHookTime = CurrentEvalTime;
	bHookTrackValid = FMath::IsFinite(CurrentEvalTime);
}

void UACECharacterAppearanceComponent::ResetHookTracking()
{
	HookTrackKey = 0;
	PreviousHookTime = 0.f;
	bHookTrackValid = false;
}

void UACECharacterAppearanceComponent::EnsurePartMeshes(int32 Count)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	EnsureMeshRoot();
	USceneComponent* AttachParent = MeshRoot ? MeshRoot.Get() : Owner->GetRootComponent();
	while (PartMeshes.Num() < Count)
	{
		const FName Name = *FString::Printf(TEXT("ACEPart_%d"), PartMeshes.Num());
		UProceduralMeshComponent* Proc = NewObject<UProceduralMeshComponent>(Owner, Name);
		if (AttachParent)
		{
			Proc->SetupAttachment(AttachParent);
		}
		Proc->RegisterComponent();
		Proc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Proc->SetCastShadow(false);
		Proc->bCastDynamicShadow = false;
		Proc->bCastContactShadow = false;
		Proc->bCastInsetShadow = false;
		Proc->bCastFarShadow = false;
		PartMeshes.Add(Proc);
	}
	for (int32 i = 0; i < PartMeshes.Num(); ++i)
	{
		if (PartMeshes[i])
		{
			PartMeshes[i]->SetVisibility(i < Count);
		}
	}
}

void UACECharacterAppearanceComponent::HideOwnerPrimitiveMeshes()
{
	AActor* Owner = GetOwner();
	if (!Owner || !bHideOwnerMeshes)
	{
		return;
	}
	TArray<UPrimitiveComponent*> Primatives;
	Owner->GetComponents<UPrimitiveComponent>(Primatives);
	for (UPrimitiveComponent* Prim : Primatives)
	{
		if (!Prim || PartMeshes.Contains(Prim))
		{
			continue;
		}
		if (Prim->IsA(UProceduralMeshComponent::StaticClass()))
		{
			continue;
		}
		Prim->SetVisibility(false);
	}
}

void UACECharacterAppearanceComponent::ClearAppearance()
{
	WorldLightingInstances.Reset(); bWorldLightingInterior=false;
	if (AActor* Owner = GetOwner())
		if (auto* Scripts = Owner->FindComponentByClass<UACEScriptComponent>()) Scripts->StopAllEffects();
	for (UProceduralMeshComponent* P : PartMeshes)
	{
		if (P)
		{
			P->ClearAllMeshSections();
			P->EmptyOverrideMaterials();
			P->SetVisibility(false);
		}
	}
	bHasMesh = false;
	SetupId = 0;
	MotionTableId = 0;
	DefaultAnimationId = 0;
	bBillboardSpriteParts = false;
	bPreviewAnimation = false;
	Appearance = FACEObjDesc();
	AppliedPlacementId = 0;
	AppliedAppearanceHash = 0;
	AppliedTranslucencyKey = -1.f;
	AppliedWorldScale = 0.f;
	AppliedPartCollision = false;
	LocomotionForward = 0.f;
	LocomotionStrafe = 0.f;
	LocomotionPlayRate = 1.f;
	RunBlend = 0.f;
	AnimMode = EACEAnimMode::Locomotion;
	bDoorHoldFinal = false;
	bHoldActionFinal = false;
	bHoldActionFinalAfterFinish = false;
	bSuppressNextHookDispatch = false;
	AnimTime = 0.f;
	DeferredPoseDeltaTime = 0.f;
	ResetHookTracking();
}

bool UACECharacterAppearanceComponent::ApplyWorldObject(const FACEWorldObject& Object, float InWorldScale, bool bEnablePartCollision)
{
	ACE_PROFILE_SCOPE(Appearance);
	WorldScale = InWorldScale;
	const int32 NewSetupId = Object.SetupId;
	int32 PlacementId = Object.PlacementId;
	const bool bUsesOnOff = Object.UsesOnOffMotion();
	const bool bHeld = Object.ParentGuid != 0 && Object.ParentLocation != 0;
	const bool bStaticProp = !Object.bIsPlayer
		&& (Object.ItemType & ACEItemType::Creature) == 0
		&& Object.ParentGuid == 0
		&& Object.WielderId == 0
		&& Object.ContainerId == 0
		&& !bUsesOnOff;
	if (bHeld)
	{
		// Always use the ParentLocation placement (RightHandCombat / LeftHand / Shield).
		// Server Resting/Default/leftover ids leave the gfx centered on the grip so the
		// staff reads twice as long and particle hooks sit in the hand.
		const int32 HeldPlacement = ACEPlacementFromParentLocation(Object.ParentLocation);
		if (HeldPlacement != 0)
		{
			PlacementId = HeldPlacement;
		}
	}
	else if (PlacementId == 0 && (bStaticProp || bUsesOnOff)
		&& NewSetupId != 0x02000306)
	{
		// Doors/chests skipped Resting because they use On/Off motion. Default (0) part
		// frames are often a T-pose slab that does not sit in the Setup jamb.
		// Portalspace_background (DIDMap 0x02000306) has placement 0 only — no Resting 101.
		PlacementId = UACEDatSubsystem::ACEPlacementResting;
	}

	// ObjDescEvent / WieldItem re-broadcast OnObjectCreated frequently. Skip the expensive
	// ClearAllMeshSections + DAT rebuild when Setup + clothing fingerprint are unchanged.
	const uint64 AppearanceHash = Object.Appearance.GetContentHash();
	const float TranslucencyKey = FMath::RoundToFloat(Object.Translucency * 255.f);
	if (bHasMesh
		&& SetupId == NewSetupId
		&& AppliedPlacementId == PlacementId
		&& AppliedAppearanceHash == AppearanceHash
		&& FMath::IsNearlyEqual(AppliedTranslucencyKey, TranslucencyKey)
		&& FMath::IsNearlyEqual(AppliedWorldScale, InWorldScale)
		&& AppliedPartCollision == bEnablePartCollision)
	{
		MotionTableId = Object.MotionTableId != 0 ? Object.MotionTableId : MotionTableId;
		DefaultAnimationId = Object.DefaultAnimationId != 0 ? Object.DefaultAnimationId : DefaultAnimationId;
		bHeldKeepPlacementPose = bHeld;
		if (Object.IsCorpse() && MotionTableId != 0)
		{
			SetHeldActionMotion(ACEMotion::Dead, ACEMotion::StanceNonCombat);
		}
		const bool bCreatureLikeEarly = Object.bIsPlayer || Object.bIsSelf
			|| (Object.ItemType & ACEItemType::Creature) != 0;
		if (bCreatureLikeEarly)
		{
			SetPartsCastShadow(true, /*bInset*/ false);
		}
		return true;
	}

	const bool PreserveAnimation = bHasMesh && SetupId == NewSetupId && (Object.bIsPlayer || Object.bIsSelf || (Object.ItemType & ACEItemType::Creature));
	TArray<FTransform> PreviousParts;
	if (PreserveAnimation) for (const auto& Part : PartMeshes)
		PreviousParts.Add(Part ? Part->GetRelativeTransform() : FTransform::Identity);
	SetupId = NewSetupId;
	MotionTableId = Object.MotionTableId;
	DefaultAnimationId = Object.DefaultAnimationId;
	Appearance = Object.Appearance;
	if (!PreserveAnimation)
	{
		AnimTime = 0.f;
		DeferredPoseDeltaTime = 0.f;
		bHasMesh = false;
		bDoorHoldFinal = false;
		bHoldActionFinal = false;
		bHoldActionFinalAfterFinish = false;
		bResumeDefaultAnimAfterAction = false;
		bSuppressNextHookDispatch = false;
		AnimMode = EACEAnimMode::Locomotion;
		ResetHookTracking();
	}
	bHasMesh = false;
	const bool bIsCorpse = Object.IsCorpse();
	const bool bCreatureLike = (Object.ItemType & ACEItemType::Creature) != 0
		|| Object.bIsPlayer || Object.bIsSelf;
	bPlayIdleMotion = bIsCorpse || bCreatureLike || bUsesOnOff;
	SetComponentTickEnabled(bPlayIdleMotion);

	if (SetupId == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("ACEAppearance: '%s' has no SetupId — ObjectCreate physics desc had no CSetup flag, falling back to placeholder"), *Object.Name);
		return false;
	}

	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (!GI)
	{
		return false;
	}
	UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>();
	if (!Dat)
	{
		return false;
	}

	FACEBuiltSetupMesh Built;
	const double BuildStart = FPlatformTime::Seconds();
	if (!Dat->BuildSetupAppearance(static_cast<uint32>(SetupId), Appearance, WorldScale, Built, PlacementId)
		|| Built.Parts.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEAppearance: failed Setup 0x%08X for '%s'"), SetupId, *Object.Name);
		return false;
	}

	// A dissolve's saved materials belong to the previous ObjDesc. Retire them
	// before replacing the parts, otherwise UnHide restores the old character's
	// palette and texture replacements onto this character's geometry.
	if (AActor* Owner = GetOwner())
		if (auto* Scripts = Owner->FindComponentByClass<UACEScriptComponent>()) Scripts->NotifyAppearanceChanging();
	EnsurePartMeshes(Built.Parts.Num());
	const double MeshStart = FPlatformTime::Seconds();
	TArray<UProceduralMeshComponent*> Meshes;
	for (int32 i = 0; i < Built.Parts.Num(); ++i)
	{
		Meshes.Add(PartMeshes[i]);
	}

	uint32 DefaultMT = 0;
	if (!Dat->ApplySetupParts(Meshes, SetupId, Appearance, WorldScale, BindTransforms, DefaultMT,
		PlacementId, bEnablePartCollision, Object.Translucency, &Built))
	{
		return false;
	}
	if (MotionTableId == 0)
	{
		MotionTableId = static_cast<int32>(DefaultMT);
	}
	if (bUseWorldLighting)
	{
		WorldLightingInstances.Reset();
		for (auto* Part : Meshes)
			if (Part) for (int32 I = 0; I < Part->GetNumMaterials(); ++I)
			{
				auto* Source=Part->GetMaterial(I);
				auto* WorldMaterial=Dat->GetWorldObjectMaterial(Source);
				if (WorldMaterial && WorldMaterial!=Source)
				{
					auto* Instance=UMaterialInstanceDynamic::Create(WorldMaterial->GetMaterial(),this);
					Instance->CopyMaterialUniformParameters(WorldMaterial);
					Dat->UpdateWorldObjectLighting(Instance,false);
					WorldLightingInstances.Add(Instance); Part->SetMaterial(I,Instance);
				}
			}
		bWorldLightingInterior=false;
		UpdateCellLighting(Object.Position.CellId);
	}
	const double ApplyEnd = FPlatformTime::Seconds();
	if (ApplyEnd-BuildStart > .008)
		UE_LOG(LogTemp, Log, TEXT("ACEAppearance cost: '%s' setup=%08X decode/components=%.2fms mesh/materials=%.2fms parts=%d"),
			*Object.Name,SetupId,(MeshStart-BuildStart)*1000,(ApplyEnd-MeshStart)*1000,Built.Parts.Num());

	{
		float StepUp = 0.5f, Height = 2.f, Radius = 0.5f;
		uint32 SetupDefaultAnim = 0;
		if (Dat->TryGetSetupPhysics(static_cast<uint32>(SetupId), StepUp, Height, Radius, SetupDefaultAnim))
		{
			if (DefaultAnimationId == 0)
			{
				DefaultAnimationId = static_cast<int32>(SetupDefaultAnim);
			}
		}
	}

	// Retail HasDefaultAnim is Static-only. Held weapons keep Placement (RightHandCombat)
	// and do not run DefaultAnimLoop — those hooks StopParticle the Setup DefaultScript
	// drips (acid/frost/fire). Elemental FX comes from StartDefaultScripts instead.
	bHeldKeepPlacementPose = bHeld;
	const bool bUseDefaultAnimLoop = DefaultAnimationId != 0
		&& !bHeld
		&& !bUsesOnOff
		&& !bIsCorpse
		&& !bCreatureLike;
	if (bUseDefaultAnimLoop)
	{
		AnimMode = EACEAnimMode::DefaultAnimLoop;
		ObjectAnimFrame = FTransform::Identity;
		bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
	}
	else if (bUsesOnOff && MotionTableId != 0)
	{
		AnimMode = EACEAnimMode::DoorTransition;
		bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
		// Prefer server On/Off. Do NOT infer open from Ethereal alone — closed doors can
		// still carry Ethereal in weenie PhysicsState. Default closed when unsure.
		// Pose is applied after bHasMesh below (SetDoorHeldCommand needs the mesh).
	}
	else if (bIsCorpse && MotionTableId != 0)
	{
		// Pose applied after mesh is ready (below) so bind pose never flashes.
		bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
	}
	else if (bCreatureLike && MotionTableId != 0)
	{
		if (!PreserveAnimation) AnimMode = EACEAnimMode::Locomotion;
		bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
	}
	else if (Object.IsLifeStone() && MotionTableId != 0)
	{
		// Fallback when Setup.DefaultAnimation is missing: MotionTable idle cycle.
		AnimMode = EACEAnimMode::Locomotion;
		bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
	}

	if (!bHeld)
	{
		const float S = FMath::Clamp(Object.Scale, 0.25f, 4.f);
		if (AActor* Owner = GetOwner())
		{
			UCapsuleComponent* Capsule = bAlignMeshToCapsuleBottom ? Owner->FindComponentByClass<UCapsuleComponent>() : nullptr;
			const FVector Feet = Capsule ? Capsule->GetComponentLocation() - Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight() : FVector::ZeroVector;
			Owner->SetActorScale3D(FVector(S));
			if (Capsule)
			{
				const FVector NewFeet = Capsule->GetComponentLocation() - Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight();
				Owner->AddActorWorldOffset(Feet - NewFeet, false);
			}
		}
	}

	EnsureMeshRoot();
	for (int32 i = 0; i < BindTransforms.Num(); ++i)
	{
		if (PreviousParts.IsValidIndex(i) && PartMeshes.IsValidIndex(i) && PartMeshes[i])
			PartMeshes[i]->SetRelativeTransform(PreviousParts[i]);
		else ApplyPartTransform(i, BindTransforms[i]);
	}

	HideOwnerPrimitiveMeshes();
	bHasMesh = true;
	AppliedPlacementId = PlacementId;
	AppliedAppearanceHash = AppearanceHash;
	++AppearanceRevision;
	AppliedTranslucencyKey = TranslucencyKey;
	AppliedWorldScale = InWorldScale;
	AppliedPartCollision = bEnablePartCollision;

	// Doors/chests: apply held On/Off AFTER the mesh exists. Calling SetDoorHeldCommand earlier
	// returned without posing, leaving the Setup bind pose (chests often look permanently open).
	if (bUsesOnOff && MotionTableId != 0)
	{
		const int32 Cmd = (Object.InitialMotionCommand == ACEMotion::OnCommandU16
			|| Object.InitialMotionCommand == ACEMotion::OffCommandU16)
			? Object.InitialMotionCommand
			: static_cast<int32>(ACEMotion::OffCommandU16);
		SetDoorHeldCommand(Cmd);
	}

	if (bIsCorpse && MotionTableId != 0)
	{
		const int32 Style = Object.InitialMotionStyle != 0
			? Object.InitialMotionStyle
			: static_cast<int32>(ACEMotion::StanceNonCombat);
		const int32 Cmd = (Object.InitialMotionCommand == ACEMotion::DeadCommandU16
			|| Object.InitialMotionCommand == static_cast<int32>(ACEMotion::Dead))
			? Object.InitialMotionCommand
			: static_cast<int32>(ACEMotion::Dead);
		SetHeldActionMotion(Cmd, Style);
	}
	else if (!bUsesOnOff && MotionTableId != 0 && ACEMotion::IsHeldRestCommand(Object.InitialMotionCommand))
	{
		const int32 Style = Object.InitialMotionStyle != 0
			? Object.InitialMotionStyle
			: static_cast<int32>(ACEMotion::StanceNonCombat);
		SetHeldActionMotion(Object.InitialMotionCommand, Style);
	}

	int32 TotalVerts = 0;
	FBox LocalBounds(ForceInit);
	for (const FACEBuiltSetupPart& Part : Built.Parts)
	{
		for (const FACEBuiltMeshSection& Sec : Part.Sections)
		{
			for (const FVector& V : Sec.Vertices)
			{
				LocalBounds += Part.BindTransform.TransformPosition(V);
				++TotalVerts;
			}
		}
	}
	const FVector Extent = TotalVerts > 0 ? LocalBounds.GetExtent() : FVector::ZeroVector;
	// Do not camera-lock thin world meshes. Doors, pedestals, cacti, and trees are
	// authored in Setup space; yawing them toward the camera was never correct.
	bBillboardSpriteParts = false;
	UE_LOG(LogTemp, Log, TEXT("ACEAppearance: applied '%s' setup=0x%08X parts=%d verts=%d boxExtent=(%.1f,%.1f,%.1f) meshYaw=%.0f overrides=%s"),
		*Object.Name, SetupId, Built.Parts.Num(), TotalVerts, Extent.X, Extent.Y, Extent.Z, MeshFacingYawDegrees,
		Appearance.HasVisualOverrides() ? TEXT("yes") : TEXT("no"));
	if (Object.bIsPlayer || Object.bIsSelf)
	{
		int32 HeadAnim = 0;
		uint32 HeadGfx = 0;
		int32 HeadTex = 0;
		for (const FACEObjDescAnimPartChange& Ap : Appearance.AnimPartChanges)
		{
			if (Ap.PartIndex == 0x10)
			{
				++HeadAnim;
				HeadGfx = static_cast<uint32>(Ap.PartId);
			}
		}
		for (const FACEObjDescTextureChange& Tc : Appearance.TextureChanges)
		{
			if (Tc.PartIndex == 0x10)
			{
				++HeadTex;
			}
		}
		UE_LOG(LogTemp, Verbose,
			TEXT("ACEAppearance: head part0x10 animParts=%d gfx=0x%08X texChanges=%d subPals=%d paletteBase=0x%08X"),
			HeadAnim, HeadGfx, HeadTex, Appearance.SubPalettes.Num(),
			static_cast<uint32>(Appearance.PaletteBaseId));
	}
	if (bCreatureLike)
	{
		SetPartsCastShadow(true, /*bInset*/ false);
	}
	return true;
}

void UACECharacterAppearanceComponent::UpdateCellLighting(uint32 CellId)
{
	const uint32 LocalCell=CellId & 0xFFFFu;
	const bool bInterior=LocalCell>=0x100u && LocalCell<0xFFFEu;
	if (bWorldLightingInterior==bInterior) return;
	bWorldLightingInterior=bInterior;
	auto* GI=GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	auto* Dat=GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	for (UMaterialInstanceDynamic* Material:WorldLightingInstances)
	{
		if (Dat) Dat->UpdateWorldObjectLighting(Material,bInterior);
	}
}

bool UACECharacterAppearanceComponent::HasAuthoredPhysicsGeometry() const
{
	for (UProceduralMeshComponent* Proc : PartMeshes)
		if (Proc) for (int32 I = 0; I < Proc->GetNumSections(); ++I)
		{
			const auto* Section = Proc->GetProcMeshSection(I);
			if (Section && Section->ProcIndexBuffer.Num() >= 3 &&
				Proc->ComponentTags.Contains(FName(*FString::Printf(TEXT("ACEPhysicsSection_%d"), I)))) return true;
		}
	return false;
}

void UACECharacterAppearanceComponent::ConfigurePartCollision(bool bBlocking, bool bQueryVisibilityOnly, bool bSelectOnMesh)
{
	for (UProceduralMeshComponent* Proc : PartMeshes)
	{
		if (!Proc || !Proc->GetNumSections())
		{
			continue;
		}
		const bool bComplexChanged = !Proc->bUseComplexAsSimpleCollision;
		if (bBlocking || bQueryVisibilityOnly || bSelectOnMesh)
			Proc->bUseComplexAsSimpleCollision = true;
		for (int32 Index = 0; Index < Proc->GetNumSections(); ++Index)
		{
			const FProcMeshSection* Section = Proc->GetProcMeshSection(Index);
			if (!Section) continue;
			const bool bPhysics = Proc->ComponentTags.Contains(
				FName(*FString::Printf(TEXT("ACEPhysicsSection_%d"), Index)));
			const bool bCollide = bBlocking ? bPhysics : (!bPhysics && (bQueryVisibilityOnly || bSelectOnMesh));
			if (Section->bEnableCollision != bCollide)
			{
				FProcMeshSection Updated = *Section;
				Updated.bEnableCollision = bCollide;
				Proc->SetProcMeshSection(Index, Updated);
			}
		}
		if (bBlocking)
		{
			Proc->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Proc->SetCollisionObjectType(ECC_WorldStatic);
			Proc->SetCollisionResponseToAllChannels(ECR_Block);
			Proc->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			// Doors: Visibility on the DAT mesh so loot behind isn't occluded by a fat capsule.
			// Other props: capsule CollisionProxy handles hover/select.
			Proc->SetCollisionResponseToChannel(ECC_Visibility,
				bSelectOnMesh ? ECR_Block : ECR_Ignore);
			Proc->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			Proc->bUseComplexAsSimpleCollision = true;
			// Forge / lifestone / static props: block the pawn but do not allow step-up —
			// TryStepUp onto complex meshes caused shake/trap/pass-through. Doors keep Yes.
			Proc->CanCharacterStepUpOn = bSelectOnMesh ? ECB_Yes : ECB_No;
			if (bComplexChanged) Proc->RecreatePhysicsState();
		}
		else if (bQueryVisibilityOnly || bSelectOnMesh)
		{
			// Open / ethereal door: walk through, still pick the mesh silhouette.
			Proc->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Proc->SetCollisionObjectType(ECC_WorldDynamic);
			Proc->SetCollisionResponseToAllChannels(ECR_Ignore);
			Proc->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Proc->bUseComplexAsSimpleCollision = true;
			if (bComplexChanged) Proc->RecreatePhysicsState();
		}
		else
		{
			Proc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void UACECharacterAppearanceComponent::SetLocomotionInput(float Forward, float Strafe, bool bRunning, float PlayRate)
{
	if (AnimMode == EACEAnimMode::DoorTransition)
	{
		return;
	}
	if (AnimMode == EACEAnimMode::DefaultAnimLoop)
	{
		// Static props ignore loco. Creatures mis-classified as DefaultAnimLoop promote.
		if (MotionTableId == 0 || (FMath::IsNearlyZero(Forward) && FMath::IsNearlyZero(Strafe)))
		{
			return;
		}
		AnimMode = EACEAnimMode::Locomotion;
		bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
	}
	// ActionOneShot still records loco so we resume the right cycle when the attack ends.
	LocomotionForward = Forward;
	LocomotionStrafe = Strafe;
	bLocomotionRunning = bRunning;
	LocomotionPlayRate = FMath::Max(0.05f, PlayRate);
	// Snap run blend so remotes don't linger in a walk/run interpolate while charging.
	if (bRunning && !FMath::IsNearlyZero(Forward))
	{
		RunBlend = 1.f;
	}
	else if (!bRunning)
	{
		RunBlend = 0.f;
	}
	// Retail chat emotes hold the last frame until the player moves — movement also
	// cancels a still-playing chat pose (hold-pending), like retail's forward-command rewrite.
	// Jump/Falling must NOT cancel: airborne + W still feeds locomotion axes for land resume,
	// and clearing Falling mid-air made the run cycle play instead of the jump.
	// Ignore analog/camera noise: IsNearlyZero cancelled *point* / *playpossum* on the next tick.
	const bool bJumpHold = ActionCommand == 0x1000004bu // Jumpup
		|| ActionCommand == 0x40000015u // Falling
		|| ActionCommand == 0x4000001du; // JumpCharging
	const bool bMoving = FMath::Abs(Forward) > 0.15f || FMath::Abs(Strafe) > 0.15f;
	const bool bIdleTwitch = (ActionCommand >= 0x10000051u && ActionCommand <= 0x10000054u)
		|| (ActionCommand >= 0x400000E4u && ActionCommand <= 0x400000E6u);
	if ((bHoldActionFinal || bHoldActionFinalAfterFinish || bIdleTwitch) && AnimMode == EACEAnimMode::ActionOneShot
		&& !bJumpHold
		&& ActionCommand != ACEMotion::Dead
		&& bMoving)
	{
		bHoldActionFinal = false;
		bHoldActionFinalAfterFinish = false;
		BeginPoseBlendFromCurrent();
		AnimMode = EACEAnimMode::Locomotion;
		ActionCommand = 0;
		AnimTime = 0.f;
		DeferredPoseDeltaTime = 0.f;
		ResetHookTracking();
	}
}

void UACECharacterAppearanceComponent::SetPreferredStyle(int32 Style)
{
	// MotionStance values are 0x8000… — negative when stored as signed int32.
	const uint32 AsU32 = static_cast<uint32>(Style);
	if (AsU32 == 0)
	{
		return;
	}
	uint32 Expanded = AsU32;
	if (Expanded <= 0xFFFFu)
	{
		Expanded = ACEMotion::ExpandPackedCommand(static_cast<uint16>(Expanded));
	}
	// Only accept MotionStance family (0x8000…).
	if ((Expanded & 0xFF000000u) != 0x80000000u)
	{
		return;
	}
	if (Expanded == PreferredStyle)
	{
		return;
	}

	// Capture the current visual pose so the next ticks can cross-fade into the new stance.
	StanceBlendFrom.SetNum(PartMeshes.Num());
	for (int32 i = 0; i < PartMeshes.Num(); ++i)
	{
		if (PartMeshes[i])
		{
			StanceBlendFrom[i] = PartMeshes[i]->GetRelativeTransform();
		}
		else if (BindTransforms.IsValidIndex(i))
		{
			StanceBlendFrom[i] = BindTransforms[i];
		}
		else
		{
			StanceBlendFrom[i] = FTransform::Identity;
		}
	}
	StanceBlendAlpha = 0.f;
	PreferredStyle = Expanded;
}

void UACECharacterAppearanceComponent::BeginPoseBlendFromCurrent()
{
	StanceBlendFrom.SetNum(PartMeshes.Num());
	for (int32 i = 0; i < PartMeshes.Num(); ++i)
	{
		if (PartMeshes[i])
		{
			StanceBlendFrom[i] = PartMeshes[i]->GetRelativeTransform();
		}
		else if (BindTransforms.IsValidIndex(i))
		{
			StanceBlendFrom[i] = BindTransforms[i];
		}
		else
		{
			StanceBlendFrom[i] = FTransform::Identity;
		}
	}
	StanceBlendAlpha = 0.f;
}

void UACECharacterAppearanceComponent::ApplyAnimatedPartsWithBlend(
	const TArray<FTransform>& Animated, int32 AnimatedCount, float DeltaTime)
{
	TArray<FTransform> Blended;
	const TArray<FTransform>* Pose = &Animated;
	if (StanceBlendAlpha < 1.f - KINDA_SMALL_NUMBER && StanceBlendFrom.Num() == PartMeshes.Num())
	{
		Blended = Animated;
		Pose = &Blended;
		StanceBlendAlpha = FMath::Clamp(StanceBlendAlpha + DeltaTime / StanceBlendDuration, 0.f, 1.f);
		const float Alpha = StanceBlendAlpha;
		const int32 N = FMath::Min(Blended.Num(), StanceBlendFrom.Num());
		for (int32 i = 0; i < N; ++i)
		{
			FTransform Out;
			Out.Blend(StanceBlendFrom[i], Blended[i], Alpha);
			Blended[i] = Out;
		}
		if (StanceBlendAlpha >= 1.f - KINDA_SMALL_NUMBER)
		{
			StanceBlendFrom.Reset();
			StanceBlendAlpha = 1.f;
		}
	}
	for (int32 i = 0; i < PartMeshes.Num(); ++i)
	{
		if (i < AnimatedCount && Pose->IsValidIndex(i))
		{
			FTransform PartXform = (*Pose)[i];
			if (BindTransforms.IsValidIndex(i))
			{
				PartXform.SetScale3D(BindTransforms[i].GetScale3D());
			}
			ApplyPartTransform(i, PartXform);
		}
		else if (BindTransforms.IsValidIndex(i))
		{
			ApplyPartTransform(i, BindTransforms[i]);
		}
	}
}

void UACECharacterAppearanceComponent::PlayActionMotion(int32 InActionCommand, float PlayRate, int32 Style, bool bHoldFinalPose)
{
	// Remotes stuck in DefaultAnimLoop (missing Creature flag, etc.) must still play cast/attack.
	if (AnimMode == EACEAnimMode::DefaultAnimLoop)
	{
		bResumeDefaultAnimAfterAction = DefaultAnimationId != 0;
		AnimMode = EACEAnimMode::Locomotion;
	}
	if (AnimMode == EACEAnimMode::DoorTransition)
	{
		return;
	}
	uint32 Cmd = static_cast<uint32>(InActionCommand);
	if (Cmd == 0)
	{
		return;
	}
	if (Cmd <= 0xFFFFu)
	{
		Cmd = ACEMotion::ExpandPackedCommand(static_cast<uint16>(Cmd));
	}
	const bool bMagicPowerUp = (Cmd >= 0x1000006Fu && Cmd <= 0x10000078u)
		|| (Cmd >= 0x1000012Bu && Cmd <= 0x10000134u);
	const bool bMagicCastGesture = (Cmd >= 0x4000002Bu && Cmd <= 0x40000039u)
		|| Cmd == 0x400000D3u // CastSpell
		|| Cmd == 0x400000E0u // UseMagicStaff
		|| Cmd == 0x400000E1u; // UseMagicWand
	const bool bCurPowerUp = (ActionCommand >= 0x1000006Fu && ActionCommand <= 0x10000078u)
		|| (ActionCommand >= 0x1000012Bu && ActionCommand <= 0x10000134u);
	// Scarab windups own HighFrame particle tiers (frame 9/19/29…). Never cut them short
	// when the cast gesture arrives — queue it (and chained PowerUps) instead.
	if (AnimMode == EACEAnimMode::ActionOneShot && bCurPowerUp
		&& (bMagicCastGesture || bMagicPowerUp) && ActionCommand != Cmd)
	{
		QueueActionMotion(static_cast<int32>(Cmd), PlayRate, Style, bHoldFinalPose);
		return;
	}
	// Ignore duplicate while already playing OR holding the same action — Motion
	// CommandList echoes were restarting playpossum from t=0 (looked fast, never held).
	if (AnimMode == EACEAnimMode::ActionOneShot && ActionCommand == Cmd)
	{
		return;
	}
	BeginPoseBlendFromCurrent();
	AnimMode = EACEAnimMode::ActionOneShot;
	ActionCommand = Cmd;
	PendingActionCommands.Reset();
	PendingActionStyles.Reset();
	PendingActionPlayRates.Reset();
	PendingActionHolds.Reset();
	bHoldActionFinal = false;
	bHoldActionFinalAfterFinish = bHoldFinalPose;
	// ChatPose / soul-emote states (0x1300 / 0x4200 / 0x4300) always hold the final frame
	// until locomotion — even if the caller forgot bHoldFinalPose.
	const uint32 HoldFam = Cmd & 0xFF000000u;
	if (HoldFam == 0x13000000u || HoldFam == 0x42000000u || HoldFam == 0x43000000u
		|| ACEMotion::IsHeldRestCommand(Cmd) || Cmd == 0x40000015u || Cmd == 0x4000001Du)
	{
		bHoldActionFinalAfterFinish = true;
	}
	// MotionStance is 0x8000… (negative as int32) — do not use Style > 0.
	ActionStyle = static_cast<uint32>(Style);
	if (ActionStyle != 0 && ActionStyle <= 0xFFFFu)
	{
		ActionStyle = ACEMotion::ExpandPackedCommand(static_cast<uint16>(ActionStyle));
	}
	if ((ActionStyle & 0xFF000000u) != 0x80000000u)
	{
		ActionStyle = PreferredStyle != 0 ? PreferredStyle : ACEMotion::StanceNonCombat;
	}
	// Magic windups / casts: prefer the SERVER stance (HandCombat for most monsters).
	// Only fall back to Magic when style is unset / NonCombat (player scarab casts).
	// Forcing Magic for every cast made monster CastSpell resolve as melee via MT fallbacks.
	if ((bMagicPowerUp || bMagicCastGesture)
		&& (ActionStyle == 0 || ActionStyle == ACEMotion::StanceNonCombat))
	{
		ActionStyle = ACEMotion::StanceMagic;
	}
	ActionPlayRate = FMath::Max(0.05f, PlayRate);
	// Pickup motions must not inherit run-rate ForwardSpeed (>1) from InterpretedState.
	const uint16 Low = static_cast<uint16>(Cmd);
	if (Low == 0x0018 || Low == 0x0136 || Low == 0x0137 || Low == 0x0138 || Low == 0x0139)
	{
		ActionPlayRate = FMath::Clamp(ActionPlayRate, 0.05f, 1.f);
	}
	// Retail chat poses always enqueue at speed_mod 1.0 (MotionData framerate drives the
	// pace). A run-speed InterpretedState echo must not fast-forward *wave* / *bow*.
	const uint32 HiFam = Cmd & 0xFF000000u;
	if (HiFam == 0x13000000u || HiFam == 0x42000000u || HiFam == 0x43000000u)
	{
		ActionPlayRate = 1.f;
	}
	if (bMagicPowerUp || bMagicCastGesture)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ACEAnim: PlayAction 0x%08X style=0x%08X rate=%.2f (PowerUp=%d Cast=%d)"),
			Cmd, ActionStyle, ActionPlayRate, bMagicPowerUp ? 1 : 0, bMagicCastGesture ? 1 : 0);
	}
	bActionEverEvaluated = false;
	AnimTime = 0.f;
	DeferredPoseDeltaTime = 0.f;
	bSuppressNextHookDispatch = false;
	ResetHookTracking();
}

void UACECharacterAppearanceComponent::QueueActionMotion(int32 InActionCommand, float PlayRate, int32 Style, bool bHoldFinalPose)
{
	uint32 Cmd = static_cast<uint32>(InActionCommand);
	if (Cmd == 0)
	{
		return;
	}
	if (Cmd <= 0xFFFFu)
	{
		Cmd = ACEMotion::ExpandPackedCommand(static_cast<uint16>(Cmd));
	}
	PendingActionCommands.Add(Cmd);
	PendingActionPlayRates.Add(FMath::Max(0.05f, PlayRate));
	uint32 St = static_cast<uint32>(Style);
	if (St != 0 && St <= 0xFFFFu)
	{
		St = ACEMotion::ExpandPackedCommand(static_cast<uint16>(St));
	}
	PendingActionStyles.Add(St);
	PendingActionHolds.Add(bHoldFinalPose);
}

void UACECharacterAppearanceComponent::CancelHeldActionMotion()
{
	// Retail clears a held chat-pose when the player moves or jumps — the emote either
	// froze on its last frame (bHoldActionFinal) or is still pending a hold.
	if (AnimMode == EACEAnimMode::ActionOneShot && (bHoldActionFinal || bHoldActionFinalAfterFinish))
	{
		bHoldActionFinal = false;
		bHoldActionFinalAfterFinish = false;
		BeginPoseBlendFromCurrent();
		AnimMode = EACEAnimMode::Locomotion;
		ActionCommand = 0;
		AnimTime = 0.f;
		DeferredPoseDeltaTime = 0.f;
		ResetHookTracking();
	}
}

void UACECharacterAppearanceComponent::QueueHeldActionAfterCurrent(int32 InActionCommand, int32 Style)
{
	uint32 Cmd = static_cast<uint32>(InActionCommand);
	if (Cmd == 0)
	{
		QueuedHoldAction = 0;
		QueuedHoldStyle = 0;
		return;
	}
	if (Cmd <= 0xFFFFu)
	{
		Cmd = ACEMotion::ExpandPackedCommand(static_cast<uint16>(Cmd));
	}
	QueuedHoldAction = Cmd;
	uint32 St = static_cast<uint32>(Style);
	if (St != 0 && St <= 0xFFFFu)
	{
		St = ACEMotion::ExpandPackedCommand(static_cast<uint16>(St));
	}
	if ((St & 0xFF000000u) != 0x80000000u)
	{
		St = PreferredStyle != 0 ? PreferredStyle : ACEMotion::StanceNonCombat;
	}
	QueuedHoldStyle = St;
}

void UACECharacterAppearanceComponent::SetSuppressLocoIdleBlend(bool bSuppress)
{
	bSuppressLocoIdleBlend = bSuppress;
	if (bSuppress)
	{
		// Keep "moving" so we don't capture a Ready blend mid-jump.
		bWasLocomotionMoving = true;
	}
	else
	{
		// Align with actual axes — releasing WASD during jump must blend to Ready on land.
		bWasLocomotionMoving = !FMath::IsNearlyZero(LocomotionForward)
			|| !FMath::IsNearlyZero(LocomotionStrafe);
	}
}

void UACECharacterAppearanceComponent::ClearJumpMotionIfAny()
{
	constexpr uint32 Jumpup = 0x1000004bu;
	constexpr uint32 Falling = 0x40000015u;
	constexpr uint32 JumpCharging = 0x4000001du;
	const bool bJumpCmd = ActionCommand == Jumpup || ActionCommand == Falling || ActionCommand == JumpCharging;
	const bool bQueuedJump = QueuedHoldAction == Jumpup || QueuedHoldAction == Falling || QueuedHoldAction == JumpCharging;
	if (!bJumpCmd && !bQueuedJump)
	{
		return;
	}
	QueuedHoldAction = 0;
	QueuedHoldStyle = 0;
	bSuppressLocoIdleBlend = false;
	bWasLocomotionMoving = !FMath::IsNearlyZero(LocomotionForward)
		|| !FMath::IsNearlyZero(LocomotionStrafe);
	if (AnimMode == EACEAnimMode::ActionOneShot && bJumpCmd)
	{
		BeginPoseBlendFromCurrent();
		AnimMode = EACEAnimMode::Locomotion;
		ActionCommand = 0;
		bHoldActionFinal = false;
		AnimTime = 0.f;
		DeferredPoseDeltaTime = 0.f;
		ResetHookTracking();
	}
}

void UACECharacterAppearanceComponent::ClearActionMotion()
{
	if (AnimMode != EACEAnimMode::ActionOneShot)
	{
		return;
	}
	QueuedHoldAction = 0;
	QueuedHoldStyle = 0;
	bSuppressLocoIdleBlend = false;
	BeginPoseBlendFromCurrent();
	AnimMode = EACEAnimMode::Locomotion;
	ActionCommand = 0;
	bHoldActionFinal = false;
	AnimTime = 0.f;
	DeferredPoseDeltaTime = 0.f;
	ResetHookTracking();
}

void UACECharacterAppearanceComponent::SetHeldActionMotion(int32 InActionCommand, int32 Style)
{
	uint32 Cmd = static_cast<uint32>(InActionCommand);
	if (Cmd == 0)
	{
		return;
	}
	if (Cmd <= 0xFFFFu)
	{
		Cmd = ACEMotion::ExpandPackedCommand(static_cast<uint16>(Cmd));
	}
	if (AnimMode == EACEAnimMode::ActionOneShot && bHoldActionFinal && ActionCommand == Cmd)
	{
		return;
	}
	// A held spawn pose is already at its authoritative final frame. Do not blend
	// back from the Setup bind pose on the next animation tick.
	StanceBlendFrom.Reset();
	StanceBlendAlpha = 1.f;
	AnimMode = EACEAnimMode::ActionOneShot;
	bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
	ActionCommand = Cmd;
	bHoldActionFinal = true;
	ActionStyle = static_cast<uint32>(Style);
	if (ActionStyle != 0 && ActionStyle <= 0xFFFFu)
	{
		ActionStyle = ACEMotion::ExpandPackedCommand(static_cast<uint16>(ActionStyle));
	}
	if ((ActionStyle & 0xFF000000u) != 0x80000000u)
	{
		ActionStyle = PreferredStyle != 0 ? PreferredStyle : ACEMotion::StanceNonCombat;
	}
	ActionPlayRate = 1.f;
	bSuppressNextHookDispatch = true;
	AnimTime = 1000.f;
	ResetHookTracking();

	if (!bHasMesh || MotionTableId == 0 || PartMeshes.Num() == 0)
	{
		return;
	}
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return;
	}

	TArray<FTransform> Animated;
	int32 AnimatedCount = 0;
	bool bFinished = false;
	bool bOk = false;
	// Prefer Dead cycle (resting corpse pose); fall back to Ready→Dead link final frame.
	bOk = Dat->EvaluateMotionCommand(
		static_cast<uint32>(MotionTableId), ActionCommand, AnimTime,
		PartMeshes.Num(), Animated, WorldScale, AnimatedCount,
		nullptr, nullptr, ActionStyle, /*bLoop*/ false, &bFinished);
	if (!bOk)
	{
		bOk = Dat->EvaluateMotionLink(
			static_cast<uint32>(MotionTableId), ACEMotion::Ready, ActionCommand, AnimTime,
			PartMeshes.Num(), Animated, WorldScale, AnimatedCount, bFinished,
			nullptr, nullptr, ActionStyle);
	}
	if (!bOk)
	{
		return;
	}
	for (int32 i = 0; i < PartMeshes.Num(); ++i)
	{
		if (i < AnimatedCount && Animated.IsValidIndex(i))
		{
			FTransform PartXform = Animated[i];
			if (BindTransforms.IsValidIndex(i))
			{
				PartXform.SetScale3D(BindTransforms[i].GetScale3D());
			}
			ApplyPartTransform(i, PartXform);
		}
		else if (BindTransforms.IsValidIndex(i))
		{
			ApplyPartTransform(i, BindTransforms[i]);
		}
	}
	RecookPartPhysics();
	const uint64 HeldTrackKey = (5ull << 60)
		| static_cast<uint64>(HashCombine(GetTypeHash(ActionCommand), GetTypeHash(ActionStyle)));
	CommitHookTime(HeldTrackKey, AnimTime);
	bSuppressNextHookDispatch = false;
}

void UACECharacterAppearanceComponent::ApplyBindPartTransforms()
{
	for (int32 i = 0; i < PartMeshes.Num(); ++i)
	{
		if (BindTransforms.IsValidIndex(i))
		{
			ApplyPartTransform(i, BindTransforms[i]);
		}
	}
	RecookPartPhysics();
}

void UACECharacterAppearanceComponent::PlayDoorMotion(int32 ToCommandU16)
{
	const uint32 To = (static_cast<uint32>(ToCommandU16) <= 0xFFFFu)
		? (0x40000000u | (static_cast<uint32>(ToCommandU16) & 0xFFFFu))
		: static_cast<uint32>(ToCommandU16);
	if (To != ACEMotion::On && To != ACEMotion::Off)
	{
		return;
	}
	// Ignore duplicate On/Off while already playing or holding that pose (UpdateMotion can
	// arrive more than once for the same transition).
	if (AnimMode == EACEAnimMode::DoorTransition && DoorToCommand == To)
	{
		return;
	}
	const uint32 From = (To == ACEMotion::On) ? ACEMotion::Off : ACEMotion::On;
	AnimMode = EACEAnimMode::DoorTransition;
	bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
	DoorFromCommand = From;
	DoorToCommand = To;
	bDoorHoldFinal = false;
	bSuppressNextHookDispatch = false;
	AnimTime = 0.f;
	DeferredPoseDeltaTime = 0.f;
	ResetHookTracking();
}

void UACECharacterAppearanceComponent::SetDoorHeldCommand(int32 CommandU16)
{
	const uint32 Raw = static_cast<uint32>(CommandU16);
	const uint32 Cmd = (Raw <= 0xFFFFu)
		? (0x40000000u | (Raw & 0xFFFFu))
		: Raw;
	if (Cmd != ACEMotion::On && Cmd != ACEMotion::Off)
	{
		return;
	}
	AnimMode = EACEAnimMode::DoorTransition;
	bPlayIdleMotion = true;
	SetComponentTickEnabled(true);
	DoorFromCommand = (Cmd == ACEMotion::On) ? ACEMotion::Off : ACEMotion::On;
	DoorToCommand = Cmd;
	bDoorHoldFinal = true;
	bSuppressNextHookDispatch = true;
	AnimTime = 1000.f;
	ResetHookTracking();

	if (!bHasMesh || PartMeshes.Num() == 0)
	{
		return;
	}

	auto RestoreClosedVisuals = [this]()
	{
		SetAppearanceVisible(true);
		if (UACEScriptComponent* Scripts = GetOwner()
			? GetOwner()->FindComponentByClass<UACEScriptComponent>() : nullptr)
		{
			Scripts->RestoreMeshVisuals();
		}
	};

	if (MotionTableId == 0)
	{
		if (Cmd == ACEMotion::Off)
		{
			ApplyBindPartTransforms();
			RestoreClosedVisuals();
		}
		return;
	}
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return;
	}
	TArray<FTransform> Animated;
	int32 AnimatedCount = 0;
	bool bFinished = false;
	bool bUsedLink = true;
	TArray<FACEDatAnimationHook> HeldHooks;
	bool bOk = Dat->EvaluateMotionLink(
		static_cast<uint32>(MotionTableId), DoorFromCommand, DoorToCommand, AnimTime,
		PartMeshes.Num(), Animated, WorldScale, AnimatedCount, bFinished,
		nullptr, &HeldHooks, ACEMotion::StanceNonCombat);
	if (!bOk)
	{
		bUsedLink = false;
		// Do not loop On/Off cycles — a large AnimTime must clamp to the final closed/open frame.
		bOk = Dat->EvaluateMotionCommand(
			static_cast<uint32>(MotionTableId), DoorToCommand, AnimTime,
			PartMeshes.Num(), Animated, WorldScale, AnimatedCount,
			nullptr, &HeldHooks, ACEMotion::StanceNonCombat, /*bLoop*/ false);
	}
	if (!bOk)
	{
		if (Cmd == ACEMotion::Off)
		{
			ApplyBindPartTransforms();
			RestoreClosedVisuals();
		}
		return;
	}
	for (int32 i = 0; i < PartMeshes.Num(); ++i)
	{
		if (i < AnimatedCount && Animated.IsValidIndex(i))
		{
			FTransform PartXform = Animated[i];
			if (BindTransforms.IsValidIndex(i))
			{
				PartXform.SetScale3D(BindTransforms[i].GetScale3D());
			}
			ApplyPartTransform(i, PartXform);
		}
		else if (BindTransforms.IsValidIndex(i))
		{
			ApplyPartTransform(i, BindTransforms[i]);
		}
	}
	RecookPartPhysics();
	const uint64 HeldTrackKey = bUsedLink
		? ((2ull << 60) | static_cast<uint64>(HashCombine(GetTypeHash(DoorFromCommand), GetTypeHash(DoorToCommand))))
		: ((3ull << 60) | static_cast<uint64>(DoorToCommand));
	CommitHookTime(HeldTrackKey, AnimTime);
	// Keep suppress so the first DoorTransition tick cannot replay On-clip NoDraw/Transparent.
	bSuppressNextHookDispatch = true;
	// A late ObjectCreate arrives in a settled state. Apply the completed visual
	// hooks from its transition, without replaying sounds or particle bursts.
	if (UACEScriptComponent* Scripts = GetOwner()->FindComponentByClass<UACEScriptComponent>())
	{
		for (FACEDatAnimationHook Hook : HeldHooks)
		{
			if (Hook.Type == EACEAnimationHookType::TransparentPart
				|| Hook.Type == EACEAnimationHookType::Transparent
				|| Hook.Type == EACEAnimationHookType::Scale
				|| Hook.Type == EACEAnimationHookType::NoDraw)
			{
				Hook.Start=Hook.End; Hook.Time=0.f;
				Scripts->DispatchAnimationHook(Hook);
			}
		}
	}
}

void UACECharacterAppearanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	ACE_PROFILE_SCOPE(Animation);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bHasMesh || PartMeshes.Num() == 0)
	{
		return;
	}
	if (!bPlayIdleMotion)
	{
		SetComponentTickEnabled(false);
		return;
	}
	DeferredPoseDeltaTime += DeltaTime;
	if (AActor* Owner = GetOwner())
	{
		if (!bPreviewCapture && !bPreviewAnimation && !Owner->IsA<AACELoadingScreenActor>() && !Owner->IsA<AACERegionSceneryActor>())
		{
			if (UWorld* World = GetWorld())
			{
				if (const APlayerController* PC = World->GetFirstPlayerController())
				{
					FVector Eye = Owner->GetActorLocation();
					if (PC->PlayerCameraManager)
					{
						Eye = PC->PlayerCameraManager->GetCameraLocation();
					}
					const float DistSq = static_cast<float>(
						FVector::DistSquared(Owner->GetActorLocation(), Eye));
					if (DistSq > FMath::Square(8000.f))
					{
						return;
					}
					if (DistSq > FMath::Square(4000.f)
						&& ((GFrameCounter + Owner->GetUniqueID()) & 3u) != 0u)
					{
						return;
					}
				}
			}
		}
	}
	// Pose evaluation may be throttled at distance; the motion clock must not
	// run at one quarter speed or replay an old idle when the creature approaches.
	DeltaTime = DeferredPoseDeltaTime;
	DeferredPoseDeltaTime = 0.f;
	const bool bNeedsMotionTable = AnimMode != EACEAnimMode::DefaultAnimLoop;
	if (bNeedsMotionTable && MotionTableId == 0)
	{
		return;
	}
	if (AnimMode == EACEAnimMode::DefaultAnimLoop && DefaultAnimationId == 0)
	{
		return;
	}

	if (!(AnimMode == EACEAnimMode::DoorTransition && bDoorHoldFinal)
		&& !(AnimMode == EACEAnimMode::ActionOneShot && bHoldActionFinal))
	{
		const float Rate = (AnimMode == EACEAnimMode::ActionOneShot) ? ActionPlayRate : LocomotionPlayRate;
		AnimTime += DeltaTime * Rate;
	}
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (!GI)
	{
		return;
	}
	UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>();
	if (!Dat)
	{
		return;
	}

	if (AnimMode == EACEAnimMode::DefaultAnimLoop)
	{
		TickObjectAnimFrame(DeltaTime);
		TArray<FTransform> Animated;
		TArray<FACEDatAnimationHook> Hooks;
		int32 AnimatedCount = 0;
		const uint64 TrackKey = (1ull << 60) | static_cast<uint32>(DefaultAnimationId);
		const float* PreviousTime = GetPreviousHookTime(TrackKey, AnimTime);
		const float AnimFramerate = (SetupId == 0x02000306) ? 40.f : 30.f;
		const int32 AnimLowFrame = bPreviewAnimation ? PreviewStartFrame : (SetupId == 0x02000306) ? 1 : 0;
		if (Dat->EvaluateAnimationLoop(static_cast<uint32>(DefaultAnimationId), AnimTime, PartMeshes.Num(), Animated, WorldScale, AnimatedCount,
			PreviousTime, &Hooks, AnimFramerate, AnimLowFrame))
		{
			CommitHookTime(TrackKey, AnimTime);
			ApplyDefaultAnimPartTransforms(Animated, AnimatedCount);
			DispatchCrossedHooks(Hooks);
		}
		return;
	}

	if (AnimMode == EACEAnimMode::ActionOneShot)
	{
		TArray<FTransform> Animated;
		TArray<FACEDatAnimationHook> Hooks;
		int32 AnimatedCount = 0;
		bool bFinished = false;
		const uint64 TrackKey = (5ull << 60)
			| static_cast<uint64>(HashCombine(GetTypeHash(ActionCommand), GetTypeHash(ActionStyle)));
		const float* PreviousTime = bSuppressNextHookDispatch ? nullptr : GetPreviousHookTime(TrackKey, AnimTime);
		// Dead must play Ready→Dead fall (Link) then hold; Dead cycle alone is the resting pose.
		// Pickup (drop / loot) also uses Ready→Pickup Links — Cycles finish too quickly vs ACE
		// GetAnimationLength(stance, Pickup, Ready) which times the server delay.
		// Other 0x4000/0x4100 actions prefer Cycles (Sit, Sleep, …). Magic cast gestures'
		// Cycles are framerate-0 end holds — prefer Ready→gesture Links so CastSpell /
		// MagicThrowMissile / etc. play the real cast (level windups already Link-first).
		const uint32 Family = ActionCommand & 0xF0000000u;
		const uint32 ActionFam8 = ActionCommand & 0xFF000000u;
		const bool bIsDead = ActionCommand == ACEMotion::Dead;
		const uint16 ActionLow = static_cast<uint16>(ActionCommand);
		const bool bIsPickup = ActionLow == 0x0018
			|| ActionLow == 0x0136 || ActionLow == 0x0137
			|| ActionLow == 0x0138 || ActionLow == 0x0139;
		const bool bIsMagicCast = (ActionCommand >= 0x4000002Bu && ActionCommand <= 0x40000039u)
			|| ActionCommand == 0x400000D3u
			|| ActionCommand == 0x400000E0u
			|| ActionCommand == 0x400000E1u;
		// Aim/Reload cycles are endpoints, just like cast gestures. Their
		// Ready -> action links contain the actual timed missile animation.
		const bool bIsMissileGesture = ActionCommand == 0x40000016u
			|| (ActionCommand >= 0x4000001Eu && ActionCommand <= 0x4000002Au);
		// Consumables likewise have a held endpoint cycle; the link is the bite/sip.
		const bool bIsConsumable = ActionLow == 0x001A || ActionLow == 0x001B;
		const bool bIsChatPose = ActionFam8 == 0x13000000u || ActionFam8 == 0x42000000u
			|| ActionFam8 == 0x43000000u;
		const bool bIsRestHold = ACEMotion::IsHeldRestCommand(ActionCommand);
		const bool bIsJump = ActionCommand == 0x1000004Bu || ActionCommand == 0x40000015u || ActionCommand == 0x4000001Du;
		// ChatPose / soul emotes (Point, PossumState, …) must play Ready→action Links then
		// hang. Preferring Cycles for 0x4300 made *playpossum* a 1-frame snap that never held.
		// Sleeping/Sitting/Crouch also hold — but spawn uses SetHeldActionMotion (final frame).
		const bool bPreferCycle = bHoldActionFinal || (!bIsDead && !bIsPickup && !bIsMagicCast && !bIsMissileGesture && !bIsConsumable && !bIsChatPose && !bIsRestHold && !bIsJump
			&& (Family == 0x40000000u || Family == 0x41000000u));
		bool bOk = false;
		if (bPreferCycle)
		{
			bOk = Dat->EvaluateMotionCommand(
				static_cast<uint32>(MotionTableId), ActionCommand, AnimTime,
				PartMeshes.Num(), Animated, WorldScale, AnimatedCount,
				PreviousTime, &Hooks, ActionStyle, /*bLoop*/ false, &bFinished);
		}
		if (!bOk)
		{
			bOk = Dat->EvaluateMotionLink(
				static_cast<uint32>(MotionTableId), ACEMotion::Ready, ActionCommand, AnimTime,
				PartMeshes.Num(), Animated, WorldScale, AnimatedCount, bFinished,
				PreviousTime, &Hooks, ActionStyle);
		}
		if (!bOk && bIsDead && ActionStyle != ACEMotion::StanceNonCombat)
		{
			bOk = Dat->EvaluateMotionLink(
				static_cast<uint32>(MotionTableId), ACEMotion::Ready, ActionCommand, AnimTime,
				PartMeshes.Num(), Animated, WorldScale, AnimatedCount, bFinished,
				PreviousTime, &Hooks, ACEMotion::StanceNonCombat);
		}
		if (!bOk && !bPreferCycle)
		{
			bOk = Dat->EvaluateMotionCommand(
				static_cast<uint32>(MotionTableId), ActionCommand, AnimTime,
				PartMeshes.Num(), Animated, WorldScale, AnimatedCount,
				PreviousTime, &Hooks, ActionStyle, /*bLoop*/ false, &bFinished);
		}
		if (!bOk && bIsDead && ActionStyle != ACEMotion::StanceNonCombat)
		{
			bOk = Dat->EvaluateMotionCommand(
				static_cast<uint32>(MotionTableId), ActionCommand, AnimTime,
				PartMeshes.Num(), Animated, WorldScale, AnimatedCount,
				PreviousTime, &Hooks, ACEMotion::StanceNonCombat, /*bLoop*/ false, &bFinished);
		}
		if (bOk)
		{
			bActionEverEvaluated = true;
			CommitHookTime(TrackKey, AnimTime);
			bSuppressNextHookDispatch = false;
			ApplyAnimatedPartsWithBlend(Animated, AnimatedCount, DeltaTime);
			DispatchCrossedHooks(Hooks);
		}
		// Never abandon a magic windup/cast on a single failed eval (MotionTable swap mid-tick
		// used to skip PowerUp instantly → every spell looked like a bare L1 cast gesture).
		const bool bMagicAction = (ActionCommand >= 0x1000006Fu && ActionCommand <= 0x10000078u)
			|| (ActionCommand >= 0x1000012Bu && ActionCommand <= 0x10000134u)
			|| (ActionCommand >= 0x4000002Bu && ActionCommand <= 0x40000039u)
			|| ActionCommand == 0x400000D3u || ActionCommand == 0x400000E0u || ActionCommand == 0x400000E1u;
		const uint32 ActionFam = ActionCommand & 0xFF000000u;
		const bool bChatPoseAction = ActionFam == 0x13000000u || ActionFam == 0x42000000u
			|| ActionFam == 0x43000000u;
		// Chat poses / magic: give Link time to resolve before giving up.
		const bool bGiveUpFailed = !bOk
			&& !(bMagicAction && AnimTime < 2.f)
			&& !(bChatPoseAction && AnimTime < 3.f);
		if ((bFinished || bGiveUpFailed) && !bHoldActionFinal)
		{
			const bool bLeavingCast = ACEIsMagicCastCommand(ActionCommand);
			if (bIsDead && bOk && bFinished)
			{
				// Keep the final death frame until ObjectDelete / corpse replace.
				bHoldActionFinal = true;
			}
			else if (bHoldActionFinalAfterFinish && (bFinished || bActionEverEvaluated) && bOk)
			{
				bHoldActionFinal = true;
				bHoldActionFinalAfterFinish = false;
			}
			else if (bHoldActionFinalAfterFinish && !bOk && bActionEverEvaluated)
			{
				// Evaluation dropped out past the last frame — freeze the pose we already
				// applied rather than snapping back to locomotion Ready (chat-pose hold).
				bHoldActionFinal = true;
				bHoldActionFinalAfterFinish = false;
			}
			else if (PendingActionCommands.Num() > 0)
			{
				const uint32 Next = PendingActionCommands[0];
				const uint32 NextStyle = PendingActionStyles.IsValidIndex(0) ? PendingActionStyles[0] : 0u;
				const float NextRate = PendingActionPlayRates.IsValidIndex(0) ? PendingActionPlayRates[0] : 1.f;
				const bool bNextHold = PendingActionHolds.IsValidIndex(0) && PendingActionHolds[0];
				PendingActionCommands.RemoveAt(0);
				if (PendingActionStyles.Num() > 0) { PendingActionStyles.RemoveAt(0); }
				if (PendingActionPlayRates.Num() > 0) { PendingActionPlayRates.RemoveAt(0); }
				if (PendingActionHolds.Num() > 0) { PendingActionHolds.RemoveAt(0); }
				// Start next without wiping the rest of the scarab/cast queue.
				BeginPoseBlendFromCurrent();
				AnimMode = EACEAnimMode::ActionOneShot;
				ActionCommand = Next;
				bHoldActionFinal = false;
				bHoldActionFinalAfterFinish = bNextHold;
				ActionStyle = NextStyle;
				if (ActionStyle != 0 && ActionStyle <= 0xFFFFu)
				{
					ActionStyle = ACEMotion::ExpandPackedCommand(static_cast<uint16>(ActionStyle));
				}
				if ((ActionStyle & 0xFF000000u) != 0x80000000u)
				{
					ActionStyle = PreferredStyle != 0 ? PreferredStyle : ACEMotion::StanceNonCombat;
				}
				const bool bNextPowerUp = (Next >= 0x1000006Fu && Next <= 0x10000078u)
					|| (Next >= 0x1000012Bu && Next <= 0x10000134u);
				const bool bNextCast = (Next >= 0x4000002Bu && Next <= 0x40000039u)
					|| Next == 0x400000D3u || Next == 0x400000E0u || Next == 0x400000E1u;
				if ((bNextPowerUp || bNextCast)
					&& (ActionStyle == 0 || ActionStyle == ACEMotion::StanceNonCombat))
				{
					ActionStyle = ACEMotion::StanceMagic;
				}
				ActionPlayRate = FMath::Max(0.05f, NextRate);
				bActionEverEvaluated = false;
				AnimTime = 0.f;
				DeferredPoseDeltaTime = 0.f;
				bSuppressNextHookDispatch = false;
				ResetHookTracking();
				ACEStopCastGestureIfLeaving(GetOwner(), bLeavingCast, bNextCast);
			}
			else if (QueuedHoldAction != 0)
			{
				const uint32 Next = QueuedHoldAction;
				const uint32 NextStyle = QueuedHoldStyle;
				QueuedHoldAction = 0;
				QueuedHoldStyle = 0;
				SetHeldActionMotion(static_cast<int32>(Next), static_cast<int32>(NextStyle));
				ACEStopCastGestureIfLeaving(GetOwner(), bLeavingCast, ACEIsMagicCastCommand(Next));
			}
			else
			{
				BeginPoseBlendFromCurrent();
				ActionCommand = 0;
				AnimTime = 0.f;
				DeferredPoseDeltaTime = 0.f;
				ResetHookTracking();
				if (bResumeDefaultAnimAfterAction && DefaultAnimationId != 0)
				{
					bResumeDefaultAnimAfterAction = false;
					AnimMode = EACEAnimMode::DefaultAnimLoop;
					ObjectAnimFrame = FTransform::Identity;
				}
				else
				{
					AnimMode = EACEAnimMode::Locomotion;
				}
				ACEStopCastGestureIfLeaving(GetOwner(), bLeavingCast, false);
			}
		}
		return;
	}

	if (AnimMode == EACEAnimMode::DoorTransition)
	{
		TArray<FTransform> Animated;
		TArray<FACEDatAnimationHook> Hooks;
		int32 AnimatedCount = 0;
		bool bFinished = false;
		const uint64 LinkTrackKey = (2ull << 60)
			| static_cast<uint64>(HashCombine(GetTypeHash(DoorFromCommand), GetTypeHash(DoorToCommand)));
		uint64 ActiveTrackKey = LinkTrackKey;
		float ActiveEvalTime = AnimTime;
		float SuppressedPreviousTime = AnimTime;
		const float* PreviousTime = bSuppressNextHookDispatch
			? &SuppressedPreviousTime
			: GetPreviousHookTime(LinkTrackKey, AnimTime);
		bool bOk = Dat->EvaluateMotionLink(
			static_cast<uint32>(MotionTableId), DoorFromCommand, DoorToCommand, AnimTime,
			PartMeshes.Num(), Animated, WorldScale, AnimatedCount, bFinished, PreviousTime, &Hooks,
			ACEMotion::StanceNonCombat);
		if (!bOk)
		{
			// Fallback: evaluate Cycles entry for On/Off if Links missing.
			const float EvalTime = bDoorHoldFinal ? 1000.f : AnimTime;
			ActiveEvalTime = EvalTime;
			SuppressedPreviousTime = EvalTime;
			ActiveTrackKey = (3ull << 60) | static_cast<uint64>(DoorToCommand);
			PreviousTime = bSuppressNextHookDispatch
				? &SuppressedPreviousTime
				: GetPreviousHookTime(ActiveTrackKey, EvalTime);
			bOk = Dat->EvaluateMotionCommand(
				static_cast<uint32>(MotionTableId), DoorToCommand, EvalTime,
				PartMeshes.Num(), Animated, WorldScale, AnimatedCount, PreviousTime, &Hooks,
				ACEMotion::StanceNonCombat, /*bLoop*/ false);
			bFinished = bDoorHoldFinal || AnimTime > 2.f;
		}
		if (bOk)
		{
			CommitHookTime(ActiveTrackKey, ActiveEvalTime);
			bSuppressNextHookDispatch = false;
			const bool bJustFinished = bFinished && !bDoorHoldFinal;
			if (bFinished)
			{
				bDoorHoldFinal = true;
			}
			for (int32 i = 0; i < PartMeshes.Num(); ++i)
			{
				if (i < AnimatedCount && Animated.IsValidIndex(i))
				{
					FTransform PartXform = Animated[i];
					if (BindTransforms.IsValidIndex(i))
					{
						PartXform.SetScale3D(BindTransforms[i].GetScale3D());
					}
					ApplyPartTransform(i, PartXform);
				}
				else if (BindTransforms.IsValidIndex(i))
				{
					ApplyPartTransform(i, BindTransforms[i]);
				}
			}
			if (bJustFinished || (bDoorHoldFinal && AnimTime < 0.05f))
			{
				RecookPartPhysics();
			}
			// Held On/Off must not re-dispatch end-clip NoDraw every tick (pedestal vanished).
			if (!bDoorHoldFinal || bJustFinished)
			{
				DispatchCrossedHooks(Hooks);
			}
		}
		return;
	}

	const bool bMoving = !FMath::IsNearlyZero(LocomotionForward) || !FMath::IsNearlyZero(LocomotionStrafe);
	{
		const int8 FwdSign = (LocomotionForward > 0.15f) ? 1
			: (LocomotionForward < -0.15f) ? -1 : 0;
		if (FwdSign != 0 && LastLocomotionFwdSign != 0 && FwdSign != LastLocomotionFwdSign)
		{
			AnimTime = 0.f;
			DeferredPoseDeltaTime = 0.f;
			BeginPoseBlendFromCurrent();
		}
		if (FwdSign != 0)
		{
			LastLocomotionFwdSign = FwdSign;
		}
		else if (!bMoving)
		{
			LastLocomotionFwdSign = 0;
		}
	}
	if (!bSuppressLocoIdleBlend && bMoving != bWasLocomotionMoving)
	{
		BeginPoseBlendFromCurrent();
		bWasLocomotionMoving = bMoving;
	}
	else if (bMoving)
	{
		bWasLocomotionMoving = true;
	}
	const bool bForwardDominant = FMath::Abs(LocomotionForward) + KINDA_SMALL_NUMBER >= FMath::Abs(LocomotionStrafe);
	const float TargetRunBlend = bMoving && bForwardDominant && LocomotionForward > 0.f && bLocomotionRunning ? 1.f : 0.f;
	RunBlend = FMath::FInterpTo(RunBlend, TargetRunBlend, DeltaTime, 5.f);

	constexpr uint32 WalkForward = 0x45000005u;
	constexpr uint32 WalkBackwards = 0x45000006u;
	constexpr uint32 RunForward = 0x44000007u;
	constexpr uint32 SideStepRight = 0x6500000fu;
	constexpr uint32 SideStepLeft = 0x65000010u;
	constexpr uint32 Ready = 0x41000003u;

	TArray<FTransform> Animated;
	TArray<FACEDatAnimationHook> Hooks;
	int32 AnimatedCount = 0;
	bool bOk = false;
	auto MakeTrackKey = [](uint32 Command, bool bReverse)
	{
		return (4ull << 60) | static_cast<uint64>(Command) | (bReverse ? (1ull << 40) : 0ull);
	};
	auto Evaluate = [&](uint32 Command, float Time, TArray<FTransform>& Out, int32& OutCount,
		bool bCollectHooks, TArray<FACEDatAnimationHook>* CollectedHooks = nullptr)
	{
		const uint64 TrackKey = MakeTrackKey(Command, Time < 0.f);
		const float* PreviousTime = bCollectHooks ? GetPreviousHookTime(TrackKey, Time) : nullptr;
		const bool bEvaluated = Dat->EvaluateMotionCommand(
			static_cast<uint32>(MotionTableId), Command, Time,
			PartMeshes.Num(), Out, WorldScale, OutCount,
			PreviousTime, bCollectHooks ? CollectedHooks : nullptr, PreferredStyle);
		if (bEvaluated && bCollectHooks)
		{
			CommitHookTime(TrackKey, Time);
		}
		return bEvaluated;
	};

	if (bMoving && !bForwardDominant)
	{
		// Retail represents left strafe as SideStepRight with negative speed. Reverse the
		// authored right-strafe cycle so MTs without a separate left cycle still animate.
		const bool bLeft = LocomotionStrafe < 0.f;
		bOk = Evaluate(SideStepRight, bLeft ? -AnimTime : AnimTime, Animated, AnimatedCount, true, &Hooks);
		if (!bOk && bLeft)
		{
			bOk = Evaluate(SideStepLeft, AnimTime, Animated, AnimatedCount, true, &Hooks);
		}
	}
	else if (bMoving && LocomotionForward < -KINDA_SMALL_NUMBER)
	{
		// ACE: WalkBackwards canonicalizes to WalkForward at -0.65 playback rate.
		constexpr float BackwardsFactor = 0.65f;
		bOk = Evaluate(WalkForward, -AnimTime * BackwardsFactor, Animated, AnimatedCount, true, &Hooks);
		if (!bOk)
		{
			bOk = Evaluate(WalkBackwards, AnimTime * BackwardsFactor, Animated, AnimatedCount, true, &Hooks);
		}
	}
	else if (bMoving)
	{
		// Never blend walk↔run: mismatched cycle phases mirrored limbs ("flip on repeat").
		// Gait and playback rate are independent: a fast walk remains a walk.
		const bool bWantRun = LocomotionForward > 0.f
			&& bLocomotionRunning;
		if (bWantRun)
		{
			bOk = Evaluate(RunForward, AnimTime, Animated, AnimatedCount, true, &Hooks);
			if (!bOk)
			{
				bOk = Evaluate(WalkForward, AnimTime, Animated, AnimatedCount, true, &Hooks);
			}
		}
		else
		{
			bOk = Evaluate(WalkForward, AnimTime, Animated, AnimatedCount, true, &Hooks);
		}
	}
	if (!bOk)
	{
		const uint64 IdleTrackKey = (5ull << 60) | static_cast<uint32>(MotionTableId)
			| (static_cast<uint64>(PreferredStyle) << 20);
		const float* PreviousTime = GetPreviousHookTime(IdleTrackKey, AnimTime);
		// Prefer stance Ready cycle (HandCombat / Magic / …) so combat idle persists.
		if (PreferredStyle != 0)
		{
			bOk = Dat->EvaluateMotionCommand(
				static_cast<uint32>(MotionTableId), Ready, AnimTime,
				PartMeshes.Num(), Animated, WorldScale, AnimatedCount,
				PreviousTime, &Hooks, PreferredStyle);
		}
		if (!bOk)
		{
			bOk = Dat->EvaluateIdleMotion(static_cast<uint32>(MotionTableId), AnimTime, PartMeshes.Num(), Animated, WorldScale, AnimatedCount,
				PreviousTime, &Hooks);
		}
		if (bOk)
		{
			CommitHookTime(IdleTrackKey, AnimTime);
		}
	}
	if (!bOk)
	{
		return;
	}

	// Stance / loco / action cross-fade toward the newly evaluated pose.
	ApplyAnimatedPartsWithBlend(Animated, AnimatedCount, DeltaTime);
	DispatchCrossedHooks(Hooks);
}

void UACECharacterAppearanceComponent::UpdateVRLowerBody(float Dt)
{
	if (!bVRPoseControlled || !MeshRoot || BindTransforms.Num() < 9 || Dt <= 0.f) return;
	const FVector Position = GetOwner()->GetActorLocation();
	const FVector Delta = bVRLowerBodyReady ? Position - VRPreviousBodyLocation : FVector::ZeroVector;
	VRPreviousBodyLocation = Position; bVRLowerBodyReady = true;
	const float Distance = Delta.Size2D();
	const bool Continuous = Dt < .25f && Delta.SizeSquared() < FMath::Square(200.f);
	const float Speed = Continuous ? Distance / Dt : 0.f;
	if (!Continuous) VRGaitTime = VRGaitBlend = 0.f;
	const bool Moving = Speed > 10.f;
	VRGaitBlend = FMath::FInterpConstantTo(VRGaitBlend, Moving ? 1.f : 0.f, Dt, 7.f);
	const FVector Local = MeshRoot->GetComponentQuat().UnrotateVector(Delta);
	const bool Sideways = FMath::Abs(Local.X) > FMath::Abs(Local.Y);
	const bool Run = Speed > WorldScale * 3.5f && !Sideways;
	// Human Setup faces +Y. Distances, not elapsed wall time, advance the gait,
	// so collisions and server corrections cannot leave feet running in place.
	const bool Reverse = Sideways ? Local.X > 0.f : Local.Y < 0.f;
	if (Moving) VRGaitTime += (Reverse ? -1.f : 1.f) * Distance / (WorldScale * (Run ? 4.f : 3.12f));
	TArray<FTransform> Animated; int32 Count = 0;
	auto* Dat = GetWorld()->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
	const uint32 Command = Sideways ? 0x6500000fu : Run ? 0x44000007u : 0x45000005u;
	const bool Evaluated = VRGaitBlend > 0.f && Dat && Dat->EvaluateMotionCommand(
		MotionTableId, Command, VRGaitTime, PartMeshes.Num(), Animated, WorldScale, Count,
		nullptr, nullptr, ACEMotion::StanceNonCombat) && Count >= 9 && Animated.Num() >= 9;
	for (int32 I = 0; I < 9; ++I)
	{
		if (!PartMeshes.IsValidIndex(I) || !PartMeshes[I]) continue;
		FTransform Pose = BindTransforms[I];
		if (I > 0 && Evaluated)
		{
			FTransform Walking = Animated[I].GetRelativeTransform(Animated[0]) * BindTransforms[0];
			Walking.SetScale3D(BindTransforms[I].GetScale3D());
			Pose.Blend(BindTransforms[I], Walking, VRGaitBlend);
		}
		PartMeshes[I]->SetRelativeTransform(Pose);
	}
}
