#include "ACEParticleBatchComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/IConsoleManager.h"
#include "ACEParticleUpdateSubsystem.h"
#include "Engine/World.h"

static TAutoConsoleVariable<int32> CVarActiveParticlePrefix(TEXT("ace.Particles.ActivePrefix"), 1,
	TEXT("Upload/draw only live particle slots. Set 0 to compare the reserved-capacity path."));
static TAutoConsoleVariable<int32> CVarPrepareParticleBounds(TEXT("ace.Particles.PreparedBounds"), 1,
	TEXT("Compute exact particle bounds during worker vertex generation. 0 computes them during submission."));

void UACEParticleBatchComponent::InitializeParticles(int32 Capacity)
{
	MaxParticles = FMath::Max(1, Capacity);
	LastFlushedParticleCount = 0;
	Transforms.Reserve(MaxParticles); Opacities.Reserve(MaxParticles);
	Sections.SetNum(GetNumSections());
	for (int32 S=0; S<Sections.Num(); ++S)
	{
		const FProcMeshSection Original = *GetProcMeshSection(S);
		auto& Out = Sections[S]; Out.Source = Original.ProcVertexBuffer;
		Out.bUniformNormal=!Out.Source.IsEmpty();
		for(int32 V=1;V<Out.Source.Num();++V)
			Out.bUniformNormal &= Out.Source[V].Normal==Out.Source[0].Normal;
		Out.IndicesPerParticle = Original.ProcIndexBuffer.Num();
		const int32 N=Out.Source.Num(), Count=N*MaxParticles;
		Out.Positions.SetNumZeroed(Count); Out.Normals.SetNumZeroed(Count);
		Out.UVs.SetNumZeroed(Count); Out.Colors.Init(FColor(255,255,255,0), Count); Out.Tangents.SetNum(Count);
		TArray<int32> Indices; Indices.Reserve(Original.ProcIndexBuffer.Num()*MaxParticles);
		for(int32 P=0; P<MaxParticles; ++P)
		{
			for(uint32 I:Original.ProcIndexBuffer) Indices.Add(P*N+I);
			for(int32 I=0; I<N; ++I) { Out.UVs[P*N+I]=Out.Source[I].UV0; Out.Tangents[P*N+I]=Out.Source[I].Tangent; }
		}
		CreateMeshSection(S,Out.Positions,Indices,Out.Normals,Out.UVs,Out.Colors,Out.Tangents,false);
	}
}

int32 UACEParticleBatchComponent::AddParticle(const FTransform& WorldTransform, float Opacity)
{
	if(Transforms.Num()>=MaxParticles) return INDEX_NONE;
	const int32 Index=Transforms.Add(WorldTransform.GetRelativeTransform(GetComponentTransform()));
	Opacities.Add(Opacity); bDirty=true; return Index;
}
void UACEParticleBatchComponent::RemoveParticle(int32 Index)
{
	if(!Transforms.IsValidIndex(Index)) return;
	Transforms.RemoveAtSwap(Index,EAllowShrinking::No); Opacities.RemoveAtSwap(Index,EAllowShrinking::No); bDirty=true;
}
void UACEParticleBatchComponent::SetParticleVisual(int32 Index, const FTransform& WorldTransform, float Opacity)
{
	if(!Transforms.IsValidIndex(Index)) return;
	const FTransform Local = WorldTransform.GetRelativeTransform(GetComponentTransform());
	// Presentation can refresh at display cadence while a stationary glow's
	// lifecycle is unchanged. Do not upload identical vertex buffers each frame.
	if (Transforms[Index].Equals(Local, 1.e-8) && Opacities[Index] == Opacity) return;
	Transforms[Index]=Local; Opacities[Index]=Opacity; bDirty=true;
}
void UACEParticleBatchComponent::ClearParticles()
{
	Transforms.Reset(); Opacities.Reset(); bDirty=true;
}
void UACEParticleBatchComponent::FlushParticles()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ACE_ParticleBatchFlush);
	if(!bDirty && bLastFlushedUseActivePrefix==(CVarActiveParticlePrefix.GetValueOnGameThread()!=0)) return;
	if(auto* World=GetWorld(); World && IsRegistered())
		if(auto* Updates=World->GetSubsystem<UACEParticleUpdateSubsystem>(); Updates && Updates->Queue(this)) return;
	if(!BeginParticleFlush()) return;
	PrepareParticleVertices();
	SubmitParticleVertices();
}

