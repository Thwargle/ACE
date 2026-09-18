#pragma once

#include "CoreMinimal.h"
#include "ConvexVolume.h"

class UACEDatSubsystem;
class UWorld;

/**
 * Outdoor→indoor portal admission (retail building aperture → copied PView).
 *
 * Each doorway starts a view narrowed by its actual aperture. Connected cells
 * inherit that view, while DAT residency lists remain separate from draw admission.
 * Per-fragment cell clipping is still handled separately by the renderer.
 */
namespace ACEOutdoorPortalPlan
{
	/** One admitted outdoor doorway aperture in world space (for depth-only pass). */
	struct FAdmittedAperture
	{
		uint32 DestEnvCellId = 0;
		uint16 OtherPortalId = 0;
		TArray<FVector> WorldVerts;
		FVector WorldNormal = FVector::ZeroVector;
	};

	/** Camera eye + view frustum from the local player's projection data. */
	bool TryGetPlayerViewFrustum(UWorld* World, FVector& OutCameraWorldPos, FConvexVolume& OutFrustum);

	/**
	 * Collect EnvCell ids visible through building portals whose reverse aperture faces the
	 * camera and clips to a nonempty polygon against the view frustum.
	 * Connected cells must also survive the inherited aperture view.
	 * Also fills OutApertures for depth-only doorway meshes (never EnvCell fragment clips).
	 */
	void CollectOutdoorAdmittedEnvCells(
		UACEDatSubsystem& Dat,
		const FVector& CameraWorldPos,
		const FConvexVolume& ViewFrustum,
		float WorldScale,
		const TArrayView<const uint32> LandblockKeys,
		TSet<int32>& OutAdmittedEnvCells,
		TArray<FAdmittedAperture>* OutApertures = nullptr,
		TArray<FAdmittedAperture>* OutExitApertures = nullptr);

	/**
	 * Every building-portal doorway aperture on a landblock in world space (no frustum
	 * test). Shader clips / PORT holes are occupancy-independent — look-in from the
	 * street and look-out from inside both need the same slabs.
	 */
	void CollectLandblockDoorwayApertures(
		UACEDatSubsystem& Dat,
		uint32 LandblockKey,
		float WorldScale,
		TArray<FAdmittedAperture>& OutApertures);

	/**
	 * Indoor PView::DrawInside analogue: traverse each path with its inherited aperture
	 * frustum. Visible 0xFFFF portals set bOutLookOutLand (retail
	 * outside_view → clipped LScape through the doorway, not unhiding the whole plaza).
	 */
	void CollectIndoorPViewCells(
		UACEDatSubsystem& Dat,
		uint32 ViewerEnvCellId,
		const FVector& CameraWorldPos,
		const FConvexVolume& ViewFrustum,
		float WorldScale,
		TSet<int32>& OutDrawCells,
		bool& bOutLookOutLand,
		int32 MaxCells = 48, TArray<FAdmittedAperture>* OutOutsideApertures = nullptr);

	/** Sutherland–Hodgman clip of a convex polygon against one plane (keep N·P+D >= 0). */
	bool ClipPolygonAgainstPlane(
		const TArray<FVector>& InVerts,
		const FVector& PlaneN,
		double PlaneD,
		TArray<FVector>& OutVerts);

	/** Clip against all frustum planes; returns false if result has fewer than 3 verts. */
	bool ClipPolygonAgainstFrustum(
		const TArray<FVector>& InVerts,
		const FConvexVolume& Frustum,
		TArray<FVector>& OutVerts);

	/** Restrict a view to rays passing through an already clipped convex aperture. */
	FConvexVolume MakePortalFrustum(const FVector& CameraWorldPos,
		const TArray<FVector>& ClippedAperture, const FConvexVolume& ParentFrustum);
}
