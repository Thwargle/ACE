#include "Dat/ACEOutdoorPortalPlan.h"

#include "ACEDatSubsystem.h"
#include "ACETypes.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "Engine/Engine.h"
#include "StereoRendering.h"
#include "VR/ACEVRComponent.h"
#include "Camera/CameraComponent.h"

namespace ACEOutdoorPortalPlan
{
	// PView apertures describe visibility from the eye. A raster near plane is
	// allowed to cut wall triangles, but must not discard an entire adjacent room
	// when the camera is closer to its doorway than GNearClippingPlane.
	static FConvexVolume MakeCameraPortalFrustum(const FVector& Eye, const FConvexVolume& RenderFrustum)
	{
		FConvexVolume Result = RenderFrustum;
		for (FPlane& Plane : Result.Planes)
		{
			if (Plane.PlaneDot(Eye) > 0.1)
				Plane.W = FVector::DotProduct(FVector(Plane.X, Plane.Y, Plane.Z), Eye);
		}
		Result.Init();
		return Result;
	}

	bool TryGetPlayerViewFrustum(UWorld* World, FVector& OutCameraWorldPos, FConvexVolume& OutFrustum)
	{
		OutFrustum.Planes.Reset();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return false;
		}

		FRotator CamRot;
		PC->GetPlayerViewPoint(OutCameraWorldPos, CamRot);
		if (auto* VR = PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UACEVRComponent>() : nullptr;
			VR && VR->IsActive() && VR->Head)
		{
			// Residency/PView must cover both eyes, not the desktop mirror's FOV.
			OutCameraWorldPos = VR->Head->GetComponentLocation();
			const FQuat Rotation = VR->Head->GetComponentQuat();
			float Horizontal = 1.f, Vertical = 1.f;
			if (GEngine && GEngine->StereoRenderingDevice.IsValid())
				for (int32 Eye = 0; Eye < 2; ++Eye)
				{
					const FMatrix Projection = GEngine->StereoRenderingDevice->GetStereoProjectionMatrix(Eye);
					Horizontal = FMath::Max(Horizontal, float((1. + FMath::Abs(Projection.M[2][0])) / FMath::Max(.01, FMath::Abs(Projection.M[0][0]))));
					Vertical = FMath::Max(Vertical, float((1. + FMath::Abs(Projection.M[2][1])) / FMath::Max(.01, FMath::Abs(Projection.M[1][1]))));
				}
			// Leave angular room for render-thread head tracking and physical eye
			// separation. The GPU still clips against each eye's true projection.
			Horizontal = FMath::Tan(FMath::Min(1.48f, FMath::Atan(Horizontal) + .18f));
			Vertical = FMath::Tan(FMath::Min(1.48f, FMath::Atan(Vertical) + .18f));
			for (const FVector& Local : {FVector(-Horizontal,1,0), FVector(-Horizontal,-1,0),
				FVector(-Vertical,0,1), FVector(-Vertical,0,-1), FVector(-1,0,0)})
			{
				const FVector N = Rotation.RotateVector(Local.GetSafeNormal());
				OutFrustum.Planes.Add(FPlane(OutCameraWorldPos + N * 8.f, N));
			}
			OutFrustum.Init();
			return true;
		}

		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (LP->ViewportClient && LP->ViewportClient->Viewport)
			{
				FSceneViewProjectionData ProjData;
				if (LP->GetProjectionData(LP->ViewportClient->Viewport, ProjData))
				{
					OutCameraWorldPos = ProjData.ViewOrigin;
					GetViewFrustumBounds(OutFrustum, ProjData.ComputeViewProjectionMatrix(), true);
					return OutFrustum.Planes.Num() >= 4;
				}
			}
		}

		// Fallback when viewport projection is unavailable (PIE startup / no LP).
		float FovDeg = 90.f;
		if (PC->PlayerCameraManager)
		{
			FovDeg = PC->PlayerCameraManager->GetFOVAngle();
		}
		constexpr float Aspect = 16.f / 9.f;
		const float HalfFov = FMath::DegreesToRadians(FovDeg * 0.5f);
		const FMatrix ViewRotationMatrix = FInverseRotationMatrix(CamRot) * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		const FMatrix ViewMatrix = FTranslationMatrix(-OutCameraWorldPos) * ViewRotationMatrix;
		const FMatrix ProjMatrix = FReversedZPerspectiveMatrix(
			HalfFov, Aspect, 1.f, GNearClippingPlane, 100000.f);
		GetViewFrustumBounds(OutFrustum, ViewMatrix * ProjMatrix, true);
		return OutFrustum.Planes.Num() >= 4;
	}

	bool ClipPolygonAgainstPlane(
		const TArray<FVector>& InVerts,
		const FVector& PlaneN,
		double PlaneD,
		TArray<FVector>& OutVerts)
	{
		OutVerts.Reset();
		const int32 N = InVerts.Num();
		if (N < 3 || PlaneN.IsNearlyZero())
		{
			return false;
		}

		auto Dist = [&](const FVector& P) -> double
		{
			return FVector::DotProduct(PlaneN, P) + PlaneD;
		};

		for (int32 i = 0; i < N; ++i)
		{
			const FVector& A = InVerts[i];
			const FVector& B = InVerts[(i + 1) % N];
			const double Da = Dist(A);
			const double Db = Dist(B);
			const bool bAIn = Da >= -KINDA_SMALL_NUMBER;
			const bool bBIn = Db >= -KINDA_SMALL_NUMBER;
			if (bAIn && bBIn)
			{
				OutVerts.Add(B);
			}
			else if (bAIn && !bBIn)
			{
				const double T = Da / (Da - Db);
				OutVerts.Add(FMath::Lerp(A, B, T));
			}
			else if (!bAIn && bBIn)
			{
				const double T = Da / (Da - Db);
				OutVerts.Add(FMath::Lerp(A, B, T));
				OutVerts.Add(B);
			}
		}
		return OutVerts.Num() >= 3;
	}

	bool ClipPolygonAgainstFrustum(
		const TArray<FVector>& InVerts,
		const FConvexVolume& Frustum,
		TArray<FVector>& OutVerts)
	{
		TArray<FVector> Cur = InVerts;
		TArray<FVector> Next;
		for (const FPlane& Plane : Frustum.Planes)
		{
			// FConvexVolume planes point OUT: inside is PlaneDot <= 0. Our
			// polygon clipper keeps N·P+D >= 0, so negate the entire plane.
			const FVector N(-Plane.X, -Plane.Y, -Plane.Z);
			const double D = Plane.W;
			if (!ClipPolygonAgainstPlane(Cur, N, D, Next))
			{
				OutVerts.Reset();
				return false;
			}
			Cur = MoveTemp(Next);
			Next.Reset();
		}
		OutVerts = MoveTemp(Cur);
		return OutVerts.Num() >= 3;
	}

	FConvexVolume MakePortalFrustum(const FVector& CameraWorldPos,
		const TArray<FVector>& ClippedAperture, const FConvexVolume& ParentFrustum)
	{
		FConvexVolume Result = ParentFrustum;
		if (ClippedAperture.Num() < 3) return Result;
		FVector Center = FVector::ZeroVector;
		for (const FVector& V : ClippedAperture) Center += V;
		Center /= ClippedAperture.Num();
		for (int32 I = 0; I < ClippedAperture.Num(); ++I)
		{
			const FVector A = ClippedAperture[I] - CameraWorldPos;
			const FVector B = ClippedAperture[(I + 1) % ClippedAperture.Num()] - CameraWorldPos;
			FVector N = FVector::CrossProduct(A, B).GetSafeNormal();
			if (N.IsNearlyZero()) continue;
			if (FVector::DotProduct(N, Center - CameraWorldPos) > 0) N = -N;
			Result.Planes.Add(FPlane(CameraWorldPos, N));
		}
		Result.Init();
		return Result;
	}

	void CollectLandblockDoorwayApertures(UACEDatSubsystem& Dat, uint32 LandblockKey,
		float WorldScale, TArray<FAdmittedAperture>& OutApertures)
	{
		OutApertures.Reset();
		const uint32 Key=LandblockKey & 0xFFFF0000u;
		FACEDatLandblockInfo Info;
		if (!Dat.LoadLandblockInfo(Key,Info)) return;
		const FVector Origin=FACEPosition::AceVectorToUnreal(
			FVector(((Key>>24)&255)*192.f,((Key>>16)&255)*192.f,0),WorldScale);
		for (const auto& Building : Info.Buildings)
		{
			const auto* Setup=Dat.FindSetupMesh(Building.ModelId,WorldScale);
			if (!Setup) { Dat.RequestSetupMesh(Building.ModelId,WorldScale); continue; }
			const FTransform Frame(FACEPosition::AceQuatToUnreal(FQuat(Building.Orientation)),
				Origin+FACEPosition::AceVectorToUnreal(FVector(Building.Origin),WorldScale));
			for (const auto& Part : Setup->Parts) for (const auto& Poly : Part.Portals)
			{
				if (!Building.Portals.IsValidIndex(Poly.PortalIndex) || Poly.Vertices.Num()<3) continue;
				const auto& Portal=Building.Portals[Poly.PortalIndex];
				if (Portal.OtherCellId<0x100 || Portal.OtherCellId==0xFFFF) continue;
				const FTransform Transform=Part.BindTransform*Frame;
				FAdmittedAperture& A=OutApertures.AddDefaulted_GetRef();
				A.DestEnvCellId=Key|Portal.OtherCellId; A.OtherPortalId=Portal.OtherPortalId;
				// PView::ConstructView(CBldPortal*, CPolygon*): use the building's
				// PORT polygon and CBldPortal side, not the reverse interior polygon.
				A.WorldNormal=Transform.TransformVectorNoScale(Poly.Normal).GetSafeNormal()
					*(Portal.IsPortalSide() ? -1.f : 1.f);
				for (const auto& V:Poly.Vertices) A.WorldVerts.Add(Transform.TransformPosition(V));
			}
		}
	}

	void CollectOutdoorAdmittedEnvCells(UACEDatSubsystem& Dat, const FVector& CameraWorldPos,
		const FConvexVolume& ViewFrustum, float WorldScale, const TArrayView<const uint32> LandblockKeys,
		TSet<int32>& OutAdmittedEnvCells, TArray<FAdmittedAperture>* OutApertures,
		TArray<FAdmittedAperture>* OutExitApertures)
	{
		OutAdmittedEnvCells.Reset();
		const FConvexVolume PortalFrustum = MakeCameraPortalFrustum(CameraWorldPos, ViewFrustum);
		if (OutApertures) OutApertures->Reset();
		if (OutExitApertures) OutExitApertures->Reset();
		for (uint32 Key:LandblockKeys)
		{
			TArray<FAdmittedAperture> Doors;
			CollectLandblockDoorwayApertures(Dat,Key,WorldScale,Doors);
			for (auto& Door:Doors)
			{
				// PView uses the portal side and clipped view. The supplied resident
				// landblocks bound work; a second 80m cutoff left distant shells hollow.
				if (FVector::DotProduct(Door.WorldNormal,CameraWorldPos-Door.WorldVerts[0]) < -0.0002f*WorldScale) continue;
				TArray<FVector> Clipped;
				if (!ClipPolygonAgainstFrustum(Door.WorldVerts,PortalFrustum,Clipped)) continue;
				Door.WorldVerts=MoveTemp(Clipped);
				if (OutApertures) OutApertures->Add(Door);
				TSet<int32> ThroughDoor; bool Outside=false; TArray<FAdmittedAperture> Exits;
				CollectIndoorPViewCells(Dat,Door.DestEnvCellId,CameraWorldPos,
					MakePortalFrustum(CameraWorldPos,Door.WorldVerts,PortalFrustum),WorldScale,
					ThroughDoor,Outside,128,OutExitApertures ? &Exits : nullptr);
				OutAdmittedEnvCells.Append(ThroughDoor);
				if (OutExitApertures) OutExitApertures->Append(Exits);
			}
		}
	}

	void CollectIndoorPViewCells(
		UACEDatSubsystem& Dat, uint32 ViewerEnvCellId,
		const FVector& CameraWorldPos, const FConvexVolume& ViewFrustum,
		float WorldScale, TSet<int32>& OutDrawCells, bool& bOutLookOutLand,
		int32 MaxCells, TArray<FAdmittedAperture>* OutOutsideApertures)
	{
		OutDrawCells.Reset();
		bOutLookOutLand = false;
		if (OutOutsideApertures) OutOutsideApertures->Reset();
		if ((ViewerEnvCellId & 0xFFFFu) < 0x0100u || MaxCells <= 0) return;
		auto CellToWorld = [WorldScale](uint32 Id, const FACEBuiltEnvCellMesh& Mesh)
		{
			const FVector Origin = FACEPosition::AceVectorToUnreal(
				FVector(((Id >> 24) & 0xFF) * 192.f, ((Id >> 16) & 0xFF) * 192.f, 0), WorldScale);
			return Mesh.GetCellLocalToLandblock(WorldScale) * FTransform(FQuat::Identity, Origin);
		};
		struct FView
		{
			uint32 Cell;
			FConvexVolume Frustum;
			TArray<uint32> Path;
		};
		TArray<FView> Queue;
		TArray<FAdmittedAperture> LandscapeDoors;
		bool bHaveLandscapeDoors = false;
		Queue.Add({ViewerEnvCellId, MakeCameraPortalFrustum(CameraWorldPos, ViewFrustum), {ViewerEnvCellId}});
		OutDrawCells.Add(static_cast<int32>(ViewerEnvCellId));
		// A cell can have multiple disjoint portal views. Deduplicating by cell loses
		// the second doorway; prevent cycles per path instead (PView::copy_view).
		constexpr int32 MaxViews = 512;
		for (int32 Qi = 0; Qi < Queue.Num() && Qi < MaxViews; ++Qi)
		{
			const FView View = Queue[Qi]; // Queue growth invalidates references.
			if (View.Cell == 0)
			{
				// PView::DrawCells calls LScape::draw with the outside portal view.
				// That draw can enter another door across a courtyard. Keep the exit
				// frustum and path, so this cannot admit rooms behind the viewer.
				if (!bHaveLandscapeDoors)
				{
					const int32 X = (ViewerEnvCellId >> 24) & 255, Y = (ViewerEnvCellId >> 16) & 255;
					for (int32 DX = -1; DX <= 1; ++DX)
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						if (X+DX < 0 || X+DX > 254 || Y+DY < 0 || Y+DY > 254) continue;
						TArray<FAdmittedAperture> Doors;
						CollectLandblockDoorwayApertures(Dat, ((X+DX)<<24)|((Y+DY)<<16), WorldScale, Doors);
						LandscapeDoors.Append(Doors);
					}
					bHaveLandscapeDoors = true;
				}
				for (const FAdmittedAperture& Door : LandscapeDoors)
				{
					if (View.Path.Contains(Door.DestEnvCellId) || Queue.Num() >= MaxViews) continue;
					if (FVector::DotProduct(Door.WorldNormal, CameraWorldPos-Door.WorldVerts[0]) < -0.0002*WorldScale) continue;
					TArray<FVector> Clipped;
					if (!ClipPolygonAgainstFrustum(Door.WorldVerts, View.Frustum, Clipped)) continue;
					if (!OutDrawCells.Contains(Door.DestEnvCellId) && OutDrawCells.Num() >= MaxCells) continue;
					OutDrawCells.Add(Door.DestEnvCellId);
					FView Next{Door.DestEnvCellId, MakePortalFrustum(CameraWorldPos, Clipped, View.Frustum), View.Path};
					Next.Path.Add(Door.DestEnvCellId); Queue.Add(MoveTemp(Next));
				}
				continue;
			}
			const FACEBuiltEnvCellMesh* Mesh = Dat.FindEnvCellMesh(View.Cell, WorldScale);
			if (!Mesh) { Dat.RequestEnvCellMesh(View.Cell, WorldScale); continue; }
			const FTransform Xform = CellToWorld(View.Cell, *Mesh);
			const FVector CamLocal = Xform.InverseTransformPosition(CameraWorldPos);
			for (int32 I = 0; I < Mesh->CellPortals.Num(); ++I)
			{
				const FACEDatCellPortal& Portal = Mesh->CellPortals[I];
				if (!Mesh->PortalApertureLocalVerts.IsValidIndex(I)
					|| Mesh->PortalApertureLocalVerts[I].Num() < 3
					|| !Mesh->PortalApertureLocalNormals.IsValidIndex(I)
					|| !Mesh->PortalApertureLocalD.IsValidIndex(I)) continue;
				const FVector N = Mesh->PortalApertureLocalNormals[I];
				const double Side = FVector::DotProduct(N, CamLocal) + Mesh->PortalApertureLocalD[I];
				if (Side > 0.0002f * WorldScale) continue;
				TArray<FVector> Verts, Clipped;
				for (const FVector& V : Mesh->PortalApertureLocalVerts[I]) Verts.Add(Xform.TransformPosition(V));
				if (!ClipPolygonAgainstFrustum(Verts, View.Frustum, Clipped)) continue;
				if (Portal.IsOutsidePortal())
				{
					bOutLookOutLand = true;
					if (OutOutsideApertures)
					{
						FAdmittedAperture& A = OutOutsideApertures->AddDefaulted_GetRef();
						A.DestEnvCellId = View.Cell; A.OtherPortalId = I;
						A.WorldVerts = Clipped; A.WorldNormal = Xform.TransformVectorNoScale(N).GetSafeNormal();
					}
					if (!View.Path.Contains(0u) && Queue.Num() < MaxViews)
					{
						FConvexVolume Through = MakePortalFrustum(CameraWorldPos, Clipped, View.Frustum);
						Through.Planes.Add(FPlane(Clipped[0], -Xform.TransformVectorNoScale(N).GetSafeNormal()));
						Through.Init();
						FView Next{0, MoveTemp(Through), View.Path};
						Next.Path.Add(0); Queue.Add(MoveTemp(Next));
					}
					continue;
				}
				const uint32 Dest = (View.Cell & 0xFFFF0000u) | Portal.OtherCellId;
				if (Portal.OtherCellId < 0x0100u || View.Path.Contains(Dest)
					|| Queue.Num() >= MaxViews) continue;
				if (!OutDrawCells.Contains(static_cast<int32>(Dest)) && OutDrawCells.Num() >= MaxCells) continue;
				FConvexVolume Through = MakePortalFrustum(CameraWorldPos, Clipped, View.Frustum);
				const FACEBuiltEnvCellMesh* Other = Dat.FindEnvCellMesh(Dest, WorldScale);
				if (!Portal.IsExactMatch() && Portal.OtherPortalId != 0xFFFFu)
				{
					if (!Other) { Dat.RequestEnvCellMesh(Dest, WorldScale); continue; }
					if (!Other->PortalApertureLocalVerts.IsValidIndex(Portal.OtherPortalId)) continue;
					Verts.Reset();
					const FTransform OtherXform = CellToWorld(Dest, *Other);
					for (const FVector& V : Other->PortalApertureLocalVerts[Portal.OtherPortalId])
						Verts.Add(OtherXform.TransformPosition(V));
					if (!ClipPolygonAgainstFrustum(Verts, Through, Clipped)) continue;
					Through = MakePortalFrustum(CameraWorldPos, Clipped, Through);
				}
				OutDrawCells.Add(static_cast<int32>(Dest));
				FView Next{Dest, MoveTemp(Through), View.Path};
				Next.Path.Add(Dest); Queue.Add(MoveTemp(Next));
				Dat.RequestEnvCellMesh(Dest, WorldScale);
			}
		}
	}
}
