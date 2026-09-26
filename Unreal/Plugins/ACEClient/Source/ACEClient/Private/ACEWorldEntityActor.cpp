#include "ACEWorldEntityActor.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRRemoteAvatarComponent.h"
#include "ACEProfiling.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEScriptComponent.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "ACELandblockActor.h"
#include "ACEEnvCellActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PrimitiveComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "ACEBodySweep.h"

namespace
{
	bool IsRetailPortalEffectSetup(uint32 SetupId)
	{
		switch (SetupId)
		{
		case 0x020001B3u: // Purple
		case 0x020005D2u: // Blue
		case 0x020005D3u: // Green
		case 0x020005D4u: // Orange
		case 0x020005D5u: // Red
		case 0x020005D6u: // Yellow
		case 0x020006F4u: // White
		case 0x020008FDu: // Shadow
		case 0x02000F2Eu: // Broken
		case 0x020019E4u: // Destroyed
			return true;
		default:
			return false;
		}
	}

	void HideCollisionVisuals(UCapsuleComponent* Proxy, UTextRenderComponent* Label)
	{
		if (Proxy)
		{
			Proxy->SetHiddenInGame(true);
			Proxy->SetVisibility(false);
			Proxy->SetCastShadow(false);
			Proxy->SetRenderInMainPass(false);
			Proxy->SetVisibleInSceneCaptureOnly(false);
		}
		if (Label)
		{
			Label->SetVisibility(false);
			Label->SetHiddenInGame(true);
			Label->SetText(FText::GetEmpty());
		}
	}

	/** Retail CPartArray::GetSelectionSphere — Visibility pick is that sphere (actor scale applies). */
	bool TrySetupSelectionCapsule(
		UACEDatSubsystem* Dat, int32 InSetupId, float WorldScale,
		float& OutRadius, float& OutHalfHeight, FVector& OutCenterRel)
	{
		if (!Dat || InSetupId == 0)
		{
			return false;
		}
		FVector3f SelOriginAc = FVector3f::ZeroVector;
		float SelRadiusAc = 0.f;
		float IgnoredStep = 0.f, IgnoredH = 0.f, IgnoredR = 0.f;
		uint32 IgnoredAnim = 0;
		if (!Dat->TryGetSetupPhysics(
			static_cast<uint32>(InSetupId), IgnoredStep, IgnoredH, IgnoredR, IgnoredAnim,
			nullptr, &SelOriginAc, &SelRadiusAc)
			|| SelRadiusAc <= 0.05f)
		{
			return false;
		}
		OutCenterRel = FACEPosition::AceVectorToUnreal(
			FVector(SelOriginAc.X, SelOriginAc.Y, SelOriginAc.Z), WorldScale);
		const float R = FMath::Clamp(SelRadiusAc * WorldScale, 8.f, 2500.f);
		OutRadius = R;
		OutHalfHeight = R;
		return true;
	}

	void ExpandPickFromVisualBounds(
		UACECharacterAppearanceComponent* Appearance, const FTransform& ActorXform,
		float& Radius, float& HalfHeight, FVector& CenterRel)
	{
		if (!Appearance)
		{
			return;
		}
		FBox VisualBox(ForceInit);
		if (!Appearance->GetVisualWorldBounds(VisualBox) || VisualBox.IsValid == 0)
		{
			return;
		}
		const FVector Ext = VisualBox.GetExtent();
		const float GfxR = FMath::Max(Ext.X, Ext.Y);
		const float GfxH = Ext.Z;
		if (GfxR <= 1.f && GfxH <= 1.f)
		{
			return;
		}
		if (GfxR > Radius * 1.15f || GfxH > HalfHeight * 1.15f)
		{
			CenterRel = ActorXform.InverseTransformPosition(VisualBox.GetCenter());
			Radius = FMath::Clamp(FMath::Max(Radius, GfxR), 12.f, 600.f);
			HalfHeight = FMath::Clamp(FMath::Max(HalfHeight, GfxH), 12.f, 600.f);
		}
	}
}

AACEWorldEntityActor::AACEWorldEntityActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	// No engine BasicShapes capsule in UE 5.8 — leave empty. DAT appearance / CollisionProxy
	// own visuals and blocking; this component is only a rare no-DAT fallback (hidden then).
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	CollisionProxy = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionProxy"));
	CollisionProxy->SetupAttachment(Root);
	CollisionProxy->InitCapsuleSize(40.f, 90.f);
	CollisionProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionProxy->SetHiddenInGame(true);
	CollisionProxy->bDrawOnlyIfSelected = true;
	CollisionProxy->SetVisibility(false);
	CollisionProxy->SetCastShadow(false);
	CollisionProxy->SetCanEverAffectNavigation(false);

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(Root);
	NameLabel->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetWorldSize(28.f);
	NameLabel->SetTextRenderColor(FColor::White);
	NameLabel->SetText(FText::GetEmpty());
	// Retail uses cursor hover blurbs — never draw always-on world nameplates.
	NameLabel->SetVisibility(false);
	NameLabel->SetHiddenInGame(true);

	Appearance = CreateDefaultSubobject<UACECharacterAppearanceComponent>(TEXT("Appearance"));
	Appearance->bUseWorldLighting = true;
	Appearance->bHideOwnerMeshes = true;

	ScriptComponent = CreateDefaultSubobject<UACEScriptComponent>(TEXT("ScriptComponent"));
	ScriptComponent->AddTickPrerequisiteComponent(Appearance);
}

void AACEWorldEntityActor::InitializeFromObject(const FACEWorldObject& Object, float InWorldScale, bool bApplyDatAppearance)
{
	const bool ApplyPlacement = !Object.bAppearanceOnlyUpdate || ACEGuid != Object.Guid;
	ACEGuid = Object.Guid;
	ACEName = Object.Name;
	SetupId = Object.SetupId;
	WeenieClassId = Object.WeenieClassId;
	MotionTableId = Object.MotionTableId;
	ItemType = Object.ItemType;
	InitialMotionCommand = Object.InitialMotionCommand;
	ObjectDescriptionFlags = Object.ObjectDescriptionFlags;
	PhysicsState = Object.PhysicsState;
	UseRadius = Object.UseRadius;
	bIsSelf = Object.bIsSelf;
	bIsPlayer = Object.bIsPlayer;
	if (bIsPlayer && !bIsSelf && !FindComponentByClass<UACEVRRemoteAvatarComponent>())
	{
		auto* Remote = NewObject<UACEVRRemoteAvatarComponent>(this);
		AddInstanceComponent(Remote); Remote->RegisterComponent();
		Remote->AddTickPrerequisiteActor(this);
		Remote->AddTickPrerequisiteComponent(Appearance);
		ScriptComponent->AddTickPrerequisiteComponent(Remote);
	}
	bReceivedDeathMotion = Object.bDying;
	ParentGuid = Object.ParentGuid;
	ParentLocation = Object.ParentLocation;
	CurrentWieldedLocation = Object.CurrentWieldedLocation;
	WielderId = Object.WielderId;
	WorldScale = InWorldScale;
	ProjectileAmmoType = Object.AmmoType;
	// Keep existing DAT mesh across ObjDesc rebroadcasts — clearing bUsingDatMesh flashed the
	// placeholder capsule every clothing/palette tick (other players blinking).
	const bool bKeepMesh = Appearance && Appearance->HasAppearance()
		&& Appearance->GetSetupId() == Object.SetupId;
	if (!bKeepMesh)
	{
		bUsingDatMesh = false;
		bParticleOnlyAppearance = false;
	}
	if (ParentGuid == 0 && GetAttachParentActor())
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	bAttachedToParent = false;

	// Local player must never exist as a world entity (duplicate body + blocks movement).
	if (Object.bIsSelf)
	{
		Mesh->SetVisibility(false);
		NameLabel->SetVisibility(false);
		DisableAllCollision();
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		return;
	}

	HideCollisionVisuals(CollisionProxy, NameLabel);

	const float S = FMath::Clamp(Object.Scale, 0.25f, 4.f);
	HeldWeenieScale = S;
	SetActorScale3D(FVector(S));

	// Script state first — default PhysicsScripts arm only after BindTransforms exist.
	if (ScriptComponent)
	{
		ScriptComponent->InitializeFromObject(Object, WorldScale);
	}

	if (bApplyDatAppearance)
	{
		ApplyDatAppearanceFromObject(Object); // NotifyAppearanceReady inside
	}
	else if (Object.SetupId == 0 && ScriptComponent)
	{
		ScriptComponent->NotifyAppearanceReady(Object.UsesOnOffMotion());
	}

	// Freestanding world objects block the player; ethereal / missiles / corpses / self never do.
	const bool bWorldCollide = ParentGuid == 0 && !Object.bIsSelf && !Object.IsEthereal()
		&& !Object.IsCorpse()
		&& (Object.PhysicsState & ACEPhysicsState::Missile) == 0;
	ConfigureWorldCollision(bWorldCollide);
	if (bWorldCollide && Mesh)
	{
		// Keep the capsule as an invisible collision proxy when DAT mesh is showing.
		Mesh->SetVisibility(!bUsingDatMesh && ParentGuid == 0);
	}
	else
	{
		Mesh->SetVisibility(ParentGuid == 0 && !bUsingDatMesh);
	}

	if (Object.bIsPlayer && Mesh)
	{
		Mesh->SetVectorParameterValueOnMaterials(TEXT("Color"), FVector(0.2f, 0.6f, 1.f));
	}

	if (ParentGuid == 0 && Object.bHasPosition && ApplyPlacement)
	{
		SetActorHiddenInGame(!bCellVisible);
		// Carry CreateObject velocity into the position update. Pre-applying it
		// initializes prediction at the old actor position and suppresses spawn snap.
		AcePhysicsOmega = Object.Omega;
		FACEPosition SpawnPosition = Object.Position;
		if (Object.bHasVelocity)
		{
			SpawnPosition.bHasVelocity = true;
			SpawnPosition.Velocity = Object.Velocity;
		}
		ApplyACEPosition(SpawnPosition);
	}
	else if (ParentGuid != 0)
	{
		ConfigureWorldCollision(false);
		Mesh->SetVisibility(false);
		NameLabel->SetVisibility(false);
		// Stay hidden until AttachToParentActor succeeds — prevents orphan meshes at feet.
		SetActorHiddenInGame(true);
	}

	// ApplyACEPosition already consumes descriptor velocity and resolves the
	// grounded flag. Reapplying it here made grounded runners ballistic again.
	if (Object.bHasVelocity && ApplyPlacement && !Object.bHasPosition && ParentGuid == 0)
	{
		ApplyPhysicsVelocity(Object.Velocity, Object.Omega);
	}
	// Apply after proxy/collision setup as well as after a deferred DAT mesh cook.
	// Those paths must not restore server-invisible marker/creature meshes.
	ApplyPhysicsState(PhysicsState);
	RefreshActorTickEnabled();
}

void AACEWorldEntityActor::DisableAllCollision()
{
	TArray<UPrimitiveComponent*> Prims;
	GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (Prim)
		{
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Prim->SetGenerateOverlapEvents(false);
			Prim->CanCharacterStepUpOn = ECB_No;
		}
	}
}

