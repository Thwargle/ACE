#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACESetupMeshBuilder.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACELandSurfaceAtlas.h"
#include "Dat/ACEDatFileTypes.h"
#include "Dat/ACEDatMotionPlayer.h"
#include "Dat/ACEPortalViewMask.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "ACEDatSubsystem.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialInstanceConstant;
class UStaticMesh;
class UStaticMeshComponent;
class UACEUIResourceResolver;

UCLASS(Config=Engine)
class ACECLIENT_API UACEDatSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UACEDatSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual bool IsTickableInEditor() const override { return false; }

	void TrackRuntimeTexture(UTexture2D* Tex);
	UACEUIResourceResolver* GetUiResources();
private:
	UPROPERTY(Transient) TObjectPtr<UACEUIResourceResolver> SharedUiResources;
public:
	void UntrackRuntimeTexture(UTexture2D* Tex);
	/** Count + byte LRU trim for textures/setup caches. Safe to call often. */
	void TrimMemoryCaches();

	/** Blocking load — prefer BeginBackgroundLoad / EnsureLoaded (non-blocking). */
	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	bool LoadDatDirectory(const FString& Directory);

	/**
	 * Returns true when portal DAT is indexed and builders are ready.
	 * If not ready, kicks off a background load and returns false immediately —
	 * never blocks the game thread on multi-hundred-MB DAT indexing.
	 */
	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	bool EnsureLoaded();

	/** Start indexing portal/cell (and optional highres) on a worker thread. Safe to call often. */
	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	void BeginBackgroundLoad(bool bRetryFailed = false);

	const FString& GetDatLoadError() const { return DatLoadError; }

	UFUNCTION(BlueprintPure, Category = "ACE|DAT")
	bool IsDatReady() const { return bPortalLoaded; }
	/** Directory changes must not relabel a worker that is indexing another installation. */
	bool IsDatLoading() const { return bBackgroundLoadInProgress || bCellBackgroundLoadInProgress; }

	/**
	 * When false, landblock/EnvCell presenters must not stream. Portal-space tunnel mesh
	 * is built first so the player never sees a black frame or world pop-in.
	 */
	UFUNCTION(BlueprintPure, Category = "ACE|DAT")
	bool IsWorldStreamingAllowed() const { return bWorldStreamingAllowed; }

	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	void SetWorldStreamingAllowed(bool bAllowed);

	/** True while the portal tunnel / reveal is active — outdoor ambient & world SFX muted. */
	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	void SetInPortalSpace(bool bInPortal) { bInPortalSpace = bInPortal; }

	UFUNCTION(BlueprintPure, Category = "ACE|DAT")
	bool IsInPortalSpace() const { return bInPortalSpace; }

	/** Player CellId is an EnvCell (building / dungeon) — not the portal tunnel. */
	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	void SetIndoorEnvironment(bool bIndoor) { bIndoorEnvironment = bIndoor; }

	UFUNCTION(BlueprintPure, Category = "ACE|DAT")
	bool IsIndoorEnvironment() const { return bIndoorEnvironment; }

	/** Inside a building EnvCell (shop) — keep outdoor sun, but self-lit fill so rooms aren't caves. */
	void SetBuildingInteriorFill(bool bFill) { bBuildingInteriorFill = bFill; }
	bool IsBuildingInteriorFill() const { return bBuildingInteriorFill; }

	/**
	 * During portal-space, stream the destination at a low game-thread budget so the
	 * tunnel camera stays smooth. Full rate resumes after reveal.
	 */
	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	void SetLightweightStreaming(bool bLightweight) { bLightweightStreaming = bLightweight; }

	UFUNCTION(BlueprintPure, Category = "ACE|DAT")
	bool IsLightweightStreaming() const { return bLightweightStreaming; }

	/** Prefetch the portal-space Setup (and its textures) before world streaming begins. */
	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	bool PrefetchPortalSpaceSetup(int32 SetupId = 0x02000306, float WorldScale = 1.f);

	UFUNCTION(BlueprintPure, Category = "ACE|DAT")
	bool IsCellReady() const { return CellDat.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "ACE|DAT")
	FString GetDatDirectory() const { return DatDirectory; }

	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	void SetDatDirectory(const FString& Directory) { DatDirectory = Directory; }

	/**
	 * Retail PhysicsObj.InitObjectEnd sets PlacementFrame 0x65 (Resting) for static scenery.
	 * EnvCell / landblock Stab must bake that placement or paintings hang at Default (floor).
	 */
	static constexpr int32 ACEPlacementResting = 101;

	const FACEBuiltSetupMesh* GetOrBuildSetupMesh(uint32 SetupId, float WorldScale = 1.f, int32 PlacementId = 0);

	/** Async Setup geometry (DAT parse / GfxObj fans) — mirrors landblock Request API. */
	enum class EACESetupMeshStatus : uint8
	{
		NotReady,
		Pending,
		Ready,
		Failed
	};
	EACESetupMeshStatus RequestSetupMesh(uint32 SetupId, float WorldScale = 1.f, int32 PlacementId = 0);
	const FACEBuiltSetupMesh* FindSetupMesh(uint32 SetupId, float WorldScale = 1.f, int32 PlacementId = 0) const;
	/** Hold across material/physics creation, which may pump pending game-thread builds. */
	TSharedPtr<const FACEBuiltSetupMesh> GetOrBuildSetupMeshShared(uint32 SetupId, float WorldScale = 1.f, int32 PlacementId = 0);
	void AllowSetupMeshRetry(uint32 SetupId, float WorldScale = 1.f, int32 PlacementId = 0);

	/**
	 * Build or return a cached transient UStaticMesh for a Setup (flattened parts).
	 * Requires FindSetupMesh Ready. Used by HISM scenery / EnvCell statics.
	 */
	UStaticMesh* GetOrCreateSetupStaticMesh(uint32 SetupId, float WorldScale = 1.f, bool bEnableCollision = true, int32 PlacementId = 0, bool bParticleGfx = false, bool bStencilHoleClip = false, bool bOutdoorLit = false, bool bPhysicsCollisionOnly = false);
	/** Particle GfxObj → shared UStaticMesh (HW cube shrink + particle materials, no collision). */
	UStaticMesh* GetOrCreateParticleStaticMesh(uint32 GfxObjId, float WorldScale);
	/**
	 * Copy cached section MIDs onto a component. UStaticMesh FastBuild leaves Engine default
	 * (lit WorldGrid) on the GPU — DefaultLit + GI off is the black/grey building silhouette.
	 */
	void BindSetupStaticMeshMaterials(UStaticMeshComponent* Comp, uint32 SetupId, float WorldScale, bool bEnableCollision, int32 PlacementId, bool bParticleGfx = false, bool bStencilHoleClip = false, bool bOutdoorLit = false, bool bPhysicsCollisionOnly = false);

	bool BuildSetupAppearance(uint32 SetupId, const FACEObjDesc& Appearance, float WorldScale, FACEBuiltSetupMesh& OutMesh, int32 PlacementId = 0);

	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	bool ApplySetupToProceduralMesh(UProceduralMeshComponent* ProcMesh, int32 SetupId, float WorldScale = 1.f, bool bEnableCollision = false, int32 PlacementId = 0, bool bForceDrawCollision = false, bool bIndoorStairCollision = false, bool bOutdoorLit = false, bool bReverseFaces = false);

	/**
	 * Particle GfxObjs: same as ApplySetupToProceduralMesh but never falls back to
	 * VertexColor/WorldGrid (those paint solid white planes under FX).
	 */
	bool ApplyParticleGfxToProceduralMesh(UProceduralMeshComponent* ProcMesh, int32 GfxObjId, float WorldScale = 1.f, bool bEnableCollision = false);

	/**
	 * Hide translucent/additive sections. Mixed-blend ProceduralMeshComponents skip the
	 * whole primitive in CSM — Yaraq shop shells (opaque walls + portal fills) never cast.
	 */
	static void HideNonOpaqueProcMeshSections(UProceduralMeshComponent* ProcMesh);

	bool ApplySetupParts(const TArray<UProceduralMeshComponent*>& PartMeshes, int32 SetupId, const FACEObjDesc& Appearance, float WorldScale, TArray<FTransform>& OutBindTransforms, uint32& OutDefaultMotionTableId, int32 PlacementId = 0, bool bEnableCollision = false, float ObjectTranslucency = 0.f, const FACEBuiltSetupMesh* Prebuilt = nullptr);

	/** Parent Setup HoldingLocations[ParentLocation] → part index + AC-space frame. */
	bool GetHoldingLocation(uint32 ParentSetupId, int32 ParentLocation, int32& OutPartIndex, FTransform& OutUnrealRelative, float WorldScale);

	const FACEBuiltLandblockMesh* GetOrBuildLandblockMesh(uint32 LandblockId, float WorldScale = 100.f);

	/** Status of an async landblock mesh request (memory / disk / worker). */
	enum class EACELandMeshStatus : uint8
	{
		NotReady, // DAT not indexed yet
		Pending,  // queued or building / loading from disk
		Ready,    // in LandblockCache — safe to ApplyLandblockToProceduralMesh
		Failed
	};

	/**
	 * Non-blocking: returns Ready when the mesh is in memory, queues a background
	 * disk-tile load or DAT rebuild otherwise. Prefer this over GetOrBuildLandblockMesh
	 * for streaming so the game thread never stalls on TexMerge / DAT I/O.
	 * Disk tiles live under Saved/ACEClient/MeshCache (purged/budgeted on DAT load).
	 */
	EACELandMeshStatus RequestLandblockMesh(uint32 LandblockId, float WorldScale = 100.f);

	/** Memory-only lookup — never builds. */
	const FACEBuiltLandblockMesh* FindLandblockMesh(uint32 LandblockId) const;

	/** True when a background bake / disk load for this landblock is already queued. */
	bool IsLandblockMeshInFlight(uint32 LandblockId) const;

	/** Drop pending async builds outside this keep-set (landblock high-words). */
	void CancelLandblockMeshRequestsOutside(const TSet<int32>& KeepLandblockIds);

	/** Evict baked land meshes / footprints outside the keep ring (memory). */
	void EvictLandblockMeshesOutside(const TSet<int32>& KeepLandblockIds);

	/** Clear a prior async failure so the next RequestLandblockMesh will re-queue. */
	void AllowLandblockMeshRetry(uint32 LandblockId);

	/**
	 * Outdoor floor Z from the landblock heightfield (same verts as the visual mesh).
	 * UnrealXY are world positions; OutUnrealZ is absolute world Z for feet on the terrain.
	 * Indoor movement should ignore outdoor-terrain hits rather than refusing samples here.
	 */
	bool SampleOutdoorGroundZ(float UnrealX, float UnrealY, float WorldScale, float& OutUnrealZ, FVector* OutUnrealNormal = nullptr);

	/**
	 * Retail water-depth sink in Unreal cm (0 if dry / mesh missing).
	 * Same discrete depths as ACE ObjCell.get_water_depth.
	 */
	float GetOutdoorWaterDepthCm(float UnrealX, float UnrealY, float WorldScale);

	/**
	 * Retail LandblockStruct.water_type for the landblock containing XY:
	 * 0=NotWater, 1=PartiallyWater, 2=EntirelyWater.
	 */
	uint8 GetOutdoorBlockWaterType(float UnrealX, float UnrealY, float WorldScale);

	/**
	 * Per 24×24 outdoor cell water type (LandCell.WaterType). Used by
	 * get_water_depth (0 / 0.1 / 0.45 / 0.9 AC). Walk blocking uses
	 * GetOutdoorBlockWaterType — retail get_block_water_type().
	 */
	uint8 GetOutdoorCellWaterType(float UnrealX, float UnrealY, float WorldScale);

	/** True when landblock-local Unreal XY sits under a building interior footprint. */
	bool IsUnderBuildingInterior(uint32 LandblockId, float LocalUnrealX, float LocalUnrealY, float WorldScale);

	/** World-space XY under a building floor/shell footprint (doorway stairs, shop floors). */
	bool IsWorldXYUnderBuildingInterior(float UnrealX, float UnrealY, float WorldScale);

	/**
	 * LandblockInfo.Buildings index that owns an indoor EnvCell (portal BFS from PortalCellIds).
	 * INDEX_NONE outdoors / unknown. Used to hide only the occupied Setup shell indoors.
	 */
	int32 FindBuildingInfoIndexForIndoorCell(uint32 LandblockId, uint32 CellId, float WorldScale);

	/**
	 * Occupied building Setup shell only (player CellId). Do not hide every PVS shop —
	 * that made the town vanish on enter. Other facades stay for look-out.
	 */
	void CollectBuildingShellHideIndices(uint32 LandblockId, uint32 PlayerCellId,
		const TSet<int32>& VisibleIndoorCellIds, float WorldScale, TArray<int32>& OutHideIndices);
	/** Outdoor: hide Setup shells the player is standing in/near (open-air vendor counters). */
	void CollectNearbyOutdoorBuildingShellHideIndices(uint32 LandblockId, const FVector& PlayerWorld,
		float WorldScale, TArray<int32>& OutHideIndices);
	/**
	 * World XY box of the building the player is standing in (FloorTris, else indoor cell bounds).
	 * Used to discard LScape under shop floors whether occupancy is still outdoor or already indoor.
	 */
	bool TryGetBuildingLandClipBox(uint32 LandblockId, uint32 PlayerCellId, const FVector& PlayerWorld,
		float WorldScale, FVector& OutMin, FVector& OutMax);

	UFUNCTION(BlueprintCallable, Category = "ACE|DAT")
	bool ApplyLandblockToProceduralMesh(UProceduralMeshComponent* ProcMesh, int32 LandblockId, float WorldScale = 100.f, int32 PolySize = 1, int32 TransDir = 0);

	/** Drop cached basement-lid punch masks for a landblock (rebuild on next Apply). */
	void InvalidateBuildingInteriorFootprints(uint32 LandblockId);

	/**
	 * Combine several landblock meshes into one ProcMesh, offsetting verts into chunk-local space
	 * (origin = ChunkOriginLandblockId SW corner). GPU sections share one land material.
	 */
	bool ApplyLandblockChunkToProceduralMesh(UProceduralMeshComponent* ProcMesh,
		const TArray<const FACEBuiltLandblockMesh*>& Meshes, int32 ChunkOriginLandblockId, float WorldScale = 100.f);

	/** RegionDesc-generated outdoor scenery (bushes/birds/flora) for one landblock. */
	bool CollectRegionScenery(uint32 LandblockId, float WorldScale, TArray<FACEDatRegionSceneryItem>& OutItems);

	/** RegionDesc SkyDesc (day groups / sky objects / time-of-day keys). Null until DAT ready. */
	const FACEDatRegionSky* GetRegionSkyInfo();

	/** Night/day land emissive. SceneryScale (-1 = derive) keeps interiors readable at night. */
	void SetWorldEmissiveScale(float LandScale, float SceneryScale = -1.f, const FLinearColor& AmbientTint = FLinearColor::White);

	/** Retail linear distance fog (MinWorldFog→MaxWorldFog) on land/scenery materials. */
	void SetWorldDistanceFog(float StartCm, float EndCm, const FLinearColor& Color, float Amount);
	/** Apply current world fog params to a land/scenery material instance (MID or MIC). */
	void ApplyDistanceFogToMaterial(UMaterialInterface* Inst) const;

	/** RegionDesc SoundDesc ambient STB list. Null until DAT ready. */
	const FACEDatRegionSoundInfo* GetRegionSoundInfo();

	/**
	 * Resolve outdoor AmbientSTBDesc for a land cell using terrain type + scenery bits
	 * (same indices as CollectRegionScenery / retail CTerrainDesc::GetSTBDesc).
	 */
	bool TryResolveAmbientSTBForCell(uint32 CellId, const FACEDatAmbientSTBDesc*& OutStb);

	/**
	 * LandblockInfo (LB|0xFFFE) — buildings, CBldPortal StabLists. Results are cached by
	 * landblock key; cleared with other DAT caches on reload.
	 */
	bool LoadLandblockInfo(uint32 LandblockId, FACEDatLandblockInfo& OutInfo);

	/** Static world-space doors only; camera clipping is evaluated every frame. */
	void LoadBuildingDoorwayApertures(uint32 LandblockId, float WorldScale,
		TArray<ACEOutdoorPortalPlan::FAdmittedAperture>& OutApertures);

	/** Raw EnvCell unpack (Flags / CellPortals) without building draw geometry. */
	bool LoadEnvCell(uint32 EnvCellId, FACEDatEnvCell& OutCell);

	const FACEBuiltEnvCellMesh* GetOrBuildEnvCellMesh(uint32 EnvCellId, float WorldScale = 100.f);

	/** Async EnvCell geometry — prefer for streaming; GetOrBuild remains for prediction. */
	enum class EACEEnvCellMeshStatus : uint8
	{
		NotReady,
		Pending,
		Ready,
		Failed
	};
	EACEEnvCellMeshStatus RequestEnvCellMesh(uint32 EnvCellId, float WorldScale = 100.f);
	const FACEBuiltEnvCellMesh* FindEnvCellMesh(uint32 EnvCellId, float WorldScale = 100.f) const;
	void CancelEnvCellMeshRequestsOutside(const TSet<int32>& KeepEnvCellIds);
	/** Evict baked EnvCell meshes not in Keep (full CellIds). */
	void EvictEnvCellMeshesOutside(const TSet<int32>& KeepEnvCellIds, float WorldScale);
	void AllowEnvCellMeshRetry(uint32 EnvCellId, float WorldScale = 100.f);
	/** Soft cap Setup bake cache so portal / town hops don't unbounded-grow RAM. */
	void TrimSetupMeshCache(int32 MaxEntries = 192);
	/** Soft cap scenery HISM static meshes built while traveling. */
	void TrimSetupStaticMeshCache(int32 MaxEntries = 512);
	/** Soft cap ObjDesc / resolved RGBA textures (signs, clothing) that grow with travel. */
	void TrimResolvedTextureCache(int32 MaxEntries = 256);
	/** Soft cap DAT surface decode + UTexture maps in the texture resolver (largest RAM leak). */
	void TrimTextureResolverCaches(int32 MaxTextures = 384, int32 MaxDecodedSurfaces = 256, int64 MaxBytes = 384ll << 20);

	/**
	 * Approximate EnvCell containment for doorway entry/exit (AABB in cell-local space).
	 * Used by client prediction — the unchanged ACE server still authorizes the final CellId.
	 */
	bool IsPointInsideEnvCell(uint32 EnvCellId, const FVector& UnrealWorldPos, float WorldScale, float PaddingCm = 5.f);

	/**
	 * Resolve the EnvCell that should contain UnrealWorldPos near HintCellId.
	 * Outdoor: tests building portal entry cells. Indoor: current cell then VisibleCells.
	 */
	bool ResolveContainingEnvCell(uint32 HintCellId, const FVector& UnrealWorldPos, float WorldScale, uint32& OutEnvCellId);

	bool EvaluateIdleMotion(uint32 MotionTableId, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
		const float* PreviousTimeSeconds = nullptr, TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr) const;
	bool GetMotionVelocity(uint32 MotionTableId, uint32 Command, uint32 Style, FVector& Out) const;
	bool EvaluateMotionCommand(uint32 MotionTableId, uint32 MotionCommand, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
		const float* PreviousTimeSeconds = nullptr, TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr, uint32 PreferredStyle = 0,
		bool bLoop = true, bool* bOutFinished = nullptr) const;
	/** One-shot MotionTable Links transition (door Off→On / attack Ready→Slash). Clamps to final frame.
	 *  PreferredStyle = CurrentStyle (HandCombat etc.); 0 = DefaultStyle / NonCombat fallbacks. */
	bool EvaluateMotionLink(uint32 MotionTableId, uint32 FromCommand, uint32 ToCommand, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount, bool& bOutFinished,
		const float* PreviousTimeSeconds = nullptr, TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr, uint32 PreferredStyle = 0) const;
	/** Loop a raw Animation DID (Setup DefaultAnimation / lifestones). */
	bool EvaluateAnimationLoop(uint32 AnimationId, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
		const float* PreviousTimeSeconds = nullptr, TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr,
		float Framerate = 30.f, int32 LowFrame = 0) const;
	/** Setup collision/animation metadata (StepUp/StepDown heights are in AC units). */
	bool TryGetSetupPhysics(uint32 SetupId, float& OutStepUpHeight, float& OutHeight, float& OutRadius, uint32& OutDefaultAnimationId, float* OutStepDownHeight = nullptr,
		FVector3f* OutSelectionOriginAc = nullptr, float* OutSelectionRadiusAc = nullptr);
	/** Setup defaults used by the PhysicsScript/sound runtime. */
	bool TryGetSetupRuntimeMetadata(uint32 SetupId, uint32& OutDefaultScriptId, uint32& OutScriptTableId, uint32& OutSoundTableId);
	bool GetSetupCollisionShapes(uint32 SetupId, TArray<FACEDatCollisionShape>& OutShapes, bool& bHasPhysicsBSP);

	/** Parsed client_portal.dat script/particle assets; returned pointers remain valid until DAT reset. */
	const FACEDatParticleEmitterInfo* GetParticleEmitterInfo(uint32 Id);
	/** Average color + luminosity of a particle GfxObj's first surface (point-light tint). */
	bool TryEstimateGfxLight(uint32 GfxObjId, FLinearColor& OutColor, float& OutLuminosity);
	const FACEDatPhysicsScript* GetPhysicsScript(uint32 Id);
	const FACEDatPhysicsScriptTable* GetPhysicsScriptTable(uint32 Id);
	const FACEDatWave* GetWave(uint32 Id);
	const FACEDatSoundTable* GetSoundTable(uint32 Id);

	FACEDatTextureResolver* GetTextureResolver() const { return TextureResolver.Get(); }
	FACEDatDatabase* GetPortalDat() const { return PortalDat.Get(); }

	/** SpellTable (0x0E00000E) name + icon DID. Lazily parsed once portal.dat is ready. */
	bool TryGetSpellInfo(uint32 SpellId, FString& OutName, uint32& OutIconDid);
	bool TryGetSpellDescription(uint32 SpellId, FString& OutDescription);
	bool TryGetSpellExamination(uint32 SpellId, FString& OutDetails);
	/** DisplayOrder from SpellTable — used to sort the spellbook like retail. */
	bool TryGetSpellDisplayOrder(uint32 SpellId, uint32& OutDisplayOrder);
	/**
	 * MagicSchool (1=War..5=Void) and UI spell level 1–8 derived from SpellBase.Power
	 * via retail SpellFormula.MinPower thresholds (for spellbook filter checkboxes).
	 */
	bool TryGetSpellSchoolAndLevel(uint32 SpellId, uint32& OutSchool, uint32& OutLevel);
	uint32 GetSpellIconPowerLevel(uint32 SpellId);

	/**
	 * SpellTable targeting fields used by CastTargetedSpell / CastUntargetedSpell.
	 * Bitfield includes SpellFlags.SelfTargeted (0x8). NonComponentTargetType == 0 means
	 * ItemType.None (untargeted); otherwise a world target is required.
	 */
	bool TryGetSpellTargeting(uint32 SpellId, uint32& OutBitfield, uint32& OutNonComponentTargetType);

	/**
	 * SpellComponentTable (0x0E00000F) joined with the SpellComponents DualDidMapper
	 * (0x27000002) so each entry carries the weenie class id the server expects in
	 * SetDesiredComponentLevel. Sorted by component type then name, retail list order.
	 */
	const TArray<FACESpellComponentInfo>& GetSpellComponents();

	/** ContractTable (0x0E00001D) entry for a contract id from the server's tracker table. */
	const FACEDatContractInfo* GetContractInfo(uint32 ContractId);

	/**
	 * ChatPoseTable (0x0E000007) — Mag-nus *wave* / *bow* poses.
	 * OutCommand is the MotionCommand name; OutMy/OtherEmote are display strings (%s / %p).
	 */
	bool TryGetChatPose(const FString& PoseKey, FString& OutCommand, FString& OutMyEmote, FString& OutOtherEmote);
	/** All ChatPoseTable pose keys (lowercased), for retail @emotes listing. */
	void GetChatPoseKeys(TArray<FString>& OutKeys);

	/** SkillTable (0x0E000004) name + icon DID. Lazily parsed once portal.dat is ready. */
	bool TryGetSkillInfo(uint32 SkillId, FString& OutName, uint32& OutIconDid);
	/** CACQualities: DAT formulas, raw/buffed attributes and top-layer enchantments. */
	void RecomputePlayerStats(FACEPlayerVitals& Vitals, const TArray<FACEActiveEnchantment>& Enchantments);
	/** SkillTable trained credit cost (GameAction TrainSkill). May be 0 for free skills. */
	bool TryGetSkillTrainedCost(uint32 SkillId, int32& OutTrainedCost);
	/**
	 * SkillTable MinLevel: 1 = usable when Untrained, 2 = requires Trained+.
	 * Used to split retail Untrained vs Unusable list sections (both are SAC=Untrained).
	 */
	bool TryGetSkillMinLevel(uint32 SkillId, uint32& OutMinLevel);

	/**
	 * XP table 0x0E000018 helpers.
	 * AttributeXpList[r] is cumulative XP required for rank r.
	 * Returns XP still needed to reach the next rank from current ExperienceSpent.
	 */
	bool TryGetAttributeXpToNextRank(int32 CurrentXpSpent, int64& OutXpNeeded, int64* OutMaxSpendable = nullptr, int32 RankCount = 1);
	bool TryGetVitalXpToNextRank(int32 CurrentXpSpent, int64& OutXpNeeded, int64* OutMaxSpendable = nullptr, int32 RankCount = 1);
	/** Trained (SAC=2) or Specialized (SAC=3) skill XP tables from 0x0E000018. */
	bool TryGetSkillXpToNextRank(int32 AdvancementClass, int32 CurrentXpSpent, int64& OutXpNeeded, int64* OutMaxSpendable = nullptr, int32 RankCount = 1);
	bool TryGetXpToNextLevel(int64 TotalExperience, int32 CurrentLevel, int64& OutXpNeeded, float* OutProgress01 = nullptr);

	/**
	 * True once after land mesh format changes — terrain presenter should respawn landblocks.
	 * If bOutClearEnvCells is set, also clear EnvCell actors (geometry/cache format change).
	 * Land-material-only bumps leave EnvCells alone (avoids floor pop-in / sky texture thrash).
	 */
	bool ConsumeLandMeshReloadRequest(bool* bOutClearEnvCells = nullptr);

	void RebuildLandSurfaceAtlas();

	UMaterialInterface* GetVertexColorMaterial();
	/** Complete shader parent set, generated in the editor and loaded from cooked assets in game builds. */
	TArray<UMaterialInterface*> GetRuntimeMaterialParents();
	UMaterialInterface* GetVRComfortMaterial();
	/** PhysicsBSP sections must not use VertexColor/WorldGrid (lit → black with GI off). */
	UMaterialInterface* EnsureInvisibleCollisionMaterial();
	UMaterialInterface* GetOrCreateTexturedMaterial(uint32 SurfaceId);
	/** Live MID for UStaticMesh / HISM (same graph as PMC — runtime MIC never bound textures). */
	UMaterialInterface* GetOrCreateStaticMeshMaterial(uint32 SurfaceId);
	/** Outdoor landblock scenery (plants / stabs) — DefaultLit so CSM casts onto and from them. */
	UMaterialInterface* GetOrCreateOutdoorLitMaterial(uint32 SurfaceId);
	UMaterialInterface* GetOrCreateEnvCellMaterial(uint32 SurfaceId);
	void SetInteriorAmbient(const FLinearColor& Color);
	void SetInteriorUsesOutdoorAmbient(bool bUseOutdoor);
	/**
	 * Particle GfxObj materials — never opaque solid swatches (those look like emitter squares).
	 * Returns null when the surface cannot be drawn safely as soft FX.
	 */
	UMaterialInterface* GetOrCreateParticleMaterial(uint32 SurfaceId);
	/** Celestial particle cards use the sky's clamp sampler and unmipped texture. */
	UMaterialInstanceDynamic* CreateSkyParticleMaterial(UMaterialInterface* Source, UObject* Outer);
	/**
	 * UV-textured material for a GfxObj surface, with ObjDesc TextureChanges/SubPalettes applied.
	 * Prefer this over vertex-color fallback so clothing/armor keep proper surface detail.
	 */
	/** WrapAxes: bit 0 = U, bit 1 = V. Preserve authored tiling without wrapping face-patch edges. */
	UMaterialInterface* GetOrCreateResolvedMaterial(uint32 SurfaceId, int32 PartIndex, const FACEObjDesc& Appearance, float ObjectTranslucency = 0.f, uint8 WrapAxes = 0);
	/** Dedicated retail CreatureMode vertex lighting, isolated from world fog and materials. */
	UMaterialInterface* CreateExaminationMaterial(UMaterialInterface* Source, UObject* Outer);
	UMaterialInterface* GetUniformInteriorMaterial(UMaterialInterface* Source);
	UMaterialInterface* GetWorldObjectMaterial(UMaterialInterface* Source);
	void UpdateWorldObjectLighting(UMaterialInstanceDynamic* Material, bool bInterior);
	/** Land cell TexMerge bake → MID (cached by PCode). */
	UMaterialInterface* GetOrCreateLandMaterial(uint32 PCode, const TArray<FColor>& Pixels, int32 Width, int32 Height,
		FACETerrainBlendCache::FBlendPtr SharedBlend = nullptr);

	/** Shared land Texture2DArray material (GPU TexMerge). Null until atlas is ready. */
	UMaterialInterface* GetOrCreateLandGpuMaterial();

	bool IsLandSurfaceAtlasReady() const { return LandAtlas && LandAtlas->IsReady(); }

	/**
	 * Indoor DrawInside analogue of Render::PortalList: keep LScape fragments whose
	 * camera ray crosses an actually visible, clipped 0xFFFF portal polygon.
	 */
	void SetLandLookOutClip(bool bEnable, const FVector& CameraWorld,
		const TArray<ACEOutdoorPortalPlan::FAdmittedAperture>& Apertures,
		const TArray<ACEOutdoorPortalPlan::FAdmittedAperture>* LookInExits = nullptr);
	/** Legacy name — disables look-out clip. Plane-list clipping is unused. */
	void SetOutdoorPortalLandClip(bool bEnable, const TArray<FVector4f>& WorldPlanes);
	/**
	 * Indoor occupancy: discard LScape fragments whose world XY sits inside the occupied
	 * EnvCell AABB (grass through shop floors) without hiding the plaza outside the box.
	 */
	void SetLandInteriorClip(bool bEnable, const FVector& WorldMin, const FVector& WorldMax);

	/**
	 * Indoor look-out: land MIDs sample CustomDepth stencil written by outside-portal polys.
	 * Do not enable — SceneTexture-masked land renders black outdoors/indoors.
	 */
	void SetPortalLookOutLand(bool bEnable);
	bool IsPortalLookOutLand() const { return bPortalLookOutLand; }
	/** Remap an existing land section MID to opaque or look-out variant (same ACETexture). */
	UMaterialInterface* RemapLandMaterialForPortalLookOut(UMaterialInterface* Current, bool bLookOut);
	UMaterialInterface* RemapLandMaterialWithDepthBias(UMaterialInterface* Current, float DepthBiasCm);
	UMaterialInterface* GetPortalStencilWriterMaterial();
	/**
	 * Opaque depth-only doorway aperture (no color in main pass). Opens building Setup
	 * doorways in the depth buffer — never installed as an EnvCell fragment clip.
	 */
	UMaterialInterface* GetPortalDepthApertureMaterial();
	UMaterialInterface* EnsureAceLandLookOutMaterialBase();
	/**
	 * Building Setup shells: DefaultLit + world-space doorway slab clip. Outdoor walls
	 * receive/cast CSM; EnvCell interiors stay on the Unlit textured path.
	 */
	UMaterialInterface* EnsureAceBuildingShellMaterialBase();
	UMaterialInterface* GetOrCreateBuildingShellMaterial(uint32 SurfaceId);

	/** True when EnvCell is in a LandblockInfo building's portal BFS (not a pure dungeon cell). */
	bool IsBuildingInteriorEnvCell(uint32 EnvCellId, float WorldScale);

	/** Two-sided unlit Texture param material with UsedWithProceduralMeshes (required or meshes go white/pink). */
	UMaterialInterface* EnsureAceUnlitTexturedMaterialBase();
	/** Land heightfield — Opaque unlit (portal-plane clip disabled; caused black voids). */
	UMaterialInterface* EnsureAceLandMaterialBase();
	/** ClipMap holes — Masked OpacityMask. */
	UMaterialInterface* EnsureAceUnlitMaskedMaterialBase();
	UMaterialInterface* EnsureAceOutdoorLitMaterialBase();
	UMaterialInterface* EnsureAceOutdoorLitMaskedMaterialBase();
	/** Soft translucency (lifestones / glass) — Translucent Opacity from texture A * OpacityMul. */
	UMaterialInterface* EnsureAceUnlitTranslucentMaterialBase();
	/** Additive FX (portal swirls / spell particles) — Additive emissive * OpacityMul. */
	UMaterialInterface* EnsureAceUnlitAdditiveMaterialBase();
	/** Spell/weapon particles — additive fade without squaring OpacityMul. */
	UMaterialInterface* EnsureAceParticleAdditiveMaterialBase();
	UMaterialInterface* EnsureAceParticleTranslucentMaterialBase();
	UMaterialInterface* EnsureAceBatchedParticleMaterialBase(bool bAdditive);
	/** Bumped when sky overlay UMaterials are flushed so AACESkyDomeActor rebuilds. */
	int32 GetSkyMaterialGeneration() const { return SkyMaterialGeneration; }
	/** Sky cloud / star layers — Translucent unlit, height fog disabled. */
	UMaterialInterface* EnsureAceSkyTranslucentMaterialBase();
	/** Sky cloud layers — Translucent unlit, UV Wrap+Frac (tiled cloud cards). */
	UMaterialInterface* EnsureAceSkyTranslucentWrapMaterialBase();
	/** Day-dome cube faces (opaque DAT images) — Opaque unlit so far walls do not stack as sheets. */
	UMaterialInterface* EnsureAceSkyOpaqueMaterialBase();
	/** Sun / glowing celestial quads — Additive unlit, height fog disabled. */
	UMaterialInterface* EnsureAceSkyAdditiveMaterialBase();
	/** Day-dome solid vertex-color backdrop — opaque unlit (avoids full-screen translucent overdraw). */
	UMaterialInterface* EnsureAceSkyVertexColorMaterialBase();
	/** Translucent vertex-color sky fill (no depth write) so clouds still composite in front. */
	UMaterialInterface* EnsureAceSkyColorFillMaterialBase();
	/** Rain sheets — translucent overlay with depth test disabled so streaks draw over buildings. */
	UMaterialInterface* EnsureAceWeatherTranslucentMaterialBase();
	/** Rain / lightning additive streaks — depth test disabled. */
	UMaterialInterface* EnsureAceWeatherAdditiveMaterialBase();

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "ACE|DAT")
	FString DatDirectory = TEXT("C:/Turbine/Asheron's Call");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|DAT")
	bool bAutoLoadOnInitialize = false;

	/**
	 * Load client_highres.dat (ACViewer always does). Portal often stores Texture stubs with
	 * Length==0; pixel payloads live in highres. Disable only to speed first-index experiments.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|DAT")
	bool bLoadHighResDat = true;

	/** Used by the background loader to hand opened DAT handles back to the game thread. */
	struct FBackgroundLoadResult
	{
		TUniquePtr<FACEDatDatabase> Portal;
		TUniquePtr<FACEDatDatabase> HighRes;
		TUniquePtr<FACEDatDatabase> Cell;
		bool bOk = false;
		double ElapsedSeconds = 0.0;
		FString Error;
	};

	void InstallBackgroundLoadResult(TSharedRef<FBackgroundLoadResult> Result, int32 Generation);
	void BeginCellDatBackgroundLoad();
	void InstallCellDatBackgroundLoadResult(TUniquePtr<FACEDatDatabase>&& Cell, int32 Generation);

