#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "Dat/ACEDatCursor.h"

enum class EACEGfxObjFlags : uint32
{
	HasPhysics = 0x1,
	HasDrawing = 0x2,
	Unknown = 0x4,
	HasDIDDegrade = 0x8
};
ENUM_CLASS_FLAGS(EACEGfxObjFlags)

enum class EACESetupFlags : uint32
{
	HasParent = 0x1,
	HasDefaultScale = 0x2,
	AllowFreeHeading = 0x4,
	HasPhysicsBSP = 0x8
};
ENUM_CLASS_FLAGS(EACESetupFlags)

enum class EACEBspType : uint8
{
	Drawing = 0,
	Physics = 1,
	Cell = 2
};

enum class EACECullMode : int32
{
	Landblock = 0,
	None = 1,
	Clockwise = 2,
	CounterClockwise = 3
};

enum class EACEStipplingType : uint8
{
	None = 0x0,
	Positive = 0x1,
	Negative = 0x2,
	Both = 0x3,
	NoPos = 0x4,
	NoNeg = 0x8,
	NoUVS = 0x14
};
ENUM_CLASS_FLAGS(EACEStipplingType)

struct FACEDatSWVertex
{
	FVector3f Origin = FVector3f::ZeroVector;
	FVector3f Normal = FVector3f::ZeroVector;
	TArray<FVector2f> UVs;
};

struct FACEDatPolygon
{
	uint8 NumPts = 0;
	EACEStipplingType Stippling = EACEStipplingType::None;
	EACECullMode SidesType = EACECullMode::None;
	int16 PosSurface = 0;
	int16 NegSurface = 0;
	TArray<int16> VertexIds;
	TArray<uint8> PosUVIndices;
	TArray<uint8> NegUVIndices;
};

struct FACEDatGfxObj
{
	uint32 Id = 0;
	EACEGfxObjFlags Flags = static_cast<EACEGfxObjFlags>(0);
	TArray<uint32> Surfaces;
	TMap<uint16, FACEDatSWVertex> Vertices;
	/** Drawing polygons (HasDrawing). */
	TMap<uint16, FACEDatPolygon> Polygons;
	/** Physics polygons (HasPhysics) — retail outdoor/stab walk/block collision. */
	TMap<uint16, FACEDatPolygon> PhysicsPolygons;
	/** PhysicsBSP leaf poly ids; an empty set has no collision faces. */
	TSet<uint16> PhysicsCollisionPolyIds;
	/** DrawingBSP PORT membership describes visibility independently of polygon surfaces. */
	TSet<uint16> DrawingPortalPolygonIds;
	/** Only InPolys referenced by DrawingBSP are submitted by retail. */
	TSet<uint16> DrawingPolygonIds;
	TMultiMap<int32, uint16> DrawingPortalIndices;
	FVector3f SortCenter = FVector3f::ZeroVector;
	uint32 DIDDegrade = 0;
	uint32 DrawMode = 1; // First GfxObjInfo.DegradeMode; populated with the degrade table.
	float MaxDegradeDistance = 100.f; // AC meters; CPhysicsPart's fallback without a degrade table.
};

/** Setup HoldingLocations / ConnectionPoints entry (PartId + Frame). */
struct FACEDatLocationType
{
	int32 PartId = 0;
	FTransform3f Frame = FTransform3f::Identity;
};

struct FACEDatLightInfo
{
	FVector3f Origin = FVector3f::ZeroVector;
	FQuat4f Orientation = FQuat4f::Identity;
	uint32 Color = 0xFFFFFFFF;
	float Intensity = 0.f;
	float Falloff = 0.f;
	float ConeAngle = 0.f;
};

struct FACEDatCollisionShape
{
	FVector3f Origin = FVector3f::ZeroVector;
	float Radius = 0.f;
	/** Zero for a sphere; positive for a flat-ended retail CCylSphere. */
	float Height = 0.f;
};

