#include "ACETerrainChunkActor.h"
#include "ACELandblockActor.h"
#include "ACEDatSubsystem.h"
#include "ACETypes.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"

AACETerrainChunkActor::AACETerrainChunkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	TerrainMesh->bPreferCachedDraws = true;
	SetRootComponent(TerrainMesh);
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TerrainMesh->SetCollisionResponseToAllChannels(ECR_Block);
	TerrainMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	TerrainMesh->bUseAsyncCooking = true;
	TerrainMesh->bUseComplexAsSimpleCollision = true;
	TerrainMesh->SetMobility(EComponentMobility::Movable);
	TerrainMesh->SetCastShadow(false);
	TerrainMesh->bCastDynamicShadow = false;
	TerrainMesh->bCastFarShadow = false;
	TerrainMesh->bCastInsetShadow = false;
	TerrainMesh->ComponentTags.Add(FName(TEXT("ACEOutdoorTerrain")));
}

void AACETerrainChunkActor::SetOutdoorTerrainCollisionEnabled(bool bEnabled)
{
	bWantTerrainCollision = bEnabled;
	ApplyDesiredOutdoorTerrainState();
}

void AACETerrainChunkActor::SetOutdoorTerrainHiddenInGame(bool bHideTerrain)
{
	bWantTerrainHidden = bHideTerrain;
	if (bHideTerrain)
	{
		bWantTerrainCollision = false;
	}
	ApplyDesiredOutdoorTerrainState();
}

void AACETerrainChunkActor::ApplyDesiredOutdoorTerrainState()
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

void AACETerrainChunkActor::SetLandPortalLookOut(bool bLookOut, UACEDatSubsystem* Dat)
{
	if (!TerrainMesh || !Dat || TerrainMesh->GetNumSections() <= 0)
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

void AACETerrainChunkActor::SetLandEnvCellFloorPriority(bool bEnable, UACEDatSubsystem* Dat, bool bIndoorLookOut)
{
	if (!TerrainMesh || !Dat || TerrainMesh->GetNumSections() <= 0)
	{
		return;
	}
	// Keep rendered ground at the collision surface; only break coplanar ties.
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
		else if (Cur)
		{
			if (UMaterialInterface* Remapped = Dat->RemapLandMaterialWithDepthBias(Cur, Bias))
			{
				TerrainMesh->SetMaterial(i, Remapped);
			}
		}
	}
	TerrainMesh->MarkRenderStateDirty();
}

void AACETerrainChunkActor::SetIndoorScenerySuppressed(bool bSuppress, const TArray<int32>& HideBuildingInfoIndices,
	uint32 ShellHideLandblockId)
{
	static const TArray<int32> EmptyHide;
	for (const auto& Pair : ChildLandblocks)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const uint32 ChildLb = static_cast<uint32>(Pair.Key) & 0xFFFF0000u;
		const TArray<int32>& ShellHide = (ShellHideLandblockId != 0 && ChildLb == ShellHideLandblockId)
			? HideBuildingInfoIndices
			: EmptyHide;
		Pair.Value->SetIndoorScenerySuppressed(bSuppress, ShellHide);
	}
}

void AACETerrainChunkActor::SetBuildingShellsBlockPawn(bool bBlockPawn)
{
	SetBuildingShellsBlockPawn(bBlockPawn, TArray<int32>());
}

void AACETerrainChunkActor::SetBuildingShellsBlockPawn(bool bBlockPawn, const TArray<int32>& IgnoreBuildingIndices, uint32 IgnoreLandblock)
{
	for (const auto& Pair : ChildLandblocks)
	{
		if (Pair.Value)
		{
			Pair.Value->SetBuildingShellsBlockPawn(bBlockPawn,
				IgnoreLandblock==0 || uint32(Pair.Key)==IgnoreLandblock ? IgnoreBuildingIndices : TArray<int32>());
		}
	}
}

void AACETerrainChunkActor::ApplyDegradeCull(float EndCullCm)
{
	for (const auto& Pair : ChildLandblocks)
	{
		if (Pair.Value)
		{
			Pair.Value->ApplyDegradeCull(EndCullCm);
		}
	}
}

void AACETerrainChunkActor::ApplyOutdoorDoorwayClips(const TArray<FACEBuildingDoorwayClip>& Clips)
{
	for (const auto& Pair : ChildLandblocks)
	{
		if (Pair.Value)
		{
			Pair.Value->ApplyOutdoorDoorwayClips(Clips);
		}
	}
}

bool AACETerrainChunkActor::IsChunkTerrainReady() const
{
	return bTerrainApplied && TerrainMesh && TerrainMesh->GetNumSections() > 0
		&& (TerrainMesh->IsCollisionEnabled() || !bWantTerrainCollision);
}

bool AACETerrainChunkActor::ContainsLandblock(int32 LandblockId) const
{
	const uint32 Id = static_cast<uint32>(LandblockId) & 0xFFFF0000u;
	const int32 Ox = (ChunkOriginLandblockId >> 24) & 0xFF;
	const int32 Oy = (ChunkOriginLandblockId >> 16) & 0xFF;
	const int32 X = (Id >> 24) & 0xFF;
	const int32 Y = (Id >> 16) & 0xFF;
	return X >= Ox && X < Ox + ChunkSizeLandblocks && Y >= Oy && Y < Oy + ChunkSizeLandblocks;
}

