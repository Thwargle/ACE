#include "Dat/ACECellTransit.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "ACETypes.h"
#include "ACEEnvCellActor.h"
#include "ProceduralMeshComponent.h"
#include "EngineUtils.h"

namespace ACECellTransit
{
	void RestrictRoomCollision(UWorld& World, const TSet<uint32>& Cells, FCollisionQueryParams& Params)
	{
		for (TActorIterator<AACEEnvCellActor> It(&World); It; ++It)
			if (It->EnvCellId != 0 && !Cells.Contains(uint32(It->EnvCellId)))
			{
				// CPhysicsObj::calc_cross_cells_static registers furniture with
				// every overlapping cell. Its owning EnvCell is not its bounds.
				Params.AddIgnoredComponent(It->CellCollisionMesh.Get());
				Params.AddIgnoredComponent(It->CellMesh.Get());
			}
	}
	namespace
	{
		constexpr float LandCellSize = 24.f;
		constexpr float LandblockSize = FACELandblockMeshBuilder::LandblockSize; // 192
		constexpr int32 MaxTransitCells = 64;
		constexpr int32 MaxGrowPasses = 8;

		uint32 MakeLandCellId(int32 BlockX, int32 BlockY, int32 CellX, int32 CellY)
		{
			BlockX = FMath::Clamp(BlockX, 0, 255);
			BlockY = FMath::Clamp(BlockY, 0, 255);
			CellX = FMath::Clamp(CellX, 0, 7);
			CellY = FMath::Clamp(CellY, 0, 7);
			const uint32 Lb = (static_cast<uint32>(BlockX) << 24) | (static_cast<uint32>(BlockY) << 16);
			const uint32 ShortId = static_cast<uint32>(CellX * 8 + CellY) + 1u; // 1..64
			return Lb | ShortId;
		}

		void AddUnique(TArray<uint32>& Cells, TSet<uint32>& Seen, uint32 CellId)
		{
			if (CellId == 0 || Seen.Contains(CellId) || Cells.Num() >= MaxTransitCells)
			{
				return;
			}
			Seen.Add(CellId);
			Cells.Add(CellId);
		}

		bool SphereIntersectsCellBsp(
			const TArray<FACEDatCellBspNode>& Nodes,
			const FVector& AceLocal,
			float AceRadius)
		{
			if (Nodes.Num() == 0)
			{
				return false;
			}
			const float CheckRad = AceRadius + 0.01f;
			int32 Idx = 0;
			constexpr int32 MaxWalk = 4096;
			for (int32 Step = 0; Step < MaxWalk; ++Step)
			{
				if (!Nodes.IsValidIndex(Idx))
				{
					return false;
				}
				const FACEDatCellBspNode& Node = Nodes[Idx];
				if (Node.bLeaf)
				{
					return true; // EntirelyInside / PartiallyInside leaf
				}
				const float Dist = Node.Plane.X * static_cast<float>(AceLocal.X)
					+ Node.Plane.Y * static_cast<float>(AceLocal.Y)
					+ Node.Plane.Z * static_cast<float>(AceLocal.Z)
					+ Node.Plane.W;
				if (Dist <= -CheckRad)
				{
					return false; // Outside
				}
				if (Dist >= CheckRad)
				{
					if (Node.PosChild == INDEX_NONE)
					{
						return true;
					}
					Idx = Node.PosChild;
					continue;
				}
				// Straddling plane — PosChild partially-inside test (retail ignores NegChild).
				if (Node.PosChild == INDEX_NONE)
				{
					return true;
				}
				Idx = Node.PosChild;
			}
			return false;
		}

		bool PointInEnvCellAabbFallback(const FACEBuiltEnvCellMesh& Mesh, const FVector& LocalUE, float PadCm)
		{
			if (!Mesh.bHasLocalBounds)
			{
				return false;
			}
			const FVector Pad(PadCm, PadCm, PadCm);
			return FBox(Mesh.LocalBoundsMin - Pad, Mesh.LocalBoundsMax + Pad).IsInsideOrOn(LocalUE);
		}