struct FACEDatSetupModel
{
	uint32 Id = 0;
	EACESetupFlags Flags = static_cast<EACESetupFlags>(0);
	TArray<uint32> Parts;
	TArray<uint32> ParentIndex;
	TArray<FVector3f> DefaultScale;
	/** ParentLocation → hold frame on a body part (used to attach weapons). */
	TMap<int32, FACEDatLocationType> HoldingLocations;
	TMap<int32, FACEDatLightInfo> Lights;
	/** Placement id → per-part frames (0=Default, 1=RightHandCombat, …). */
	TMap<int32, TArray<FTransform3f>> PlacementFrames;
	/** Convenience alias for PlacementFrames[0] when present. */
	TArray<FTransform3f> DefaultPlacementFrames;
	TArray<FACEDatCollisionShape> CylSpheres;
	TArray<FACEDatCollisionShape> Spheres;
	float Height = 2.f;
	float Radius = 0.5f;
	float StepUpHeight = 0.5f;
	float StepDownHeight = 0.5f;
	/** SetupModel.SelectionSphere (AC units, object space). Radius 0 = dummy / unused. */
	FVector3f SelectionSphereOrigin = FVector3f::ZeroVector;
	float SelectionSphereRadius = 0.f;
	uint32 DefaultAnimation = 0;
	uint32 DefaultScript = 0;
	uint32 DefaultMotionTable = 0;
	uint32 DefaultSoundTable = 0;
	uint32 DefaultScriptTable = 0;
};

enum class EACEAnimationHookType : int32
{
	Unknown = -1,
	NoOp = 0,
	Sound = 1,
	SoundTable = 2,
	Attack = 3,
	AnimationDone = 4,
	ReplaceObject = 5,
	Ethereal = 6,
	TransparentPart = 7,
	Luminous = 8,
	LuminousPart = 9,
	Diffuse = 10,
	DiffusePart = 11,
	Scale = 12,
	CreateParticle = 13,
	DestroyParticle = 14,
	StopParticle = 15,
	NoDraw = 16,
	DefaultScript = 17,
	DefaultScriptPart = 18,
	CallPES = 19,
	Transparent = 20,
	SoundTweaked = 21,
	SetOmega = 22,
	TextureVelocity = 23,
	TextureVelocityPart = 24,
	SetLight = 25,
	CreateBlockingParticle = 26
};

enum class EACEAnimationHookDirection : int32
{
	Unknown = -2,
	Backward = -1,
	Both = 0,
	Forward = 1
};

/** Retained, non-polymorphic representation of every client AnimationHook payload. */
struct FACEDatAnimationHook
{
	EACEAnimationHookType Type = EACEAnimationHookType::Unknown;
	EACEAnimationHookDirection Direction = EACEAnimationHookDirection::Unknown;

	// Shared scalar payload slots. Their meaning follows Type (sound/emitter/part/PES/etc.).
	uint32 Id = 0;
	uint32 PartIndex = 0;
	uint32 SecondaryId = 0;
	int32 State = 0;
	float Start = 0.f;
	float End = 0.f;
	float Time = 0.f;
	float Priority = 0.f;
	float Probability = 0.f;
	float Volume = 0.f;
	float USpeed = 0.f;
	float VSpeed = 0.f;

	// Attack cone.
	FVector2f AttackLeft = FVector2f::ZeroVector;
	FVector2f AttackRight = FVector2f::ZeroVector;
	float AttackRadius = 0.f;
	float AttackHeight = 0.f;

	// SetOmega or CreateParticle offset.
	FVector3f Vector = FVector3f::ZeroVector;
	FTransform3f Frame = FTransform3f::Identity;
};

struct FACEDatParticleEmitterInfo
{
	uint32 Id = 0;
	uint32 Unknown = 0;
	int32 EmitterType = 0;
	int32 ParticleType = 0;
	uint32 GfxObjId = 0;
	uint32 HwGfxObjId = 0;
	double Birthrate = 0.0;
	int32 MaxParticles = 0;
	int32 InitialParticles = 0;
	int32 TotalParticles = 0;
	double TotalSeconds = 0.0;
	double Lifespan = 0.0;
	double LifespanRand = 0.0;
	FVector3f OffsetDir = FVector3f::ZeroVector;
	float MinOffset = 0.f;
	float MaxOffset = 0.f;
	FVector3f A = FVector3f::ZeroVector;
	float MinA = 0.f;
	float MaxA = 0.f;
	FVector3f B = FVector3f::ZeroVector;
	float MinB = 0.f;
	float MaxB = 0.f;
	FVector3f C = FVector3f::ZeroVector;
	float MinC = 0.f;
	float MaxC = 0.f;
	float StartScale = 0.f;
	float FinalScale = 0.f;
	float ScaleRand = 0.f;
	float StartTrans = 0.f;
	float FinalTrans = 0.f;
	float TransRand = 0.f;
	int32 IsParentLocal = 0;
};

