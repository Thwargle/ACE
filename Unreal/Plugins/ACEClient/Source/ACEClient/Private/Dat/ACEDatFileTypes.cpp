#include "Dat/ACEDatFileTypes.h"

namespace ACEDatUnpack
{
	static bool SkipSphere(FACEDatCursor& Cur)
	{
		return Cur.Skip(sizeof(float) * 4);
	}

	static bool SkipPlane(FACEDatCursor& Cur)
	{
		return Cur.Skip(sizeof(float) * 4);
	}

	static bool SkipFrame(FACEDatCursor& Cur)
	{
		return Cur.Skip(sizeof(float) * 7);
	}

	bool SkipBspTree(FACEDatCursor& Cur, EACEBspType TreeType, int32 Depth, TSet<uint16>* OutDrawingPortalPolys,
		TSet<uint16>* OutPhysicsPolyIds, TMultiMap<int32, uint16>* OutPortalIndices, TSet<uint16>* OutDrawingPolyIds)
	{
		// Desynced streams can recurse forever on garbage that keeps looking like split nodes.
		constexpr int32 MaxDepth = 4096;
		if (Depth > MaxDepth)
		{
			UE_LOG(LogTemp, Error, TEXT("ACEDat: SkipBspTree exceeded depth %d — aborting"), MaxDepth);
			return false;
		}

		ANSICHAR Tag[5];
		if (!Cur.PeekFourCCReversed(Tag))
		{
			return false;
		}

		// IMPORTANT: BSP node type tags are CASE-SENSITIVE ("BPIN" pos+neg vs "BpIN" neg-only,
		// "BPnN" vs "BpnN"). FString::operator== is case-INSENSITIVE in Unreal, which collapsed
		// those distinct tags together and made the pos/neg child recursion read the wrong number
		// of subtrees — desyncing the stream and failing to unpack every GfxObj whose drawing BSP
		// contained a split node (i.e. anything more than a trivial single-polygon mesh). Compare
		// the raw ASCII tag bytes case-sensitively instead.
		if (FCStringAnsi::Strcmp(Tag, "PORT") == 0)
		{
			bool bOk = true;
			Cur.ReadFourCCReversed(bOk);
			if (!bOk || !SkipPlane(Cur))
			{
				return false;
			}
			if (!SkipBspTree(Cur, TreeType, Depth + 1, OutDrawingPortalPolys, OutPhysicsPolyIds, OutPortalIndices, OutDrawingPolyIds)
				|| !SkipBspTree(Cur, TreeType, Depth + 1, OutDrawingPortalPolys, OutPhysicsPolyIds, OutPortalIndices, OutDrawingPolyIds))
			{
				return false;
			}
			if (TreeType == EACEBspType::Drawing)
			{
				if (!SkipSphere(Cur))
				{
					return false;
				}
				const uint32 NumPolys = Cur.ReadU32(bOk);
				const uint32 NumPortals = Cur.ReadU32(bOk);
				if (!bOk)
				{
					return false;
				}
				for (uint32 Pi = 0; Pi < NumPolys; ++Pi)
				{
					const uint16 PolyId = Cur.ReadU16(bOk);
					if (!bOk) return false;
					if (OutDrawingPolyIds) OutDrawingPolyIds->Add(PolyId);
				}
				for (uint32 Pi = 0; Pi < NumPortals; ++Pi)
				{
					const int16 PortalIndex = Cur.ReadI16(bOk);
					const int16 PolygonId = Cur.ReadI16(bOk);
					if (!bOk)
					{
						return false;
					}
					if (OutPortalIndices && PortalIndex >= 0 && PolygonId >= 0)
						OutPortalIndices->AddUnique(PortalIndex, static_cast<uint16>(PolygonId));
					if (OutDrawingPortalPolys && PolygonId >= 0)
					{
						OutDrawingPortalPolys->Add(static_cast<uint16>(PolygonId));
					}
				}
			}
			return true;
		}

		if (FCStringAnsi::Strcmp(Tag, "LEAF") == 0)
		{
			bool bOk = true;
			Cur.ReadFourCCReversed(bOk);
			Cur.ReadI32(bOk);
			if (!bOk)
			{
				return false;
			}
			if (TreeType == EACEBspType::Physics)
			{
				Cur.ReadI32(bOk); // Solid
				if (!bOk || !SkipSphere(Cur))
				{
					return false;
				}
				const uint32 NumPolys = Cur.ReadU32(bOk);
				if (!bOk)
				{
					return false;
				}
				for (uint32 Pi = 0; Pi < NumPolys; ++Pi)
				{
					const uint16 PolyId = Cur.ReadU16(bOk);
					if (!bOk)
					{
						return false;
					}
					if (OutPhysicsPolyIds)
					{
						OutPhysicsPolyIds->Add(PolyId);
					}
				}
			}
			return true;
		}

		bool bOk = true;
		Cur.ReadFourCCReversed(bOk);
		if (!bOk || !SkipPlane(Cur))
		{
			return false;
		}

		const bool bPos =
			FCStringAnsi::Strcmp(Tag, "BPnn") == 0 || FCStringAnsi::Strcmp(Tag, "BPIn") == 0 ||
			FCStringAnsi::Strcmp(Tag, "BPIN") == 0 || FCStringAnsi::Strcmp(Tag, "BPnN") == 0;
		const bool bNeg =
			FCStringAnsi::Strcmp(Tag, "BpIN") == 0 || FCStringAnsi::Strcmp(Tag, "BpnN") == 0 ||
			FCStringAnsi::Strcmp(Tag, "BPIN") == 0 || FCStringAnsi::Strcmp(Tag, "BPnN") == 0;

		if (bPos && !SkipBspTree(Cur, TreeType, Depth + 1, OutDrawingPortalPolys, OutPhysicsPolyIds, OutPortalIndices, OutDrawingPolyIds))
		{
			return false;
		}
		if (bNeg && !SkipBspTree(Cur, TreeType, Depth + 1, OutDrawingPortalPolys, OutPhysicsPolyIds, OutPortalIndices, OutDrawingPolyIds))
		{
			return false;
		}

		if (TreeType == EACEBspType::Cell)
		{
			return true;
		}

		if (!SkipSphere(Cur))
		{
			return false;
		}

		if (TreeType == EACEBspType::Physics)
		{
			return true;
		}

		const uint32 NumPolys = Cur.ReadU32(bOk);
		if (!bOk) return false;
		for (uint32 Pi = 0; Pi < NumPolys; ++Pi)
		{
			const uint16 PolyId = Cur.ReadU16(bOk);
			if (!bOk) return false;
			if (OutDrawingPolyIds) OutDrawingPolyIds->Add(PolyId);
		}
		return true;
	}

	bool UnpackCellBspTree(FACEDatCursor& Cur, TArray<FACEDatCellBspNode>& OutNodes, int32 Depth)
	{
		// Mirrors SkipBspTree(..., Cell) but stores nodes for EnvCell.point_in_cell.
		constexpr int32 MaxDepth = 4096;
		if (Depth > MaxDepth)
		{
			UE_LOG(LogTemp, Error, TEXT("ACEDat: UnpackCellBspTree exceeded depth %d — aborting"), MaxDepth);
			return false;
		}

		ANSICHAR Tag[5];
		if (!Cur.PeekFourCCReversed(Tag))
		{
			return false;
		}

		auto ReadPlane = [&](FVector4f& OutPlane) -> bool
		{
			bool bOk = true;
			const float Nx = Cur.ReadF32(bOk);
			const float Ny = Cur.ReadF32(bOk);
			const float Nz = Cur.ReadF32(bOk);
			const float D = Cur.ReadF32(bOk);
			if (!bOk)
			{
				return false;
			}
			OutPlane = FVector4f(Nx, Ny, Nz, D);
			return true;
		};

		if (FCStringAnsi::Strcmp(Tag, "PORT") == 0)
		{
			bool bOk = true;
			Cur.ReadFourCCReversed(bOk);
			const int32 Self = OutNodes.AddDefaulted();
			FACEDatCellBspNode& Node = OutNodes[Self];
			Node.bLeaf = false;
			if (!bOk || !ReadPlane(Node.Plane))
			{
				return false;
			}
			const int32 PosStart = OutNodes.Num();
			if (!UnpackCellBspTree(Cur, OutNodes, Depth + 1))
			{
				return false;
			}
			OutNodes[Self].PosChild = PosStart;
			const int32 NegStart = OutNodes.Num();
			if (!UnpackCellBspTree(Cur, OutNodes, Depth + 1))
			{
				return false;
			}
			OutNodes[Self].NegChild = NegStart;
			// Cell PORT: no sphere / poly / portal lists after children.
			return true;
		}

		if (FCStringAnsi::Strcmp(Tag, "LEAF") == 0)
		{
			bool bOk = true;
			Cur.ReadFourCCReversed(bOk);
			Cur.ReadI32(bOk); // LeafIndex
			if (!bOk)
			{
				return false;
			}
			const int32 Self = OutNodes.AddDefaulted();
			OutNodes[Self].bLeaf = true;
			// Cell LEAF has no plane / Solid / sphere / polys.
			return true;
		}

		bool bOk = true;
		Cur.ReadFourCCReversed(bOk);
		const int32 Self = OutNodes.AddDefaulted();
		FACEDatCellBspNode& Node = OutNodes[Self];
		Node.bLeaf = false;
		if (!bOk || !ReadPlane(Node.Plane))
		{
			return false;
		}

		const bool bPos =
			FCStringAnsi::Strcmp(Tag, "BPnn") == 0 || FCStringAnsi::Strcmp(Tag, "BPIn") == 0 ||
			FCStringAnsi::Strcmp(Tag, "BPIN") == 0 || FCStringAnsi::Strcmp(Tag, "BPnN") == 0;
		const bool bNeg =
			FCStringAnsi::Strcmp(Tag, "BpIN") == 0 || FCStringAnsi::Strcmp(Tag, "BpnN") == 0 ||
			FCStringAnsi::Strcmp(Tag, "BPIN") == 0 || FCStringAnsi::Strcmp(Tag, "BPnN") == 0;

		if (bPos)
		{
			const int32 PosStart = OutNodes.Num();
			if (!UnpackCellBspTree(Cur, OutNodes, Depth + 1))
			{
				return false;
			}
			OutNodes[Self].PosChild = PosStart;
		}
		if (bNeg)
		{
			const int32 NegStart = OutNodes.Num();
			if (!UnpackCellBspTree(Cur, OutNodes, Depth + 1))
			{
				return false;
			}
			OutNodes[Self].NegChild = NegStart;
		}
		return true;
	}