void AACEWorldEntityActor::ConfigureAttachedPickCollision()
{
	// Wielded weapons stay ethereal to the player but remain Visibility-queryable for
	// select / right-click identify (retail allows targeting held items).
	SetActorEnableCollision(bCellVisible && !bReceivedDeathMotion);
	DisableAllCollision();
	if (Mesh)
	{
		Mesh->SetVisibility(false);
		Mesh->SetHiddenInGame(true);
	}
	HideCollisionVisuals(CollisionProxy, NameLabel);
	if (Appearance && bUsingDatMesh)
	{
		Appearance->ConfigurePartCollision(false, /*bQueryVisibilityOnly*/ true);
	}
	if (CollisionProxy)
	{
		float HalfHeight = 40.f;
		float Radius = 12.f;
		FVector CapsuleCenterRel = FVector(0.f, 0.f, HalfHeight);
		UACEDatSubsystem* Dat = nullptr;
		if (UGameInstance* GI = GetGameInstance())
		{
			Dat = GI->GetSubsystem<UACEDatSubsystem>();
		}
		if (!TrySetupSelectionCapsule(Dat, SetupId, WorldScale, Radius, HalfHeight, CapsuleCenterRel))
		{
			FBox VisualBox(ForceInit);
			if (Appearance && Appearance->GetVisualWorldBounds(VisualBox) && VisualBox.IsValid)
			{
				const FVector Ext = VisualBox.GetExtent();
				CapsuleCenterRel = GetActorTransform().InverseTransformPosition(VisualBox.GetCenter());
				Radius = FMath::Clamp(FMath::Max(Ext.X, Ext.Y), 6.f, 40.f);
				HalfHeight = FMath::Clamp(Ext.Z, 10.f, 110.f);
			}
			else if (Dat)
			{
				float StepAc = 0.5f, HeightAc = 1.f, RadiusAc = 0.2f;
				uint32 IgnoredAnim = 0;
				Dat->TryGetSetupPhysics(static_cast<uint32>(SetupId), StepAc, HeightAc, RadiusAc, IgnoredAnim);
				HalfHeight = FMath::Clamp(HeightAc * WorldScale * 0.5f, 10.f, 110.f);
				Radius = FMath::Clamp(RadiusAc * WorldScale, 6.f, 40.f);
				CapsuleCenterRel = FVector(0.f, 0.f, HalfHeight);
			}
		}
		CollisionProxy->SetRelativeLocation(CapsuleCenterRel);
		CollisionProxy->SetCapsuleSize(Radius, HalfHeight);
		CollisionProxy->SetGenerateOverlapEvents(false);
		CollisionProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CollisionProxy->SetCollisionObjectType(ECC_WorldDynamic);
		CollisionProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
		CollisionProxy->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		CollisionProxy->CanCharacterStepUpOn = ECB_No;
		HideCollisionVisuals(CollisionProxy, NameLabel);
	}
}

void AACEWorldEntityActor::ConfigureWorldCollision(bool bEnable)
{
	// Self never participates in world pick or physics.
	if (bIsSelf)
	{
		DisableAllCollision();
		if (Appearance)
		{
			Appearance->ConfigurePartCollision(false, false);
		}
		SetActorEnableCollision(false);
		return;
	}
	// Attached / parented weapons: pick-only (no blocking).
	if (ParentGuid != 0 || bAttachedToParent)
	{
		ConfigureAttachedPickCollision();
		return;
	}

	SetActorEnableCollision(bCellVisible && !bReceivedDeathMotion);
	if (Mesh)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	float StepAc = 0.5f, HeightAc = 2.f, RadiusAc = 0.75f;
	uint32 IgnoredAnim = 0;
	UACEDatSubsystem* Dat = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		Dat = GI->GetSubsystem<UACEDatSubsystem>();
		if (Dat)
		{
			Dat->TryGetSetupPhysics(static_cast<uint32>(SetupId), StepAc, HeightAc, RadiusAc, IgnoredAnim);
		}
	}
	// Ground loot / tiny props often author near-zero cylinders — keep a usable pick volume.
	float HalfHeight = FMath::Max(25.f, HeightAc * WorldScale * 0.5f);
	float Radius = FMath::Max(25.f, RadiusAc * WorldScale);
	MovementRadius = FMath::Max(1.f, RadiusAc * WorldScale);
	MovementStepHeight = FMath::Max(0.f, StepAc * WorldScale);
	MeleeBodyHeight = FMath::Max(0.f, HeightAc * WorldScale);
	MovementHalfHeight = FMath::Max(MovementRadius, HeightAc * WorldScale * .5f);
	// Doors/chests often author wide Setup cylinders that steal clicks from nearby objects.
	if (IsDoor() || IsOpenable())
	{
		Radius = FMath::Clamp(RadiusAc * WorldScale, 25.f, 55.f);
	}

	const bool bFxOnlySetup = IsRetailPortalEffectSetup(static_cast<uint32>(SetupId));
	const bool bHavePartMesh = Appearance && bUsingDatMesh && !bFxOnlySetup;
	const bool bBlocking = bEnable;
	const bool bCreatureLike = bIsPlayer || (ItemType & ACEItemType::Creature) != 0;
	if (Dat && (CollisionSetupId != uint32(SetupId) || !FMath::IsNearlyEqual(CollisionWorldScale, WorldScale)))
	{
		for (UPrimitiveComponent* Shape : AuthoredCollision) if (Shape) Shape->DestroyComponent();
		AuthoredCollision.Reset();
		CollisionSetupId = SetupId; CollisionWorldScale = WorldScale;
		TArray<FACEDatCollisionShape> Shapes;
		// Retain the fallback even when Setup advertises BSP: only actual part
		// physics geometry (including model replacements) can supersede it.
		if (Dat->GetSetupCollisionShapes(SetupId, Shapes, bSetupHasPhysicsBSP))
			for (const auto& Shape : Shapes)
			{
				if (!FMath::IsFinite(Shape.Radius) || Shape.Radius <= 0.f || !FMath::IsFinite(Shape.Height)) continue;
				UPrimitiveComponent* Body = nullptr;
				if (Shape.Height > 0.f)
				{
					// Retail CCylSphere has flat ends, unlike an Unreal capsule.
					// A 64-sided convex cylinder keeps radial error below 0.13%.
					auto* Cylinder = NewObject<UProceduralMeshComponent>(this);
					Cylinder->bUseComplexAsSimpleCollision = false;
					TArray<FVector> Verts;
					for (int32 I=0; I<64; ++I)
					{
						const float Angle = 2.f * PI * I / 64.f;
						const FVector P(FMath::Cos(Angle)*Shape.Radius*WorldScale, FMath::Sin(Angle)*Shape.Radius*WorldScale, 0);
						Verts.Add(P); Verts.Add(P+FVector(0,0,Shape.Height*WorldScale));
					}
					Cylinder->AddCollisionConvexMesh(Verts);
					Cylinder->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAsComplex;
					Body = Cylinder;
				}
				else
				{
					auto* Sphere = NewObject<USphereComponent>(this);
					Sphere->bDrawOnlyIfSelected = true;
					Sphere->SetSphereRadius(Shape.Radius*WorldScale); Body = Sphere;
				}
				Body->SetupAttachment(Root);
				Body->SetMobility(EComponentMobility::Movable);
				Body->SetRelativeLocation(FACEPosition::AceVectorToUnreal(FVector(Shape.Origin), WorldScale));
				Body->SetVisibility(false); Body->SetHiddenInGame(true);
				Body->SetRenderInMainPass(false); Body->SetRenderInDepthPass(false);
				Body->SetCastShadow(false); Body->ComponentTags.Add(TEXT("ACECollisionOnly"));
				Body->SetGenerateOverlapEvents(false); Body->SetCanEverAffectNavigation(false);
				Body->CanCharacterStepUpOn = ECB_No;
				AddInstanceComponent(Body); Body->RegisterComponent(); AuthoredCollision.Add(Body);
			}
	}
	// Retail CPartArray tests actual gfx physics BSPs before falling back to
	// Setup cylinders/spheres. A model replacement must not enable both bodies.
	const bool bMeshHasPawnCollision = bHavePartMesh && Appearance->HasAuthoredPhysicsGeometry();
	for (UPrimitiveComponent* Body : AuthoredCollision)
	{
		if (bCreatureLike) Body->ComponentTags.AddUnique(TEXT("ACECreatureBody"));
		else Body->ComponentTags.Remove(TEXT("ACECreatureBody"));
		const bool bPkPlayer = bIsPlayer && IsPkOrPkLite();
		Body->SetCollisionEnabled(bBlocking && (bCreatureLike || !bMeshHasPawnCollision)
			? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Body->SetCollisionObjectType(bPkPlayer ? ECC_Pawn : ECC_WorldDynamic);
		Body->SetCollisionResponseToAllChannels(ECR_Ignore);
		Body->SetCollisionResponseToChannel(ECC_Pawn, (!bIsPlayer || bPkPlayer) ? ECR_Block : ECR_Ignore);
	}

	FVector CapsuleCenterRel = FVector(0.f, 0.f, HalfHeight);
	if (!bCreatureLike && TrySetupSelectionCapsule(Dat, SetupId, WorldScale, Radius, HalfHeight, CapsuleCenterRel))
	{
		// Weapons / loot / portals: authored selection sphere (retail pick volume).
	}
	else if (bFxOnlySetup)
	{
		Radius = FMath::Max(Radius, 1.5f * WorldScale);
		HalfHeight = Radius;
		CapsuleCenterRel = FVector(0.f, 0.f, Radius);
	}
	// Setup physics cylinders sit at the hinge/feet — size the visibility pick from the DAT mesh.
	else if ((IsDoor() || IsOpenable()) && bHavePartMesh)
	{
		FBox VisualBox(ForceInit);
		if (Appearance->GetVisualWorldBounds(VisualBox))
		{
			const FVector Ext = VisualBox.GetExtent();
			const FVector WorldCenter = VisualBox.GetCenter();
			CapsuleCenterRel = GetActorTransform().InverseTransformPosition(WorldCenter);
			Radius = FMath::Clamp(FMath::Max(Ext.X, Ext.Y), 35.f, 90.f);
			HalfHeight = FMath::Clamp(Ext.Z, 50.f, 350.f);
		}
	}

	if (!bCreatureLike && bHavePartMesh)
	{
		ExpandPickFromVisualBounds(Appearance, GetActorTransform(), Radius, HalfHeight, CapsuleCenterRel);
	}

	// An empty procedural physics section cannot replace the authored Setup body.
	const bool bDoorOrOpenable = IsDoor() || IsOpenable();
	// Creatures/players: prefer SelectionSphere over physics cylinder (retail pick).
	if (bCreatureLike && TrySetupSelectionCapsule(Dat, SetupId, WorldScale, Radius, HalfHeight, CapsuleCenterRel))
	{
		// Feet-origin actor + SelOriginAc places the capsule on torso/head.
	}
	// Creatures/players: Setup cylinder only (retail cylsphere). Mesh physics on
	// wielded weapons drew capsules and let the pawn occupy the same point.
	const bool bUseMeshPhysics = !bCreatureLike && bHavePartMesh && bMeshHasPawnCollision
		&& (bDoorOrOpenable || bBlocking);
	const bool bProxyBlocks = bBlocking && !bUseMeshPhysics && AuthoredCollision.IsEmpty();
	if (CollisionProxy)
	{
		if (bCreatureLike) CollisionProxy->ComponentTags.AddUnique(TEXT("ACECreatureBody"));
		else CollisionProxy->ComponentTags.Remove(TEXT("ACECreatureBody"));
		CollisionProxy->SetRelativeLocation(CapsuleCenterRel);
		CollisionProxy->SetCapsuleSize(Radius, HalfHeight);
		CollisionProxy->SetGenerateOverlapEvents(false);
		CollisionProxy->SetCanEverAffectNavigation(false);
		HideCollisionVisuals(CollisionProxy, NameLabel);
		if (bUseMeshPhysics)
		{
			CollisionProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			CollisionProxy->SetCollisionObjectType(ECC_WorldDynamic);
			CollisionProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
			CollisionProxy->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			CollisionProxy->CanCharacterStepUpOn = ECB_No;
		}
		else if (bCreatureLike && bProxyBlocks)
		{
			// QueryOnly: still hit by pawn sweeps, but WorldStatic EnvCells cannot depenetrate
			// NPCs onto another floor when indoor collision turns on.
			// Players: PK / PK Lite use ECC_Pawn so they collide with each other; NPK Ignore
			// Pawn. Local NPK Ignore ECC_Pawn so they also walk through PK capsules.
			const bool bPkPlayer = bIsPlayer && IsPkOrPkLite();
			CollisionProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			CollisionProxy->SetCollisionObjectType(bPkPlayer ? ECC_Pawn : ECC_WorldDynamic);
			CollisionProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
			CollisionProxy->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			CollisionProxy->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			CollisionProxy->CanCharacterStepUpOn = ECB_No;
			if (!bIsPlayer || bPkPlayer)
			{
				CollisionProxy->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			}
		}
		else if (bProxyBlocks)
		{
			CollisionProxy->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			CollisionProxy->SetCollisionObjectType(ECC_WorldStatic);
			CollisionProxy->SetCollisionResponseToAllChannels(ECR_Block);
			CollisionProxy->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			CollisionProxy->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			CollisionProxy->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			CollisionProxy->CanCharacterStepUpOn = ECB_No; // props: bump only, no step-up
		}
		else
		{
			CollisionProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			CollisionProxy->SetCollisionObjectType(ECC_WorldDynamic);
			CollisionProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
			CollisionProxy->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			CollisionProxy->CanCharacterStepUpOn = ECB_No;
		}
	}

	if (bHavePartMesh && bDoorOrOpenable)
	{
		Appearance->ConfigurePartCollision(bBlocking, /*bQueryVisibilityOnly*/ !bBlocking,
			/*bSelectOnMesh*/ true);
	}
	else if (bHavePartMesh && bCreatureLike)
	{
		// Retail pick is GetSelectionSphere, not gfx-part tris. Mesh vis on legs-only
		// PhysicsPolygons made give/select work only from the waist down.
		Appearance->ConfigurePartCollision(false, false);
	}
	else if (bHavePartMesh && bBlocking)
	{
		Appearance->ConfigurePartCollision(true, false, false);
	}
	else if (Appearance)
	{
		Appearance->ConfigurePartCollision(false, false);
	}
}

