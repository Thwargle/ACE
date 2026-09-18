#include "ACELandblockActor.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACERegionSceneryActor.h"
#include "ACEScriptComponent.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACETypes.h"
#include "Dat/ACEDatFileTypes.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

namespace
{
	/** Shared across all landblock actors — 11×11 × 14ms scenery cooks was the FPS cliff. */
	uint64 GSceneryBudgetFrame = MAX_uint64;
	double GSceneryBudgetUsedSec = 0.0;
	constexpr double GSceneryGlobalBudgetSec = 0.004; // 4ms total per frame
}

AACELandblockActor::AACELandblockActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	TerrainMesh->bPreferCachedDraws = true;
	SetRootComponent(TerrainMesh);
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TerrainMesh->SetCollisionObjectType(ECC_WorldStatic);
	TerrainMesh->SetCollisionResponseToAllChannels(ECR_Block);
	TerrainMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	// Mouse pick uses ECC_Visibility on world-object capsules only — terrain must not
	// stop LineTraceMulti (it returns only until the first blocking Visibility hit).
	TerrainMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	TerrainMesh->bUseAsyncCooking = true;
	TerrainMesh->bUseComplexAsSimpleCollision = true;
	// Movable: streamer SetActorLocation + attached scenery. Do not cast — a
	// heightfield caster starves CSM of resolution for characters/buildings.
	TerrainMesh->SetMobility(EComponentMobility::Movable);
	TerrainMesh->SetCastShadow(false);
	TerrainMesh->bCastDynamicShadow = false;
	TerrainMesh->bCastFarShadow = false;
	TerrainMesh->bCastInsetShadow = false;
	TerrainMesh->bReceivesDecals = true;
	// Ground-snap ignores this tag while the protocol position is inside an EnvCell.
	TerrainMesh->ComponentTags.Add(FName(TEXT("ACEOutdoorTerrain")));
}

void AACELandblockActor::SetOutdoorTerrainCollisionEnabled(bool bEnabled)
{
	bWantTerrainCollision = bEnabled;
	ApplyDesiredOutdoorTerrainState();
}

void AACELandblockActor::SetOutdoorTerrainHiddenInGame(bool bHideTerrain)
{
	bWantTerrainHidden = bHideTerrain;
	if (bHideTerrain)
	{
		bWantTerrainCollision = false;
	}
	ApplyDesiredOutdoorTerrainState();
}

void AACELandblockActor::ApplyDesiredOutdoorTerrainState()
{
	if (!TerrainMesh)
	{
		return;
	}
	const bool bHide = bWantTerrainHidden;
	if (TerrainMesh->bHiddenInGame != bHide || TerrainMesh->IsVisible() == bHide)
	{
		TerrainMesh->SetHiddenInGame(bHide);
		TerrainMesh->SetVisibility(!bHide);
	}
	if (TerrainMesh->CastShadow)
	{
		TerrainMesh->SetCastShadow(false);
	}
	// Indoor: never let the heightfield support the pawn. Async cook / mesh re-apply
	// used to flip QueryAndPhysics back on while the mesh stayed hidden.
	if (!bWantTerrainCollision)
	{
		TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TerrainMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	else
	{
		TerrainMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TerrainMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}

void AACELandblockActor::SetLandPortalLookOut(bool bLookOut)
{
	if (!TerrainMesh || TerrainMesh->GetNumSections() <= 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return;
	}
	const int32 Num = TerrainMesh->GetNumSections();
	for (int32 i = 0; i < Num; ++i)
	{
		UMaterialInterface* Cur = TerrainMesh->GetMaterial(i);
		if (UMaterialInterface* Remapped = Dat->RemapLandMaterialForPortalLookOut(Cur, bLookOut))
		{
			TerrainMesh->SetMaterial(i, Remapped);
		}
	}
	TerrainMesh->MarkRenderStateDirty();
}

void AACELandblockActor::SetLandEnvCellFloorPriority(bool bEnable, UACEDatSubsystem* Dat, bool bIndoorLookOut)
{
	if (!TerrainMesh || TerrainMesh->GetNumSections() <= 0)
	{
		return;
	}
	// Resolve coplanar floors by millimetres, not tens of centimetres. The old
	// 22/48 cm depth offset detached contact shadows from correctly grounded feet.
	// Indoor look-out uses the same physical depth as the exterior view.
	(void)bIndoorLookOut;
	const float Bias = bEnable ? 0.25f : 0.f;
	const int32 Num = TerrainMesh->GetNumSections();
	if (AppliedLandDepthBias == Bias && AppliedLandDepthBiasSections == Num)
	{
		return;
	}
	AppliedLandDepthBias = Bias;
	AppliedLandDepthBiasSections = Num;
	for (int32 i = 0; i < Num; ++i)
	{
		UMaterialInterface* Cur = TerrainMesh->GetMaterial(i);
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Cur))
		{
			Mid->SetScalarParameterValue(TEXT("LandDepthBias"), Bias);
		}
		else if (Dat && Cur)
		{
			if (UMaterialInterface* Remapped = Dat->RemapLandMaterialWithDepthBias(Cur, Bias))
			{
				TerrainMesh->SetMaterial(i, Remapped);
			}
		}
	}
	TerrainMesh->MarkRenderStateDirty();
}