private:
	bool ApplySetupToProceduralMeshInternal(UProceduralMeshComponent* ProcMesh, int32 SetupId, float WorldScale, bool bEnableCollision, int32 PlacementId, bool bParticleSafe, bool bForceDrawCollision = false, bool bIndoorStairCollision = false, bool bOutdoorLit = false, bool bReverseFaces = false);

	/**
	 * Landblock-local Unreal XY triangles for building footprints.
	 * FloorTris / ShellTris — flora skip + outdoor land collision ignore under interiors.
	 * Never used to CSG-punch the heightfield (banned — always full LScape outdoors).
	 */
	struct FInteriorHoleTri
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		FVector2D C = FVector2D::ZeroVector;
		/** Average floor Z (Unreal cm, landblock-local). */
		float Z = 0.f;
	};

	struct FBuildingHoleMask
	{
		/** EnvCell upward faces — flora skip + IsUnderBuildingInterior. */
		TArray<FInteriorHoleTri> FloorTris;
		/** EnvCell downward faces — retained for diagnostics / future open-sky gates. */
		TArray<FInteriorHoleTri> CeilingTris;
		/** Setup ground hull — flora/scenery skip only. */
		TArray<FInteriorHoleTri> ShellTris;
		/** Inset of ShellTris — flora/courtyard skip only. */
		TArray<FInteriorHoleTri> InsetShellTris;
		/** Indoor EnvCell ids reached from this building's portals (full CellId). */
		TSet<uint32> IndoorCellIds;
		/** Index into FACEDatLandblockInfo::Buildings for this landblock. */
		int32 LandblockBuildingIndex = INDEX_NONE;
	};

	struct FBuildingInteriorMask
	{
		TArray<FBuildingHoleMask> Buildings;
		/** False when indoor EnvCell meshes were missing during build — rebuild later. */
		bool bComplete = true;
	};

	void ClearLoadedState();
	const FBuildingInteriorMask& GetOrBuildBuildingInteriorFootprints(uint32 LandblockId, float WorldScale);
	static bool PointInTriList(const FVector2D& Pt, const TArray<FInteriorHoleTri>& Tris);
	static bool PointInSingleTri(const FVector2D& Pt, const FInteriorHoleTri& Tri);
	/** True when XY sits on an EnvCell floor footprint (flora / land-collision ignore). */
	static bool PointInInteriorHole(const FVector2D& Pt, float TerrainZ, const FBuildingInteriorMask& Mask);

	uint64 ComputeDatFingerprint() const;
	void TryStartDiskCacheMaintenance();
	void PumpLandblockBuildQueue();
	void OnLandblockBuildComplete(uint32 LandblockId, float WorldScale, int32 Generation, bool bOk, TSharedPtr<FACEBuiltLandblockMesh> Mesh);

	void PumpSetupBuildQueue();
	void OnSetupBuildComplete(uint32 SetupId, float WorldScale, uint64 CacheKey, int32 Generation, bool bOk, TSharedPtr<FACEBuiltSetupMesh> Mesh);

	void PumpEnvCellBuildQueue();
	void OnEnvCellBuildComplete(uint32 EnvCellId, float WorldScale, uint64 CacheKey, int32 Generation, bool bOk, TSharedPtr<FACEBuiltEnvCellMesh> Mesh);

	static uint64 MakeScaleCacheKey(uint32 Id, float WorldScale);
	static uint64 MakeSetupCacheKey(uint32 Id, float WorldScale, int32 PlacementId);
	static uint64 MakeSetupStaticMeshCacheKey(uint32 Id, float WorldScale, int32 PlacementId, bool bEnableCollision, bool bParticleGfx, bool bStencilHoleClip, bool bOutdoorLit, bool bPhysicsCollisionOnly = false);

	struct FPendingLandblockBuild
	{
		uint32 LandblockId = 0;
		float WorldScale = 100.f;
	};

	struct FPendingScaledMeshBuild
	{
		uint32 Id = 0;
		float WorldScale = 1.f;
		uint64 CacheKey = 0;
		/** ACE.Entity.Enum.Placement — static Stab scenery uses Resting (101). */
		int32 PlacementId = 0;
	};

	TUniquePtr<FACEDatDatabase> PortalDat;
	TUniquePtr<FACEDatDatabase> HighResDat;
	TUniquePtr<FACEDatDatabase> CellDat;
	TUniquePtr<FACEDatTextureResolver> TextureResolver;
	TUniquePtr<FACESetupMeshBuilder> Builder;
	TUniquePtr<FACELandblockMeshBuilder> LandBuilder;
	TUniquePtr<FACEEnvCellMeshBuilder> EnvCellBuilder;
	TUniquePtr<FACELandSurfaceAtlas> LandAtlas;
	mutable TUniquePtr<FACEDatMotionPlayer> MotionPlayer;
	friend class FACERetailMeshApplicationTest;
	friend class FACEMissingDatLoginTest;
	friend class FACETerrainArrivalTest;
	friend class FACEDoorwayGeometryCacheTest;
	friend class FACESetupMetadataCacheTest;
	// Entries cannot move with TMap growth while a renderer is consuming their parts.
	TMap<uint64, TSharedPtr<const FACEBuiltSetupMesh>> SetupMeshCache;
	TMap<uint32, TSharedPtr<FACEBuiltLandblockMesh>> LandblockCache;
	TMap<uint32, FACEDatLandblockInfo> LandblockInfoCache;
	struct FCachedDoorwayGeometry
	{
		TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Apertures;
		uint64 LastUse = 0;
	};
	struct FSetupRuntimeMetadata
	{
		float StepUp = .5f, StepDown = .5f, Height = 2.f, Radius = .5f;
		FVector3f SelectionOrigin = FVector3f::ZeroVector;
		float SelectionRadius = 0.f;
		uint32 Animation = 0, Script = 0, ScriptTable = 0, SoundTable = 0;
		TArray<FACEDatCollisionShape> Shapes;
		bool bPhysicsBSP = false, bValid = false;
		uint64 LastUse = 0;
	};
	const FSetupRuntimeMetadata* FindSetupRuntimeMetadata(uint32 SetupId);
	TMap<uint32, FSetupRuntimeMetadata> SetupRuntimeMetadataCache;
	FSetupRuntimeMetadata UncachedSetupRuntimeMetadata;
	uint64 SetupRuntimeMetadataUse = 0;
	TMap<uint64, FCachedDoorwayGeometry> DoorwayGeometryCache;
	uint64 DoorwayGeometryUse = 0;
	TMap<uint32, FBuildingInteriorMask> BuildingInteriorFootprints;
	TMap<uint64, FACEBuiltEnvCellMesh> EnvCellMeshCache;

	UPROPERTY(Transient)
	TMap<uint64, TObjectPtr<UStaticMesh>> SetupStaticMeshCache;

	/** Section materials for BindSetupStaticMeshMaterials (FastBuild does not keep MIDs on the GPU). */
	TMap<uint64, TArray<TObjectPtr<UMaterialInterface>>> SetupStaticMeshSlotMaterials;

	UPROPERTY(Transient)
	TSet<TObjectPtr<UTexture2D>> RuntimeTextureKeep;

	double MemoryTrimAccum = 0.0;
	FACEDatRegionSceneTables RegionSceneTables;
	FACEDatRegionSky RegionSky;
	FACEDatRegionSoundInfo RegionSoundInfo;
	TMap<uint32, FACEDatScene> SceneCache;
	TMap<uint32, FACEDatParticleEmitterInfo> ParticleEmitterInfoCache;
	TMap<uint32, FACEDatPhysicsScript> PhysicsScriptCache;
	TMap<uint32, FACEDatPhysicsScriptTable> PhysicsScriptTableCache;
	TMap<uint32, FACEDatWave> WaveCache;
	TMap<uint32, FACEDatSoundTable> SoundTableCache;
	struct FSpellInfoCacheEntry
	{
		FString Name;
		FString Description;
		uint32 IconDid = 0;
		uint32 Bitfield = 0;
		uint32 NonComponentTargetType = 0;
		uint32 DisplayOrder = 0;
		uint32 School = 0;
		uint32 Power = 0;
		uint32 IconPowerLevel = 0;
		uint32 BaseMana = 0;
		float BaseRange = 0.f, RangeMod = 0.f;
		double Duration = 0.0;
		uint32 TrainedCost = 0;
		/** SkillTable only: 1 = usable untrained, 2 = needs trained. */
		uint32 MinLevel = 0;
		uint32 FormulaW = 0, FormulaX = 0, FormulaY = 0, FormulaZ = 0;
		uint32 FormulaAttr1 = 0, FormulaAttr2 = 0;
	};
	bool EnsureSpellTableLoaded();
	bool EnsureSpellComponentTableLoaded();
	TArray<FACESpellComponentInfo> SpellComponentCache;
	bool bSpellComponentTableLoaded = false;
	bool EnsureContractTableLoaded();
	TMap<uint32, FACEDatContractInfo> ContractCache;
	bool bContractTableLoaded = false;
	bool EnsureChatPoseTableLoaded();
	TMap<uint32, FSpellInfoCacheEntry> SpellInfoCache;
	bool bSpellTableLoaded = false;
	struct FChatPoseEntry
	{
		FString Command;
		FString MyEmote;
		FString OtherEmote;
	};
	TMap<FString, FChatPoseEntry> ChatPoseCache;
	bool bChatPoseTableLoaded = false;
	TMap<uint32, FSpellInfoCacheEntry> SkillInfoCache;
	bool bSkillTableLoaded = false;
	bool bXpTableLoaded = false;
	TArray<uint32> AttributeXpList;
	TArray<uint32> VitalXpList;
	TArray<uint32> TrainedSkillXpList;
	TArray<uint32> SpecializedSkillXpList;
	TArray<uint64> CharacterLevelXPList;
	bool EnsureXpTableLoaded();
	bool bRegionSceneTablesLoaded = false;
	bool bRegionSkyLoaded = false;
	bool bRegionSoundLoaded = false;
	bool bPortalLoaded = false;
	bool bBackgroundLoadInProgress = false;
	FString DatLoadError;
	FString FailedDatDirectory;
	bool bCellBackgroundLoadInProgress = false;
	bool bPendingDiskCacheMaintenance = false;
	bool bWorldStreamingAllowed = false;
	bool bLightweightStreaming = false;
	bool bInPortalSpace = false;
	bool bIndoorEnvironment = false;
	bool bBuildingInteriorFill = false;
	int32 BackgroundLoadGeneration = 0;
	int32 SkyMaterialGeneration = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> InvisibleCollisionMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TexturedMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LandMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LandLookOutMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PortalStencilWriterMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PortalDepthApertureMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BuildingShellMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> MaskedMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OutdoorLitMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OutdoorLitMaskedMaterialBase;

	bool bLandLookOutClip = false;
	bool bLandLookInClip = false;
	FVector LandLookOutCam = FVector::ZeroVector;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LandPortalViewTexture;
	TArray<FVector4f> LandPortalViewRecords;
	int32 LandPortalViewCount = 0;
	void ApplyOutdoorPortalLandClipToMid(UMaterialInstanceDynamic* Mid) const;
	bool bLandInteriorClip = false;
	FVector LandInteriorClipMin = FVector::ZeroVector;
	FVector LandInteriorClipMax = FVector::ZeroVector;
	void ApplyLandInteriorClipToMid(UMaterialInstanceDynamic* Mid) const;
	/** Indoor CanSeeOutside: land uses CustomStencil look-out MIDs (abandoned). */
	bool bPortalLookOutLand = false;

	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> LookOutLandMaterialCache;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TranslucentMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> AdditiveMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ParticleAdditiveMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ParticleTranslucentMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SkyTranslucentMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SkyTranslucentWrapMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SkyOpaqueMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SkyAdditiveMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SkyVertexColorMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SkyColorFillMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WeatherTranslucentMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WeatherAdditiveMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LandGpuMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LandGpuMaterialInstance;

	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> TexturedMaterialCache;

	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> StaticMeshMaterialCache;

	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> BuildingShellMaterialCache;

	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> OutdoorLitMaterialCache;
	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> EnvCellMaterialCache;
	FLinearColor InteriorAmbient = FLinearColor::White;
	FLinearColor OutdoorInteriorAmbient = FLinearColor(0.15f, 0.15f, 0.15f);
	bool bInteriorUsesOutdoorAmbient = true;

	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> ParticleMaterialCache;

	// Visible components own these resources. Cache lookup must not retain every
	// terrain blend or appearance encountered during a long portal/travel session.
	TMap<uint32, TWeakObjectPtr<UMaterialInstanceDynamic>> LandMaterialCache;
	float WorldEmissiveScale = 0.14f;
	FLinearColor WorldAmbientTint = FLinearColor::White;
	float SceneryEmissiveScale = 0.65f;
	float WorldFogStartCm = 20000.f;
	float WorldFogEndCm = 90000.f;
	float WorldFogAmount = 1.f;
	FLinearColor WorldFogColor = FLinearColor(0.55f, 0.62f, 0.78f);
	void ApplyDistanceFogToMid(UMaterialInstanceDynamic* Mid) const;
	void ApplyDistanceFogToMaterialInstance(UMaterialInterface* Inst) const;
	void ApplyWorldDistanceFogToLandscapes() const;

	TMap<uint32, TWeakObjectPtr<UTexture2D>> LandTextureCache;

	/** Appearance-resolved SurfaceTexture MIDs (key includes part + swap ids). */
	UPROPERTY(Transient)
	TMap<uint64, TObjectPtr<UMaterialInstanceDynamic>> ResolvedMaterialCache;
	UPROPERTY(Transient)
	TMap<uint8, TObjectPtr<UMaterialInterface>> ExaminationMaterialBases;
	UPROPERTY() TMap<uint8, TObjectPtr<UMaterialInterface>> InteriorObjectMaterialBases;
	TMap<TWeakObjectPtr<UMaterialInterface>, TWeakObjectPtr<UMaterialInterface>> InteriorObjectMaterials;
	TMap<TWeakObjectPtr<UMaterialInterface>, TWeakObjectPtr<UMaterialInstanceDynamic>> WorldObjectMaterials;
	TMap<TWeakObjectPtr<UMaterialInstanceDynamic>, bool> WorldLightingInstances;

	UPROPERTY(Transient)
	TMap<uint64, TObjectPtr<UTexture2D>> ResolvedTextureCache;

	int32 AppliedSurfaceUnpackVersion = 0;
	bool bLandMeshReloadRequested = false;

	/** Invalidates in-flight landblock workers when DATs are replaced / torn down. */
	int32 LandblockBuildGeneration = 0;
	FThreadSafeCounter ActiveLandblockBuilds;
	TArray<FPendingLandblockBuild> PendingLandblockBuilds;
	TSet<uint32> PendingLandblockIds;
	TSet<uint32> FailedLandblockMeshIds;
	uint64 CachedDatFingerprint = 0;

	int32 SetupBuildGeneration = 0;
	FThreadSafeCounter ActiveSetupBuilds;
	TArray<FPendingScaledMeshBuild> PendingSetupBuilds;
	TSet<uint64> PendingSetupKeys;
	TSet<uint64> FailedSetupKeys;

	int32 EnvCellBuildGeneration = 0;
	FThreadSafeCounter ActiveEnvCellBuilds;
	TArray<FPendingScaledMeshBuild> PendingEnvCellBuilds;
	TSet<uint64> PendingEnvCellKeys;
	TSet<uint64> FailedEnvCellKeys;
};