struct FACEDatPhysicsScriptEntry
{
	double StartTime = 0.0;
	FACEDatAnimationHook Hook;
};

struct FACEDatPhysicsScript
{
	uint32 Id = 0;
	TArray<FACEDatPhysicsScriptEntry> Entries;
};

struct FACEDatPhysicsScriptCandidate
{
	float Mod = 0.f;
	uint32 ScriptId = 0;
};

struct FACEDatPhysicsScriptTable
{
	uint32 Id = 0;
	TMap<uint32, TArray<FACEDatPhysicsScriptCandidate>> Entries;
};

struct FACEDatWave
{
	uint32 Id = 0;
	TArray<uint8> Header;
	TArray<uint8> Data;
};

struct FACEDatSoundTableEntry
{
	uint32 SoundId = 0;
	float Priority = 0.f;
	float Probability = 0.f;
	float Volume = 0.f;
};

struct FACEDatSoundData
{
	TArray<FACEDatSoundTableEntry> Entries;
	uint32 Unknown = 0;
};

struct FACEDatSoundTable
{
	uint32 Id = 0;
	uint32 Unknown = 0;
	TArray<FACEDatSoundTableEntry> SoundHash;
	uint16 PackedBucketSize = 0;
	TMap<uint32, FACEDatSoundData> Data;
};

enum class EACESurfaceType : uint32
{
	Base1Solid = 0x1,
	Base1Image = 0x2,
	Base1ClipMap = 0x4,
	Translucent = 0x10,
	Diffuse = 0x20,
	Luminous = 0x40,
	Alpha = 0x100,
	InvAlpha = 0x200,
	Additive = 0x10000
};
ENUM_CLASS_FLAGS(EACESurfaceType)

enum class EACESurfacePixelFormat : uint32
{
	R8G8B8 = 20,
	A8R8G8B8 = 21,
	R5G6B5 = 23,
	A4R4G4B4 = 26,
	A8 = 28,
	P8 = 41,
	INDEX16 = 101,
	CUSTOM_LSCAPE_R8G8B8 = 243,
	CUSTOM_LSCAPE_ALPHA = 244,
	CUSTOM_RAW_JPEG = 500,
	DXT1 = 827611204,
	DXT3 = 861165636,
	DXT5 = 894720068
};

struct FACEDatSurface
{
	uint32 Id = 0;
	EACESurfaceType Type = static_cast<EACESurfaceType>(0);
	uint32 OrigTextureId = 0; // SurfaceTexture 0x05
	uint32 OrigPaletteId = 0;
	uint32 ColorValue = 0; // ARGB solid
	float Translucency = 0.f;
	float Luminosity = 0.f;
	float Diffuse = 1.f;
};

struct FACEDatSurfaceTexture
{
	uint32 Id = 0;
	TArray<uint32> Textures; // Texture 0x06 ids
};

/** ContractTable (0x0E00001D) entry — quest panel text and marker positions. */
struct FACEDatContractInfo
{
	uint32 ContractId = 0;
	FString Name;
	FString Description;
	FString DescriptionProgress;
	FString NameNPCStart;
	FString NameNPCEnd;
	/** ObjCellIDs of the start NPC and quest area (0 when the contract has none). */
	uint32 CellNPCStart = 0;
	FVector3f OriginNPCStart = FVector3f::ZeroVector;
	uint32 CellQuestArea = 0;
	FVector3f OriginQuestArea = FVector3f::ZeroVector;
};

/** SpellComponentTable (0x0E00000F) entry joined with its weenie class id. */
struct FACESpellComponentInfo
{
	uint32 ComponentId = 0;
	/** Weenie class id from DualDidMapper 0x27000002; 0 when the mapper has no entry. */
	uint32 Wcid = 0;
	FString Name;
	/** SpellComponentsTable.Type: 1=Scarab 2=Herb 3=Powder 4=Potion 5=Talisman 6=Taper 7=Pea. */
	uint32 Type = 0;
	uint32 Category = 0;
	uint32 IconDid = 0;
};

struct FACEDatTexture
{
	uint32 Id = 0;
	int32 Width = 0;
	int32 Height = 0;
	EACESurfacePixelFormat Format = EACESurfacePixelFormat::A8R8G8B8;
	TArray<uint8> SourceData;
	uint32 DefaultPaletteId = 0;
	bool bHasPalette = false;
};

