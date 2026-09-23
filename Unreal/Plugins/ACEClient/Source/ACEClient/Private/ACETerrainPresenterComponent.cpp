#include "ACETerrainPresenterComponent.h"
#include "ACEProfiling.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACELandblockActor.h"
#include "ACETerrainChunkActor.h"
#include "ACEEnvCellActor.h"
#include "ACERegionSceneryActor.h"
#include "ACEPlayerController.h"
#include "ACEWorldEntityActor.h"
#include "ACEWorldPresenterComponent.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEScriptComponent.h"
#include "ACESkyDomeActor.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEStreamingBudget.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Templates/Function.h"
#include "Engine/World.h"
#include "ACEWcTerrainRuntime.h"
#include "Engine/WorldComposition.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

namespace
{
	bool IsIndoorCellId(uint32 CellId)
	{
		return (CellId & 0xFFFFu) >= 0x0100u;
	}

	/** True when the outdoor landcell is inset from the edge facing PrevCenterKey. */
	bool IsWellInsideLandblock(uint32 CellId, int32 PrevCenterKey)
	{
		if (IsIndoorCellId(CellId) || PrevCenterKey < 0)
		{
			return true;
		}
		const int32 Lbx = static_cast<int32>((CellId >> 24) & 0xFF);
		const int32 Lby = static_cast<int32>((CellId >> 16) & 0xFF);
		const int32 PrevX = (PrevCenterKey >> 8) & 0xFF;
		const int32 PrevY = PrevCenterKey & 0xFF;
		const int32 Low = static_cast<int32>(CellId & 0xFFFFu);
		if (Low < 1)
		{
			return false;
		}
		const int32 Idx = Low - 1;
		const int32 CellX = Idx / 8;
		const int32 CellY = Idx % 8;
		if (PrevX > Lbx && CellX >= 7)
		{
			return false;
		}
		if (PrevX < Lbx && CellX <= 0)
		{
			return false;
		}
		if (PrevY > Lby && CellY >= 7)
		{
			return false;
		}
		if (PrevY < Lby && CellY <= 0)
		{
			return false;
		}
		return true;
	}

	bool TryGetEnvCellWorldBox(UACEDatSubsystem* Dat, const AACEEnvCellActor* Env, int32 CellId,
		float WorldScale, FBox& OutBox)
	{
		// Prefer DAT cell-local bounds. ProceduralMesh world bounds often swallow the
		// landblock origin and mark every plaza look-in as "on the roof."
		const uint32 U = static_cast<uint32>(CellId);
		if (Dat)
		{
			const FACEBuiltEnvCellMesh* Mesh = Dat->FindEnvCellMesh(U, WorldScale);
			if (Mesh && Mesh->bHasLocalBounds)
			{
				const uint32 Lbx = (U >> 24) & 0xFF;
				const uint32 Lby = (U >> 16) & 0xFF;
				const FVector LbOrigin = FACEPosition::AceVectorToUnreal(
					FVector(Lbx * 192.f, Lby * 192.f, 0.f), WorldScale);
				const FTransform CellToWorld = Mesh->GetCellLocalToLandblock(WorldScale)
					* FTransform(FQuat::Identity, LbOrigin);
				OutBox = FBox(Mesh->LocalBoundsMin, Mesh->LocalBoundsMax).TransformBy(CellToWorld);
				return OutBox.IsValid != 0;
			}
		}
		if (Env && Env->CellMesh)
		{
			OutBox = Env->CellMesh->Bounds.GetBox();
			return OutBox.IsValid != 0;
		}
		return false;
	}

	/** CellPortal OtherCellId is 16-bit. Housing/mansion wings often live on a neighbor LB. */
	uint32 ResolveIndoorPortalCell(UACEDatSubsystem* Dat, uint32 FromCellId, uint16 OtherCellId, float WorldScale)
	{
		if (!Dat || OtherCellId < 0x0100u || OtherCellId == 0xFFFFu)
		{
			return 0;
		}
		const uint32 Direct = (FromCellId & 0xFFFF0000u) | OtherCellId;
		if (Dat->FindEnvCellMesh(Direct, WorldScale) != nullptr)
		{
			return Direct;
		}
		FACEDatEnvCell Probe;
		if (Dat->LoadEnvCell(Direct, Probe))
		{
			return Direct;
		}
		const int32 Cx = static_cast<int32>((FromCellId >> 24) & 0xFF);
		const int32 Cy = static_cast<int32>((FromCellId >> 16) & 0xFF);
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				if (DX == 0 && DY == 0)
				{
					continue;
				}
				const int32 X = Cx + DX;
				const int32 Y = Cy + DY;
				if (X < 0 || X > 255 || Y < 0 || Y > 255)
				{
					continue;
				}
				const uint32 Neighbor = (static_cast<uint32>(X) << 24)
					| (static_cast<uint32>(Y) << 16) | OtherCellId;
				if (Dat->FindEnvCellMesh(Neighbor, WorldScale) != nullptr
					|| Dat->LoadEnvCell(Neighbor, Probe))
				{
					return Neighbor;
				}
			}
		}
		return Direct;
	}

	void AddIndoorPvsFromDat(UACEDatSubsystem* Dat, uint32 CellId, float WorldScale,
		TSet<int32>& VisibleIndoor, TSet<int32>* CollideIndoor, bool bIncludeVisibleCells = true);

	void ExpandIndoorPvsHops(UACEDatSubsystem* Dat, uint32 SeedCell, float WorldScale, int32 ExtraHops,
		TSet<int32>& VisibleIndoor, TSet<int32>* CollideIndoor, int32 MaxCells,
		bool bIncludeVisibleCells = true)
	{
		if (!Dat || ExtraHops <= 0)
		{
			return;
		}
		TSet<int32> Frontier = VisibleIndoor;
		Frontier.Add(static_cast<int32>(SeedCell));
		for (int32 Hop = 0; Hop < ExtraHops; ++Hop)
		{
			if (VisibleIndoor.Num() >= MaxCells)
			{
				break;
			}
			TArray<int32> Wave = Frontier.Array();
			TSet<int32> Next;
			for (int32 Id : Wave)
			{
				TSet<int32> Step;
				AddIndoorPvsFromDat(Dat, static_cast<uint32>(Id), WorldScale, Step, CollideIndoor,
					bIncludeVisibleCells);
				for (int32 N : Step)
				{
					if (VisibleIndoor.Contains(N))
					{
						continue;
					}
					VisibleIndoor.Add(N);
					Next.Add(N);
				}
			}
			if (Next.Num() == 0)
			{
				break;
			}
			Frontier = MoveTemp(Next);
		}
	}

	void AddOccupiedBuildingEnvCells(UACEDatSubsystem* Dat, uint32 CellId, float WorldScale,
		TSet<int32>& OutCells)
	{
		if (!Dat || !IsIndoorCellId(CellId))
		{
			return;
		}
		const uint32 Key = CellId & 0xFFFF0000u;
		FACEDatLandblockInfo Info;
		if (!Dat->LoadLandblockInfo(Key, Info) || Info.Buildings.Num() == 0)
		{
			return;
		}
		const int32 Bi = Dat->FindBuildingInfoIndexForIndoorCell(Key, CellId, WorldScale);
		auto AddBuilding = [&](const FACEDatLandblockBuilding& Building)
		{
			for (uint16 Id : Building.PortalCellIds)
			{
				if (Id >= 0x0100u && Id != 0xFFFFu)
				{
					OutCells.Add(static_cast<int32>(Key | Id));
				}
			}
			for (const FACEDatBuildingPortal& Portal : Building.Portals)
			{
				if (Portal.OtherCellId >= 0x0100u && Portal.OtherCellId != 0xFFFFu)
				{
					OutCells.Add(static_cast<int32>(Key | Portal.OtherCellId));
				}
				for (uint16 Stab : Portal.StabCells)
				{
					if (Stab >= 0x0100u)
					{
						OutCells.Add(static_cast<int32>(Key | Stab));
					}
				}
			}
		};
		if (Bi != INDEX_NONE && Info.Buildings.IsValidIndex(Bi))
		{
			AddBuilding(Info.Buildings[Bi]);
		}
		else if (Info.Buildings.Num() <= 6)
		{
			// Small housing landblock (cottages) — spawn every house interior so wing rooms exist.
			// Towns have dozens of buildings; those stay on PVS hops only.
			for (const FACEDatLandblockBuilding& Building : Info.Buildings)
			{
				AddBuilding(Building);
			}
		}
	}

	void AddIndoorPvsFromDat(UACEDatSubsystem* Dat, uint32 CellId, float WorldScale,
		TSet<int32>& VisibleIndoor, TSet<int32>* CollideIndoor, bool bIncludeVisibleCells)
	{
		if (!Dat || !IsIndoorCellId(CellId))
		{
			return;
		}
		auto AddFull = [&](uint32 FullId)
		{
			if (!IsIndoorCellId(FullId))
			{
				return;
			}
			VisibleIndoor.Add(static_cast<int32>(FullId));
			if (CollideIndoor)
			{
				CollideIndoor->Add(static_cast<int32>(FullId));
			}
		};

		if (const FACEBuiltEnvCellMesh* Built = Dat->FindEnvCellMesh(CellId, WorldScale))
		{
			if (bIncludeVisibleCells)
			{
				for (uint16 Vc : Built->VisibleCells)
				{
					if (Vc >= 0x0100u && Vc != 0xFFFFu)
					{
						AddFull(ResolveIndoorPortalCell(Dat, CellId, Vc, WorldScale));
					}
				}
			}
			for (const FACEDatCellPortal& Portal : Built->CellPortals)
			{
				if (!Portal.IsOutsidePortal())
				{
					AddFull(ResolveIndoorPortalCell(Dat, CellId, Portal.OtherCellId, WorldScale));
				}
			}
			return;
		}

		FACEDatEnvCell DatCell;
		if (!Dat->LoadEnvCell(CellId, DatCell))
		{
			Dat->RequestEnvCellMesh(CellId, WorldScale);
			return;
		}
		if (bIncludeVisibleCells)
		{
			for (uint16 Vc : DatCell.VisibleCells)
			{
				if (Vc >= 0x0100u && Vc != 0xFFFFu)
				{
					AddFull(ResolveIndoorPortalCell(Dat, CellId, Vc, WorldScale));
				}
			}
		}
		for (const FACEDatCellPortal& Portal : DatCell.CellPortals)
		{
			if (!Portal.IsOutsidePortal())
			{
				AddFull(ResolveIndoorPortalCell(Dat, CellId, Portal.OtherCellId, WorldScale));
			}
		}
	}

	/** Retail CLandBlock::grab_visible_cells → CBldPortal::add_to_stablist (StabCells).
	 *  OtherCellId is kept when it is missing from the stab list (empty portal connectors). */
	void AddLandblockGrabVisible(UACEDatSubsystem* Dat, uint32 LandblockKey, float WorldScale,
		TFunctionRef<void(uint32)> AddCell)
	{
		if (!Dat)
		{
			return;
		}
		FACEDatLandblockInfo Info;
		const uint32 Key = LandblockKey & 0xFFFF0000u;
		if (!Dat->LoadLandblockInfo(Key, Info) || Info.Buildings.Num() == 0)
		{
			return;
		}
		for (const FACEDatLandblockBuilding& Building : Info.Buildings)
		{
			for (const FACEDatBuildingPortal& Portal : Building.Portals)
			{
				if (Portal.OtherCellId >= 0x0100u && Portal.OtherCellId != 0xFFFFu)
				{
					const uint32 EntryId = Key | Portal.OtherCellId;
					AddCell(EntryId);
					Dat->RequestEnvCellMesh(EntryId, WorldScale);
				}
				for (uint16 Stab : Portal.StabCells)
				{
					if (Stab < 0x0100u || Stab == 0xFFFFu)
					{
						continue;
					}
					const uint32 StabId = Key | Stab;
					AddCell(StabId);
					Dat->RequestEnvCellMesh(StabId, WorldScale);
				}
			}
		}
	}

	/** Nearby shops only: StabList + entry VisibleCells so street look-in matches indoor PVS. */
	void AddLookInStabCells(UACEDatSubsystem* Dat, uint32 LandblockKey, const FVector& PlayerUe,
		float WorldScale, const TArray<ACEOutdoorPortalPlan::FAdmittedAperture>& Apertures,
		TFunctionRef<void(uint32)> AddCell)
	{
		if (!Dat)
		{
			return;
		}
		FACEDatLandblockInfo Info;
		const uint32 Key = LandblockKey & 0xFFFF0000u;
		if (!Dat->LoadLandblockInfo(Key, Info) || Info.Buildings.Num() == 0)
		{
			return;
		}
		TSet<uint32> AdmittedDests;
		for (const ACEOutdoorPortalPlan::FAdmittedAperture& A : Apertures)
		{
			if ((A.DestEnvCellId & 0xFFFF0000u) == Key)
			{
				AdmittedDests.Add(A.DestEnvCellId);
			}
		}
		const uint32 Lbx = (Key >> 24) & 0xFF;
		const uint32 Lby = (Key >> 16) & 0xFF;
		const FVector LbOrigin = FACEPosition::AceVectorToUnreal(
			FVector(Lbx * 192.f, Lby * 192.f, 0.f), WorldScale);
		const float NearDoorCmSq = FMath::Square(50.f * WorldScale);
		TArray<TPair<float, int32>> Ranked;
		Ranked.Reserve(Info.Buildings.Num());
		for (int32 Bi = 0; Bi < Info.Buildings.Num(); ++Bi)
		{
			const FACEDatLandblockBuilding& Building = Info.Buildings[Bi];
			const FVector BldWorld = LbOrigin + FACEPosition::AceVectorToUnreal(
				FVector(Building.Origin.X, Building.Origin.Y, Building.Origin.Z), WorldScale);
			const float DistSq = static_cast<float>(FVector::DistSquared(PlayerUe, BldWorld));
			bool bAdmit = false;
			for (const FACEDatBuildingPortal& Portal : Building.Portals)
			{
				if (Portal.OtherCellId >= 0x0100u && Portal.OtherCellId != 0xFFFFu
					&& AdmittedDests.Contains(Key | Portal.OtherCellId))
				{
					bAdmit = true;
					break;
				}
			}
			if (DistSq <= NearDoorCmSq || bAdmit)
			{
				Ranked.Emplace(DistSq, Bi);
			}
		}
		Ranked.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
		{
			return A.Key < B.Key;
		});
		int32 Extra = 0;
		constexpr int32 MaxExtra = 96;
		constexpr int32 MaxBuildings = 8;
		const int32 UseN = FMath::Min(MaxBuildings, Ranked.Num());
		auto AddPvs = [&](uint32 CellId)
		{
			if (Extra >= MaxExtra)
			{
				return;
			}
			AddCell(CellId);
			Dat->RequestEnvCellMesh(CellId, WorldScale);
			++Extra;
			TArray<uint16> Visible;
			if (const FACEBuiltEnvCellMesh* Mesh = Dat->FindEnvCellMesh(CellId, WorldScale))
			{
				Visible = Mesh->VisibleCells;
			}
			else
			{
				FACEDatEnvCell DatCell;
				if (Dat->LoadEnvCell(CellId, DatCell))
				{
					Visible = DatCell.VisibleCells;
				}
			}
			for (uint16 Vc : Visible)
			{
				if (Vc < 0x0100u || Vc == 0xFFFFu || Extra >= MaxExtra)
				{
					continue;
				}
				const uint32 Vid = Key | Vc;
				AddCell(Vid);
				Dat->RequestEnvCellMesh(Vid, WorldScale);
				++Extra;
			}
		};
		for (int32 Ri = 0; Ri < UseN; ++Ri)
		{
			const FACEDatLandblockBuilding& Building = Info.Buildings[Ranked[Ri].Value];
			for (const FACEDatBuildingPortal& Portal : Building.Portals)
			{
				if (Portal.OtherCellId < 0x0100u || Portal.OtherCellId == 0xFFFFu)
				{
					continue;
				}
				const uint32 EntryId = Key | Portal.OtherCellId;
				AddPvs(EntryId);
				for (uint16 Stab : Portal.StabCells)
				{
					if (Stab < 0x0100u || Extra >= MaxExtra)
					{
						continue;
					}
					const uint32 StabId = Key | Stab;
					AddCell(StabId);
					Dat->RequestEnvCellMesh(StabId, WorldScale);
					++Extra;
				}
			}
		}
	}
}

UACETerrainPresenterComponent::UACETerrainPresenterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	LandblockClass = AACELandblockActor::StaticClass();
	TerrainChunkClass = AACETerrainChunkActor::StaticClass();
	EnvCellClass = AACEEnvCellActor::StaticClass();
	LoadRadius = ACEStreamingBudget::TerrainRadius(5);
	UnloadRadius = ACEStreamingBudget::TerrainRadius(6);
	FullDetailRadius = 1; // get_block_orient Chebyshev ≤ 1 → side_cell_count 8.
	StagedLoadRadius = 0;
	StagedExpandAccum = 0.f;
	LandblocksPerTick = 4;
	EnvCellsPerTick = 8;
	EnvCellTimeBudgetMs = 4.f;
	TerrainChunkSize = 1;
	AmbientRandom.Initialize(0xACEA11);
}

bool UACETerrainPresenterComponent::IsPlayerCellCollisionReady() const
{
	if (!bHasKnownCell)
	{
		return false;
	}

	const int32 CenterLB = static_cast<int32>(LastKnownCellId & 0xFFFF0000u);
	if (!IsIndoorCell(LastKnownCellId))
	{
		if (TerrainChunkSize > 1)
		{
			for (const auto& Pair : SpawnedChunks)
			{
				if (Pair.Value && Pair.Value->ContainsLandblock(CenterLB) && Pair.Value->IsChunkTerrainReady())
				{
					if (AACELandblockActor* Child = Pair.Value->FindChildLandblock(CenterLB))
					{
						return Child->IsLandblockReady();
					}
					return true;
				}
			}
			return false;
		}
		const AACELandblockActor* Land = Spawned.FindRef(CenterLB);
		return Land && Land->IsLandblockReady();
	}

	const int32 CellId = static_cast<int32>(LastKnownCellId);
	if (FailedEnvCells.Contains(CellId))
	{
		return false;
	}
	const AACEEnvCellActor* Actor = SpawnedEnvCells.FindRef(CellId);
	return Actor && Actor->IsCollisionCooked();

}

bool UACETerrainPresenterComponent::IsPlayerCellVisualReady() const
{
	if (!bHasKnownCell || !IsIndoorCell(LastKnownCellId))
	{
		return true;
	}
	const AACEEnvCellActor* Actor = SpawnedEnvCells.FindRef(static_cast<int32>(LastKnownCellId));
	if (!Actor || !Actor->CellMesh)
	{
		return false;
	}
	return Actor->HasPendingStaticObjects() == false;
}

bool UACETerrainPresenterComponent::NeedsExteriorTerrain(uint32 CellId) const
{
	if (!IsIndoorCell(CellId)) return CellId != 0;
	const auto* World = GetWorld();
	auto* Dat = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
	FACEDatLandblockInfo Info;
	return Dat && Dat->LoadLandblockInfo(CellId & 0xFFFF0000u, Info) && !Info.Buildings.IsEmpty();
}