void AACELandblockActor::SetIndoorScenerySuppressed(bool bSuppress, const TArray<int32>& HideBuildingInfoIndices)
{
	if (bIndoorSceneryStateApplied && bIndoorScenerySuppressed == bSuppress
		&& IndoorHideBuildingInfoIndices == HideBuildingInfoIndices
		&& AppliedIndoorSceneryMeshCount == SceneryMeshes.Num()
		&& AppliedIndoorBuildingShellCount == BuildingShells.Num()) return;
	bIndoorSceneryStateApplied = true;
	bIndoorScenerySuppressed = bSuppress;
	IndoorHideBuildingInfoIndices = HideBuildingInfoIndices;
	AppliedIndoorSceneryMeshCount = SceneryMeshes.Num();
	AppliedIndoorBuildingShellCount = BuildingShells.Num();
	// Camera occupancy does not unload the outdoor setup. Retain detail resources
	// in the landblock keep ring; hide them only when its detail level changes.
	auto SetDetail = [this](UPrimitiveComponent* Mesh, bool bCollisionOnly = false)
	{
		if (!Mesh) return;
		Mesh->SetHiddenInGame(!bFullResDetail || bCollisionOnly);
		Mesh->SetVisibility(bFullResDetail && !bCollisionOnly);
		if (!bFullResDetail)
		{
			if (!SuspendedSceneryCollision.Contains(Mesh))
				SuspendedSceneryCollision.Add(Mesh, Mesh->GetCollisionEnabled());
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else if (const auto* Previous = SuspendedSceneryCollision.Find(Mesh))
		{
			Mesh->SetCollisionEnabled(*Previous);
			SuspendedSceneryCollision.Remove(Mesh);
		}
	};
	for (UProceduralMeshComponent* Mesh : SceneryMeshes) SetDetail(Mesh);
	for (const FBuildingShell& Shell : BuildingShells)
	{
		SetDetail(Shell.Mesh);
		SetDetail(Shell.CollisionMesh, true);
	}
	for (const auto& Pair : SceneryHisms) SetDetail(Pair.Value);
	for (AACERegionSceneryActor* Scenery : AnimatedScenery)
	{
		if (!Scenery) continue;
		Scenery->SetActorHiddenInGame(!bFullResDetail);
		Scenery->SetActorEnableCollision(bFullResDetail);
		if (Scenery->Appearance) Scenery->Appearance->SetComponentTickEnabled(bFullResDetail);
		if (Scenery->ScriptComponent) Scenery->ScriptComponent->SetComponentTickEnabled(bFullResDetail);
	}
}

void AACELandblockActor::SetBuildingShellsBlockPawn(bool bBlockPawn)
{
	SetBuildingShellsBlockPawn(bBlockPawn, TArray<int32>());
}

void AACELandblockActor::SetBuildingShellsBlockPawn(bool bBlockPawn, const TArray<int32>& IgnoreBuildingIndices)
{
	const int32 ShellCount = BuildingMeshIndices.Num() + BuildingShells.Num() + SceneryMeshes.Num();
	uint32 IgnoreHash = GetTypeHash(IgnoreBuildingIndices.Num());
	for (int32 Idx : IgnoreBuildingIndices)
	{
		IgnoreHash = HashCombine(IgnoreHash, GetTypeHash(Idx));
	}
	if (bBuildingShellPawnApplied
		&& bLastBuildingShellBlockPawn == bBlockPawn
		&& AppliedBuildingShellCount == ShellCount
		&& AppliedBuildingShellIgnoreHash == IgnoreHash)
	{
		return;
	}
	bBuildingShellPawnApplied = true;
	bLastBuildingShellBlockPawn = bBlockPawn;
	AppliedBuildingShellCount = ShellCount;
	AppliedBuildingShellIgnoreHash = IgnoreHash;

	TSet<int32> IgnoreSet;
	IgnoreSet.Reserve(IgnoreBuildingIndices.Num());
	for (int32 Idx : IgnoreBuildingIndices)
	{
		IgnoreSet.Add(Idx);
	}

	auto ApplyPawnResponse = [&](UPrimitiveComponent* Mesh, int32 InfoIndex)
	{
		if (!Mesh)
		{
			return;
		}
		const bool bIgnore = !bBlockPawn
			|| (InfoIndex != INDEX_NONE && IgnoreSet.Contains(InfoIndex));
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, bIgnore ? ECR_Ignore : ECR_Block);
		Mesh->RecreatePhysicsState();
	};

	for (int32 i = 0; i < SceneryMeshes.Num(); ++i)
	{
		UProceduralMeshComponent* Mesh = SceneryMeshes[i];
		if (!Mesh)
		{
			continue;
		}
		int32 InfoIndex = INDEX_NONE;
		const int32 BldgSlot = BuildingMeshIndices.IndexOfByKey(i);
		if (BldgSlot != INDEX_NONE && BuildingInfoIndices.IsValidIndex(BldgSlot))
		{
			InfoIndex = BuildingInfoIndices[BldgSlot];
		}
		// Indoor: EnvCell PhysicsBSP owns the room. Stab stairs + neighbor shells both
		// blocking pawn crushed the capsule through the floor / out of the building.
		ApplyPawnResponse(Mesh, InfoIndex);
	}
	for (const FBuildingShell& Shell : BuildingShells)
	{
		ApplyPawnResponse(Shell.Mesh, Shell.InfoIndex);
		ApplyPawnResponse(Shell.CollisionMesh, Shell.InfoIndex);
	}
}

bool AACELandblockActor::IsOutdoorTerrainMeshReady() const
{
	const bool bSceneryOnly = TerrainMesh && TerrainMesh->bHiddenInGame;
	if (bSceneryOnly)
	{
		return true;
	}
	return TerrainMesh && TerrainMesh->GetNumSections() > 0;
}

bool AACELandblockActor::IsLandblockReady() const
{
	// Scenery-only children (chunk mode): terrain lives on the parent chunk.
	const bool bSceneryOnly = TerrainMesh && TerrainMesh->bHiddenInGame;
	if (!bSceneryOnly)
	{
		if (!TerrainMesh || TerrainMesh->GetNumSections() <= 0)
		{
			return false;
		}
		if (!TerrainMesh->IsCollisionEnabled() && bWantTerrainCollision)
		{
			return false;
		}
	}
	// Buildings/stabs must finish; RegionDesc flora may still stream after reveal.
	for (const FPendingScenery& Item : PendingScenery)
	{
		if (Item.bBuilding)
		{
			return false;
		}
	}
	return true;
}

void AACELandblockActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PendingScenery.Num() == 0 && !bRegionDescCollectPending)
	{
		SetActorTickEnabled(false);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		if (UWorld* World = GetWorld())
		{
			GI = World->GetGameInstance();
		}
	}
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		PendingScenery.Reset();
		bRegionDescCollectPending = false;
		SetActorTickEnabled(false);
		return;
	}

	if (bRegionDescCollectPending)
	{
		TryQueueRegionDesc(Dat);
	}

	if (PendingScenery.Num() == 0)
	{
		if (!bRegionDescCollectPending)
		{
			SetActorTickEnabled(false);
		}
		return;
	}

	FVector PlayerUe = GetActorLocation();
	bool bPlayerLb = false;
	if (GI)
	{
		if (UACEClientSubsystem* Client = GI->GetSubsystem<UACEClientSubsystem>())
		{
			const FACEPosition Pos = Client->GetPlayerPosition();
			if (Pos.IsValid())
			{
				PlayerUe = Pos.ToUnrealLocation(WorldScale);
				bPlayerLb = (static_cast<uint32>(Pos.CellId) & 0xFFFF0000u)
					== static_cast<uint32>(LandblockId);
			}
		}
	}
	if (PendingScenery.Num() > 1)
	{
		PendingScenery.Sort([&](const FPendingScenery& A, const FPendingScenery& B)
		{
			if (A.bBuilding != B.bBuilding)
			{
				return A.bBuilding;
			}
			auto DistSq = [&](const FPendingScenery& Item) -> float
			{
				const FVector World = GetActorLocation() + FACEPosition::AceVectorToUnreal(
					FVector(Item.Origin.X, Item.Origin.Y, Item.Origin.Z), WorldScale);
				return static_cast<float>(FVector::DistSquared(PlayerUe, World));
			};
			return DistSq(A) < DistSq(B);
		});
	}

	if (GSceneryBudgetFrame != GFrameCounter)
	{
		GSceneryBudgetFrame = GFrameCounter;
		GSceneryBudgetUsedSec = 0.0;
	}
	const bool bLightweight = Dat->IsLightweightStreaming();
	const bool bPortalDrain = Dat->IsInPortalSpace() && !bLightweight;
	const double FrameBudget = GSceneryGlobalBudgetSec;
	const double GlobalRemaining = FrameBudget - GSceneryBudgetUsedSec;
	if (GlobalRemaining <= 0.0)
	{
		return;
	}

	const int32 CountBudget = FMath::Max(1, bPlayerLb ? 12 : SceneryPerTick);
	const double OwnBudgetSec = bPlayerLb
		? (bPortalDrain ? 0.002 : 0.008)
		: FMath::Max(0.5f, SceneryTimeBudgetMs) * 0.001;
	const double BudgetSec = FMath::Min(OwnBudgetSec, GlobalRemaining);
	const double StartSec = FPlatformTime::Seconds();
	int32 Spawned = 0;
	int32 StalledPending = 0;
	int32 RotatedFlora = 0;
	while (PendingScenery.Num() > 0 && Spawned < CountBudget)
	{
		if (FPlatformTime::Seconds() - StartSec >= BudgetSec) break;
		const FPendingScenery Item = PendingScenery[0];
		PendingScenery.RemoveAt(0, 1, EAllowShrinking::No);
		if (bSkipNonBuildingScenery && !Item.bBuilding)
		{
			continue;
		}
		if (bLightweight && !Item.bBuilding)
		{
			PendingScenery.Add(Item);
			++RotatedFlora;
			if (RotatedFlora >= PendingScenery.Num())
			{
				break;
			}
			continue;
		}
		if (!TrySpawnOneScenery(Dat, Item))
		{
			// Setup still baking — re-queue and try other Models so one DID can't block the ring.
			PendingScenery.Add(Item);
			++StalledPending;
			if (StalledPending >= PendingScenery.Num())
			{
				break;
			}
			continue;
		}
		++Spawned;
		StalledPending = 0;
		if (Spawned > 0 && (FPlatformTime::Seconds() - StartSec) >= BudgetSec)
		{
			break;
		}
	}
	GSceneryBudgetUsedSec += FPlatformTime::Seconds() - StartSec;

	if (PendingScenery.Num() == 0)
	{
		if (bRegionDescCollectPending)
		{
			return;
		}
		int32 HismInstances = 0;
		for (const auto& Pair : SceneryHisms)
		{
			if (Pair.Value)
			{
				HismInstances += Pair.Value->GetInstanceCount();
			}
		}
		UE_LOG(LogTemp, Log, TEXT("ACE: Landblock 0x%08X scenery finished — %d HISM instances (%d pools), %d bldg, %d proc, %d animated"),
			LandblockId, HismInstances, SceneryHisms.Num(), BuildingShells.Num(), SceneryMeshes.Num(), AnimatedScenery.Num());
		SetActorTickEnabled(false);
	}
}

	// Actor ownership/attachment does not destroy separately spawned actors in UE.
