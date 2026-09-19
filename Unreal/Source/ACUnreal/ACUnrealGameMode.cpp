#include "ACUnrealGameMode.h"
#include "ACEWcTerrainRuntime.h"
#include "ACELoginStreamingWorldSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACUnrealPawn.h"
#include "ACELoginHUD.h"
#include "Camera/PlayerCameraManager.h"
#include "ACEWorldPresenterComponent.h"
#include "ACETerrainPresenterComponent.h"
#include "Engine/WorldComposition.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "GameFramework/PlayerStart.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "ACEDatSubsystem.h"
#include "ACESkyDomeActor.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "ACETerrainChunkActor.h"
#include "ACEWorldEntityActor.h"
#include "ACERegionSceneryActor.h"
#include "Components/PrimitiveComponent.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/WorldSettings.h"
#include "WorldPartition/WorldPartition.h"

namespace ACUnrealWorldComposition
{
	static void RelocatePlayerStartsForLogin(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			It->SetActorLocation(FVector::ZeroVector);
		}
	}

	static void HideLoadedWcLandscapes(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			It->SetActorHiddenInGame(true);
			It->SetActorEnableCollision(false);
		}
	}
}

AACUnrealGameMode::AACUnrealGameMode()
{
	// TEMP DIAGNOSTIC: ticking so we can draw an always-on-screen "I am alive" message.
	PrimaryActorTick.bCanEverTick = true;

	PlayerControllerClass = AACEPlayerController::StaticClass();
	DefaultPawnClass = AACUnrealPawn::StaticClass();
	// Guaranteed-visible backdrop/diagnostic overlay (Canvas-drawn, independent of UMG/Slate) —
	// automatically hides itself once the UMG login widget is confirmed on screen.
	HUDClass = AACELoginHUD::StaticClass();

	WorldPresenter = CreateDefaultSubobject<UACEWorldPresenterComponent>(TEXT("WorldPresenter"));
	WorldPresenter->bSkipSelf = true;
	WorldPresenter->WorldScale = 100.f;
	WorldPresenter->bApplyDatAppearance = true;
	WorldPresenter->DatAppearancesPerTick = 3;
	WorldPresenter->SpawnsPerTick = 6;

	TerrainPresenter = CreateDefaultSubobject<UACETerrainPresenterComponent>(TEXT("TerrainPresenter"));
	TerrainPresenter->WorldScale = 100.f;
	TerrainPresenter->LoadRadius = 5;   // Retail LScape mid_radius default → 11×11
	TerrainPresenter->UnloadRadius = 6;
	TerrainPresenter->FullDetailRadius = 1;
	TerrainPresenter->LandblocksPerTick = 4;
}

void AACUnrealGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	ApplyLoginStreamingSetup();
}

void AACUnrealGameMode::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	ApplyLoginStreamingSetup();
}

void AACUnrealGameMode::ApplyLoginStreamingSetup()
{
	if (UWorld* World = GetWorld())
	{
		const bool bWcMap = World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0;
		const bool bWpMap = World->GetWorldPartition() != nullptr;
		if (bWcMap || bWpMap)
		{
			ACUnrealWorldComposition::RelocatePlayerStartsForLogin(World);
		}
	}
}

APawn* AACUnrealGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	UWorld* World = GetWorld();
	const bool bWcMap = World && World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0;
	const bool bWpMap = World && World->GetWorldPartition() != nullptr;
	if (bWcMap || bWpMap)
	{
		TSubclassOf<APawn> PawnClass = DefaultPawnClass ? DefaultPawnClass : TSubclassOf<APawn>(AACUnrealPawn::StaticClass());
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
		return World->SpawnActor<APawn>(PawnClass, SpawnTransform, Params);
	}
	return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
}

