#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VR/ACEVRPose.h"
#include "ACEVRRemoteAvatarComponent.generated.h"

/** Cosmetic poses run after retail locomotion; they never move physics bodies. */
UCLASS()
class ACECLIENT_API UACEVRRemoteAvatarComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UACEVRRemoteAvatarComponent();
	virtual void TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Tick) override;
	USceneComponent* GetHeldAnchor(int32 ParentLocation, bool TwoHanded = false) const;
	bool UpdateMissileAttachment(class AACEWorldEntityActor* Item);
private:
	bool bApplied = false;
	uint32 PoseFlags = 0;
	FTransform RetailRoot;
	FACEVRPose VisualPose;
	TMap<TWeakObjectPtr<class AACEWorldEntityActor>,uint64> EquipmentRevisions;
	UPROPERTY(Transient) TObjectPtr<class UProceduralMeshComponent> BowString;
};