/** client_portal.dat Font (0x40…) — glyph metrics + FG/BG A8 atlas DIDs. */
struct FACEDatFontChar
{
	uint16 Unicode = 0;
	uint16 OffsetX = 0;
	uint16 OffsetY = 0;
	uint8 Width = 0;
	uint8 Height = 0;
	uint8 HorizontalOffsetBefore = 0;
	uint8 HorizontalOffsetAfter = 0;
	uint8 VerticalOffsetBefore = 0;
};

struct FACEDatFont
{
	uint32 Id = 0;
	uint32 MaxCharHeight = 0;
	uint32 MaxCharWidth = 0;
	TArray<FACEDatFontChar> Chars;
	TMap<uint16, int32> IndexByUnicode;
	uint32 NumHorizontalBorderPixels = 0;
	uint32 NumVerticalBorderPixels = 0;
	uint32 BaselineOffset = 0;
	uint32 ForegroundSurfaceDataID = 0;
	uint32 BackgroundSurfaceDataID = 0;
};

struct FACEDatPalette
{
	uint32 Id = 0;
	TArray<uint32> Colors; // ARGB
};

struct FACEDatAnimData
{
	uint32 AnimId = 0;
	int32 LowFrame = 0;
	int32 HighFrame = -1;
	float Framerate = 30.f;
};

struct FACEDatMotionData
{
	FVector3f Velocity = FVector3f::ZeroVector;
	FVector3f Omega = FVector3f::ZeroVector;
	bool bHasVelocity = false;
	TArray<FACEDatAnimData> Anims;
};

struct FACEDatMotionTable
{
	uint32 Id = 0;
	uint32 DefaultStyle = 0;
	TMap<uint32, uint32> StyleDefaults;
	TMap<uint32, FACEDatMotionData> Cycles;
	/** Outer key = stance hash; inner key = motion command → transition anim data. */
	TMap<uint32, TMap<uint32, FACEDatMotionData>> Links;
};

struct FACEDatAnimationFrame
{
	TArray<FTransform3f> PartFrames;
	TArray<FACEDatAnimationHook> Hooks;
};

struct FACEDatAnimation
{
	uint32 Id = 0;
	uint32 Flags = 0;
	uint32 NumParts = 0;
	uint32 NumFrames = 0;
	TArray<FACEDatAnimationFrame> PartFrames;
};

struct FACEDatCellLandblock
{
	uint32 Id = 0;
	bool bHasObjects = false;
	uint16 Terrain[81] = {};
	uint8 Height[81] = {};
};

/** One static scenery placement (Stab) from LandblockInfo. */
struct FACEDatLandblockStab
{
	uint32 Id = 0; // Setup 0x02 or GfxObj 0x01
	FVector3f Origin = FVector3f::ZeroVector;
	FQuat4f Orientation = FQuat4f::Identity;
};

/** One static object placement (Stab) inside an EnvCell — same on-disk shape as
 *  FACEDatLandblockStab (Id + Frame), kept distinct since EnvCell.StaticObjects is a
 *  separately-shaped list (see ACE.DatLoader.Entity.Stab / FileTypes.EnvCell). */
struct FACEDatStab
{
	uint32 Id = 0; // Setup 0x02 or GfxObj 0x01
	FVector3f Origin = FVector3f::ZeroVector;
	FQuat4f Orientation = FQuat4f::Identity;
};

enum class EACEEnvCellFlags : uint32
{
	SeenOutside = 0x1,
	HasStaticObjs = 0x2,
	HasRestrictionObj = 0x8
};
ENUM_CLASS_FLAGS(EACEEnvCellFlags)

/** CellPortal — connects an EnvCell to a neighboring cell through a polygon (not the
 *  teleport "portal" — see ACE.DatLoader.Entity.CellPortal). */
struct FACEDatCellPortal
{
	uint16 Flags = 0;
	uint16 PolygonId = 0;
	uint16 OtherCellId = 0;
	uint16 OtherPortalId = 0;

	/** ACE: PortalSide bit clear means portal_side=true (positive half-space). */
	bool IsPortalSide() const { return (Flags & 0x2u) == 0; }
	bool IsExactMatch() const { return (Flags & 0x1u) != 0; }
	/** OtherCellId 0xFFFF = transition to outdoor landcells. */
	bool IsOutsidePortal() const { return OtherCellId == 0xFFFFu; }
};