		FVector EnvCellLocalUnreal(
			const FACEBuiltEnvCellMesh& Mesh,
			uint32 EnvCellId,
			const FVector& UnrealWorldPos,
			float WorldScale)
		{
			const uint32 Lbx = (EnvCellId >> 24) & 0xFF;
			const uint32 Lby = (EnvCellId >> 16) & 0xFF;
			const FVector LandblockOrigin = FACEPosition::AceVectorToUnreal(
				FVector(Lbx * LandblockSize, Lby * LandblockSize, 0.f), WorldScale);
			// UE FTransform: A*B applies A first, then B. Cell frame first, then the
			// landblock translation (matches AACEEnvCellActor component placement).
			const FTransform CellToWorld = Mesh.GetCellLocalToLandblock(WorldScale)
				* FTransform(FQuat::Identity, LandblockOrigin);
			return CellToWorld.InverseTransformPosition(UnrealWorldPos);
		}

		FVector LocalUeToAce(const FVector& LocalUE, float WorldScale)
		{
			const float InvScale = (WorldScale > KINDA_SMALL_NUMBER) ? (1.f / WorldScale) : 1.f;
			return FVector(-LocalUE.X * InvScale, LocalUE.Y * InvScale, LocalUE.Z * InvScale);
		}

		void FindBuildingTransit(
			UACEDatSubsystem& Dat,
			uint32 LandCellId,
			const FVector& UnrealCenter,
			float RadiusCm,
			float WorldScale,
			TArray<uint32>& Cells,
			TSet<uint32>& Seen,
			bool& bHitsInterior)
		{
			FACEDatLandblockInfo Info;
			const uint32 Lb = LandblockKey(LandCellId);
			if (!Dat.LoadLandblockInfo(Lb, Info))
			{
				return;
			}
			for (const FACEDatLandblockBuilding& Building : Info.Buildings)
			{
				for (const FACEDatBuildingPortal& Portal : Building.Portals)
				{
					if (Portal.OtherCellId < 0x0100u || Portal.OtherCellId == 0xFFFFu)
					{
						continue;
					}
					const uint32 EntryId = Lb | Portal.OtherCellId;
					// Sphere-tested (retail check_building_transit). Footprint-under admit
					// while indoor added every shop entry on the landblock and pinched the
					// capsule in stairwells (overlapping PhysicsBSP).
					if (SphereIntersectsEnvCell(Dat, EntryId, UnrealCenter, RadiusCm, WorldScale))
					{
						AddUnique(Cells, Seen, EntryId);
						bHitsInterior = true;
					}
				}
			}
		}

		void FindEnvCellTransit(
			UACEDatSubsystem& Dat,
			uint32 EnvCellId,
			const FVector& UnrealCenter,
			float RadiusCm,
			float WorldScale,
			const FVector& AceCenter,
			float AceRadius,
			TArray<uint32>& Cells,
			TSet<uint32>& Seen,
			bool& bHitsInterior)
		{
			const FACEBuiltEnvCellMesh* Mesh = Dat.FindEnvCellMesh(EnvCellId, WorldScale);
			if (!Mesh)
			{
				Dat.RequestEnvCellMesh(EnvCellId, WorldScale);
				return;
			}
			const uint32 Lb = LandblockKey(EnvCellId);
			bool bCheckOutside = false;

			for (const FACEDatCellPortal& Portal : Mesh->CellPortals)
			{
				if (Portal.IsOutsidePortal())
				{
					// Without portal planes, approximate: if sphere still intersects this cell
					// while SeenOutside / we have an outside portal, allow outdoor seed.
					if (SphereIntersectsEnvCell(Dat, EnvCellId, UnrealCenter, RadiusCm, WorldScale)
						|| Mesh->SeesOutside())
					{
						bCheckOutside = true;
					}
					continue;
				}

				const uint32 OtherId = Lb | Portal.OtherCellId;
				if (SphereIntersectsEnvCell(Dat, OtherId, UnrealCenter, RadiusCm, WorldScale))
				{
					AddUnique(Cells, Seen, OtherId);
					bHitsInterior = true;
				}
				else if (!Dat.FindEnvCellMesh(OtherId, WorldScale))
				{
					// Unloaded neighbor: add when sphere still intersects this cell (portal approach).
					if (SphereIntersectsEnvCell(Dat, EnvCellId, UnrealCenter, RadiusCm * 1.25f, WorldScale))
					{
						AddUnique(Cells, Seen, OtherId);
						bHitsInterior = true;
					}
				}
			}

			if (bCheckOutside)
			{
				AddOutdoorLandNeighbors(EnvCellId, AceCenter, AceRadius, Cells);
				for (uint32 LandId : Cells)
				{
					if (!IsIndoorCell(LandId))
					{
						Seen.Add(LandId);
					}
				}
			}
		}