bool UACETerrainPresenterComponent::IsPlayerIndoorNeighborhoodReady() const
{
	if (!bHasKnownCell || !IsIndoorCell(LastKnownCellId))
	{
		return true;
	}
	if (!IsPlayerCellCollisionReady() || !IsPlayerCellVisualReady())
	{
		return false;
	}

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return false;
	}

	Dat->RequestEnvCellMesh(LastKnownCellId, WorldScale);
	const FACEBuiltEnvCellMesh* Current = Dat->FindEnvCellMesh(LastKnownCellId, WorldScale);
	if (!Current)
	{
		return false;
	}

	// Walls/ceilings of marketplace / dungeon rooms often live in neighbor EnvCells.
	// Require the current cell's portal graph + VisibleCells (capped) before portal exit.
	TArray<int32> Neighbors;
	Neighbors.Reserve(48);
	const uint32 LandblockKey = LastKnownCellId & 0xFFFF0000u;
	auto AddNeighbor = [&](uint16 ShortId)
	{
		if (ShortId == 0 || ShortId == 0xFFFFu || ShortId < 0x0100u)
		{
			return;
		}
		const int32 FullId = static_cast<int32>(LandblockKey | ShortId);
		if (FullId == static_cast<int32>(LastKnownCellId) || Neighbors.Contains(FullId))
		{
			return;
		}
		Neighbors.Add(FullId);
	};
	for (const FACEDatCellPortal& Portal : Current->CellPortals)
	{
		AddNeighbor(Portal.OtherCellId);
	}
	constexpr int32 MaxVisibleNeighbors = 32;
	for (uint16 Vc : Current->VisibleCells)
	{
		if (Neighbors.Num() >= MaxVisibleNeighbors)
		{
			break;
		}
		AddNeighbor(Vc);
	}

	int32 DrawableRequired = 0;
	int32 DrawableReady = 0;
	int32 StillUnknown = 0;
	for (const int32 Id : Neighbors)
	{
		const UACEDatSubsystem::EACEEnvCellMeshStatus St =
			Dat->RequestEnvCellMesh(static_cast<uint32>(Id), WorldScale);
		if (St == UACEDatSubsystem::EACEEnvCellMeshStatus::Pending
			|| St == UACEDatSubsystem::EACEEnvCellMeshStatus::NotReady)
		{
			return false;
		}
		if (St == UACEDatSubsystem::EACEEnvCellMeshStatus::Failed || FailedEnvCells.Contains(Id))
		{
			continue;
		}
		const FACEBuiltEnvCellMesh* NeighborMesh = Dat->FindEnvCellMesh(static_cast<uint32>(Id), WorldScale);
		if (!NeighborMesh)
		{
			++StillUnknown;
			continue;
		}
		if (NeighborMesh->bPortalConnector
			|| (NeighborMesh->IsEmpty() && NeighborMesh->StaticObjects.Num() == 0))
		{
			continue;
		}
		// Stab-only hubs have StaticObjects but no CellMesh sections — don't block ready.
		bool bHasDrawSections = false;
		for (const FACEBuiltMeshSection& Sec : NeighborMesh->Sections)
		{
			if (!Sec.IsEmpty())
			{
				bHasDrawSections = true;
				break;
			}
		}
		if (bHasDrawSections)
		{
			++DrawableRequired;
		}
		const AACEEnvCellActor* Actor = SpawnedEnvCells.FindRef(Id);
		if (!Actor)
		{
			return false;
		}
		if (HeldIndoorCollide.Contains(Id)
			&& (!Actor->IsCollisionCooked() || Actor->HasPendingStaticObjects()))
		{
			return false;
		}
		if (bHasDrawSections)
		{
			if (!Actor->CellMesh || Actor->CellMesh->GetNumSections() <= 0)
			{
				return false;
			}
			++DrawableReady;
		}
	}
	// Plaza spawn cell portals its walls/ceiling — never treat "all skipped/failed" as ready
	// (that exited portal into a gray void with only the floor cell).
	if (Neighbors.Num() > 0 && (StillUnknown > 0 || DrawableReady == 0))
	{
		return false;
	}
	return DrawableReady >= DrawableRequired;
}

bool UACETerrainPresenterComponent::IsCenterSceneryComplete() const
{
	// Portal / enter-world "center ready" = walkable collision + buildings on the
	// player landblock. Full RegionDesc flora drains after reveal (IsSceneryComplete).
	if (!bHasKnownCell || !IsPlayerCellCollisionReady())
	{
		return false;
	}
	if (IsIndoorCell(LastKnownCellId))
	{
		return IsPlayerIndoorNeighborhoodReady();
	}
	// Outdoor: IsPlayerCellCollisionReady already requires IsLandblockReady on center.
	return true;
}

bool UACETerrainPresenterComponent::IsLoadRadiusTerrainReadyForRadius(int32 Radius) const
{
	if (!bHasKnownCell)
	{
		return false;
	}
	if (bWcBakedTerrainMode)
	{
		return IsLoadRadiusSceneryCompleteForRadius(Radius);
	}
	const int32 CenterX = static_cast<int32>((LastKnownCellId >> 24) & 0xFF);
	const int32 CenterY = static_cast<int32>((LastKnownCellId >> 16) & 0xFF);
	const int32 R = FMath::Max(0, Radius);

	auto LandblockKey = [](int32 X, int32 Y) -> int32
	{
		return (X << 24) | (Y << 16);
	};

	if (TerrainChunkSize > 1)
	{
		const int32 ChunkSize = FMath::Clamp(TerrainChunkSize, 1, 16);
		auto AlignChunk = [ChunkSize](int32 Coord) -> int32
		{
			return (Coord / ChunkSize) * ChunkSize;
		};
		auto MakeChunkKey = [](int32 Ox, int32 Oy) -> int32
		{
			return (Ox << 24) | (Oy << 16);
		};
		TSet<int32> NeededChunks;
		for (int32 DX = -R; DX <= R; ++DX)
		{
			for (int32 DY = -R; DY <= R; ++DY)
			{
				const int32 X = CenterX + DX;
				const int32 Y = CenterY + DY;
				if (X < 0 || X > 255 || Y < 0 || Y > 255)
				{
					continue;
				}
				NeededChunks.Add(MakeChunkKey(AlignChunk(X), AlignChunk(Y)));
			}
		}
		if (NeededChunks.Num() == 0)
		{
			return false;
		}
		for (int32 CK : NeededChunks)
		{
			const AACETerrainChunkActor* Chunk = SpawnedChunks.FindRef(CK);
			if (!Chunk || !Chunk->IsChunkTerrainReady())
			{
				return false;
			}
		}
		return true;
	}

	for (int32 DX = -R; DX <= R; ++DX)
	{
		for (int32 DY = -R; DY <= R; ++DY)
		{
			const int32 X = CenterX + DX;
			const int32 Y = CenterY + DY;
			if (X < 0 || X > 255 || Y < 0 || Y > 255)
			{
				continue;
			}
			const int32 LB = LandblockKey(X, Y);
			// Failed neighbors must not count as ready — that caused portal exit into voids.
			if (FailedLandblocks.Contains(LB))
			{
				return false;
			}
			const AACELandblockActor* Land = Spawned.FindRef(LB);
			if (!Land || !Land->IsOutdoorTerrainMeshReady())
			{
				return false;
			}
		}
	}
	return true;
}

bool UACETerrainPresenterComponent::IsLoadRadiusBuildingsReadyForRadius(int32 Radius) const
{
	if (!bHasKnownCell)
	{
		return false;
	}
	if (bWcBakedTerrainMode)
	{
		return IsLoadRadiusSceneryCompleteForRadius(Radius);
	}
	const int32 CenterX = static_cast<int32>((LastKnownCellId >> 24) & 0xFF);
	const int32 CenterY = static_cast<int32>((LastKnownCellId >> 16) & 0xFF);
	const int32 R = FMath::Max(0, Radius);

	auto LandblockKey = [](int32 X, int32 Y) -> int32
	{
		return (X << 24) | (Y << 16);
	};

	if (TerrainChunkSize > 1)
	{
		for (int32 DX = -R; DX <= R; ++DX)
		{
			for (int32 DY = -R; DY <= R; ++DY)
			{
				const int32 X = CenterX + DX;
				const int32 Y = CenterY + DY;
				if (X < 0 || X > 255 || Y < 0 || Y > 255)
				{
					continue;
				}
				const int32 LB = LandblockKey(X, Y);
				if (FailedLandblocks.Contains(LB))
				{
					continue;
				}
				const AACETerrainChunkActor* Chunk = nullptr;
				for (const auto& Pair : SpawnedChunks)
				{
					if (Pair.Value && Pair.Value->ContainsLandblock(LB))
					{
						Chunk = Pair.Value;
						break;
					}
				}
				if (!Chunk || !Chunk->IsChunkTerrainReady())
				{
					return false;
				}
				if (const AACELandblockActor* Child = Chunk->FindChildLandblock(LB))
				{
					if (!Child->IsLandblockReady())
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	for (int32 DX = -R; DX <= R; ++DX)
	{
		for (int32 DY = -R; DY <= R; ++DY)
		{
			const int32 X = CenterX + DX;
			const int32 Y = CenterY + DY;
			if (X < 0 || X > 255 || Y < 0 || Y > 255)
			{
				continue;
			}
			const int32 LB = LandblockKey(X, Y);
			if (FailedLandblocks.Contains(LB))
			{
				continue;
			}
			const AACELandblockActor* Land = Spawned.FindRef(LB);
			if (!Land || !Land->IsLandblockReady())
			{
				return false;
			}
		}
	}
	return true;
}

bool UACETerrainPresenterComponent::IsLoadRadiusSceneryComplete() const
{
	if (!bLastSyncComplete)
	{
		return false;
	}
	if (TerrainChunkSize > 1)
	{
		if (SpawnedChunks.Num() == 0)
		{
			return false;
		}
		for (const auto& Pair : SpawnedChunks)
		{
			if (!Pair.Value || !Pair.Value->IsChunkSceneryComplete())
			{
				return false;
			}
		}
		return true;
	}
	if (Spawned.Num() == 0)
	{
		return false;
	}
	for (const auto& Pair : Spawned)
	{
		const AACELandblockActor* Land = Pair.Value;
		if (!Land || !Land->IsSceneryComplete())
		{
			return false;
		}
	}
	return true;
}

bool UACETerrainPresenterComponent::IsLoadRadiusSceneryCompleteForRadius(int32 Radius) const
{
	if (!bHasKnownCell)
	{
		return false;
	}
	if (TerrainChunkSize > 1)
	{
		return IsLoadRadiusSceneryComplete();
	}

	const int32 CenterX = static_cast<int32>((LastKnownCellId >> 24) & 0xFF);
	const int32 CenterY = static_cast<int32>((LastKnownCellId >> 16) & 0xFF);
	const int32 R = FMath::Max(0, Radius);
	auto LandblockKey = [](int32 X, int32 Y) -> int32
	{
		return (X << 24) | (Y << 16);
	};

	for (int32 DX = -R; DX <= R; ++DX)
	{
		for (int32 DY = -R; DY <= R; ++DY)
		{
			const int32 X = CenterX + DX;
			const int32 Y = CenterY + DY;
			if (X < 0 || X > 255 || Y < 0 || Y > 255)
			{
				continue;
			}
			const int32 LB = LandblockKey(X, Y);
			if (FailedLandblocks.Contains(LB))
			{
				continue;
			}
			const AACELandblockActor* Land = Spawned.FindRef(LB);
			if (!Land || !Land->IsSceneryComplete())
			{
				return false;
			}
		}
	}
	return true;
}

void UACETerrainPresenterComponent::BeginPlay()
{
	Super::BeginPlay();
	const bool bWcBakedMap = GetWorld() && GetWorld()->WorldComposition
		&& GetWorld()->WorldComposition->GetTilesList().Num() > 0;
	if (bWcBakedMap)
	{
		LoadRadius = 0;
		UnloadRadius = 0;
		SetComponentTickEnabled(false);
		EnsureTerrainStreamingSubscriptions();
		return;
	}

	bool bDeferStreaming = false;
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				if (!Dat->IsWorldStreamingAllowed())
				{
					bDeferStreaming = true;
				}
			}
		}
	}
	if (bDeferStreaming)
	{
		LoadRadius = 0;
		UnloadRadius = 0;
		SetComponentTickEnabled(false);
	}

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			Client = GI->GetSubsystem<UACEClientSubsystem>();
			if (Client)
			{
				Client->OnEnteredWorld.AddDynamic(this, &UACETerrainPresenterComponent::HandleEnteredWorld);
				Client->OnPositionUpdate.AddDynamic(this, &UACETerrainPresenterComponent::HandlePositionUpdate);
				Client->OnSessionStateChanged.AddDynamic(this, &UACETerrainPresenterComponent::HandleLogout);
			}
		}
		// Stream landblocks off the packet path. Enter-world ObjectCreate floods used to call
		// SyncAroundCell (and EnsureLoaded) mid-datagram — that froze the game thread before
		// any frame could render. 0.1s drain is plenty for progressive load/unload.
		World->GetTimerManager().SetTimer(RetryTimerHandle, this, &UACETerrainPresenterComponent::RetrySyncIfNeeded, 0.1f, true);
	}
	EnsureAmbientScripts();
}

void UACETerrainPresenterComponent::EnsureTerrainStreamingSubscriptions()
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (!Client)
			{
				Client = GI->GetSubsystem<UACEClientSubsystem>();
			}
			if (Client)
			{
				Client->OnEnteredWorld.AddDynamic(this, &UACETerrainPresenterComponent::HandleEnteredWorld);
				Client->OnPositionUpdate.AddDynamic(this, &UACETerrainPresenterComponent::HandlePositionUpdate);
				Client->OnSessionStateChanged.AddDynamic(this, &UACETerrainPresenterComponent::HandleLogout);
			}
		}
		if (!World->GetTimerManager().IsTimerActive(RetryTimerHandle))
		{
			World->GetTimerManager().SetTimer(RetryTimerHandle, this, &UACETerrainPresenterComponent::RetrySyncIfNeeded, 0.1f, true);
		}
	}
}

void UACETerrainPresenterComponent::RestoreProceduralStreamingAfterLogin(int32 InLoadRadius, int32 InUnloadRadius)
{
	if (!GetWorld())
	{
		return;
	}
	// Baked WC tiles use RestoreWcBakedCollisionStreaming. Empty/null WC and World
	// Partition OpenWorld still need DAT LScape — the old !WorldComposition early-out
	// left LoadRadius=0 and tick disabled, so only the center landblock ever spawned.
	if (GetWorld()->WorldComposition && GetWorld()->WorldComposition->GetTilesList().Num() > 0)
	{
		return;
	}
	bWcBakedTerrainMode = false;
	LoadRadius = ACEStreamingBudget::TerrainRadius(InLoadRadius);
	UnloadRadius = FMath::Max(LoadRadius, ACEStreamingBudget::TerrainRadius(InUnloadRadius));
	if (StagedLoadRadius > LoadRadius)
	{
		StagedLoadRadius = LoadRadius;
	}
	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Warning, TEXT("ACE: TerrainPresenter procedural streaming restored (LoadRadius=%d staged=%d)"),
		LoadRadius, StagedLoadRadius);
}

void UACETerrainPresenterComponent::RestoreWcBakedCollisionStreaming(int32 InLoadRadius, int32 InUnloadRadius)
{
	UWorld* World = GetWorld();
	if (!World || !World->WorldComposition || World->WorldComposition->GetTilesList().Num() == 0)
	{
		return;
	}
	if (LoadRadius > 0)
	{
		return;
	}
	bWcBakedTerrainMode = true;
	LoadRadius = FMath::Max(2, InLoadRadius);
	UnloadRadius = FMath::Max(LoadRadius, InUnloadRadius);
	StagedLoadRadius = LoadRadius;
	SetComponentTickEnabled(true);
	EnsureTerrainStreamingSubscriptions();
	UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter WC collision shells restored (LoadRadius=%d)"), LoadRadius);
}

void UACETerrainPresenterComponent::KickLandblockLoginBurst()
{
	bLastSyncComplete = false;
	bLastEnvSyncComplete = false;
	LastEnvSyncKey = 0;
	LastEnvDirtyKey = 0;
	LandblockLoginBurstTicks = 40;
	StagedExpandAccum = 0.f;
	bHoldStagedLoadRadius = true;
	StagedLoadRadius = FMath::Max(StagedLoadRadius, LoadRadius);
	if (Client)
	{
		const FACEPosition Pos = Client->GetPlayerPosition();
		if (Pos.IsValid())
		{
			CommitOccupancyCellId(static_cast<uint32>(Pos.CellId), /*bServerAuthoritative*/ true,
				/*bForce*/ true);
		}
	}
	// This is also called from PlayerCreate's network dispatch. Schedule work,
	// never build terrain/rooms synchronously before the loading view can render.
	LastEnvSyncFrame = MAX_uint64;
}

bool UACETerrainPresenterComponent::ShouldHoldStagedLoadRadius() const
{
	if (bHoldStagedLoadRadius || LandblockLoginBurstTicks > 0)
	{
		return true;
	}
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (const UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				return Dat->IsInPortalSpace();
			}
		}
	}
	return false;
}

bool UACETerrainPresenterComponent::IsLandblockKept(uint32 LandblockId) const
{
	const int32 Key = static_cast<int32>(LandblockId & 0xFFFF0000u);
    // A remote dungeon is resident independently of the last outdoor landscape.
    if (bHasKnownCell && Key == static_cast<int32>(LastKnownCellId & 0xFFFF0000u)) return true;
	if (KeptLandblocks.Num() > 0)
	{
		return KeptLandblocks.Contains(Key);
	}
	if (!bHasKnownCell)
	{
		return false;
	}
	if (Key == static_cast<int32>(LastKnownCellId & 0xFFFF0000u))
	{
		return true;
	}
	return LastOutdoorCellId != 0 && Key == static_cast<int32>(LastOutdoorCellId & 0xFFFF0000u);
}

bool UACETerrainPresenterComponent::IsLandblockSpawned(uint32 LandblockId) const
{
	const int32 Key = static_cast<int32>(LandblockId & 0xFFFF0000u);
	if (Spawned.Contains(Key))
	{
		return true;
	}
	for (const auto& Pair : SpawnedChunks)
	{
		if (Pair.Value && Pair.Key == Key)
		{
			return true;
		}
	}
	return false;
}

namespace
{
	void SyncWorldEntitiesToKeepRing(AActor* Owner, const TSet<int32>& KeepLbs)
	{
		if (!Owner)
		{
			return;
		}
		if (UACEWorldPresenterComponent* WorldPres = Owner->FindComponentByClass<UACEWorldPresenterComponent>())
		{
			WorldPres->SyncEntitiesToKeepRing(KeepLbs);
		}
	}
}

void UACETerrainPresenterComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	ACE_PROFILE_SCOPE(Terrain);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateSkyWeatherState();
	TickAmbientSounds(DeltaTime);
	TickDegradeController(DeltaTime);
	ShadowRefreshAccum += DeltaTime;
	if (ShadowRefreshAccum >= 0.25f)
	{
		ShadowRefreshAccum = 0.f;
		FVector CamWorld = FVector::ZeroVector;
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
				{
					CamWorld = CamMgr->GetCameraLocation();
				}
				else if (APawn* Pawn = PC->GetPawn())
				{
					CamWorld = Pawn->GetActorLocation();
				}
			}
		}
		RefreshOutdoorShadowCasters(CamWorld);
		RefreshEntityShadowCasters(CamWorld);
	}

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				UpdateCameraVisibility();
				if (Dat->IsInPortalSpace() && Dat->IsWorldStreamingAllowed()
					&& !Dat->IsLightweightStreaming() && bHasKnownCell && !bLastSyncComplete)
					SyncAroundCell(LastKnownCellId);
			}
		}
	}
}

void UACETerrainPresenterComponent::EnsureAmbientScripts()
{
	AActor* Owner = GetOwner();
	if (!Owner || AmbientScripts)
	{
		return;
	}
	AmbientScripts = NewObject<UACEScriptComponent>(Owner, TEXT("ACEAmbientScripts"));
	if (!AmbientScripts)
	{
		return;
	}
	Owner->AddInstanceComponent(AmbientScripts);
	AmbientScripts->RegisterComponent();
	// Ambient uses PlayAmbientSoundFromTable (world positions) — not weather 2D sheets.
	AmbientScripts->InitializeForEnvironment(WorldScale);
}