void AACUnrealGameMode::StartPlay()
{
	Super::StartPlay();

	// Retail AC is flat-shaded. Our ACE textures use Unlit (emissive path); bloom makes them
	// glow, and the OpenWorld directional sun makes DefaultLit look like pitch-black shadows.
	// DAT fog is material-driven. Auto-exposure/bloom wash DefaultLit land.
	if (IConsoleVariable* Bloom = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BloomQuality")))
	{
		Bloom->Set(0, ECVF_SetByCode);
	}
	if (IConsoleVariable* EyeAdapt = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptationQuality")))
	{
		EyeAdapt->Set(0, ECVF_SetByCode);
	}
	if (IConsoleVariable* Gi = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod")))
	{
		Gi->Set(0, ECVF_SetByCode);
	}
	if (IConsoleVariable* Refl = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod")))
	{
		Refl->Set(0, ECVF_SetByCode);
	}
	if (UWorld* World = GetWorld())
	{
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			Settings->bForceNoPrecomputedLighting = true;
		}
	}
	// Force 100% screen percentage — temporal upsampling / dynamic res causes DAT texture flicker.
	if (IConsoleVariable* ScreenPct = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		ScreenPct->Set(100.f, ECVF_SetByCode);
	}
	if (IConsoleVariable* DynRes = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicRes.OperationMode")))
	{
		DynRes->Set(0, ECVF_SetByCode);
	}
	if (IConsoleVariable* TAAUp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TemporalAA.Upsampling")))
	{
		TAAUp->Set(0, ECVF_SetByCode);
	}
	// DAT sky cube disables vertex fog so height fog does not wash it. Terrain uses DAT WorldFog.
	if (IConsoleVariable* FogCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Fog")))
	{
		FogCvar->Set(1, ECVF_SetByCode);
	}
	if (IConsoleVariable* Atmo = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SupportSkyAtmosphere")))
	{
		Atmo->Set(0, ECVF_SetByCode);
	}

	if (GEngine && GetWorld() && !GetWorld()->IsPlayInEditor())
	{
		if (UGameUserSettings* Settings = GEngine->GetGameUserSettings())
		{
			Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
			const FIntPoint Desktop = Settings->GetDesktopResolution();
			if (Desktop.X > 0 && Desktop.Y > 0)
			{
				Settings->SetScreenResolution(Desktop);
			}
			Settings->ApplySettings(false);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ACE: AACUnrealGameMode::StartPlay — DefaultPlayerControllerClass=%s"),
		PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("NULL"));

	if (UWorld* World = GetWorld())
	{
		if (UACELoginStreamingWorldSubsystem* Streaming = World->GetSubsystem<UACELoginStreamingWorldSubsystem>())
		{
			Streaming->MaintainLoginStreamingHold();
		}

		bool bAllowWorldStream = false;
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				bAllowWorldStream = Dat->IsWorldStreamingAllowed();
			}
		}

		if (TerrainPresenter && !bAllowWorldStream)
		{
			TerrainPresenter->LoadRadius = 0;
			TerrainPresenter->UnloadRadius = 0;
			TerrainPresenter->SetComponentTickEnabled(false);
		}

		if (UWorldComposition* WorldComposition = World->WorldComposition)
		{
			const int32 NumTiles = WorldComposition->GetTilesList().Num();
			if (NumTiles > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("ACE: World Composition map (%d tiles) — using baked WC terrain, disabling procedural TerrainPresenter"),
					NumTiles);
				bHideTemplateLevelActors = false;
				if (TerrainPresenter)
				{
					TerrainPresenter->LoadRadius = 0;
					TerrainPresenter->UnloadRadius = 0;
					TerrainPresenter->SetComponentTickEnabled(false);
				}
			}
		}
		else if (World->GetWorldPartition())
		{
			UE_LOG(LogTemp, Log, TEXT("ACE: World Partition map — disabling procedural TerrainPresenter during login"));
			if (TerrainPresenter)
			{
				TerrainPresenter->LoadRadius = 0;
				TerrainPresenter->UnloadRadius = 0;
				TerrainPresenter->SetComponentTickEnabled(false);
			}
		}
	}

	ACUnrealWorldComposition::HideLoadedWcLandscapes(GetWorld());

	if (UWorld* World = GetWorld())
	{
		const bool bWcMap = World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0;
		const float DatDelay = bWcMap ? 0.2f : 0.05f;
		GetWorldTimerManager().SetTimer(LoginDatLoadTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
				{
					Dat->BeginBackgroundLoad();
				}
			}
		}), DatDelay, false);
	}

	ForceLoginUIAttempts = 0;
	GetWorldTimerManager().SetTimer(ForceLoginUITimerHandle, this, &AACUnrealGameMode::TryForceShowLoginUI, 0.25f, true);

	if (bHideTemplateLevelActors)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &AACUnrealGameMode::HideConflictingTemplateActors);
	}

	// Retail DAT-driven sky — WC maps defer spawn until after login / enter-world.
	EnsureAceSkySpawned();

	if (bShowACEDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(6100, 8.f, FColor::Cyan, TEXT("ACE: GameMode::StartPlay ran"));
	}
}

void AACUnrealGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (UWorld* World = GetWorld())
	{
		if (UACELoginStreamingWorldSubsystem* Streaming = World->GetSubsystem<UACELoginStreamingWorldSubsystem>())
		{
			bool bAllowWorldStream = true;
			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
				{
					bAllowWorldStream = Dat->IsWorldStreamingAllowed();
				}
			}
			if (bAllowWorldStream)
			{
				Streaming->RestoreWorldStreamingIfAllowed();
				if (!bAceSkyReady)
				{
					EnsureAceSkySpawned();
				}
				if (World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0)
				{
					ACEWcTerrainRuntime::MaintainLandscapePresentation(World);
				}
				if (TerrainPresenter && TerrainPresenter->LoadRadius == 0)
				{
					const bool bWcMap = World->WorldComposition
						&& World->WorldComposition->GetTilesList().Num() > 0;
					if (bWcMap)
					{
						TerrainPresenter->RestoreWcBakedCollisionStreaming(1, 1);
					}
					else
					{
						// OpenWorld is World Partition with no WC tiles — still DAT LScape.
						TerrainPresenter->RestoreProceduralStreamingAfterLogin(5, 6);
					}
				}
			}
			else
			{
				Streaming->MaintainLoginStreamingHold();
			}
		}
	}

	// The OpenWorld template map streams its Landscape/skydome actors in via World Partition,
	// so a single StartPlay pass can miss actors that load in a moment later as the local
	// player's streaming source becomes active. Re-scan at a low, cheap frequency rather than
	// every tick.
	if (bHideTemplateLevelActors && !bTemplateActorScanComplete)
	{
		TimeSinceLastTemplateActorScan += DeltaSeconds;
		if (TimeSinceLastTemplateActorScan >= 1.f)
		{
			TimeSinceLastTemplateActorScan = 0.f;
			HideConflictingTemplateActors();
		}
	}

	// Per-tick diagnostic message — gated behind bShowACEDebug (default false); the login UI is
	// reliable now, so this was previously just adding permanent on-screen clutter.
	if (bShowACEDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(6101, 0.f, FColor::Cyan,
			FString::Printf(TEXT("ACE: GameMode alive — LoginUIAttempts=%d"), ForceLoginUIAttempts));
	}
}

