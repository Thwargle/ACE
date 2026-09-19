#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ACUnrealGameMode.generated.h"

class UACEWorldPresenterComponent;
class UACETerrainPresenterComponent;

/**
 * Default game mode for ACUnreal: ACEPlayerController + world/terrain presenters.
 * Press Play → login UI → Enter World.
 */
UCLASS()
class ACUNREAL_API AACUnrealGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AACUnrealGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PreInitializeComponents() override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void ApplyLoginStreamingSetup();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACEWorldPresenterComponent> WorldPresenter;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ACE")
	TObjectPtr<UACETerrainPresenterComponent> TerrainPresenter;

	/** Loud on-screen "I am alive" diagnostics — off by default now that login UI works
	 *  reliably. Flip on if you need to re-diagnose GameMode/PlayerController startup issues. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Debug")
	bool bShowACEDebug = false;

	/** The project's default map is the Engine's OpenWorld template (landscape + skydome mesh +
	 *  atmosphere), which we can't edit directly. Instead of authoring a blank level, we hide the
	 *  conflicting template landscape/skydome-mesh actors at runtime so ACE's own terrain/lighting
	 *  is what the player actually sees. Set false to restore the template scenery (e.g. for
	 *  debugging PIE viewport/camera issues in isolation from ACE). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE|Debug")
	bool bHideTemplateLevelActors = true;

	/** Spawn the retail DAT-driven sky (AACESkyDomeActor) — replaces the Unreal sky entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bSpawnAceSky = true;

	/** Spawn AACESkyDomeActor once login hold ends (WC maps defer past StartPlay). */
	void EnsureAceSkySpawned();

	/** Level SkyLights with real-time capture but no atmosphere paint black — disable them. */
	void SuppressConflictingLevelSkyLights();

protected:
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	/** Defensive retry in case the PlayerController's own BeginPlay->ShowLoginUI path didn't fire yet. */
	void TryForceShowLoginUI();

	/** Hides the OpenWorld template's Landscape (replaced by ACE terrain), skydome mesh,
	 *  SkyAtmosphere, and ExponentialHeightFog. The DAT sky cube is the atmosphere. */
	void HideConflictingTemplateActors();

	FTimerHandle ForceLoginUITimerHandle;
	FTimerHandle LoginDatLoadTimerHandle;
	int32 ForceLoginUIAttempts = 0;
	float TimeSinceLastTemplateActorScan = 0.f;
	int32 QuietTemplateScans = 0;
	bool bTemplateActorScanComplete = false;
	bool bAceSkyReady = false;
};
