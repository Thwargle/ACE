#include "Dat/ACESetupMeshBuilder.h"
#include "Dat/ACEDatCursor.h"
#include "ACETypes.h"
#include "Dat/ACEPolygonMeshBuilder.h"



bool FACESetupMeshBuilder::LoadGfxObj(uint32 GfxObjId, FACEDatGfxObj& Out)
{
	if (const FACEDatGfxObj* Cached = GfxCache.Find(GfxObjId))
	{
		Out = *Cached;
		return true;
	}

	TArray<uint8> Blob;
	bool bFound = Portal && Portal->ReadFile(GfxObjId, Blob);
	if (!bFound && HighRes)
	{
		bFound = HighRes->ReadFile(GfxObjId, Blob);
	}
	if (!bFound)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: missing GfxObj 0x%08X"), GfxObjId);
		return false;
	}

	FACEDatCursor Cur(Blob);
	FACEDatGfxObj Parsed;
	if (!ACEDatUnpack::UnpackGfxObj(Cur, Parsed))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: failed to unpack GfxObj 0x%08X"), GfxObjId);
		return false;
	}

	// CPhysicsPart::GetMaxDegradeDistance honors the authored zero-distance
	// models (tutorial spawn arrow 010028CA / degrade 11000118). They are DAT
	// stabs, not server objects with a HiddenAdmin bit. Keep physics separate.
	if (Parsed.DIDDegrade && Portal->ReadFile(Parsed.DIDDegrade, Blob))
	{
		FACEDatCursor Degrade(Blob);
		bool Ok = true;
		const uint32 Id = Degrade.ReadU32(Ok);
		const uint32 Count = Degrade.ReadU32(Ok);
		if (Ok && Id == Parsed.DIDDegrade && Count > 0 && Count <= static_cast<uint32>(Degrade.Remaining() / 20))
		{
			float MaxDistance = 0.f;
			for (uint32 Index = 0; Index < Count; ++Index)
			{
				const uint32 Model = Degrade.ReadU32(Ok);
				const uint32 Mode = Degrade.ReadU32(Ok);
				if (Index == 0 && Mode >= 1 && Mode <= 5) Parsed.DrawMode = Mode;
				Degrade.ReadF32(Ok); Degrade.ReadF32(Ok); // minimum / ideal
				const float Max = Degrade.ReadF32(Ok);
				// Retail uses the last drawable level (the final entry is the null
				// sentinel), or the first level for one/two-entry tables.
				if (Index == (Count <= 2 ? 0u : Count - 2) && FMath::IsFinite(Max))
					Parsed.MaxDegradeDistance = FMath::Max(0.f, Max);
				if (Model) MaxDistance = FMath::Max(MaxDistance, Max);
			}
			if (Ok && MaxDistance <= 0.f) Parsed.Polygons.Reset();
		}
	}
	GfxCache.Add(GfxObjId, Parsed);
	Out = MoveTemp(Parsed);
	return true;
}

void FACESetupMeshBuilder::AppendGfxObjLocal(const FACEDatGfxObj& Gfx, int32 PartIndex,
    const FACEObjDesc* Appearance, float WorldScale, FACEBuiltSetupPart& OutPart)
{
    OutPart.DrawMode = Gfx.DrawMode;
    OutPart.MaxDegradeDistance = Gfx.MaxDegradeDistance;
    // BSP PORT stores (CBldPortal index, polygon id). The exterior polygon can
    // differ from the reverse EnvCell aperture, especially for open courtyards.
    for (const auto& Entry : Gfx.DrawingPortalIndices)
    {
        const auto* Poly = Gfx.Polygons.Find(Entry.Value);
        if (!Poly) continue;
        FACEBuiltSetupPortal Aperture; Aperture.PortalIndex = Entry.Key;
        TArray<FVector> AceVertices;
        for (int16 Id : Poly->VertexIds) if (const auto* V=Gfx.Vertices.Find(static_cast<uint16>(Id)))
        {
            AceVertices.Add(FVector(V->Origin));
            Aperture.Vertices.Add(FACEPosition::AceVectorToUnreal(FVector(V->Origin),WorldScale));
        }
        if (AceVertices.Num()<3) continue;
        Aperture.Normal=FACEPosition::AceVectorToUnreal(FVector::CrossProduct(AceVertices[1]-AceVertices[0],
            AceVertices[2]-AceVertices[0]).GetSafeNormal(),1.f);
        OutPart.Portals.Add(MoveTemp(Aperture));
    }
    // Retail draws the constructed mesh, then BSPPORTAL/PView replaces each
    // aperture's depth with its portal view. A PORT polygon is not a wall:
    // some have opaque diagnostic colors (Yaraq tower 010014C3, poly 1).
    // Unreal draws the admitted cells directly, so keep PORT geometry solely
    // in OutPart.Portals for admission/depth masking, never as a colored face.
    TMap<uint16, FACEDatPolygon> DrawPolygons = Gfx.Polygons;
    for (const auto& Aperture : Gfx.DrawingPortalIndices) DrawPolygons.Remove(Aperture.Value);
    ACEPolygonMeshBuilder::Append(DrawPolygons, Gfx.Vertices, Gfx.Surfaces,
        Textures, PartIndex, Appearance, WorldScale, OutPart.Sections);
}