// Retail releases these objects with their owning cell/landblock.
void AACELandblockActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearScenery();
    Super::EndPlay(EndPlayReason);
}

void AACELandblockActor::Destroyed()
{
    ClearScenery();
    Super::Destroyed();
}

void AACELandblockActor::ClearScenery()
{
	SuspendedSceneryCollision.Reset();
	bSceneryQueued = false;
	bIndoorSceneryStateApplied = false;
	bBuildingShellPawnApplied = false;
	PendingScenery.Reset();
	BuildingMeshIndices.Reset();
	BuildingInfoIndices.Reset();
	bIndoorScenerySuppressed = false;
	IndoorHideBuildingInfoIndices.Reset();
	bRegionDescCollectPending = false;
	for (UProceduralMeshComponent* Mesh : SceneryMeshes)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	SceneryMeshes.Reset();
	for (FBuildingShell& Shell : BuildingShells)
	{
		if (Shell.Mesh) Shell.Mesh->DestroyComponent();
		if (Shell.CollisionMesh) Shell.CollisionMesh->DestroyComponent();
	}
	BuildingShells.Reset();
	LastDoorwayClipFingerprint = 0;
	for (auto& Pair : SceneryHisms)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();
			Pair.Value->DestroyComponent();
		}
	}
	SceneryHisms.Reset();
	for (AACERegionSceneryActor* Actor : AnimatedScenery)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	AnimatedScenery.Reset();
	ClaimedLiveSceneryModels.Reset();
	LiveRegionAnimCount = 0;
	SetActorTickEnabled(false);
}

void AACELandblockActor::SetFullResDetail(bool bFull)
{
	if (bFullResDetail == bFull) return;
	bFullResDetail = bFull;
	bIndoorSceneryStateApplied = false;
	bBuildingShellPawnApplied = false;
	SetIndoorScenerySuppressed(bIndoorScenerySuppressed, IndoorHideBuildingInfoIndices);
	if (!bFull)
	{
		SetActorTickEnabled(false);
		return;
	}
	if (bSceneryQueued)
	{
		SetActorTickEnabled(!IsSceneryComplete());
		return;
	}
	UGameInstance* GI = GetGameInstance();
	if (!GI && GetWorld()) GI = GetWorld()->GetGameInstance();
	if (UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr) QueueScenery(Dat);
}

void AACELandblockActor::SetTerrainLod(int32 PolySize, int32 TransDir)
{
	const int32 UsePoly = (PolySize == 2 || PolySize == 4 || PolySize == 8) ? PolySize : 1;
	const int32 UseDir = (UsePoly > 1) ? TransDir : 0;
	if (TerrainMesh && TerrainMesh->GetNumSections() > 0)
	{
		// Runtime terrain keeps the complete heightfield and uses texture mipmaps.
		// Moving between distance rings must not rebuild/cook an unchanged mesh.
		AppliedPolySize = UsePoly;
		AppliedTransDir = UseDir;
		return;
	}
	if (TerrainMesh && TerrainMesh->bHiddenInGame && TerrainMesh->GetNumSections() == 0)
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		if (UWorld* World = GetWorld())
		{
			GI = World->GetGameInstance();
		}
	}
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat || !TerrainMesh)
	{
		return;
	}
	if (Dat->ApplyLandblockToProceduralMesh(TerrainMesh, LandblockId, WorldScale, UsePoly, UseDir))
	{
		AppliedPolySize = UsePoly;
		AppliedTransDir = UseDir;
		ApplyDesiredOutdoorTerrainState();
	}
}

void AACELandblockActor::CollectSceneryShadowCandidates(const FVector& CameraWorld, float MaxDistCm, TArray<FACESceneryShadowCand>& Out) const
{
	constexpr float MinFloraRadiusCm = 350.f;
	constexpr float MinPropRadiusCm = 25.f;
	TSet<int32> BuildingSet;
	BuildingSet.Reserve(BuildingMeshIndices.Num());
	for (const int32 Idx : BuildingMeshIndices)
	{
		BuildingSet.Add(Idx);
	}
	for (int32 i = 0; i < SceneryMeshes.Num(); ++i)
	{
		UProceduralMeshComponent* Proc = SceneryMeshes[i];
		if (!Proc || Proc->bHiddenInGame)
		{
			continue;
		}
		const bool bBuilding = BuildingSet.Contains(i);
		const float MinRadius = bBuilding ? 0.f : MinPropRadiusCm;
		const FBoxSphereBounds B = Proc->CalcBounds(Proc->GetComponentTransform());
		if (!bBuilding && B.SphereRadius < MinRadius)
		{
			continue;
		}
		const float Dist = FMath::Max(0.f,
			static_cast<float>(FVector::Dist(B.Origin, CameraWorld) - B.SphereRadius));
		const float DistLimit = (Proc->CastShadow && Proc->bCastDynamicShadow)
			? MaxDistCm * 1.2f
			: MaxDistCm;
		if (Dist > DistLimit)
		{
			continue;
		}
		FACESceneryShadowCand C;
		C.Proc = Proc;
		C.DistCm = Dist;
		C.SphereRadius = B.SphereRadius;
		C.Kind = bBuilding ? 0 : 1;
		Out.Add(C);
	}
	for (AACERegionSceneryActor* Actor : AnimatedScenery)
	{
		if (!IsValid(Actor) || !Actor->Appearance || Actor->IsHidden())
		{
			continue;
		}
		FBox Box;
		float Dist = static_cast<float>(FVector::Dist(Actor->GetActorLocation(), CameraWorld));
		float Radius = 0.f;
		if (Actor->Appearance->GetVisualWorldBounds(Box) && Box.IsValid)
		{
			Radius = static_cast<float>(Box.GetExtent().GetMax());
			Dist = FMath::Max(0.f, static_cast<float>(FVector::Dist(Box.GetCenter(), CameraWorld) - Radius));
		}
		const bool bCasting = Actor->Appearance && Actor->Appearance->GetPartsCastShadow();
		const float DistLimit = bCasting ? MaxDistCm * 1.2f : MaxDistCm;
		if (Radius < MinPropRadiusCm || Dist > DistLimit)
		{
			continue;
		}
		FACESceneryShadowCand C;
		C.Animated = Actor;
		C.DistCm = Dist;
		C.SphereRadius = Radius;
		C.Kind = 2;
		Out.Add(C);
	}
}

