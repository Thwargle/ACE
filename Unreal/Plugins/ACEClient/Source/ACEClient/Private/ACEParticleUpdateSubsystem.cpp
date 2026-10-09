#include "ACEParticleUpdateSubsystem.h"
#include "ACEParticleBatchComponent.h"
#include "Async/ParallelFor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<int32> CVarParticleParallelUpdates(TEXT("ace.Particles.ParallelUpdates"), 1,
	TEXT("Prepare crowded particle vertex updates on workers without changing visuals. 0 uses immediate serial updates."));
static TAutoConsoleVariable<int32> CVarParticleParallelVertices(TEXT("ace.Particles.ParallelMinVertices"), 131072,
	TEXT("Minimum dirty particle vertices across a world before parallel preparation is worthwhile."));

bool UACEParticleUpdateSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World=Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UACEParticleUpdateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostTickHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&UACEParticleUpdateSubsystem::OnWorldPostActorTick);
}

void UACEParticleUpdateSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
	Pending.Reset();
	Super::Deinitialize();
}

bool UACEParticleUpdateSubsystem::Queue(UACEParticleBatchComponent* Batch)
{
	check(IsInGameThread());
	if(CVarParticleParallelUpdates.GetValueOnGameThread()==0 || !GetWorld()->bInTick) return false;
	if(!Batch->bFlushQueued)
	{
		Batch->bFlushQueued=true;
		Pending.Add(Batch);
	}
	return true;
}

void UACEParticleUpdateSubsystem::OnWorldPostActorTick(UWorld* World,ELevelTick TickType,float DeltaSeconds)
{
	if(World==GetWorld()) FlushPending();
}

void UACEParticleUpdateSubsystem::FlushPending()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ACE_WorldParticleUpdates);
	TArray<UACEParticleBatchComponent*,TInlineAllocator<128>> Work;
	LastVertexCount=0;
	for(const auto& Weak:Pending)
	{
		auto* Batch=Weak.Get();
		if(!IsValid(Batch)) continue;
		Batch->bFlushQueued=false;
		if(Batch->IsBeingDestroyed() || !Batch->BeginParticleFlush()) continue;
		Work.Add(Batch);
		LastVertexCount+=Batch->GetPendingVertexCount();
	}
	Pending.Reset();
	LastBatchCount=Work.Num();
	bLastParallel=Work.Num()>1 && LastVertexCount>=FMath::Max(1,CVarParticleParallelVertices.GetValueOnGameThread())
		&& CVarParticleParallelUpdates.GetValueOnGameThread()!=0 && FApp::ShouldUseThreadingForPerformance();
	if(!bLastParallel)
	{
		// Keep each small batch hot in cache instead of preparing the whole scene
		// before the serial submission pass. Empty worlds do no task dispatch.
		for(auto* Batch:Work) { Batch->PrepareParticleVertices(); Batch->SubmitParticleVertices(); }
		return;
	}
	// All component mutation is finished before dispatch. Each task owns separate
	// CPU arrays, and the synchronous join precedes all UObject/render updates.
	// Tiny sprite emitters are common. Dispatching one task per emitter costs
	// more than the transforms; group them into a bounded number of coarse jobs.
	const int32 Jobs=FMath::Clamp(Work.Num()/8,2,8);
	ParallelFor(Jobs,[&](int32 Job)
	{
		const int32 Begin=Work.Num()*Job/Jobs,End=Work.Num()*(Job+1)/Jobs;
		for(int32 Index=Begin;Index<End;++Index) Work[Index]->PrepareParticleVertices();
	});
	for(auto* Batch:Work) Batch->SubmitParticleVertices();
}