void FACESetupMeshBuilder::AppendGfxObjPhysicsLocal(const FACEDatGfxObj& Gfx, float WorldScale, FACEBuiltSetupPart& OutPart)
{
	if (Gfx.PhysicsPolygons.Num() == 0)
	{
		return;
	}

	// Only PhysicsBSP leaf references participate in retail collision. An empty
	// leaf set is empty space, not permission to use draw faces or unused polygons.
	TArray<uint16> PolyIds = Gfx.PhysicsCollisionPolyIds.Array();

	FACEBuiltMeshSection Section;
	Section.bCollisionOnly = true;
	Section.SurfaceId = 0;

	for (const uint16 PolyId : PolyIds)
	{
		// DrawingBSP and PhysicsBSP have independent polygon dictionaries.
		// A drawing PORT id or transparent surface cannot remove a physics face.
		const FACEDatPolygon* PolyPtr = Gfx.PhysicsPolygons.Find(PolyId);
		if (!PolyPtr)
		{
			continue;
		}
		const FACEDatPolygon& Poly = *PolyPtr;
		if (Poly.NumPts < 3 || Poly.VertexIds.Num() < 3)
		{
			continue;
		}

		const int32 BaseIndex = Section.Vertices.Num();
		TArray<int32> LocalIndices;
		LocalIndices.Reserve(Poly.VertexIds.Num());

		for (const int16 VertId : Poly.VertexIds)
		{
			const FACEDatSWVertex* SW = Gfx.Vertices.Find(static_cast<uint16>(VertId));
			if (!SW)
			{
				continue;
			}

			const FVector LocalPos = FACEPosition::AceVectorToUnreal(
				FVector(SW->Origin.X, SW->Origin.Y, SW->Origin.Z), WorldScale);
			const FVector LocalN = FACEPosition::AceVectorToUnreal(
				FVector(SW->Normal.X, SW->Normal.Y, SW->Normal.Z), 1.f).GetSafeNormal();

			Section.Vertices.Add(LocalPos);
			Section.Normals.Add(LocalN);
			Section.UVs.Add(FVector2D::ZeroVector);
			Section.VertexColors.Add(FLinearColor::White);
			LocalIndices.Add(BaseIndex + LocalIndices.Num());
		}

		if (LocalIndices.Num() < 3)
		{
			continue;
		}

		// Double-sided after RH→LH so walkables aren't one-way.
		for (int32 i = 1; i + 1 < LocalIndices.Num(); ++i)
		{
			Section.Triangles.Add(LocalIndices[0]);
			Section.Triangles.Add(LocalIndices[i + 1]);
			Section.Triangles.Add(LocalIndices[i]);

			Section.Triangles.Add(LocalIndices[0]);
			Section.Triangles.Add(LocalIndices[i]);
			Section.Triangles.Add(LocalIndices[i + 1]);
		}
	}

	if (!Section.IsEmpty())
	{
		OutPart.Sections.Add(MoveTemp(Section));
	}
}

bool FACESetupMeshBuilder::BuildSetup(uint32 SetupId, float WorldScale, FACEBuiltSetupMesh& OutMesh, int32 PlacementId)
{
	FACEObjDesc Empty;
	return BuildSetupWithAppearance(SetupId, Empty, WorldScale, OutMesh, PlacementId);
}

bool FACESetupMeshBuilder::GetHoldingLocation(uint32 SetupId, int32 ParentLocation, int32& OutPartIndex, FTransform3f& OutFrame)
{
	OutPartIndex = 0;
	OutFrame = FTransform3f::Identity;
	if (!Portal || SetupId == 0 || ParentLocation == 0)
	{
		return false;
	}
	auto* Locations = HoldingLocationCache.Find(SetupId);
	if (!Locations)
	{
		TArray<uint8> Blob;
		bool bFound = Portal->ReadFile(SetupId, Blob);
		if (!bFound && HighRes) bFound = HighRes->ReadFile(SetupId, Blob);
		if (!bFound) return false;
		FACEDatCursor Cur(Blob);
		FACEDatSetupModel Setup;
		if (!ACEDatUnpack::UnpackSetupModel(Cur, Setup)) return false;
		if (HoldingLocationCache.Num() >= 128)
		{
			auto Evicted = HoldingLocationCache.CreateIterator();
			Evicted.RemoveCurrent();
		}
		Locations = &HoldingLocationCache.Add(SetupId, MoveTemp(Setup.HoldingLocations));
	}
	if (const FACEDatLocationType* Loc = Locations->Find(ParentLocation))
	{
		OutPartIndex = Loc->PartId;
		OutFrame = Loc->Frame;
		return true;
	}
	return false;
}

