#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
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
private:
	bool bApplied = false;
	uint32 PoseFlags = 0;
	FTransform RetailRoot;
};