void AACELandblockActor::ForceDisableShadowCasting()
{
	for (UProceduralMeshComponent* Proc : SceneryMeshes)
	{
		if (!Proc)
		{
			continue;
		}
		if (Proc->CastShadow || Proc->bCastDynamicShadow)
		{
			Proc->SetCastShadow(false);
			Proc->bCastDynamicShadow = false;
			Proc->bCastInsetShadow = false;
			Proc->bCastContactShadow = false;
		}
	}
	for (const FBuildingShell& Shell : BuildingShells)
	{
		if (UStaticMeshComponent* Mesh = Shell.Mesh)
		{
			Mesh->SetCastShadow(false);
			Mesh->bCastDynamicShadow = false;
			Mesh->bCastInsetShadow = false;
			Mesh->bCastContactShadow = false;
		}
	}
	for (const auto& Pair : SceneryHisms)
	{
		if (UInstancedStaticMeshComponent* Hism = Pair.Value)
		{
			Hism->SetCastShadow(false);
			Hism->bCastDynamicShadow = false;
		}
	}
	for (AACERegionSceneryActor* Actor : AnimatedScenery)
	{
		if (IsValid(Actor) && Actor->Appearance)
		{
			Actor->Appearance->SetPartsCastShadow(false, /*bInset*/ false);
		}
	}
}

void AACELandblockActor::ApplyDegradeCull(float EndCullCm)
{
	AppliedDegradeEndCullCm = FMath::Max(0.f, EndCullCm);
	const float StartCull = AppliedDegradeEndCullCm > 1.f ? AppliedDegradeEndCullCm * 0.85f : 0.f;
	const float EndCull = AppliedDegradeEndCullCm > 1.f ? AppliedDegradeEndCullCm : 0.f;
	for (const auto& Pair : SceneryHisms)
	{
		if (UInstancedStaticMeshComponent* Hism = Pair.Value)
		{
			Hism->bNeverDistanceCull = EndCull <= 1.f;
			Hism->SetCullDistances(StartCull, EndCull);
		}
	}
}

void AACELandblockActor::ApplyOutdoorDoorwayClips(const TArray<FACEBuildingDoorwayClip>& Clips)
{
    // Door openings are authored transparent polygons in the DAT model. A
    // world-space rectangular slab erased adjacent walls, jambs and shadows.
    auto ClearLegacyClip = [](UMeshComponent* Mesh)
    {
        if (!Mesh) return;
        for (int32 I=0; I<Mesh->GetNumMaterials(); ++I)
            if (auto* Mid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(I)); Mid && Mid->GetOuter()==Mesh)
            {
                Mid->SetVectorParameterValue(TEXT("Door0Extent"),FLinearColor::Transparent);
                Mid->SetVectorParameterValue(TEXT("Door1Extent"),FLinearColor::Transparent);
            }
    };
    for (const auto& Shell : BuildingShells) ClearLegacyClip(Shell.Mesh);
    for (int32 I : BuildingMeshIndices) if (SceneryMeshes.IsValidIndex(I)) ClearLegacyClip(SceneryMeshes[I]);

}

void AACELandblockActor::ApplyWorldDistanceFog(UACEDatSubsystem* Dat)
{
	if (!Dat)
	{
		return;
	}
	auto ApplyMesh = [Dat](UMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}
		const int32 NumMats = Mesh->GetNumMaterials();
		for (int32 Si = 0; Si < NumMats; ++Si)
		{
			if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Si)))
			{
				Dat->ApplyDistanceFogToMaterial(Mid);
			}
		}
	};
	ApplyMesh(TerrainMesh);
	for (const FBuildingShell& Shell : BuildingShells)
	{
		ApplyMesh(Shell.Mesh);
	}
	for (UProceduralMeshComponent* Mesh : SceneryMeshes)
	{
		ApplyMesh(Mesh);
	}
	for (const auto& Pair : SceneryHisms)
	{
		ApplyMesh(Pair.Value);
	}
}