bool AACEWorldEntityActor::ApplyDatAppearanceFromObject(const FACEWorldObject& Object)
{
	if (!Appearance)
	{
		return false;
	}
	const bool bCreatureLike = Object.bIsPlayer
		|| (Object.ItemType & ACEItemType::Creature) != 0;
	// Cook DAT mesh collision for pick even on wielded items. ParentGuid used to skip
	// that cook, so ConfigureAttachedPickCollision had nothing to trace.
	const bool bWantCollision = !Object.bIsSelf && !bCreatureLike;
	// Always cook complex collision for freestanding objects so ethereal doors stay pickable
	// (QueryOnly Visibility) after open — response channels are set in ApplyPhysicsState.
	const uint64 HashBefore = Appearance->GetAppliedAppearanceHash();
	const bool bHadMesh = Appearance->HasAppearance();
	if (Appearance->ApplyWorldObject(Object, WorldScale, bWantCollision))
	{
		bUsingDatMesh = true;
		bParticleOnlyAppearance = false;
		Mesh->SetVisibility(false);
		// Retail portal Setups use 0x0100168B as an invisible particle anchor. Its tiny
		// ClipMap can fall back to a white quad; only the scripted emitters should draw.
		if (IsRetailPortalEffectSetup(static_cast<uint32>(SetupId)))
		{
			Appearance->SetAppearanceVisible(false);
			for (int32 PartIdx = 0;; ++PartIdx)
			{
				USceneComponent* PartScene = Appearance->GetPartMesh(PartIdx);
				UProceduralMeshComponent* Part = Cast<UProceduralMeshComponent>(PartScene);
				if (!Part)
				{
					break;
				}
				Part->ClearAllMeshSections();
				Part->SetVisibility(false);
				Part->SetHiddenInGame(true);
			}
		}
		NameLabel->SetVisibility(false);
		NameLabel->SetHiddenInGame(true);
		if (ParentGuid != 0 || bAttachedToParent)
		{
			ConfigureWorldCollision(false);
		}
		else if (!bIsSelf)
		{
			Mesh->SetVisibility(false);
		}
		// Only (re)arm default scripts on a real mesh cook — hash-skip ObjDesc spam was
		// restarting NoDraw/Transparent loops and blinking other players.
		const bool bMeshRebuilt = !bHadMesh
			|| Appearance->GetAppliedAppearanceHash() != HashBefore;
		const bool bNeedScripts = ScriptComponent
			&& (!ScriptComponent->IsEffectReady() || !ScriptComponent->HasStartedDefaultScripts());
		if (ScriptComponent && (bMeshRebuilt || bNeedScripts))
		{
			ScriptComponent->NotifyAppearanceReady(Object.UsesOnOffMotion());
		}
		ApplyPhysicsState(PhysicsState);
		return true;
	}
	bUsingDatMesh = false;
	Mesh->SetVisibility(ParentGuid == 0 && !IsMeshSuppressed());
	// Particle-only Setups (Font of Jojii 0x0200058E, Gout  PhysicsState.ParticleEmitter)
	// can fail the mesh cook while still needing DefaultScript / DefaultAnim emitters.
	if (ScriptComponent)
	{
		UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
		uint32 DefScript = 0, ScriptTable = 0, SoundTable = 0;
		if (Dat && Object.SetupId != 0)
		{
			Dat->TryGetSetupRuntimeMetadata(static_cast<uint32>(Object.SetupId), DefScript, ScriptTable, SoundTable);
		}
		const bool bParticleObj = Object.WeenieClassId != 16919
			&& ((Object.PhysicsState & ACEPhysicsState::ParticleEmitter) != 0
			|| (Object.PhysicsState & ACEPhysicsState::HasDefaultScript) != 0
			|| DefScript != 0
			|| Object.DefaultAnimationId != 0);
		if (bParticleObj)
		{
			bParticleOnlyAppearance = true;
			if (Appearance)
			{
				Appearance->SetAppearanceVisible(false);
			}
			Mesh->SetVisibility(false);
			ScriptComponent->NotifyAppearanceReady(Object.UsesOnOffMotion());
			return true;
		}
	}
	// Presenter retries appearance; NotifyAppearanceReady runs on success or after give-up.
	return false;
}

void AACEWorldEntityActor::ClearAttachedState()
{
	if (GetAttachParentActor())
	{
		if (auto* ParentAppearance = GetAttachParentActor()->FindComponentByClass<UACECharacterAppearanceComponent>())
			RemoveTickPrerequisiteComponent(ParentAppearance);
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	bAttachedToParent = false;
	bHaveHeldLocalFrame = false;
	HeldParentPartIndex = INDEX_NONE;
	HeldLocalFrame = FTransform::Identity;
	DisableAllCollision();
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AACEWorldEntityActor::AttachToParentActor(AActor* ParentActor, int32 InParentLocation)
{
	if (!ParentActor)
	{
		return;
	}

	ParentLocation = InParentLocation;
	if (auto* VR = ParentActor->FindComponentByClass<UACEVRComponent>(); VR && VR->IsActive())
		AddTickPrerequisiteComponent(VR);
	if (auto* Remote = ParentActor->FindComponentByClass<UACEVRRemoteAvatarComponent>())
		AddTickPrerequisiteComponent(Remote);
	UACECharacterAppearanceComponent* ParentAppearance = ParentActor->FindComponentByClass<UACECharacterAppearanceComponent>();
	if (ParentAppearance)
	{
		ParentAppearance->EnsureMeshRoot();
		// Emit only after the current hand pose has propagated to the weapon.
		AddTickPrerequisiteComponent(ParentAppearance);
	}

	USceneComponent* AttachComp = nullptr;
	FTransform Relative = FTransform::Identity;
	bHaveHeldLocalFrame = false;
	HeldParentPartIndex = INDEX_NONE;
	HeldLocalFrame = FTransform::Identity;

	// Retail: child world = Combine(parentPartFrame, holdingFrame). Attach to the animated
	// part mesh itself so the weapon rides the hand as MotionTable poses update.
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				int32 ParentSetup = 0;
				if (ParentAppearance)
				{
					ParentSetup = ParentAppearance->GetSetupId();
				}
				if (ParentSetup == 0)
				{
					if (UACEClientSubsystem* Client = GI->GetSubsystem<UACEClientSubsystem>())
					{
						if (AACEWorldEntityActor* ParentEntity = Cast<AACEWorldEntityActor>(ParentActor))
						{
							ParentSetup = ParentEntity->SetupId;
						}
						else if (Cast<APawn>(ParentActor))
						{
							FACEWorldObject Self;
							if (Client->GetWorldObject(Client->GetPlayerGuid(), Self))
							{
								ParentSetup = Self.SetupId;
							}
						}
					}
				}

				int32 HoldPart = 0;
				FTransform HoldFrame;
				if (ParentSetup != 0 && Dat->GetHoldingLocation(static_cast<uint32>(ParentSetup), InParentLocation, HoldPart, HoldFrame, WorldScale))
				{
					HeldParentPartIndex = HoldPart;
					HeldLocalFrame = HoldFrame;
					bHaveHeldLocalFrame = true;
					Relative = HoldFrame;
					if (ParentAppearance)
					{
						AttachComp = ParentAppearance->GetPartMesh(HoldPart);
					}
				}
			}
		}
	}

	if (!AttachComp && ParentAppearance)
	{
		AttachComp = ParentAppearance->GetMeshRoot();
		if (bHaveHeldLocalFrame)
		{
			// Part mesh not ready yet — compose against current/bind pose under MeshRoot.
			FTransform PartXform = FTransform::Identity;
			ParentAppearance->GetPartCurrentTransform(HeldParentPartIndex, PartXform);
			// ACE Combine(part, hold) == Unreal (Part * Hold).
			Relative = PartXform * HeldLocalFrame;
		}
	}
	if (!AttachComp)
	{
		AttachComp = ParentActor->GetRootComponent();
	}
	if (!AttachComp)
	{
		return;
	}

	if (!bHaveHeldLocalFrame)
	{
		// Fallback approximate hand sockets (AC meters) if Setup HoldingLocations missing.
		FVector AceOffset = FVector::ZeroVector;
		switch (InParentLocation)
		{
		case 1: case 8: AceOffset = FVector(0.15f, 0.35f, 0.95f); break;
		case 2: case 9: AceOffset = FVector(-0.15f, 0.35f, 0.95f); break;
		case 3: AceOffset = FVector(-0.25f, 0.20f, 0.85f); break;
		case 4: AceOffset = FVector(0.f, 0.10f, 0.55f); break;
		case 5: AceOffset = FVector(-0.15f, -0.20f, 0.90f); break;
		default: AceOffset = FVector(0.f, 0.30f, 0.90f); break;
		}
		Relative = FTransform(FRotator::ZeroRotator, FACEPosition::AceVectorToUnreal(AceOffset, WorldScale));
	}

	AttachToComponent(AttachComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeTransform(Relative);
	SetActorScale3D(FVector(FMath::Clamp(HeldWeenieScale, 0.25f, 4.f)));
	ConfigureAttachedPickCollision();
	Mesh->SetVisibility(false);
	Mesh->SetHiddenInGame(true);
	HideCollisionVisuals(CollisionProxy, NameLabel);
	SetActorHiddenInGame(!bCellVisible);
	HideCollisionVisuals(CollisionProxy, NameLabel);
	bAttachedToParent = true;
	// Equip notifications can arrive while peaceful. The support-hand pose
	// applies immediately, without waiting for a combat-mode animation.
	UpdateHeldAttachmentPose();
	RefreshActorTickEnabled();
}