/**
 * One node of CellStruct CellBSP (BSPType.Cell). Retail EnvCell.point_in_cell walks this
 * tree: Front/Close → PosChild (or true); Behind → false (NegChild is never walked).
 * LEAF nodes have no plane — GetSide(0) → Close → inside.
 */
struct FACEDatCellBspNode
{
	bool bLeaf = false;
	/** Splitting plane Normal.xyz + D (ACE cell-local). Unused when bLeaf. */
	FVector4f Plane = FVector4f(0.f, 0.f, 0.f, 0.f);
	int32 PosChild = INDEX_NONE;
	int32 NegChild = INDEX_NONE;
};

/** Indoor cell geometry (walls/floor/ceiling) — the pre-fab block referenced by many EnvCells
 *  via EnvironmentId. Vertices + drawing/physics polygons + CellBSP for point_in_cell.
 *  Physics/Drawing BSP trees are still parsed-and-discarded (poly ids collected). See
 *  ACE.DatLoader.Entity.CellStruct / ACE.Server.Physics.BSP.BSPNode.point_inside_cell_bsp. */
struct FACEDatCellStruct
{
	TMap<uint16, FACEDatSWVertex> Vertices;
	TMap<uint16, FACEDatPolygon> Polygons;
	/** Walkable / blocking surfaces used by retail indoor collision (not decorative draw polys). */
	TMap<uint16, FACEDatPolygon> PhysicsPolygons;
	/** PhysicsBSP leaf poly ids; an empty set has no collision faces. */
	TSet<uint16> PhysicsCollisionPolyIds;
	/** DrawingBSP PORT polys — extra doorway planes beyond EnvCell.CellPortals. */
	TSet<uint16> DrawingPortalPolygonIds;
	TSet<uint16> DrawingPolygonIds;
	/** CellBSP nodes; root is index 0. Empty → AABB fallback for point_in_cell. */
	TArray<FACEDatCellBspNode> CellBspNodes;
};

/** client_portal.dat 0x0D file — a dictionary of CellStruct prefab blocks, keyed by the
 *  EnvCell's CellStructure id. See ACE.DatLoader.FileTypes.Environment. */
struct FACEDatEnvironment
{
	uint32 Id = 0;
	TMap<uint32, FACEDatCellStruct> Cells;
};

/** Indoor cell (dungeon / building interior) from client_cell*.dat — fileId is the full
 *  CellId (as reported by @loc) whose low word is >= 0x0100. See
 *  ACE.DatLoader.FileTypes.EnvCell. */
struct FACEDatEnvCell
{
	uint32 Id = 0;
	EACEEnvCellFlags Flags = static_cast<EACEEnvCellFlags>(0);
	TArray<uint32> Surfaces; // full 0x08000000 ids — index directly with Polygon.PosSurface/NegSurface
	uint32 EnvironmentId = 0; // 0x0D000000 prefab model — look up in client_portal.dat
	uint16 CellStructure = 0; // key into Environment.Cells for this EnvCell's block
	FVector3f Origin = FVector3f::ZeroVector; // Position frame — cell placement within its landblock
	FQuat4f Orientation = FQuat4f::Identity;
	TArray<FACEDatCellPortal> CellPortals;
	TArray<uint16> VisibleCells; // short cell ids (OR with landblock high word for full CellId)
	TArray<FACEDatStab> StaticObjects;
	uint32 RestrictionObj = 0;

	bool SeesOutside() const
	{
		return EnumHasAnyFlags(Flags, EACEEnvCellFlags::SeenOutside);
	}

	bool HasOutsidePortal() const
	{
		for (const FACEDatCellPortal& Portal : CellPortals)
		{
			if (Portal.IsOutsidePortal())
			{
				return true;
			}
		}
		return false;
	}

	/** Retail indoor LScape peek gate (does not require built OutsidePortalPlanes). */
	bool CanSeeOutside() const
	{
		return SeesOutside() || HasOutsidePortal();
	}
};

/**
 * One building door/window portal (retail CBldPortal). Immutable outdoor→indoor seed:
 * destination EnvCell, reverse/incoming portal index on that cell, and StabList PVS.
 * Portals admit and order FullCell EnvCell shells — they are not GPU fragment clips.
 */
