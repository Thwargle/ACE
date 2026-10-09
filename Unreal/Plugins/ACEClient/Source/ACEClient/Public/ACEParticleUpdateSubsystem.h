#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ACEParticleUpdateSubsystem.generated.h"

class UACEParticleBatchComponent;

/** Shares particle vertex work across the world without changing effect density or cadence. */
UCLASS()
class ACECLIENT_API UACEParticleUpdateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool Queue(UACEParticleBatchComponent* Batch);
	/** Game-thread flush; workers only prepare private CPU arrays, never touch render state. */
	void FlushPending();
	int32 GetLastBatchCount() const { return LastBatchCount; }
	int64 GetLastVertexCount() const { return LastVertexCount; }
	bool WasLastFlushParallel() const { return bLastParallel; }
private:
	void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	TArray<TWeakObjectPtr<UACEParticleBatchComponent>> Pending;
	FDelegateHandle PostTickHandle;
	int32 LastBatchCount = 0;
	int64 LastVertexCount = 0;
	bool bLastParallel = false;
};