	bool SkipAnimationHook(FACEDatCursor& Cur)
	{
		FACEDatAnimationHook Ignored;
		return UnpackAnimationHook(Cur, Ignored);
	}

	bool UnpackAnimationHook(FACEDatCursor& Cur, FACEDatAnimationHook& Out)
	{
		Out = FACEDatAnimationHook();
		bool bOk = true;
		const int32 Type = static_cast<int32>(Cur.ReadU32(bOk));
		Out.Type = static_cast<EACEAnimationHookType>(Type);
		Out.Direction = static_cast<EACEAnimationHookDirection>(Cur.ReadI32(bOk));
		if (!bOk)
		{
			return false;
		}

		switch (Type)
		{
		case 0:  // NoOp
		case 4:  // AnimationDone
		case 17: // DefaultScript
			return true;
		case 1:  // Sound Id
		case 2:  // SoundTable
		case 14: // DestroyParticle
		case 15: // StopParticle
			Out.Id = Cur.ReadU32(bOk);
			return bOk;
		case 6:  // Ethereal
		case 16: // NoDraw
		case 25: // SetLight
			Out.State = Cur.ReadI32(bOk);
			return bOk;
		case 18: // DefaultScriptPart
			Out.PartIndex = Cur.ReadU32(bOk);
			return bOk;
		case 3: // AttackCone
			Out.PartIndex = Cur.ReadU32(bOk);
			Out.AttackLeft.X = Cur.ReadF32(bOk);
			Out.AttackLeft.Y = Cur.ReadF32(bOk);
			Out.AttackRight.X = Cur.ReadF32(bOk);
			Out.AttackRight.Y = Cur.ReadF32(bOk);
			Out.AttackRadius = Cur.ReadF32(bOk);
			Out.AttackHeight = Cur.ReadF32(bOk);
			return bOk;
		case 5: // ReplaceObject
		{
			Out.PartIndex = Cur.ReadU16(bOk);
			if (!bOk)
			{
				return false;
			}
			Out.Id = Cur.ReadAsDataIdOfKnownType(0x01000000, bOk);
			return bOk;
		}
		case 7:  // TransparentPart
		case 9:  // LuminousPart
		case 11: // DiffusePart
			Out.PartIndex = Cur.ReadU32(bOk);
			Out.Start = Cur.ReadF32(bOk);
			Out.End = Cur.ReadF32(bOk);
			Out.Time = Cur.ReadF32(bOk);
			return bOk;
		case 8:  // Luminous
		case 10: // Diffuse
		case 20: // Transparent
			Out.Start = Cur.ReadF32(bOk);
			Out.End = Cur.ReadF32(bOk);
			Out.Time = Cur.ReadF32(bOk);
			return bOk;
		case 12: // Scale
			Out.End = Cur.ReadF32(bOk);
			Out.Time = Cur.ReadF32(bOk);
			return bOk;
		case 13: // CreateParticle
		case 26: // CreateBlockingParticle
		{
			Out.Id = Cur.ReadU32(bOk); // EmitterInfoId
			Out.PartIndex = Cur.ReadU32(bOk);
			FVector3f Origin;
			FQuat4f Rotation;
			if (!bOk || !Cur.ReadFrame(Origin, Rotation))
			{
				return false;
			}
			Out.Frame = FTransform3f(Rotation, Origin);
			Out.SecondaryId = Cur.ReadU32(bOk); // EmitterId
			return bOk;
		}
		case 19: // CallPES
			Out.Id = Cur.ReadU32(bOk);
			Out.Time = Cur.ReadF32(bOk); // Pause
			return bOk;
		case 21: // SoundTweaked
			Out.Id = Cur.ReadU32(bOk);
			Out.Priority = Cur.ReadF32(bOk);
			Out.Probability = Cur.ReadF32(bOk);
			Out.Volume = Cur.ReadF32(bOk);
			return bOk;
		case 22: // SetOmega
			Out.Vector = Cur.ReadVector3(bOk);
			return bOk;
		case 23: // TextureVelocity
			Out.USpeed = Cur.ReadF32(bOk);
			Out.VSpeed = Cur.ReadF32(bOk);
			return bOk;
		case 24: // TextureVelocityPart
			Out.PartIndex = Cur.ReadU32(bOk);
			Out.USpeed = Cur.ReadF32(bOk);
			Out.VSpeed = Cur.ReadF32(bOk);
			return bOk;
		default:
			// Retail DAT only authors hooks 0–26. Treat unknown as empty payload so
			// SkipAnimationHook / animation seek stays safe instead of aborting the parent blob.
			UE_LOG(LogTemp, Warning,
				TEXT("ACEDat: unknown AnimationHook type %d — assuming empty payload"), Type);
			Out.Type = EACEAnimationHookType::Unknown;
			return true;
		}
	}

	bool UnpackPolygon(FACEDatCursor& Cur, FACEDatPolygon& Out)
	{
		bool bOk = true;
		Out.NumPts = Cur.ReadU8(bOk);
		Out.Stippling = static_cast<EACEStipplingType>(Cur.ReadU8(bOk));
		Out.SidesType = static_cast<EACECullMode>(Cur.ReadI32(bOk));
		Out.PosSurface = Cur.ReadI16(bOk);
		Out.NegSurface = Cur.ReadI16(bOk);
		if (!bOk)
		{
			return false;
		}

		Out.VertexIds.SetNum(Out.NumPts);
		for (uint8 i = 0; i < Out.NumPts; ++i)
		{
			Out.VertexIds[i] = Cur.ReadI16(bOk);
		}
		if (!bOk)
		{
			return false;
		}

		const bool bNoPos = EnumHasAnyFlags(Out.Stippling, EACEStipplingType::NoPos);
		if (!bNoPos)
		{
			Out.PosUVIndices.SetNum(Out.NumPts);
			for (uint8 i = 0; i < Out.NumPts; ++i)
			{
				Out.PosUVIndices[i] = Cur.ReadU8(bOk);
			}
		}

		const bool bNoNeg = EnumHasAnyFlags(Out.Stippling, EACEStipplingType::NoNeg);
		if (Out.SidesType == EACECullMode::Clockwise && !bNoNeg)
		{
			Out.NegUVIndices.SetNum(Out.NumPts);
			for (uint8 i = 0; i < Out.NumPts; ++i)
			{
				Out.NegUVIndices[i] = Cur.ReadU8(bOk);
			}
		}

		if (Out.SidesType == EACECullMode::None)
		{
			Out.NegSurface = Out.PosSurface;
			Out.NegUVIndices = Out.PosUVIndices;
		}

		return bOk;
	}

	bool UnpackSWVertex(FACEDatCursor& Cur, FACEDatSWVertex& Out)
	{
		bool bOk = true;
		const uint16 NumUVs = Cur.ReadU16(bOk);
		Out.Origin = Cur.ReadVector3(bOk);
		Out.Normal = Cur.ReadVector3(bOk);
		if (!bOk)
		{
			return false;
		}
		Out.UVs.SetNum(NumUVs);
		for (uint16 i = 0; i < NumUVs; ++i)
		{
			Out.UVs[i].X = Cur.ReadF32(bOk);
			Out.UVs[i].Y = Cur.ReadF32(bOk);
		}
		return bOk;
	}

	bool UnpackGfxObj(FACEDatCursor& Cur, FACEDatGfxObj& Out)
	{
		Out = FACEDatGfxObj();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.Flags = static_cast<EACEGfxObjFlags>(Cur.ReadU32(bOk));
		if (!bOk)
		{
			return false;
		}

		const uint32 SurfaceCount = Cur.ReadCompressedUInt32(bOk);
		if (!bOk)
		{
			return false;
		}
		Out.Surfaces.SetNum(static_cast<int32>(SurfaceCount));
		for (uint32 i = 0; i < SurfaceCount; ++i)
		{
			Out.Surfaces[i] = Cur.ReadU32(bOk);
		}

		const int32 VertexType = Cur.ReadI32(bOk);
		const uint32 NumVertices = Cur.ReadU32(bOk);
		if (!bOk || VertexType != 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("ACEDat: unsupported VertexType %d on GfxObj 0x%08X"), VertexType, Out.Id);
			return false;
		}
		for (uint32 i = 0; i < NumVertices; ++i)
		{
			const uint16 Key = Cur.ReadU16(bOk);
			FACEDatSWVertex Vert;
			if (!bOk || !UnpackSWVertex(Cur, Vert))
			{
				return false;
			}
			Out.Vertices.Add(Key, MoveTemp(Vert));
		}

		if (EnumHasAnyFlags(Out.Flags, EACEGfxObjFlags::HasPhysics))
		{
			const uint32 PhysPolyCount = Cur.ReadCompressedUInt32(bOk);
			if (!bOk)
			{
				return false;
			}
			for (uint32 i = 0; i < PhysPolyCount; ++i)
			{
				const uint16 Key = Cur.ReadU16(bOk);
				FACEDatPolygon Poly;
				if (!bOk || !UnpackPolygon(Cur, Poly))
				{
					return false;
				}
				Out.PhysicsPolygons.Add(Key, MoveTemp(Poly));
			}
			if (!SkipBspTree(Cur, EACEBspType::Physics, 0, nullptr, &Out.PhysicsCollisionPolyIds))
			{
				return false;
			}
		}

		Out.SortCenter = Cur.ReadVector3(bOk);
		if (!bOk)
		{
			return false;
		}