void AACEWorldEntityActor::UpdateHeldAttachmentPose()
{
	if (!bAttachedToParent)
	{
		return;
	}

	AActor* ParentActor = GetAttachParentActor();
	if (!ParentActor)
	{
		ParentActor = GetOwner();
	}
	if (auto* VR = ParentActor ? ParentActor->FindComponentByClass<UACEVRComponent>() : nullptr; VR && VR->IsActive())
		if (VR->UpdateMissileAttachment(this)) return;
	if (auto* Remote=ParentActor ? ParentActor->FindComponentByClass<UACEVRRemoteAvatarComponent>() : nullptr)
		if (Remote->UpdateMissileAttachment(this)) return;
	if (!bHaveHeldLocalFrame) return;
	UACECharacterAppearanceComponent* ParentAppearance = ParentActor
		? ParentActor->FindComponentByClass<UACECharacterAppearanceComponent>()
		: nullptr;
	if (!ParentAppearance)
	{
		return;
	}

	USceneComponent* DesiredParent = ParentAppearance->GetPartMesh(HeldParentPartIndex);
	if (auto* VR = ParentActor->FindComponentByClass<UACEVRComponent>(); VR && VR->IsActive())
	{
		if (auto* Hand = VR->GetHeldAnchor(ParentLocation, (CurrentWieldedLocation & ACEEquipMask::TwoHanded) != 0)) DesiredParent = Hand;
	}
	else if (auto* Remote = ParentActor->FindComponentByClass<UACEVRRemoteAvatarComponent>())
	{
		if (auto* Hand = Remote->GetHeldAnchor(ParentLocation, (CurrentWieldedLocation & ACEEquipMask::TwoHanded) != 0)) DesiredParent = Hand;
	}
	const bool bAttachedToPart = DesiredParent && GetRootComponent() && GetRootComponent()->GetAttachParent() == DesiredParent;
	if (DesiredParent && !bAttachedToPart)
	{
		// Hand part mesh appeared after first attach — reparent so animation drives the weapon.
		AttachToComponent(DesiredParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		SetActorRelativeTransform(HeldLocalFrame);
		SetActorScale3D(FVector(FMath::Clamp(HeldWeenieScale, 0.25f, 4.f)));
	}
	else if (!DesiredParent)
	{
		// Still waiting on part meshes — keep MeshRoot compose up to date with anim.
		if (USceneComponent* MeshRoot = ParentAppearance->GetMeshRoot())
		{
			if (GetRootComponent() && GetRootComponent()->GetAttachParent() != MeshRoot)
			{
				AttachToComponent(MeshRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			}
			FTransform PartXform = FTransform::Identity;
			ParentAppearance->GetPartCurrentTransform(HeldParentPartIndex, PartXform);
			SetActorRelativeTransform(PartXform * HeldLocalFrame);
			SetActorScale3D(FVector(FMath::Clamp(HeldWeenieScale, 0.25f, 4.f)));
		}
	}
	else
	{
		// The component attachment already propagates the animated hand pose.
		// Reapplying picking collision here rebuilt weapon physics every frame.
		return;
	}

	ConfigureAttachedPickCollision();
}

FVector AACEWorldEntityActor::ResolvePredictedMovement(const FVector& From, const FVector& Destination) const
{
	if (!GetWorld() || IsCorpse() || (PhysicsState & ACEPhysicsState::Missile)
		|| (!bIsPlayer && !(ItemType & ACEItemType::Creature))) return Destination;
	const float Scale = GetActorScale3D().GetAbsMax();
	const float Half = MovementHalfHeight * Scale;
	const auto Shape = FCollisionShape::MakeCapsule(MovementRadius * Scale, Half);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ACERemoteBody), true, this);
	const FVector Offset(0, 0, Half + 1.f);
	FVector Position = From + Offset;
	FVector Remaining = Destination - From;
	for (int32 Pass = 0; Pass < 3 && !Remaining.IsNearlyZero(.01f); ++Pass)
	{
		FHitResult Hit;
		if (!ACEBodySweep::Sweep(*GetWorld(), Hit, Position, Position + Remaining, Shape, Query))
		{
			Position += Remaining;
			break;
		}
		// Never resolve an overlap by teleporting through the next obstruction.
		// The next authoritative position can correct pre-existing penetration.
		if (Hit.bStartPenetrating) break;
		Position += Remaining * FMath::Max(0.f, Hit.Time - .001f);
		Remaining *= 1.f - Hit.Time;
		const FVector Normal = Hit.Normal.GetSafeNormal();
		Remaining -= Normal * FMath::Min(0.0, FVector::DotProduct(Remaining, Normal));
	}
	return Position - Offset;
}

bool AACEWorldEntityActor::ResolveFacingTarget(int32 TargetGuid, FVector& Location) const
{
	const auto* GI = GetGameInstance();
	const auto* Client = GI ? GI->GetSubsystem<UACEClientSubsystem>() : nullptr;
	if (!Client || TargetGuid == 0 || TargetGuid == ACEGuid) return false;
	if (TargetGuid == Client->GetPlayerGuid() && Client->GetPlayerPosition().IsValid())
	{
		Location = Client->GetPlayerPosition().ToUnrealLocation(WorldScale);
		return true;
	}
	FACEWorldObject Target;
	if (!Client->GetWorldObject(TargetGuid, Target) || !Target.bHasPosition || !Target.Position.IsValid()) return false;
	Location = Target.Position.ToUnrealLocation(WorldScale);
	return true;
}