void UACETerrainPresenterComponent::RebuildAmbientSchedule(uint32 CellId)
{
	AmbientSlots.Reset();
	AmbientStbKey = 0;
	// Cell 0 = pre-enter-world / unknown; never build landblock-0 STB (login leak).
	if (CellId == 0 || IsIndoorCell(CellId))
	{
		return;
	}
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return;
	}
	const FACEDatAmbientSTBDesc* Stb = nullptr;
	if (!Dat->TryResolveAmbientSTBForCell(CellId, Stb) || !Stb)
	{
		return;
	}
	AmbientStbKey = (Stb->SoundTableId << 8) ^ static_cast<uint32>(Stb->Sounds.Num());
	const double Now = FPlatformTime::Seconds();
	AmbientSlots.Reserve(Stb->Sounds.Num());
	for (const FACEDatAmbientSoundDesc& Amb : Stb->Sounds)
	{
		FAmbientSlot Slot;
		Slot.SoundTableId = Stb->SoundTableId;
		Slot.SoundType = Amb.SoundType;
		Slot.Volume = Amb.Volume;
		Slot.BaseChance = Amb.BaseChance;
		Slot.MinRate = FMath::Max(2.f, Amb.MinRate);
		Slot.MaxRate = FMath::Max(Slot.MinRate, Amb.MaxRate);
		Slot.bContinuous = Amb.IsContinuous();
		Slot.bContinuousPlaying = false;
		Slot.NextPlaySeconds = Now + AmbientRandom.FRandRange(0.f, Slot.bContinuous ? 0.f : Slot.MaxRate);
		AmbientSlots.Add(Slot);
	}
	UE_LOG(LogTemp, Log, TEXT("ACE Ambient: cell=0x%08X table=0x%08X slots=%d"),
		CellId, Stb->SoundTableId, AmbientSlots.Num());
}

void UACETerrainPresenterComponent::TickAmbientSounds(float DeltaTime)
{
	(void)DeltaTime;
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	const bool bPortalSpace = Dat && Dat->IsInPortalSpace();
	// Login / unknown cell / portal / indoor: silence. Do not schedule cell=0 STB.
	const bool bMuteAmbient = !bHasKnownCell
		|| LastKnownCellId == 0
		|| bPortalSpace
		|| IsIndoorCell(LastKnownCellId);
	if (bMuteAmbient)
	{
		if (AmbientSlots.Num() > 0 || (AmbientScripts && AmbientScripts->IsComponentTickEnabled()))
		{
			AmbientSlots.Reset();
			AmbientStbKey = 0;
			if (AmbientScripts)
			{
				AmbientScripts->StopAllSounds();
				AmbientScripts->SetComponentTickEnabled(false);
			}
		}
		return;
	}
	EnsureAmbientScripts();
	if (!AmbientScripts)
	{
		return;
	}
	if (!AmbientScripts->IsComponentTickEnabled())
	{
		AmbientScripts->SetComponentTickEnabled(true);
	}

	const FACEDatAmbientSTBDesc* Stb = nullptr;
	uint32 NewKey = AmbientStbKey;
	if (Dat && Dat->TryResolveAmbientSTBForCell(LastKnownCellId, Stb) && Stb)
	{
		NewKey = (Stb->SoundTableId << 8) ^ static_cast<uint32>(Stb->Sounds.Num());
	}
	if (NewKey != AmbientStbKey || AmbientSlots.Num() == 0)
	{
		if (AmbientScripts)
		{
			AmbientScripts->StopAllSounds(.75f);
		}
		RebuildAmbientSchedule(LastKnownCellId);
	}

	const double Now = FPlatformTime::Seconds();
	FVector PlayerUe = FVector::ZeroVector;
	bool bHavePlayer = false;
	if (Client)
	{
		const FACEPosition Pos = Client->GetPlayerPosition();
		if (Pos.IsValid())
		{
			PlayerUe = Pos.ToUnrealLocation(WorldScale);
			bHavePlayer = true;
		}
	}
	else if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* P = PC->GetPawn())
			{
				PlayerUe = P->GetActorLocation();
				bHavePlayer = true;
			}
		}
	}
	if (!bHavePlayer)
	{
		return;
	}

	for (FAmbientSlot& Slot : AmbientSlots)
	{
		if (Slot.bContinuous)
		{
			// Continuous beds are looping ambient — start once near the player, never stack.
			if (Slot.bContinuousPlaying)
			{
				continue;
			}
			const float DistAc = AmbientRandom.FRandRange(2.f, 10.f);
			const float Yaw = AmbientRandom.FRandRange(0.f, 2.f * PI);
			const FVector PlayAt = PlayerUe + FVector(
				-FMath::Sin(Yaw) * DistAc * WorldScale,
				FMath::Cos(Yaw) * DistAc * WorldScale,
				AmbientRandom.FRandRange(-1.f, 3.f) * WorldScale);
			AmbientScripts->PlayAmbientSoundFromTable(
				static_cast<int32>(Slot.SoundTableId),
				static_cast<int32>(Slot.SoundType),
				Slot.Volume,
				PlayAt,
				/*bLoop*/ true);
			Slot.bContinuousPlaying = true;
			continue;
		}

		if (Now < Slot.NextPlaySeconds)
		{
			continue;
		}
		const float Interval = AmbientRandom.FRandRange(Slot.MinRate, Slot.MaxRate);
		Slot.NextPlaySeconds = Now + Interval;
		if (Slot.BaseChance < 1.f && AmbientRandom.FRand() > Slot.BaseChance)
		{
			continue;
		}
		// Intermittent outdoor cues scatter around the avatar (distance + stereo pan).
		const float DistAc = AmbientRandom.FRandRange(8.f, 36.f);
		const float Yaw = AmbientRandom.FRandRange(0.f, 2.f * PI);
		const FVector PlayAt = PlayerUe + FVector(
			-FMath::Sin(Yaw) * DistAc * WorldScale,
			FMath::Cos(Yaw) * DistAc * WorldScale,
			AmbientRandom.FRandRange(-1.f, 3.f) * WorldScale);
		AmbientScripts->PlayAmbientSoundFromTable(
			static_cast<int32>(Slot.SoundTableId),
			static_cast<int32>(Slot.SoundType),
			Slot.Volume,
			PlayAt,
			/*bLoop*/ false);
	}
}

void UACETerrainPresenterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryTimerHandle);
	}
	ClearLandblocks();
	ClearEnvCells();
	AmbientSlots.Reset();
	AmbientScripts = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UACETerrainPresenterComponent::ClearLandblocks()
{
	for (auto& Pair : Spawned)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	Spawned.Reset();
	for (auto& Pair : SpawnedChunks)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	SpawnedChunks.Reset();
	FailedLandblocks.Reset();
	KeptLandblocks.Reset();
	LastCenterLB = -1;
	bLastSyncComplete = false;
	// Do not zero StagedLoadRadius here. Mesh-format reloads call ClearLandblocks while
	// the player is still in portal space; dropping staged radius to 0 froze the 11×11
	// ring and cell-collision never became ready. ResetStreamingForTransition owns that.
	StagedExpandAccum = 0.f;
	PendingStreamCenterKey = -1;
	StreamCenterHoldAccum = 0.f;
}

void UACETerrainPresenterComponent::ResetStreamingForTransition()
{
	// Destroy actors so the portal tunnel isn't fighting 100+ landblocks ticking scenery.
	// Keep destination DAT meshes and in-flight builds for fast nearby/relog portals,
	// but release departed areas before the next scene starts allocating resources.
	ClearLandblocks();
	ClearEnvCells();
	LastCenterLB = -1;
	bLastSyncComplete = false;
	LastEnvSyncKey = 0;
	LastEnvDirtyKey = 0;
	LastEnvLbActorCount = -1;
	bLastEnvSyncComplete = false;
	FailedEnvCells.Reset();
	FailedLandblocks.Reset();
	LastOutdoorApertures.Reset();
	ClearOutdoorPortalDepthApertures();
	StagedLoadRadius = 0;
	StagedExpandAccum = 0.f;
	LandblockLoginBurstTicks = 0;
	bHoldStagedLoadRadius = false;
	PendingStreamCenterKey = -1;
	StreamCenterHoldAccum = 0.f;
	{
		KeptLandblocks.Reset();
		SyncWorldEntitiesToKeepRing(GetOwner(), KeptLandblocks);
	}
	if (Client && GetWorld() && GetWorld()->GetGameInstance())
	{
		if (auto* Dat = GetWorld()->GetGameInstance()->GetSubsystem<UACEDatSubsystem>())
		{
			const FACEPosition Destination = Client->GetPlayerPosition();
			if (Destination.IsValid())
			{
				TSet<int32> KeepMeshes;
				const int32 X = (Destination.CellId >> 24) & 255, Y = (Destination.CellId >> 16) & 255;
				const int32 Radius = NeedsExteriorTerrain(Destination.CellId) ? FMath::Max(LoadRadius, UnloadRadius) : 0;
				for (int32 DX = -Radius; DX <= Radius; ++DX)
					for (int32 DY = -Radius; DY <= Radius; ++DY)
						if (X+DX >= 0 && X+DX <= 255 && Y+DY >= 0 && Y+DY <= 255)
							KeepMeshes.Add(static_cast<int32>((uint32(X+DX) << 24) | (uint32(Y+DY) << 16)));
				Dat->RetireWorldMeshesOutside(KeepMeshes, WorldScale);
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter transition actors cleared; destination mesh caches retained"));
}

void UACETerrainPresenterComponent::HandleLogout(EACESessionState NewState)
{
	if (NewState == EACESessionState::Disconnected
		|| NewState == EACESessionState::Failed
		|| NewState == EACESessionState::CharacterSelect)
	{
		TrackedPlayerGuid = 0;
		bHasKnownCell = false;
		ClearLandblocks();
		ClearEnvCells();
	}
}

void UACETerrainPresenterComponent::HandleEnteredWorld(int32 PlayerGuid, const FACEPosition& SpawnPosition)
{
	TrackedPlayerGuid = PlayerGuid;
	if (!SpawnPosition.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE: TerrainPresenter EnteredWorld — invalid spawn position, waiting for UpdatePosition"));
		return;
	}

	LastKnownCellId = static_cast<uint32>(SpawnPosition.CellId);
	bHasKnownCell = true;
	if (!IsIndoorCell(LastKnownCellId))
	{
		LastOutdoorCellId = LastKnownCellId;
	}
	// Force the next RetrySyncIfNeeded tick to (re)stream — do NOT sync here. This callback
	// runs inside the same PollSockets drain as the ObjectCreate flood.
	LastCenterLB = -1;
	bLastSyncComplete = false;
	LastEnvSyncKey = 0;
	LastEnvDirtyKey = 0;
	LastEnvLbActorCount = -1;
	bLastEnvSyncComplete = false;
	FailedEnvCells.Reset();
	if (LoadRadius == 0)
	{
		if (UWorld* World = GetWorld())
		{
			const bool bWcMap = World->WorldComposition
				&& World->WorldComposition->GetTilesList().Num() > 0;
			if (bWcMap)
			{
				RestoreWcBakedCollisionStreaming(1, 1);
			}
			else
			{
				RestoreProceduralStreamingAfterLogin(5, 6);
			}
		}
	}
	PendingStreamCenterKey = -1;
	StreamCenterHoldAccum = 0.f;
	KickLandblockLoginBurst();
	if (UWorld* World = GetWorld())
	{
		if (World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0)
		{
			FACEPosition Pos = SpawnPosition;
			if (Client)
			{
				const FACEPosition ClientPos = Client->GetPlayerPosition();
				if (ClientPos.IsValid())
				{
					Pos = ClientPos;
				}
			}
			if (Pos.IsValid())
			{
				ACEWcTerrainRuntime::RequestTerrainAround(
					World, Pos.ToUnrealLocation(WorldScale), /*RingTiles*/ 3);
			}
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("ACE: TerrainPresenter EnteredWorld guid=0x%08X cell=0x%08X — landblock stream scheduled"),
		PlayerGuid, LastKnownCellId);
	UpdateBuildingVisibility();
}

void UACETerrainPresenterComponent::HandlePositionUpdate(int32 ObjectGuid, const FACEPosition& Position)
{
	if (TrackedPlayerGuid != 0 && ObjectGuid != TrackedPlayerGuid)
	{
		return;
	}
	if (!Position.IsValid())
	{
		return;
	}
	if (TrackedPlayerGuid == 0)
	{
		TrackedPlayerGuid = ObjectGuid;
	}

	const uint32 PrevOcc = LastKnownCellId;
	const bool bWasIndoor = IsIndoorCell(LastKnownCellId);
	FACEPosition Presentation = Position;
	bool bLocal = false;
	if (UWorld* World = GetWorld())
		if (const auto* PC = Cast<AACEPlayerController>(World->GetFirstPlayerController()))
			bLocal = PC->TryGetLocallyPredictedPosition(Presentation);
	CommitOccupancyCellId(static_cast<uint32>(Presentation.CellId), /*bServerAuthoritative*/ !bLocal);
	const uint32 NewCell = LastKnownCellId;
	const int32 NewCenterX = static_cast<int32>((NewCell >> 24) & 0xFF);
	const int32 NewCenterY = static_cast<int32>((NewCell >> 16) & 0xFF);
	const int32 NewCenterKey = (NewCenterX << 8) | NewCenterY;

	const bool bNowIndoor = IsIndoorCell(NewCell);
	const bool bCenterChanged = (NewCenterKey != LastCenterLB);
	const bool bCellChanged = (NewCell != PrevOcc);

	if (bCenterChanged)
	{
		if (LastCenterLB >= 0)
		{
			const int32 OldX = (LastCenterLB >> 8) & 0xFF;
			const int32 OldY = LastCenterLB & 0xFF;
			const int32 Jump = FMath::Max(FMath::Abs(NewCenterX - OldX), FMath::Abs(NewCenterY - OldY));
			if (Jump >= 2 && !ShouldHoldStagedLoadRadius())
			{
				bLastSyncComplete = false;
				bLastEnvSyncComplete = false;
				if (Jump >= LoadRadius)
				{
					StagedLoadRadius = 1;
					StagedExpandAccum = 0.f;
				}
			}
		}
		else
		{
			bLastSyncComplete = false;
			bLastEnvSyncComplete = false;
		}
	}
	if (bCellChanged)
	{
		// Indoor PVS is per CellId. Outdoor StabList is per landblock — clearing complete
		// on every 8×8 landcell flicker re-ran SyncEnvCells every tick (~5 FPS).
		if (bNowIndoor || bWasIndoor)
		{
			bLastEnvSyncComplete = false;
			UpdateBuildingVisibility();
		}
	}
}

void UACETerrainPresenterComponent::RetrySyncIfNeeded()
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				if (!Dat->IsWorldStreamingAllowed())
				{
					return;
				}
			}
		}
	}

	if (!bHasKnownCell && !(Client && Client->GetPlayerPosition().IsValid()))
	{
		return;
	}

	// Prefer the session's reported pose for landblock ring center. During portal space,
	// never let predicted/pawn CellId walk the ring toward (0,0) while the pawn is hidden.
	if (Client)
	{
		const FACEPosition Pos = GetPresentationPosition();
		if (Pos.IsValid())
		{
			uint32 NewCell = static_cast<uint32>(Pos.CellId);
			bool bInPortal = false;
			if (UWorld* World = GetWorld())
			{
				if (UGameInstance* GI = World->GetGameInstance())
				{
					if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
					{
						bInPortal = Dat->IsInPortalSpace();
					}
				}
			}
			if (bWasInPortalSpace && !bInPortal)
			{
				KickLandblockLoginBurst();
			}
			bWasInPortalSpace = bInPortal;
			if (!bInPortal)
			{
				if (UWorld* World = GetWorld())
				{
					if (APlayerController* PC = World->GetFirstPlayerController())
					{
						if (const AACEPlayerController* AcePC = Cast<AACEPlayerController>(PC))
						{
							const int32 Eff = AcePC->GetEffectiveCellId();
							if (Eff != 0)
							{
								const uint32 EffCell = static_cast<uint32>(Eff);
								const bool bSessionIndoor = (NewCell & 0xFFFFu) >= 0x0100u;
								const bool bEffIndoor = (EffCell & 0xFFFFu) >= 0x0100u;
								// Marketplace plaza XY overlaps outdoor landcells. Following an
								// outdoor prediction while the server CellId is indoor wiped the
								// EnvCell PVS (walls/roof) every tick.
								if (bSessionIndoor && !bEffIndoor)
								{
									// Keep session indoor cell for streaming.
								}
								else if (!bSessionIndoor && bEffIndoor)
								{
									// Keep session outdoor. Following a false indoor prediction
									// (arches, jump-into-shop, camera in a doorway) dumped LScape,
									// culled weenies/scenery, and hid every other interior.
								}
								else if (!bSessionIndoor)
								{
									// Outdoor: the ring must follow the pawn, not a stale
									// server CellId (that left the 3×3 stuck on Yaraq while
									// walking the water). Adjacent flicker is debounced in
									// SyncAroundCell — do not require SessLb == EffLb.
									NewCell = EffCell;
								}
								else
								{
									const uint32 SessLb = NewCell & 0xFFFF0000u;
									const uint32 EffLb = EffCell & 0xFFFF0000u;
									if (SessLb == EffLb)
									{
										NewCell = EffCell;
									}
								}
							}
						}
					}
				}
			}
			const int32 NewCenterX = static_cast<int32>((NewCell >> 24) & 0xFF);
			const int32 NewCenterY = static_cast<int32>((NewCell >> 16) & 0xFF);
			const int32 NewCenterKey = (NewCenterX << 8) | NewCenterY;
			if (NewCenterKey != LastCenterLB)
			{
				const int32 OldX = (LastCenterLB >> 8) & 0xFF;
				const int32 OldY = LastCenterLB & 0xFF;
				const int32 Jump = LastCenterLB >= 0
					? FMath::Max(FMath::Abs(NewCenterX - OldX), FMath::Abs(NewCenterY - OldY))
					: 2;
				// Adjacent hold must not clear complete — that rebuilt the 3×3 + hiding
				// every shop shell every RetrySync tick while walking Yaraq.
				if (Jump >= 2)
				{
					bLastSyncComplete = false;
					bLastEnvSyncComplete = false;
				}
			}
			const uint32 PrevOcc = LastKnownCellId;
			const bool bWasIndoor = IsIndoorCell(LastKnownCellId);
			const uint32 SessionCell = static_cast<uint32>(Pos.CellId);
			CommitOccupancyCellId(NewCell, NewCell == SessionCell && IsIndoorCell(NewCell));
			const bool bNowIndoor = IsIndoorCell(LastKnownCellId);
			const bool bCellChanged = (LastKnownCellId != PrevOcc);
			if (bCellChanged && (bNowIndoor || bWasIndoor))
			{
				UpdateBuildingVisibility();
			}
		}
	}

	if (!bHasKnownCell)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				bool bClearEnvCells = false;
				if (Dat->ConsumeLandMeshReloadRequest(&bClearEnvCells))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("ACE: TerrainPresenter reloading landblocks%s (mesh format change)"),
						bClearEnvCells ? TEXT("+envcells") : TEXT(" only"));
					ClearLandblocks();
					if (bClearEnvCells)
					{
						ClearEnvCells();
					}
					KickLandblockLoginBurst();
				}
			}
		}
	}

	const int32 CenterX = static_cast<int32>((LastKnownCellId >> 24) & 0xFF);
	const int32 CenterY = static_cast<int32>((LastKnownCellId >> 16) & 0xFF);
	const int32 CenterKey = (CenterX << 8) | CenterY;
	const bool bCenterChanged = (CenterKey != LastCenterLB);

	if (bCenterChanged || !bLastSyncComplete)
	{
		SyncAroundCell(LastKnownCellId);
		if (UWorld* World = GetWorld())
		{
			if (World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0 && Client)
			{
				const FACEPosition Pos = Client->GetPlayerPosition();
				if (Pos.IsValid())
				{
					ACEWcTerrainRuntime::RequestTerrainAround(
						World, Pos.ToUnrealLocation(WorldScale), /*RingTiles*/ 4);
				}
			}
		}
	}
	int32 MaxStaged = LoadRadius;
	if (StagedLoadRadius < MaxStaged
		&& !IsIndoorCell(LastKnownCellId)
		&& IsLoadRadiusTerrainReadyForRadius(StagedLoadRadius))
	{
		const float ExpandNeed = 0.1f;
		StagedExpandAccum += 0.1f;
		if (StagedExpandAccum >= ExpandNeed)
		{
			StagedExpandAccum = 0.f;
			++StagedLoadRadius;
			bLastSyncComplete = false;
		}
	}
	else if (StagedLoadRadius < MaxStaged)
	{
		StagedExpandAccum = 0.f;
	}
	if (LandblockLoginBurstTicks > 0)
	{
		--LandblockLoginBurstTicks;
		if (LandblockLoginBurstTicks == 0)
		{
			bHoldStagedLoadRadius = false;
		}
	}

	if (bEnableEnvCells && LastEnvSyncFrame != GFrameCounter)
	{
		const uint32 PrevStream = EnvStreamCellId;
		EnvStreamCellId = LastKnownCellId;
		IndoorStreamHoldUntil = 0.0;
		if (EnvStreamCellId != PrevStream && IsIndoorCell(LastKnownCellId) != IsIndoorCell(PrevStream))
		{
			UpdateBuildingVisibility();
		}
		const bool bIndoorNow = IsIndoorCell(LastKnownCellId);
		const uint32 DirtyKey = EnvStreamCellId;
		LastEnvSyncFrame = GFrameCounter;
		// Indoor: resync when occupancy CellId changes. Outdoor: CollectNeeded grows
		// with nearby/admitted StabLists — LastEnvSyncKey early-outs when unchanged.
		if (!bLastEnvSyncComplete || DirtyKey != LastEnvDirtyKey || !bIndoorNow)
		{
			SyncEnvCells();
			if (bLastEnvSyncComplete)
			{
				LastEnvDirtyKey = DirtyKey;
			}
		}
		if (!bIndoorNow)
		{
			UpdateOutdoorEnvCollision();
			UpdateOutdoorEnvCellDraw();
		}
	}
}