void AACELandblockActor::ApplySceneryShadowWinners(
	const TArray<UProceduralMeshComponent*>& WinningProcs,
	const TArray<AACERegionSceneryActor*>& WinningAnimated,
	const FVector& CameraWorld,
	float MaxDistCm,
	float StickyDistCm)
{
	auto IsWinningProc = [&WinningProcs](const UProceduralMeshComponent* Proc) -> bool
	{
		return WinningProcs.Contains(Proc);
	};
	auto IsWinningAnim = [&WinningAnimated](const AACERegionSceneryActor* Actor) -> bool
	{
		return WinningAnimated.Contains(Actor);
	};
	TSet<int32> BuildingSet;
	for (const int32 Idx : BuildingMeshIndices)
	{
		BuildingSet.Add(Idx);
	}
	for (int32 i = 0; i < SceneryMeshes.Num(); ++i)
	{
		UProceduralMeshComponent* Proc = SceneryMeshes[i];
		if (!Proc)
		{
			continue;
		}
		const FBoxSphereBounds B = Proc->CalcBounds(Proc->GetComponentTransform());
		const float Dist = FMath::Max(0.f,
			static_cast<float>(FVector::Dist(B.Origin, CameraWorld) - B.SphereRadius));
		const bool bSticky = Proc->CastShadow && Proc->bCastDynamicShadow && Dist <= StickyDistCm;
		const bool bBuildingMesh = BuildingSet.Contains(i);
		const bool bCast = (bBuildingMesh && Dist <= MaxDistCm) || IsWinningProc(Proc) || bSticky;
		if (static_cast<bool>(Proc->CastShadow) != bCast || Proc->bCastDynamicShadow != bCast)
		{
			Proc->SetCastShadow(bCast);
			Proc->bCastDynamicShadow = bCast;
			if (BuildingSet.Contains(i))
			{
				Proc->bCastShadowAsTwoSided = true;
			}
			Proc->MarkRenderStateDirty();
		}
	}
	for (AACERegionSceneryActor* Actor : AnimatedScenery)
	{
		if (!IsValid(Actor) || !Actor->Appearance)
		{
			continue;
		}
		const bool bCast = IsWinningAnim(Actor);
		if (Actor->Appearance->GetPartsCastShadow() != bCast)
		{
			Actor->Appearance->SetPartsCastShadow(bCast, /*bInset*/ false);
		}
	}
	constexpr float BuildingShadowDistCm = 80000.f;
	for (const FBuildingShell& Shell : BuildingShells)
	{
		UStaticMeshComponent* Mesh = Shell.Mesh;
		if (!Mesh || Mesh->bHiddenInGame)
		{
			continue;
		}
		const FBoxSphereBounds B = Mesh->CalcBounds(Mesh->GetComponentTransform());
		const float Dist = FMath::Max(0.f,
			static_cast<float>(FVector::Dist(B.Origin, CameraWorld) - B.SphereRadius));
		const bool bCast = Dist <= BuildingShadowDistCm;
		if (static_cast<bool>(Mesh->CastShadow) != bCast || Mesh->bCastDynamicShadow != bCast || !Mesh->bCastShadowAsTwoSided)
		{
			Mesh->SetCastShadow(bCast);
			Mesh->bCastDynamicShadow = bCast;
			Mesh->bCastShadowAsTwoSided = true;
			Mesh->bCastInsetShadow = false;
			Mesh->bCastContactShadow = false;
			Mesh->MarkRenderStateDirty();
		}
	}
	constexpr float HismShadowDistCm = 20000.f;
	for (const auto& Pair : SceneryHisms)
	{
		UInstancedStaticMeshComponent* Hism = Pair.Value;
		if (!Hism)
		{
			continue;
		}
		const FBoxSphereBounds B = Hism->CalcBounds(Hism->GetComponentTransform());
		const float Dist = FMath::Max(0.f,
			static_cast<float>(FVector::Dist(B.Origin, CameraWorld) - B.SphereRadius));
		const bool bCast = Dist <= HismShadowDistCm;
		if (static_cast<bool>(Hism->CastShadow) != bCast || Hism->bCastDynamicShadow != bCast)
		{
			Hism->SetCastShadow(bCast);
			Hism->bCastDynamicShadow = bCast;
			Hism->bCastShadowAsTwoSided = bCast;
			Hism->MarkRenderStateDirty();
		}
	}
}