void AACEWorldEntityActor::Tick(float DeltaTime)
{
	ACE_PROFILE_SCOPE(Remote);
	Super::Tick(DeltaTime);
	FACEVRPose VRRoot;
	const auto* VRClient=GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UACEClientSubsystem>() : nullptr;
	const bool HasVRRoot=bIsPlayer && !bIsSelf && VRClient && VRClient->GetSession()
		&& VRClient->GetSession()->GetVRPose(ACEGuid,VRRoot) && VRRoot.Version==2;
	// PostPhysics applies the tracked root once, together with the hands. A
	// second root writer here used to undo grounding/interpolation each frame.
	if (!HasVRRoot) bHaveVRPresentation = false;

	if (!HasVRRoot && bPendingGroundClamp && bRetryGroundClamp && !bAttachedToParent && ParentGuid == 0
		&& !bHavePhysicsVelocity)
	{
		GroundClampRetrySeconds -= DeltaTime;
		FVector Loc = GetActorLocation();
		if (ClampLocationToGround(Loc))
		{
			RemotePredictLocation = Loc;
			RemoteAnchorLocation = Loc;
			SetActorLocation(Loc);
			bPendingGroundClamp = false;
			GroundClampRetrySeconds = 0.f;
		}
		else if (GroundClampRetrySeconds <= 0.f)
		{
			bPendingGroundClamp = false;
		}
	}

	if (!HasVRRoot && !bAttachedToParent && ParentGuid == 0 && bHaveRemotePredict)
	{
		// Prediction and animation consume the same elapsed time. Discarding time
		// below 20 FPS made actors run in place and then snap at the next F748.
		const float Step = FMath::Max(DeltaTime, 0.f);
		const FVector PredictionStart = RemotePredictLocation;
		if (bHavePhysicsVelocity)
		{
			// Spell / missile projectiles: integrate ACE PhysicsObj velocity (optional gravity).
			// ACE PhysicsGlobals.Gravity = 9.8 in AC units/sec² (same magnitude as Earth m/s²).
			const bool bMissile = (PhysicsState & ACEPhysicsState::Missile) != 0;
			const FVector Acceleration = (PhysicsState & ACEPhysicsState::Gravity) != 0
				? FVector(0.f, 0.f, -9.8f) : FVector::ZeroVector;
			// Velocity in ObjectCreate / VectorUpdate is world-space ACE units/sec.
			const FVector UnrealVel = FACEPosition::AceVectorToUnreal(AcePhysicsVelocity, WorldScale);
			RemotePredictLocation += UnrealVel * Step
				+ FACEPosition::AceVectorToUnreal(Acceleration, WorldScale) * (0.5f * Step * Step);
			AcePhysicsVelocity += Acceleration * Step;
			RemoteAnchorLocation = RemotePredictLocation;

			if (!AcePhysicsOmega.IsNearlyZero())
			{
				// Angular velocity is an axial vector: reflection of AC's X axis
				// reverses its handedness as well as its X component.
				const FVector OmegaUe = -FACEPosition::AceVectorToUnreal(AcePhysicsOmega, 1.f);
				const float Angle = OmegaUe.Size() * Step;
				if (Angle > KINDA_SMALL_NUMBER)
				{
					RemotePredictRotation = (FQuat(OmegaUe.GetSafeNormal(), Angle) * RemotePredictRotation).GetNormalized();
				}
			}
			else if (AcePhysicsVelocity.SizeSquared() > 0.01f &&
				(bMissile && (PhysicsState & ACEPhysicsState::AlignPath)))
			{
				// AlignPath: face travel direction (AC forward = +Y → Unreal after X flip).
				const FVector Dir = FACEPosition::AceVectorToUnreal(AcePhysicsVelocity, 1.f).GetSafeNormal();
				if (!Dir.IsNearlyZero())
				{
					// Only AlignPath projectiles face velocity. Creatures may
					// strafe or jump without changing their networked heading.
					RemotePredictRotation = FRotationMatrix::MakeFromYZ(Dir, FVector::UpVector).ToQuat();
				}
			}

			// Creatures/pets are not Unreal pawns — ballistic integrate has no capsule contact.
			// Seat on traced ground so summons don't fall through floors after a jump F748.
			if (!bMissile)
			{
				float GroundZ = 0.f;
				if (TraceGroundZ(RemotePredictLocation, GroundZ, bIsPlayer || (ItemType & ACEItemType::Creature)))
				{
					const float SkinCm = 1.f;
					if (RemotePredictLocation.Z <= GroundZ + SkinCm)
					{
						RemotePredictLocation.Z = GroundZ + SkinCm;
						RemoteAnchorLocation.Z = RemotePredictLocation.Z;
						if (AcePhysicsVelocity.Z < 0.f)
						{
							AcePhysicsVelocity.Z = 0.f;
						}
						// Released inventory retains its resting orientation and stops
						// at contact while awaiting the authoritative settled position.
						// Contact ends ballistic ownership; locomotion resumes from UpdateMotion.
						AcePhysicsVelocity = FVector::ZeroVector;
						if (AcePhysicsVelocity.SizeSquared() < 0.01f)
						{
							bHavePhysicsVelocity = false;
							AcePhysicsVelocity = FVector::ZeroVector;
						}
					}
				}
			}

			RemotePredictLocation = ResolvePredictedMovement(PredictionStart, RemotePredictLocation);
			if (bIsPlayer || (ItemType & ACEItemType::Creature))
			{
				// F748 also corrects airborne actors. Do not bypass presentation
				// smoothing just because a jump/velocity packet is active.
				const float Alpha = 1.f-FMath::Exp(-RemotePositionSmoothing*Step);
				FVector Display = FMath::Lerp(GetActorLocation(), RemotePredictLocation, Alpha);
				if (!bHavePhysicsVelocity) ClampLocationToGround(Display);
				SetActorLocation(Display);
				SetActorRotation(FQuat::Slerp(GetActorQuat(),RemotePredictRotation,Alpha).GetNormalized());
			}
			else
			{
				SetActorLocation(RemotePredictLocation);
				SetActorRotation(RemotePredictRotation);
			}
		}
		else
		{
			// Stock ACE only broadcasts F748 ~1 Hz. UpdateMotion is the dense signal for
			// turn + locomotion — integrate those here so remotes rotate/run continuously
			// instead of jumping once per position packet (~180° at default turn rate).
			FVector Velocity = FVector::ZeroVector;
			if (RemoteMotion.StickyTargetGuid != 0)
			{
				FVector Target;
				if (ResolveFacingTarget(RemoteMotion.StickyTargetGuid, Target))
				{
					const FVector Direction = (Target - RemotePredictLocation).GetSafeNormal2D();
					if (!Direction.IsNearlyZero()) RemotePredictRotation = FACEPosition::QuatFromUnrealTravelDir2D(Direction);
				}
				else RemoteMotion.StickyTargetGuid = 0;
			}
			const bool bMoveTo = RemoteMotion.MovementType == 6 || RemoteMotion.MovementType == 7;
			const bool bTurnTo = RemoteMotion.MovementType == 8 || RemoteMotion.MovementType == 9;

			if (bTurnTo)
			{
				// TurnToObject: face frozen target direction. Never fall back to DesiredHeading=0
				// (that faces due north). TurnToHeading uses ACE degree heading.
				FVector FaceDir = RemoteTurnFaceDir;
				if (!bHaveRemoteTurnFace || !FaceDir.Normalize())
				{
					bHaveRemoteTurnFace = false;
					if (RemoteMotion.MovementType == 8 && RemoteMotion.MoveToTargetGuid != 0)
					{
						if (UWorld* World = GetWorld())
						{
							if (UGameInstance* GI = World->GetGameInstance())
							{
								if (UACEClientSubsystem* Client = GI->GetSubsystem<UACEClientSubsystem>())
								{
									FVector TargetLoc = FVector::ZeroVector;
									bool bHaveTarget = false;
									FACEWorldObject TargetObj;
									if (Client->GetWorldObject(RemoteMotion.MoveToTargetGuid, TargetObj)
										&& TargetObj.bHasPosition && TargetObj.Position.IsValid())
									{
										TargetLoc = TargetObj.Position.ToUnrealLocation(WorldScale);
										bHaveTarget = true;
									}
									if (RemoteMotion.MoveToTargetGuid == Client->GetPlayerGuid())
									{
										const FACEPosition PlayerPos = Client->GetPlayerPosition();
										if (PlayerPos.IsValid())
										{
											TargetLoc = PlayerPos.ToUnrealLocation(WorldScale);
											bHaveTarget = true;
										}
									}
									if (bHaveTarget)
									{
										FaceDir = TargetLoc - RemotePredictLocation;
										FaceDir.Z = 0.f;
									}
								}
							}
						}
					}
					else if (RemoteMotion.MovementType == 9)
					{
						FaceDir = FACEPosition::UnrealDirFromAceHeadingDegrees(
							RemoteMotion.MoveToDesiredHeading);
					}
					if (FaceDir.Normalize())
					{
						RemoteTurnFaceDir = FaceDir;
						bHaveRemoteTurnFace = true;
					}
				}
				if (bHaveRemoteTurnFace && FaceDir.Normalize())
				{
					const FQuat Want = FACEPosition::QuatFromUnrealTravelDir2D(FaceDir);
					const float TurnRate = FMath::Clamp(RemoteMotion.TurnToSpeed, 0.25f, 4.f);
					RemotePredictRotation = FMath::QInterpTo(
						RemotePredictRotation, Want, DeltaTime, TurnRate * 4.f);
					if (Appearance)
					{
						Appearance->SetLocomotionInput(0.f, 0.f, false, 1.f);
					}
					const FVector CurFwd = RemotePredictRotation.RotateVector(FVector::YAxisVector).GetSafeNormal2D();
					if (FVector::DotProduct(CurFwd, FaceDir) >= 0.995f)
					{
						RemotePredictRotation = Want;
						RemoteMotion.MovementType = 0;
						RemoteMotion.MoveToTargetGuid = 0;
						bHaveRemoteTurnFace = false;
					}
				}
			}
			else if (bMoveTo)
			{
				FVector TargetLoc = RemotePredictLocation;
				float TargetRadius = 0.f, TargetHeight = 0.f;
				bool bHaveTarget = false;
				if (RemoteMotion.bHaveMoveToTarget)
				{
					FACEPosition Dest;
					Dest.CellId = RemoteMotion.MoveToCellId;
					Dest.Location = RemoteMotion.MoveToLocalAce;
					TargetLoc = Dest.ToUnrealLocation(WorldScale);
					bHaveTarget = true;
				}
				if (RemoteMotion.MovementType == 6 && RemoteMotion.MoveToTargetGuid != 0)
				{
					if (UWorld* World = GetWorld())
					{
						if (UGameInstance* GI = World->GetGameInstance())
						{
							if (UACEClientSubsystem* Client = GI->GetSubsystem<UACEClientSubsystem>())
							{
								FACEWorldObject TargetObj;
								if (Client->GetWorldObject(RemoteMotion.MoveToTargetGuid, TargetObj))
								{
									if (TargetObj.Position.IsValid())
									{ TargetLoc = TargetObj.Position.ToUnrealLocation(WorldScale); bHaveTarget = true; }
									if (ApproachTargetSetup != uint32(TargetObj.SetupId))
									{
										if (auto* Dat = GI->GetSubsystem<UACEDatSubsystem>())
										{
											float SetupStep, Height, Radius; uint32 Anim;
											if (Dat->TryGetSetupPhysics(TargetObj.SetupId, SetupStep, Height, Radius, Anim))
											{
												ApproachTargetSetup = TargetObj.SetupId;
												ApproachTargetRadiusAc = Radius; ApproachTargetHeightAc = Height;
											}
										}
									}
									if (ApproachTargetSetup == uint32(TargetObj.SetupId))
									{
										const float Scale = (TargetObj.Scale > 0.f ? TargetObj.Scale : 1.f) * WorldScale;
										TargetRadius = ApproachTargetRadiusAc * Scale; TargetHeight = ApproachTargetHeightAc * Scale;
									}
								}
								// WorldObjects retains the last network echo of our avatar;
								// the session pose advances every local movement frame.
								if (RemoteMotion.MoveToTargetGuid == Client->GetPlayerGuid() && Client->GetPlayerPosition().IsValid())
								{
									TargetLoc = Client->GetPlayerPosition().ToUnrealLocation(WorldScale);
									bHaveTarget = true;
								}
							}
						}
					}
				}

				if (bHaveTarget)
				{
					FVector ToTarget = TargetLoc - RemotePredictLocation;
					const float RangeCm = RemoteMotion.ApproachDistance(ToTarget, GetMeleeBodyRadius(),
						GetMeleeBodyHeight(), TargetRadius, TargetHeight);
					ToTarget.Z = 0.f;
					const float DistCm = ToTarget.Size();
					// Reaching the body-edge stopping distance ends translation, not
					// facing. Melee MoveToObject can start with the target already
					// beside/behind us, and must still follow that target's heading.
					if (DistCm > SMALL_NUMBER)
					{
						RemotePredictRotation = FACEPosition::QuatFromUnrealTravelDir2D(ToTarget / DistCm);
					}
					RemoteMotion.UpdateMoveToGait(RangeCm / WorldScale);
					RemoteMotion.ForwardUnitsPerSecond = (RemoteMotion.bRunning ? RemoteRunSpeedAc : RemoteWalkSpeedAc)
						* RemoteMotion.AnimPlayRate;
					const float RemainingCm = RangeCm - FMath::Max(RemoteMotion.MoveToDistance, 0.f) * WorldScale;
					if (RemainingCm > .01f && DistCm > SMALL_NUMBER)
					{
						const FVector Dir = ToTarget / DistCm;
						const float Speed = FMath::Max(RemoteMotion.ForwardUnitsPerSecond, 0.f);
						Velocity = Dir * FMath::Min(Speed * WorldScale, RemainingCm / FMath::Max(Step, SMALL_NUMBER));
						if (Appearance)
						{
							Appearance->SetLocomotionInput(
								1.f, 0.f, RemoteMotion.bRunning,
								FMath::Max(0.05f, RemoteMotion.AnimPlayRate));
						}
					}
					else
					{
						RemoteMotion.bMoving = false;
						RemoteMotion.ForwardUnitsPerSecond = 0.f;
						RemoteMotion.Forward = 0.f;
						if (Appearance)
						{
							Appearance->SetLocomotionInput(0.f, 0.f, false, 1.f);
						}
					}
				}
			}
			else
			{
				if (!FMath::IsNearlyZero(RemoteMotion.Turn))
				{
					const float Rad = FMath::DegreesToRadians(
						-RemoteTurnRateDegrees * RemoteMotion.Turn * DeltaTime);
					const FQuat TurnDelta(FVector::UpVector, Rad);
					RemotePredictRotation = (TurnDelta * RemotePredictRotation).GetNormalized();
				}

				if (RemoteMotion.bMoving
					|| !FMath::IsNearlyZero(RemoteMotion.ForwardUnitsPerSecond)
					|| !FMath::IsNearlyZero(RemoteMotion.StrafeUnitsPerSecond))
				{
					const FVector Forward = RemotePredictRotation.RotateVector(FVector::YAxisVector);
					const FVector Right = RemotePredictRotation.RotateVector(FVector(-1.f, 0.f, 0.f));
					Velocity = (
						Forward * RemoteMotion.ForwardUnitsPerSecond
						+ Right * RemoteMotion.StrafeUnitsPerSecond) * WorldScale;
					if (Appearance)
					{
						const float F = FMath::Clamp(RemoteMotion.Forward, -1.f, 1.f);
						const float S = FMath::Clamp(RemoteMotion.Strafe, -1.f, 1.f);
						const float Fwd = !FMath::IsNearlyZero(F) ? F
							: (RemoteMotion.ForwardUnitsPerSecond > KINDA_SMALL_NUMBER ? 1.f
								: (RemoteMotion.ForwardUnitsPerSecond < -KINDA_SMALL_NUMBER ? -1.f : 0.f));
						Appearance->SetLocomotionInput(
							Fwd, S, RemoteMotion.bRunning,
							FMath::Max(0.05f, RemoteMotion.AnimPlayRate));
					}
				}
			}

			RemotePredictLocation += Velocity * Step;

			// Clamp runaway drift if UpdateMotion stalls without a correcting F748.
			const float MaxDriftCm = RemoteMaxExtrapolate * FMath::Max(
				FMath::Abs(RemoteMotion.ForwardUnitsPerSecond),
				FMath::Abs(RemoteMotion.StrafeUnitsPerSecond) > KINDA_SMALL_NUMBER
					? FMath::Abs(RemoteMotion.StrafeUnitsPerSecond)
					: 4.f) * WorldScale;
			const FVector Drift = RemotePredictLocation - RemoteAnchorLocation;
			if (Drift.SizeSquared() > FMath::Square(FMath::Max(MaxDriftCm, 50.f)))
			{
				RemotePredictLocation = RemoteAnchorLocation
					+ Drift.GetSafeNormal() * FMath::Max(MaxDriftCm, 50.f);
			}

			// Collision constrains extrapolated motion, not correction to a server pose.
			// Sweeping from the displayed (lagging) actor here stranded enemies behind
			// local obstacles while targeting/projectiles/corpses used the server position.
			// Ground at the prediction's own XY before sweeping, otherwise an
			// uphill horizontal sweep repeatedly collides with the rising floor.
			const bool Creature=bIsPlayer || (ItemType & ACEItemType::Creature);
			if (Creature) ClampLocationToGround(RemotePredictLocation,&RemotePredictRotation);
			const FVector Resolved=ResolvePredictedMovement(PredictionStart, RemotePredictLocation);
			const bool XYChanged=FVector::DistSquared2D(Resolved,RemotePredictLocation)>.0001;
			RemotePredictLocation=Resolved;
			if (Creature && XYChanged) ClampLocationToGround(RemotePredictLocation,&RemotePredictRotation);
			const float Alpha = 1.f - FMath::Exp(-RemotePositionSmoothing * DeltaTime);
			FVector NextLoc = FMath::Lerp(GetActorLocation(), RemotePredictLocation, Alpha);
			// Always seat Z for grounded remotes within the band — continuous XY integrate
			// used to leave feet floating between sparse F748s.
			// Match initial placement: Stuck signs/wall props retain the server's Z
			// even while a small network correction keeps their actor ticking.
			if (bClampToGround && (ObjectDescriptionFlags & ACEObjectDescFlag::Stuck) == 0)
			{
				// The displayed point lags prediction. Never copy its terrain Z
				// into a prediction at different XY on a hill.
				ClampLocationToGround(NextLoc,&RemotePredictRotation);
			}
			SetActorLocation(NextLoc);

			FQuat TargetRot = RemotePredictRotation;
			if ((TargetRot | GetActorQuat()) < 0.f)
			{
				TargetRot = TargetRot * -1.f;
			}
			// MoveTo owns the desired facing, but turns are still displayed over time.
			SetActorRotation(FQuat::Slerp(GetActorQuat(), TargetRot, Alpha).GetNormalized());
		}
	}

	if (bAttachedToParent)
	{
		UpdateHeldAttachmentPose();
	}

	RefreshActorTickEnabled();
}

bool AACEWorldEntityActor::ResolveIndoorOccupancy(const FVector& AtLocation, uint32& OutEnvCellId) const
{
	OutEnvCellId = 0;
	// Retail occupancy is the server CellId. Geometric EnvCell tests under outdoor
	// arches seated NPCs on look-in interiors (Collector floating in the gateway).
	if ((static_cast<uint32>(LastAceCellId) & 0xFFFFu) >= 0x0100u)
	{
		OutEnvCellId = static_cast<uint32>(LastAceCellId);
		return true;
	}
	(void)AtLocation;
	return false;
}

void AACEWorldEntityActor::ApplyRemoteVRRoot(const FACEVRPose& Pose, float DeltaTime)
{
	const FVector TrackedRoot = Pose.Root * WorldScale;
	const bool Snap = !bHaveVRPresentation || Pose.Teleport != VRPresentationTeleport
		|| FVector::DistSquared(GetActorLocation(),TrackedRoot) > FMath::Square(RemoteSnapDistance*WorldScale);
	bHaveVRPresentation = true;
	VRPresentationTeleport = Pose.Teleport;
	RemotePredictLocation = RemoteAnchorLocation = TrackedRoot;
	FVector Display = Snap ? TrackedRoot : FMath::Lerp(GetActorLocation(),TrackedRoot,
		1.f-FMath::Exp(-RemotePositionSmoothing*FMath::Max(0.f,DeltaTime)));
	if (!bHavePhysicsVelocity) ClampLocationToGround(Display);
	SetActorLocation(Display);
}