struct FACEDatBuildingPortal
{
	/** CBldPortal flags (ExactMatch / PortalSide) — same bit layout as CellPortal. */
	uint16 Flags = 0;
	/** Interior cell short-id immediately behind the door (0xFFFF = outside). */
	uint16 OtherCellId = 0xFFFFu;
	/** Index into destination EnvCell.CellPortals (incoming/reverse portal). */
	uint16 OtherPortalId = 0;
	/** Retail StabList: cell short-ids visible through this portal. */
	TArray<uint16> StabCells;

	bool IsPortalSide() const { return (Flags & 0x2u) == 0; }
	bool IsExactMatch() const { return (Flags & 0x1u) != 0; }
};

struct FACEDatLandblockBuilding
{
	uint32 ModelId = 0;
	FVector3f Origin = FVector3f::ZeroVector;
	FQuat4f Orientation = FQuat4f::Identity;
	/** Per-doorway portals with their StabLists (retail visibility through that door). */
	TArray<FACEDatBuildingPortal> Portals;
	/** Flattened Portals (OtherCellId + StabCells) — connectivity/footprint callers. */
	TArray<uint16> PortalCellIds;
};

struct FACEDatLandblockInfo
{
	uint32 Id = 0;
	/** Count of indoor EnvCells for this landblock (IDs LB|0x0100 .. LB|0x0100+NumCells-1). */
	uint32 NumCells = 0;
	TArray<FACEDatLandblockStab> Objects;
	TArray<FACEDatLandblockBuilding> Buildings;
};

/** RegionDesc LandSurfaces.TexMerge — terrain type → SurfaceTexture + blend alpha maps. */
struct FACEDatTerrainTex
{
	uint32 TexGID = 0;
	uint32 TexTiling = 1;
	uint32 DetailTexTiling = 1;
	uint32 DetailTexGID = 0;
};

struct FACEDatTMTerrainDesc
{
	uint32 TerrainType = 0;
	FACEDatTerrainTex TerrainTex;
};

struct FACEDatTerrainAlphaMap
{
	uint32 TCode = 0;
	uint32 TexGID = 0;
};

struct FACEDatRoadAlphaMap
{
	uint32 RCode = 0;
	uint32 RoadTexGID = 0;
};

struct FACEDatTexMerge
{
	uint32 BaseTexSize = 32;
	TArray<FACEDatTerrainAlphaMap> CornerTerrainMaps;
	TArray<FACEDatTerrainAlphaMap> SideTerrainMaps;
	TArray<FACEDatRoadAlphaMap> RoadMaps;
	TArray<FACEDatTMTerrainDesc> TerrainDesc;
};

/** RegionDesc SceneInfo / TerrainTypes.SceneTypes — drives outdoor bushes/birds/flora. */
struct FACEDatSceneObjectDesc
{
	uint32 ObjId = 0;
	FVector Origin = FVector::ZeroVector;
	FQuat Orientation = FQuat::Identity;
	float Freq = 1.f;
	float DisplaceX = 0.f;
	float DisplaceY = 0.f;
	float MinScale = 1.f;
	float MaxScale = 1.f;
	float MaxRotation = 0.f;
	float MinSlope = -1.f;
	float MaxSlope = 1.f;
	uint32 Align = 0;
	uint32 Orient = 0;
	uint32 WeenieObj = 0;
};

struct FACEDatScene
{
	uint32 Id = 0;
	TArray<FACEDatSceneObjectDesc> Objects;
};

struct FACEDatRegionSceneTables
{
	/** SceneInfo.SceneTypes[i].Scenes — list of Scene DIDs (0x12......). */
	TArray<TArray<uint32>> SceneTypes;
	/** SceneInfo.SceneTypes[i].StbIndex — index into RegionDesc SoundInfo.STBDesc. */
	TArray<uint32> SceneTypeStbIndices;
	/** TerrainTypes[i].SceneTypes — indices into SceneTypes. */
	TArray<TArray<uint32>> TerrainSceneTypeIndices;
	bool bValid = false;
};

/** RegionDesc SoundDesc.AmbientSoundDesc — intermittent / continuous outdoor SFX. */
struct FACEDatAmbientSoundDesc
{
	uint32 SoundType = 0;
	float Volume = 1.f;
	float BaseChance = 1.f;
	float MinRate = 1.f;
	float MaxRate = 10.f;
	bool IsContinuous() const { return BaseChance <= KINDA_SMALL_NUMBER; }
};