		uint32 OutdoorLandCellFromAce(const FVector& AceCenter)
		{
			const int32 BlockX = FMath::FloorToInt(AceCenter.X / LandblockSize);
			const int32 BlockY = FMath::FloorToInt(AceCenter.Y / LandblockSize);
			const float LocalX = AceCenter.X - static_cast<float>(BlockX) * LandblockSize;
			const float LocalY = AceCenter.Y - static_cast<float>(BlockY) * LandblockSize;
			const int32 CellX = FMath::Clamp(FMath::FloorToInt(LocalX / LandCellSize), 0, 7);
			const int32 CellY = FMath::Clamp(FMath::FloorToInt(LocalY / LandCellSize), 0, 7);
			return MakeLandCellId(BlockX, BlockY, CellX, CellY);
		}

		/** True when the sample is on the outdoor half-space of a 0xFFFF portal (not a side wall). */
		bool SampleOnOutdoorPortalSide(
			const FACEBuiltEnvCellMesh& Mesh,
			uint32 EnvCellId,
			const FVector& UnrealPos,
			float WorldScale)
		{
			const FVector LocalUE = EnvCellLocalUnreal(Mesh, EnvCellId, UnrealPos, WorldScale);
			for (int32 i = 0; i < Mesh.CellPortals.Num(); ++i)
			{
				if (!Mesh.CellPortals[i].IsOutsidePortal())
				{
					continue;
				}
				if (!Mesh.PortalApertureLocalNormals.IsValidIndex(i))
				{
					continue;
				}
				const FVector N = Mesh.PortalApertureLocalNormals[i];
				const float D = Mesh.PortalApertureLocalD.IsValidIndex(i)
					? Mesh.PortalApertureLocalD[i] : 0.f;
				if ((N | LocalUE) + D > 0.f)
				{
					return true;
				}
			}
			for (const FVector4f& Plane : Mesh.OutsidePortalPlanes)
			{
				if (Plane.X * LocalUE.X + Plane.Y * LocalUE.Y + Plane.Z * LocalUE.Z + Plane.W > 0.f)
				{
					return true;
				}
			}
			return false;
		}
	} // namespace

	FVector UnrealToAceGlobal(const FVector& UnrealPos, float WorldScale)
	{
		const float InvScale = (WorldScale > KINDA_SMALL_NUMBER) ? (1.f / WorldScale) : 1.f;
		return FVector(-UnrealPos.X * InvScale, UnrealPos.Y * InvScale, UnrealPos.Z * InvScale);
	}

	bool SphereIntersectsEnvCell(
		UACEDatSubsystem& Dat,
		uint32 EnvCellId,
		const FVector& UnrealCenter,
		float RadiusCm,
		float WorldScale)
	{
		if (!IsIndoorCell(EnvCellId))
		{
			return false;
		}
		const FACEBuiltEnvCellMesh* Mesh = Dat.FindEnvCellMesh(EnvCellId, WorldScale);
		if (!Mesh)
		{
			Dat.RequestEnvCellMesh(EnvCellId, WorldScale);
			return false;
		}

		const FVector LocalUE = EnvCellLocalUnreal(*Mesh, EnvCellId, UnrealCenter, WorldScale);
		const float AceRadius = (WorldScale > KINDA_SMALL_NUMBER) ? (RadiusCm / WorldScale) : RadiusCm;

		if (Mesh->CellBspNodes.Num() > 0)
		{
			return SphereIntersectsCellBsp(Mesh->CellBspNodes, LocalUeToAce(LocalUE, WorldScale), AceRadius);
		}

		const float Pad = FMath::Max(RadiusCm, 5.f);
		return PointInEnvCellAabbFallback(*Mesh, LocalUE, Pad);
	}