AACELandblockActor* AACETerrainChunkActor::FindChildLandblock(int32 LandblockId) const
{
	return ChildLandblocks.FindRef(LandblockId & 0xFFFF0000);
}

bool AACETerrainChunkActor::IsChunkSceneryComplete() const
{
	for (const auto& Pair : ChildLandblocks)
	{
		if (Pair.Value && !Pair.Value->IsSceneryComplete())
		{
			return false;
		}
	}
	return ChildLandblocks.Num() > 0 || bTerrainApplied;
}

// Actor ownership/attachment does not destroy separately spawned actors in UE.
// Retail releases these objects with their owning cell/landblock.
void AACETerrainChunkActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearChildren();
    Super::EndPlay(EndPlayReason);
}

void AACETerrainChunkActor::Destroyed()
{
    ClearChildren();
    Super::Destroyed();
}

void AACETerrainChunkActor::ClearChildren()
{
	for (auto& Pair : ChildLandblocks)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	ChildLandblocks.Reset();
}

bool AACETerrainChunkActor::TryLoadChunk(UACEDatSubsystem* Dat)
{
	if (!Dat || !TerrainMesh)
	{
		return false;
	}

	const int32 Ox = (ChunkOriginLandblockId >> 24) & 0xFF;
	const int32 Oy = (ChunkOriginLandblockId >> 16) & 0xFF;
	const int32 Size = FMath::Clamp(ChunkSizeLandblocks, 1, 16);

	TArray<const FACEBuiltLandblockMesh*> Ready;
	Ready.Reserve(Size * Size);
	for (int32 DX = 0; DX < Size; ++DX)
	{
		for (int32 DY = 0; DY < Size; ++DY)
		{
			const int32 X = Ox + DX;
			const int32 Y = Oy + DY;
			if (X > 255 || Y > 255)
			{
				continue;
			}
			const uint32 LB = (static_cast<uint32>(X) << 24) | (static_cast<uint32>(Y) << 16);
			const auto Status = Dat->RequestLandblockMesh(LB, WorldScale);
			if (Status == UACEDatSubsystem::EACELandMeshStatus::Failed)
			{
				continue;
			}
			if (Status != UACEDatSubsystem::EACELandMeshStatus::Ready)
			{
				return false;
			}
			if (const FACEBuiltLandblockMesh* Mesh = Dat->FindLandblockMesh(LB))
			{
				Ready.Add(Mesh);
			}
		}
	}
	if (Ready.Num() == 0)
	{
		return false;
	}

	if (!Dat->ApplyLandblockChunkToProceduralMesh(TerrainMesh, Ready, ChunkOriginLandblockId, WorldScale))
	{
		return false;
	}
	ApplyDesiredOutdoorTerrainState();

	const FVector Origin = FACEPosition::AceVectorToUnreal(
		FVector(Ox * 192.f, Oy * 192.f, 0.f), WorldScale);
	SetActorLocation(Origin);
	bTerrainApplied = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	for (const FACEBuiltLandblockMesh* Mesh : Ready)
	{
		const int32 LB = static_cast<int32>(Mesh->LandblockId & 0xFFFF0000u);
		if (ChildLandblocks.Contains(LB))
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AACELandblockActor* Child = World->SpawnActor<AACELandblockActor>(
			AACELandblockActor::StaticClass(), GetActorTransform(), Params);
		if (!Child)
		{
			continue;
		}
		Child->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		if (Child->LoadLandblockSceneryOnly(LB, WorldScale))
		{
			ChildLandblocks.Add(LB, Child);
		}
		else
		{
			Child->Destroy();
		}
	}
	return true;
}

bool AACETerrainChunkActor::ReapplyOutdoorTerrain(UACEDatSubsystem* Dat)
{
	if (!Dat || !TerrainMesh || !bTerrainApplied)
	{
		return false;
	}
	const int32 Ox = (ChunkOriginLandblockId >> 24) & 0xFF;
	const int32 Oy = (ChunkOriginLandblockId >> 16) & 0xFF;
	const int32 Size = FMath::Clamp(ChunkSizeLandblocks, 1, 16);
	TArray<const FACEBuiltLandblockMesh*> Ready;
	for (int32 DX = 0; DX < Size; ++DX)
	{
		for (int32 DY = 0; DY < Size; ++DY)
		{
			const int32 X = Ox + DX;
			const int32 Y = Oy + DY;
			if (X > 255 || Y > 255)
			{
				continue;
			}
			const uint32 LB = (static_cast<uint32>(X) << 24) | (static_cast<uint32>(Y) << 16);
			Dat->InvalidateBuildingInteriorFootprints(LB);
			if (const FACEBuiltLandblockMesh* Mesh = Dat->FindLandblockMesh(LB))
			{
				Ready.Add(Mesh);
			}
		}
	}
	if (Ready.Num() == 0)
	{
		return false;
	}
	const bool bOk = Dat->ApplyLandblockChunkToProceduralMesh(TerrainMesh, Ready, ChunkOriginLandblockId, WorldScale);
	ApplyDesiredOutdoorTerrainState();
	return bOk;
}