int32 UACETerrainPresenterComponent::LandblockChebyshev(int32 LandblockKey, int32 CenterX, int32 CenterY)
{
	const int32 X = (LandblockKey >> 24) & 0xFF;
	const int32 Y = (LandblockKey >> 16) & 0xFF;
	return FMath::Max(FMath::Abs(X - CenterX), FMath::Abs(Y - CenterY));
}

void UACETerrainPresenterComponent::ApplyLandblockDetailLevels(int32 CenterX, int32 CenterY)
{
	auto ApplyActor = [CenterX, CenterY](int32 LB, AACELandblockActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		const int32 DX = ((LB >> 24) & 0xFF) - CenterX;
		const int32 DY = ((LB >> 16) & 0xFF) - CenterY;
		int32 PolySize = 1;
		int32 TransDir = 0;
		FACELandblockMeshBuilder::GetBlockOrient(DX, DY, PolySize, TransDir);
		Actor->SetTerrainLod(PolySize, TransDir);
		// Keep scenery one ring ahead of detailed terrain. Otherwise crossing a
		// landblock boundary creates an entire visible town/forest in one frame.
		Actor->SetFullResDetail(FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) <= 2);
	};
	for (const auto& Pair : Spawned)
	{
		ApplyActor(Pair.Key, Pair.Value);
	}
	for (const auto& ChunkPair : SpawnedChunks)
	{
		if (!ChunkPair.Value)
		{
			continue;
		}
		for (const auto& Child : ChunkPair.Value->GetChildLandblocks())
		{
			ApplyActor(Child.Key, Child.Value);
		}
	}
}

void UACETerrainPresenterComponent::SyncAroundCell(uint32 CellId)
{
	UWorld* World = GetWorld();
	if (!World || (!LandblockClass && TerrainChunkSize <= 1))
	{
		return;
	}
	const auto* StreamingDat = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
	const bool bPortalStreaming = StreamingDat && StreamingDat->IsInPortalSpace();
	if (bPortalStreaming)
	{
		// Tick and the regular 10Hz retry timer share a single install budget.
		if (LastPortalTerrainSyncFrame == GFrameCounter) return;
		LastPortalTerrainSyncFrame = GFrameCounter;
	}
	if (TerrainChunkSize > 1 && !TerrainChunkClass)
	{
		TerrainChunkClass = AACETerrainChunkActor::StaticClass();
	}

	// A dungeon has its own landblock coordinate frame and no outdoor shell.
	// Keeping the previous town here rendered its scenery, queued its entities,
	// and excluded the destination's actors from the keep ring.
	if (IsIndoorCell(CellId))
	{
		UACEDatSubsystem* Dat = World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
		FACEDatLandblockInfo Info;
		if (Dat && Dat->LoadLandblockInfo(CellId & 0xFFFF0000u, Info) && Info.Buildings.IsEmpty())
		{
			const int32 DungeonKey = static_cast<int32>(CellId & 0xFFFF0000u);
			if (KeptLandblocks.Num() != 1 || !KeptLandblocks.Contains(DungeonKey))
			{
				ClearLandblocks();
				KeptLandblocks.Add(DungeonKey);
				Dat->CancelLandblockMeshRequestsOutside({});
				Dat->EvictLandblockMeshesOutside({});
			}
			LastCenterLB = static_cast<int32>(CellId >> 16);
			bLastSyncComplete = true;
			SyncWorldEntitiesToKeepRing(GetOwner(), KeptLandblocks);
			return;
		}
	}
	const uint32 LscapeCell = CellId;

	int32 CenterX = static_cast<int32>((LscapeCell >> 24) & 0xFF);
	int32 CenterY = static_cast<int32>((LscapeCell >> 16) & 0xFF);
	int32 CenterKey = (CenterX << 8) | CenterY;

	if (LastCenterLB >= 0 && CenterKey != LastCenterLB)
	{
		const int32 OldX = (LastCenterLB >> 8) & 0xFF;
		const int32 OldY = LastCenterLB & 0xFF;
		const int32 Jump = FMath::Max(FMath::Abs(CenterX - OldX), FMath::Abs(CenterY - OldY));
		if (Jump >= 2 && !ShouldHoldStagedLoadRadius())
		{
			if (Jump >= LoadRadius)
			{
				StagedLoadRadius = 1;
				StagedExpandAccum = 0.f;
			}
			PendingStreamCenterKey = -1;
			StreamCenterHoldAccum = 0.f;
		}
		else if (Jump == 1)
		{
			if (PendingStreamCenterKey != CenterKey)
			{
				PendingStreamCenterKey = CenterKey;
				StreamCenterHoldAccum = 0.f;
				UE_LOG(LogTemp, Log,
					TEXT("ACE: TerrainPresenter hold stream center (%d,%d) — pending (%d,%d)"),
					OldX, OldY, CenterX, CenterY);
			}
			StreamCenterHoldAccum += 0.1f;
			const bool bCommit = StreamCenterHoldAccum >= 0.45f
				&& IsWellInsideLandblock(LscapeCell, LastCenterLB);
			if (!bCommit)
			{
				CenterX = OldX;
				CenterY = OldY;
				CenterKey = LastCenterLB;
				if (bLastSyncComplete)
				{
					return;
				}
			}
			else
			{
				StagedLoadRadius = LoadRadius;
				StagedExpandAccum = 0.f;
				PendingStreamCenterKey = -1;
				StreamCenterHoldAccum = 0.f;
			}
		}
	}
	else
	{
		PendingStreamCenterKey = -1;
		StreamCenterHoldAccum = 0.f;
	}

	if (CenterKey == LastCenterLB && bLastSyncComplete)
	{
		return;
	}

	const int32 EffectiveLoadRadius = FMath::Clamp(StagedLoadRadius, 0, FMath::Max(0, LoadRadius));
	// While staging, do not keep a wider hysteresis ring than we are allowed to show.
	const int32 EffectiveUnloadRadius = (StagedLoadRadius < LoadRadius)
		? EffectiveLoadRadius
		: FMath::Max(LoadRadius, UnloadRadius);
	const int32 ChunkSize = FMath::Clamp(TerrainChunkSize, 1, 16);

	auto AlignChunk = [ChunkSize](int32 Coord) -> int32
	{
		return (Coord / ChunkSize) * ChunkSize;
	};
	auto MakeChunkKey = [](int32 Ox, int32 Oy) -> int32
	{
		return (Ox << 24) | (Oy << 16);
	};

	UGameInstance* GI = World->GetGameInstance();
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;

	if (ChunkSize > 1)
	{
		TSet<int32> NeededLoadChunks;
		TSet<int32> NeededKeepChunks;
		TSet<int32> NeededKeepLBs;

		auto AddLB = [&](int32 X, int32 Y, bool bLoad)
		{
			if (X < 0 || X > 255 || Y < 0 || Y > 255)
			{
				return;
			}
			const int32 LB = (X << 24) | (Y << 16);
			NeededKeepLBs.Add(LB);
			const int32 Ox = AlignChunk(X);
			const int32 Oy = AlignChunk(Y);
			const int32 CK = MakeChunkKey(Ox, Oy);
			NeededKeepChunks.Add(CK);
			if (bLoad)
			{
				NeededLoadChunks.Add(CK);
			}
		};

		AddLB(CenterX, CenterY, true);
		for (int32 DX = -EffectiveLoadRadius; DX <= EffectiveLoadRadius; ++DX)
		{
			for (int32 DY = -EffectiveLoadRadius; DY <= EffectiveLoadRadius; ++DY)
			{
				AddLB(CenterX + DX, CenterY + DY, true);
			}
		}
		for (int32 DX = -EffectiveUnloadRadius; DX <= EffectiveUnloadRadius; ++DX)
		{
			for (int32 DY = -EffectiveUnloadRadius; DY <= EffectiveUnloadRadius; ++DY)
			{
				AddLB(CenterX + DX, CenterY + DY, false);
			}
		}

		TArray<int32> ToRemove;
		for (const auto& Pair : SpawnedChunks)
		{
			if (!NeededKeepChunks.Contains(Pair.Key))
			{
				ToRemove.Add(Pair.Key);
			}
		}
		for (int32 Key : ToRemove)
		{
			if (AACETerrainChunkActor* A = SpawnedChunks.FindRef(Key))
			{
				UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter unload chunk 0x%08X"), Key);
				A->Destroy();
			}
			SpawnedChunks.Remove(Key);
		}

		// Tear down legacy per-LB actors when switching to chunks.
		for (auto& Pair : Spawned)
		{
			if (Pair.Value)
			{
				Pair.Value->Destroy();
			}
		}
		Spawned.Reset();

		LastCenterLB = CenterKey;
		KeptLandblocks = NeededKeepLBs;
		SyncWorldEntitiesToKeepRing(GetOwner(), KeptLandblocks);
		if (Dat)
		{
			Dat->CancelLandblockMeshRequestsOutside(NeededKeepLBs);
			Dat->EvictLandblockMeshesOutside(NeededKeepLBs);
		}

		TArray<int32> OrderedChunks = NeededLoadChunks.Array();
		OrderedChunks.Sort([CenterX, CenterY, AlignChunk](int32 A, int32 B)
		{
			const int32 Ax = (A >> 24) & 0xFF;
			const int32 Ay = (A >> 16) & 0xFF;
			const int32 Bx = (B >> 24) & 0xFF;
			const int32 By = (B >> 16) & 0xFF;
			const int32 Da = FMath::Max(FMath::Abs(Ax - AlignChunk(CenterX)), FMath::Abs(Ay - AlignChunk(CenterY)));
			const int32 Db = FMath::Max(FMath::Abs(Bx - AlignChunk(CenterX)), FMath::Abs(By - AlignChunk(CenterY)));
			return Da < Db;
		});

		int32 LoadedThisCall = 0;
		int32 MaxChunks = Dat && Dat->IsLightweightStreaming() ? 1 : 2;
		if (Dat && Dat->IsInPortalSpace() && !Dat->IsLightweightStreaming())
		{
			MaxChunks = 1;
		}
		for (int32 CK : OrderedChunks)
		{
			if (SpawnedChunks.Contains(CK))
			{
				continue;
			}
			if (LoadedThisCall >= MaxChunks)
			{
				break;
			}
			if (!Dat)
			{
				break;
			}

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AACETerrainChunkActor* Chunk = World->SpawnActor<AACETerrainChunkActor>(
				TerrainChunkClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Chunk)
			{
				continue;
			}
			Chunk->ChunkOriginLandblockId = CK;
			Chunk->ChunkSizeLandblocks = ChunkSize;
			Chunk->WorldScale = WorldScale;
			if (Chunk->TryLoadChunk(Dat))
			{
				SpawnedChunks.Add(CK, Chunk);
				++LoadedThisCall;
				UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter load chunk 0x%08X (%d chunks)"), CK, SpawnedChunks.Num());
			}
			else
			{
				// Keep actor so next tick retries once pending LB meshes finish.
				SpawnedChunks.Add(CK, Chunk);
			}
		}

		// Retry incomplete chunks (pending async LB builds).
		for (auto& Pair : SpawnedChunks)
		{
			if (Pair.Value && !Pair.Value->IsChunkTerrainReady())
			{
				Pair.Value->TryLoadChunk(Dat);
			}
		}

		int32 Missing = 0;
		for (int32 CK : NeededLoadChunks)
		{
			AACETerrainChunkActor* C = SpawnedChunks.FindRef(CK);
			if (!C || !C->IsChunkTerrainReady())
			{
				++Missing;
			}
		}
		bLastSyncComplete = (Missing == 0);
		if (!bLastSyncComplete)
		{
			LastCenterLB = -1;
		}
		ApplyLandblockDetailLevels(CenterX, CenterY);
		return;
	}

	// ---- Per-landblock path (TerrainChunkSize == 1) ----
	if (!LandblockClass)
	{
		return;
	}

	TArray<int32> Ordered;
	TSet<int32> NeededLoad;
	TSet<int32> NeededKeep;
	const int32 CenterLB = (CenterX << 24) | (CenterY << 16);
	Ordered.Add(CenterLB);
	NeededLoad.Add(CenterLB);
	NeededKeep.Add(CenterLB);

	auto AddRing = [&](int32 Radius, bool bQueueLoad)
	{
		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			for (int32 DY = -Radius; DY <= Radius; ++DY)
			{
				if (DX == 0 && DY == 0)
				{
					continue;
				}
				const int32 X = CenterX + DX;
				const int32 Y = CenterY + DY;
				if (X < 0 || X > 255 || Y < 0 || Y > 255)
				{
					continue;
				}
				const int32 LB = (X << 24) | (Y << 16);
				NeededKeep.Add(LB);
				if (bQueueLoad)
				{
					NeededLoad.Add(LB);
					Ordered.Add(LB);
				}
			}
		}
	};

	if (EffectiveLoadRadius > 0)
	{
		AddRing(EffectiveLoadRadius, /*bQueueLoad*/ true);
	}
	if (EffectiveUnloadRadius > EffectiveLoadRadius)
	{
		AddRing(EffectiveUnloadRadius, /*bQueueLoad*/ false);
	}

	// Nearest first (Chebyshev) so the ring around the player fills before far corners.
	Ordered.Sort([CenterX, CenterY](int32 A, int32 B)
	{
		const int32 Ax = (A >> 24) & 0xFF;
		const int32 Ay = (A >> 16) & 0xFF;
		const int32 Bx = (B >> 24) & 0xFF;
		const int32 By = (B >> 16) & 0xFF;
		const int32 Da = FMath::Max(FMath::Abs(Ax - CenterX), FMath::Abs(Ay - CenterY));
		const int32 Db = FMath::Max(FMath::Abs(Bx - CenterX), FMath::Abs(By - CenterY));
		if (Da != Db)
		{
			return Da < Db;
		}
		return A < B;
	});

	// Drop permanent-fail marks for landblocks that are back in the keep set (DAT may be ready now).
	{
		TArray<int32> RetryKeys;
		for (int32 FailedKey : FailedLandblocks)
		{
			if (NeededKeep.Contains(FailedKey))
			{
				RetryKeys.Add(FailedKey);
			}
		}
		for (int32 Key : RetryKeys)
		{
			FailedLandblocks.Remove(Key);
			if (Dat)
			{
				Dat->AllowLandblockMeshRetry(static_cast<uint32>(Key));
			}
		}
	}

	// Unload only outside UnloadRadius — keeps a hysteresis ring so boundary crossings
	// don't destroy the landblock the camera is still looking at.
	TArray<int32> ToRemove;
	for (const auto& Pair : Spawned)
	{
		if (!NeededKeep.Contains(Pair.Key))
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (int32 Key : ToRemove)
	{
		if (AACELandblockActor* A = Spawned.FindRef(Key))
		{
			UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter unload landblock 0x%08X"), Key);
			A->Destroy();
		}
		Spawned.Remove(Key);
		FailedLandblocks.Remove(Key);
	}

	LastCenterLB = CenterKey;
	KeptLandblocks = NeededKeep;

	const bool bDatReady = Dat && Dat->IsDatReady();

	if (Dat)
	{
		// Drop queued builds that walked out of the keep ring — don't waste workers on them.
		Dat->CancelLandblockMeshRequestsOutside(NeededKeep);
		Dat->EvictLandblockMeshesOutside(NeededKeep);
		// UACEDatSubsystem::Tick owns the shared cache budget. This path used to
		// shrink it from 2048 setups/1024 textures to 192/384 every five seconds,
		// repeatedly throwing away assets still needed by the streaming ring.

	}

	const int32 MaxPerCall = FMath::Max(1, LandblocksPerTick);
	int32 EffectiveMax = MaxPerCall;
	if (Dat && Dat->IsLightweightStreaming())
	{
		EffectiveMax = 1;
	}
	if (LandblockLoginBurstTicks > 0)
	{
		EffectiveMax = FMath::Max(EffectiveMax, 8);
	}
	if (IsIndoorCell(CellId))
	{
		// A town interior can see outside (including open Sanctuary bridges).
		// Dungeons returned above. Keep loading the exterior at a bounded rate
		// instead of stopping forever until the player walks through the exit.
		EffectiveMax = 1;
	}
	int32 LoadedThisCall = 0;
	// Background work must not stop when this frame's mesh-install budget is
	// exhausted. Queue the visible ring nearest first; the DAT subsystem bounds
	// worker concurrency independently. This also fills otherwise idle workers
	// while completed meshes wait for their game-thread upload.
	if (Dat && EffectiveMax > 0)
	{
		for (int32 LB : Ordered)
		{
			if (!Spawned.Contains(LB) && !FailedLandblocks.Contains(LB)
				&& !Dat->FindLandblockMesh(static_cast<uint32>(LB)))
				Dat->RequestLandblockMesh(static_cast<uint32>(LB), WorldScale);
		}
	}
	const double ApplyStartSec = FPlatformTime::Seconds();
	for (int32 LB : Ordered)
	{
		if (!NeededLoad.Contains(LB))
		{
			continue;
		}
		if (Spawned.Contains(LB) || FailedLandblocks.Contains(LB))
		{
			continue;
		}
		if (LoadedThisCall >= EffectiveMax || (LoadedThisCall > 0
			&& FPlatformTime::Seconds()-ApplyStartSec >= (bPortalStreaming ? .002 : .004)))
		{
			break;
		}

		if (!Dat)
		{
			break;
		}

		const uint32 LbId = static_cast<uint32>(LB);
		// Async: queue DAT/disk bake off the game thread; only spawn once the mesh is Ready.
		const UACEDatSubsystem::EACELandMeshStatus Status = Dat->RequestLandblockMesh(LbId, WorldScale);
		if (Status == UACEDatSubsystem::EACELandMeshStatus::NotReady)
		{
			UE_LOG(LogTemp, Verbose, TEXT("ACE: TerrainPresenter defer landblock 0x%08X (DAT not ready)"), LB);
			break;
		}
		if (Status == UACEDatSubsystem::EACELandMeshStatus::Pending)
		{
			continue;
		}
		if (Status == UACEDatSubsystem::EACELandMeshStatus::Failed)
		{
			FailedLandblocks.Add(LB);
			UE_LOG(LogTemp, Warning, TEXT("ACE: TerrainPresenter failed landblock 0x%08X mesh — skipping"), LB);
			continue;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AACELandblockActor* Actor = World->SpawnActor<AACELandblockActor>(LandblockClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
		int32 PolySize = 1;
		int32 TransDir = 0;
		FACELandblockMeshBuilder::GetBlockOrient(
			((LB >> 24) & 0xFF) - CenterX, ((LB >> 16) & 0xFF) - CenterY, PolySize, TransDir);
		const bool bPreloadScenery = FMath::Max(FMath::Abs(int32((uint32(LB) >> 24) & 0xFF) - CenterX),
			FMath::Abs(int32((uint32(LB) >> 16) & 0xFF) - CenterY)) <= 2;
		if (Actor && Actor->LoadLandblock(LB, WorldScale, bPreloadScenery, PolySize, TransDir))
		{
			if (bWcBakedTerrainMode)
			{
				Actor->SetOutdoorTerrainHiddenInGame(true);
				Actor->SetOutdoorTerrainCollisionEnabled(false);
				Actor->SetBuildingShellsBlockPawn(true);
				Actor->SetSkipNonBuildingScenery(true);
			}
			Spawned.Add(LB, Actor);
			++LoadedThisCall;
			UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter load landblock 0x%08X (%d spawned, loadR=%d unloadR=%d)"),
				LB, Spawned.Num(), EffectiveLoadRadius, EffectiveUnloadRadius);
		}
		else
		{
			if (Actor)
			{
				Actor->Destroy();
			}
			if (bDatReady)
			{
				FailedLandblocks.Add(LB);
				UE_LOG(LogTemp, Warning, TEXT("ACE: TerrainPresenter failed landblock 0x%08X apply — skipping"), LB);
			}
		}
	}

	int32 Missing = 0;
	int32 FailedInRing = 0;
	for (int32 LB : NeededLoad)
	{
		if (!Spawned.Contains(LB))
		{
			++Missing;
			if (FailedLandblocks.Contains(LB))
			{
				++FailedInRing;
			}
		}
	}
	// Failed LBs are still holes — never mark complete or RetrySync stops forever.
	bLastSyncComplete = (Missing == 0);
	if (FailedInRing > 0)
	{
		bLastSyncComplete = false;
	}
	{
		SyncWorldEntitiesToKeepRing(GetOwner(), KeptLandblocks);
	}
    const double ProgressNow = FPlatformTime::Seconds();
    const FIntPoint ProgressCenter(CenterX,CenterY);
    if (ProgressCenter != LastTerrainProgressCenter || LastTerrainProgressRemaining < 0
        || Missing != LastTerrainProgressRemaining || LoadedThisCall > 0 || EffectiveMax == 0)
        LastTerrainProgressAt = ProgressNow;
    LastTerrainProgressCenter = ProgressCenter;
    LastTerrainProgressRemaining = Missing;
    if (!bLastSyncComplete)
    {
        if (ProgressNow - LastTerrainPendingLogAt >= 10.0)
        {
            LastTerrainPendingLogAt = ProgressNow;
            if (EffectiveMax > 0 && ProgressNow - LastTerrainProgressAt >= 30.0)
            {
                UE_LOG(LogTemp, Warning, TEXT("ACE: TerrainPresenter stalled: %d pending, %d failed, no progress for %.0fs (loadR=%d)"),
                    Missing, FailedInRing, ProgressNow-LastTerrainProgressAt, EffectiveLoadRadius);
            }
            else
            {
                UE_LOG(LogTemp, Verbose, TEXT("ACE: TerrainPresenter streaming %d/%d terrain blocks (loadR=%d, scenery preloadR=2, paused=%d)"),
                    NeededLoad.Num()-Missing, NeededLoad.Num(), EffectiveLoadRadius, EffectiveMax==0);
            }
        }
    }
	else if (LoadedThisCall > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter SyncAroundCell center=(%d,%d) — complete, %d landblock(s) (loadR=%d)"),
			CenterX, CenterY, Spawned.Num(), EffectiveLoadRadius);
	}

	// Only when the spawned set changed. Re-hiding every RetrySync recreated shop
	// collision and looked like buildings loading/unloading in town.
	ApplyLandblockDetailLevels(CenterX, CenterY);
	if (LoadedThisCall > 0)
	{
		UpdateBuildingVisibility();
	}
}