	void AddOutdoorLandNeighbors(
		uint32 HintOrAdjustedCellId,
		const FVector& AceCenter,
		float AceRadius,
		TArray<uint32>& InOutLandCellIds)
	{
		TSet<uint32> Seen;
		for (uint32 Existing : InOutLandCellIds)
		{
			Seen.Add(Existing);
		}

		auto AddLand = [&](int32 Bx, int32 By, int32 Cx, int32 Cy)
		{
			AddUnique(InOutLandCellIds, Seen, MakeLandCellId(Bx, By, Cx, Cy));
		};

		// Sphere may span landblock edges — walk a small ACE AABB in landcell units.
		const float MinX = AceCenter.X - AceRadius;
		const float MaxX = AceCenter.X + AceRadius;
		const float MinY = AceCenter.Y - AceRadius;
		const float MaxY = AceCenter.Y + AceRadius;

		const int32 MinBX = FMath::FloorToInt(MinX / LandblockSize);
		const int32 MaxBX = FMath::FloorToInt(MaxX / LandblockSize);
		const int32 MinBY = FMath::FloorToInt(MinY / LandblockSize);
		const int32 MaxBY = FMath::FloorToInt(MaxY / LandblockSize);

		bool bAdded = false;
		for (int32 Bx = MinBX; Bx <= MaxBX; ++Bx)
		{
			for (int32 By = MinBY; By <= MaxBY; ++By)
			{
				if (Bx < 0 || Bx > 255 || By < 0 || By > 255)
				{
					continue;
				}
				const float OriginX = static_cast<float>(Bx) * LandblockSize;
				const float OriginY = static_cast<float>(By) * LandblockSize;
				const int32 MinCX = FMath::Clamp(FMath::FloorToInt((MinX - OriginX) / LandCellSize), 0, 7);
				const int32 MaxCX = FMath::Clamp(FMath::FloorToInt((MaxX - OriginX) / LandCellSize), 0, 7);
				const int32 MinCY = FMath::Clamp(FMath::FloorToInt((MinY - OriginY) / LandCellSize), 0, 7);
				const int32 MaxCY = FMath::Clamp(FMath::FloorToInt((MaxY - OriginY) / LandCellSize), 0, 7);
				for (int32 Cx = MinCX; Cx <= MaxCX; ++Cx)
				{
					for (int32 Cy = MinCY; Cy <= MaxCY; ++Cy)
					{
						AddLand(Bx, By, Cx, Cy);
						bAdded = true;
					}
				}
			}
		}

		if (!bAdded)
		{
			// Fall back to hint outdoor / outdoor-from-hint landblock.
			if (!IsIndoorCell(HintOrAdjustedCellId))
			{
				AddUnique(InOutLandCellIds, Seen, HintOrAdjustedCellId);
			}
			else
			{
				AddUnique(InOutLandCellIds, Seen, LandblockKey(HintOrAdjustedCellId) | 0x1u);
			}
		}
	}

	void FindCellList(
		UACEDatSubsystem& Dat,
		uint32 HintCellId,
		const FVector& UnrealSphereCenter,
		float SphereRadiusCm,
		float WorldScale,
		TArray<uint32>& OutCellIds,
		bool* bOutHitsInterior)
	{
		OutCellIds.Reset();
		bool bHitsInterior = false;
		TSet<uint32> Seen;

		const FVector AceCenter = UnrealToAceGlobal(UnrealSphereCenter, WorldScale);
		const float AceRadius = (WorldScale > KINDA_SMALL_NUMBER)
			? (SphereRadiusCm / WorldScale) : SphereRadiusCm;

		if (IsIndoorCell(HintCellId))
		{
			AddUnique(OutCellIds, Seen, HintCellId);
			bHitsInterior = true;
		}
		else
		{
			AddOutdoorLandNeighbors(HintCellId, AceCenter, AceRadius, OutCellIds);
			for (uint32 Id : OutCellIds)
			{
				Seen.Add(Id);
			}
		}

		for (int32 Pass = 0; Pass < MaxGrowPasses; ++Pass)
		{
			const int32 CountAtPassStart = OutCellIds.Num();
			for (int32 i = 0; i < CountAtPassStart && OutCellIds.Num() < MaxTransitCells; ++i)
			{
				const uint32 CellId = OutCellIds[i];
				if (IsIndoorCell(CellId))
				{
					FindEnvCellTransit(Dat, CellId, UnrealSphereCenter, SphereRadiusCm, WorldScale,
						AceCenter, AceRadius, OutCellIds, Seen, bHitsInterior);
				}
				else
				{
					FindBuildingTransit(Dat, CellId, UnrealSphereCenter, SphereRadiusCm, WorldScale,
						OutCellIds, Seen, bHitsInterior);
				}
			}
			if (OutCellIds.Num() == CountAtPassStart)
			{
				break;
			}
		}

		if (bOutHitsInterior)
		{
			*bOutHitsInterior = bHitsInterior;
		}
	}

