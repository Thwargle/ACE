#pragma once

#include "CoreMinimal.h"

class UACEDatSubsystem;
class UWorld;
struct FCollisionQueryParams;

/**
 * Retail-style cell-partitioned collision candidates (ObjCell::find_cell_list /
 * find_transit_cells / BuildingObj transit) for client pawn sweeps and CellId prediction.
 *
 * This is CTransition-lite: it does not simulate full ACE Physics transitions, but it
 * produces the same candidate cell set that gates which EnvCells / outdoor land should
 * participate in UE sweeps.
 */
namespace ACECellTransit
{
	/** Restrict room BSP queries without discarding static objects crossing cells. */
	void RestrictRoomCollision(UWorld& World, const TSet<uint32>& Cells, FCollisionQueryParams& Params);
	inline bool IsIndoorCell(uint32 CellId)
	{
		return (CellId & 0xFFFFu) >= 0x0100u;
	}

	/** Retail SmartBox::update_viewer sphere (0.3 AC). */
	inline float RetailViewerSphereRadiusCm(float WorldScale)
	{
		return 0.3f * WorldScale;
	}

	/** Capsule-bound radius for find_cell_list / transit (matches movement sweeps). */
	inline float ComputeCapsuleTransitRadiusCm(
		float CapsuleRadiusCm, float CapsuleHalfHeightCm, float ExtraCm = 8.f)
	{
		const float Bound = FMath::Sqrt(
			FMath::Square(CapsuleRadiusCm) + FMath::Square(CapsuleHalfHeightCm));
		return FMath::Max(ExtraCm, Bound);
	}

	inline uint32 LandblockKey(uint32 CellId)
	{
		return CellId & 0xFFFF0000u;
	}

	/** Unreal cm → ACE global XYZ (RH, matches FACEPosition). */
	FVector UnrealToAceGlobal(const FVector& UnrealPos, float WorldScale);

	/**
	 * EnvCell.CellStructure.sphere_intersects_cell — CellBSP walk with radius.
	 * Returns true when the sphere is EntirelyInside or PartiallyInside (not Outside).
	 */
	bool SphereIntersectsEnvCell(
		UACEDatSubsystem& Dat,
		uint32 EnvCellId,
		const FVector& UnrealCenter,
		float RadiusCm,
		float WorldScale);

	/**
	 * LandCell.add_all_outside_cells — seed outdoor landcells whose 24×24 ACE squares
	 * the sphere overlaps (plus Moore neighbors at edges).
	 */
	void AddOutdoorLandNeighbors(
		uint32 HintOrAdjustedCellId,
		const FVector& AceCenter,
		float AceRadius,
		TArray<uint32>& InOutLandCellIds);

	/**
	 * ObjCell.find_cell_list for one sphere. OutCellIds mixes outdoor landcells and EnvCells.
	 * bOutHitsInterior is set when any indoor cell is in the set (BuildingObj / EnvCell transit).
	 */
	void FindCellList(
		UACEDatSubsystem& Dat,
		uint32 HintCellId,
		const FVector& UnrealSphereCenter,
		float SphereRadiusCm,
		float WorldScale,
		TArray<uint32>& OutCellIds,
		bool* bOutHitsInterior = nullptr);

	/**
	 * CTransition-lite CellId after a move: prefer point_in_cell among transit EnvCells,
	 * then outdoor exit via SeenOutside / outside portals, else outdoor landcell from XY.
	 */
	bool ResolveTransitCellId(
		UACEDatSubsystem& Dat,
		uint32 HintCellId,
		const FVector& UnrealWorldPos,
		float CapsuleRadiusCm,
		float WorldScale,
		uint32& OutCellId);

	/**
	 * Sample CTransition along Start→End so CellId cannot tunnel through a wall in one step.
	 * Each sample uses ResolveTransitCellId (pawn occupancy — outdoor XY exit is allowed).
	 */
	bool ResolvePathCellId(
		UACEDatSubsystem& Dat,
		uint32 HintCellId,
		const FVector& UnrealStart,
		const FVector& UnrealEnd,
		float CapsuleRadiusCm,
		float WorldScale,
		uint32& OutCellId);

	/**
	 * SmartBox::update_viewer analogue. Walk a 0.3 AC sphere from the player cell toward
	 * the camera. Blocked samples keep the last valid cell — never OutdoorLandCellFromAce
	 * through a wall (that was the shop-floor LScape leak). Outdoor only when the sample
	 * is already outdoor or on the outdoor half-space of a 0xFFFF portal.
	 */
	bool ResolveViewerCellId(
		UACEDatSubsystem& Dat,
		uint32 PlayerCellId,
		const FVector& PlayerUnrealPos,
		const FVector& CameraUnrealPos,
		float WorldScale,
		uint32& OutCellId);
}