void UACETerrainPresenterComponent::ClearEnvCells()
{
	ClearOutdoorPortalDepthApertures();
	for (auto& Pair : SpawnedEnvCells)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	SpawnedEnvCells.Reset();
	FailedEnvCells.Reset();
	LastOutdoorApertures.Reset();
	LastEnvSyncKey = 0;
	LastEnvDirtyKey = 0;
	LastEnvLbActorCount = -1;
	OutdoorPeekKeepUntil.Reset();
	LastOutdoorEnvSyncSec = 0.0;
	EnvStreamCellId = 0;
	IndoorStreamHoldUntil = 0.0;
	OccupancyFlipUntil = 0.0;
	PendingOccupancyCellId = 0;
	HeldIndoorVisible.Reset();
	bLastEnvSyncComplete = false;
}

void UACETerrainPresenterComponent::ClearOutdoorPortalDepthApertures()
{
	OutdoorPortalDepthFingerprint = 0;
	if (OutdoorPortalDepthMesh)
	{
		OutdoorPortalDepthMesh->ClearAllMeshSections();
		OutdoorPortalDepthMesh->DestroyComponent();
		OutdoorPortalDepthMesh = nullptr;
	}
}

void UACETerrainPresenterComponent::SyncOutdoorPortalDepthApertures(
	const TArray<ACEOutdoorPortalPlan::FAdmittedAperture>& Apertures)
{
	(void)Apertures;
	// ProceduralMesh ignores bRenderInMainPass in the editor viewport and painted
	// yellow/default portal flaps in doorways. Doorway holes are shader slabs on shells.
	ClearOutdoorPortalDepthApertures();
}

void UACETerrainPresenterComponent::CollectNeededEnvCells(TArray<int32>& OutOrdered, TSet<int32>& OutNeeded)
{
	OutOrdered.Reset();
	OutNeeded.Reset();

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return;
	}

	auto AddCell = [&](uint32 FullId)
	{
		if (!IsIndoorCell(FullId))
		{
			return;
		}
		const int32 Id = static_cast<int32>(FullId);
		if (FailedEnvCells.Contains(Id) || OutNeeded.Contains(Id))
		{
			return;
		}
		OutNeeded.Add(Id);
		OutOrdered.Add(Id);
	};

	for (int32 Id : HeldIndoorVisible) AddCell(Id);

	const uint32 StreamCell = LastKnownCellId;
	const uint32 PlayerLb = StreamCell & 0xFFFF0000u;

	FVector PlayerUe = FVector::ZeroVector;
	if (Client)
	{
		const FACEPosition Pos = Client->GetPlayerPosition();
		if (Pos.IsValid())
		{
			PlayerUe = Pos.ToUnrealLocation(WorldScale);
		}
	}
	if (PlayerUe.IsNearlyZero() && World)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				PlayerUe = Pawn->GetActorLocation();
			}
		}
	}

	auto CellWorldLoc = [&](int32 Id) -> FVector
	{
		const uint32 U = static_cast<uint32>(Id);
		const uint32 Lbx = (U >> 24) & 0xFF;
		const uint32 Lby = (U >> 16) & 0xFF;
		FVector Origin = FACEPosition::AceVectorToUnreal(
			FVector(Lbx * 192.f, Lby * 192.f, 0.f), WorldScale);
		if (const TObjectPtr<AACEEnvCellActor>* Existing = SpawnedEnvCells.Find(Id))
		{
			if (const AACEEnvCellActor* Actor = Existing->Get())
			{
				if (Actor->CellMesh)
				{
					return Actor->CellMesh->GetComponentLocation();
				}
				return Actor->GetActorLocation();
			}
		}
		if (const FACEBuiltEnvCellMesh* Mesh = Dat->FindEnvCellMesh(U, WorldScale))
		{
			Origin += Mesh->GetCellLocalToLandblock(WorldScale).GetLocation();
			return Origin;
		}
		FACEDatEnvCell DatCell;
		if (Dat->LoadEnvCell(U, DatCell))
		{
			Origin += FACEPosition::AceVectorToUnreal(
				FVector(DatCell.Origin.X, DatCell.Origin.Y, DatCell.Origin.Z), WorldScale);
		}
		return Origin;
	};

	bool bKeepOutdoorGrab = true;
	if (IsIndoorCell(StreamCell))
	{
		// CEnvCell::grab_visible_cells: this cell + stab_list (VisibleCells / indoor portals).
		AddCell(StreamCell);
		Dat->RequestEnvCellMesh(StreamCell, WorldScale);
		TSet<int32> Pvs;
		AddIndoorPvsFromDat(Dat, StreamCell, WorldScale, Pvs, nullptr);
		if (IsIndoorCell(ViewerCellId) && ViewerCellId != StreamCell)
		{
			AddCell(ViewerCellId);
			Dat->RequestEnvCellMesh(ViewerCellId, WorldScale);
			AddIndoorPvsFromDat(Dat, ViewerCellId, WorldScale, Pvs, nullptr);
		}
		{
			FACEDatLandblockInfo Info;
			const bool bDungeon = !Dat->LoadLandblockInfo(StreamCell & 0xFFFF0000u, Info)
				|| Info.Buildings.Num() == 0;
			if (bDungeon)
			{
				ExpandIndoorPvsHops(Dat, StreamCell, WorldScale, 3, Pvs, nullptr, 48);
				bKeepOutdoorGrab = false;
			}
		}
		// Stair/landing CellPortals must spawn even when DAT VisibleCells omits the upper floor.
		TSet<int32> Stair;
		Stair.Add(static_cast<int32>(StreamCell));
		AddIndoorPvsFromDat(Dat, StreamCell, WorldScale, Stair, nullptr, /*bIncludeVisibleCells*/ false);
		ExpandIndoorPvsHops(Dat, StreamCell, WorldScale, 3, Stair, nullptr, 20,
			/*bIncludeVisibleCells*/ false);
		Pvs.Append(Stair);
		for (int32 Id : Pvs)
		{
			AddCell(static_cast<uint32>(Id));
			Dat->RequestEnvCellMesh(static_cast<uint32>(Id), WorldScale);
		}
		if (!bKeepOutdoorGrab)
		{
			OutOrdered.Sort([&](int32 A, int32 B)
			{
				return FVector::DistSquared(PlayerUe, CellWorldLoc(A))
					< FVector::DistSquared(PlayerUe, CellWorldLoc(B));
			});
			return;
		}
		// Town shop: keep doorway grab_visible so walking in/out does not destroy
		// street look-in actors (that hitch + black doors from the plaza).
	}

	// Outdoor / town indoor: CLandBlock::grab_visible_cells for every full-res landblock.
	{
		const int32 Cx = static_cast<int32>((PlayerLb >> 24) & 0xFF);
		const int32 Cy = static_cast<int32>((PlayerLb >> 16) & 0xFF);
		const int32 FullR = GetFullDetailRadius();
		for (int32 DX = -FullR; DX <= FullR; ++DX)
		{
			for (int32 DY = -FullR; DY <= FullR; ++DY)
			{
				const int32 X = Cx + DX;
				const int32 Y = Cy + DY;
				if (X < 0 || X > 255 || Y < 0 || Y > 255)
				{
					continue;
				}
				const uint32 Lb = (static_cast<uint32>(X) << 24) | (static_cast<uint32>(Y) << 16);
				AddLandblockGrabVisible(Dat, Lb, WorldScale, AddCell);
				AddLookInStabCells(Dat, Lb, PlayerUe, WorldScale, LastOutdoorApertures, AddCell);
			}
		}
	}
	OutOrdered.Sort([&](int32 A, int32 B)
	{
		return FVector::DistSquared(PlayerUe, CellWorldLoc(A))
			< FVector::DistSquared(PlayerUe, CellWorldLoc(B));
	});
}

void UACETerrainPresenterComponent::TickDegradeController(float DeltaTime)
{
	// Retail Render::CalcDegLevel — Mamdani fuzzy controller over FPS, deg_mul in [-1,+1].
	const float Fps = 1.f / FMath::Max(DeltaTime, 0.0005f);
	DegradeFpsEma = (DegradeFpsEma < 1.f) ? Fps : FMath::Lerp(DegradeFpsEma, Fps, 0.12f);

	auto Triangle = [](float X, float A, float B, float C) -> float
	{
		if (X <= A || X >= C)
		{
			return 0.f;
		}
		return (X < B)
			? (X - A) / FMath::Max(KINDA_SMALL_NUMBER, B - A)
			: (C - X) / FMath::Max(KINDA_SMALL_NUMBER, C - B);
	};
	auto LowShoulder = [](float X, float Peak, float End) -> float
	{
		if (X <= Peak)
		{
			return 1.f;
		}
		if (X >= End)
		{
			return 0.f;
		}
		return (End - X) / FMath::Max(KINDA_SMALL_NUMBER, End - Peak);
	};
	auto HighShoulder = [](float X, float Start, float Peak) -> float
	{
		if (X >= Peak)
		{
			return 1.f;
		}
		if (X <= Start)
		{
			return 0.f;
		}
		return (X - Start) / FMath::Max(KINDA_SMALL_NUMBER, Peak - Start);
	};

	float Num = 0.f;
	float Den = 0.f;
	auto AddRule = [&](float Mu, float Cons)
	{
		Num += Mu * Cons;
		Den += Mu;
	};
	AddRule(LowShoulder(DegradeFpsEma, 6.f, 9.f), -0.15f);
	AddRule(Triangle(DegradeFpsEma, 8.f, 8.75f, 9.5f), -0.02f);
	AddRule(Triangle(DegradeFpsEma, 9.f, 12.f, 15.f), 0.f);
	AddRule(Triangle(DegradeFpsEma, 12.5f, 16.25f, 20.f), 0.01f);
	AddRule(HighShoulder(DegradeFpsEma, 15.f, 25.f), 0.10f);

	const float DeltaMul = (Den > KINDA_SMALL_NUMBER) ? (Num / Den) : 0.f;
	const float NewMul = FMath::Clamp(DegradeMul + DeltaMul, -1.f, 1.f);

	bool bNovel = true;
	for (const float Hist : DegradeHistory)
	{
		if (FMath::Abs(Hist - NewMul) < 0.01f)
		{
			bNovel = false;
			break;
		}
	}
	DegradeApplyAccum += DeltaTime;
	if (!bNovel && DegradeApplyAccum < 0.25f)
	{
		return;
	}
	if (bNovel)
	{
		DegradeMul = NewMul;
		DegradeHistory.Add(NewMul);
		if (DegradeHistory.Num() > 29)
		{
			DegradeHistory.RemoveAt(0, DegradeHistory.Num() - 29, EAllowShrinking::No);
		}
	}
	if (DegradeApplyAccum < 0.25f && !bNovel)
	{
		return;
	}
	DegradeApplyAccum = 0.f;

	// Map deg_mul to HISM end-cull: 3 landblocks (worst) → 5 (best, full 11×11 flora).
	const float DistAc = FMath::Lerp(192.f * 3.f, 192.f * 5.f, (DegradeMul + 1.f) * 0.5f);
	const float EndCm = DistAc * WorldScale;
	const uint32 PlayerLb = LastKnownCellId & 0xFFFF0000u;
	for (const auto& Pair : Spawned)
	{
		if (Pair.Value)
		{
			const float ThisEnd = (static_cast<uint32>(Pair.Key) == PlayerLb) ? 0.f : EndCm;
			Pair.Value->ApplyDegradeCull(ThisEnd);
		}
	}
	for (const auto& Pair : SpawnedChunks)
	{
		if (Pair.Value)
		{
			Pair.Value->ApplyDegradeCull(EndCm);
		}
	}
}

void UACETerrainPresenterComponent::ApplyOutdoorDoorwayClipsFromApertures()
{
	// Retail doorway holes are DrawingBSP PORT polygons omitted from the Setup mesh.
	// Shader slabs match DAT building portals on the player landblock — occupancy must
	// not clear them (indoor empty clips hid the facade and left a white void).
	TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Source = LastOutdoorApertures;
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (Dat)
	{
		const uint32 PlayerLb = LastKnownCellId & 0xFFFF0000u;
		ACEOutdoorPortalPlan::CollectLandblockDoorwayApertures(*Dat, PlayerLb, WorldScale, Source);
	}

	TArray<FACEBuildingDoorwayClip> Clips;
	Clips.Reserve(Source.Num());
	for (const ACEOutdoorPortalPlan::FAdmittedAperture& A : Source)
	{
		if (A.WorldVerts.Num() < 3)
		{
			continue;
		}
		FACEBuildingDoorwayClip C;
		C.DestEnvCellId = A.DestEnvCellId;
		C.Normal = A.WorldNormal.GetSafeNormal();
		if (C.Normal.IsNearlyZero())
		{
			continue;
		}
		FVector Sum = FVector::ZeroVector;
		for (const FVector& V : A.WorldVerts)
		{
			Sum += V;
		}
		C.Center = Sum / static_cast<float>(A.WorldVerts.Num());
		const FVector Nn = C.Normal;
		const FVector T = FMath::Abs(Nn.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
		const FVector R = FVector::CrossProduct(T, Nn).GetSafeNormal();
		const FVector Uax = FVector::CrossProduct(Nn, R);
		if (R.IsNearlyZero())
		{
			continue;
		}
		float MaxW = 0.f;
		float MaxH = 0.f;
		for (const FVector& V : A.WorldVerts)
		{
			const FVector L = V - C.Center;
			MaxW = FMath::Max(MaxW, FMath::Abs(static_cast<float>(FVector::DotProduct(L, R))));
			MaxH = FMath::Max(MaxH, FMath::Abs(static_cast<float>(FVector::DotProduct(L, Uax))));
		}
		// Retail omits DrawingBSP PORT polys — the wooden frame stays. Oversized slabs ate
		// the frame and adjacent stucco. Pad just enough for rasterization of a thin shell.
		C.HalfWidth = FMath::Max(8.f, MaxW + 4.f);
		C.HalfHeight = FMath::Max(8.f, MaxH + 4.f);
		C.Thickness = 36.f;
		Clips.Add(C);
	}
	if (Clips.Num() == 0 && IsIndoorCell(LastKnownCellId) && LastDoorwayClipFingerprint != 0)
	{
		return;
	}
	uint32 Fp = GetTypeHash(Clips.Num());
	for (const FACEBuildingDoorwayClip& C : Clips)
	{
		Fp = HashCombine(Fp, GetTypeHash(C.DestEnvCellId));
		Fp = HashCombine(Fp, GetTypeHash(FMath::RoundToInt(C.Center.X)));
		Fp = HashCombine(Fp, GetTypeHash(FMath::RoundToInt(C.Center.Y)));
		Fp = HashCombine(Fp, GetTypeHash(FMath::RoundToInt(C.Center.Z)));
		Fp = HashCombine(Fp, GetTypeHash(FMath::RoundToInt(C.HalfWidth)));
		Fp = HashCombine(Fp, GetTypeHash(FMath::RoundToInt(C.HalfHeight)));
		Fp = HashCombine(Fp, GetTypeHash(FMath::RoundToInt(C.Thickness)));
	}
	const int32 PlayerLb = static_cast<int32>(LastKnownCellId & 0xFFFF0000u);
	int32 ShellCount = 0;
	AACELandblockActor* PlayerLbActor = Spawned.FindRef(PlayerLb);
	if (!PlayerLbActor)
	{
		for (const auto& Pair : SpawnedChunks)
		{
			PlayerLbActor = Pair.Value ? Pair.Value->FindChildLandblock(PlayerLb) : nullptr;
			if (PlayerLbActor)
			{
				break;
			}
		}
	}
	if (PlayerLbActor)
	{
		ShellCount = PlayerLbActor->GetDoorwayClipMeshCount();
	}
	Fp = HashCombine(Fp, GetTypeHash(ShellCount));
	if (Fp == LastDoorwayClipFingerprint && PlayerLb == LastDoorwayClipLandblock)
	{
		return;
	}

	auto ApplyToLandblock = [&](int32 LbKey, const TArray<FACEBuildingDoorwayClip>& DoorClips)
	{
		if (AACELandblockActor* Lb = Spawned.FindRef(LbKey))
		{
			Lb->ApplyOutdoorDoorwayClips(DoorClips);
			return;
		}
		for (const auto& Pair : SpawnedChunks)
		{
			if (AACELandblockActor* Child = Pair.Value ? Pair.Value->FindChildLandblock(LbKey) : nullptr)
			{
				Child->ApplyOutdoorDoorwayClips(DoorClips);
				return;
			}
		}
	};

	if (LastDoorwayClipLandblock != 0 && LastDoorwayClipLandblock != PlayerLb)
	{
		ApplyToLandblock(LastDoorwayClipLandblock, TArray<FACEBuildingDoorwayClip>());
	}
	LastDoorwayClipFingerprint = Fp;
	LastDoorwayClipLandblock = PlayerLb;
	{
		static int32 LastClipLog = -1;
		if (Clips.Num() != LastClipLog)
		{
			LastClipLog = Clips.Num();
			UE_LOG(LogTemp, Log, TEXT("ACE: doorway clips=%d (player LB 0x%08X)"), Clips.Num(), PlayerLb);
		}
	}
	ApplyToLandblock(PlayerLb, Clips);
}

void UACETerrainPresenterComponent::UpdateSkyWeatherState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UGameInstance* GI = World->GetGameInstance();
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	const bool bPortalSpace = Dat && Dat->IsInPortalSpace();
	// Retail GameSky::Draw(after): weather only when SmartBox::is_player_outside.
	bool bPlayerOutside = !bHasKnownCell || !IsIndoorCell(LastKnownCellId);
	if (!bHasKnownCell && Client)
	{
		const FACEPosition Pose = Client->GetPlayerPosition();
		if (Pose.IsValid())
		{
			bPlayerOutside = !IsIndoorCell(static_cast<uint32>(Pose.CellId));
		}
	}
	bool bDisableWeatherFx = false;
	if (UACEClientSubsystem* OptionsClient = GI ? GI->GetSubsystem<UACEClientSubsystem>() : nullptr)
	{
		bDisableWeatherFx = (OptionsClient->GetCharacterOptions1() & 0x00010000u) != 0;
	}
	const bool bEnableSkyWeather = !bPortalSpace
		&& bPlayerOutside
		&& !bDisableWeatherFx;
	for (TActorIterator<AACESkyDomeActor> It(World); It; ++It)
	{
		if (AACESkyDomeActor* Sky = *It)
		{
			Sky->SetWeatherEnabled(bEnableSkyWeather);
		}
	}
}