bool AACEWorldEntityActor::TraceGroundZ(const FVector& AtLocation, float& OutGroundZ, bool bCreatureSupport) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Moving feet use a short support probe. A tall ray can choose a shop
	// ceiling/roof instead of the floor under the actor.
	const float UpProbe = bCreatureSupport ? MovementStepHeight*GetActorScale3D().GetAbsMax()+2.f : 4.f*WorldScale;
	const float DownProbe = bCreatureSupport ? GroundClampBandAc*WorldScale : 16.f*WorldScale;
	const FVector Start(AtLocation.X, AtLocation.Y, AtLocation.Z + UpProbe);
	const FVector End(AtLocation.X, AtLocation.Y, AtLocation.Z - DownProbe);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ACERemoteGround), /*bTraceComplex*/ true, this);
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);

	uint32 OccupiedEnvCell = 0;
	const bool bIndoorCell = ResolveIndoorOccupancy(AtLocation, OccupiedEnvCell);

	// Multi-hit: skip fat prop capsules / sign-board scenery so NPCs stand on terrain,
	// not on town-sign boards or Setup cylinders.
	// Prefer the walkable surface nearest the server Z — first-hit from above often lands on
	// EnvCell roofs/ceilings (UpProbe starts above the ceiling), which put vendors on the roof.
	TArray<FHitResult> Hits;
	float BestAbsDelta = TNumericLimits<float>::Max();
	float BestZ = AtLocation.Z;
	bool bFound = false;
	if (World->LineTraceMultiByObjectType(Hits, Start, End, ObjParams, Params))
	{
		for (const FHitResult& Hit : Hits)
		{
			if (!Hit.bBlockingHit || !Hit.Component.IsValid())
			{
				continue;
			}
			if (Cast<AACEWorldEntityActor>(Hit.GetActor()) != nullptr)
			{
				// Other weenies (signs, chests, NPCs) are never floor for ground-clamp.
				continue;
			}
			if (const AACELandblockActor* Lb = Cast<AACELandblockActor>(Hit.GetActor()))
			{
				// Outdoor stabs (Yaraq town sign, flora) use forced draw collision — not walkable floors.
				// Only the heightfield TerrainMesh is ground — and never under indoor cells
				// (pulls dungeon pets/NPCs onto outdoor terrain Z through the floor).
				if ((bIndoorCell && Lb->TerrainMesh == Hit.Component.Get())
					|| (!bCreatureSupport && Lb->TerrainMesh != Hit.Component.Get()))
				{
					continue;
				}
			}
			if (bIndoorCell && Hit.Component.IsValid()
				&& Hit.Component->ComponentTags.Contains(FName(TEXT("ACEOutdoorTerrain"))))
			{
				continue;
			}
			if (const AACEEnvCellActor* Env = Cast<AACEEnvCellActor>(Hit.GetActor()))
			{
				// Outdoor pets/NPCs must never seat on EnvCells — Yaraq dungeon lids under
				// plaza footprints sit meters below grade and yanked summons under the world.
				if (!bIndoorCell && !bCreatureSupport)
				{
					continue;
				}
				// Do not choose another story just because its mesh overlaps in XY.
				if (static_cast<uint32>(Env->EnvCellId) != OccupiedEnvCell)
				{
					// A remote can cross cell portals before its next F748. Accept
					// only a streamed cell whose actual volume contains these feet.
					auto* GI = World->GetGameInstance();
					auto* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
					if (!bCreatureSupport || !Dat || !Dat->FindEnvCellMesh(Env->EnvCellId,WorldScale)
						|| !Dat->IsPointInsideEnvCell(Env->EnvCellId,Hit.ImpactPoint+FVector(0,0,2),WorldScale)) continue;
				}
				// Prefer PhysicsPolygons floors when present — draw CellMesh includes ceilings/roofs.
				if (Env->CellCollisionMesh && Env->CellCollisionMesh->GetNumSections() > 0
					&& Hit.Component.Get() == Env->CellMesh)
				{
					continue;
				}
			}
			// Walls / underside of ceilings are not standable.
			if (Hit.ImpactNormal.Z < 0.45f)
			{
				continue;
			}
			float HitZ = Hit.ImpactPoint.Z;
			// Outdoor heightfield: retail contact is landZ - waterDepth (NPCs were left on the
			// dry surface until they moved, then soft-follow yanked them up to grade).
			if (const AACELandblockActor* LbTerrain = Cast<AACELandblockActor>(Hit.GetActor()))
			{
				if (LbTerrain->TerrainMesh == Hit.Component.Get())
				{
					if (UGameInstance* GI = World->GetGameInstance())
					{
						if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
						{
							HitZ -= Dat->GetOutdoorWaterDepthCm(AtLocation.X, AtLocation.Y, WorldScale);
						}
					}
				}
			}
			else if (Hit.Component.IsValid()
				&& Hit.Component->ComponentTags.Contains(FName(TEXT("ACEOutdoorTerrain"))))
			{
				if (UGameInstance* GI = World->GetGameInstance())
				{
					if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
					{
						HitZ -= Dat->GetOutdoorWaterDepthCm(AtLocation.X, AtLocation.Y, WorldScale);
					}
				}
			}
			if (bIndoorCell && (HitZ - AtLocation.Z) > 1.25f * WorldScale)
			{
				continue;
			}
			const float AbsDelta = FMath::Abs(HitZ - AtLocation.Z);
			if (AbsDelta < BestAbsDelta)
			{
				BestAbsDelta = AbsDelta;
				BestZ = HitZ;
				bFound = true;
			}
		}
	}
	if (bFound)
	{
		OutGroundZ = BestZ;
		return true;
	}

	// Indoor cells: never use outdoor heightfield — that pulls dungeon props to terrain Z.
	uint32 OccupiedEnvCellFallback = 0;
	if (ResolveIndoorOccupancy(AtLocation, OccupiedEnvCellFallback))
	{
		return false;
	}

	// Terrain mesh not streamed here yet — fall back to the outdoor heightfield sampler.
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			float SampledZ = AtLocation.Z;
			if (Dat->SampleOutdoorGroundZ(AtLocation.X, AtLocation.Y, WorldScale, SampledZ))
			{
				OutGroundZ = SampledZ;
				return true;
			}
		}
	}
	return false;
}

void AACEWorldEntityActor::SetSelectionHighlight(float Strength)
{
	if (!Appearance) return;
	for (int32 P=0; P<Appearance->GetPartCount(); ++P)
		if (auto* Part=Cast<UPrimitiveComponent>(Appearance->GetPartMesh(P)))
		{
			const auto& Data=Part->GetCustomPrimitiveData().Data;
			if ((Data.IsValidIndex(1) ? Data[1] : 0.f) != Strength) Part->SetCustomPrimitiveDataFloat(1,Strength);
		}
}

bool AACEWorldEntityActor::FindMeleeContact(const FVector& A, const FVector& B, float& Along) const
{
	FVector Blade, Body;
	FMath::SegmentDistToSegmentSafe(A,B,GetActorLocation(),GetActorLocation()+FVector(0,0,GetMeleeBodyHeight()),Blade,Body);
	bool Hit = FVector::DistSquared(Blade,Body) <= FMath::Square(GetMeleeBodyRadius()+WorldScale*.18f);
	Along = Hit ? float(FVector::Distance(A,Blade)/FMath::Max(1.,FVector::Distance(A,B))) : 1.f;
	if (!Appearance) return Hit;
	for (int32 P=0; P<Appearance->GetPartCount(); ++P)
	{
		auto* Part=Cast<UProceduralMeshComponent>(Appearance->GetPartMesh(P));
		if (!Part || !Part->IsVisible()) continue;
		const FTransform Transform=Part->GetComponentTransform();
		const FVector Start=Transform.InverseTransformPosition(A), End=Transform.InverseTransformPosition(B);
		const FVector Padding=FVector(WorldScale*.18f)/Transform.GetScale3D().GetAbs().ComponentMax(FVector(.001f));
		for (int32 S=0; S<Part->GetNumSections(); ++S)
			if (const auto* Section=Part->GetProcMeshSection(S); Section && Section->bSectionVisible)
			{
				const FBox Bounds=Section->SectionLocalBox.ExpandBy(Padding);
				float Time=0; FVector Position, Normal;
				if (Bounds.IsInsideOrOn(Start) || FMath::LineExtentBoxIntersection(Bounds,Start,End,FVector::ZeroVector,Position,Normal,Time))
				{ Along=FMath::Min(Along,Time); Hit=true; }
			}
	}
	return Hit;
}

FBox AACEWorldEntityActor::GetProjectileContactBounds(float RadiusCm) const
{
	FBox Bounds(ForceInit);
	const float Pad=FMath::Max(0.f,RadiusCm)+WorldScale*.10f;
	if(Appearance) for(int32 P=0;P<Appearance->GetPartCount();++P)
	{
		auto* Part=Cast<UProceduralMeshComponent>(Appearance->GetPartMesh(P));
		if(!Part || !Part->IsVisible()) continue;
		const FTransform& Transform=Part->GetComponentTransform();
		const FVector Padding=FVector(Pad)/Transform.GetScale3D().GetAbs().ComponentMax(FVector(.001f));
		for(int32 S=0;S<Part->GetNumSections();++S)
			if(const auto* Section=Part->GetProcMeshSection(S);Section && Section->bSectionVisible)
				Bounds+=Section->SectionLocalBox.ExpandBy(Padding).TransformBy(Transform);
	}
	if(!Bounds.IsValid)
	{
		const float R=GetMeleeBodyRadius();
		Bounds=FBox(GetActorLocation()-FVector(R,R,0),GetActorLocation()+FVector(R,R,GetMeleeBodyHeight())).ExpandBy(Pad);
	}
	return Bounds;
}

bool AACEWorldEntityActor::FindProjectileContact(const FVector& A, const FVector& B, float RadiusCm, float& Along) const
{
	Along=1.f; bool Hit=false, HasGeometry=false;
	const float Pad=FMath::Max(0.f,RadiusCm)+WorldScale*.10f;
	if (Appearance) for (int32 P=0; P<Appearance->GetPartCount(); ++P)
	{
		auto* Part=Cast<UProceduralMeshComponent>(Appearance->GetPartMesh(P));
		if (!Part || !Part->IsVisible()) continue;
		const FTransform Transform=Part->GetComponentTransform();
		const FVector Start=Transform.InverseTransformPosition(A), End=Transform.InverseTransformPosition(B);
		const FVector Padding=FVector(Pad)/Transform.GetScale3D().GetAbs().ComponentMax(FVector(.001f));
		for (int32 S=0; S<Part->GetNumSections(); ++S)
			if (const auto* Section=Part->GetProcMeshSection(S); Section && Section->bSectionVisible)
			{
				HasGeometry=true; const FBox Bounds=Section->SectionLocalBox.ExpandBy(Padding);
				float Time=0; FVector Position, Normal;
				if (Bounds.IsInsideOrOn(Start) || FMath::LineExtentBoxIntersection(Bounds,Start,End,FVector::ZeroVector,Position,Normal,Time))
				{ Along=FMath::Min(Along,Time); Hit=true; }
			}
	}
	if (!HasGeometry)
	{
		const float R=GetMeleeBodyRadius();
		const FBox Bounds=FBox(GetActorLocation()-FVector(R,R,0),GetActorLocation()+FVector(R,R,GetMeleeBodyHeight())).ExpandBy(Pad);
		FVector Position,Normal;
		if (Bounds.IsInsideOrOn(A)) { Along=0; return true; }
		return FMath::LineExtentBoxIntersection(Bounds,A,B,FVector::ZeroVector,Position,Normal,Along);
	}
	return Hit;
}

FVector AACEWorldEntityActor::GetSoundEmitLocation() const
{
	if (CollisionProxy && CollisionProxy->IsRegistered())
	{
		return CollisionProxy->GetComponentLocation();
	}
	if (Appearance && bUsingDatMesh)
	{
		FBox VisualBox(ForceInit);
		if (Appearance->GetVisualWorldBounds(VisualBox) && VisualBox.IsValid)
		{
			return VisualBox.GetCenter();
		}
	}
	return GetActorLocation();
}