UInstancedStaticMeshComponent* AACELandblockActor::GetOrCreateSceneryHism(
	UACEDatSubsystem* Dat, uint32 SetupId, bool bEnableCollision, int32 PlacementId)
{
	const uint64 MeshKey = SceneryMeshKey(SetupId, PlacementId, bEnableCollision);
	if (TObjectPtr<UInstancedStaticMeshComponent>* Existing = SceneryHisms.Find(MeshKey))
	{
		return Existing->Get();
	}

	UStaticMesh* StaticMesh = Dat->GetOrCreateSetupStaticMesh(
		SetupId, WorldScale, bEnableCollision, PlacementId, /*bParticleGfx*/ false, /*bStencilHoleClip*/ false, /*bOutdoorLit*/ true);
	if (!StaticMesh)
	{
		return nullptr;
	}

	const FName CompName = *FString::Printf(TEXT("SceneryISM_%08X_%d_%d"), SetupId, PlacementId, bEnableCollision);
	UInstancedStaticMeshComponent* Hism = NewObject<UInstancedStaticMeshComponent>(this, CompName);
	if (!Hism)
	{
		return nullptr;
	}
	Hism->SetupAttachment(GetRootComponent());
	Hism->SetStaticMesh(StaticMesh);
	Hism->SetMobility(EComponentMobility::Movable);
	Dat->BindSetupStaticMeshMaterials(
		Hism, SetupId, WorldScale, bEnableCollision, PlacementId, /*bParticleGfx*/ false, /*bStencilHoleClip*/ false, /*bOutdoorLit*/ true);
	// HISM cluster culling hid every copy except the one live animated actor. ISM +
	// never-distance-cull keeps trees/cacti visible at town range.
	Hism->bNeverDistanceCull = AppliedDegradeEndCullCm <= 1.f;
	Hism->SetCullDistances(0.f, 0.f);
	Hism->LDMaxDrawDistance = 0.f;
	Hism->SetCachedMaxDrawDistance(0.f);
	Hism->InstanceStartCullDistance = 0.f;
	Hism->InstanceEndCullDistance = 0.f;
	Hism->bUseAsOccluder = false;
	if (AppliedDegradeEndCullCm > 1.f)
	{
		Hism->bNeverDistanceCull = false;
		Hism->SetCullDistances(AppliedDegradeEndCullCm * 0.85f, AppliedDegradeEndCullCm);
	}
	Hism->bUseDefaultCollision = false;
	Hism->SetCastShadow(true);
	Hism->bCastDynamicShadow = true;
	Hism->bCastShadowAsTwoSided = true;
	Hism->bCastContactShadow = false;
	Hism->bCastFarShadow = false;
	Hism->bCastInsetShadow = false;
	if (bEnableCollision && StaticMesh->GetBodySetup())
	{
		Hism->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Hism->SetCollisionObjectType(ECC_WorldStatic);
		Hism->SetCollisionResponseToAllChannels(ECR_Block);
		Hism->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	else
	{
		Hism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Hism->RegisterComponent();
	SceneryHisms.Add(MeshKey, Hism);
	return Hism;
}

bool AACELandblockActor::TrySpawnOneScenery(UACEDatSubsystem* Dat, const FPendingScenery& Item)
{
	if (!Dat || Item.ModelId == 0)
	{
		return true; // drop
	}

	// Retail PhysicsObj.InitObjectEnd uses Placement.Resting (101) for static Stab scenery.
	const int32 PlacementId = (!Item.bBuilding && !Item.bRegionDesc)
		? UACEDatSubsystem::ACEPlacementResting
		: 0;

	float IgnoredStep = 0.f, IgnoredH = 0.f, IgnoredR = 0.f;
	uint32 DefaultAnim = 0;
	Dat->TryGetSetupPhysics(Item.ModelId, IgnoredStep, IgnoredH, IgnoredR, DefaultAnim);
	uint32 DefaultScript = 0, ScriptTableId = 0, SoundTableId = 0;
	Dat->TryGetSetupRuntimeMetadata(Item.ModelId, DefaultScript, ScriptTableId, SoundTableId);
	const bool bPhysicsScript = DefaultScript != 0 && (DefaultScript & 0xFF000000u) == 0x33000000u;

	const FQuat WorldQuat = FACEPosition::AceQuatToUnreal(
		Item.Orientation.W, FVector(Item.Orientation.X, Item.Orientation.Y, Item.Orientation.Z));
	const FVector RelLoc = FACEPosition::AceVectorToUnreal(
		FVector(Item.Origin.X, Item.Origin.Y, Item.Origin.Z), WorldScale);
	const float Scale = Item.Scale > KINDA_SMALL_NUMBER ? Item.Scale : 1.f;
	const FTransform RelXform(WorldQuat, RelLoc, FVector(Scale));

	// Live actor: DefaultAnimation (birds) plus PhysicsScript props (torches). ISM cannot
	// play scripts — instancing extra torches left them unlit. Spawn every PhysicsScript
	// stab; RegionDesc birds stay capped.
	constexpr int32 MaxLiveRegionAnim = 3;
	if (Item.bRegionDesc && DefaultAnim != 0 && LiveRegionAnimCount >= MaxLiveRegionAnim)
	{
		return true;
	}
	const bool bNeedsLiveActor = !Item.bBuilding && (
		DefaultAnim != 0
		|| bPhysicsScript);
	if (bNeedsLiveActor)
	{
		const UACEDatSubsystem::EACESetupMeshStatus Status = Dat->RequestSetupMesh(
			Item.ModelId, WorldScale, PlacementId);
		if (Status == UACEDatSubsystem::EACESetupMeshStatus::Pending
			|| Status == UACEDatSubsystem::EACESetupMeshStatus::NotReady)
		{
			return false;
		}
		UWorld* World = GetWorld();
		if (!World)
		{
			return true;
		}
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AACERegionSceneryActor* Scenery = World->SpawnActor<AACERegionSceneryActor>(
			AACERegionSceneryActor::StaticClass(), GetActorTransform(), Params);
		if (!Scenery)
		{
			return true;
		}
		Scenery->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
		if (!Scenery->InitializeFromSetup(static_cast<int32>(Item.ModelId), 1.f, WorldScale, /*bEnableCollision*/ true))
		{
			Scenery->Destroy();
			return false;
		}
		Scenery->SetActorRelativeTransform(RelXform);
		if (UACECharacterAppearanceComponent* App = Scenery->Appearance)
		{
			App->SetPartsCastShadow(false, /*bInset*/ false);
			App->SetComponentTickEnabled(true);
		}
		if (UACEScriptComponent* Scripts = Scenery->ScriptComponent)
		{
			Scripts->SetComponentTickEnabled(true);
		}
		ClaimedLiveSceneryModels.Add(Item.ModelId);
		AnimatedScenery.Add(Scenery);
		if (Item.bRegionDesc && DefaultAnim != 0)
		{
			++LiveRegionAnimCount;
		}
		if (bIndoorScenerySuppressed)
		{
			Scenery->SetActorHiddenInGame(true);
			Scenery->SetActorEnableCollision(false);
			if (UACECharacterAppearanceComponent* App = Scenery->Appearance)
			{
				App->SetComponentTickEnabled(false);
			}
			if (UACEScriptComponent* Scripts = Scenery->ScriptComponent)
			{
				Scripts->SetComponentTickEnabled(false);
				Scripts->StopAllSounds();
				Scripts->StopAllEffects();
			}
		}
		return true;
	}

	// Buildings/stabs/RegionDesc flora: async Request gate — sync Apply after portal
	// was baking hundreds of trees on the game thread.
	{
		const UACEDatSubsystem::EACESetupMeshStatus Status = Dat->RequestSetupMesh(Item.ModelId, WorldScale, PlacementId);
		if (Status == UACEDatSubsystem::EACESetupMeshStatus::Pending
			|| Status == UACEDatSubsystem::EACESetupMeshStatus::NotReady)
		{
			return false;
		}
		if (Status == UACEDatSubsystem::EACESetupMeshStatus::Failed)
		{
			Dat->AllowSetupMeshRetry(Item.ModelId, WorldScale, PlacementId);
		}
	}

	// CLandBlock::generate_objects creates RegionDesc scenery with the same
	// CPhysicsObj::makeObject path as stabs. Use its authored PhysicsBSP: blanket
	// ethereal treatment removed tree trunks and rocks along with empty foliage.
	const bool bCollide = true;
	if (Item.bBuilding)
	{
		UStaticMesh* StaticMesh = Dat->GetOrCreateSetupStaticMesh(
			Item.ModelId, WorldScale, bCollide, PlacementId, /*bParticleGfx*/ false, /*bStencilHoleClip*/ true);
		if (StaticMesh)
		{
			const FName CompName = *FString::Printf(TEXT("BldgSM_%08X_%d"), Item.ModelId, BuildingShells.Num());
			UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, CompName);
			if (!Comp)
			{
				return true;
			}
			Comp->SetupAttachment(GetRootComponent());
			Comp->SetStaticMesh(StaticMesh);
			Dat->BindSetupStaticMeshMaterials(
				Comp, Item.ModelId, WorldScale, bCollide, PlacementId, /*bParticleGfx*/ false, /*bStencilHoleClip*/ true);
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetCastShadow(true);
			Comp->bCastDynamicShadow = true;
			Comp->bCastContactShadow = false;
			Comp->bCastFarShadow = false;
			Comp->bCastInsetShadow = false;
			// Retail shells contain thin roof/wall faces. Shadow depth must cover
			// both sides without making their main-pass drawing two-sided.
			Comp->bCastShadowAsTwoSided = true;
			Comp->SetRelativeTransform(RelXform);
			Comp->LDMaxDrawDistance = 0.f;
			Comp->SetCachedMaxDrawDistance(0.f);
			Comp->bNeverDistanceCull = true;
			// Shader doorway clips discard pixels; HZB still uses the solid Setup bounds
			// and hid every EnvCell inside the shop.
			Comp->bUseAsOccluder = false;
			if (bCollide)
			{
				Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Comp->SetCollisionObjectType(ECC_WorldStatic);
				Comp->SetCollisionResponseToAllChannels(ECR_Block);
				Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
				Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
				Comp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
				Comp->CanCharacterStepUpOn = ECB_Yes;
				Comp->ComponentTags.AddUnique(FName(TEXT("ACEBuildingShell")));
			}
			else
			{
				Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			Comp->RegisterComponent();
			FBuildingShell Shell;
			Shell.InfoIndex = Item.LandblockBuildingIndex;
			Shell.Mesh = Comp;
			if (bCollide)
			{
				UStaticMesh* PhysMesh = Dat->GetOrCreateSetupStaticMesh(
					Item.ModelId, WorldScale, true, PlacementId, /*bParticleGfx*/ false,
					/*bStencilHoleClip*/ false, /*bOutdoorLit*/ false, /*bPhysicsCollisionOnly*/ true);
				if (PhysMesh)
				{
					const FName PhysName = *FString::Printf(TEXT("BldgPhys_%08X_%d"), Item.ModelId, BuildingShells.Num());
					UStaticMeshComponent* Phys = NewObject<UStaticMeshComponent>(this, PhysName);
					if (Phys)
					{
						Phys->SetupAttachment(GetRootComponent());
						Phys->SetStaticMesh(PhysMesh);
						Dat->BindSetupStaticMeshMaterials(
							Phys, Item.ModelId, WorldScale, true, PlacementId, false, false, false, true);
						Phys->SetMobility(EComponentMobility::Movable);
						Phys->SetCastShadow(false);
						Phys->bCastDynamicShadow = false;
						Phys->SetHiddenInGame(true);
						Phys->SetVisibility(false);
						Phys->bUseAsOccluder = false;
						Phys->SetRelativeTransform(RelXform);
						Phys->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
						Phys->SetCollisionObjectType(ECC_WorldStatic);
						Phys->SetCollisionResponseToAllChannels(ECR_Block);
						Phys->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
						Phys->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
						Phys->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
						Phys->CanCharacterStepUpOn = ECB_Yes;
						Phys->ComponentTags.AddUnique(FName(TEXT("ACEBuildingShell")));
						Phys->RegisterComponent();
						Shell.CollisionMesh = Phys;
					}
				}
			}
			BuildingShells.Add(Shell);
			if (IndoorHideBuildingInfoIndices.Contains(Item.LandblockBuildingIndex))
			{
				Comp->SetHiddenInGame(true);
				Comp->SetVisibility(false);
				Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				if (Shell.CollisionMesh)
				{
					Shell.CollisionMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				}
			}
			return true;
		}
	}

	// Shared ID3DXMesh analogue: one UStaticMesh per Setup, instanced for flora/stabs.
	if (!Item.bBuilding)
	{
		if (UInstancedStaticMeshComponent* Hism = GetOrCreateSceneryHism(Dat, Item.ModelId, bCollide, PlacementId))
		{
			// Some authored stab lists repeat exactly the same placement. Drawing
			// and colliding it twice adds work and can create coplanar flicker.
			for (int32 I = 0; I < Hism->GetInstanceCount(); ++I)
			{
				FTransform Existing;
				if (Hism->GetInstanceTransform(I, Existing, false) && Existing.Equals(RelXform, 1.e-6)) return true;
			}
			Hism->AddInstance(RelXform, /*bWorldSpace*/ false);
			Hism->MarkRenderStateDirty();
			return true;
		}
	}

	// PMC fallback when StaticMesh cook fails (unknown Setup / empty mesh).
	const TCHAR* Kind = Item.bBuilding ? TEXT("Bldg") : (Item.bRegionDesc ? TEXT("Region") : TEXT("Stab"));
	const FName CompName = *FString::Printf(TEXT("Scenery_%s_%08X_%d"), Kind, Item.ModelId, SceneryMeshes.Num());
	UProceduralMeshComponent* Proc = NewObject<UProceduralMeshComponent>(this, CompName);
	if (!Proc)
	{
		return true;
	}
	Proc->SetupAttachment(GetRootComponent());
	Proc->RegisterComponent();
	// Buildings stay sync so pawn collision exists the same frame they spawn.
	// Outdoor stabs and flora async-cook — Yaraq's ~90 colliding stabs per LB were a hitch.
	Proc->bUseAsyncCooking = !Item.bBuilding;
	Proc->bUseComplexAsSimpleCollision = true;
	// LOD enables nearby casters. Spawning every unique PMC with CastShadow on is the
	// Ayan pop + hitch (whole-scene cache recast of the new ring).
	Proc->SetCastShadow(false);
	Proc->bCastDynamicShadow = false;
	Proc->bCastContactShadow = false;
	Proc->bCastFarShadow = false;
	Proc->bCastInsetShadow = false;
	// Thin building shells block light from either side.
	Proc->bCastShadowAsTwoSided = Item.bBuilding;

	// Buildings + landblock Stabs: PhysicsBSP + walkable draw faces. Outdoor wooden stairs /
	// stoops are often Stab scenery (not Building shells) with soft-alpha + empty PhysicsBSP —
	// forceDraw=0 left ZERO collision (log spam 0x020002F3–F5). Region flora uses authored physics only.
	const bool bForceDrawCollision = Item.bBuilding || (!Item.bRegionDesc && bCollide);
	const bool bIndoorStairCollision = Item.bBuilding;
	if (!Dat->ApplySetupToProceduralMesh(Proc, static_cast<int32>(Item.ModelId), WorldScale, bCollide, PlacementId,
		bForceDrawCollision, bIndoorStairCollision, /*bOutdoorLit*/ true, /*bReverseFaces*/ Item.bBuilding))
	{
		UE_LOG(LogTemp, Verbose, TEXT("ACE: scenery Setup 0x%08X failed to build (%s)"), Item.ModelId, Kind);
		Proc->DestroyComponent();
		return true;
	}

	if (bCollide)
	{
		Proc->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Proc->SetCollisionObjectType(ECC_WorldStatic);
		Proc->SetCollisionResponseToAllChannels(ECR_Block);
		Proc->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Proc->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Proc->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		// Buildings + outdoor Stab stairs: allow step onto ledges. Region flora stays off.
		Proc->CanCharacterStepUpOn = bForceDrawCollision ? ECB_Yes : ECB_No;
		Proc->RecreatePhysicsState();
	}
	else
	{
		Proc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Proc->CanCharacterStepUpOn = ECB_No;
	}
	Proc->SetRelativeTransform(RelXform);
	if (Item.bBuilding)
	{

	}
	Proc->UpdateBounds();
	if (bCollide && !Item.bBuilding && !Item.bRegionDesc)
	{
		// Deck/dock tops are thin walkable tris — the capsule walks through the edge.
		// A hidden slab at the mesh top blocks side entry without filling the volume
		// under the dock (pilings stay in the XY extent, Z stays at the deck).
		const FBoxSphereBounds LocalB = Proc->GetLocalBounds();
		if (LocalB.BoxExtent.X > 40.f && LocalB.BoxExtent.Y > 40.f)
		{
			UBoxComponent* Slab = NewObject<UBoxComponent>(this);
			if (Slab)
			{
				Slab->SetupAttachment(Proc);
				Slab->SetRelativeLocation(LocalB.Origin + FVector(0.f, 0.f, LocalB.BoxExtent.Z - 18.f));
				Slab->SetBoxExtent(FVector(LocalB.BoxExtent.X, LocalB.BoxExtent.Y, 18.f));
				Slab->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Slab->SetCollisionObjectType(ECC_WorldStatic);
				Slab->SetCollisionResponseToAllChannels(ECR_Block);
				Slab->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
				Slab->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
				Slab->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				Slab->CanCharacterStepUpOn = ECB_No;
				Slab->SetHiddenInGame(true);
				Slab->SetCastShadow(false);
				Slab->RegisterComponent();
			}
		}
	}
	if (Item.bBuilding)
	{
		Proc->SetCastShadow(true);
		Proc->bCastDynamicShadow = true;
		Proc->bCastShadowAsTwoSided = true;
	}
	Proc->MarkRenderStateDirty();
	const int32 MeshIndex = SceneryMeshes.Add(Proc);
	if (Item.bBuilding)
	{
		BuildingMeshIndices.Add(MeshIndex);
		BuildingInfoIndices.Add(Item.LandblockBuildingIndex);
		Proc->ComponentTags.AddUnique(FName(TEXT("ACEBuildingShell")));
		Proc->CanCharacterStepUpOn = ECB_No;
		const bool bHideThisShell = IndoorHideBuildingInfoIndices.Contains(Item.LandblockBuildingIndex);
		if (bHideThisShell)
		{
			Proc->SetHiddenInGame(true);
			Proc->SetVisibility(false);
			Proc->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Proc->SetCollisionObjectType(ECC_WorldStatic);
			Proc->SetCollisionResponseToAllChannels(ECR_Ignore);
			Proc->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
			Proc->RecreatePhysicsState();
		}
	}
	else if (bIndoorScenerySuppressed)
	{
		Proc->SetHiddenInGame(true);
		Proc->SetVisibility(false);
		Proc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	return true;
}

void AACELandblockActor::QueueScenery(UACEDatSubsystem* Dat)
{
	ClearScenery();
	if (!Dat)
	{
		return;
	}

	bSceneryQueued = true;
	FACEDatLandblockInfo Info;
	const bool bHaveInfo = Dat->LoadLandblockInfo(static_cast<uint32>(LandblockId), Info);
	if (bHaveInfo)
	{
		PendingScenery.Reserve(Info.Objects.Num() + Info.Buildings.Num());
		for (int32 Bi = 0; Bi < Info.Buildings.Num(); ++Bi)
		{
			const FACEDatLandblockBuilding& Building = Info.Buildings[Bi];
			if (Building.ModelId == 0)
			{
				continue;
			}
			FPendingScenery Item;
			Item.ModelId = Building.ModelId;
			Item.Origin = Building.Origin;
			Item.Orientation = Building.Orientation;
			Item.bBuilding = true;
			Item.LandblockBuildingIndex = Bi;
			PendingScenery.Add(Item);
			Dat->RequestSetupMesh(Building.ModelId, WorldScale);
		}
		for (const FACEDatLandblockStab& Stab : Info.Objects)
		{
			if (Stab.Id == 0)
			{
				continue;
			}
			FPendingScenery Item;
			Item.ModelId = Stab.Id;
			Item.Origin = Stab.Origin;
			Item.Orientation = Stab.Orientation;
			Item.bBuilding = false;
			PendingScenery.Add(Item);
			Dat->RequestSetupMesh(Stab.Id, WorldScale, UACEDatSubsystem::ACEPlacementResting);
		}

		UE_LOG(LogTemp, Log, TEXT("ACE: Landblock 0x%08X scenery queued — %d buildings, %d stabs"),
			LandblockId, Info.Buildings.Num(), Info.Objects.Num());
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("ACE: Landblock 0x%08X has no LandblockInfo — RegionDesc only"), LandblockId);
	}

	TryQueueRegionDesc(Dat);

	if (PendingScenery.Num() > 0 || bRegionDescCollectPending)
	{
		SetActorTickEnabled(true);
	}
}

bool AACELandblockActor::TryQueueRegionDesc(UACEDatSubsystem* Dat)
{
	if (!Dat)
	{
		bRegionDescCollectPending = false;
		return false;
	}

	TArray<FACEDatRegionSceneryItem> RegionItems;
	if (!Dat->CollectRegionScenery(static_cast<uint32>(LandblockId), WorldScale, RegionItems))
	{
		bRegionDescCollectPending = true;
		SetActorTickEnabled(true);
		return false;
	}

	bRegionDescCollectPending = false;
	for (const FACEDatRegionSceneryItem& Region : RegionItems)
	{
		FPendingScenery Item;
		Item.ModelId = Region.SetupId;
		Item.Origin = FVector3f(Region.OriginAc);
		Item.Orientation = FQuat4f(
			Region.Orientation.X, Region.Orientation.Y, Region.Orientation.Z, Region.Orientation.W);
		Item.Scale = Region.Scale;
		Item.bBuilding = false;
		Item.bRegionDesc = true;
		PendingScenery.Add(Item);
		Dat->RequestSetupMesh(Region.SetupId, WorldScale);
	}
	UE_LOG(LogTemp, Log, TEXT("ACE: Landblock 0x%08X RegionDesc scenery +%d"),
		LandblockId, RegionItems.Num());
	return true;
}

bool AACELandblockActor::LoadLandblockSceneryOnly(int32 InLandblockId, float InWorldScale, bool bInFullResDetail)
{
	LandblockId = InLandblockId & 0xFFFF0000;
	WorldScale = InWorldScale;

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		if (UWorld* World = GetWorld())
		{
			GI = World->GetGameInstance();
		}
	}
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return false;
	}

	// Empty collision on child — chunk owns walkable terrain.
	if (TerrainMesh)
	{
		TerrainMesh->ClearAllMeshSections();
		TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TerrainMesh->SetVisibility(false);
		TerrainMesh->SetHiddenInGame(true);
	}

	const uint32 Cell = static_cast<uint32>(LandblockId);
	const uint32 Lbx = (Cell >> 24) & 0xFF;
	const uint32 Lby = (Cell >> 16) & 0xFF;
	const FVector Origin = FACEPosition::AceVectorToUnreal(FVector(Lbx * 192.f, Lby * 192.f, 0.f), WorldScale);
	SetActorLocation(Origin);

	bFullResDetail = bInFullResDetail;
	if (bFullResDetail)
	{
		QueueScenery(Dat);
	}
	return true;
}