bool FACESetupMeshBuilder::BuildGfxObjOnly(uint32 GfxObjId, const FACEObjDesc& Appearance, float WorldScale, FACEBuiltSetupMesh& OutMesh)
{
	// A bare GfxObj (0x01xxxxxx) used directly as a SetupId — the AC client wraps these in a
	// single-part "simple setup" (CPhysicsObj / CreateSimpleSetup). Many static world props
	// (life stones, statues, forges, dropped items) reference a GfxObj here rather than a real
	// 0x02 SetupModel, so we must build one identity-placed part from it.
	OutMesh = FACEBuiltSetupMesh();

	FACEDatGfxObj Gfx;
	if (!LoadGfxObj(GfxObjId, Gfx))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: SetupId 0x%08X is a GfxObj but failed to load"), GfxObjId);
		return false;
	}

	FACEBuiltSetupPart Part;
	Part.GfxObjId = GfxObjId;
	Part.BindTransform = FTransform::Identity;

	const FACEObjDesc* AppearancePtr = Appearance.HasVisualOverrides() ? &Appearance : nullptr;
	AppendGfxObjLocal(Gfx, 0, AppearancePtr, WorldScale, Part);
	AppendGfxObjPhysicsLocal(Gfx, WorldScale, Part);
	OutMesh.Parts.Add(MoveTemp(Part));

	const bool bOk = OutMesh.Parts.Num() > 0;
	if (!bOk)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: GfxObj-setup 0x%08X produced no geometry (no draw/physics polys)"), GfxObjId);
	}
	return bOk;
}