void AACEWorldEntityActor::SupportDroppedItem(FVector& Location, const FQuat& Rotation) const
{
	if (!Appearance || ParentGuid != 0 || IsWieldedWorldItem() || bIsPlayer || bIsSelf
		|| (ItemType & ACEItemType::Creature) != 0 || (PhysicsState & ACEPhysicsState::Gravity) == 0
		|| (ObjectDescriptionFlags & ACEObjectDescFlag::Stuck) != 0
		|| IsRetailPortalEffectSetup(static_cast<uint32>(SetupId))) return;
	if (GroundSupportRevision != Appearance->GetAppearanceRevision())
	{
		GroundSupportRevision=Appearance->GetAppearanceRevision(); GroundSupportPoints.Reset();
		// Reuse visible part bounds; cache at appearance changes only. No mesh
		// generation, rigid-body simulation or extra draw calls for dropped items.
		for (int32 P=0; P<Appearance->GetPartCount() && P<8; ++P)
		{
			auto* Part=Cast<UProceduralMeshComponent>(Appearance->GetPartMesh(P));
			if (!Part || !Part->IsVisible()) continue;
			FBox Box(ForceInit);
			for (int32 S=0; S<Part->GetNumSections(); ++S)
				if (const auto* Section=Part->GetProcMeshSection(S); Section && Section->bSectionVisible) Box+=Section->SectionLocalBox;
			if (!Box.IsValid) continue;
			const FTransform PartToActor=Part->GetComponentTransform().GetRelativeTransform(GetActorTransform());
			for (int32 Corner=0; Corner<8; ++Corner)
				GroundSupportPoints.Add(PartToActor.TransformPosition(FVector(Corner&1 ? Box.Max.X : Box.Min.X,
					Corner&2 ? Box.Max.Y : Box.Min.Y,Corner&4 ? Box.Max.Z : Box.Min.Z)));
		}
	}
	UACEDatSubsystem* Dat=GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
	uint32 Cell; const bool Indoor=ResolveIndoorOccupancy(Location,Cell);
	const FTransform Pose(Rotation,Location,GetActorScale3D());
	float SupportedZ=Location.Z;
	for (const FVector& Local:GroundSupportPoints)
	{
		const FVector Point=Pose.TransformPosition(Local);
		float Ground;
		const bool Found=!Indoor && Dat ? Dat->SampleOutdoorGroundZ(Point.X,Point.Y,WorldScale,Ground) : TraceGroundZ(Point,Ground);
		if (Found && (!Indoor || Ground-Location.Z<WorldScale*1.25f))
			SupportedZ=FMath::Max(SupportedZ,float(Location.Z+Ground+1.f-Point.Z));
	}
	Location.Z=SupportedZ;
}

bool AACEWorldEntityActor::ClampLocationToGround(FVector& InOutLocation, const FQuat* Rotation) const
{
	if (!bClampToGround)
	{
		return true;
	}
	// Stuck scenery (town signs, wall props) must keep authored server Z — clamping pulls
	// "The Eagle's Blade" etc. down onto the street grade.
	if ((ObjectDescriptionFlags & ACEObjectDescFlag::Stuck) != 0)
	{
		return true;
	}

	float GroundZ = 0.f;
	uint32 OccupiedEnv = 0;
	const bool bIndoorEnt = ResolveIndoorOccupancy(InOutLocation, OccupiedEnv);
	const bool bCreatureLike = bIsPlayer || bIsSelf
		|| (ItemType & ACEItemType::Creature) != 0;
	if (bCreatureLike)
	{
		// Retail advances contact/step-down along with locomotion. Sampling the
		// terrain only (or skipping indoor feet) leaves stair-step height changes
		// until the next server position. Keep jumps/flying actors out of this path.
		if (bHavePhysicsVelocity) return true;
		if (TraceGroundZ(InOutLocation,GroundZ,true))
		{
			const float Delta = InOutLocation.Z-GroundZ;
			if (Delta >= -MovementStepHeight*GetActorScale3D().GetAbsMax()-2.f
				&& Delta < GroundClampBandAc*WorldScale) InOutLocation.Z=GroundZ+1.f;
		}
		// Missing geometry must not make every idle NPC tick/retrace indefinitely.
		return true;
	}
	bool bHaveGround = false;
	if (!bIndoorEnt)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
				{
					bHaveGround = Dat->SampleOutdoorGroundZ(InOutLocation.X, InOutLocation.Y, WorldScale, GroundZ);
				}
			}
		}
	}
	if (!bHaveGround)
	{
		bHaveGround = TraceGroundZ(InOutLocation, GroundZ);
	}
	if (!bHaveGround)
	{
		return false;
	}
	if (bIndoorEnt && (GroundZ - InOutLocation.Z) > 1.25f * WorldScale)
	{
		return true;
	}

	constexpr float SkinCm = 1.f;
	float WaterDepthCm = 0.f;
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				WaterDepthCm = Dat->GetOutdoorWaterDepthCm(InOutLocation.X, InOutLocation.Y, WorldScale);
			}
		}
	}
	const float BandCm = FMath::Max(0.5f, GroundClampBandAc) * WorldScale
		+ FMath::Max(0.f, WaterDepthCm);
	const float Delta = InOutLocation.Z - GroundZ;
	const bool bPortalFx = IsRetailPortalEffectSetup(static_cast<uint32>(SetupId));
	// Re-seat feet onto the surface when we're near it, or when we've sunk beneath it. Large
	// positive gaps (jumping, flying, higher dungeon floors) are left to the server F748.
	// MaxDrop must cover the full Band — a 1.25 AC drop cap inside a 2.5 AC band left remotes
	// permanently floating in the dead zone.
	const float MaxDropCm = BandCm;
	if (Delta < -0.5f)
	{
		// Sunk under the surface — always lift.
		InOutLocation.Z = GroundZ + SkinCm;
		SupportDroppedItem(InOutLocation, Rotation ? *Rotation : GetActorQuat());
	}
	else if (Delta < BandCm || (bPortalFx && Delta < 8.f * WorldScale))
	{
		if (Delta <= MaxDropCm)
		{
			InOutLocation.Z = GroundZ + SkinCm;
			SupportDroppedItem(InOutLocation, Rotation ? *Rotation : GetActorQuat());
		}
	}
	return true;
}

void AACEWorldEntityActor::RefreshActorTickEnabled()
{
	const bool bMissile = (PhysicsState & ACEPhysicsState::Missile) != 0;
	// Appearance has its own component tick. Settled creatures need no repeated
	// occupancy/ground queries; position and motion packets wake this actor again.
	const bool bSettling = bHaveRemotePredict &&
		(!GetActorLocation().Equals(RemotePredictLocation, 0.1f)
		 || !GetActorQuat().Equals(RemotePredictRotation, 0.0001f));
	const bool bDirectedMotion = RemoteMotion.MovementType >= 6 && RemoteMotion.MovementType <= 9;
	const bool bNeed = bAttachedToParent || bHavePhysicsVelocity || bMissile || bSettling || bDirectedMotion || RemoteMotion.StickyTargetGuid != 0
		|| RemoteMotion.bMoving || bPendingGroundClamp
		|| !FMath::IsNearlyZero(RemoteMotion.Turn)
		|| !FMath::IsNearlyZero(RemoteMotion.StrafeUnitsPerSecond)
		|| !FMath::IsNearlyZero(RemoteMotion.ForwardUnitsPerSecond);
	SetActorTickEnabled(bNeed);
}

void AACEWorldEntityActor::ApplyACEPosition(const FACEPosition& Position)
{
	if (bAttachedToParent || ParentGuid != 0)
	{
		return;
	}
	if (!Position.IsValid())
	{
		return;
	}
	FVector NewLocation = Position.ToUnrealLocation(WorldScale);
	FQuat NewRotation = Position.ToUnrealQuat();
	LastAceCellId = Position.CellId;
	if (Appearance) Appearance->UpdateCellLighting(Position.CellId);
	if (bHaveRemotePredict && (NewRotation | RemotePredictRotation) < 0.f)
	{
		NewRotation = NewRotation * -1.f;
	}

	const bool bMoveTo = RemoteMotion.MovementType == 6 || RemoteMotion.MovementType == 7;
	const bool bMoving = RemoteMotion.bMoving
		|| !FMath::IsNearlyZero(RemoteMotion.ForwardUnitsPerSecond)
		|| bMoveTo;
	const bool bSnap = !bHaveRemotePredict
		|| FVector::DistSquared(GetActorLocation(), NewLocation)
			> FMath::Square(RemoteSnapDistance * WorldScale);

	RemoteAnchorLocation = NewLocation;
	// Every accepted F748 replaces the simulation anchor. Render interpolation
	// smooths this correction; a two-metre dead zone cannot be used in melee combat.
	RemotePredictLocation = NewLocation;
	// MoveTo facing comes from travel direction each tick. Applying sparse F748 orientation
	// here fought that Dir and flipped enemies ~1 Hz (looked like a ping-pong anim).
	if (!bMoveTo || !bMoving)
	{
		RemotePredictRotation = NewRotation;
	}
	bHaveRemotePredict = true;

	const bool bMissile = (PhysicsState & ACEPhysicsState::Missile) != 0;
	if (Position.bHasVelocity)
	{
		ApplyPhysicsVelocity(Position.Velocity, AcePhysicsOmega);
	}
	else if (!bMissile)
	{
		// F748 without velocity means locomotion / grounded — stop leftover ballistic
		// integrate from a prior jump (pets/summons were falling through floors).
		bHavePhysicsVelocity = false;
		AcePhysicsVelocity = FVector::ZeroVector;
		AcePhysicsOmega = FVector::ZeroVector;
	}
	if (Position.bIsGrounded && !bMissile)
	{
		bHavePhysicsVelocity = false;
		AcePhysicsVelocity = FVector::ZeroVector;
	}

	// F748 corrects the prediction; Tick blends the displayed heading too. A hard
	// rotation here made each sparse packet visibly snap a walking player's body.

	if (bSnap)
	{
		// Projectiles integrate their own arc (with gravity) — don't pin them to the floor.
		if (!bHavePhysicsVelocity && !bMissile)
		{
			// Indoor: TraceGroundZ hits EnvCell physics mesh when streamed; never outdoor heightfield.
			if (ClampLocationToGround(NewLocation, &NewRotation))
			{
				bPendingGroundClamp = false;
				GroundClampRetrySeconds = 0.f;
			}
			else if (bRetryGroundClamp)
			{
				bPendingGroundClamp = true;
				GroundClampRetrySeconds = 4.f;
			}
			RemoteAnchorLocation = NewLocation;
			RemotePredictLocation = NewLocation;
		}
		SetActorLocation(NewLocation);
		// MoveTo facing is owned by travel Dir each tick — don't snap to sparse F748 yaw.
		if (!bMoveTo || !bMoving)
		{
			SetActorRotation(NewRotation);
		}
	}
	RefreshActorTickEnabled();
}

