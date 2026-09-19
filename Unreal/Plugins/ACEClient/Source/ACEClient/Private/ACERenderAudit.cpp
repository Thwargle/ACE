#include "ACERenderAudit.h"
#include "ACEScriptComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACEClientSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "Engine/GameInstance.h"
#include "ACELandblockActor.h"
#include "ACETerrainChunkActor.h"
#include "ACEEnvCellActor.h"
#include "ACERegionSceneryActor.h"
#include "ACESkyDomeActor.h"
#include "ACELoadingScreenActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/WidgetComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshResources.h"
#include "RHIStats.h"
#include "RHI.h"

static TAutoConsoleVariable<int32> GACEMovementDebug(TEXT("ace.Movement.Debug"), 0,
	TEXT("Log requested and collision-resolved player movement once per second."));

namespace
{
	bool MainEligible(const UPrimitiveComponent* P)
	{
		return P->IsRegistered() && P->IsVisible() && !P->bHiddenInGame
			&& !P->GetOwner()->IsHidden() && P->bRenderInMainPass
			&& !P->bVisibleInSceneCaptureOnly && !P->bOwnerNoSee;
	}
	FString Category(const AActor* A)
	{
		if (A->IsA<AACEWorldEntityActor>()) return TEXT("Entities / equipment");
		if (A->IsA<AACELandblockActor>() || A->IsA<AACETerrainChunkActor>()) return TEXT("Terrain / outdoor scenery");
		if (A->IsA<AACEEnvCellActor>()) return TEXT("Interiors");
		if (A->IsA<AACERegionSceneryActor>()) return TEXT("Animated scenery");
		if (A->IsA<AACESkyDomeActor>()) return TEXT("Sky / weather");
		if (A->IsA<AACELoadingScreenActor>()) return TEXT("Portal tunnel");
		return A->GetClass()->GetName();
	}
}