FACEPosition UACETerrainPresenterComponent::GetPresentationPosition() const
{
	FACEPosition Position;
	if (UWorld* World = GetWorld())
		if (const auto* PC = Cast<AACEPlayerController>(World->GetFirstPlayerController()))
			if (PC->TryGetLocallyPredictedPosition(Position)) return Position;
	return Client ? Client->GetPlayerPosition() : Position;
}

uint32 UACETerrainPresenterComponent::ResolveDrawOccupancyCellId(UACEDatSubsystem* Dat) const
{
	(void)Dat;
	return ViewerCellId != 0 ? ViewerCellId : LastKnownCellId;
}

uint32 UACETerrainPresenterComponent::ResolveCollideOccupancyCellId(UACEDatSubsystem* Dat) const
{
	(void)Dat;
	return LastKnownCellId;
}

void UACETerrainPresenterComponent::RefreshViewerCellId(UACEDatSubsystem* Dat)
{
	if (!Dat || LastKnownCellId == 0)
	{
		ViewerCellId = LastKnownCellId;
		return;
	}

	UWorld* World = GetWorld();
	FVector PlayerUe = FVector::ZeroVector;
	bool bHavePlayerUe = false;
	if (Client)
	{
		const FACEPosition Pos = GetPresentationPosition();
		if (Pos.IsValid())
		{
			PlayerUe = Pos.ToUnrealLocation(WorldScale);
			bHavePlayerUe = true;
		}
	}
	if (!bHavePlayerUe && World)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				PlayerUe = Pawn->GetActorLocation();
				bHavePlayerUe = true;
			}
		}
	}

	FVector CamUe = PlayerUe;
	FConvexVolume CameraFrustum;
	const bool bHaveView = ACEOutdoorPortalPlan::TryGetPlayerViewFrustum(World, CamUe, CameraFrustum);
	if (World && !bHaveView)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
			{
				CamUe = CamMgr->GetCameraLocation();
			}
		}
	}

	if (!bHavePlayerUe)
	{
		ViewerCellId = LastKnownCellId;
		return;
	}

	uint32 Resolved = LastKnownCellId;
	if (!ACECellTransit::ResolveViewerCellId(
			*Dat, LastKnownCellId, PlayerUe, CamUe, WorldScale, Resolved))
	{
		Resolved = LastKnownCellId;
	}
	ViewerCellId = Resolved;

	{
		static uint32 LastLogViewer = 0;
		static uint32 LastLogPlayer = 0;
		if (ViewerCellId != LastKnownCellId
			&& (ViewerCellId != LastLogViewer || LastKnownCellId != LastLogPlayer))
		{
			LastLogViewer = ViewerCellId;
			LastLogPlayer = LastKnownCellId;
			UE_LOG(LogTemp, Log, TEXT("ACE: viewer_cell=0x%08X player=0x%08X"),
				ViewerCellId, LastKnownCellId);
		}
	}
}

void UACETerrainPresenterComponent::CommitOccupancyCellId(uint32 Candidate, bool bServerAuthoritative, bool bForce)
{
	if (Candidate == 0)
	{
		return;
	}
	bHasKnownCell = true;
	const bool bCandIndoor = IsIndoorCell(Candidate);
	const bool bCurIndoor = IsIndoorCell(LastKnownCellId);
	const double Now = FPlatformTime::Seconds();

	if (bForce)
	{
		OccupancyFlipUntil = 0.0;
		PendingOccupancyCellId = 0;
		LastKnownCellId = Candidate;
		if (!bCandIndoor)
		{
			LastOutdoorCellId = Candidate;
		}
		return;
	}

	if (Candidate == LastKnownCellId)
	{
		OccupancyFlipUntil = 0.0;
		PendingOccupancyCellId = 0;
		if (!bCandIndoor)
		{
			LastOutdoorCellId = Candidate;
		}
		return;
	}

	// Room-to-room or landcell: follow immediately. Occupancy *mode* (in/out) is what strobes.
	if (bCandIndoor == bCurIndoor || LastKnownCellId == 0)
	{
		OccupancyFlipUntil = 0.0;
		PendingOccupancyCellId = 0;
		LastKnownCellId = Candidate;
		if (!bCandIndoor)
		{
			LastOutdoorCellId = Candidate;
		}
		return;
	}

	// Server indoor CellId is the physics object's cell — commit now (retail CTransition).
	if (bServerAuthoritative && bCandIndoor)
	{
		OccupancyFlipUntil = 0.0;
		PendingOccupancyCellId = 0;
		LastKnownCellId = Candidate;
		UE_LOG(LogTemp, Warning, TEXT("ACE: occupancy indoor cell=0x%08X (server)"), Candidate);
		return;
	}

	if (PendingOccupancyCellId != Candidate)
	{
		PendingOccupancyCellId = Candidate;
		OccupancyFlipUntil = Now + 0.28;
		return;
	}
	if (Now < OccupancyFlipUntil)
	{
		return;
	}

	OccupancyFlipUntil = 0.0;
	PendingOccupancyCellId = 0;
	LastKnownCellId = Candidate;
	if (!bCandIndoor)
	{
		LastOutdoorCellId = Candidate;
	}
	UE_LOG(LogTemp, Warning, TEXT("ACE: occupancy %s cell=0x%08X"),
		bCandIndoor ? TEXT("indoor") : TEXT("outdoor"), Candidate);
}

bool UACETerrainPresenterComponent::IsWorldCellVisible(int32 CellId) const
{
	if (!IsIndoorCell(static_cast<uint32>(CellId))) return bShowOutdoorEntities;
	const auto* Cell = SpawnedEnvCells.FindRef(CellId).Get();
	return Cell && !Cell->IsHidden();
}

void UACETerrainPresenterComponent::UpdateCameraVisibility()
{
	ACE_PROFILE_SCOPE(Visibility);
	UWorld* World = GetWorld();
	UACEDatSubsystem* Dat = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat || !bHasKnownCell || Dat->IsInPortalSpace()) return;
	if (bWorldHiddenForPortal)
	{
		// Loading can finish every actor before portal exit. The deferred login
		// burst then has no new mesh or cell change to trigger a full refresh.
		// Component visibility below cannot clear an actor's hidden flag (which
		// also hides its buildings/scenery). Restore once on the first world frame.
		UpdateBuildingVisibility();
	}
	RefreshViewerCellId(Dat);
	const uint32 DrawCell = ResolveDrawOccupancyCellId(Dat);
	const bool bIndoor = IsIndoorCell(DrawCell);
	bool bOutside = !bIndoor;
	if (bIndoor)
	{
		FVector Eye;
		FConvexVolume Frustum;
		if (!ACEOutdoorPortalPlan::TryGetPlayerViewFrustum(World, Eye, Frustum)) return;
		TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Exits;
		ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat, DrawCell, Eye, Frustum,
			WorldScale, HeldIndoorVisible, bOutside, 128, &Exits);
		// PView clips landscape against actual exit polygons, never an inferred
		// building AABB (which also cut away courtyards and roads outside).
		Dat->SetLandInteriorClip(false, FVector::ZeroVector, FVector::ZeroVector);
		Dat->SetLandLookOutClip(true, Eye, Exits);
		for (const auto& Pair : SpawnedEnvCells)
		{
			if (!Pair.Value) continue;
			const bool bVisible = HeldIndoorVisible.Contains(Pair.Key);
			Pair.Value->SetEnvCellHiddenInGame(!bVisible);
			if (bVisible) Pair.Value->EnsureStaticObjectsQueued();
		}
		for (int32 Id : HeldIndoorVisible)
		{
			if (!SpawnedEnvCells.Contains(Id) && !FailedEnvCells.Contains(Id))
			{
				Dat->RequestEnvCellMesh(Id, WorldScale);
				bLastEnvSyncComplete = false;
			}
		}
	}
	else
	{
		HeldIndoorVisible.Reset();
		UpdateOutdoorEnvCellDraw();
	}
	for (const auto& Pair : Spawned)
		if (Pair.Value) Pair.Value->SetOutdoorTerrainHiddenInGame(bWcBakedTerrainMode || !bOutside);
	for (const auto& Pair : SpawnedChunks)
		if (Pair.Value) Pair.Value->SetOutdoorTerrainHiddenInGame(bWcBakedTerrainMode || !bOutside);
	bShowOutdoorEntities = bOutside;
	if (auto* Entities = GetOwner()->FindComponentByClass<UACEWorldPresenterComponent>())
		Entities->RefreshCellVisibility();
}