void AACUnrealGameMode::HideConflictingTemplateActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 HiddenLandscapes = 0;
	int32 SoftenedDirectionalLights = 0;
	int32 HiddenSkyMeshes = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsHidden())
		{
			continue;
		}
		if (Actor->IsA<AACESkyDomeActor>() || Actor->ActorHasTag(FName(TEXT("ACERuntimeLight")))
			|| Actor->IsA<AACEEnvCellActor>() || Actor->IsA<AACELandblockActor>()
			|| Actor->IsA<AACETerrainChunkActor>() || Actor->IsA<AACEWorldEntityActor>()
			|| Actor->IsA<AACERegionSceneryActor>())
		{
			continue;
		}

		if (Actor->IsA<ALandscapeProxy>())
		{
			Actor->SetActorHiddenInGame(true);
			Actor->SetActorEnableCollision(false);
			TArray<UPrimitiveComponent*> Prims;
			Actor->GetComponents<UPrimitiveComponent>(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				if (!Prim)
				{
					continue;
				}
				Prim->SetVisibility(false);
				Prim->SetCastShadow(false);
				Prim->bCastHiddenShadow = false;
			}
			++HiddenLandscapes;
			continue;
		}

		if (AExponentialHeightFog* HeightFog = Cast<AExponentialHeightFog>(Actor))
		{
			if (UExponentialHeightFogComponent* Fog = HeightFog->FindComponentByClass<UExponentialHeightFogComponent>())
			{
				Fog->SetFogDensity(0.f);
				Fog->SetFogMaxOpacity(0.f);
				Fog->SetVolumetricFog(false);
				Fog->SetVisibility(false);
				Fog->Deactivate();
			}
			Actor->SetActorHiddenInGame(true);
			++HiddenSkyMeshes;
			continue;
		}

		if (APostProcessVolume* PPV = Cast<APostProcessVolume>(Actor))
		{
			PPV->bEnabled = false;
			PPV->bUnbound = false;
			PPV->BlendWeight = 0.f;
			PPV->Settings.bOverride_DynamicGlobalIlluminationMethod = true;
			PPV->Settings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
			PPV->Settings.bOverride_ReflectionMethod = true;
			PPV->Settings.ReflectionMethod = EReflectionMethod::None;
			PPV->Settings.bOverride_bMegaLights = true;
			PPV->Settings.bMegaLights = false;
			++HiddenSkyMeshes;
			continue;
		}

		// ACE reuses the OpenWorld directional light as the DAT sun. Do not disable
		// CastShadows — that left DefaultLit LScape in a full-scene shadow (pitch black)
		// with no character/building/tree contacts. Only detach it from SkyAtmosphere.
		if (Actor->IsA<ADirectionalLight>())
		{
			if (UDirectionalLightComponent* DirLight = Actor->FindComponentByClass<UDirectionalLightComponent>())
			{
				if (DirLight->Intensity > 0.f || DirLight->IsVisible() || DirLight->bAtmosphereSunLight)
				{
					DirLight->SetAtmosphereSunLight(false);
					DirLight->SetCastShadows(false);
					DirLight->SetVisibility(false);
					DirLight->SetIntensity(0.f);
					++SoftenedDirectionalLights;
				}
			}
			continue;
		}

		// Default Unreal sky (SkyAtmosphere / volumetric clouds). Do not hide a dedicated
		// ASkyLight — ACE's ambient sky is one, and hiding it every second crushed land fill.
		bool bHadSkyComponent = false;
		if (USkyAtmosphereComponent* Atmo = Actor->FindComponentByClass<USkyAtmosphereComponent>())
		{
			Atmo->SetVisibility(false);
			Atmo->Deactivate();
			bHadSkyComponent = true;
		}
		if (UVolumetricCloudComponent* Cloud = Actor->FindComponentByClass<UVolumetricCloudComponent>())
		{
			Cloud->SetVisibility(false);
			Cloud->Deactivate();
			bHadSkyComponent = true;
		}
		if (bHadSkyComponent)
		{
			if (USkyLightComponent* SkyLight = Actor->FindComponentByClass<USkyLightComponent>())
			{
				SkyLight->SetVisibility(false);
			}
		}
		if (bHadSkyComponent)
		{
			Actor->SetActorHiddenInGame(true);
			++HiddenSkyMeshes;
			continue;
		}

		// Hide the OpenWorld skydome mesh — its "Is Sky" material paints the yellow
		// "DOES NOT COVER THAT PART OF THE SCREEN" warning. SkyAtmosphere still draws the sky.
		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);
		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			const UStaticMesh* Mesh = MeshComp ? MeshComp->GetStaticMesh() : nullptr;
			if (Mesh && Mesh->GetName().Contains(TEXT("Sky")))
			{
				Actor->SetActorHiddenInGame(true);
				Actor->SetActorEnableCollision(false);
				++HiddenSkyMeshes;
				break;
			}
		}
	}

	if (HiddenLandscapes > 0 || SoftenedDirectionalLights > 0 || HiddenSkyMeshes > 0)
	{
		QuietTemplateScans = 0;
		UE_LOG(LogTemp, Log, TEXT("ACE: HideConflictingTemplateActors — hid %d landscape, %d skydome-mesh, softened %d sun light(s)"),
			HiddenLandscapes, HiddenSkyMeshes, SoftenedDirectionalLights);
	}
	else
	{
		++QuietTemplateScans;
		if (QuietTemplateScans >= 3)
		{
			bTemplateActorScanComplete = true;
			UE_LOG(LogTemp, Warning, TEXT("ACE: HideConflictingTemplateActors latched — stopping world scan"));
		}
	}
}