FACERenderAudit FACERenderAudit::Collect(UWorld* World)
{
	FACERenderAudit Out;
	if (!World) return Out;
	TMap<FString, FString> Identities, StaticInstances;
	TSet<UTexture2D*> LandscapeTextures;
	auto Identity = [&](const FString& Key, AActor* A)
	{
		if (const auto* First = Identities.Find(Key))
		{
			++Out.DuplicateIdentityCount;
			if (Out.DuplicateIdentities.Num() < 32)
				Out.DuplicateIdentities.Add(Key + TEXT(": ") + *First + TEXT(" + ") + A->GetName());
		}
		else Identities.Add(Key, A->GetName());
	};
	auto StaticInstance = [&](UStaticMeshComponent* Mesh, const FTransform& Transform, int32 Index)
	{
		// Include materials: overlaid geometry with a different shader can be intentional.
		FString Key = Mesh->GetStaticMesh()->GetPathName() + TEXT("|") + Transform.ToString();
		for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
			Key += TEXT("|") + GetPathNameSafe(Mesh->GetMaterial(I));
		const FString Name = FString::Printf(TEXT("%s[%d]"), *Mesh->GetPathName(), Index);
		if (const auto* First = StaticInstances.Find(Key))
		{
			++Out.OverlappingStaticInstanceCount;
			if (Out.OverlappingStaticInstances.Num() < 32)
				Out.OverlappingStaticInstances.Add(*First + TEXT(" + ") + Name);
		}
		else StaticInstances.Add(Key, Name);
	};
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A) || A->IsActorBeingDestroyed()) continue;
		auto& G = Out.Groups.FindOrAdd(Category(A));
		++G.Actors;
		TInlineComponentArray<UACEScriptComponent*> Scripts(A);
		for (auto* Script : Scripts) Script->AccumulateParticleAudit(Out.ParticleEmitters, Out.DegradedEmitters, Out.ActiveParticles, Out.ParticleLights);
		if (auto* Entity = Cast<AACEWorldEntityActor>(A); Entity && Entity->GetACEGuid())
			Identity(FString::Printf(TEXT("GUID %08X"), Entity->GetACEGuid()), A);
		if (auto* Land = Cast<AACELandblockActor>(A); Land && Land->LandblockId)
			Identity(FString::Printf(TEXT("Landblock %08X"), Land->LandblockId), A);
		if (auto* Cell = Cast<AACEEnvCellActor>(A); Cell && Cell->EnvCellId)
			Identity(FString::Printf(TEXT("EnvCell %08X"), Cell->EnvCellId), A);
		if (auto* Chunk = Cast<AACETerrainChunkActor>(A))
			Identity(FString::Printf(TEXT("TerrainChunk %08X"), Chunk->ChunkOriginLandblockId), A);
		TInlineComponentArray<UPrimitiveComponent*> Primitives(A);
		for (auto* P : Primitives)
		{
			if (!IsValid(P) || !P->IsRegistered()) continue;
			++G.Primitives;
			const bool bMain = MainEligible(P);
			if (bMain) ++G.MainPrimitives;
			const bool bShadow = P->CastShadow && P->bCastDynamicShadow
				&& (bMain || P->bCastHiddenShadow);
			auto Section = [&](UMaterialInterface* Material)
			{
				if (bMain && Material && Material->GetMaterial()->GetName().StartsWith(TEXT("M_ACELandLit")))
				{
					UTexture* Bound = nullptr;
					Material->GetTextureParameterValue(TEXT("ACETexture"), Bound);
					if (auto* Texture = Cast<UTexture2D>(Bound); Texture && !LandscapeTextures.Contains(Texture))
					{
						LandscapeTextures.Add(Texture);
						++Out.LandscapeTextureFormats.FindOrAdd(FString::Printf(TEXT("%dx%d %s mips=%d"),
							Texture->GetSizeX(), Texture->GetSizeY(), GPixelFormats[Texture->GetPixelFormat()].Name, Texture->GetNumMips()));
						Out.LandscapeTextureBytes += Texture->CalcTextureMemorySizeEnum(TMC_AllMips);
					}
				}
				if (bMain)
				{
					++G.Sections;
					if (Material && IsTranslucentBlendMode(Material->GetBlendMode())) ++G.TranslucentSections;
				}
				if (bShadow) ++G.ShadowSections;
			};
			if (auto* Proc = Cast<UProceduralMeshComponent>(P))
			{
				for (int32 I = 0; I < Proc->GetNumSections(); ++I)
					if (const auto* S = Proc->GetProcMeshSection(I); S && S->bSectionVisible && S->GetRenderIndexCount() > 0)
						Section(Proc->GetMaterial(I));
			}
			else if (auto* Mesh = Cast<UStaticMeshComponent>(P); Mesh && Mesh->GetStaticMesh())
			{
				auto* ISM = Cast<UInstancedStaticMeshComponent>(Mesh);
				const int32 Instances = ISM ? ISM->GetInstanceCount() : 1;
				if (bMain) G.Instances += Instances;
				if (Instances > 0)
					if (const auto* Data = Mesh->GetStaticMesh()->GetRenderData(); Data && Data->LODResources.Num())
						for (const auto& S : Data->LODResources[0].Sections)
							if (S.NumTriangles) Section(Mesh->GetMaterial(S.MaterialIndex));
				if (bMain)
				{
					for (int32 I = 0; I < Instances; ++I)
					{
						FTransform T = Mesh->GetComponentTransform();
						if (ISM && !ISM->GetInstanceTransform(I, T, true)) continue;
						StaticInstance(Mesh, T, I);
					}
				}
			}
		}
		TInlineComponentArray<USceneCaptureComponent2D*> Captures(A);
		for (auto* C : Captures)
			if (C->IsRegistered() && C->TextureTarget)
				Out.Captures.Add(FString::Printf(TEXT("%s %dx%d everyFrame=%d onMove=%d showOnly=%d list=%d"),
					*C->GetPathName(), C->TextureTarget->SizeX, C->TextureTarget->SizeY,
					C->bCaptureEveryFrame, C->bCaptureOnMovement,
					C->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList, C->ShowOnlyComponents.Num()));
	}
	return Out;
}