void UACETerrainPresenterComponent::UpdateBuildingVisibility()
{
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	RefreshViewerCellId(Dat);
	const uint32 DrawCell = ResolveDrawOccupancyCellId(Dat);
	const uint32 CollideCell = ResolveCollideOccupancyCellId(Dat);
	const uint32 VisCell = DrawCell;
	const bool bIndoorDraw = IsIndoorCell(DrawCell);
	const bool bIndoorCollide = IsIndoorCell(CollideCell);

	const bool bPortalSpace = Dat && Dat->IsInPortalSpace();

	FVector PlayerUe = FVector::ZeroVector;
	bool bHavePlayerUe = false;
	if (Client)
	{
		const FACEPosition Pos = GetPresentationPosition();
		if (Pos.IsValid())
		{
			PlayerUe = Pos.ToUnrealLocation(WorldScale);
			bHavePlayerUe = true;
		}
	}
	if (!bHavePlayerUe && World)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				PlayerUe = Pawn->GetActorLocation();
				bHavePlayerUe = true;
			}
		}
	}

	// Draw occupancy is viewer_cell (which indoor rooms). Residency (LScape / look-in
	// vs DrawInside) follows the pawn CellId — camera flicker at a doorway must not
	// hide the plaza or drop the interior.
	TSet<int32> VisibleIndoor;
	TSet<int32> CollideIndoor;
	TSet<int32> IndoorStatics;
	const FACEBuiltEnvCellMesh* Current = nullptr;
	bool bPViewLookOut = false;
	TArray<ACEOutdoorPortalPlan::FAdmittedAperture> VisibleOutsideApertures;
	if (bIndoorDraw && Dat && !bPortalSpace)
	{
		// SmartBox's viewer cell owns visibility; the pawn cell only owns physics.
		// Keep PVS residency in SyncIndoor/SyncOutdoor; it is not a draw list.
		VisibleIndoor.Add(static_cast<int32>(DrawCell));
		FVector CamPos = PlayerUe;
		FConvexVolume Frustum;
		if (ACEOutdoorPortalPlan::TryGetPlayerViewFrustum(World, CamPos, Frustum))
		{
			ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat, DrawCell, CamPos, Frustum,
				WorldScale, VisibleIndoor, bPViewLookOut, 128, &VisibleOutsideApertures);
		}
		IndoorStatics = VisibleIndoor;
		HeldIndoorVisible = VisibleIndoor;
		for (int32 Id : VisibleIndoor) Dat->RequestEnvCellMesh(static_cast<uint32>(Id), WorldScale);
	}
	else
	{
		HeldIndoorVisible.Reset();
	}
	if (bIndoorCollide && Dat)
	{
		Current = Dat->FindEnvCellMesh(CollideCell, WorldScale);
		if (!Current) Dat->RequestEnvCellMesh(CollideCell, WorldScale);
	}

	if (bIndoorCollide && Dat)
	{
		CollideIndoor.Add(static_cast<int32>(CollideCell));
		if (IsIndoorCell(LastKnownCellId))
		{
			CollideIndoor.Add(static_cast<int32>(LastKnownCellId));
		}
		// Occupied cell + CellPortal hops (not full VisibleCells). Neighboring PVS PhysicsBSP
		// overlaps doorways and lets you walk through solid walls. Extra hops cover stairs.
		AddIndoorPvsFromDat(Dat, CollideCell, WorldScale, CollideIndoor, nullptr,
			/*bIncludeVisibleCells*/ false);
		ExpandIndoorPvsHops(Dat, CollideCell, WorldScale, 3, CollideIndoor, nullptr, 20,
			/*bIncludeVisibleCells*/ false);
		if (bHavePlayerUe)
		{
			TArray<uint32> Transit;
			ACECellTransit::FindCellList(*Dat, CollideCell, PlayerUe, 180.f, WorldScale, Transit, nullptr);
			for (uint32 CellId : Transit)
			{
				if (ACECellTransit::IsIndoorCell(CellId))
				{
					CollideIndoor.Add(static_cast<int32>(CellId));
					if (!SpawnedEnvCells.Contains(static_cast<int32>(CellId)))
					{
						Dat->RequestEnvCellMesh(CellId, WorldScale);
					}
				}
			}
		}
		const double NowKeep = FPlatformTime::Seconds();
		if (CollideCell != LastIndoorCollideCell)
		{
			if (LastIndoorCollideCell != 0)
			{
				IndoorCollideKeepUntil = NowKeep + 0.45;
			}
			LastIndoorCollideCell = CollideCell;
		}
		if (NowKeep < IndoorCollideKeepUntil)
		{
			CollideIndoor.Append(HeldIndoorCollide);
		}
		HeldIndoorCollide = CollideIndoor;
		for (int32 NeighborId : CollideIndoor)
		{
			Dat->RequestEnvCellMesh(static_cast<uint32>(NeighborId), WorldScale);
		}
	}
	else
	{
		HeldIndoorCollide.Reset();
		IndoorCollideKeepUntil = 0.0;
		LastIndoorCollideCell = 0;
	}

	// Retail CTransition::build_cell_array/check_other_cells tests the transit
	// neighborhood, and CEnvCell::find_collisions tests its static objects too.
	// A destination can have no room triangles (0179010D); deferring its stabs
	// until visibility, or enabling only that cell, leaves placement without a floor.
	if (bPortalSpace)
	{
		bWorldHiddenForPortal = true;
		for (const auto& Pair : SpawnedEnvCells)
		{
			if (AACEEnvCellActor* Env = Pair.Value)
			{
				const bool bRequired = CollideIndoor.Contains(Pair.Key);
				Env->SetEnvCellCollisionActive(bRequired);
				if (bRequired) Env->EnsureStaticObjectsQueued();
				Env->SetEnvCellHiddenInGame(true);
			}
		}
		// SmartBox::Hide affects presentation, not destination collision preparation.
		for (const auto& Pair : Spawned)
			if (Pair.Value) Pair.Value->SetActorHiddenInGame(true);
		for (const auto& Pair : SpawnedChunks)
			if (Pair.Value) Pair.Value->SetActorHiddenInGame(true);
		ClearOutdoorPortalDepthApertures();
		return;
	}
	bWorldHiddenForPortal = false;

	// Unified residency (retail SmartBox — no land CSG punch):
	//   Outdoor: full unpunched LScape + Setup shells + StabList peeks
	//   Indoor: hide LScape (SeenOutside is NOT a land-draw gate — DAT sets it on back rooms).
	// Collision stays cell-partitioned (land on outdoors, land off indoors).
	// Retail CBldPortal: StabList = EnvCells visible through a doorway from outside (PVS).
	// Draw opens a portal only when side-test + frustum-clipped aperture is nonempty
	// (copied PView). Streaming still loads the full StabList; depth apertures write
	// doorway Z before EnvCell shells / building exteriors. Never GPU-clip EnvCell shells.
	TSet<int32> DoorwayEntries;
	TSet<int32> DoorwayPeek;
	TSet<int32> TransitOutdoorCollide;
	if (Dat)
	{
		auto AddDoorways = [&](int32 LbKey)
		{
			FACEDatLandblockInfo Info;
			if (!Dat->LoadLandblockInfo(static_cast<uint32>(LbKey) & 0xFFFF0000u, Info))
			{
				return;
			}
			const uint32 Key = static_cast<uint32>(LbKey) & 0xFFFF0000u;
			for (const FACEDatLandblockBuilding& Building : Info.Buildings)
			{
				for (const FACEDatBuildingPortal& Portal : Building.Portals)
				{
					if (Portal.OtherCellId >= 0x0100u && Portal.OtherCellId != 0xFFFFu)
					{
						const int32 EntryKey = static_cast<int32>(Key | Portal.OtherCellId);
						DoorwayEntries.Add(EntryKey);
						DoorwayPeek.Add(EntryKey);
					}
					for (uint16 StabCell : Portal.StabCells)
					{
						if (StabCell >= 0x0100u)
						{
							DoorwayPeek.Add(static_cast<int32>(Key | StabCell));
						}
					}
				}
			}
		};
		for (const auto& Pair : Spawned)
		{
			AddDoorways(Pair.Key);
		}
		for (const auto& Pair : SpawnedChunks)
		{
			if (Pair.Value)
			{
				for (const auto& Child : Pair.Value->GetChildLandblocks())
				{
					AddDoorways(Child.Key);
				}
			}
		}

		// P1 outdoor collide: sphere-tested entry cells from find_cell_list — never peek/StabList.
		if (!bIndoorCollide && bHavePlayerUe)
		{
			TArray<uint32> Transit;
			ACECellTransit::FindCellList(*Dat, LastKnownCellId, PlayerUe, 80.f, WorldScale, Transit, nullptr);
			for (uint32 CellId : Transit)
			{
				if (ACECellTransit::IsIndoorCell(CellId) && DoorwayEntries.Contains(static_cast<int32>(CellId)))
				{
					TransitOutdoorCollide.Add(static_cast<int32>(CellId));
				}
			}
		}
	}

	bool bCanSeeOutside = false;
	bool bDoorwayLookOut = false;
	if (bIndoorCollide && Dat)
	{
		if (Current)
		{
			bCanSeeOutside = Current->CanSeeOutside();
			bDoorwayLookOut = Current->HasOutsidePortal();
		}
		const int32 CurId = static_cast<int32>(CollideCell);
		if (const AACEEnvCellActor* Actor = SpawnedEnvCells.FindRef(CurId))
		{
			if (!bCanSeeOutside)
			{
				bCanSeeOutside = Actor->bSeenOutside;
			}
			if (!bDoorwayLookOut)
			{
				bDoorwayLookOut = Actor->bHasOutsidePortal;
			}
		}
		if (!bDoorwayLookOut && !Current)
		{
			FACEDatEnvCell DatCell;
			if (Dat->LoadEnvCell(CollideCell, DatCell))
			{
				bDoorwayLookOut = DatCell.HasOutsidePortal();
			}
		}
	}

	// Retail PView::DrawCells: LScape only when a 0xFFFF portal is in the clipped view.
	// Occupied doorway cells keep land (this cell's hole shows the street). PVS-wide
	// 0xFFFF / town-landblock unhid the plaza through back-room walls.
	bool bLookOutLand = !bIndoorDraw || bPViewLookOut;
	const uint32 PlayerLbKey = LastKnownCellId & 0xFFFF0000u;
	TArray<int32> HideBuildingIndices;
	bool bBuildingInterior = false;
	bool bTownLandblock = false;
	if (Dat)
	{
		FACEDatLandblockInfo LbInfo;
		bTownLandblock = Dat->LoadLandblockInfo(PlayerLbKey, LbInfo) && LbInfo.Buildings.Num() > 0;
		if (bIndoorCollide)
		{
			const int32 OccupiedBuilding=Dat->FindBuildingInfoIndexForIndoorCell(PlayerLbKey, CollideCell, WorldScale);
			if (OccupiedBuilding != INDEX_NONE) HideBuildingIndices.Add(OccupiedBuilding);
		}
		bBuildingInterior = HideBuildingIndices.Num() > 0;
	}
	// Hide LScape while the *pawn* is indoor (retail SmartBox: no unclipped land).
	// Viewer_cell must not own this — camera indoor at a threshold hid the plaza.
	bool bUnderTownLand = false;
	if (bHavePlayerUe && Dat)
	{
		float LandZ = 0.f;
		if (Dat->SampleOutdoorGroundZ(PlayerUe.X, PlayerUe.Y, WorldScale, LandZ))
		{
			// Cellars only — Yaraq shops sit 0.5–1.5 m below plaza; 80 cm trapped
			// occupancy indoor at every doorway (lag + rain PES never restarted).
			bUnderTownLand = bTownLandblock && PlayerUe.Z < (LandZ - 280.f);
		}
	}
	// Retail PView::DrawInside: LScape with PortalList = 0xFFFF clip views — land stays
	// submitted, fragments only survive through clipped doorway polygons. Hiding the actor left
	// a black void through shop doors. Unhiding the whole plaza painted grass as floors.
	FVector LookCam = bHavePlayerUe ? PlayerUe : FVector::ZeroVector;
	if (World)
	{
		FConvexVolume LookFrustum;
		ACEOutdoorPortalPlan::TryGetPlayerViewFrustum(World, LookCam, LookFrustum);
	}
	const bool bPortalClipLand = bIndoorDraw && VisibleOutsideApertures.Num() > 0;
	// Sealed dungeon: no 0xFFFF doorway → no LScape (retail outside_view.view_count==0).
	const bool bHideAllOutdoorTerrain = bIndoorDraw && !bPortalClipLand;
	bShowOutdoorEntities = !bHideAllOutdoorTerrain;
	bool bOccCollisionReady = !bIndoorCollide;
	if (bIndoorCollide)
	{
		if (AACEEnvCellActor* OccEnv = SpawnedEnvCells.FindRef(static_cast<int32>(CollideCell)))
		{
			// Retail indoor occupancy walks EnvCell PhysicsBSP (town shops and dungeons).
			OccEnv->SetEnvCellCollisionActive(true, false, /*bAllowAsync*/ false);
		}
		for (int32 CollideId : CollideIndoor)
		{
			if (AACEEnvCellActor* Env = SpawnedEnvCells.FindRef(CollideId))
			{
				const bool bCurrent = CollideId == static_cast<int32>(CollideCell);
				Env->SetEnvCellCollisionActive(true, false, /*bAllowAsync*/ !bCurrent);
				if (bCurrent && Env->IsCollisionCooked())
				{
					bOccCollisionReady = true;
				}
			}
		}
	}
	const bool bTerrainCollision = !bHideAllOutdoorTerrain;
	// Town indoor: never fall back to LScape — that dumps the pawn through the shop to the street.
	const bool bOutdoorTerrainCollide = (!bIndoorCollide && !bUnderTownLand)
		|| (!bOccCollisionReady && !bTownLandblock);
	// Outdoor shell pawn blocking is refreshed every tick in UpdateOutdoorEnvCollision
	// (occupancy does not change while walking up to a doorway).
	// Only the occupied building yields to its EnvCell BSP. Entering one shop
	// must not disable the physical walls of every other building in town.
	const bool bShellBlockPawn = true;
	if (Dat)
	{
		Dat->SetPortalLookOutLand(false);
		// Sealed indoor only. Town shops keep outdoor sun/CSM; land uses PortalList clip.
		Dat->SetIndoorEnvironment(bIndoorCollide && !bLookOutLand && !bPortalClipLand);
		Dat->SetInteriorUsesOutdoorAmbient(!bIndoorCollide || bCanSeeOutside);
		Dat->SetBuildingInteriorFill(bBuildingInterior || (!bIndoorCollide && DoorwayPeek.Num() > 0));
		// Outdoor draw below owns its entry/exit mask. Clearing it here would
		// upload an empty plane texture and then rebuild it again every refresh.
		if (bIndoorDraw) Dat->SetLandLookOutClip(true, LookCam, VisibleOutsideApertures);
	}
	{
		static uint32 LastLoggedCell = 0;
		static bool LastLoggedHide = false;
		static bool LastLoggedCanSee = false;
		static bool LastLoggedLookOut = false;
		if (DrawCell != LastLoggedCell || bHideAllOutdoorTerrain != LastLoggedHide
			|| bCanSeeOutside != LastLoggedCanSee || bLookOutLand != LastLoggedLookOut)
		{
			LastLoggedCell = DrawCell;
			LastLoggedHide = bHideAllOutdoorTerrain;
			LastLoggedCanSee = bCanSeeOutside;
			LastLoggedLookOut = bLookOutLand;
			UE_LOG(LogTemp, Verbose,
				TEXT("ACEResidency: cell=0x%08X indoorDraw=%d indoorCollide=%d canSeeOut=%d lookOut=%d hideLScape=%d landCollide=%d"),
				DrawCell, bIndoorDraw ? 1 : 0, bIndoorCollide ? 1 : 0, bCanSeeOutside ? 1 : 0, bLookOutLand ? 1 : 0,
				bHideAllOutdoorTerrain ? 1 : 0, bTerrainCollision ? 1 : 0);
		}
	}

	auto LandblockHasDoorwayPeek = [&DoorwayPeek](int32 LbKey) -> bool
	{
		const uint32 Key = static_cast<uint32>(LbKey) & 0xFFFF0000u;
		for (int32 PeekId : DoorwayPeek)
		{
			if ((static_cast<uint32>(PeekId) & 0xFFFF0000u) == Key)
			{
				return true;
			}
		}
		return false;
	};

	for (const auto& Pair : Spawned)
	{
		if (Pair.Value)
		{
			Pair.Value->SetActorHiddenInGame(false);
			Pair.Value->SetActorEnableCollision(true);
			Pair.Value->SetOutdoorTerrainCollisionEnabled(!bWcBakedTerrainMode && bOutdoorTerrainCollide);
			Pair.Value->SetOutdoorTerrainHiddenInGame(bWcBakedTerrainMode || bHideAllOutdoorTerrain);
			const bool bLbHasBuildings = [&]()
			{
				if (!Dat)
				{
					return false;
				}
				FACEDatLandblockInfo Info;
				return Dat->LoadLandblockInfo(static_cast<uint32>(Pair.Key) & 0xFFFF0000u, Info)
					&& Info.Buildings.Num() > 0;
			}();
			const bool bLbEnvFloorPriority = bLbHasBuildings
				|| LandblockHasDoorwayPeek(Pair.Key)
				|| (static_cast<uint32>(Pair.Key) == PlayerLbKey
					&& (bTownLandblock || bBuildingInterior));
			Pair.Value->SetLandEnvCellFloorPriority(bLbEnvFloorPriority || bIndoorCollide, Dat,
				/*bIndoorLookOut*/ true);
			// Never hide occupied Setup shells. Retail indoor look-out still draws them
			// (PORT omitted); hiding left a white void above the arch.
			Pair.Value->SetIndoorScenerySuppressed(false, TArray<int32>());
			Pair.Value->SetBuildingShellsBlockPawn(bShellBlockPawn,
				uint32(Pair.Key) == PlayerLbKey && bIndoorCollide ? HideBuildingIndices : TArray<int32>());
		}
	}
	for (const auto& Pair : SpawnedChunks)
	{
		if (Pair.Value)
		{
			Pair.Value->SetActorHiddenInGame(false);
			Pair.Value->SetActorEnableCollision(true);
			Pair.Value->SetOutdoorTerrainCollisionEnabled(!bWcBakedTerrainMode && bOutdoorTerrainCollide);
			Pair.Value->SetOutdoorTerrainHiddenInGame(bWcBakedTerrainMode || bHideAllOutdoorTerrain);
			const bool bChunkHasBuildings = [&]()
			{
				if (!Dat)
				{
					return false;
				}
				FACEDatLandblockInfo Info;
				return Dat->LoadLandblockInfo(static_cast<uint32>(Pair.Key) & 0xFFFF0000u, Info)
					&& Info.Buildings.Num() > 0;
			}();
			const bool bChunkEnvFloorPriority = bChunkHasBuildings
				|| LandblockHasDoorwayPeek(Pair.Key)
				|| Pair.Value->ContainsLandblock(static_cast<int32>(PlayerLbKey));
			Pair.Value->SetLandEnvCellFloorPriority(bChunkEnvFloorPriority || bIndoorCollide, Dat,
				/*bIndoorLookOut*/ true);
			Pair.Value->SetIndoorScenerySuppressed(false, TArray<int32>(), 0);
			Pair.Value->SetBuildingShellsBlockPawn(bShellBlockPawn,
				bIndoorCollide ? HideBuildingIndices : TArray<int32>(), PlayerLbKey);
		}
	}

	if (Dat) Dat->SetLandInteriorClip(false, FVector::ZeroVector, FVector::ZeroVector);

	UpdateSkyWeatherState();

	for (const auto& Pair : SpawnedEnvCells)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const bool bShow = bIndoorDraw && VisibleIndoor.Contains(Pair.Key);
		const bool bPawnCollide = bIndoorCollide && CollideIndoor.Contains(Pair.Key);
		if (!bIndoorDraw && !bIndoorCollide)
		{
			continue;
		}
		Pair.Value->SetEnvCellHiddenInGame(!bShow);
		const bool bCurrentCollide = Pair.Key == static_cast<int32>(CollideCell);
		Pair.Value->SetEnvCellCollisionActive(bPawnCollide, false, /*bAllowAsync*/ !bCurrentCollide);
		if (bPawnCollide || (bShow && IndoorStatics.Contains(Pair.Key)))
		{
			Pair.Value->EnsureStaticObjectsQueued();
		}
		// Authored EnvCell Z. Land PDO pushes LScape behind coplanar floors (look-in / look-out).
		// Do not lift cell meshes — that was not retail and broke doorway collision.
		Pair.Value->SetLookInDrawLift(false);
		Pair.Value->SetLookInDepthBias(0.f);
		// Indoor: keep authored ceilings. Outdoor peeks hide EnvCell lids so the Setup
		// roof wins — coplanar lids z-fought the circular Yaraq shop roof.
		Pair.Value->SetCeilingDrawSuppressed(false);
		Pair.Value->SetPortalStencilActive(false);
		if (!bShow || bIndoorCollide)
		{
			Pair.Value->SetSunCastShadow(false);
		}
	}
	// CSM refresh is on a 0.25s cadence in TickComponent (casters + entities).

	if (bIndoorDraw)
	{
		ClearOutdoorPortalDepthApertures();
		ApplyOutdoorDoorwayClipsFromApertures();
	}
	else
	{
		UpdateOutdoorEnvCellDraw();
	}
}

void UACETerrainPresenterComponent::UpdateOutdoorEnvCellDraw()
{
	// Some streaming callbacks run with the pawn outside and the camera inside.
	// Their outdoor refresh must not overwrite the camera's indoor PView draw set.
	if (IsIndoorCell(ViewerCellId != 0 ? ViewerCellId : LastKnownCellId)) return;
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat)
	{
		return;
	}

	FVector CamPos = FVector::ZeroVector;
	FConvexVolume Frustum;
	TSet<int32> Admitted;
	TArray<ACEOutdoorPortalPlan::FAdmittedAperture> LookInExits;
	LastOutdoorApertures.Reset();
	const uint32 PlayerLb = LastKnownCellId & 0xFFFF0000u;
	TArray<uint32>& AdmitKeys=OutdoorAdmitKeys;
	const int32 FullR=GetFullDetailRadius();
	if (OutdoorAdmitLandblock!=PlayerLb || OutdoorAdmitRadius!=FullR)
	{
		OutdoorAdmitLandblock=PlayerLb;OutdoorAdmitRadius=FullR;AdmitKeys.Reset();
		const int32 Cx = static_cast<int32>((PlayerLb >> 24) & 0xFF);
		const int32 Cy = static_cast<int32>((PlayerLb >> 16) & 0xFF);
		for (int32 DX = -FullR; DX <= FullR; ++DX)
		{
			for (int32 DY = -FullR; DY <= FullR; ++DY)
			{
				const int32 X = Cx + DX;
				const int32 Y = Cy + DY;
				if (X < 0 || X > 255 || Y < 0 || Y > 255)
				{
					continue;
				}
				AdmitKeys.Add((static_cast<uint32>(X) << 24) | (static_cast<uint32>(Y) << 16));
			}
		}
	}
	if (AdmitKeys.Num() == 0)
	{
		AdmitKeys.Add(PlayerLb);
	}
	if (ACEOutdoorPortalPlan::TryGetPlayerViewFrustum(World, CamPos, Frustum))
	{
		ACEOutdoorPortalPlan::CollectOutdoorAdmittedEnvCells(
			*Dat, CamPos, Frustum, WorldScale, AdmitKeys, Admitted, &LastOutdoorApertures, &LookInExits);
	}

	const double Now = FPlatformTime::Seconds();
	for (int32 Id : Admitted)
	{
		OutdoorPeekKeepUntil.Add(Id, Now + 2.0);
	}
	for (auto It = OutdoorPeekKeepUntil.CreateIterator(); It; ++It)
	{
		if (Now >= It.Value())
		{
			It.RemoveCurrent();
		}
	}

	for (const auto& Pair : SpawnedEnvCells)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const bool bShow = Admitted.Contains(Pair.Key);
		Pair.Value->SetEnvCellHiddenInGame(!bShow);
		if (bShow) Pair.Value->EnsureStaticObjectsQueued();
		Pair.Value->SetLookInDrawLift(false);
		// UE 5.8 PixelDepthOffset only pushes surfaces *away* (negative is clamped).
		// Land PDO (positive) puts LScape behind these floors; do not offset EnvCells.
		Pair.Value->SetLookInDepthBias(0.f);
		// Hide EnvCell lids from the street so the Setup roof wins (circular shop flicker).
		Pair.Value->SetCeilingDrawSuppressed(false);
		Pair.Value->SetPortalStencilActive(false);
		Pair.Value->SetSunCastShadow(false);
	}

	SyncOutdoorPortalDepthApertures(LastOutdoorApertures);
	ApplyOutdoorDoorwayClipsFromApertures();

    // Retail draws interior geometry over land through the clipped entry view.
    // Preserve terrain beyond visible exits, including open courtyards.
    Dat->SetLandInteriorClip(false,FVector::ZeroVector,FVector::ZeroVector);
    Dat->SetLandLookOutClip(LastOutdoorApertures.Num()>0,CamPos,LastOutdoorApertures,&LookInExits);

	{
		static int32 LastAdmitLog = -1;
		static int32 LastKeepLog = -1;
		const int32 KeepN = OutdoorPeekKeepUntil.Num();
		const int32 AdmitN = Admitted.Num();
		if (AdmitN != LastAdmitLog || KeepN != LastKeepLog)
		{
			LastAdmitLog = AdmitN;
			LastKeepLog = KeepN;
			UE_LOG(LogTemp, Verbose,
				TEXT("ACE: outdoor PView admit=%d peekKeep=%d spawnedEnv=%d"),
				AdmitN, KeepN, SpawnedEnvCells.Num());
		}
	}
}