struct FACEDatAmbientSTBDesc
{
	uint32 SoundTableId = 0;
	TArray<FACEDatAmbientSoundDesc> Sounds;
};

struct FACEDatRegionSoundInfo
{
	TArray<FACEDatAmbientSTBDesc> STBDescs;
	bool bValid = false;
};

/** One RegionDesc-generated scenery placement in landblock-local AC units. */
struct FACEDatRegionSceneryItem
{
	uint32 SetupId = 0;
	FVector OriginAc = FVector::ZeroVector;
	FQuat Orientation = FQuat::Identity;
	float Scale = 1.f;
};

/** SkyTimeOfDay.SkyObjReplace — per-time-of-day tweak for one SkyObject (by index). */
struct FACEDatSkyObjectReplace
{
	uint32 ObjectIndex = 0;
	uint32 GfxObjId = 0;
	/** Yaw degrees (retail rotates the object about vertical). -1/0 = unused. */
	float Rotate = 0.f;
	/** 0..100 (%). -1 = leave unchanged. 100 = fully transparent. */
	float Transparent = -1.f;
	/** 0..100 (%) brightness. */
	float Luminosity = 0.f;
	float MaxBright = 0.f;
};

/** One SkyDesc time-of-day key (Begin = fraction of the 7620s Dereth day). */
struct FACEDatSkyTimeOfDay
{
	float Begin = 0.f;
	float DirBright = 0.f;
	float DirHeading = 0.f;
	float DirPitch = 0.f;
	uint32 DirColor = 0;
	float AmbBright = 0.f;
	uint32 AmbColor = 0;
	/** World fog near/far distances in AC units. */
	float MinWorldFog = 0.f;
	float MaxWorldFog = 0.f;
	uint32 WorldFogColor = 0;
	uint32 WorldFog = 0;
	TArray<FACEDatSkyObjectReplace> Replaces;
};

/**
 * One celestial object: dome / star shell / cloud plane / sun / moon GfxObj.
 * BeginTime==EndTime==0 → always in the GetSky window, then SkyObjReplace.Transparent
 * fades it (0 = opaque, 100 = hidden). Starfield 0x010015EF is this case: night=0,
 * day=100. Timed objects (sun/moons) use Begin/End plus optional Transparent.
 */
struct FACEDatSkyObject
{
	float BeginTime = 0.f;
	float EndTime = 0.f;
	float BeginAngle = 0.f;
	float EndAngle = 0.f;
	/** UV scroll velocity (clouds). */
	float TexVelocityX = 0.f;
	float TexVelocityY = 0.f;
	uint32 DefaultGfxObjectId = 0;
	uint32 DefaultPesObjectId = 0;
	uint32 Properties = 0;
};

/** SkyDesc.DayGroups entry — one weather flavor ("Sunny", "Cloudy", ...). */
struct FACEDatSkyDayGroup
{
	float ChanceOfOccur = 0.f;
	FString DayName;
	TArray<FACEDatSkyObject> Objects;
	TArray<FACEDatSkyTimeOfDay> TimesOfDay;
};

/** RegionDesc 0x13000000 SkyDesc + the GameTime fields needed to phase it. */
struct FACEDatRegionSky
{
	/** Seconds per Dereth day (7620 in retail). */
	float DayLengthSeconds = 7620.f;
	/** GameTime.DaysPerYear (360). Used by SkyDesc::CalcPresentDayGroup LCG. */
	uint32 DaysPerYear = 360;
	/** GameTime.ZeroYear (10). Seed is current_day + DaysPerYear * current_year. */
	uint32 ZeroYear = 10;
	/** GameTime epoch offset: both weather day and time of day use Ticks + ZeroTimeOfYear. */
	double ZeroTimeOfYear = 3600.0;
	TArray<FACEDatSkyDayGroup> DayGroups;
	bool bValid = false;
};

namespace ACEDatUnpack
{
	/**
	 * Unpack CellStruct CellBSP into OutNodes (root = index 0). Retail point_inside_cell_bsp
	 * walks PosChild only; NegChild is still stored so the stream stays in sync.
	 */
	bool UnpackCellBspTree(FACEDatCursor& Cur, TArray<FACEDatCellBspNode>& OutNodes, int32 Depth = 0);

