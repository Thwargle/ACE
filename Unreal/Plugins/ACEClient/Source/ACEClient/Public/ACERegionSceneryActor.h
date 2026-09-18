#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ACERegionSceneryActor.generated.h"

class UACECharacterAppearanceComponent;
class UACEScriptComponent;

/**
 * RegionDesc outdoor scenery that needs Setup DefaultAnimation (birds, waving flora).
 * Static trees/bushes stay as procedural meshes on AACELandblockActor.
 *
 * Birds circle because DefaultAnimation frame 0 fires SetOmega while parts sit on a
 * radial offset — ScriptComponent applies that angular velocity each tick.
 */
UCLASS()
class ACECLIENT_API AACERegionSceneryActor : public AActor
{
	GENERATED_BODY()

public:
	AACERegionSceneryActor();

	/** Build DAT Setup parts and loop DefaultAnimation when present. */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	bool InitializeFromSetup(int32 SetupId, float Scale = 1.f, float InWorldScale = 100.f, bool bEnableCollision = false);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACECharacterAppearanceComponent> Appearance;

	/** Receives SetOmega / particle hooks from DefaultAnimation playback. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACEScriptComponent> ScriptComponent;

	UPROPERTY(BlueprintReadOnly, Category = "ACE")
	float WorldScale = 100.f;
};