bool AACELandblockActor::LoadLandblock(int32 InLandblockId, float InWorldScale, bool bInFullResDetail, int32 PolySize, int32 TransDir)
{
	LandblockId = InLandblockId & 0xFFFF0000;
	WorldScale = InWorldScale;

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		if (UWorld* World = GetWorld())
		{
			GI = World->GetGameInstance();
		}
	}
	if (!GI)
	{
		UE_LOG(LogTemp, Error, TEXT("ACE: LoadLandblock 0x%08X — no GameInstance"), LandblockId);
		return false;
	}

	UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>();
	const int32 UsePoly = (PolySize == 2 || PolySize == 4 || PolySize == 8) ? PolySize : 1;
	const int32 UseDir = (UsePoly > 1) ? TransDir : 0;
	if (!Dat || !Dat->ApplyLandblockToProceduralMesh(TerrainMesh, LandblockId, WorldScale, UsePoly, UseDir))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE: LoadLandblock 0x%08X — failed (DatReady=%d); will retry on next sync"),
			LandblockId, (Dat && Dat->IsDatReady()) ? 1 : 0);
		return false;
	}
	AppliedPolySize = UsePoly;
	AppliedTransDir = UseDir;
	ApplyDesiredOutdoorTerrainState();

	const uint32 Cell = static_cast<uint32>(LandblockId);
	const uint32 Lbx = (Cell >> 24) & 0xFF;
	const uint32 Lby = (Cell >> 16) & 0xFF;
	const FVector Origin = FACEPosition::AceVectorToUnreal(FVector(Lbx * 192.f, Lby * 192.f, 0.f), WorldScale);
	SetActorLocation(Origin);

	bFullResDetail = bInFullResDetail;
	if (bFullResDetail)
	{
		QueueScenery(Dat);
	}
	return true;
}

bool AACELandblockActor::ReapplyOutdoorTerrain()
{
	if (!TerrainMesh || LandblockId == 0)
	{
		return false;
	}
	// Scenery-only children under chunks keep an empty hidden TerrainMesh.
	if (!TerrainMesh->IsVisible() && TerrainMesh->GetNumSections() == 0
		&& TerrainMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		return false;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		if (UWorld* World = GetWorld())
		{
			GI = World->GetGameInstance();
		}
	}
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return false;
	}
	Dat->InvalidateBuildingInteriorFootprints(static_cast<uint32>(LandblockId));
	const bool bOk = Dat->ApplyLandblockToProceduralMesh(TerrainMesh, LandblockId, WorldScale, AppliedPolySize, AppliedTransDir);
	ApplyDesiredOutdoorTerrainState();
	return bOk;
}