void AACUnrealGameMode::EnsureAceSkySpawned()
{
	if (!bSpawnAceSky)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (TActorIterator<AACESkyDomeActor> It(World); It)
	{
		bAceSkyReady = true;
		return;
	}

	// The template sky is also present during account/creation screens. Disable its
	// real-time capture before waiting for world streaming, so preview captures do
	// not report a missing sky atmosphere or inherit an unrelated template sky.
	SuppressConflictingLevelSkyLights();
	bool bAllowWorldStream = false;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			bAllowWorldStream = Dat->IsWorldStreamingAllowed();
		}
	}
	if (!bAllowWorldStream)
	{
		return;
	}

	FActorSpawnParameters SkyParams;
	SkyParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FVector SkyLoc = FVector::ZeroVector;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			SkyLoc = PC->PlayerCameraManager->GetCameraLocation();
		}
		else if (APawn* Pawn = PC->GetPawn())
		{
			SkyLoc = Pawn->GetActorLocation();
		}
	}
	if (World->SpawnActor<AACESkyDomeActor>(SkyLoc, FRotator::ZeroRotator, SkyParams))
	{
		bAceSkyReady = true;
		UE_LOG(LogTemp, Log, TEXT("ACE: spawned AACESkyDomeActor (deferred after login hold)"));
	}
}

void AACUnrealGameMode::SuppressConflictingLevelSkyLights()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA<AACESkyDomeActor>() || Actor->ActorHasTag(FName(TEXT("ACERuntimeLight"))))
		{
			continue;
		}

		bool bHadSkyComponent = false;
		if (USkyAtmosphereComponent* Atmo = Actor->FindComponentByClass<USkyAtmosphereComponent>())
		{
			Atmo->SetVisibility(false);
			Atmo->Deactivate();
			bHadSkyComponent = true;
		}
		if (UVolumetricCloudComponent* Cloud = Actor->FindComponentByClass<UVolumetricCloudComponent>())
		{
			Cloud->SetVisibility(false);
			Cloud->Deactivate();
			bHadSkyComponent = true;
		}

		if (USkyLightComponent* SkyLight = Actor->FindComponentByClass<USkyLightComponent>())
		{
			// Real-time capture without SkyAtmosphere paints black and spams the viewport warning.
			if (SkyLight->bRealTimeCapture) SkyLight->SetRealTimeCaptureEnabled(false);
			// Runtime ACE lighting owns ambient illumination. A standalone template
			// SkyLight has no atmosphere component but must still be suppressed.
			SkyLight->SetVisibility(false);
		}

		if (bHadSkyComponent)
		{
			Actor->SetActorHiddenInGame(true);
		}
	}
}

void AACUnrealGameMode::TryForceShowLoginUI()
{
	if (auto* Client = GetGameInstance()->GetSubsystem<UACEClientSubsystem>())
	{
		const auto State = Client->GetSessionState();
		if (State == EACESessionState::InWorld || State == EACESessionState::CharacterSelect)
		{
			GetWorldTimerManager().ClearTimer(ForceLoginUITimerHandle);
			return;
		}
	}
	++ForceLoginUIAttempts;

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	AACEPlayerController* AcePC = Cast<AACEPlayerController>(PC);

	UE_LOG(LogTemp, Warning, TEXT("ACE: TryForceShowLoginUI attempt %d — PC=%s AcePC=%s LoginUIVisible=%d"),
		ForceLoginUIAttempts,
		PC ? *PC->GetClass()->GetName() : TEXT("NULL"),
		AcePC ? TEXT("valid") : TEXT("NULL"),
		AcePC ? AcePC->IsLoginUIVisible() : 0);

	if (AcePC)
	{
		// Keep calling this every 0.25s until the widget is CONFIRMED actually composited into
		// the viewport (not just that the PlayerController/UObject exists) — AddToPlayerScreen
		// can silently fail early on if the GameViewport isn't ready yet, and ShowLoginUI() has
		// its own internal retry, but this is a defensive outer loop in case that path is ever
		// skipped (e.g. this GameMode running without AACEPlayerController::BeginPlay firing).
		AcePC->ShowLoginUI();
		if (AcePC->IsLoginUIVisible())
		{
			GetWorldTimerManager().ClearTimer(ForceLoginUITimerHandle);
			return;
		}
	}

	// 0.25s * 80 = 20s — generous enough to cover a slow "Play Standalone" window/viewport
	// startup while still eventually giving up if something is fundamentally broken.
	if (ForceLoginUIAttempts >= 80)
	{
		UE_LOG(LogTemp, Error, TEXT("ACE: TryForceShowLoginUI gave up after %d attempts — login UI never became visible"), ForceLoginUIAttempts);
		GetWorldTimerManager().ClearTimer(ForceLoginUITimerHandle);
	}
}