		if (EnumHasAnyFlags(Out.Flags, EACEGfxObjFlags::HasDrawing))
		{
			const uint32 DrawPolyCount = Cur.ReadCompressedUInt32(bOk);
			if (!bOk)
			{
				return false;
			}
			for (uint32 i = 0; i < DrawPolyCount; ++i)
			{
				const uint16 Key = Cur.ReadU16(bOk);
				FACEDatPolygon Poly;
				if (!bOk || !UnpackPolygon(Cur, Poly))
				{
					return false;
				}
				Out.Polygons.Add(Key, MoveTemp(Poly));
			}
			if (!SkipBspTree(Cur, EACEBspType::Drawing, 0, &Out.DrawingPortalPolygonIds, nullptr, &Out.DrawingPortalIndices, &Out.DrawingPolygonIds))
			{
				return false;
			}
		}

		if (EnumHasAnyFlags(Out.Flags, EACEGfxObjFlags::HasDIDDegrade))
		{
			Out.DIDDegrade = Cur.ReadU32(bOk);
		}

		return bOk;
	}

	static bool SkipLocationType(FACEDatCursor& Cur)
	{
		bool bOk = true;
		Cur.ReadI32(bOk);
		return bOk && SkipFrame(Cur);
	}

	static bool UnpackLocationType(FACEDatCursor& Cur, FACEDatLocationType& Out)
	{
		bool bOk = true;
		Out.PartId = Cur.ReadI32(bOk);
		FVector3f Origin;
		FQuat4f Quat;
		if (!bOk || !Cur.ReadFrame(Origin, Quat))
		{
			return false;
		}
		Out.Frame = FTransform3f(Quat, Origin);
		return true;
	}

	static bool UnpackDictLocations(FACEDatCursor& Cur, TMap<int32, FACEDatLocationType>& Out)
	{
		Out.Reset();
		bool bOk = true;
		const int32 Count = Cur.ReadI32(bOk);
		if (!bOk || Count < 0)
		{
			return false;
		}
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Key = Cur.ReadI32(bOk);
			FACEDatLocationType Loc;
			if (!bOk || !UnpackLocationType(Cur, Loc))
			{
				return false;
			}
			Out.Add(Key, Loc);
		}
		return true;
	}

	static bool SkipDictLocations(FACEDatCursor& Cur)
	{
		bool bOk = true;
		const int32 Count = Cur.ReadI32(bOk);
		if (!bOk || Count < 0)
		{
			return false;
		}
		for (int32 i = 0; i < Count; ++i)
		{
			Cur.ReadI32(bOk); // key
			if (!bOk || !SkipLocationType(Cur))
			{
				return false;
			}
		}
		return true;
	}

	bool UnpackSetupModel(FACEDatCursor& Cur, FACEDatSetupModel& Out)
	{
		Out = FACEDatSetupModel();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.Flags = static_cast<EACESetupFlags>(Cur.ReadU32(bOk));
		if (!bOk)
		{
			return false;
		}

		const uint32 NumParts = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}
		Out.Parts.SetNum(static_cast<int32>(NumParts));
		for (uint32 i = 0; i < NumParts; ++i)
		{
			Out.Parts[i] = Cur.ReadU32(bOk);
		}

		if (EnumHasAnyFlags(Out.Flags, EACESetupFlags::HasParent))
		{
			Out.ParentIndex.SetNum(static_cast<int32>(NumParts));
			for (uint32 i = 0; i < NumParts; ++i)
			{
				Out.ParentIndex[i] = Cur.ReadU32(bOk);
			}
		}

		if (EnumHasAnyFlags(Out.Flags, EACESetupFlags::HasDefaultScale))
		{
			Out.DefaultScale.SetNum(static_cast<int32>(NumParts));
			for (uint32 i = 0; i < NumParts; ++i)
			{
				Out.DefaultScale[i] = Cur.ReadVector3(bOk);
			}
		}

		// HoldingLocations then ConnectionPoints
		if (!bOk || !UnpackDictLocations(Cur, Out.HoldingLocations) || !SkipDictLocations(Cur))
		{
			return false;
		}

		const int32 PlacementCount = Cur.ReadI32(bOk);
		if (!bOk || PlacementCount < 0)
		{
			return false;
		}

		for (int32 p = 0; p < PlacementCount; ++p)
		{
			const int32 PlacementKey = Cur.ReadI32(bOk);
			if (!bOk)
			{
				return false;
			}

			TArray<FTransform3f> Frames;
			Frames.SetNum(static_cast<int32>(NumParts));
			for (uint32 i = 0; i < NumParts; ++i)
			{
				FVector3f Origin;
				FQuat4f Quat;
				if (!Cur.ReadFrame(Origin, Quat))
				{
					return false;
				}
				Frames[i] = FTransform3f(Quat, Origin);
			}

			const uint32 NumHooks = Cur.ReadU32(bOk);
			if (!bOk)
			{
				return false;
			}
			for (uint32 h = 0; h < NumHooks; ++h)
			{
				if (!SkipAnimationHook(Cur))
				{
					return false;
				}
			}

			Out.PlacementFrames.Add(PlacementKey, Frames);
			if (PlacementKey == 0)
			{
				Out.DefaultPlacementFrames = Frames;
			}
		}

		// CylSpheres
		{
			const uint32 Count = Cur.ReadU32(bOk);
			if (!bOk || Count > 4096) return false;
			for (uint32 I=0; I<Count; ++I)
			{
				auto& Shape=Out.CylSpheres.AddDefaulted_GetRef();
				Shape.Origin=Cur.ReadVector3(bOk);
				Shape.Radius=Cur.ReadF32(bOk); Shape.Height=Cur.ReadF32(bOk);
				if (!bOk) return false;
			}
		}

		// Spheres
		{
			const uint32 Count = Cur.ReadU32(bOk);
			if (!bOk || Count > 4096) return false;
			for (uint32 I=0; I<Count; ++I)
			{
				auto& Shape=Out.Spheres.AddDefaulted_GetRef();
				Shape.Origin=Cur.ReadVector3(bOk); Shape.Radius=Cur.ReadF32(bOk);
				if (!bOk) return false;
			}
		}

		Out.Height = Cur.ReadF32(bOk);
		Out.Radius = Cur.ReadF32(bOk);
		Out.StepUpHeight = Cur.ReadF32(bOk);
		Out.StepDownHeight = Cur.ReadF32(bOk);
		SkipSphere(Cur); // SortingSphere
		Out.SelectionSphereOrigin = Cur.ReadVector3(bOk);
		Out.SelectionSphereRadius = Cur.ReadF32(bOk);

		// Lights dictionary
		{
			const int32 Count = Cur.ReadI32(bOk);
			if (!bOk || Count < 0 || Count > 4096)
			{
				return false;
			}
			for (int32 i = 0; i < Count; ++i)
			{
				const int32 Key = Cur.ReadI32(bOk);
				FACEDatLightInfo Light;
				if (!bOk || !Cur.ReadFrame(Light.Origin, Light.Orientation))
				{
					return false;
				}
				Light.Color = Cur.ReadU32(bOk);
				Light.Intensity = Cur.ReadF32(bOk);
				Light.Falloff = Cur.ReadF32(bOk);
				Light.ConeAngle = Cur.ReadF32(bOk);
				Out.Lights.Add(Key, Light);
			}
		}

		Out.DefaultAnimation = Cur.ReadU32(bOk);
		Out.DefaultScript = Cur.ReadU32(bOk);
		Out.DefaultMotionTable = Cur.ReadU32(bOk);
		Out.DefaultSoundTable = Cur.ReadU32(bOk);
		Out.DefaultScriptTable = Cur.ReadU32(bOk);
		return bOk;
	}

	bool UnpackSurface(FACEDatCursor& Cur, FACEDatSurface& Out)
	{
		// ACE.DatLoader Surface.Unpack does NOT read Id — blob starts with Type.
		Out = FACEDatSurface();
		bool bOk = true;
		Out.Type = static_cast<EACESurfaceType>(Cur.ReadU32(bOk));
		if (!bOk)
		{
			return false;
		}
		const bool bImage = EnumHasAnyFlags(Out.Type, EACESurfaceType::Base1Image)
			|| EnumHasAnyFlags(Out.Type, EACESurfaceType::Base1ClipMap);
		if (bImage)
		{
			Out.OrigTextureId = Cur.ReadU32(bOk);
			Out.OrigPaletteId = Cur.ReadU32(bOk);
		}
		else
		{
			Out.ColorValue = Cur.ReadU32(bOk);
		}
		Out.Translucency = Cur.ReadF32(bOk);
		Out.Luminosity = Cur.ReadF32(bOk);
		Out.Diffuse = Cur.ReadF32(bOk);
		return bOk;
	}

	bool UnpackSurfaceTexture(FACEDatCursor& Cur, FACEDatSurfaceTexture& Out)
	{
		Out = FACEDatSurfaceTexture();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Cur.ReadI32(bOk); // Unknown
		Cur.ReadU8(bOk);  // UnknownByte
		const int32 Count = Cur.ReadI32(bOk);
		if (!bOk || Count < 0)
		{
			return false;
		}
		Out.Textures.SetNum(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			Out.Textures[i] = Cur.ReadU32(bOk);
		}
		return bOk;
	}

	bool UnpackTexture(FACEDatCursor& Cur, FACEDatTexture& Out)
	{
		Out = FACEDatTexture();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Cur.ReadI32(bOk); // Unknown
		Out.Width = Cur.ReadI32(bOk);
		Out.Height = Cur.ReadI32(bOk);
		Out.Format = static_cast<EACESurfacePixelFormat>(Cur.ReadU32(bOk));
		const int32 Length = Cur.ReadI32(bOk);
		if (!bOk || Length < 0 || !Cur.CanRead(Length))
		{
			return false;
		}
		Out.SourceData.SetNumUninitialized(Length);
		Cur.ReadBytes(Out.SourceData.GetData(), Length);
		if (Out.Format == EACESurfacePixelFormat::INDEX16 || Out.Format == EACESurfacePixelFormat::P8)
		{
			Out.DefaultPaletteId = Cur.ReadU32(bOk);
			Out.bHasPalette = bOk;
		}
		return bOk;
	}

	bool UnpackFont(FACEDatCursor& Cur, FACEDatFont& Out)
	{
		Out = FACEDatFont();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.MaxCharHeight = Cur.ReadU32(bOk);
		Out.MaxCharWidth = Cur.ReadU32(bOk);
		const uint32 NumCharacters = Cur.ReadU32(bOk);
		if (!bOk || NumCharacters > 0x10000u)
		{
			return false;
		}
		Out.Chars.Reserve(static_cast<int32>(NumCharacters));
		for (uint32 i = 0; i < NumCharacters; ++i)
		{
			FACEDatFontChar Ch;
			Ch.Unicode = Cur.ReadU16(bOk);
			Ch.OffsetX = Cur.ReadU16(bOk);
			Ch.OffsetY = Cur.ReadU16(bOk);
			Ch.Width = Cur.ReadU8(bOk);
			Ch.Height = Cur.ReadU8(bOk);
			Ch.HorizontalOffsetBefore = Cur.ReadU8(bOk);
			Ch.HorizontalOffsetAfter = Cur.ReadU8(bOk);
			Ch.VerticalOffsetBefore = Cur.ReadU8(bOk);
			if (!bOk)
			{
				return false;
			}
			Out.IndexByUnicode.Add(Ch.Unicode, Out.Chars.Num());
			Out.Chars.Add(Ch);
		}
		Out.NumHorizontalBorderPixels = Cur.ReadU32(bOk);
		Out.NumVerticalBorderPixels = Cur.ReadU32(bOk);
		Out.BaselineOffset = Cur.ReadU32(bOk);
		Out.ForegroundSurfaceDataID = Cur.ReadU32(bOk);
		Out.BackgroundSurfaceDataID = Cur.ReadU32(bOk);
		return bOk;
	}

	bool UnpackPalette(FACEDatCursor& Cur, FACEDatPalette& Out)
	{
		Out = FACEDatPalette();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		const int32 Count = Cur.ReadI32(bOk);
		if (!bOk || Count < 0)
		{
			return false;
		}
		Out.Colors.SetNum(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			Out.Colors[i] = Cur.ReadU32(bOk);
		}
		return bOk;
	}

	static bool UnpackMotionData(FACEDatCursor& Cur, FACEDatMotionData& Out)
	{
		Out = FACEDatMotionData();
		bool bOk = true;
		const uint8 NumAnims = Cur.ReadU8(bOk);
		Cur.ReadU8(bOk); // Bitfield
		const uint8 Flags = Cur.ReadU8(bOk);
		Cur.AlignBoundary();
		Out.Anims.SetNum(NumAnims);
		for (uint8 i = 0; i < NumAnims; ++i)
		{
			Out.Anims[i].AnimId = Cur.ReadU32(bOk);
			Out.Anims[i].LowFrame = Cur.ReadI32(bOk);
			Out.Anims[i].HighFrame = Cur.ReadI32(bOk);
			Out.Anims[i].Framerate = Cur.ReadF32(bOk);
		}
		if ((Flags & 0x1) != 0) // HasVelocity
		{
			Out.bHasVelocity = true;
			Out.Velocity.X = Cur.ReadF32(bOk); Out.Velocity.Y = Cur.ReadF32(bOk); Out.Velocity.Z = Cur.ReadF32(bOk);
		}
		if ((Flags & 0x2) != 0) // HasOmega
		{
			Out.Omega.X = Cur.ReadF32(bOk); Out.Omega.Y = Cur.ReadF32(bOk); Out.Omega.Z = Cur.ReadF32(bOk);
		}
		return bOk;
	}

	bool UnpackMotionTable(FACEDatCursor& Cur, FACEDatMotionTable& Out)
	{
		Out = FACEDatMotionTable();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.DefaultStyle = Cur.ReadU32(bOk);
		const uint32 NumStyleDefaults = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumStyleDefaults; ++i)
		{
			const uint32 Stance = Cur.ReadU32(bOk);
			const uint32 Motion = Cur.ReadU32(bOk);
			Out.StyleDefaults.Add(Stance, Motion);
		}

		auto UnpackMotionDict = [&](TMap<uint32, FACEDatMotionData>& Dict) -> bool
		{
			const uint32 Count = Cur.ReadU32(bOk);
			if (!bOk)
			{
				return false;
			}
			for (uint32 i = 0; i < Count; ++i)
			{
				const uint32 Key = Cur.ReadU32(bOk);
				FACEDatMotionData MD;
				if (!bOk || !UnpackMotionData(Cur, MD))
				{
					return false;
				}
				Dict.Add(Key, MoveTemp(MD));
			}
			return true;
		};

		if (!UnpackMotionDict(Out.Cycles))
		{
			return false;
		}

		// Modifiers — skip into a temp map
		{
			TMap<uint32, FACEDatMotionData> Modifiers;
			if (!UnpackMotionDict(Modifiers))
			{
				return false;
			}
		}

		// Links: Dict<u32, Dict<u32, MotionData>> — door On/Off transitions live here.
		{
			const uint32 OuterCount = Cur.ReadU32(bOk);
			if (!bOk)
			{
				return false;
			}
			for (uint32 i = 0; i < OuterCount; ++i)
			{
				const uint32 OuterKey = Cur.ReadU32(bOk);
				TMap<uint32, FACEDatMotionData> Inner;
				if (!bOk || !UnpackMotionDict(Inner))
				{
					return false;
				}
				Out.Links.Add(OuterKey, MoveTemp(Inner));
			}
		}

		return bOk;
	}

	bool UnpackAnimation(FACEDatCursor& Cur, FACEDatAnimation& Out)
	{
		Out = FACEDatAnimation();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.Flags = Cur.ReadU32(bOk);
		Out.NumParts = Cur.ReadU32(bOk);
		Out.NumFrames = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}

		if ((Out.Flags & 0x1) != 0) // PosFrames
		{
			for (uint32 i = 0; i < Out.NumFrames; ++i)
			{
				if (!SkipFrame(Cur))
				{
					return false;
				}
			}
		}

		Out.PartFrames.SetNum(static_cast<int32>(Out.NumFrames));
		for (uint32 f = 0; f < Out.NumFrames; ++f)
		{
			FACEDatAnimationFrame& Frame = Out.PartFrames[f];
			Frame.PartFrames.SetNum(static_cast<int32>(Out.NumParts));
			for (uint32 p = 0; p < Out.NumParts; ++p)
			{
				FVector3f Origin;
				FQuat4f Quat;
				if (!Cur.ReadFrame(Origin, Quat))
				{
					return false;
				}
				Frame.PartFrames[p] = FTransform3f(Quat, Origin);
			}
			const uint32 NumHooks = Cur.ReadU32(bOk);
			if (!bOk)
			{
				return false;
			}
			for (uint32 h = 0; h < NumHooks; ++h)
			{
				FACEDatAnimationHook Hook;
				if (!UnpackAnimationHook(Cur, Hook))
				{
					return false;
				}
				Frame.Hooks.Add(MoveTemp(Hook));
			}
		}

		return true;
	}

	bool UnpackParticleEmitterInfo(FACEDatCursor& Cur, FACEDatParticleEmitterInfo& Out)
	{
		Out = FACEDatParticleEmitterInfo();
		const int32 Start = Cur.Pos;
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.Unknown = Cur.ReadU32(bOk);
		Out.EmitterType = Cur.ReadI32(bOk);
		Out.ParticleType = Cur.ReadI32(bOk);
		Out.GfxObjId = Cur.ReadU32(bOk);
		Out.HwGfxObjId = Cur.ReadU32(bOk);
		bOk = bOk && Cur.Read(Out.Birthrate);
		Out.MaxParticles = Cur.ReadI32(bOk);
		Out.InitialParticles = Cur.ReadI32(bOk);
		Out.TotalParticles = Cur.ReadI32(bOk);
		bOk = bOk && Cur.Read(Out.TotalSeconds);
		bOk = bOk && Cur.Read(Out.Lifespan);
		bOk = bOk && Cur.Read(Out.LifespanRand);
		Out.OffsetDir = Cur.ReadVector3(bOk);
		Out.MinOffset = Cur.ReadF32(bOk);
		Out.MaxOffset = Cur.ReadF32(bOk);
		Out.A = Cur.ReadVector3(bOk);
		Out.MinA = Cur.ReadF32(bOk);
		Out.MaxA = Cur.ReadF32(bOk);
		Out.B = Cur.ReadVector3(bOk);
		Out.MinB = Cur.ReadF32(bOk);
		Out.MaxB = Cur.ReadF32(bOk);
		Out.C = Cur.ReadVector3(bOk);
		Out.MinC = Cur.ReadF32(bOk);
		Out.MaxC = Cur.ReadF32(bOk);
		Out.StartScale = Cur.ReadF32(bOk);
		Out.FinalScale = Cur.ReadF32(bOk);
		Out.ScaleRand = Cur.ReadF32(bOk);
		Out.StartTrans = Cur.ReadF32(bOk);
		Out.FinalTrans = Cur.ReadF32(bOk);
		Out.TransRand = Cur.ReadF32(bOk);
		Out.IsParentLocal = Cur.ReadI32(bOk);
		const int32 BytesRead = Cur.Pos - Start;
		if (bOk && BytesRead != 176)
		{
			UE_LOG(LogTemp, Error, TEXT("ACEDat: ParticleEmitterInfo read %d bytes, expected 176"), BytesRead);
			return false;
		}
		return bOk;
	}

	bool UnpackPhysicsScript(FACEDatCursor& Cur, FACEDatPhysicsScript& Out)
	{
		Out = FACEDatPhysicsScript();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		const uint32 Count = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}
		Out.Entries.SetNum(static_cast<int32>(Count));
		for (uint32 i = 0; i < Count; ++i)
		{
			FACEDatPhysicsScriptEntry& Entry = Out.Entries[i];
			if (!Cur.Read(Entry.StartTime) || !UnpackAnimationHook(Cur, Entry.Hook))
			{
				return false;
			}
		}
		return true;
	}

	bool UnpackPhysicsScriptTable(FACEDatCursor& Cur, FACEDatPhysicsScriptTable& Out)
	{
		Out = FACEDatPhysicsScriptTable();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		const uint32 MapCount = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}
		for (uint32 i = 0; i < MapCount; ++i)
		{
			const uint32 Type = Cur.ReadU32(bOk);
			const uint32 CandidateCount = Cur.ReadU32(bOk);
			if (!bOk)
			{
				return false;
			}
			TArray<FACEDatPhysicsScriptCandidate> Candidates;
			Candidates.SetNum(static_cast<int32>(CandidateCount));
			for (uint32 c = 0; c < CandidateCount; ++c)
			{
				Candidates[c].Mod = Cur.ReadF32(bOk);
				Candidates[c].ScriptId = Cur.ReadU32(bOk);
			}
			if (!bOk)
			{
				return false;
			}
			Out.Entries.Add(Type, MoveTemp(Candidates));
		}
		return true;
	}

	bool UnpackWave(FACEDatCursor& Cur, FACEDatWave& Out)
	{
		Out = FACEDatWave();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		const int32 HeaderSize = Cur.ReadI32(bOk);
		const int32 DataSize = Cur.ReadI32(bOk);
		if (!bOk || HeaderSize < 0 || DataSize < 0 || HeaderSize > Cur.Remaining())
		{
			return false;
		}
		Out.Header.SetNumUninitialized(HeaderSize);
		if (!Cur.ReadBytes(Out.Header.GetData(), HeaderSize) || DataSize > Cur.Remaining())
		{
			return false;
		}
		Out.Data.SetNumUninitialized(DataSize);
		return Cur.ReadBytes(Out.Data.GetData(), DataSize);
	}

	static bool UnpackSoundTableEntry(FACEDatCursor& Cur, FACEDatSoundTableEntry& Out)
	{
		bool bOk = true;
		Out.SoundId = Cur.ReadU32(bOk);
		Out.Priority = Cur.ReadF32(bOk);
		Out.Probability = Cur.ReadF32(bOk);
		Out.Volume = Cur.ReadF32(bOk);
		return bOk;
	}

	bool UnpackSoundTable(FACEDatCursor& Cur, FACEDatSoundTable& Out)
	{
		Out = FACEDatSoundTable();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.Unknown = Cur.ReadU32(bOk);
		const uint32 HashCount = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}
		Out.SoundHash.SetNum(static_cast<int32>(HashCount));
		for (uint32 i = 0; i < HashCount; ++i)
		{
			if (!UnpackSoundTableEntry(Cur, Out.SoundHash[i]))
			{
				return false;
			}
		}

		const uint16 PackedCount = Cur.ReadU16(bOk);
		Out.PackedBucketSize = Cur.ReadU16(bOk);
		if (!bOk)
		{
			return false;
		}
		for (uint16 i = 0; i < PackedCount; ++i)
		{
			const uint32 Key = Cur.ReadU32(bOk);
			const uint32 EntryCount = Cur.ReadU32(bOk);
			if (!bOk)
			{
				return false;
			}
			FACEDatSoundData Value;
			Value.Entries.SetNum(static_cast<int32>(EntryCount));
			for (uint32 e = 0; e < EntryCount; ++e)
			{
				if (!UnpackSoundTableEntry(Cur, Value.Entries[e]))
				{
					return false;
				}
			}
			Value.Unknown = Cur.ReadU32(bOk);
			if (!bOk)
			{
				return false;
			}
			Out.Data.Add(Key, MoveTemp(Value));
		}
		return true;
	}

	bool UnpackCellLandblock(FACEDatCursor& Cur, FACEDatCellLandblock& Out)
	{
		Out = FACEDatCellLandblock();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.bHasObjects = (Cur.ReadU32(bOk) == 1);
		for (int32 i = 0; i < 81; ++i)
		{
			Out.Terrain[i] = Cur.ReadU16(bOk);
		}
		for (int32 i = 0; i < 81; ++i)
		{
			Out.Height[i] = Cur.ReadU8(bOk);
		}
		Cur.AlignBoundary();
		return bOk;
	}

	bool UnpackLandblockInfo(FACEDatCursor& Cur, FACEDatLandblockInfo& Out)
	{
		Out = FACEDatLandblockInfo();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.NumCells = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}

		const uint32 NumObjects = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}
		Out.Objects.Reserve(static_cast<int32>(NumObjects));
		for (uint32 i = 0; i < NumObjects && bOk; ++i)
		{
			FACEDatLandblockStab Stab;
			Stab.Id = Cur.ReadU32(bOk);
			if (!bOk || !Cur.ReadFrame(Stab.Origin, Stab.Orientation))
			{
				return false;
			}
			Out.Objects.Add(Stab);
		}

		const uint16 NumBuildings = Cur.ReadU16(bOk);
		const uint16 PackMask = Cur.ReadU16(bOk);
		if (!bOk)
		{
			return false;
		}
		Out.Buildings.Reserve(NumBuildings);
		for (uint16 b = 0; b < NumBuildings && bOk; ++b)
		{
			FACEDatLandblockBuilding Building;
			Building.ModelId = Cur.ReadU32(bOk);
			if (!bOk || !Cur.ReadFrame(Building.Origin, Building.Orientation))
			{
				return false;
			}
			Cur.ReadU32(bOk); // NumLeaves
			const uint32 NumPortals = Cur.ReadU32(bOk);
			for (uint32 p = 0; p < NumPortals && bOk; ++p)
			{
				FACEDatBuildingPortal Portal;
				Portal.Flags = Cur.ReadU16(bOk);
				Portal.OtherCellId = Cur.ReadU16(bOk);
				Portal.OtherPortalId = Cur.ReadU16(bOk);
				const uint16 NumStabs = Cur.ReadU16(bOk);
				const uint16 OtherCellId = Portal.OtherCellId;
				if (OtherCellId != 0 && OtherCellId != 0xFFFFu)
				{
					Building.PortalCellIds.AddUnique(OtherCellId);
				}
				for (uint16 s = 0; s < NumStabs && bOk; ++s)
				{
					const uint16 StabCell = Cur.ReadU16(bOk);
					if (StabCell != 0 && StabCell != 0xFFFFu)
					{
						Portal.StabCells.AddUnique(StabCell);
						Building.PortalCellIds.AddUnique(StabCell);
					}
				}
				Building.Portals.Add(MoveTemp(Portal));
				Cur.AlignBoundary();
			}
			if (!bOk)
			{
				return false;
			}
			Out.Buildings.Add(Building);
		}

		// Restriction table (optional) — skip so we don't leave the cursor mid-file.
		if ((PackMask & 1) != 0)
		{
			const uint16 TableSize = Cur.ReadU16(bOk);
			Cur.ReadU16(bOk); // bucket size
			for (uint16 i = 0; i < TableSize && bOk; ++i)
			{
				Cur.ReadU32(bOk);
				Cur.ReadU32(bOk);
			}
		}
		return bOk;
	}

	bool UnpackRegionLandHeightTable(FACEDatCursor& Cur, TArray<float>& OutTable256)
	{
		OutTable256.Reset();
		bool bOk = true;
		Cur.ReadU32(bOk); // Id
		Cur.ReadU32(bOk); // RegionNumber
		Cur.ReadU32(bOk); // Version
		Cur.ReadPString(bOk);
		Cur.AlignBoundary();

		// LandDefs header floats/ints then 256 height table
		Cur.Skip(sizeof(int32) * 2);      // NumBlockLength/Width
		Cur.Skip(sizeof(float));          // SquareLength
		Cur.Skip(sizeof(int32) * 2);      // LBlockLength, VertexPerCell
		Cur.Skip(sizeof(float) * 3);      // MaxObjHeight, SkyHeight, RoadWidth
		OutTable256.SetNum(256);
		for (int32 i = 0; i < 256; ++i)
		{
			OutTable256[i] = Cur.ReadF32(bOk);
		}
		return bOk;
	}

	bool UnpackRegionTexMerge(FACEDatCursor& Cur, FACEDatTexMerge& Out)
	{
		Out = FACEDatTexMerge();
		bool bOk = true;
		Cur.ReadU32(bOk); // Id
		Cur.ReadU32(bOk); // RegionNumber
		Cur.ReadU32(bOk); // Version
		Cur.ReadPString(bOk);
		Cur.AlignBoundary();
		if (!bOk)
		{
			return false;
		}

		// LandDefs
		Cur.Skip(sizeof(int32) * 2);
		Cur.Skip(sizeof(float));
		Cur.Skip(sizeof(int32) * 2);
		Cur.Skip(sizeof(float) * 3);
		Cur.Skip(sizeof(float) * 256); // LandHeightTable

		// GameTime
		if (!Cur.Skip(8)) // ZeroTimeOfYear double
		{
			return false;
		}
		Cur.ReadU32(bOk); // ZeroYear
		Cur.ReadF32(bOk); // DayLength
		Cur.ReadU32(bOk); // DaysPerYear
		Cur.ReadPString(bOk);
		Cur.AlignBoundary();

		const uint32 NumTimesOfDay = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumTimesOfDay && bOk; ++i)
		{
			Cur.ReadF32(bOk);
			Cur.ReadU32(bOk); // IsNight
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}

		const uint32 NumDaysOfWeek = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumDaysOfWeek && bOk; ++i)
		{
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}

		const uint32 NumSeasons = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumSeasons && bOk; ++i)
		{
			Cur.ReadU32(bOk);
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		if (!bOk)
		{
			return false;
		}

		const uint32 PartsMask = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}

		auto SkipListU32 = [&]() -> bool
		{
			const uint32 N = Cur.ReadU32(bOk);
			return bOk && Cur.Skip(static_cast<int32>(N) * 4);
		};

		if ((PartsMask & 0x10) != 0)
		{
			// SkyDesc
			Cur.Skip(16); // TickSize + LightTickSize doubles
			Cur.AlignBoundary();
			const uint32 NumDayGroups = Cur.ReadU32(bOk);
			for (uint32 g = 0; g < NumDayGroups && bOk; ++g)
			{
				Cur.ReadF32(bOk);
				Cur.ReadPString(bOk);
				Cur.AlignBoundary();
				const uint32 NumSkyObj = Cur.ReadU32(bOk);
				for (uint32 s = 0; s < NumSkyObj && bOk; ++s)
				{
					Cur.Skip(6 * 4); // 6 floats
					Cur.Skip(3 * 4); // 3 uints
					Cur.AlignBoundary();
				}
				const uint32 NumSkyTime = Cur.ReadU32(bOk);
				for (uint32 t = 0; t < NumSkyTime && bOk; ++t)
				{
					Cur.Skip(4); // Begin
					Cur.Skip(3 * 4); // DirBright/Heading/Pitch
					Cur.Skip(4); // DirColor
					Cur.Skip(4); // AmbBright
					Cur.Skip(4); // AmbColor
					Cur.Skip(2 * 4); // Min/MaxWorldFog
					Cur.Skip(2 * 4); // WorldFogColor/WorldFog
					Cur.AlignBoundary();
					const uint32 NumReplace = Cur.ReadU32(bOk);
					for (uint32 r = 0; r < NumReplace && bOk; ++r)
					{
						Cur.Skip(2 * 4); // ObjectIndex + GFXObjId
						Cur.Skip(4 * 4); // 4 floats
						Cur.AlignBoundary();
					}
				}
			}
		}

		if ((PartsMask & 0x01) != 0)
		{
			const uint32 NumStb = Cur.ReadU32(bOk);
			for (uint32 i = 0; i < NumStb && bOk; ++i)
			{
				Cur.ReadU32(bOk); // STBId
				const uint32 NumSounds = Cur.ReadU32(bOk);
				if (!bOk || !Cur.Skip(static_cast<int32>(NumSounds) * 20))
				{
					return false;
				}
			}
		}

		if ((PartsMask & 0x02) != 0)
		{
			const uint32 NumSceneTypes = Cur.ReadU32(bOk);
			for (uint32 i = 0; i < NumSceneTypes && bOk; ++i)
			{
				Cur.ReadU32(bOk); // StbIndex
				if (!SkipListU32())
				{
					return false;
				}
			}
		}
		if (!bOk)
		{
			return false;
		}

		// TerrainDesc: TerrainTypes list + LandSurf
		const uint32 NumTerrainTypes = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumTerrainTypes && bOk; ++i)
		{
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
			Cur.ReadU32(bOk); // TerrainColor
			if (!SkipListU32())
			{
				return false;
			}
		}

		const uint32 LandSurfType = Cur.ReadU32(bOk);
		if (!bOk || LandSurfType == 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("ACEDat: LandSurf Type=%u not supported"), LandSurfType);
			return false;
		}

		Out.BaseTexSize = Cur.ReadU32(bOk);

		auto ReadAlphaMaps = [&](TArray<FACEDatTerrainAlphaMap>& Dest) -> bool
		{
			const uint32 N = Cur.ReadU32(bOk);
			Dest.SetNum(static_cast<int32>(N));
			for (uint32 i = 0; i < N && bOk; ++i)
			{
				Dest[i].TCode = Cur.ReadU32(bOk);
				Dest[i].TexGID = Cur.ReadU32(bOk);
			}
			return bOk;
		};

		if (!ReadAlphaMaps(Out.CornerTerrainMaps) || !ReadAlphaMaps(Out.SideTerrainMaps))
		{
			return false;
		}

		const uint32 NumRoads = Cur.ReadU32(bOk);
		Out.RoadMaps.SetNum(static_cast<int32>(NumRoads));
		for (uint32 i = 0; i < NumRoads && bOk; ++i)
		{
			Out.RoadMaps[i].RCode = Cur.ReadU32(bOk);
			Out.RoadMaps[i].RoadTexGID = Cur.ReadU32(bOk);
		}

		const uint32 NumTM = Cur.ReadU32(bOk);
		Out.TerrainDesc.SetNum(static_cast<int32>(NumTM));
		for (uint32 i = 0; i < NumTM && bOk; ++i)
		{
			Out.TerrainDesc[i].TerrainType = Cur.ReadU32(bOk);
			FACEDatTerrainTex& T = Out.TerrainDesc[i].TerrainTex;
			T.TexGID = Cur.ReadU32(bOk);
			T.TexTiling = Cur.ReadU32(bOk);
			Cur.Skip(6 * 4); // Max/Min Vert Bright/Saturate/Hue
			T.DetailTexTiling = Cur.ReadU32(bOk);
			T.DetailTexGID = Cur.ReadU32(bOk);
		}

		if (bOk)
		{
			UE_LOG(LogTemp, Log, TEXT("ACEDat: TexMerge BaseTexSize=%u terrains=%d corners=%d sides=%d roads=%d"),
				Out.BaseTexSize, Out.TerrainDesc.Num(), Out.CornerTerrainMaps.Num(),
				Out.SideTerrainMaps.Num(), Out.RoadMaps.Num());
		}
		return bOk;
	}

	bool UnpackScene(FACEDatCursor& Cur, FACEDatScene& Out)
	{
		Out = FACEDatScene();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		const uint32 NumObjects = Cur.ReadU32(bOk);
		Out.Objects.SetNum(static_cast<int32>(NumObjects));
		for (uint32 i = 0; i < NumObjects && bOk; ++i)
		{
			FACEDatSceneObjectDesc& Obj = Out.Objects[i];
			Obj.ObjId = Cur.ReadU32(bOk);
			Obj.Origin.X = Cur.ReadF32(bOk);
			Obj.Origin.Y = Cur.ReadF32(bOk);
			Obj.Origin.Z = Cur.ReadF32(bOk);
			const float Qw = Cur.ReadF32(bOk);
			const float Qx = Cur.ReadF32(bOk);
			const float Qy = Cur.ReadF32(bOk);
			const float Qz = Cur.ReadF32(bOk);
			Obj.Orientation = FQuat(Qx, Qy, Qz, Qw);
			Obj.Freq = Cur.ReadF32(bOk);
			Obj.DisplaceX = Cur.ReadF32(bOk);
			Obj.DisplaceY = Cur.ReadF32(bOk);
			Obj.MinScale = Cur.ReadF32(bOk);
			Obj.MaxScale = Cur.ReadF32(bOk);
			Obj.MaxRotation = Cur.ReadF32(bOk);
			Obj.MinSlope = Cur.ReadF32(bOk);
			Obj.MaxSlope = Cur.ReadF32(bOk);
			Obj.Align = Cur.ReadU32(bOk);
			Obj.Orient = Cur.ReadU32(bOk);
			Obj.WeenieObj = Cur.ReadU32(bOk);
		}
		return bOk;
	}

	bool UnpackRegionSceneTables(FACEDatCursor& Cur, FACEDatRegionSceneTables& Out)
	{
		Out = FACEDatRegionSceneTables();
		bool bOk = true;
		Cur.ReadU32(bOk); // Id
		Cur.ReadU32(bOk); // RegionNumber
		Cur.ReadU32(bOk); // Version
		Cur.ReadPString(bOk);
		Cur.AlignBoundary();
		if (!bOk)
		{
			return false;
		}

		Cur.Skip(sizeof(int32) * 2);
		Cur.Skip(sizeof(float));
		Cur.Skip(sizeof(int32) * 2);
		Cur.Skip(sizeof(float) * 3);
		Cur.Skip(sizeof(float) * 256);

		if (!Cur.Skip(8))
		{
			return false;
		}
		Cur.ReadU32(bOk);
		Cur.ReadF32(bOk);
		Cur.ReadU32(bOk);
		Cur.ReadPString(bOk);
		Cur.AlignBoundary();

		const uint32 NumTimesOfDay = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumTimesOfDay && bOk; ++i)
		{
			Cur.ReadF32(bOk);
			Cur.ReadU32(bOk);
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		const uint32 NumDaysOfWeek = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumDaysOfWeek && bOk; ++i)
		{
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		const uint32 NumSeasons = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumSeasons && bOk; ++i)
		{
			Cur.ReadU32(bOk);
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		if (!bOk)
		{
			return false;
		}

		const uint32 PartsMask = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}

		auto SkipListU32 = [&]() -> bool
		{
			const uint32 N = Cur.ReadU32(bOk);
			return bOk && Cur.Skip(static_cast<int32>(N) * 4);
		};

		if ((PartsMask & 0x10) != 0)
		{
			Cur.Skip(16);
			Cur.AlignBoundary();
			const uint32 NumDayGroups = Cur.ReadU32(bOk);
			for (uint32 g = 0; g < NumDayGroups && bOk; ++g)
			{
				Cur.ReadF32(bOk);
				Cur.ReadPString(bOk);
				Cur.AlignBoundary();
				const uint32 NumSkyObj = Cur.ReadU32(bOk);
				for (uint32 s = 0; s < NumSkyObj && bOk; ++s)
				{
					Cur.Skip(6 * 4);
					Cur.Skip(3 * 4);
					Cur.AlignBoundary();
				}
				const uint32 NumSkyTime = Cur.ReadU32(bOk);
				for (uint32 t = 0; t < NumSkyTime && bOk; ++t)
				{
					Cur.Skip(4);
					Cur.Skip(3 * 4);
					Cur.Skip(4);
					Cur.Skip(4);
					Cur.Skip(4);
					Cur.Skip(2 * 4);
					Cur.Skip(2 * 4);
					Cur.AlignBoundary();
					const uint32 NumReplace = Cur.ReadU32(bOk);
					for (uint32 r = 0; r < NumReplace && bOk; ++r)
					{
						Cur.Skip(2 * 4);
						Cur.Skip(4 * 4);
						Cur.AlignBoundary();
					}
				}
			}
		}

		if ((PartsMask & 0x01) != 0)
		{
			const uint32 NumStb = Cur.ReadU32(bOk);
			for (uint32 i = 0; i < NumStb && bOk; ++i)
			{
				Cur.ReadU32(bOk); // STBId
				const uint32 NumSounds = Cur.ReadU32(bOk);
				if (!bOk || !Cur.Skip(static_cast<int32>(NumSounds) * 20))
				{
					return false;
				}
			}
		}

		if ((PartsMask & 0x02) != 0)
		{
			const uint32 NumSceneTypes = Cur.ReadU32(bOk);
			Out.SceneTypes.SetNum(static_cast<int32>(NumSceneTypes));
			Out.SceneTypeStbIndices.SetNum(static_cast<int32>(NumSceneTypes));
			for (uint32 i = 0; i < NumSceneTypes && bOk; ++i)
			{
				Out.SceneTypeStbIndices[i] = Cur.ReadU32(bOk); // StbIndex
				const uint32 NumScenes = Cur.ReadU32(bOk);
				Out.SceneTypes[i].SetNum(static_cast<int32>(NumScenes));
				for (uint32 s = 0; s < NumScenes && bOk; ++s)
				{
					Out.SceneTypes[i][s] = Cur.ReadU32(bOk);
				}
			}
		}

		const uint32 NumTerrainTypes = Cur.ReadU32(bOk);
		Out.TerrainSceneTypeIndices.SetNum(static_cast<int32>(NumTerrainTypes));
		for (uint32 i = 0; i < NumTerrainTypes && bOk; ++i)
		{
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
			Cur.ReadU32(bOk); // TerrainColor
			const uint32 NumIdx = Cur.ReadU32(bOk);
			Out.TerrainSceneTypeIndices[i].SetNum(static_cast<int32>(NumIdx));
			for (uint32 s = 0; s < NumIdx && bOk; ++s)
			{
				Out.TerrainSceneTypeIndices[i][s] = Cur.ReadU32(bOk);
			}
		}

		Out.bValid = bOk && Out.SceneTypes.Num() > 0 && Out.TerrainSceneTypeIndices.Num() > 0;
		return Out.bValid;
	}

	bool UnpackRegionSoundInfo(FACEDatCursor& Cur, FACEDatRegionSoundInfo& Out)
	{
		Out = FACEDatRegionSoundInfo();
		bool bOk = true;
		Cur.ReadU32(bOk); // Id
		Cur.ReadU32(bOk); // RegionNumber
		Cur.ReadU32(bOk); // Version
		Cur.ReadPString(bOk);
		Cur.AlignBoundary();
		if (!bOk)
		{
			return false;
		}

		Cur.Skip(sizeof(int32) * 2);
		Cur.Skip(sizeof(float));
		Cur.Skip(sizeof(int32) * 2);
		Cur.Skip(sizeof(float) * 3);
		Cur.Skip(sizeof(float) * 256);

		if (!Cur.Skip(8))
		{
			return false;
		}
		Cur.ReadU32(bOk);
		Cur.ReadF32(bOk);
		Cur.ReadU32(bOk);
		Cur.ReadPString(bOk);
		Cur.AlignBoundary();

		const uint32 NumTimesOfDay = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumTimesOfDay && bOk; ++i)
		{
			Cur.ReadF32(bOk);
			Cur.ReadU32(bOk);
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		const uint32 NumDaysOfWeek = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumDaysOfWeek && bOk; ++i)
		{
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		const uint32 NumSeasons = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumSeasons && bOk; ++i)
		{
			Cur.ReadU32(bOk);
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		if (!bOk)
		{
			return false;
		}

		const uint32 PartsMask = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}

		if ((PartsMask & 0x10) != 0)
		{
			Cur.Skip(16);
			Cur.AlignBoundary();
			const uint32 NumDayGroups = Cur.ReadU32(bOk);
			for (uint32 g = 0; g < NumDayGroups && bOk; ++g)
			{
				Cur.ReadF32(bOk);
				Cur.ReadPString(bOk);
				Cur.AlignBoundary();
				const uint32 NumSkyObj = Cur.ReadU32(bOk);
				for (uint32 s = 0; s < NumSkyObj && bOk; ++s)
				{
					Cur.Skip(6 * 4);
					Cur.Skip(3 * 4);
					Cur.AlignBoundary();
				}
				const uint32 NumSkyTime = Cur.ReadU32(bOk);
				for (uint32 t = 0; t < NumSkyTime && bOk; ++t)
				{
					Cur.Skip(4);
					Cur.Skip(3 * 4);
					Cur.Skip(4);
					Cur.Skip(4);
					Cur.Skip(4);
					Cur.Skip(2 * 4);
					Cur.Skip(2 * 4);
					Cur.AlignBoundary();
					const uint32 NumReplace = Cur.ReadU32(bOk);
					for (uint32 r = 0; r < NumReplace && bOk; ++r)
					{
						Cur.Skip(2 * 4);
						Cur.Skip(4 * 4);
						Cur.AlignBoundary();
					}
				}
			}
		}

		if ((PartsMask & 0x01) == 0)
		{
			return false;
		}

		const uint32 NumStb = Cur.ReadU32(bOk);
		if (!bOk || NumStb > 512)
		{
			return false;
		}
		Out.STBDescs.SetNum(static_cast<int32>(NumStb));
		for (uint32 i = 0; i < NumStb && bOk; ++i)
		{
			FACEDatAmbientSTBDesc& Stb = Out.STBDescs[i];
			Stb.SoundTableId = Cur.ReadU32(bOk);
			const uint32 NumSounds = Cur.ReadU32(bOk);
			if (!bOk || NumSounds > 256)
			{
				return false;
			}
			Stb.Sounds.SetNum(static_cast<int32>(NumSounds));
			for (uint32 s = 0; s < NumSounds && bOk; ++s)
			{
				FACEDatAmbientSoundDesc& Amb = Stb.Sounds[s];
				Amb.SoundType = Cur.ReadU32(bOk);
				Amb.Volume = Cur.ReadF32(bOk);
				Amb.BaseChance = Cur.ReadF32(bOk);
				Amb.MinRate = Cur.ReadF32(bOk);
				Amb.MaxRate = Cur.ReadF32(bOk);
			}
		}

		Out.bValid = bOk && Out.STBDescs.Num() > 0;
		return Out.bValid;
	}

	bool UnpackRegionSkyInfo(FACEDatCursor& Cur, FACEDatRegionSky& Out)
	{
		Out = FACEDatRegionSky();
		bool bOk = true;
		Cur.ReadU32(bOk); // Id
		Cur.ReadU32(bOk); // RegionNumber
		Cur.ReadU32(bOk); // Version
		Cur.ReadPString(bOk);
		Cur.AlignBoundary();
		if (!bOk)
		{
			return false;
		}

		// LandDefs
		Cur.Skip(sizeof(int32) * 2);
		Cur.Skip(sizeof(float));
		Cur.Skip(sizeof(int32) * 2);
		Cur.Skip(sizeof(float) * 3);
		Cur.Skip(sizeof(float) * 256);

		// GameTime — keep ZeroTimeOfYear + DayLength for sky phase math.
		double ZeroTimeOfYear = 0.0;
		if (!Cur.Read(ZeroTimeOfYear))
		{
			return false;
		}
		const uint32 ZeroYear = Cur.ReadU32(bOk);
		const float DayLength = Cur.ReadF32(bOk);
		const uint32 DaysPerYear = Cur.ReadU32(bOk);
		Cur.ReadPString(bOk); // YearSpec
		Cur.AlignBoundary();
		if (bOk)
		{
			Out.ZeroTimeOfYear = ZeroTimeOfYear;
			Out.ZeroYear = ZeroYear;
		}
		if (bOk && DayLength > 1.f)
		{
			Out.DayLengthSeconds = DayLength;
		}
		if (bOk && DaysPerYear > 0)
		{
			Out.DaysPerYear = DaysPerYear;
		}

		const uint32 NumTimesOfDay = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumTimesOfDay && bOk; ++i)
		{
			Cur.ReadF32(bOk);
			Cur.ReadU32(bOk); // IsNight
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		const uint32 NumDaysOfWeek = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumDaysOfWeek && bOk; ++i)
		{
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		const uint32 NumSeasons = Cur.ReadU32(bOk);
		for (uint32 i = 0; i < NumSeasons && bOk; ++i)
		{
			Cur.ReadU32(bOk);
			Cur.ReadPString(bOk);
			Cur.AlignBoundary();
		}
		if (!bOk)
		{
			return false;
		}

		const uint32 PartsMask = Cur.ReadU32(bOk);
		if (!bOk || (PartsMask & 0x10) == 0)
		{
			return false;
		}

		// SkyDesc
		Cur.Skip(16); // TickSize + LightTickSize doubles
		Cur.AlignBoundary();
		const uint32 NumDayGroups = Cur.ReadU32(bOk);
		if (!bOk || NumDayGroups > 256)
		{
			return false;
		}
		Out.DayGroups.SetNum(static_cast<int32>(NumDayGroups));
		for (uint32 g = 0; g < NumDayGroups && bOk; ++g)
		{
			FACEDatSkyDayGroup& Group = Out.DayGroups[g];
			Group.ChanceOfOccur = Cur.ReadF32(bOk);
			Group.DayName = Cur.ReadPString(bOk);
			Cur.AlignBoundary();

			const uint32 NumSkyObj = Cur.ReadU32(bOk);
			if (!bOk || NumSkyObj > 256)
			{
				return false;
			}
			Group.Objects.SetNum(static_cast<int32>(NumSkyObj));
			for (uint32 s = 0; s < NumSkyObj && bOk; ++s)
			{
				FACEDatSkyObject& Obj = Group.Objects[s];
				Obj.BeginTime = Cur.ReadF32(bOk);
				Obj.EndTime = Cur.ReadF32(bOk);
				Obj.BeginAngle = Cur.ReadF32(bOk);
				Obj.EndAngle = Cur.ReadF32(bOk);
				Obj.TexVelocityX = Cur.ReadF32(bOk);
				Obj.TexVelocityY = Cur.ReadF32(bOk);
				Obj.DefaultGfxObjectId = Cur.ReadU32(bOk);
				Obj.DefaultPesObjectId = Cur.ReadU32(bOk);
				Obj.Properties = Cur.ReadU32(bOk);
				Cur.AlignBoundary();
			}

			const uint32 NumSkyTime = Cur.ReadU32(bOk);
			if (!bOk || NumSkyTime > 256)
			{
				return false;
			}
			Group.TimesOfDay.SetNum(static_cast<int32>(NumSkyTime));
			for (uint32 t = 0; t < NumSkyTime && bOk; ++t)
			{
				FACEDatSkyTimeOfDay& Time = Group.TimesOfDay[t];
				Time.Begin = Cur.ReadF32(bOk);
				Time.DirBright = Cur.ReadF32(bOk);
				Time.DirHeading = Cur.ReadF32(bOk);
				Time.DirPitch = Cur.ReadF32(bOk);
				Time.DirColor = Cur.ReadU32(bOk);
				Time.AmbBright = Cur.ReadF32(bOk);
				Time.AmbColor = Cur.ReadU32(bOk);
				Time.MinWorldFog = Cur.ReadF32(bOk);
				Time.MaxWorldFog = Cur.ReadF32(bOk);
				Time.WorldFogColor = Cur.ReadU32(bOk);
				Time.WorldFog = Cur.ReadU32(bOk);
				Cur.AlignBoundary();

				const uint32 NumReplace = Cur.ReadU32(bOk);
				if (!bOk || NumReplace > 256)
				{
					return false;
				}
				Time.Replaces.SetNum(static_cast<int32>(NumReplace));
				for (uint32 r = 0; r < NumReplace && bOk; ++r)
				{
					FACEDatSkyObjectReplace& Rep = Time.Replaces[r];
					Rep.ObjectIndex = Cur.ReadU32(bOk);
					Rep.GfxObjId = Cur.ReadU32(bOk);
					Rep.Rotate = Cur.ReadF32(bOk);
					Rep.Transparent = Cur.ReadF32(bOk);
					Rep.Luminosity = Cur.ReadF32(bOk);
					Rep.MaxBright = Cur.ReadF32(bOk);
					Cur.AlignBoundary();
				}
			}
		}

		Out.bValid = bOk && Out.DayGroups.Num() > 0;
		return Out.bValid;
	}

	// --- Indoor EnvCell / Environment / CellStruct ------------------------------------------
	// Mirrors ACE.DatLoader.Entity.CellStruct.Unpack exactly, including BSP trees that we parse
	// only to skip (CellBSP is BSPType.Cell, PhysicsBSP is BSPType.Physics, and the optional
	// DrawingBSP is BSPType.Drawing) — getting these tree-type-dependent trailing fields wrong
	// desyncs the stream for every polygon/vertex read that follows, same failure mode as the
	// GfxObj drawing BSP bug described in SkipBspTree above.
	bool UnpackCellStruct(FACEDatCursor& Cur, FACEDatCellStruct& Out)
	{
		Out = FACEDatCellStruct();
		bool bOk = true;
		const uint32 NumPolygons = Cur.ReadU32(bOk);
		const uint32 NumPhysicsPolygons = Cur.ReadU32(bOk);
		const uint32 NumPortals = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}

		// CVertexArray: VertexType (must be 1) + count, then a Dictionary<u16, SWVertex> with a
		// fixed count (key + SWVertex::Unpack per entry, no additional length prefix).
		const int32 VertexType = Cur.ReadI32(bOk);
		const uint32 NumVertices = Cur.ReadU32(bOk);
		if (!bOk || VertexType != 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("ACEDat: CellStruct unsupported VertexType %d"), VertexType);
			return false;
		}
		for (uint32 i = 0; i < NumVertices; ++i)
		{
			const uint16 Key = Cur.ReadU16(bOk);
			FACEDatSWVertex Vert;
			if (!bOk || !UnpackSWVertex(Cur, Vert))
			{
				return false;
			}
			Out.Vertices.Add(Key, MoveTemp(Vert));
		}

		// Polygons: Dictionary<u16, Polygon> with fixed count NumPolygons (key + Polygon::Unpack,
		// no per-dictionary length prefix — same shared Polygon type as GfxObj).
		for (uint32 i = 0; i < NumPolygons; ++i)
		{
			const uint16 Key = Cur.ReadU16(bOk);
			FACEDatPolygon Poly;
			if (!bOk || !UnpackPolygon(Cur, Poly))
			{
				return false;
			}
			Out.Polygons.Add(Key, MoveTemp(Poly));
		}

		// Portals: List<u16>, fixed count — not needed for drawing, skip.
		if (!Cur.Skip(static_cast<int32>(NumPortals) * 2))
		{
			return false;
		}

		Cur.AlignBoundary();

		// CellBSP (BSPType.Cell) — keep for EnvCell.point_in_cell (retail CellStruct.point_in_cell).
		Out.CellBspNodes.Reset();
		if (!UnpackCellBspTree(Cur, Out.CellBspNodes))
		{
			return false;
		}

		// PhysicsPolygons: Dictionary<u16, Polygon>, fixed count — retail indoor walk collision.
		for (uint32 i = 0; i < NumPhysicsPolygons; ++i)
		{
			const uint16 Key = Cur.ReadU16(bOk);
			FACEDatPolygon Poly;
			if (!bOk || !UnpackPolygon(Cur, Poly))
			{
				return false;
			}
			Out.PhysicsPolygons.Add(Key, MoveTemp(Poly));
		}

		// PhysicsBSP (BSPType.Physics) — collect leaf poly ids for collision mesh.
		if (!SkipBspTree(Cur, EACEBspType::Physics, 0, nullptr, &Out.PhysicsCollisionPolyIds))
		{
			return false;
		}

		// Optional DrawingBSP (BSPType.Drawing) — present only if the following u32 flag is non-zero.
		const uint32 HasDrawingBSP = Cur.ReadU32(bOk);
		if (!bOk)
		{
			return false;
		}
		if (HasDrawingBSP != 0 && !SkipBspTree(Cur, EACEBspType::Drawing, 0, &Out.DrawingPortalPolygonIds, nullptr, nullptr, &Out.DrawingPolygonIds))
		{
			return false;
		}

		Cur.AlignBoundary();
		return true;
	}

	bool UnpackEnvironment(FACEDatCursor& Cur, FACEDatEnvironment& Out)
	{
		Out = FACEDatEnvironment();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		const uint32 NumCells = Cur.ReadU32(bOk); // Dictionary<uint,T>.Unpack — u32 count prefix
		if (!bOk)
		{
			return false;
		}
		Out.Cells.Reserve(static_cast<int32>(NumCells));
		for (uint32 i = 0; i < NumCells; ++i)
		{
			const uint32 Key = Cur.ReadU32(bOk);
			if (!bOk)
			{
				return false;
			}
			FACEDatCellStruct CS;
			if (!UnpackCellStruct(Cur, CS))
			{
				return false;
			}
			Out.Cells.Add(Key, MoveTemp(CS));
		}
		return true;
	}

	bool UnpackEnvCell(FACEDatCursor& Cur, FACEDatEnvCell& Out)
	{
		Out = FACEDatEnvCell();
		bool bOk = true;
		Out.Id = Cur.ReadU32(bOk);
		Out.Flags = static_cast<EACEEnvCellFlags>(Cur.ReadU32(bOk));
		if (!bOk || !Cur.Skip(4)) // duplicate CellId dword (client_cell.dat quirk, see EnvCell.cs)
		{
			return false;
		}

		const uint8 NumSurfaces = Cur.ReadU8(bOk);
		const uint8 NumPortals = Cur.ReadU8(bOk);
		const uint16 NumStabs = Cur.ReadU16(bOk); // VisibleCells count, despite the name
		if (!bOk)
		{
			return false;
		}

		Out.Surfaces.SetNum(NumSurfaces);
		for (uint8 i = 0; i < NumSurfaces; ++i)
		{
			Out.Surfaces[i] = 0x08000000u | Cur.ReadU16(bOk);
		}
		if (!bOk)
		{
			return false;
		}

		Out.EnvironmentId = 0x0D000000u | Cur.ReadU16(bOk);
		Out.CellStructure = Cur.ReadU16(bOk);
		if (!bOk)
		{
			return false;
		}

		if (!Cur.ReadFrame(Out.Origin, Out.Orientation))
		{
			return false;
		}

		Out.CellPortals.SetNum(NumPortals);
		for (uint8 i = 0; i < NumPortals; ++i)
		{
			FACEDatCellPortal& Portal = Out.CellPortals[i];
			Portal.Flags = Cur.ReadU16(bOk);
			Portal.PolygonId = Cur.ReadU16(bOk);
			Portal.OtherCellId = Cur.ReadU16(bOk);
			Portal.OtherPortalId = Cur.ReadU16(bOk);
		}
		if (!bOk)
		{
			return false;
		}

		Out.VisibleCells.SetNum(NumStabs);
		for (uint16 i = 0; i < NumStabs; ++i)
		{
			Out.VisibleCells[i] = Cur.ReadU16(bOk);
		}
		if (!bOk)
		{
			return false;
		}

		if (EnumHasAnyFlags(Out.Flags, EACEEnvCellFlags::HasStaticObjs))
		{
			const uint32 NumStaticObjs = Cur.ReadU32(bOk); // List<Stab>.Unpack — u32 count prefix
			if (!bOk)
			{
				return false;
			}
			Out.StaticObjects.SetNum(static_cast<int32>(NumStaticObjs));
			for (uint32 i = 0; i < NumStaticObjs; ++i)
			{
				FACEDatStab& Stab = Out.StaticObjects[i];
				Stab.Id = Cur.ReadU32(bOk);
				if (!bOk || !Cur.ReadFrame(Stab.Origin, Stab.Orientation))
				{
					return false;
				}
			}
		}

		if (EnumHasAnyFlags(Out.Flags, EACEEnvCellFlags::HasRestrictionObj))
		{
			Out.RestrictionObj = Cur.ReadU32(bOk);
		}

		return bOk;
	}
} // namespace ACEDatUnpack