	bool ResolveTransitCellId(
		UACEDatSubsystem& Dat,
		uint32 HintCellId,
		const FVector& UnrealWorldPos,
		float CapsuleRadiusCm,
		float WorldScale,
		uint32& OutCellId)
	{
		OutCellId = HintCellId;
		TArray<uint32> Transit;
		bool bHitsInterior = false;
		FindCellList(Dat, HintCellId, UnrealWorldPos, CapsuleRadiusCm, WorldScale, Transit, &bHitsInterior);

		// Prefer EnvCell that contains the point among transit candidates.
		for (uint32 CellId : Transit)
		{
			if (!IsIndoorCell(CellId))
			{
				continue;
			}
			if (Dat.IsPointInsideEnvCell(CellId, UnrealWorldPos, WorldScale))
			{
				OutCellId = CellId;
				return true;
			}
		}

		if (IsIndoorCell(HintCellId))
		{
			bool bAllowOutdoorExit = false;
			if (const FACEBuiltEnvCellMesh* Cur = Dat.FindEnvCellMesh(HintCellId, WorldScale))
			{
				// SeenOutside OR outside portal (0xFFFF). Do not clear outside-portal exit
				// when SeenOutside is false — that stranded doorway cells on BSP miss.
				bAllowOutdoorExit = Cur->CanSeeOutside();
				if (!bAllowOutdoorExit)
				{
					// Marketplace / closed dungeon: stay indoor; prefer a containing neighbor.
					for (uint16 Vc : Cur->VisibleCells)
					{
						const uint32 Neighbor = LandblockKey(HintCellId) | Vc;
						if (Dat.IsPointInsideEnvCell(Neighbor, UnrealWorldPos, WorldScale))
						{
							OutCellId = Neighbor;
							return true;
						}
					}
				}
			}

			if (bAllowOutdoorExit && !Dat.IsPointInsideEnvCell(HintCellId, UnrealWorldPos, WorldScale))
			{
				const FVector Ace = UnrealToAceGlobal(UnrealWorldPos, WorldScale);
				OutCellId = OutdoorLandCellFromAce(Ace);
				return true;
			}

			// Stay indoor on ambiguous BSP miss (keeps EnvCell PVS loaded).
			OutCellId = HintCellId;
			return true;
		}

		// Outdoor: if transit hit an entry cell and point is inside it, flip indoor.
		if (bHitsInterior)
		{
			for (uint32 CellId : Transit)
			{
				if (IsIndoorCell(CellId)
					&& Dat.IsPointInsideEnvCell(CellId, UnrealWorldPos, WorldScale))
				{
					OutCellId = CellId;
					return true;
				}
			}
		}

		const FVector Ace = UnrealToAceGlobal(UnrealWorldPos, WorldScale);
		OutCellId = OutdoorLandCellFromAce(Ace);
		return true;
	}