void FACERenderAudit::Log() const
{
	UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: GPU timestamp queries supported=%d (missing GPU trace data is not zero GPU cost)"),
		GRHIGlobals.SupportsTimestampRenderQueries ? 1 : 0);
	for (const TCHAR* Name : { TEXT("r.Android.SupportsTimestampQueries"), TEXT("r.CullInstances"),
		TEXT("ace.Render.CachedActorDraws"), TEXT("ace.Render.CacheDoorwayGeometry"), TEXT("ace.Dat.CacheSetupMetadata") })
		if (const auto* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: %s=%s"), Name, *Variable->GetString());
	UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: particleEmitters=%d degraded=%d activeParticles=%d particleLights=%d"),
		ParticleEmitters, DegradedEmitters, ActiveParticles, ParticleLights);
	for (const auto& Format : LandscapeTextureFormats)
		UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: landscapeTexture %s unique=%d"), *Format.Key, Format.Value);
	UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: landscapeTexture allocatedEstimateMiB=%.2f (unique textures, including mipmaps)"),
		double(LandscapeTextureBytes) / (1024.0 * 1024.0));
	UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: most recent RHI frame draws=%d primitives=%d (all passes; asynchronous snapshot)"), GNumDrawCallsRHI[0], GNumPrimitivesDrawnRHI[0]);
	UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: scene inventory BEFORE frustum/occlusion/distance culling; sections are NOT GPU draw calls. Stereo, depth and shadows add passes."));
	TArray<FString> Keys; Groups.GetKeys(Keys); Keys.Sort();
	for (const auto& Key : Keys)
	{
		const auto& G = Groups[Key];
		UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: %s actors=%d primitives=%d main=%d sections=%d translucent=%d shadow=%d instances=%d"),
			*Key, G.Actors, G.Primitives, G.MainPrimitives, G.Sections, G.TranslucentSections, G.ShadowSections, G.Instances);
	}
	UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: duplicateIdentities=%d overlappingStaticInstances=%d captures=%d"),
		DuplicateIdentityCount, OverlappingStaticInstanceCount, Captures.Num());
	for (const auto& S : DuplicateIdentities) UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: DUPLICATE %s"), *S);
	for (const auto& S : OverlappingStaticInstances) UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: OVERLAP (inspect authored placement) %s"), *S);
	for (const auto& S : Captures) UE_LOG(LogTemp, Display, TEXT("ACE RenderAudit: CAPTURE %s"), *S);
}

static FAutoConsoleCommandWithWorld GACERenderAuditCommand(TEXT("ace.RenderAudit"),
	TEXT("Log renderable scene inventory, duplicate object identities, overlapping static instances and scene captures. Run while stationary and again after portal travel."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { FACERenderAudit::Collect(World).Log(); }));

static FAutoConsoleCommandWithWorldAndArgs GACEEntityAuditCommand(TEXT("ace.EntityAudit"),
	TEXT("Compare one entity's received position and velocity with its displayed transform. Argument: hexadecimal GUID."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() != 1 || !World->GetGameInstance()) return;
		const uint32 Guid = FCString::Strtoui64(*Args[0], nullptr, 16);
		auto* Client = World->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
		FACEWorldObject Obj;
		if (!Client || !Client->GetWorldObject(Guid, Obj)) return;
		for (TActorIterator<AACEWorldEntityActor> It(World); It; ++It)
		{
			if (static_cast<uint32>(It->ACEGuid) != Guid) continue;
			const FVector Received = Obj.Position.ToUnrealLocation(It->WorldScale);
			UE_LOG(LogTemp, Display, TEXT("ACE EntityAudit: %08X '%s' desc=%08X physics=%08X cell=%08X received=%s drawn=%s delta=%s hasVelocity=%d velocity=%s positionVelocity=%s grounded=%d"),
				Guid, *Obj.Name, Obj.ObjectDescriptionFlags, Obj.PhysicsState, Obj.Position.CellId,
				*Received.ToString(), *It->GetActorLocation().ToString(), *(It->GetActorLocation()-Received).ToString(),
				Obj.bHasVelocity, *Obj.Velocity.ToString(), *Obj.Position.Velocity.ToString(), Obj.Position.bIsGrounded);
		}
	}));