void AACEWorldEntityActor::ApplyMotionState(const FACEObjectMotionState& Motion)
{
	const bool bDeath = Motion.IsDeathMotion();
	// A late Ready/locomotion update cannot resurrect a slain creature. Players
	// can revive without a replacement object, so their next state is allowed.
	if (bReceivedDeathMotion && !bIsPlayer && !bDeath) return;
	if (bReceivedDeathMotion && bIsPlayer && !bDeath && Appearance)
		Appearance->ClearDeathMotion();
	bReceivedDeathMotion = bDeath && !IsCorpse();
	SetActorEnableCollision(bCellVisible && !bReceivedDeathMotion);
	// A corpse is a separate object at the final death frame. Late Ready/locomotion
	// updates must not turn it back into an idle creature.
	if (IsCorpse())
	{
		if (Appearance) { Appearance->SetHeldActionMotion(ACEMotion::Dead, ACEMotion::StanceNonCombat); }
		return;
	}
	if (Appearance && ParentGuid == 0 && !bAttachedToParent)
	{
		if (Motion.CurrentStyle != 0)
		{
			Appearance->SetPreferredStyle(Motion.CurrentStyle);
		}
		const int32 Cmd = Motion.ForwardCommand;
		const bool bDoorCmd = UsesOnOffMotion()
			&& (Cmd == ACEMotion::OnCommandU16 || Cmd == ACEMotion::OffCommandU16
				|| Cmd == static_cast<int32>(ACEMotion::On) || Cmd == static_cast<int32>(ACEMotion::Off));
		if (bDoorCmd)
		{
			Appearance->PlayDoorMotion(Cmd);
			// Open immediately walkable; close waits for Ethereal clear / Off hold.
			const bool bOpening = (Cmd == ACEMotion::OnCommandU16
				|| Cmd == static_cast<int32>(ACEMotion::On)
				|| IsEthereal());
			const bool bClosing = (Cmd == ACEMotion::OffCommandU16
				|| Cmd == static_cast<int32>(ACEMotion::Off))
				&& !IsEthereal();
			if (bOpening)
			{
				ConfigureWorldCollision(false);
			}
			else if (bClosing)
			{
				ConfigureWorldCollision(true);
			}
		}
		if (!bDoorCmd)
		{
			uint32 Action = static_cast<uint32>(Motion.ActionCommand);
			if (Action == 0 && Cmd != 0)
			{
				const uint16 Low = static_cast<uint16>(Cmd & 0xFFFF);
				const bool bLocoOrDoor = Low == 0x0003 || Low == 0x0005 || Low == 0x0006 || Low == 0x0007
					|| Low == 0x000D || Low == 0x000E || Low == 0x000F || Low == 0x0010
					|| Low == ACEMotion::OnCommandU16 || Low == ACEMotion::OffCommandU16;
				if (!bLocoOrDoor)
				{
					Action = static_cast<uint32>(Cmd);
				}
			}
			if (Action != 0 && Action <= 0xFFFFu)
			{
				Action = ACEMotion::ExpandPackedCommand(static_cast<uint16>(Action));
			}
			const bool bDying = Action == ACEMotion::Dead
				|| Cmd == ACEMotion::DeadCommandU16
				|| Cmd == static_cast<int32>(ACEMotion::Dead);
			if (Action != 0)
			{
				const uint32 ActionHi = Action & 0xFF000000u;
				const bool bHoldEmote = ActionHi == 0x13000000u
					|| ActionHi == 0x42000000u || ActionHi == 0x43000000u
					|| ACEMotion::IsHeldRestCommand(Action);
				if (bDying)
				{
					// Dead is a held state only for a newly created corpse. A living
					// object's Dead update must first play the Ready -> Dead link.
					Appearance->PlayActionMotion(ACEMotion::Dead, Motion.ActionSpeed, Motion.CurrentStyle);
				}
				else if (ACEMotion::IsHeldRestCommand(Action))
				{
					// A live state change plays the transition before holding its endpoint.
					Appearance->PlayActionMotion(static_cast<int32>(Action), Motion.ActionSpeed, Motion.CurrentStyle, true);
				}
				else
				{
					Appearance->PlayActionMotion(static_cast<int32>(Action), Motion.ActionSpeed, Motion.CurrentStyle, bHoldEmote);
				}
				if (!bHoldEmote)
				{
					for (int32 Followup : Motion.ActionFollowups)
					{
						Appearance->QueueActionMotion(Followup, Motion.ActionSpeed, Motion.CurrentStyle, false);
					}
				}
			}
			else if (bDying)
			{
				Appearance->PlayActionMotion(ACEMotion::Dead, Motion.AnimPlayRate > 0.f ? Motion.AnimPlayRate : 1.f, Motion.CurrentStyle);
			}
			else if (ACEMotion::IsHeldRestCommand(static_cast<uint32>(Cmd)))
			{
				Appearance->PlayActionMotion(Cmd, Motion.AnimPlayRate, Motion.CurrentStyle, true);
			}
			else if (ACEMotion::NormalizeCommand(static_cast<uint32>(Cmd)) == ACEMotion::Ready)
			{
				Appearance->CancelHeldActionMotion();
			}
			if (bDying && ScriptComponent && !bPlayedDeathSound)
			{
				// Retail death SFX is a SoundTable Death1 hook on the fall anim. Stance
				// mismatches used to skip the Link; play the table entry here as well.
				bPlayedDeathSound = true;
				ScriptComponent->PlaySound(0x0F, 1.f);
			}
			else if (!bDying && !IsCorpse())
			{
				bPlayedDeathSound = false;
			}
			if (bDying || IsCorpse())
			{
				Appearance->SetLocomotionInput(0.f, 0.f, false, 1.f);
				bHavePhysicsVelocity = false;
				AcePhysicsVelocity = FVector::ZeroVector;
			}
			else
			{
				float Forward = Motion.Forward;
				float Strafe = Motion.Strafe;
				// MoveToObject/Position: server may omit Interpreted ForwardCommand; still drive
				// walk/run cycles from movement speed so enemies don't slide in bind pose.
				if ((Motion.MovementType == 6 || Motion.MovementType == 7)
					&& FMath::IsNearlyZero(Forward) && FMath::IsNearlyZero(Strafe)
					&& !FMath::IsNearlyZero(Motion.ForwardUnitsPerSecond))
				{
					Forward = 1.f;
				}
				Appearance->SetLocomotionInput(
					Forward, Strafe, Motion.bRunning, Motion.AnimPlayRate);
			}
		}
	}
	RemoteMotion = Motion;
	if (bDeath)
	{
		RemoteMotion.StickyTargetGuid = 0;
		RemoteMotion.bMoving = false;
		RemoteMotion.Forward = RemoteMotion.Strafe = RemoteMotion.Turn = 0.f;
		RemoteMotion.ForwardUnitsPerSecond = 0.f;
		bHavePhysicsVelocity = false;
		AcePhysicsVelocity = FVector::ZeroVector;
	}
	if (const auto* GI = GetGameInstance())
	{
		if (const auto* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			FVector Velocity;
			RemoteWalkSpeedAc = Dat->GetMotionVelocity(MotionTableId, 0x45000005, Motion.CurrentStyle, Velocity)
				? Velocity.Size2D() : 3.1199999f;
			RemoteRunSpeedAc = Dat->GetMotionVelocity(MotionTableId, 0x44000007, Motion.CurrentStyle, Velocity)
				? Velocity.Size2D() : 4.f;
		}
	}

	// Capture TurnTo face direction once at packet time (do not track the live target).
	bHaveRemoteTurnFace = false;
	RemoteTurnFaceDir = FVector::ZeroVector;
	if (Motion.MovementType == 8 || Motion.MovementType == 9)
	{
		if (!bHaveRemotePredict && ParentGuid == 0)
		{
			RemotePredictLocation = GetActorLocation();
			RemotePredictRotation = GetActorQuat();
			RemoteAnchorLocation = RemotePredictLocation;
			bHaveRemotePredict = true;
		}
		if (Motion.MovementType == 8 && Motion.MoveToTargetGuid != 0)
		{
			if (UWorld* World = GetWorld())
			{
				if (UGameInstance* GI = World->GetGameInstance())
				{
					if (UACEClientSubsystem* Client = GI->GetSubsystem<UACEClientSubsystem>())
					{
						FVector TargetLoc = FVector::ZeroVector;
						bool bHaveTarget = false;
						FACEWorldObject TargetObj;
						if (Client->GetWorldObject(Motion.MoveToTargetGuid, TargetObj)
							&& TargetObj.bHasPosition && TargetObj.Position.IsValid())
						{
							TargetLoc = TargetObj.Position.ToUnrealLocation(WorldScale);
							bHaveTarget = true;
						}
						if (Motion.MoveToTargetGuid == Client->GetPlayerGuid())
						{
							const FACEPosition PlayerPos = Client->GetPlayerPosition();
							if (PlayerPos.IsValid())
							{
								TargetLoc = PlayerPos.ToUnrealLocation(WorldScale);
								bHaveTarget = true;
							}
						}
						if (bHaveTarget)
						{
							const FVector SelfLoc = bHaveRemotePredict
								? RemotePredictLocation
								: GetActorLocation();
							FVector To = TargetLoc - SelfLoc;
							To.Z = 0.f;
							if (To.Normalize())
							{
								RemoteTurnFaceDir = To;
								bHaveRemoteTurnFace = true;
							}
						}
					}
				}
			}
		}
		else if (Motion.MovementType == 9)
		{
			FVector Dir = FACEPosition::UnrealDirFromAceHeadingDegrees(Motion.MoveToDesiredHeading);
			if (Dir.Normalize())
			{
				RemoteTurnFaceDir = Dir;
				bHaveRemoteTurnFace = true;
			}
		}
	}
	RefreshActorTickEnabled();
}

void AACEWorldEntityActor::ApplyPhysicsVelocity(const FVector& AceVelocity, const FVector& AceOmega)
{
	// Stuck non-creature props are anchored scenery even when their weenie carries
	// Gravity (Rithwic Smithy is 0x10418). Never turn a velocity packet into an
	// independent ballistic simulation of a wall fixture. F748 positions remain
	// authoritative; creatures and missiles still predict jumps and flight.
	const bool bAnchoredProp = (ObjectDescriptionFlags & ACEObjectDescFlag::Stuck) != 0
		&& !bIsPlayer && !bIsSelf && (ItemType & ACEItemType::Creature) == 0
		&& (PhysicsState & ACEPhysicsState::Missile) == 0;
	AcePhysicsVelocity = bAnchoredProp ? FVector::ZeroVector : AceVelocity;
	AcePhysicsOmega = AceOmega;
	bHavePhysicsVelocity = !AcePhysicsVelocity.IsNearlyZero();
	if (!bHaveRemotePredict && ParentGuid == 0)
	{
		RemotePredictLocation = GetActorLocation();
		RemotePredictRotation = GetActorQuat();
		RemoteAnchorLocation = RemotePredictLocation;
		bHaveRemotePredict = true;
	}
	RefreshActorTickEnabled();
}

void AACEWorldEntityActor::SetCellVisible(bool bVisible)
{
	if (bCellVisible == bVisible) return;
	bCellVisible = bVisible;
	SetActorHiddenInGame(!bVisible);
	ApplyPhysicsState(PhysicsState);
}

bool AACEWorldEntityActor::IsMeshSuppressed() const
{
	return !bCellVisible || (PhysicsState & (ACEPhysicsState::NoDraw | ACEPhysicsState::Hidden | ACEPhysicsState::Cloaked)) != 0
		|| (ObjectDescriptionFlags & ACEObjectDescFlag::HiddenAdmin) != 0;
}

void AACEWorldEntityActor::ApplyPhysicsState(int32 InPhysicsState)
{
	const bool bImpactBegan = (InPhysicsState & ACEPhysicsState::NoDraw) && !(PhysicsState & ACEPhysicsState::NoDraw);
	PhysicsState = InPhysicsState;
	if (ScriptComponent) ScriptComponent->SetPhysicsHidden((PhysicsState & ACEPhysicsState::Hidden) != 0);
	const bool bNowEthereal = IsEthereal();
	// Creature collision is independent of the server's rendering flags.
	const bool bCreatureLike = bIsPlayer || bIsSelf
		|| (ItemType & ACEItemType::Creature) != 0;
	// PhysicsDesc's Hidden flag suppresses the object regardless of item type.
	// It is independent of animation NoDraw and must survive deferred mesh creation.
	const bool bNoDraw = IsMeshSuppressed();
	SetActorEnableCollision(bCellVisible && !bReceivedDeathMotion);

	if (Appearance)
	{
		Appearance->SetAppearanceVisible(
			!bNoDraw && !IsRetailPortalEffectSetup(static_cast<uint32>(SetupId)));
	}
	if (Mesh)
	{
		Mesh->SetVisibility(!bNoDraw && !bUsingDatMesh && !bParticleOnlyAppearance);
	}

	// Door open/close animation comes only from UpdateMotion On/Off. Do not also play it
	// here on Ethereal SetState — that caused the close (and open) anim to fire twice.

	if (bIsSelf || ParentGuid != 0 || bAttachedToParent)
	{
		ConfigureWorldCollision(false);
		return;
	}
	if ((PhysicsState & ACEPhysicsState::Missile) != 0)
	{
		// The server ends flight with NoDraw + Cloaked, then plays Explode.
		// Stop the launch emitter now; keep the component available for the impact script.
		if (bImpactBegan)
		{
			ApplyPhysicsVelocity(FVector::ZeroVector, FVector::ZeroVector);
			if (ScriptComponent) ScriptComponent->StopAllEffects();
		}
		DisableAllCollision();
		if (Appearance)
		{
			Appearance->ConfigurePartCollision(false, false);
		}
		SetActorEnableCollision(false);
		return;
	}
	// Doors/chests: open = walk-through (held On or Ethereal). Closed Off = solid.
	// Prefer motion state so open doors never stay blocking if Ethereal SetState lags.
	// Creatures ignore stale Ethereal on ObjectCreate — only corpses become non-solid.
	bool bSolid = !IsCorpse();
	if (!bCreatureLike)
	{
		bSolid = !(bNowEthereal || IsCorpse());
	}
	if (UsesOnOffMotion())
	{
		const bool bOpen = (Appearance && Appearance->IsDoorHeldOpen()) || bNowEthereal;
		bSolid = !bOpen && !IsCorpse();
	}
	ConfigureWorldCollision(bSolid);
	if (Mesh && !bNoDraw)
	{
		Mesh->SetVisibility(!bUsingDatMesh && !bParticleOnlyAppearance);
	}
}