	bool SkipBspTree(FACEDatCursor& Cur, EACEBspType TreeType, int32 Depth = 0,
		TSet<uint16>* OutDrawingPortalPolys = nullptr, TSet<uint16>* OutPhysicsPolyIds = nullptr, TMultiMap<int32, uint16>* OutPortalIndices = nullptr,
		TSet<uint16>* OutDrawingPolyIds = nullptr);
	bool SkipAnimationHook(FACEDatCursor& Cur);
	bool UnpackAnimationHook(FACEDatCursor& Cur, FACEDatAnimationHook& Out);
	bool UnpackPolygon(FACEDatCursor& Cur, FACEDatPolygon& Out);
	bool UnpackSWVertex(FACEDatCursor& Cur, FACEDatSWVertex& Out);
	bool UnpackGfxObj(FACEDatCursor& Cur, FACEDatGfxObj& Out);
	bool UnpackSetupModel(FACEDatCursor& Cur, FACEDatSetupModel& Out);
	bool UnpackSurface(FACEDatCursor& Cur, FACEDatSurface& Out);
	bool UnpackSurfaceTexture(FACEDatCursor& Cur, FACEDatSurfaceTexture& Out);
	bool UnpackTexture(FACEDatCursor& Cur, FACEDatTexture& Out);
	bool UnpackFont(FACEDatCursor& Cur, FACEDatFont& Out);
	bool UnpackPalette(FACEDatCursor& Cur, FACEDatPalette& Out);
	bool UnpackMotionTable(FACEDatCursor& Cur, FACEDatMotionTable& Out);
	bool UnpackAnimation(FACEDatCursor& Cur, FACEDatAnimation& Out);
	bool UnpackParticleEmitterInfo(FACEDatCursor& Cur, FACEDatParticleEmitterInfo& Out);
	bool UnpackPhysicsScript(FACEDatCursor& Cur, FACEDatPhysicsScript& Out);
	bool UnpackPhysicsScriptTable(FACEDatCursor& Cur, FACEDatPhysicsScriptTable& Out);
	bool UnpackWave(FACEDatCursor& Cur, FACEDatWave& Out);
	bool UnpackSoundTable(FACEDatCursor& Cur, FACEDatSoundTable& Out);
	bool UnpackCellLandblock(FACEDatCursor& Cur, FACEDatCellLandblock& Out);
	/** Only needs LandHeightTable — stops after LandDefs. */
	bool UnpackRegionLandHeightTable(FACEDatCursor& Cur, TArray<float>& OutTable256);
	/** Skip GameTime/Sky/Sound/Scene and unpack TerrainInfo.LandSurfaces.TexMerge. */
	bool UnpackRegionTexMerge(FACEDatCursor& Cur, FACEDatTexMerge& Out);
	/** Capture SceneInfo + TerrainTypes.SceneTypes from RegionDesc 0x13000000. */
	bool UnpackRegionSceneTables(FACEDatCursor& Cur, FACEDatRegionSceneTables& Out);
	/** Capture SoundDesc AmbientSTBDesc list from RegionDesc 0x13000000. */
	bool UnpackRegionSoundInfo(FACEDatCursor& Cur, FACEDatRegionSoundInfo& Out);
	/** Capture GameTime.DayLength + full SkyDesc (day groups / sky objects / time keys). */
	bool UnpackRegionSkyInfo(FACEDatCursor& Cur, FACEDatRegionSky& Out);
	bool UnpackScene(FACEDatCursor& Cur, FACEDatScene& Out);

	/** Landblock static scenery (stabs + buildings) from client_cell *.dat fileId XX YY FFFE. */
	bool UnpackLandblockInfo(FACEDatCursor& Cur, FACEDatLandblockInfo& Out);

	/** Indoor cell prefab block (vertices + drawing/physics polygons); BSP trees skipped. */
	bool UnpackCellStruct(FACEDatCursor& Cur, FACEDatCellStruct& Out);
	/** client_portal.dat 0x0D Environment — dictionary of CellStruct prefab blocks. */
	bool UnpackEnvironment(FACEDatCursor& Cur, FACEDatEnvironment& Out);
	/** client_cell*.dat indoor EnvCell (fileId low word >= 0x0100). */
	bool UnpackEnvCell(FACEDatCursor& Cur, FACEDatEnvCell& Out);
}