bool UACEParticleBatchComponent::BeginParticleFlush()
{
	const bool bUseActivePrefix = CVarActiveParticlePrefix.GetValueOnGameThread() != 0;
	if(!bDirty && bLastFlushedUseActivePrefix == bUseActivePrefix) return false;
	bDirty=false;
	bLastFlushedUseActivePrefix = bUseActivePrefix;
	bPrepareBounds = bUseActivePrefix && CVarPrepareParticleBounds.GetValueOnGameThread() != 0;
	return true;
}

int64 UACEParticleBatchComponent::GetPendingVertexCount() const
{
	int64 Count=0;
	for(const auto& Section:Sections) Count+=int64(Section.Source.Num())*FMath::Max(Transforms.Num(),LastFlushedParticleCount);
	return Count;
}

void UACEParticleBatchComponent::PrepareParticleVertices(bool bOnWorker)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ACE_PrepareParticleVertices);
	// Serial updates are faster keeping the bounds calculation in submission;
	// only the worker path benefits from moving it off the game thread.
	bPrepareBounds &= bOnWorker;
	// Inactive slots start degenerate. Only slots that were active in the last
	// upload need clearing; a sparse emitter need not transform its full capacity.
	const int32 UpdateCount=FMath::Max(Transforms.Num(),LastFlushedParticleCount);
	for(int32 S=0; S<Sections.Num(); ++S)
	{
		auto& Out=Sections[S]; const int32 N=Out.Source.Num();
		Out.PreparedBounds.Init();
		for(int32 P=0; P<UpdateCount; ++P)
		{
			const bool Active=Transforms.IsValidIndex(P);
			const uint8 Alpha=Active ? uint8(FMath::Clamp(FMath::RoundToInt(Opacities[P]*255.f),0,255)) : 0;
			// Sprite/card vertices share a face normal. Rotate it once per particle,
			// while preserving distinct normals on authored three-dimensional meshes.
			const FVector Normal=Active && Out.bUniformNormal
				? Transforms[P].TransformVectorNoScale(Out.Source[0].Normal) : FVector::UpVector;
			for(int32 I=0; I<N; ++I)
			{
				const int32 V=P*N+I;
				Out.Positions[V]=Active ? Transforms[P].TransformPosition(Out.Source[I].Position) : FVector::ZeroVector;
				if (bPrepareBounds && Active) Out.PreparedBounds += Out.Positions[V];
				Out.Normals[V]=Active && !Out.bUniformNormal ? Transforms[P].TransformVectorNoScale(Out.Source[I].Normal) : Normal;
				Out.Colors[V]=Out.Source[I].Color;
				// The unbatched particle shader takes opacity from the material
				// surface/texture and OpacityMul, not DAT vertex-color alpha.
				// Many opaque RGB surfaces (rocks and blood) store zero in that
				// unused byte. Multiplying by it erased the entire batch.
				Out.Colors[V].A=Alpha;
			}
		}
	}
}

void UACEParticleBatchComponent::SubmitParticleVertices()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ACE_SubmitParticleVertices);
	for(int32 S=0; S<Sections.Num(); ++S)
	{
		auto& Out=Sections[S]; const int32 N=Out.Source.Num();
		// UVs and tangents never change after initialization.
		const int32 ActiveVertices = Transforms.Num() * N;
		if (!bLastFlushedUseActivePrefix ||
			!UpdateMeshSectionActivePrefix(S,
				MakeArrayView(Out.Positions.GetData(), ActiveVertices),
				MakeArrayView(Out.Normals.GetData(), ActiveVertices),
				MakeArrayView(Out.Colors.GetData(), ActiveVertices), Transforms.Num() * Out.IndicesPerParticle,
				bPrepareBounds ? &Out.PreparedBounds : nullptr))
		{
			UpdateMeshSection(S,Out.Positions,Out.Normals,{},Out.Colors,{});
		}
	}
	LastFlushedParticleCount=Transforms.Num();
}