void UACETerrainPresenterComponent::UpdateOutdoorEnvCollision()
{
	if (IsIndoorCell(LastKnownCellId))
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;

	FVector PlayerUe = FVector::ZeroVector;
	bool bHavePlayerUe = false;
	if (Client)
	{
		const FACEPosition Pos = GetPresentationPosition();
		if (Pos.IsValid())
		{
			PlayerUe = Pos.ToUnrealLocation(WorldScale);
			bHavePlayerUe = true;
		}
	}
	if (!bHavePlayerUe && World)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				PlayerUe = Pawn->GetActorLocation();
				bHavePlayerUe = true;
			}
		}
	}

	TSet<int32> OutdoorEnvCollide;
	if (bHavePlayerUe && Dat)
	{
		TArray<uint32> Transit;
		ACECellTransit::FindCellList(*Dat, LastKnownCellId, PlayerUe, 140.f, WorldScale, Transit, nullptr);
		for (uint32 CellId : Transit)
		{
			if (!ACECellTransit::IsIndoorCell(CellId))
			{
				continue;
			}
			const int32 Key = static_cast<int32>(CellId);
			// The DAT sphere/BSP test already establishes contact with this cell.
			// A fixed height above its floor cannot distinguish a roof from a tall
			// stairwell: it disabled the steps when approaching them from above.
			OutdoorEnvCollide.Add(Key);
		}

		for (const auto& Pair : SpawnedEnvCells)
		{
			if (!Pair.Value)
			{
				continue;
			}
			FBox Box;
			if (!TryGetEnvCellWorldBox(Dat, Pair.Value, Pair.Key, WorldScale, Box))
			{
				continue;
			}
			const bool bOverCell = PlayerUe.X >= Box.Min.X && PlayerUe.X <= Box.Max.X
				&& PlayerUe.Y >= Box.Min.Y && PlayerUe.Y <= Box.Max.Y;
			// Keep support near the actual top of the cell, including a landing
			// through a roof opening. Distant rooms on other storeys stay inactive.
			const bool bNearCellHeight = PlayerUe.Z >= Box.Min.Z - 140.f
				&& PlayerUe.Z <= Box.Max.Z + 140.f;
			if (bOverCell && bNearCellHeight)
			{
				OutdoorEnvCollide.Add(Pair.Key);
			}
		}

		const uint32 PlayerLb = LastKnownCellId & 0xFFFF0000u;
		TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Aps;
		ACEOutdoorPortalPlan::CollectLandblockDoorwayApertures(*Dat, PlayerLb, WorldScale, Aps);
		for (const ACEOutdoorPortalPlan::FAdmittedAperture& A : Aps)
		{
			if (A.WorldVerts.Num() < 3)
			{
				continue;
			}
			FVector Center = FVector::ZeroVector;
			for (const FVector& V : A.WorldVerts)
			{
				Center += V;
			}
			Center /= static_cast<float>(A.WorldVerts.Num());
			if (FVector::Dist2D(PlayerUe, Center) < 400.f
				&& FMath::Abs(PlayerUe.Z - Center.Z) < 200.f)
			{
				if (A.DestEnvCellId != 0)
				{
					OutdoorEnvCollide.Add(static_cast<int32>(A.DestEnvCellId));
				}
			}
		}
	}

	for (const auto& Pair : SpawnedEnvCells)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const bool bCollide = OutdoorEnvCollide.Contains(Pair.Key);
		if (bCollide)
		{
			Pair.Value->PrefetchCollisionCookAsync();
			// Furniture includes the stair mesh itself. Collision residency must
			// prepare it even when the entrance has not been visible yet.
			Pair.Value->EnsureStaticObjectsQueued();
		}
		Pair.Value->SetEnvCellCollisionActive(bCollide, false, /*bAllowAsync*/ false);
	}

	for (const auto& Pair : Spawned)
	{
		if (Pair.Value)
		{
			Pair.Value->SetBuildingShellsBlockPawn(true);
		}
	}
	for (const auto& Pair : SpawnedChunks)
	{
		if (Pair.Value)
		{
			Pair.Value->SetBuildingShellsBlockPawn(true);
		}
	}
}

void UACETerrainPresenterComponent::DisableAllRuntimeShadowCasters()
{
	for (const auto& Pair : Spawned)
	{
		if (Pair.Value)
		{
			Pair.Value->ForceDisableShadowCasting();
		}
	}
	for (const auto& Pair : SpawnedChunks)
	{
		if (!Pair.Value)
		{
			continue;
		}
		for (const auto& Child : Pair.Value->GetChildLandblocks())
		{
			if (Child.Value)
			{
				Child.Value->ForceDisableShadowCasting();
			}
		}
	}
	for (const auto& Pair : SpawnedEnvCells)
	{
		if (AACEEnvCellActor* Actor = Pair.Value.Get())
		{
			Actor->SetSunCastShadow(false);
		}
	}
}

void UACETerrainPresenterComponent::RefreshOutdoorShadowCasters(const FVector& CameraWorld)
{
	// Never ForceDisable the whole ring here. Indoor occupancy used to wipe every
	// caster (and recache CSM) on the 0.25s tick — enter/exit hitch, then no shadows.
	// ApplySceneryShadowWinners already turns off losers.

	constexpr float MaxDistCm = 120000.f;
	constexpr float StickyDistCm = 8000.f;
	constexpr int32 MaxWinners = 36;

	TArray<FACESceneryShadowCand> Cands;
	for (const auto& Pair : Spawned)
	{
		if (Pair.Value)
		{
			Pair.Value->CollectSceneryShadowCandidates(CameraWorld, MaxDistCm, Cands);
		}
	}
	for (const auto& Pair : SpawnedChunks)
	{
		if (!Pair.Value)
		{
			continue;
		}
		for (const auto& Child : Pair.Value->GetChildLandblocks())
		{
			if (Child.Value)
			{
				Child.Value->CollectSceneryShadowCandidates(CameraWorld, MaxDistCm, Cands);
			}
		}
	}

	Cands.Sort([](const FACESceneryShadowCand& A, const FACESceneryShadowCand& B)
	{
		return A.Kind != B.Kind ? A.Kind < B.Kind : A.DistCm < B.DistCm;
	});

	TArray<UProceduralMeshComponent*> WinProcs;
	TArray<AACERegionSceneryActor*> WinAnim;
	for (int32 i = 0; i < FMath::Min(Cands.Num(), MaxWinners); ++i)
	{
		if (Cands[i].Proc)
		{
			WinProcs.Add(Cands[i].Proc);
		}
		else if (Cands[i].Animated)
		{
			WinAnim.Add(Cands[i].Animated);
		}
	}

	for (const auto& Pair : Spawned)
	{
		if (Pair.Value)
		{
			Pair.Value->ApplySceneryShadowWinners(WinProcs, WinAnim, CameraWorld, MaxDistCm, StickyDistCm);
		}
	}
	for (const auto& Pair : SpawnedChunks)
	{
		if (!Pair.Value)
		{
			continue;
		}
		for (const auto& Child : Pair.Value->GetChildLandblocks())
		{
			if (Child.Value)
			{
				Child.Value->ApplySceneryShadowWinners(WinProcs, WinAnim, CameraWorld, MaxDistCm, StickyDistCm);
			}
		}
	}
}

void UACETerrainPresenterComponent::RefreshEntityShadowCasters(const FVector& CameraWorld)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	constexpr float MaxDistCm = 120000.f;
	for (TActorIterator<AACEWorldEntityActor> It(World); It; ++It)
	{
		AACEWorldEntityActor* Entity = *It;
		if (!IsValid(Entity) || Entity->IsHidden() || !Entity->Appearance
			|| !Entity->Appearance->HasAppearance())
		{
			continue;
		}
		const bool bCreatureLike = Entity->bIsPlayer
			|| (Entity->ItemType & ACEItemType::Creature) != 0;
		float Dist = FVector::Dist(Entity->GetActorLocation(), CameraWorld);
		FBox VisualBox(ForceInit);
		if (Entity->Appearance->GetVisualWorldBounds(VisualBox) && VisualBox.IsValid)
		{
			Dist = FVector::Dist(VisualBox.GetCenter(), CameraWorld);
		}
		if (Dist > MaxDistCm)
		{
			if (bCreatureLike)
			{
				Entity->Appearance->SetPartsCastShadow(false, /*bInset*/ true);
			}
			continue;
		}
		if (bCreatureLike)
		{
			// The cell transition controls visibility and lighting, not whether a
			// character can cast onto an admitted interior floor or doorway receiver.
			Entity->Appearance->SetPartsCastShadow(true, /*bInset*/ false);
		}
		else if (!Entity->Appearance->GetPartsCastShadow())
		{
			// Lifestones, pools, chests, portal frames — keep shadows on nearby props.
			Entity->Appearance->SetPartsCastShadow(true, /*bInset*/ false);
		}
	}

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UACECharacterAppearanceComponent* App = Pawn->FindComponentByClass<UACECharacterAppearanceComponent>())
			{
				if (App->HasAppearance())
				{
					App->SetPartsCastShadow(true, /*bInset*/ false);
				}
			}
		}
	}
}

void UACETerrainPresenterComponent::SyncEnvCells()
{
	UWorld* World = GetWorld();
	if (!World || !EnvCellClass)
	{
		return;
	}
	// Indoor dungeons / marketplace (LB 0x016C has Buildings=0) must spawn EnvCells from
	// CellId origin even when outdoor landblock actors were cleared (mesh-format reload).
	// Gating on Spawned landblocks left walls in cache but never as actors → gray void.
	if (Spawned.Num() == 0 && SpawnedChunks.Num() == 0 && !IsIndoorCell(LastKnownCellId))
	{
		return;
	}

	TArray<int32> Ordered;
	TSet<int32> Needed;
	CollectNeededEnvCells(Ordered, Needed);

	// Do not wipe spawned rooms when the camera looks away — that popped interiors
	// and rebuilt them on the next look. Evict only dead landblocks / over-cap below.

	// Indoor PVS is per held CellId. Outdoor key is landblock + keep-set.
	const uint32 EnvRingKey = IsIndoorCell(LastKnownCellId)
		? LastKnownCellId
		: ((LastKnownCellId & 0xFFFF0000u) ^ 0x0u);
	uint32 NeededHash = static_cast<uint32>(Needed.Num());
	for (int32 Id : Needed)
	{
		NeededHash ^= static_cast<uint32>(Id);
		NeededHash = HashCombine(NeededHash, GetTypeHash(Id));
	}
	const uint32 SyncKey = HashCombine(GetTypeHash(EnvRingKey), NeededHash);
	if (SyncKey == LastEnvSyncKey && bLastEnvSyncComplete)
	{
		// Recovery: peeks wiped after complete latched (mesh-format reload, empty Needed race).
		bool bMissingNeeded = false;
		for (int32 Id : Needed)
		{
			if (!SpawnedEnvCells.Contains(Id) && !FailedEnvCells.Contains(Id))
			{
				bMissingNeeded = true;
				break;
			}
		}
		if (!bMissingNeeded)
		{
			return;
		}
		bLastEnvSyncComplete = false;
	}

	auto LandblockAlive = [&](uint32 CellId) -> bool
	{
		const int32 LbKey = static_cast<int32>(CellId & 0xFFFF0000u);
		return IsLandblockKept(static_cast<uint32>(LbKey))
			|| Spawned.Contains(LbKey)
			|| SpawnedChunks.Contains(LbKey);
	};

	FVector EvictFrom = FVector::ZeroVector;
	bool bHaveEvictFrom = false;
	if (Client)
	{
		const FACEPosition Pos = Client->GetPlayerPosition();
		if (Pos.IsValid())
		{
			EvictFrom = Pos.ToUnrealLocation(WorldScale);
			bHaveEvictFrom = true;
		}
	}

	TArray<int32> ToRemove;
	TArray<TPair<float, int32>> EvictRanked;
	for (const auto& Pair : SpawnedEnvCells)
	{
		if (Needed.Contains(Pair.Key))
		{
			continue;
		}
		if (!LandblockAlive(static_cast<uint32>(Pair.Key)))
		{
			ToRemove.Add(Pair.Key);
			continue;
		}
		float DistSq = 0.f;
		if (bHaveEvictFrom)
		{
			const uint32 U = static_cast<uint32>(Pair.Key);
			const uint32 Lbx = (U >> 24) & 0xFF;
			const uint32 Lby = (U >> 16) & 0xFF;
			FVector Origin = FACEPosition::AceVectorToUnreal(
				FVector(Lbx * 192.f, Lby * 192.f, 0.f), WorldScale);
			if (Pair.Value)
			{
				Origin = Pair.Value->GetActorLocation();
			}
			DistSq = static_cast<float>(FVector::DistSquared(EvictFrom, Origin));
		}
		EvictRanked.Emplace(DistSq, Pair.Key);
	}
	constexpr int32 MaxKeptEnvCells = 384;
	const int32 KeepCount = Needed.Num() + EvictRanked.Num();
	if (KeepCount > MaxKeptEnvCells && EvictRanked.Num() > 0)
	{
		EvictRanked.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
		{
			return A.Key > B.Key;
		});
		int32 Extra = KeepCount - MaxKeptEnvCells;
		for (int32 i = 0; i < EvictRanked.Num() && Extra > 0; ++i, --Extra)
		{
			ToRemove.Add(EvictRanked[i].Value);
		}
	}
	for (int32 Id : ToRemove)
	{
		if (AACEEnvCellActor* A = SpawnedEnvCells.FindRef(Id))
		{
			UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter unload envcell 0x%08X"), Id);
			A->Destroy();
		}
		SpawnedEnvCells.Remove(Id);
		FailedEnvCells.Remove(Id);
	}

	UGameInstance* GI = World->GetGameInstance();
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (Dat)
	{
		TSet<int32> KeepMeshes = Needed;
		for (const auto& Pair : SpawnedEnvCells)
		{
			if (!ToRemove.Contains(Pair.Key))
			{
				KeepMeshes.Add(Pair.Key);
			}
		}
		Dat->CancelEnvCellMeshRequestsOutside(KeepMeshes);
		// Transition reset already removed the old actors, so ToRemove can be empty
		// even while their CPU meshes are still resident. Retire against the plan.
		Dat->EvictEnvCellMeshesOutside(KeepMeshes, WorldScale);
	}
	const bool bDatReady = Dat && Dat->IsDatReady();
	const uint32 StreamNow = EnvStreamCellId != 0 ? EnvStreamCellId : LastKnownCellId;
	const bool bIndoorNow = IsIndoorCell(StreamNow);

	// Indoor: occupancy first, then ready neighbors within the frame time budget.
	// Outdoor: drain landblock stablists (retail grab_visible) + nearby VisibleCells.
	int32 MaxPerCall = FMath::Max(1, EnvCellsPerTick);
	if (LandblockLoginBurstTicks > 0 && !bIndoorNow)
	{
		MaxPerCall = FMath::Max(MaxPerCall, 20);
	}
	if (bIndoorNow && SpawnedEnvCells.Contains(static_cast<int32>(LastKnownCellId)))
	{
		MaxPerCall = FMath::Max(4, EnvCellsPerTick);
	}
	const double BudgetSec = Dat && Dat->IsInPortalSpace() ? .002 : FMath::Max(0.5f, bIndoorNow
		? FMath::Max(EnvCellTimeBudgetMs, 3.f)
		: (LandblockLoginBurstTicks > 0 ? 8.f : FMath::Max(EnvCellTimeBudgetMs, 3.f))) * 0.001;
	const double StartSec = FPlatformTime::Seconds();
	int32 LoadedThisCall = 0;
	int32 PendingCount = 0;

	if (Dat)
	{
		const int32 Prefetch = (LandblockLoginBurstTicks > 0 && !bIndoorNow) ? 32 : 16;
		int32 Prefetched = 0;
		for (int32 Pi = 0; Pi < Ordered.Num() && Prefetched < Prefetch; ++Pi)
		{
			const int32 Id = Ordered[Pi];
			if (SpawnedEnvCells.Contains(Id) || FailedEnvCells.Contains(Id))
			{
				continue;
			}
			const uint32 Uid = static_cast<uint32>(Id);
			if (!Dat->FindEnvCellMesh(Uid, WorldScale))
			{
				Dat->RequestEnvCellMesh(Uid, WorldScale);
			}
			++Prefetched;
		}
	}

	// Always try the player's current cell first (Ordered[0] when indoors) before neighbors.
	for (int32 Id : Ordered)
	{
		if (SpawnedEnvCells.Contains(Id) || FailedEnvCells.Contains(Id))
		{
			continue;
		}
		const bool bIsCurrent = (static_cast<uint32>(Id) == LastKnownCellId)
			|| (static_cast<uint32>(Id) == StreamNow);
		const bool bBudgetExhausted = LoadedThisCall > 0
			&& (FPlatformTime::Seconds() - StartSec) >= BudgetSec;
		if ((LoadedThisCall >= MaxPerCall || bBudgetExhausted) && !bIsCurrent)
		{
			++PendingCount;
			continue;
		}

		const uint32 LandblockKey = static_cast<uint32>(Id) & 0xFFFF0000u;
		const uint32 Lbx = (LandblockKey >> 24) & 0xFF;
		const uint32 Lby = (LandblockKey >> 16) & 0xFF;
		const FVector LandblockOrigin = FACEPosition::AceVectorToUnreal(FVector(Lbx * 192.f, Lby * 192.f, 0.f), WorldScale);

		if (!Dat)
		{
			++PendingCount;
			continue;
		}

		// Prefer cache (possibly just sync-built). Fall back to async request.
		UACEDatSubsystem::EACEEnvCellMeshStatus MeshStatus = UACEDatSubsystem::EACEEnvCellMeshStatus::NotReady;
		if (Dat->FindEnvCellMesh(static_cast<uint32>(Id), WorldScale))
		{
			MeshStatus = UACEDatSubsystem::EACEEnvCellMeshStatus::Ready;
		}
		else
		{
			MeshStatus = Dat->RequestEnvCellMesh(static_cast<uint32>(Id), WorldScale);
		}
		if (MeshStatus == UACEDatSubsystem::EACEEnvCellMeshStatus::Pending
			|| MeshStatus == UACEDatSubsystem::EACEEnvCellMeshStatus::NotReady)
		{
			++PendingCount;
			continue;
		}
		if (MeshStatus == UACEDatSubsystem::EACEEnvCellMeshStatus::Failed)
		{
			if (bDatReady)
			{
				FailedEnvCells.Add(Id);
				UE_LOG(LogTemp, Warning, TEXT("ACE: TerrainPresenter failed envcell 0x%08X — skipping"), Id);
			}
			else
			{
				++PendingCount;
			}
			continue;
		}

		if (const FACEBuiltEnvCellMesh* Built = Dat->FindEnvCellMesh(static_cast<uint32>(Id), WorldScale))
		{
			// Portal connectors with stabs still need an actor for balcony/roof props.
			if (Built->StaticObjects.Num() > 0)
			{
				// keep
			}
			else if (Built->IsEmpty() && !Built->bPortalConnector)
			{
				// Empty transit — no actor. Treat as skipped so sync can latch complete.
				FailedEnvCells.Add(Id);
				continue;
			}
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AACEEnvCellActor* Actor = World->SpawnActor<AACEEnvCellActor>(EnvCellClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Actor && Actor->LoadEnvCell(Id, LandblockOrigin, WorldScale))
		{
			SpawnedEnvCells.Add(Id, Actor);
			++LoadedThisCall;
			UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter load envcell 0x%08X"), Id);
		}
		else
		{
			if (Actor)
			{
				Actor->Destroy();
			}
			// Ready mesh that still fails apply (rare) — skip rather than starve the PVS queue.
			if (bDatReady && MeshStatus == UACEDatSubsystem::EACEEnvCellMeshStatus::Ready)
			{
				FailedEnvCells.Add(Id);
				UE_LOG(LogTemp, Warning, TEXT("ACE: TerrainPresenter failed apply envcell 0x%08X — skipping"), Id);
			}
			else
			{
				++PendingCount;
				UE_LOG(LogTemp, Verbose, TEXT("ACE: TerrainPresenter defer envcell 0x%08X (apply pending)"), Id);
			}
		}
	}

	if (PendingCount > 0)
	{
		bLastEnvSyncComplete = false;
		LastEnvSyncKey = SyncKey;
	}
	else
	{
		LastEnvSyncKey = SyncKey;
		const bool bWasComplete = bLastEnvSyncComplete;
		bLastEnvSyncComplete = true;
		if (!bWasComplete)
		{
			UE_LOG(LogTemp, Log, TEXT("ACE: TerrainPresenter SyncEnvCells complete — %d loaded, %d skipped (needed=%d)"),
				SpawnedEnvCells.Num(), FailedEnvCells.Num(), Needed.Num());
		}
		if (Needed.Num() > 80)
		{
			UE_LOG(LogTemp, Verbose, TEXT("ACE: EnvCell connected set=%d (stream=0x%08X)"),
				Needed.Num(), StreamNow);
		}
	}

	if (IsIndoorCell(LastKnownCellId) && !bLastEnvSyncComplete)
	{
		static double LastEnvProgressLog = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastEnvProgressLog > 2.0)
		{
			LastEnvProgressLog = Now;
			UE_LOG(LogTemp, Warning,
				TEXT("ACE: indoor EnvCell stream cell=0x%08X needed=%d spawned=%d failed=%d pending=%d"),
				LastKnownCellId, Needed.Num(), SpawnedEnvCells.Num(), FailedEnvCells.Num(), PendingCount);
		}
	}

	if (LoadedThisCall > 0)
	{
		if (IsIndoorCell(LastKnownCellId))
		{
			UpdateBuildingVisibility();
		}
		else
		{
			UpdateOutdoorEnvCellDraw();
		}
	}
}