bool FACESetupMeshBuilder::BuildSetupWithAppearance(uint32 SetupId, const FACEObjDesc& Appearance, float WorldScale, FACEBuiltSetupMesh& OutMesh, int32 PlacementId)
{
	OutMesh = FACEBuiltSetupMesh();
	if (!Portal || SetupId == 0)
	{
		return false;
	}

	// SetupId is a DataID whose top byte encodes the file type: 0x02 = SetupModel (multi-part
	// rig with placement frames), 0x01 = a bare GfxObj used directly. Previously this always
	// parsed the blob as a SetupModel; for a 0x01 GfxObj that misread the GfxObj header as a
	// setup part list + placement frames, scattering the parts to garbage transforms (the
	// "tiny floating triangles" bug on life stones / statues / forges).
	if ((SetupId & 0xFF000000) == 0x01000000)
	{
		return BuildGfxObjOnly(SetupId, Appearance, WorldScale, OutMesh);
	}

	TArray<uint8> Blob;
	if (!Portal->ReadFile(SetupId, Blob))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: missing Setup 0x%08X"), SetupId);
		return false;
	}

	FACEDatCursor Cur(Blob);
	FACEDatSetupModel Setup;
	if (!ACEDatUnpack::UnpackSetupModel(Cur, Setup))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: failed to unpack Setup 0x%08X"), SetupId);
		return false;
	}

	// Apply clothing/face AnimPartChanges onto the Setup part list (replace only — never
	// expand beyond Setup.Parts; extras have no Placement frames and pile at the origin).
	TArray<uint32> Parts = Setup.Parts;
	for (const FACEObjDescAnimPartChange& Change : Appearance.AnimPartChanges)
	{
		const int32 Idx = static_cast<int32>(Change.PartIndex);
		if (Parts.IsValidIndex(Idx))
		{
			Parts[Idx] = static_cast<uint32>(Change.PartId);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("ACEDat: Setup 0x%08X AnimPartChange PartIndex %d beyond %d parts — skipped"),
				SetupId, Idx, Parts.Num());
		}
	}

	OutMesh.DefaultMotionTableId = Setup.DefaultMotionTable;
	OutMesh.Parts.Reserve(Parts.Num());

	const FACEObjDesc* AppearancePtr = Appearance.HasVisualOverrides() ? &Appearance : nullptr;

	for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
	{
		FACEBuiltSetupPart Part;
		Part.GfxObjId = Parts[PartIndex];
		Part.BindTransform = FTransform::Identity;

		const TArray<FTransform3f>* Frames = nullptr;
		if (PlacementId != 0)
		{
			Frames = Setup.PlacementFrames.Find(PlacementId);
		}
		if (!Frames || Frames->Num() == 0)
		{
			Frames = Setup.PlacementFrames.Find(0);
		}
		if ((!Frames || Frames->Num() == 0) && Setup.DefaultPlacementFrames.Num() > 0)
		{
			Frames = &Setup.DefaultPlacementFrames;
		}

		if (Frames && Frames->IsValidIndex(PartIndex))
		{
			const FTransform3f& T = (*Frames)[PartIndex];
			const FQuat4f R = T.GetRotation();
			const FVector3f PlaceS = T.GetScale3D();
			const FVector BindScale(
				FMath::Abs(PlaceS.X) > 0.01f ? PlaceS.X : 1.f,
				FMath::Abs(PlaceS.Y) > 0.01f ? PlaceS.Y : 1.f,
				FMath::Abs(PlaceS.Z) > 0.01f ? PlaceS.Z : 1.f);
			Part.BindTransform = FTransform(
				FACEPosition::AceQuatToUnreal(R.W, FVector(R.X, R.Y, R.Z)),
				FACEPosition::AceVectorToUnreal(FVector(T.GetTranslation().X, T.GetTranslation().Y, T.GetTranslation().Z), WorldScale),
				BindScale);
		}

		if (Setup.DefaultScale.IsValidIndex(PartIndex))
		{
			const FVector3f& S = Setup.DefaultScale[PartIndex];
			const FVector BindS = Part.BindTransform.GetScale3D();
			Part.BindTransform.SetScale3D(FVector(BindS.X * S.X, BindS.Y * S.Y, BindS.Z * S.Z));
		}

		// Human Setups pad unused cloak/extra slots with null GfxObj 0x010001EC (a 3-vert speck).
		// Drawing those shows "tiny triangles" on the back until a real cloak AnimPartChange lands.
		constexpr uint32 NullGfxObjId = 0x010001ECu;
		constexpr uint32 PortalFxAnchorGfx = 0x0100168Bu;
		if (Parts[PartIndex] != 0 && Parts[PartIndex] != NullGfxObjId && Parts[PartIndex] != PortalFxAnchorGfx)
		{
			FACEDatGfxObj Gfx;
			if (LoadGfxObj(Parts[PartIndex], Gfx))
			{
				if (Gfx.Polygons.Num() == 0)
				{
					UE_LOG(LogTemp, Verbose, TEXT("ACEDat: GfxObj 0x%08X (Setup 0x%08X part %d) has no drawing polygons (physics-only?)"),
						Parts[PartIndex], SetupId, PartIndex);
				}
				else if (Gfx.Polygons.Num() <= 1 && Gfx.Vertices.Num() <= 3
					&& Gfx.PhysicsPolygons.Num() == 0)
				{
					// Degenerate null speck only — keep 1-poly GfxObjs that still have physics.
				}
				else
				{
					AppendGfxObjLocal(Gfx, PartIndex, AppearancePtr, WorldScale, Part);
				}
				// Retail collision comes from PhysicsPolygons, not alpha-filtered draw meshes.
				AppendGfxObjPhysicsLocal(Gfx, WorldScale, Part);
			}
		}

		OutMesh.Parts.Add(MoveTemp(Part));
	}

	// Parts can be placement-only (Town Network portal ClipMap anchors). Those still need
	// BindTransforms for particle emitter parenting even with zero drawable sections.
	const bool bOk = OutMesh.Parts.Num() > 0;
	if (!bOk)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACEDat: Setup 0x%08X unpacked %d part(s) but produced zero drawable geometry (missing/physics-only GfxObjs?)"),
			SetupId, Parts.Num());
	}
	else if (OutMesh.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("ACEDat: Setup 0x%08X has %d placement part(s) but no drawable geometry (FX anchor)"),
			SetupId, OutMesh.Parts.Num());
	}
	else
	{
		int32 SectionCount = 0;
		int32 VertCount = 0;
		for (const FACEBuiltSetupPart& P : OutMesh.Parts)
		{
			SectionCount += P.Sections.Num();
			for (const FACEBuiltMeshSection& S : P.Sections)
			{
				VertCount += S.Vertices.Num();
			}
		}
		UE_LOG(LogTemp, Verbose, TEXT("ACEDat: Setup 0x%08X built %d part(s), %d section(s), %d vert(s)"),
			SetupId, OutMesh.Parts.Num(), SectionCount, VertCount);
	}
	return bOk;
}

void FACESetupMeshBuilder::TrimGfxCache(int32 MaxEntries)
{
	MaxEntries = FMath::Max(64, MaxEntries);
	const int32 Over = GfxCache.Num() - MaxEntries;
	if (Over <= 0)
	{
		return;
	}
	TArray<uint32> Keys;
	GfxCache.GetKeys(Keys);
	const int32 RemoveCount = FMath::Min(Over, Keys.Num());
	for (int32 i = 0; i < RemoveCount; ++i)
	{
		GfxCache.Remove(Keys[i]);
	}
}