	bool ResolvePathCellId(
		UACEDatSubsystem& Dat,
		uint32 HintCellId,
		const FVector& UnrealStart,
		const FVector& UnrealEnd,
		float CapsuleRadiusCm,
		float WorldScale,
		uint32& OutCellId)
	{
		OutCellId = HintCellId;
		uint32 Cell = HintCellId;
		const FVector Delta = UnrealEnd - UnrealStart;
		const float Dist = Delta.Size();
		const float StepCm = FMath::Max(CapsuleRadiusCm, 24.f);
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dist / StepCm), 1, 12);
		for (int32 i = 1; i <= Steps; ++i)
		{
			const FVector P = UnrealStart + Delta * (static_cast<float>(i) / static_cast<float>(Steps));
			uint32 Next = Cell;
			if (!ResolveTransitCellId(Dat, Cell, P, CapsuleRadiusCm, WorldScale, Next))
			{
				break;
			}
			Cell = Next;
		}
		OutCellId = Cell;
		return true;
	}

	bool ResolveViewerCellId(
		UACEDatSubsystem& Dat,
		uint32 PlayerCellId,
		const FVector& PlayerUnrealPos,
		const FVector& CameraUnrealPos,
		float WorldScale,
		uint32& OutCellId)
	{
		OutCellId = PlayerCellId;
		if (PlayerCellId == 0)
		{
			return false;
		}

		// Retail SmartBox::update_viewer sphere radius is 0.3 AC.
		const float RadiusCm = RetailViewerSphereRadiusCm(WorldScale);
		// Unreal's spring arm has already resolved the actual camera position.
		// Classify that point first. Sweeping a second camera from the player's
		// feet can stop against a stair riser/floor and leave PView in a room that
		// does not contain the eye. Its backface tests then reject visible portals.
		if (IsIndoorCell(PlayerCellId))
		{
			if (Dat.IsPointInsideEnvCell(PlayerCellId, CameraUnrealPos, WorldScale)) return true;
			if (const auto* Mesh = Dat.FindEnvCellMesh(PlayerCellId, WorldScale))
			{
				TArray<uint32, TInlineAllocator<64>> Candidates;
				for (uint16 Id : Mesh->VisibleCells) Candidates.AddUnique(LandblockKey(PlayerCellId) | Id);
				for (const auto& Portal : Mesh->CellPortals)
					if (!Portal.IsOutsidePortal()) Candidates.AddUnique(LandblockKey(PlayerCellId) | Portal.OtherCellId);
				for (uint32 Id : Candidates)
				{
					if (IsIndoorCell(Id) && Dat.IsPointInsideEnvCell(Id, CameraUnrealPos, WorldScale))
					{
						OutCellId = Id;
						return true;
					}
				}
			}
		}
		const FVector Delta = CameraUnrealPos - PlayerUnrealPos;
		const float Dist = Delta.Size();
		const float StepCm = FMath::Max(RadiusCm, 16.f);
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dist / StepCm), 1, 24);

		uint32 Cell = PlayerCellId;
		for (int32 i = 1; i <= Steps; ++i)
		{
			const FVector P = PlayerUnrealPos + Delta * (static_cast<float>(i) / static_cast<float>(Steps));
			uint32 Next = 0;

			TArray<uint32> Transit;
			FindCellList(Dat, Cell, P, RadiusCm, WorldScale, Transit, nullptr);

			for (uint32 Id : Transit)
			{
				if (IsIndoorCell(Id) && Dat.IsPointInsideEnvCell(Id, P, WorldScale))
				{
					Next = Id;
					break;
				}
			}

			if (Next == 0 && IsIndoorCell(Cell) && Dat.IsPointInsideEnvCell(Cell, P, WorldScale))
			{
				Next = Cell;
			}

			if (Next == 0 && !IsIndoorCell(Cell))
			{
				const FVector Ace = UnrealToAceGlobal(P, WorldScale);
				uint32 Land = OutdoorLandCellFromAce(Ace);
				for (uint32 Id : Transit)
				{
					if (!IsIndoorCell(Id))
					{
						Land = Id;
						break;
					}
				}
				Next = Land;
			}

			if (Next == 0 && IsIndoorCell(Cell))
			{
				const FACEBuiltEnvCellMesh* Mesh = Dat.FindEnvCellMesh(Cell, WorldScale);
				if (!Mesh)
				{
					Dat.RequestEnvCellMesh(Cell, WorldScale);
				}
				else if (Mesh->HasOutsidePortal()
					&& !Dat.IsPointInsideEnvCell(Cell, P, WorldScale)
					&& SampleOnOutdoorPortalSide(*Mesh, Cell, P, WorldScale))
				{
					const FVector Ace = UnrealToAceGlobal(P, WorldScale);
					Next = OutdoorLandCellFromAce(Ace);
				}
			}

			if (Next == 0)
			{
				// Path blocked — camera clipped through a wall. Keep last valid cell.
				break;
			}
			Cell = Next;
		}

		// The feet-to-eye walk only discovers candidate rooms; a riser or wall
		// along it is not a collision for the already-resolved spring-arm eye.
		// Never feed PView the last intermediate room when the eye left its BSP.
		if (IsIndoorCell(Cell) && !Dat.IsPointInsideEnvCell(Cell, CameraUnrealPos, WorldScale))
		{
			if (const auto* Mesh = Dat.FindEnvCellMesh(Cell, WorldScale))
			{
				TArray<uint32, TInlineAllocator<64>> Candidates;
				for (uint16 Id : Mesh->VisibleCells) Candidates.AddUnique(LandblockKey(Cell) | Id);
				for (const auto& Portal : Mesh->CellPortals)
					if (!Portal.IsOutsidePortal()) Candidates.AddUnique(LandblockKey(Cell) | Portal.OtherCellId);
				for (uint32 Id : Candidates)
				{
					if (IsIndoorCell(Id) && Dat.IsPointInsideEnvCell(Id, CameraUnrealPos, WorldScale))
					{
						OutCellId = Id;
						return true;
					}
				}
				if (Mesh->CanSeeOutside() || !IsIndoorCell(PlayerCellId))
					Cell = OutdoorLandCellFromAce(UnrealToAceGlobal(CameraUnrealPos, WorldScale));
			}
		}

		OutCellId = Cell;
		return true;
	}
}
