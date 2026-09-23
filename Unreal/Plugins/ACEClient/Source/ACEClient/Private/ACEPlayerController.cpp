#include "ACEPlayerController.h"
#include "ACEKeyboardRouter.h"
#include "HAL/IConsoleManager.h"
#include "VR/ACEVRComponent.h"
#include "ACEClientBuild.h"
#include "ACERuntimeOptions.h"
#include "ACEInputBindings.h"
#include "ACERetailPortalAnimation.h"
#include "ACEClientSubsystem.h"
#include "ACEPlaySessionRedirect.h"
#include "Engine/WorldComposition.h"
#include "ACEOpcodes.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEScriptComponent.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEDatFileTypes.h"
#include "ACELoginWidget.h"
#include "ACEHoverTooltipWidget.h"
#include "ACEMouseCursorWidget.h"
#include "UI/ACEGameHUDWidget.h"
#include "UI/ACEFrameRateWidget.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICharSelectBinder.h"
#include "UI/ACEUICharGenBinder.h"
#include "UI/ACEUIFlow.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIElement.h"
#include "UI/ACEUITypes.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACELoadingScreenActor.h"
#include "ACEWcTerrainRuntime.h"
#include "ACESkyDomeActor.h"
#include "ACEMovementComponent.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACECombatStance.h"
#include "ACEWorldPresenterComponent.h"
#include "ACETerrainPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACEEnvCellActor.h"
#include "ACELandblockActor.h"
#include "ACETerrainChunkActor.h"
#include "ProceduralMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "UObject/GarbageCollection.h"
#include "RenderCommandFence.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "ACEBodySweep.h"
#include "ACEVisibleObjectPick.h"
#include "WorldCollision.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "Engine/HitResult.h"
#include "Camera/PlayerCameraManager.h"
#include "ACECameraRetail.h"
#include "ACECameraSettings.h"
#include "ACELedgeSlide.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
// The rendered water sheet remains at the heightfield plane. Retail body
// support is below it by DAT waterDepth; a Chaos sphere/capsule against that
// sheet would undo the already depth-correct ground sample. While wading,
// retain scenery/interior collision and solve terrain using the DAT path.
void FilterWadingTerrain(UWorld& World, UACEDatSubsystem* Dat, const FVector& From,
    const FVector& To, float Scale, FCollisionQueryParams& Params)
{
    if (!Dat || (Dat->GetOutdoorWaterDepthCm(From.X,From.Y,Scale)<=0.f
        && Dat->GetOutdoorWaterDepthCm(To.X,To.Y,Scale)<=0.f)) return;
    for (TActorIterator<AACELandblockActor> It(&World); It; ++It)
        if (It->TerrainMesh) Params.AddIgnoredComponent(It->TerrainMesh.Get());
    for (TActorIterator<AACETerrainChunkActor> It(&World); It; ++It)
        if (It->TerrainMesh) Params.AddIgnoredComponent(It->TerrainMesh.Get());
}
}

AACEPlayerController::AACEPlayerController()
{
	bShowMouseCursor = true;
}

void AACEPlayerController::BeginPlay()
{
	ACERuntimeOptions::Apply();
	Super::BeginPlay();
	if (IsLocalController()) KeyboardRouter=FACEKeyboardRouter::Create(this,[this]()
	{
		return Client && Client->GetSessionState()==EACESessionState::InWorld && !IsVRActive()
			&& !ACEInputBindings::IsEditing()
			&& !(DatGameplayBinder && DatGameplayBinder->IsChatEntryFocused())
			&& !(GameHUDWidget && GameHUDWidget->IsChatEntryFocused());
	});
	if (IsLocalController()) ACEClientBuild::UpdateWindowTitle(GetWorld(), IsVRActive());
#if WITH_EDITOR
	// Standalone/PIE is a network client even while the editor window lacks focus.
	// UEditorEngine otherwise clamps it to 3 fps. Scope the exemption to this client
	// lifetime; do not change the user's global editor performance preferences.
	if (GEditor && IsLocalController())
	{
		auto Delegate = UEditorEngine::FShouldDisableCPUThrottling::CreateWeakLambda(this, [this]()
		{
			return IsVRActive() || (Client && (Client->GetSessionState() == EACESessionState::InWorld
				|| Client->GetSessionState() == EACESessionState::EnteringWorld));
		});
		EditorThrottleDelegate = Delegate.GetHandle();
		GEditor->ShouldDisableCPUThrottlingDelegates.Add(MoveTemp(Delegate));
	}
#endif
	UE_LOG(LogTemp, Warning, TEXT("ACE: AACEPlayerController::BeginPlay — IsLocalController=%d"), IsLocalController());
	if (UGameInstance* GI = GetGameInstance())
	{
		Client = GI->GetSubsystem<UACEClientSubsystem>();
		if (Client)
		{
			Client->OnEnteredWorld.AddDynamic(this, &AACEPlayerController::HandleEnteredWorld);
			Client->OnPlayerTeleportStarted.AddDynamic(this, &AACEPlayerController::HandlePlayerTeleportStarted);
			Client->OnPositionUpdate.AddDynamic(this, &AACEPlayerController::HandlePositionUpdate);
			Client->OnMotionUpdate.AddDynamic(this, &AACEPlayerController::HandleMotionUpdate);
			Client->OnPhysicsStateUpdate.AddDynamic(this, &AACEPlayerController::HandlePhysicsStateUpdate);
			Client->OnCharacterList.AddDynamic(this, &AACEPlayerController::HandleCharacterList);
			Client->OnSessionStateChanged.AddDynamic(this, &AACEPlayerController::HandleSessionStateChanged);
			Client->OnObjectCreated.AddDynamic(this, &AACEPlayerController::HandleObjectCreated);
			Client->OnVitalsUpdated.AddDynamic(this, &AACEPlayerController::HandleVitalsUpdated);
			if (TSharedPtr<FACESession> Sess = Client->GetSession())
			{
				Sess->OnUseDone.RemoveAll(this);
				Sess->OnUseDone.AddUObject(this, &AACEPlayerController::HandleUseDone);
				Sess->OnMoveToFailed.RemoveAll(this);
				Sess->OnMoveToFailed.AddUObject(this, &AACEPlayerController::HandleMoveToFailed);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ACE: AACEPlayerController::BeginPlay — UACEClientSubsystem NOT found on GameInstance"));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ACE: AACEPlayerController::BeginPlay — IsGameWorld=%d HasLocalPlayer=%d"),
		GetWorld() ? GetWorld()->IsGameWorld() : -1, GetLocalPlayer() ? 1 : 0);

	if (IsLocalController())
	{
		if (ACEPlaySessionRedirect::GPendingEnteredWorldAfterTravel.IsSet())
		{
			const ACEPlaySessionRedirect::FPendingEnteredWorldAfterTravel Pending =
				ACEPlaySessionRedirect::GPendingEnteredWorldAfterTravel.GetValue();
			ACEPlaySessionRedirect::GPendingEnteredWorldAfterTravel.Reset();
			GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Pending]()
			{
				HandleEnteredWorld(Pending.PlayerGuid, Pending.SpawnPosition);
			}));
		}

		if (ULocalPlayer* LP = GetLocalPlayer())
		{
			if (LP->ViewportClient)
			{
				LP->ViewportClient->EngineShowFlags.SetFog(true);
				LP->ViewportClient->EngineShowFlags.SetAtmosphere(false);
				LP->ViewportClient->EngineShowFlags.SetDynamicShadows(false);
			}
		}
		DeferredShowLoginUI();
	}
}

void AACEPlayerController::EnsureDatIntroCanvas()
{
	if (!IsLocalController() || !bUseDatDrivenHud || !Client)
	{
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			if (!Dat->IsDatReady())
			{
				Dat->BeginBackgroundLoad();
			}
		}
	}

	if (!DatCanvasWidget)
	{
		DatCanvasWidget = CreateWidget<UACEUICanvasWidget>(this, UACEUICanvasWidget::StaticClass());
		if (DatCanvasWidget)
		{
			DatCanvasWidget->InitializeCanvas(Client->GetUIElementManager());
			DatCanvasWidget->SetResourceResolver(Client->GetUIResourceResolver());
		}
	}
	if (!DatCanvasWidget)
	{
		return;
	}

	if (UACEUIFlow* Flow = Client->GetUIFlow())
	{
		Flow->SetMode(ACEUI::EACEUIFlowMode::Intro);
	}
	if (UACEUILayoutResolver* Layout = Client->GetUILayoutResolver())
	{
		Layout->LoadLayout(ACEUI::LayoutId::Intro);
	}
	if (UACEUIResourceResolver* Res = Client->GetUIResourceResolver())
	{
		Res->ResolveTexture(0x0600610Fu);
	}
	if (!IsVRActive() && !DatCanvasWidget->IsInViewport())
	{
		DatCanvasWidget->AddToPlayerScreen(8000);
	}
	DatCanvasWidget->SetVisibility(ESlateVisibility::Visible);
	UE_LOG(LogTemp, Log, TEXT("ACE: UIFlow Intro layout 0x21000001 (splash under credentials)"));
}

void AACEPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	KeyboardRouter.Reset();
	if (IsLocalController()) ACERuntimeOptions::ApplyDesktopUIScale(FIntPoint::ZeroValue,true);
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->ShouldDisableCPUThrottlingDelegates.RemoveAll([this](const auto& Delegate)
		{
			return Delegate.GetHandle() == EditorThrottleDelegate;
		});
	}
#endif
	SetSkyWeatherEnabled(true);
	if (LoadingScreenActor)
	{
		LoadingScreenActor->HideAllVisuals();
		LoadingScreenActor->Destroy();
		LoadingScreenActor = nullptr;
	}
	bEnterWorldLoading = false;
	bWorldRevealActive = false;

	// Prefer a clean CharacterLogOff so the account is not left orphaned online.
	if (Client)
	{
		Client->Logout();
		Client->OnEnteredWorld.RemoveAll(this);
		Client->OnPlayerTeleportStarted.RemoveAll(this);
		Client->OnPositionUpdate.RemoveAll(this);
		Client->OnMotionUpdate.RemoveAll(this);
		Client->OnPhysicsStateUpdate.RemoveAll(this);
		Client->OnCharacterList.RemoveAll(this);
		Client->OnSessionStateChanged.RemoveAll(this);
		Client->OnObjectCreated.RemoveAll(this);
		Client->OnVitalsUpdated.RemoveAll(this);
		if (TSharedPtr<FACESession> Sess = Client->GetSession())
		{
			Sess->OnUseDone.RemoveAll(this);
			Sess->OnMoveToFailed.RemoveAll(this);
		}
	}

	DestroyGameHUD();
	if(FrameRateWidget){FrameRateWidget->RemoveFromParent();FrameRateWidget=nullptr;}
	Super::EndPlay(EndPlayReason);
}

void AACEPlayerController::DeferredShowLoginUI()
{
	// Defer to next frame so PIE can finish its first Slate/viewport draw before building login widgets.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AACEPlayerController::ShowLoginUI);
	}
}

bool AACEPlayerController::IsLoginUIVisible() const
{
	if (const auto* VR = GetPawn() ? GetPawn()->FindComponentByClass<UACEVRComponent>() : nullptr;
		VR && VR->IsActive()) return VR->IsLoginPanelReady();
	if (!LoginWidget || !LoginWidget->IsInViewport())
	{
		return false;
	}

	// IsInViewport() only means the widget is COMPOSITED into the viewport's widget tree — it
	// says nothing about whether its allocated slot actually has a non-zero size. Right after
	// AddToPlayerScreen, the slot can still be sitting at a zero-sized rect (viewport pixel size
	// wasn't known yet), in which case the widget is technically "in the viewport" but 100%
	// invisible. Require a real, non-zero size before calling it "visible" so callers (e.g. the
	// diagnostic HUD) don't hide themselves prematurely.
	const FVector2D CachedGeomSize = LoginWidget->GetCachedGeometry().GetLocalSize();
	if (CachedGeomSize.X > 1.f && CachedGeomSize.Y > 1.f)
	{
		return true;
	}

	const FVector2D Desired = LoginWidget->GetDesiredSize();
	return Desired.X > 1.f && Desired.Y > 1.f;
}

bool AACEPlayerController::IsSessionDisconnected() const
{
	return !Client || Client->GetSessionState() == EACESessionState::Disconnected;
}

void AACEPlayerController::ShowLoginUI()
{
	UWorld* World = GetWorld();
	const bool bHasGameViewport = World && World->GetGameViewport() != nullptr;

	// Never stream landblocks / world entities on the login screen.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			Dat->SetWorldStreamingAllowed(false);
		}
	}

	// Hide default pawn / sky so DAT Intro splash + credentials card are the only UI.
	if (APawn* P = GetPawn())
	{
		P->SetActorHiddenInGame(true);
		P->SetActorEnableCollision(false);
		// Keep WC from streaming Dereth landscape tiles at the real PlayerStart while login UI runs.
		P->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (World)
	{
		for (TActorIterator<AACESkyDomeActor> It(World); It; ++It)
		{
			if (AACESkyDomeActor* Sky = *It)
			{
				Sky->SetActorHiddenInGame(true);
				Sky->SetWeatherEnabled(false);
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ACE: ShowLoginUI called (existing LoginWidget=%s, InViewport=%d, HasLocalPlayer=%d, HasGameViewport=%d)"),
		LoginWidget ? TEXT("valid") : TEXT("NULL"),
		LoginWidget ? LoginWidget->IsInViewport() : 0,
		GetLocalPlayer() ? 1 : 0,
		bHasGameViewport ? 1 : 0);

	EnsureDatIntroCanvas();

	if (!LoginWidget)
	{
		TSubclassOf<UACELoginWidget> ClassToUse = LoginWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UACELoginWidget::StaticClass();
		}
		UE_LOG(LogTemp, Warning, TEXT("ACE: ShowLoginUI — creating widget of class %s"), *ClassToUse->GetName());

		LoginWidget = CreateWidget<UACELoginWidget>(this, ClassToUse);
		if (!LoginWidget)
		{
			UE_LOG(LogTemp, Error, TEXT("ACE: ShowLoginUI — CreateWidget FAILED for class %s"), *ClassToUse->GetName());
			if (bShowACEDebug && GEngine)
			{
				GEngine->AddOnScreenDebugMessage(6002, 8.f, FColor::Red,
					TEXT("ACE: ShowLoginUI - CreateWidget FAILED"));
			}
			return;
		}
	}
	LoginWidget->SetUseDatIntroBackdrop(true);
	if (IsVRActive())
	{
		// The VR component owns this widget's world-space presentation. Login retries
		// must not attach a fullscreen copy over the headset's hands and keyboard.
		LoginWidget->SetVisibility(ESlateVisibility::Visible);
		ApplyLoginUIInputMode();
		return;
	}

	if (LoginWidget->IsInViewport())
	{
		// Widget is already actually on screen — re-assert visibility/input and viewport slot.
		UE_LOG(LogTemp, Warning, TEXT("ACE: ShowLoginUI — widget already in viewport, re-asserting visibility/input"));
		LoginWidget->SetVisibility(ESlateVisibility::Visible);
		LoginWidget->SetUseDatIntroBackdrop(true);
		DesiredSizeRetryAttempts = 0;
		ApplyViewportSizeToLoginWidget();
		ApplyLoginUIInputMode();
		return;
	}

	// The UObject exists but was never (successfully) composited into the viewport yet.
	// AddToPlayerScreen/AddToViewport silently return false (no crash, no visible error in
	// Standalone) if World->GetGameViewport() isn't ready — this is extremely common right
	// after BeginPlay in "Play Standalone" while the game window is still being created.
	if (!bHasGameViewport)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE: ShowLoginUI — no GameViewport yet, will retry"));
		ScheduleShowLoginUIRetry();
		return;
	}

	if (AttachLoginWidgetToScreen())
	{
		ApplyLoginUIInputMode();
		ShowLoginUIRetryAttempts = 0;
		UE_LOG(LogTemp, Warning, TEXT("ACE: ShowLoginUI — widget attached to player screen (ZOrder=9999) — IsInViewport=%d DesiredSize=%s"),
			LoginWidget->IsInViewport() ? 1 : 0, *LoginWidget->GetDesiredSize().ToString());

		if (bShowACEDebug && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(6001, 8.f, FColor::Yellow,
				TEXT("ACE: Login widget CREATED + AddToPlayerScreen (confirmed IsInViewport)"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ACE: ShowLoginUI — AddToPlayerScreen returned FALSE (GetGameViewport/OwningLocalPlayer not ready), will retry"));
		if (bShowACEDebug && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(6003, 3.f, FColor::Red,
				TEXT("ACE: AddToPlayerScreen FAILED — retrying..."));
		}
		ScheduleShowLoginUIRetry();
	}
}

bool AACEPlayerController::AttachLoginWidgetToScreen()
{
	if (!LoginWidget)
	{
		return false;
	}

	// AddToPlayerScreen (rather than AddToViewport) ties the widget to this controller's
	// LocalPlayer/viewport explicitly — more reliable than AddToViewport when the game
	// viewport window is still being created (e.g. "Play In New Window"). It returns false
	// (without crashing or logging anything visible) if the viewport isn't ready yet, so the
	// return value MUST be checked.
	if (!LoginWidget->AddToPlayerScreen(9999))
	{
		return false;
	}

	LoginWidget->SetVisibility(ESlateVisibility::Visible);
	LoginWidget->SetIsFocusable(true);

	DesiredSizeRetryAttempts = 0;
	ApplyViewportSizeToLoginWidget();

	return LoginWidget->IsInViewport();
}

void AACEPlayerController::ApplyViewportSizeToLoginWidget()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DesiredSizeRetryTimerHandle);
	}

	if (!LoginWidget || !LoginWidget->IsInViewport())
	{
		return;
	}

	FVector2D ViewportSizePx = FVector2D::ZeroVector;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSizePx);
	}
	if (ViewportSizePx.X <= 1.f || ViewportSizePx.Y <= 1.f)
	{
		if (const ULocalPlayer* LP = GetLocalPlayer())
		{
			if (LP->ViewportClient && LP->ViewportClient->Viewport)
			{
				ViewportSizePx = FVector2D(LP->ViewportClient->Viewport->GetSizeXY());
			}
		}
	}
	if (ViewportSizePx.X <= 1.f || ViewportSizePx.Y <= 1.f)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* GVC = World->GetGameViewport())
			{
				if (GVC->Viewport)
				{
					ViewportSizePx = FVector2D(GVC->Viewport->GetSizeXY());
				}
			}
		}
	}

	const bool bViewportSizeValid = ViewportSizePx.X > 1.f && ViewportSizePx.Y > 1.f;
	if (!bViewportSizeValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE: ApplyViewportSizeToLoginWidget — viewport size not ready yet"));
		ScheduleDesiredSizeRetry();
		return;
	}

	// Set the complete stretch slot together. SetPositionInViewport resets anchors
	// to (0,0), silently turning this into an auto-sized top-left widget. The lobby's
	// anchored canvas needs the allocated viewport, not its tiny desired size.
	if (UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get())
	{
		FGameViewportWidgetSlot Slot = Viewport->GetWidgetSlot(LoginWidget);
		Slot.Anchors = FAnchors(0.f, 0.f, 1.f, 1.f);
		Slot.Alignment = FVector2D::ZeroVector;
		Slot.Offsets = FMargin(0.f);
		Viewport->SetWidgetSlot(LoginWidget, Slot);
	}
	LoginWidget->ForceLayoutPrepass();

	const FVector2D ContentDesiredSize = LoginWidget->GetDesiredSize();
	const FVector2D CachedGeomSize = LoginWidget->GetCachedGeometry().GetLocalSize();
	UE_LOG(LogTemp, Warning,
		TEXT("ACE: ApplyViewportSizeToLoginWidget — ViewportSizePx=%s ContentDesiredSize=%s CachedGeom=%s"),
		*ViewportSizePx.ToString(),
		*ContentDesiredSize.ToString(),
		*CachedGeomSize.ToString());

	if (ContentDesiredSize.IsNearlyZero() || CachedGeomSize.X <= 1.f || CachedGeomSize.Y <= 1.f)
	{
		ScheduleDesiredSizeRetry();
	}
}

void AACEPlayerController::ScheduleDesiredSizeRetry()
{
	++DesiredSizeRetryAttempts;

	// ~15s of retries at 0.15s intervals — same budget as ScheduleShowLoginUIRetry.
	constexpr int32 MaxDesiredSizeRetryAttempts = 100;
	if (DesiredSizeRetryAttempts > MaxDesiredSizeRetryAttempts)
	{
		UE_LOG(LogTemp, Error, TEXT("ACE: ScheduleDesiredSizeRetry — giving up after %d attempts, viewport size never became valid"),
			DesiredSizeRetryAttempts);
		return;
	}

	if (DesiredSizeRetryAttempts <= 3)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &AACEPlayerController::ApplyViewportSizeToLoginWidget);
			return;
		}
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(DesiredSizeRetryTimerHandle, this, &AACEPlayerController::ApplyViewportSizeToLoginWidget, 0.15f, false);
	}
}

void AACEPlayerController::ApplyLoginUIInputMode()
{
	if (IsVRActive()) { SetInputMode(FInputModeGameOnly()); bShowMouseCursor = false; return; }
	if (!LoginWidget)
	{
		return;
	}
	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetWidgetToFocus(LoginWidget->TakeWidget());
	SetInputMode(InputMode);
}

void AACEPlayerController::ScheduleShowLoginUIRetry()
{
	++ShowLoginUIRetryAttempts;

	// ~15s of retries at 0.15s intervals — generous enough for even a slow window/viewport
	// startup, but bounded so we don't retry forever if something is fundamentally broken.
	constexpr int32 MaxRetryAttempts = 100;
	if (ShowLoginUIRetryAttempts > MaxRetryAttempts)
	{
		UE_LOG(LogTemp, Error, TEXT("ACE: ScheduleShowLoginUIRetry — giving up after %d attempts, GameViewport never became ready"), ShowLoginUIRetryAttempts);
		if (bShowACEDebug && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(6004, 10.f, FColor::Red,
				TEXT("ACE: Login UI FAILED — GameViewport never became ready"));
		}
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ShowLoginUIRetryTimerHandle, this, &AACEPlayerController::ShowLoginUI, 0.15f, false);
	}
}

void AACEPlayerController::HideLoginUI()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShowLoginUIRetryTimerHandle);
		World->GetTimerManager().ClearTimer(DesiredSizeRetryTimerHandle);
		// ShowLoginUI hides the sky actor — restore it when credentials leave.
		for (TActorIterator<AACESkyDomeActor> It(World); It; ++It)
		{
			if (AACESkyDomeActor* Sky = *It)
			{
				Sky->SetActorHiddenInGame(false);
			}
		}
	}
	if (LoginWidget)
	{
		LoginWidget->RemoveFromParent();
		LoginWidget = nullptr;
	}
	ApplyInWorldInputMode();
}

UACEVRComponent* AACEPlayerController::GetVRComponent() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UACEVRComponent>() : nullptr;
}

void AACEPlayerController::ApplyInWorldInputMode()
{
	if (IsVRActive()) { SetInputMode(FInputModeGameOnly()); bShowMouseCursor = false; return; }
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void AACEPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// Development packages inherit Engine debug bindings too. Reserve the game
	// keys even when an older Saved/Input.ini still contains those defaults.
	if (PlayerInput) PlayerInput->DebugExecBindings.Reset();
	GetMutableDefault<UInputSettings>()->ConsoleKeys.Reset();
	// WASD is polled in PlayerTick so no DefaultInput.ini mappings are required.
}

bool AACEPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
 const auto Mods=FSlateApplication::Get().GetModifierKeys();
 const FInputChord Chord(Params.Key,Mods.IsShiftDown(),Mods.IsControlDown(),Mods.IsAltDown(),Mods.IsCommandDown());
 if(Params.Event==IE_Pressed && ACEInputBindings::Matches(ACEInputBindings::Action(TEXT("Chat")),Chord)
  && Client && Client->GetSessionState()==EACESessionState::InWorld)
 {
  if(DatGameplayBinder && DatCanvasWidget && DatCanvasWidget->IsVisible() && !DatGameplayBinder->IsChatEntryFocused())
  {
   DatGameplayBinder->FocusChatEntry();return true;
  }
  if(GameHUDWidget && !GameHUDWidget->IsChatEntryFocused())
  {
   GameHUDWidget->FocusChatEntry();return true;
  }
 }
 return Super::InputKey(Params);
}


void AACEPlayerController::UpdateFrameRateOverlay()
{
	const bool Enabled=ACERuntimeOptions::Get(TEXT("ShowFrameRate"))>.5f;
	if(!Enabled)
	{
		if(FrameRateWidget && FrameRateWidget->GetVisibility()!=ESlateVisibility::Collapsed)
		{
			FrameRateWidget->SetVisibility(ESlateVisibility::Collapsed);
			FrameRateWidget->RemoveFromParent();FrameRateWidget->ResetSample();
		}
		return;
	}
	if(!FrameRateWidget)FrameRateWidget=CreateWidget<UACEFrameRateWidget>(this);
	if(!FrameRateWidget)return;
	FrameRateWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	FrameRateWidget->Sample(FPlatformTime::Seconds());
	if(IsVRActive()) { if(FrameRateWidget->IsInViewport())FrameRateWidget->RemoveFromParent(); }
	else if(!FrameRateWidget->IsInViewport())
	{
		FrameRateWidget->AddToViewport(10000);FrameRateWidget->SetAlignmentInViewport(FVector2D(0,0));
		FrameRateWidget->SetPositionInViewport(FVector2D(16,64),false);
		FrameRateWidget->SetDesiredSizeInViewport(FVector2D(320,48));
	}
}

void AACEPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	auto* VR = GetPawn() ? GetPawn()->FindComponentByClass<UACEVRComponent>() : nullptr;
	const bool bVR = VR && VR->IsActive();
	if (IsLocalController())
	{
		int32 Width=0,Height=0; GetViewportSize(Width,Height);
		ACERuntimeOptions::ApplyDesktopUIScale(FIntPoint(Width,Height),bVR);
		UpdateFrameRateOverlay();
	}
	if (bVR) VR->PrepareMovement(DeltaTime);
	if (bVR) PortalWorldRevealElapsed = -1.f;
	if (PortalWorldRevealElapsed >= 0.f && PlayerCameraManager)
	{
		PortalWorldRevealElapsed += FMath::Max(0.f, DeltaTime);
		int32 W = 1280, H = 720; GetViewportSize(W, H);
		const float Aspect = static_cast<float>(W) / FMath::Max(1, H);
		PlayerCameraManager->SetFOV(ACERetailPortalAnimation::StretchFov(
			ACECameraRetail::HorizontalFovDegrees(Aspect), Aspect, PortalWorldRevealElapsed, true));
		if (PortalWorldRevealElapsed < 1.f) return;
		PortalWorldRevealElapsed = -1.f;
		PlayerCameraManager->UnlockFOV();
	}

	// Per-tick diagnostic message — gated behind bShowACEDebug (default false) since the UMG
	// login widget is now the primary UI and this was previously spamming the screen every frame.
	if (bShowACEDebug && GEngine && IsLocalController())
	{
		FVector2D DebugViewportSize = FVector2D::ZeroVector;
		if (const ULocalPlayer* LP = GetLocalPlayer())
		{
			if (LP->ViewportClient && LP->ViewportClient->Viewport)
			{
				DebugViewportSize = FVector2D(LP->ViewportClient->Viewport->GetSizeXY());
			}
		}

		GEngine->AddOnScreenDebugMessage(6000, 0.f, FColor::Yellow,
			FString::Printf(TEXT("ACE: PC alive — Session=%s LoginWidget=%s InViewport=%s LoginUIVisible=%s DesiredSize=%s CachedGeomSize=%s ViewportSize=%s"),
				Client ? *UEnum::GetValueAsString(Client->GetSessionState()) : TEXT("NoClient"),
				LoginWidget ? TEXT("valid") : TEXT("NULL"),
				LoginWidget ? (LoginWidget->IsInViewport() ? TEXT("YES") : TEXT("no")) : TEXT("n/a"),
				IsLoginUIVisible() ? TEXT("YES") : TEXT("no"),
				LoginWidget ? *LoginWidget->GetDesiredSize().ToString() : TEXT("n/a"),
				LoginWidget ? *LoginWidget->GetCachedGeometry().GetLocalSize().ToString() : TEXT("n/a"),
				*DebugViewportSize.ToString()));
	}

	if (bPendingEnterWorldTransition)
	{
		bPendingEnterWorldTransition = false;
		if (Client && Client->GetSessionState() == EACESessionState::EnteringWorld)
		{
			DestroyCharacterSelectUI();
			BeginEnterWorldLoadScreen();
		}
	}
	if ((bEnterWorldLoading || bWorldRevealActive) && Client
		&& (Client->GetSessionState() == EACESessionState::EnteringWorld
			|| Client->GetSessionState() == EACESessionState::InWorld))
	{
		TickWorldTransition();
		return;
	}
	if (!Client || Client->GetSessionState() != EACESessionState::InWorld)
	{
		if (GameHUDWidget)
		{
			DestroyGameHUD();
		}
		TickPendingCharacterSelect();
		if (Client && Client->GetSessionState() == EACESessionState::CharacterSelect && bUseEnterWorldLoadScreen)
			PreparePortalScreen();
		return;
	}

	if (!bVR) { EnsureRetailMouseCursor(); UpdateOrbitCamera(DeltaTime); }

	if (UWorld* World = GetWorld())
	{
		if (World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0 && Client)
		{
			static constexpr int32 WcGameplayRingTiles = 4;
			static constexpr float WcStreamPollInterval = 0.15f;
			WcTerrainStreamAccum += DeltaTime;
			const FACEPosition Pose = Client->GetPlayerPosition();
			if (Pose.IsValid())
			{
				const FVector Loc = Pose.ToUnrealLocation(WorldScale);
				const float TileCoordX = Loc.X / 612000.f;
				const float TileCoordY = Loc.Y / 612000.f;
				const int32 TileX = (TileCoordX < 0.f)
					? FMath::CeilToInt(TileCoordX - 0.5f)
					: FMath::FloorToInt(TileCoordX + 0.5f);
				const int32 TileY = (TileCoordY < 0.f)
					? FMath::CeilToInt(TileCoordY - 0.5f)
					: FMath::FloorToInt(TileCoordY + 0.5f);
				const bool bTileChanged = TileX != WcLastStreamTileX || TileY != WcLastStreamTileY;
				const bool bFirstStream = WcLastStreamTileX == INT32_MIN;
				if (bTileChanged || bFirstStream)
				{
					if (bTileChanged)
					{
						WcLastStreamTileX = TileX;
						WcLastStreamTileY = TileY;
					}
					else
					{
						WcLastStreamTileX = TileX;
						WcLastStreamTileY = TileY;
					}
					ACEWcTerrainRuntime::ShowWcLandscapesAfterLogin(World);
					ACEWcTerrainRuntime::RequestTerrainAroundBlocking(World, Loc, WcGameplayRingTiles);
				}
				else if (WcTerrainStreamAccum >= WcStreamPollInterval)
				{
					WcTerrainStreamAccum = 0.f;
					ACEWcTerrainRuntime::RequestTerrainAround(World, Loc, WcGameplayRingTiles);
				}
			}
		}
	}

	if (!bVR) { PollObjectHover(); if (!bMouseLookActive) PollObjectClick(); }

	// Direct key poll so WASDQE works without DefaultInput.ini mappings. Matches the retail AC
	// client's scheme: W/S = forward/back, A/D = turn (yaw) left/right, Q/E = sidestep.
	// Arrow keys are the same as WASD when chat is not focused. Numpad orbits the camera.
	// Run is the default hold-key; Shift = walk (retail AC).
	const bool bChatFocused = bVR || (DatGameplayBinder && DatGameplayBinder->IsChatEntryFocused())
		|| (GameHUDWidget && GameHUDWidget->IsChatEntryFocused()) || ACEInputBindings::IsEditing();
	float F = 0.f, R = 0.f, T = 0.f;
	if (ACEInputBindings::Down(this, EKeys::W)) F += 1.f;
	if (ACEInputBindings::Down(this, EKeys::S)) F -= 1.f;
	if (ACEInputBindings::Down(this, EKeys::E)) R += 1.f;
	if (ACEInputBindings::Down(this, EKeys::Q)) R -= 1.f;
	if (bMouseForwardActive && !bChatFocused) F = 1.f;
	// WoW: A/D turn normally, and sidestep while holding the facing button.
	const bool bMouseFacing = bMouseLookActive && (bInstantMouseLookHeld || (IsUseMouseTurning() && IsInputKeyDown(EKeys::RightMouseButton)));
	if (ACEInputBindings::Down(this, EKeys::D)) { if (bMouseFacing) R += 1.f; else T += 1.f; }
	if (ACEInputBindings::Down(this, EKeys::A)) { if (bMouseFacing) R -= 1.f; else T -= 1.f; }
	F = FMath::Clamp(F, -1.f, 1.f);
	R = FMath::Clamp(R, -1.f, 1.f);
	T = FMath::Clamp(T, -1.f, 1.f);
	bRunning = !ACEInputBindings::Down(this, EKeys::LeftShift);

	// Poll the bound autorun action; directional movement cancels it.
	{
		const bool bNumLockDown = ACEInputBindings::Down(this, EKeys::NumLock);
		if (bNumLockDown && !bNumLockWasDown)
		{
			bAutoRun = !bAutoRun;
		}
		bNumLockWasDown = bNumLockDown;
		if (!FMath::IsNearlyZero(F))
		{
			bAutoRun = false;
		}
		else if (bAutoRun)
		{
			F = 1.f;
		}
	}

	// Retail number row: magic = spell bank slots; peace = inventory shortcuts.
	// DAT HUD owns this when GameHUDWidget is inactive.

	// Retail Space: hold to charge jump bar, release to Jump (0xF61B).
	const bool bSpaceDown = bVR ? VR->IsJumpHeld() : ACEInputBindings::Down(this, EKeys::SpaceBar) && !bChatFocused;
	if (!bVR && bSpaceDown && !bSpaceWasDown && !bJumpAirborne)
	{
		BeginJumpCharge();
	}
	if (!bVR && bJumpCharging)
	{
		// Retail fills the bar in ~1 second.
		JumpChargeExtent = FMath::Clamp(JumpChargeExtent + DeltaTime / 1.0f, 0.f, 1.f);
		if (DatGameplayBinder)
		{
			DatGameplayBinder->SetJumpChargeFraction(JumpChargeExtent);
		}
	}
	if (!bVR && !bSpaceDown && bSpaceWasDown && bJumpCharging)
	{
		ReleaseJump(F, R);
	}
	bSpaceWasDown = bSpaceDown;
	if (bJumpAirborne)
	{
		JumpAirborneSeconds += DeltaTime;
	}

	if (!bChatFocused && Client && Client->GetSessionState() == EACESessionState::InWorld)
	{
        if(DatGameplayBinder) DatGameplayBinder->PollKeyboardActions(this);
        else if(GameHUDWidget)for(int32 Slot=0;Slot<9;++Slot)
            if(ACEInputBindings::Pressed(this,ACEInputBindings::Shortcut(Slot)))GameHUDWidget->ActivateHotbarSlot(Slot);
	}

	// Retail: F uses / picks up the selected object (same as the hand toolbar button).
	if (!bChatFocused && ACEInputBindings::Pressed(this, ACEInputBindings::Action(TEXT("Pickup"))) && Client
		&& Client->GetSessionState() == EACESessionState::InWorld)
	{
		InteractWithSelectedObject();
	}

	if (!bChatFocused && DatGameplayBinder
		&& Client && Client->GetSessionState() == EACESessionState::InWorld)
	{
		// ToD Keyboard mapping defaults (Emotes + Movement + UI tabs).
		if (ACEInputBindings::Pressed(this, EKeys::G))
		{
			DatGameplayBinder->PlayEmoteHotkey(ACEMotion::Sitting, true);
		}
		else if (ACEInputBindings::Pressed(this, EKeys::B))
		{
			DatGameplayBinder->PlayEmoteHotkey(ACEMotion::Sleeping, true);
		}
		else if (ACEInputBindings::Pressed(this, EKeys::C))
		{
			DatGameplayBinder->PlayEmoteHotkey(ACEMotion::Crouch, true);
		}
		else if (ACEInputBindings::Pressed(this, EKeys::K))
		{
			DatGameplayBinder->PlayEmoteHotkey(0x430000f0u, true); // PointState (retail held pose)
		}
		else if (ACEInputBindings::Pressed(this, EKeys::J))
		{
			DatGameplayBinder->PlayEmoteHotkey(0x13000087u, false); // Wave
		}
		else if (ACEInputBindings::Pressed(this, EKeys::I))
		{
			DatGameplayBinder->ToggleGameplayPanel(TEXT("InventoryPanel_Field"));
		}
		else if (ACEInputBindings::Pressed(this, EKeys::P))
		{
			DatGameplayBinder->ToggleGameplayPanel(TEXT("SkillManagementPanel_Field"));
		}
		else if (ACEInputBindings::Pressed(this, EKeys::M))
		{
			DatGameplayBinder->ToggleGameplayPanel(TEXT("SpellManagementPanel_Field"));
		}
		else if (ACEInputBindings::Pressed(this, EKeys::U))
		{
			DatGameplayBinder->ToggleGameplayPanel(TEXT("QuestManagementPanel_Field"));
		}
		else if (ACEInputBindings::Pressed(this, EKeys::O))
		{
			DatGameplayBinder->ToggleGameplayPanel(TEXT("OptionsPanel_Field"));
		}
		else if (ACEInputBindings::Pressed(this, EKeys::Tilde))
		{
			DatGameplayBinder->ToggleCombatModeHotkey();
		}
		else if (ACEInputBindings::Pressed(this, EKeys::Home))
		{
			if (TSharedPtr<FACESession> Session = Client->GetSession())
			{
				const int32 Attacker = Session->GetLastAttackerGuid();
				if (Attacker != 0)
				{
					Client->SelectObject(Attacker);
				}
			}
		}
		else if (ACEInputBindings::Pressed(this, EKeys::Escape))
		{
			DatGameplayBinder->HandleEscape();
			if (APawn* P = GetPawn())
			{
				if (UACECharacterAppearanceComponent* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
				{
					App->CancelHeldActionMotion();
				}
			}
			Client->SelectObject(0);
		}
		else if (ACEInputBindings::Pressed(this, ACEInputBindings::Action(TEXT("ClosestMonster")))) CycleNearbyTarget(true, 0);
		else if (ACEInputBindings::Pressed(this, ACEInputBindings::Action(TEXT("ClosestItem")))) CycleNearbyTarget(false, 0);
		else if (ACEInputBindings::Pressed(this, EKeys::LeftBracket)) CycleNearbyTarget(false, -1);
		else if (ACEInputBindings::Pressed(this, EKeys::RightBracket)) CycleNearbyTarget(false, 1);
		else if (ACEInputBindings::Pressed(this, EKeys::Semicolon)) CycleNearbyTarget(true, -1);
		else if (ACEInputBindings::Pressed(this, EKeys::Apostrophe)) CycleNearbyTarget(true, 1);
	}

	if (!bChatFocused && ACEInputBindings::Down(this, ACEInputBindings::Action(TEXT("Stop")))) { F=R=T=0.f; bAutoRun=false; }
	if (bVR) { VR->GetMovement(F, R, bRunning); T = 0.f; bAutoRun = false; if (bServerMoveToActive) ClearServerMoveTo(); }
	const FVector VRRoomDelta = bVR ? VR->GetRoomScaleDelta() : FVector::ZeroVector;
	// Manual input cancels server-directed MoveTo approach.
	const bool bManualKeys = !FMath::IsNearlyZero(F) || !FMath::IsNearlyZero(R) || !FMath::IsNearlyZero(T);
	if (bManualKeys && (bServerMoveToActive || bAwaitingUseDone || ApproachUseSendCount > 0))
	{
		ClearServerMoveTo();
	}

	// Follow Use approach: face the target first (direct heading), then walk straight in.
	if (bServerMoveToActive && !bManualKeys && ServerMoveToTargetGuid != 0)
	{
		FACEWorldObject Target;
		if (Client->GetWorldObject(ServerMoveToTargetGuid, Target) && Target.bHasPosition)
		{
			FACEPosition PoseForFacing = bHavePredictedPose ? PredictedPose : Client->GetPlayerPosition();
			// ACE Position origins are cylinder bottoms. The pawn actor is at the Unreal
			// capsule center, so use the predicted protocol pose for server-exact range.
			const FVector SelfLoc = PoseForFacing.ToUnrealLocation(WorldScale);
			const FVector TargetLoc = Target.Position.ToUnrealLocation(WorldScale);
			FVector To = TargetLoc - SelfLoc;
			To.Z = 0.f;
			const float DistCm = To.Size();
			const float CylinderDistCm = GetUseCylinderDistanceCm(Target, SelfLoc, TargetLoc);
			// Preserve authored UseRadius (portals often use negative = must overlap cylinders).
			// Never coerce negative/zero → 0.6 — that made Use fire early and fail server-side.
			const float UseRadiusAc = bServerMoveToActive ? ServerMoveToDistance : Target.UseRadius;
			const float UseRadiusCm = UseRadiusAc * WorldScale;
			// Match server IsWithinUseRadiusOf (cylinder gap ≤ UseRadius). No DistCm shortcut.
			const float UseSlopCm = FMath::Max(2.f, 0.02f * WorldScale);

			// MoveToManager::HandleMoveToPosition checks full 3D displacement, after arrival.
			// Preserve the server's limit (15 AC for charge); zero denotes its unlimited default.
			if (CylinderDistCm > UseRadiusCm + UseSlopCm
				&& bHaveApproachStartPose && ServerMoveToFailDistance > 0.f)
			{
				const FVector StartLoc = ApproachStartPose.ToUnrealLocation(WorldScale);
				if (FVector::Distance(SelfLoc, StartLoc) / WorldScale > ServerMoveToFailDistance)
				{
					HandleMoveToFailed(0x003D);
					if (TSharedPtr<FACESession> Sess = Client->GetSession())
					{
						Sess->ReportMoveToFailure(0x003D);
					}
				}
			}

			if (bServerMoveToActive)
			{
			// Keep updating the aim point only while stationary. Once Walking begins this
			// direction is locked, guaranteeing a straight line with no steering while running.
			if ((UseApproachPhase == EACEUseApproachPhase::None
					|| UseApproachPhase == EACEUseApproachPhase::Turning)
				&& DistCm > 1.f)
			{
				ApproachTargetDir = To.GetSafeNormal();
			}

			const float Dot = ApproachTargetDir.IsNearlyZero()
				? 1.f
				: PoseForFacing.GetFacingDotUnreal2D(ApproachTargetDir);
			constexpr float FaceDotMin = 0.997f; // ~4.4°; avoid stuck turning at doors
			const bool bFacing = Dot >= FaceDotMin;

			if (UseApproachPhase == EACEUseApproachPhase::None)
			{
				UseApproachPhase = EACEUseApproachPhase::Turning;
			}
			if (UseApproachPhase == EACEUseApproachPhase::Turning)
			{
				ApproachTurnSeconds += DeltaTime;
			}
			if (UseApproachPhase == EACEUseApproachPhase::Turning
				&& (bFacing || ApproachTurnSeconds >= 1.25f))
			{
				// Finish at the exact door heading, then lock that heading for the run.
				PredictedPose = PoseForFacing;
				PredictedPose.SetAceFacingFromUnrealDir2D(ApproachTargetDir);
				ApproachTargetDir = PredictedPose.GetAceForwardVector().GetSafeNormal2D();
				if (APawn* P = GetPawn())
				{
					P->SetActorRotation(PredictedPose.ToUnrealQuat());
				}
				Client->SetReportedPosition(PredictedPose);
				UseApproachPhase = EACEUseApproachPhase::Walking;
				ApproachStallSeconds = 0.f;
				ApproachStallLastLoc = SelfLoc;
				ApproachTurnSeconds = 0.f;
			}
			bool bInUseRange = CylinderDistCm <= UseRadiusCm + UseSlopCm;
			// Physics stall against the target (capsule blocked) — fire Use even if our
			// cylinder estimate is slightly pessimistic vs the server.
			if (UseApproachPhase == EACEUseApproachPhase::Walking && !bInUseRange)
			{
				const float MovedCm = (SelfLoc - ApproachStallLastLoc).Size2D();
				ApproachStallLastLoc = SelfLoc;
				if (MovedCm < FMath::Max(5.f, 0.05f * WorldScale))
				{
					ApproachStallSeconds += DeltaTime;
				}
				else
				{
					ApproachStallSeconds = 0.f;
				}
				// Stalled while still closing: accept as arrived if already near UseRadius
				// (player walked as close as collision allows). Negative UseRadius (overlap)
				// still requires near-overlap — do not widen with the +3 AC door slop.
				const float StallLimitCm = (UseRadiusAc < 0.f)
					? (UseRadiusCm + 0.5f * WorldScale)
					: (UseRadiusCm + 3.f * WorldScale);
				if (ApproachStallSeconds >= 0.4f && CylinderDistCm <= StallLimitCm)
				{
					bInUseRange = true;
				}
				// Blocked far from the target (solid furniture/walls) — retail MoveToManager
				// cancels on failed progress. Without this the approach latched
				// bServerMoveToActive forever and later gives failed with "You're too busy!".
				else if (ApproachStallSeconds >= 2.5f)
				{
					UE_LOG(LogTemp, Log, TEXT("ACE: Use approach stalled out of range guid=0x%08X cyl=%.1f — cancel"),
						ServerMoveToTargetGuid, CylinderDistCm);
					ClearServerMoveTo();
					// Phase is now None — the rest of this block is inert this tick.
				}
			}
			auto SendApproachUse = [&](bool bForce = false)
			{
				// Give / server MoveOnly: never Use — CreateMoveToChain completes from AutoPos.
				// Sending Use runs StopExistingMoveToChains and cancels the pending give.
				if (bApproachMoveOnly || (!bResendUseWhenInRange && !bApproachPickupIntoInventory))
				{
					return;
				}
				if (!bResendUseWhenInRange && !bForce)
				{
					return;
				}
				FACEWorldObject ApproachTarget;
				const bool bHaveApproachTarget =
					Client->GetWorldObject(ServerMoveToTargetGuid, ApproachTarget);
				if (bHaveApproachTarget
					&& (Client->GetOpenVendorGuid() == ServerMoveToTargetGuid
						|| Client->GetOpenExternalContainerGuid() == ServerMoveToTargetGuid))
				{
					ClearServerMoveTo();
					return;
				}
				// Vendors / corpses / chests / talk NPCs: exactly one Use. A retry toggles
				// open containers closed (ActOnUse) so the loot window still shows items the
				// server no longer has open — PutItemInContainer then fails. Only doors keep
				// retrying until they actually open.
				const bool bRetryUse = bHaveApproachTarget && ApproachTarget.IsDoor();
				if (!bApproachPickupIntoInventory && !bRetryUse)
				{
					if (ApproachUseSendCount > 0)
					{
						if (ApproachTarget.IsVendor())
						{
							VendorOpenWaitSeconds += DeltaTime;
							if (VendorOpenWaitSeconds >= 3.f)
							{
								ClearServerMoveTo();
							}
						}
						// Do not ClearServerMoveTo / StopMovement here — GameActionMoveToState
						// cancels the server's CreateMoveToChain. Wait for UseDone.
						return;
					}
				}
				const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
				// Keep retrying Use while Holding — first send often races server CreateMoveToChain
				// (client StopMovement used to cancel it). Clear only when door opens / ClearServerMoveTo.
				if (!bForce && LastApproachUseTime > 0.0 && (Now - LastApproachUseTime) < 0.35)
				{
					return;
				}
				LastApproachUseTime = Now;
				if (!bApproachMoveOnly)
				{
					bResendUseWhenInRange = true;
				}
				if (bHavePredictedPose)
				{
					Client->SetReportedPosition(PredictedPose);
				}
				Client->FlushAutonomousPosition(true);
				Client->SetForcePositionReporting(true);
				if (bApproachPickupIntoInventory)
				{
					Client->SendPutItemInContainer(ServerMoveToTargetGuid, Client->GetPlayerGuid(), 0);
					if (APawn* Possessed = GetPawn())
					{
						if (UACECharacterAppearanceComponent* App =
							Possessed->FindComponentByClass<UACECharacterAppearanceComponent>())
						{
							// MotionCommand.Pickup — play immediately; server broadcast can lag.
							App->PlayActionMotion(static_cast<int32>(0x40000018));
						}
					}
					UE_LOG(LogTemp, Log, TEXT("ACE: PutInContainer at range guid=0x%08X cyl=%.1f use=%.1f"),
						ServerMoveToTargetGuid, CylinderDistCm, UseRadiusCm);
					// One shot only — StartPickup sets IsBusy until the anim finishes;
					// Holding retries spam WeenieError.YoureTooBusy.
					ClearServerMoveTo();
					return;
				}
				else
				{
					Client->SendUseItem(ServerMoveToTargetGuid);
					++ApproachUseSendCount;
					bAwaitingUseDone = true;
					UE_LOG(LogTemp, Log, TEXT("ACE: Use at range guid=0x%08X cyl=%.1f use=%.1f"),
						ServerMoveToTargetGuid, CylinderDistCm, UseRadiusCm);
				}
			};
			if (UseApproachPhase == EACEUseApproachPhase::Walking && bInUseRange)
			{
				// Snap facing and Use once in range — don't require FaceDot again (stuck doors).
				if (!bFacing)
				{
					PredictedPose = PoseForFacing;
					PredictedPose.SetAceFacingFromUnrealDir2D(ApproachTargetDir);
					ApproachTargetDir = PredictedPose.GetAceForwardVector().GetSafeNormal2D();
					if (APawn* P = GetPawn())
					{
						P->SetActorRotation(PredictedPose.ToUnrealQuat());
					}
					Client->SetReportedPosition(PredictedPose);
				}
				UseApproachPhase = EACEUseApproachPhase::Holding;
				if (!bApproachMoveOnly && Client)
				{
					// Stop the client run BEFORE Use. Any MoveToState after Use (including
					// axes-0) cancels server CreateMoveToChain.
					Client->SendMovementEx(0.f, 0.f, 0.f, bRunning, false, true);
					ForwardSent = 0.f;
					RightSent = 0.f;
					TurnSent = 0.f;
					bWasMoving = false;
				}
				if (!bResendUseWhenInRange && !bApproachPickupIntoInventory)
				{
					// Give hand-off: flush final pose so server CreateMoveToChain succeeds; do not Use.
					ApproachStallSeconds = 0.f;
					if (bHavePredictedPose)
					{
						Client->SetReportedPosition(PredictedPose);
					}
					Client->FlushAutonomousPosition(true);
					Client->SetForcePositionReporting(true);
					UE_LOG(LogTemp, Log, TEXT("ACE: Give/MoveOnly at range guid=0x%08X cyl=%.1f use=%.1f"),
						ServerMoveToTargetGuid, CylinderDistCm, UseRadiusCm);
				}
				else
				{
					SendApproachUse(true);
				}
			}
			// Holding in UseRadius: keep retrying Use until Ethereal / ClearServerMoveTo.
			// Vendors: stop once ApproachVendor opened the panel (avoids Open-emote spam).
			// Give/MoveOnly: keep AutoPos briefly so CreateMoveToChain can finish, then release.
			if (UseApproachPhase == EACEUseApproachPhase::Holding && bInUseRange)
			{
				if (!bResendUseWhenInRange && !bApproachPickupIntoInventory)
				{
					const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
					if (LastApproachUseTime <= 0.0 || (Now - LastApproachUseTime) >= 0.35)
					{
						LastApproachUseTime = Now;
						if (bHavePredictedPose)
						{
							Client->SetReportedPosition(PredictedPose);
						}
						Client->FlushAutonomousPosition(true);
						Client->SetForcePositionReporting(true);
					}
					ApproachStallSeconds += DeltaTime;
					if (ApproachStallSeconds >= 1.25f && !(Client && Client->IsUseBusy()))
					{
						ClearServerMoveTo();
					}
				}
				else
				{
					FACEWorldObject HoldTarget;
					if (Client->GetWorldObject(ServerMoveToTargetGuid, HoldTarget)
						&& ((HoldTarget.IsVendor()
								&& Client->GetOpenVendorGuid() == ServerMoveToTargetGuid)
							|| Client->GetOpenExternalContainerGuid() == ServerMoveToTargetGuid))
					{
						ClearServerMoveTo();
					}
					else
					{
						SendApproachUse(false);
					}
				}
			}
			if (UseApproachPhase == EACEUseApproachPhase::Holding
				&& !bInUseRange
				&& ApproachStallSeconds < 0.2f)
			{
				// Still not against the mesh and cylinder gap is outside UseRadius — keep closing.
				// Give / MoveOnly approaches must stay MoveOnly: sending Use runs
				// StopExistingMoveToChains on the server and cancels the pending give.
				UseApproachPhase = EACEUseApproachPhase::Walking;
				bResendUseWhenInRange = !bApproachMoveOnly;
			}
			if (UseApproachPhase == EACEUseApproachPhase::Holding && !bInUseRange && !bFacing)
			{
				UseApproachPhase = EACEUseApproachPhase::Turning;
			}

			F = 0.f;
			R = 0.f;
			T = 0.f;
			if (UseApproachPhase == EACEUseApproachPhase::Walking
				|| (UseApproachPhase == EACEUseApproachPhase::Holding && !bInUseRange && bResendUseWhenInRange))
			{
				// Already facing — run straight forward only (no strafe).
				// Keep F=1 until Use fires at true arrive; Holding may still close if Use pending.
				F = 1.f;
			}
			// Turning/Holding: axes stay 0; heading is applied directly on PredictedPose below.
			} // bServerMoveToActive (after fail-distance check)
		}
	}

	// Retail: no air control of the arc — lock JumpWorldAceVelocity. A/D may still yaw the
	// mesh while airborne (visual only); charge keeps turn for aim.
	const float RawF = F;
	const float RawR = R;
	if (bJumpCharging && bStandingJumpLocked)
	{
		// Snapshot aim for release (W/S/Q/E) without allowing loco prediction mid-charge.
		StandingJumpAimF = RawF;
		StandingJumpAimR = RawR;
	}
	if (bStandingJumpLocked && (bJumpCharging || bJumpAirborne))
	{
		F = 0.f;
		R = 0.f;
	}
	if (bJumpAirborne)
	{
		F = 0.f;
		R = 0.f;
		// Keep T — rotate the model; ballistic velocity stays JumpWorldAceVelocity.
	}

    PendingMouseTurnDegrees = 0.f;
    // Explicit keyboard turning takes precedence over camera-follow steering.
	// Automatic approach supplies F too; its heading belongs to the target, not the camera.
	if (!bServerMoveToActive && bMouseFacing && FMath::IsNearlyZero(T) && GetPawn())
    {
        if (auto* Boom = GetPawn()->FindComponentByClass<USpringArmComponent>())
            PendingMouseTurnDegrees = ACECameraRetail::FollowTurnDegrees(Boom->GetRelativeRotation().Yaw,
                DeltaTime, true, true);
    }

	ForwardAxis = F;
	RightAxis = R;
	TurnAxis = T;

	const bool bMouseTurnSend = (IsUseMouseTurning() || bInstantMouseLookHeld)
		&& FMath::Abs(PendingMouseTurnDegrees) > 0.25f;
	const bool bMoving = !FMath::IsNearlyZero(F) || !FMath::IsNearlyZero(R) || !FMath::IsNearlyZero(T) || !VRRoomDelta.IsNearlyZero();
	// Keep local prediction while Use approach is active, and for the full jump arc
	// (otherwise SoftReconcile snaps idle airborne poses back to the ground).
	// The network send threshold must not discard small local mouse turns.
	bLocalPredicting = bMoving || !FMath::IsNearlyZero(PendingMouseTurnDegrees)
		|| bServerMoveToActive || bJumpAirborne || bJumpCharging || (bVR && !VR->IsInputBlocked());
	const bool bChanged =
		bForceMovementResend ||
		!FMath::IsNearlyEqual(F, ForwardSent) ||
		!FMath::IsNearlyEqual(R, RightSent) ||
		!FMath::IsNearlyEqual(T, TurnSent) ||
		(bMoving != bWasMoving) ||
		(bRunning != bRunningSent) ||
		(bJumpCharging != bJumpChargeSent) ||
		(bJumpAirborne != bJumpAirborneSent) ||
		bMouseTurnSend;

	// After Use is in flight, MoveToState (F=1 leftover or axes-0) cancels CreateMoveToChain.
	const bool bFreezeMoveToState = !bManualKeys && !bJumpAirborne && !bJumpCharging
		&& (bAwaitingUseDone || ApproachUseSendCount > 0 || Client->IsUseBusy());

	if (bFreezeMoveToState)
	{
		if (bHavePredictedPose)
		{
			Client->SetReportedPosition(PredictedPose);
		}
		Client->FlushAutonomousPosition(true);
		Client->SetForcePositionReporting(true);
		if (bChanged)
		{
			bForceMovementResend = false;
			ForwardSent = F;
			RightSent = R;
			TurnSent = T;
			bRunningSent = bRunning;
			bWasMoving = bMoving;
			bJumpChargeSent = bJumpCharging;
			bJumpAirborneSent = bJumpAirborne;
		}
	}
	else if (bChanged)
	{
		if (bHavePredictedPose)
		{
			Client->SetReportedPosition(PredictedPose);
		}
		bForceMovementResend = false;
		const bool bStandingLongJump = bJumpCharging && bStandingJumpLocked
			&& JumpChargeExtent > 0.05f;
		if (bJumpAirborne)
		{
			// Contact=0 so the server keeps physics jump ownership; keep AutoPos flowing.
			Client->SendMovementEx(0.f, 0.f, 0.f, bRunning, false, false);
			Client->SetForcePositionReporting(true);
		}
		else if (bMoving || bJumpCharging || bMouseTurnSend)
		{
			// Standing charge: axes 0 + StandingLongJump bit. Run charge: keep loco Contact=1.
			const float SendF = bStandingJumpLocked ? 0.f : RawF;
			const float SendR = bStandingJumpLocked ? 0.f : RawR;
			float SendT = T;
			if (bMouseTurnSend)
			{
				SendT = FMath::Sign(PendingMouseTurnDegrees);
			}
			Client->SendMovementEx(SendF, SendR, SendT, bRunning, bStandingLongJump, true);
		}
		else if (bServerMoveToActive || (Client && Client->IsUseBusy()))
		{
			// Holding at a door/portal: zero axes with Contact=1 and keep AutoPos flowing.
			// StopMovement() cancels the server CreateMoveToChain that ActOnUse still needs.
			Client->SendMovementEx(0.f, 0.f, 0.f, bRunning, false, true);
			Client->SetForcePositionReporting(true);
		}
		else
		{
			if (bHavePredictedPose)
			{
				Client->SetReportedPosition(PredictedPose);
			}
			Client->FlushAutonomousPosition(true);
			Client->StopMovement();
			Client->SetForcePositionReporting(false);
		}
		ForwardSent = F;
		RightSent = R;
		TurnSent = T;
		bRunningSent = bRunning;
		bWasMoving = bMoving;
		bJumpChargeSent = bJumpCharging;
		bJumpAirborneSent = bJumpAirborne;
	}

	// Drive walk/run anim on the local player's appearance from the same axes we send to the server.
	if (APawn* PossessedPawn = GetPawn())
	{
		if (UACECharacterAppearanceComponent* App = PossessedPawn->FindComponentByClass<UACECharacterAppearanceComponent>())
		{
			App->UpdateCellLighting((bHavePredictedPose ? PredictedPose : Client->GetPlayerPosition()).CellId);
			if (bJumpAirborne)
			{
				App->SetSuppressLocoIdleBlend(true);
				// Standing jump: no loco axes. Run jump: keep Raw axes for land resume.
				if (!bStandingJumpLocked && (!FMath::IsNearlyZero(RawF) || !FMath::IsNearlyZero(RawR)))
				{
					App->SetLocomotionInput(RawF, RawR, bRunning,
						(bRunning && RawF > 0.f) ? Client->GetRunRate() : 1.f);
				}
				else
				{
					App->SetLocomotionInput(0.f, 0.f, false, 1.f);
				}
			}
			else if (bJumpCharging)
			{
				// Keep run/walk through the entire Space hold; JumpCharging only when standing.
				if (!bStandingJumpLocked && (!FMath::IsNearlyZero(RawF) || !FMath::IsNearlyZero(RawR)))
				{
					// Reversing axes passes through zero and temporarily installs a
					// held charge pose. Release it before resuming locomotion.
					App->ClearJumpMotionIfAny();
					App->SetSuppressLocoIdleBlend(false);
					App->SetLocomotionInput(RawF, RawR, bRunning,
						(bRunning && RawF > 0.f) ? Client->GetRunRate() : 1.f);
				}
				else
				{
					App->SetLocomotionInput(0.f, 0.f, false, 1.f);
					App->SetHeldActionMotion(static_cast<int32>(0x4000001d), 0);
				}
			}
			else
			{
				App->ClearJumpMotionIfAny();
				App->SetSuppressLocoIdleBlend(false);
				App->SetLocomotionInput(F, R, bRunning,
					(bRunning && F > 0.f) ? Client->GetRunRate() : 1.f);
			}
		}
	}

	// Resolve support, cell transitions and body sweeps together at a bounded
	// physics interval. Subdividing only the lateral sweep still sampled stairs
	// at the far end of a 50-70ms headset frame, lifting the body onto furniture
	// or dropping its floor before it actually reached the doorway.
	const float MovementFrameTime = DeltaTime;
	const int32 MovementSteps = bVR && (bMoving || bJumpAirborne)
		? FMath::Clamp(FMath::CeilToInt(MovementFrameTime * 90.f), 1, 24) : 1;
	DeltaTime = MovementFrameTime / MovementSteps;
	// Residency can change between substeps, but the world cannot stream new
	// components in the middle of this synchronous movement loop. Reuse the
	// collision filter when the admitted cells remain the same.
	TOptional<FCollisionQueryParams> MovementFilterCache;
	TSet<uint32> MovementFilterCells;
	bool bMovementFilterOutdoor = false;
	for (int32 MovementStep = 0; MovementStep < MovementSteps; ++MovementStep)
	{
	if (bSyncPossessedPawn)
	{
		if (APawn* P = GetPawn())
		{
			// Own a stable predicted pose while moving (and briefly after). Re-seeding every
			// tick from GetPlayerPosition() (server F748) caused stop/stutter rubber-banding.
			FACEPosition Pred;
			if (bHavePredictedPose)
			{
				Pred = PredictedPose;
			}
			else
			{
				Pred = Client->GetPlayerPosition();
				if (!Pred.IsValid())
				{
					return;
				}
			}

			if (bVR && !VR->IsInputBlocked())
			{
				Pred.SetAceFacingFromUnrealDir2D(VR->GetBodyForward());
				Pred.Location += FACEPosition::AceVectorToUnreal(VRRoomDelta / MovementSteps, 1.f / WorldScale);
				Pred.NormalizeOutdoorLandblock();
			}
			if (!FMath::IsNearlyZero(T) || !FMath::IsNearlyZero(PendingMouseTurnDegrees))
			{
				// Turn about AC +Z. Allowed mid-jump for visuals — arc uses JumpWorldAceVelocity.
				constexpr float TurnRateDegPerSec = 180.f;
				float TurnInput = T;
                const bool bFollowCamera = (IsUseMouseTurning() || bInstantMouseLookHeld) && !FMath::IsNearlyZero(PendingMouseTurnDegrees);
                if (bFollowCamera)
                {
                    TurnInput = PendingMouseTurnDegrees / FMath::Max(TurnRateDegPerSec * DeltaTime, KINDA_SMALL_NUMBER);
                    if (auto* Boom = P->FindComponentByClass<USpringArmComponent>())
                    {
                        FRotator Relative = Boom->GetRelativeRotation();
                        Relative.Yaw -= PendingMouseTurnDegrees;
                        Boom->SetRelativeRotation(Relative);
                    }
                }
				const FQuat Ac(Pred.RotationXYZ.X, Pred.RotationXYZ.Y, Pred.RotationXYZ.Z, Pred.RotationW);
				const FQuat TurnDelta(FVector::UpVector, FMath::DegreesToRadians(-TurnRateDegPerSec * TurnInput * DeltaTime));
				const FQuat NewAc = (TurnDelta * Ac).GetNormalized();
				Pred.RotationW = NewAc.W;
				Pred.RotationXYZ = FVector(NewAc.X, NewAc.Y, NewAc.Z);
				PendingMouseTurnDegrees = 0.f;
			}
			// Use approach: rotate in place toward the door; Walking is blocked until aligned.
			if (bServerMoveToActive && !ApproachTargetDir.IsNearlyZero()
				&& (UseApproachPhase == EACEUseApproachPhase::Turning
					|| UseApproachPhase == EACEUseApproachPhase::Holding))
			{
				FACEPosition FaceTarget = Pred;
				FaceTarget.SetAceFacingFromUnrealDir2D(ApproachTargetDir);
				const float Dot = Pred.GetFacingDotUnreal2D(ApproachTargetDir);
				const float Alpha = (Dot < 0.5f)
					? FMath::Clamp(DeltaTime * 6.f, 0.f, 1.f)
					: FMath::Clamp(DeltaTime * 8.f, 0.f, 1.f);
				const FQuat Blended = FQuat::Slerp(Pred.GetAcQuat(), FaceTarget.GetAcQuat(), Alpha).GetNormalized();
				Pred.RotationW = Blended.W;
				Pred.RotationXYZ = FVector(Blended.X, Blended.Y, Blended.Z);
			}
			if (!bJumpAirborne && (!FMath::IsNearlyZero(F) || !FMath::IsNearlyZero(R)))
			{
				// Retail CPhysicsObj::UpdatePositionInternal scales grounded animation
				// displacement by the creature's size. Airborne velocity is separate.
				const float GroundScale = GetLocalCreatureScale();
				const float ForwardSpeed = Client->GetLocomotionSpeed(bRunning) * GroundScale;
				const float SideSpeed = Client->GetSidestepSpeed(bRunning) * GroundScale;
				// ACE MotionInterp: WalkBackwards → WalkForward with speed *= -0.65.
				const float BackFactor = (F < 0.f) ? 0.65f : 1.f;
				Pred.Location += Pred.GetAceForwardInAcSpace() * (F * BackFactor) * ForwardSpeed * DeltaTime
					+ Pred.GetAceRightInAcSpace() * R * SideSpeed * DeltaTime;
				Pred.NormalizeOutdoorLandblock();
			}
			// Ballistic arc from leave-ground velocity (world ACE); no mid-air redirect.
			// Always integrate while airborne — zeroing XY at a wall used to make the full
			// vector NearlyZero at apex and stop gravity (hang midair).
			if (bJumpAirborne)
			{
				Pred.Location += JumpWorldAceVelocity * DeltaTime + FVector(0,0,-4.9f * DeltaTime * DeltaTime);
				JumpWorldAceVelocity.Z += -9.8f * DeltaTime;
				Pred.NormalizeOutdoorLandblock();
			}

			if (bLocalPredicting)
			{
				bHavePredictedPose = true;
			}

			// Soft-correct toward the server only while idle. Reconciling XY while predicting
			// pulls AutoPos backward against local run (slow movement + rubber-banding).
			if (bHavePredictedPose)
			{
				PredictedPose = Pred;
				if (bHaveLastServerPose
					&& !bEnterWorldLoading && !bWorldRevealActive
					&& !bJumpAirborne)
				{
					if (bLocalPredicting)
					{
						// Catastrophic desync only (lag spike / admin move).
						const FVector PredUe = PredictedPose.ToUnrealLocation(WorldScale);
						const FVector SrvUe = LastServerPose.ToUnrealLocation(WorldScale);
						if (FVector::Dist2D(PredUe, SrvUe) >= ServerSnapErrorCm)
						{
							SoftReconcilePredictedTowardServer(LastServerPose, DeltaTime, /*bAllowHeading*/ false);
							Pred = PredictedPose;
						}
					}
					else
					{
						SoftReconcilePredictedTowardServer(LastServerPose, DeltaTime, /*bAllowHeading*/ false);
						Pred = PredictedPose;
					}
				}
			}

			FVector Desired = Pred.ToUnrealLocation(WorldScale);
			const FVector BeforeCollision = Desired;
			const FVector Current = P->GetActorLocation();
			const uint32 MovementStartCell = Pred.CellId;
			FName MovementBlocker = NAME_None;
			FVector MovementContactNormal = FVector::ZeroVector;
			float MovementPenetration = 0.f;
			bool bMovementLedge = false;

			float CapsuleHalfHeight = 88.f;
			float CapsuleRadius = 34.f;
			if (UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>())
			{
				CapsuleHalfHeight = Cap->GetScaledCapsuleHalfHeight();
				CapsuleRadius = Cap->GetScaledCapsuleRadius();
			}

			constexpr float FloorZ = 0.66417414618662751f;
			auto FinishJumpLanding = [&]()
			{
				// Poses received before touchdown describe the old airborne height.
				// Wait for a fresh server anchor rather than reconciling back up.
				bHaveLastServerPose = false;
				bJumpAirborne = false;
				bJumpAirborneSent = false;
				bStandingJumpLocked = false;
				bHaveJumpLaunchFacing = false;
				StandingJumpAimF = 0.f;
				StandingJumpAimR = 0.f;
				JumpLocalAceVelocity = FVector::ZeroVector;
				JumpWorldAceVelocity = FVector::ZeroVector;
				JumpAirborneSeconds = 0.f;
				if (UACECharacterAppearanceComponent* App =
					P->FindComponentByClass<UACECharacterAppearanceComponent>())
				{
					App->ClearJumpMotionIfAny();
					App->SetSuppressLocoIdleBlend(false);
				}
				if (Client)
				{
					Client->SetForcePositionReporting(false);
					Client->FlushAutonomousPosition(true);
					bForceMovementResend = true;
				}
			};

			// Collision + stair climb. Dual thresholds match PhysicsGlobals:
			//   LandingZ (~0.087) — wall vs landable (step-down / jump land)
			//   FloorZ   (~0.664) — ON_WALKABLE / slope cling (≈48.4°)
			if (UWorld* World = GetWorld())
			{
				constexpr float WallSkinCm = 2.f;
				constexpr float LandingZ = 0.0871557f;
				const FCollisionShape Shape = FCollisionShape::MakeCapsule(
					FMath::Max(8.f, CapsuleRadius),
					FMath::Max(10.f, CapsuleHalfHeight));
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ACEWallBlock), /*bTraceComplex*/ true, P);
				if (bVR) { TArray<AActor*> AttachedPresentation; P->GetAttachedActors(AttachedPresentation, true, true); Params.AddIgnoredActors(AttachedPresentation); }
				// Channel sweep (ECC_Pawn): ObjectType-only queries miss WorldDynamic meshes and
				// any proxy that isn't ObjectType=WorldStatic. Solid props Block Pawn.
				constexpr ECollisionChannel MoveChannel = ECC_Pawn;

				UACEDatSubsystem* WaterDat = GetGameInstance()
					? GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;

				const bool bIndoorPred = bHavePredictedPose
					&& (static_cast<uint32>(PredictedPose.CellId) & 0xFFFFu) >= 0x0100u;
				const float StepUpCmRetail = GetStepUpHeightCm();
				// Do NOT inflate indoor step-up to a full meter — that lets the capsule
				// land on low second-story floors / climb through shop ceilings.
				// Stairs use walkable collision + lateral probes; keep retail StepUpHeight.
				const float StepUpCm = StepUpCmRetail;

				// P0: retail ObjCell::find_cell_list — only these cells participate in sweeps.
				TArray<uint32> TransitCells;
				TSet<uint32> TransitCellSet;
				TSet<uint32> TransitEnvCells;
				bool bTransitHitsInterior = false;
				bool bTransitHasOutdoorLand = !bIndoorPred;
				if (WaterDat)
				{
					const uint32 HintCell = bHavePredictedPose
						? static_cast<uint32>(PredictedPose.CellId)
						: static_cast<uint32>(Pred.CellId);
					// Center + CapsuleRadius missed walls above the waist; jump arcs into
					// neighbor EnvCells that were transit-ignored → pass-through walls.
					const float CapsuleBoundR = ACECellTransit::ComputeCapsuleTransitRadiusCm(
						CapsuleRadius, CapsuleHalfHeight);
					float TransitSphereR = CapsuleBoundR;
					const FVector DestCenterUe(
						Desired.X, Desired.Y, Desired.Z + CapsuleHalfHeight);
					if (bJumpAirborne)
					{
						const float ArcLen = FVector::Dist(
							FVector(Current.X, Current.Y, Current.Z), DestCenterUe);
						TransitSphereR = FMath::Max(
							TransitSphereR, ArcLen + CapsuleBoundR + 16.f);
					}
					else
					{
						TransitSphereR = FMath::Max(
							TransitSphereR,
							FVector::Dist2D(Current, DestCenterUe) + CapsuleBoundR);
					}
					auto AbsorbTransitAt = [&](const FVector& Center)
					{
						TArray<uint32> Local;
						bool bLocalInterior = false;
						ACECellTransit::FindCellList(
							*WaterDat, HintCell, Center, TransitSphereR, WorldScale, Local, &bLocalInterior);
						if (bLocalInterior)
						{
							bTransitHitsInterior = true;
						}
						for (uint32 CellId : Local)
						{
							if (TransitCellSet.Contains(CellId))
							{
								continue;
							}
							TransitCellSet.Add(CellId);
							TransitCells.Add(CellId);
						}
					};
					AbsorbTransitAt(FVector(Current.X, Current.Y, Current.Z));
					// Include the destination even while outdoor-resident. A walking
					// capsule can cross a doorway before its current cell changes.
					if (bJumpAirborne || !Current.Equals(DestCenterUe, .01f))
					{
						AbsorbTransitAt(DestCenterUe);
						if (bJumpAirborne)
						{
							AbsorbTransitAt(
								(FVector(Current.X, Current.Y, Current.Z) + DestCenterUe) * 0.5f);
							if (JumpWorldAceVelocity.Z > 0.f)
							{
								const float ApexZ = FMath::Max(Current.Z, DestCenterUe.Z)
									+ JumpWorldAceVelocity.Z * WorldScale * DeltaTime;
								AbsorbTransitAt(FVector(DestCenterUe.X, DestCenterUe.Y, ApexZ));
							}
						}
					}
					bTransitHasOutdoorLand = false;
					for (uint32 CellId : TransitCells)
					{
						if (ACECellTransit::IsIndoorCell(CellId))
						{
							TransitEnvCells.Add(CellId);
						}
						else
						{
							bTransitHasOutdoorLand = true;
						}
					}
					// Retail CObjCell::find_cell_list: an outdoor seed *always* runs
					// add_all_outside_cells — land cells are unconditionally in the
					// collision set while outdoor-resident. Never let a transit miss
					// leave an outdoor player with zero land collision.
					if (!bIndoorPred)
					{
						bTransitHasOutdoorLand = true;
					}
				}

				auto IsOutdoorTerrainHit = [](const FHitResult& Hit) -> bool
				{
					return Hit.Component.IsValid()
						&& Hit.Component->ComponentTags.Contains(FName(TEXT("ACEOutdoorTerrain")));
				};
				auto IsBuildingShellHit = [](const FHitResult& Hit) -> bool
				{
					return Hit.Component.IsValid()
						&& Hit.Component->ComponentTags.Contains(FName(TEXT("ACEBuildingShell")));
				};

				// Retail: land terrain always collides while the object is outdoor-resident
				// (CLandCell::find_env_collisions runs unconditionally; hits_interior_cell
				// only relaxes the *building shell* solid test, never land). Land drops out
				// of the set solely via residency: once the point is inside an EnvCell's
				// cell BSP, the seed is indoor and land is omitted unless an outside portal
				// straddle re-adds it.
				auto IsBlockingOutdoorTerrain = [&](const FHitResult& Hit, float X, float Y) -> bool
				{
					return IsOutdoorTerrainHit(Hit) && bTransitHasOutdoorLand && !bIndoorPred;
				};

				// Retail OBJECTINFO::validate_walkable: contact embeds waterDepth along the plane
				// (zDist = dist / N.z). Flat land → SurfaceZ - depth; slopes scale by 1/Nz.
				auto ApplyRetailWaterDepth = [WaterDat, this](float X, float Y, float SurfaceZ, float NormalZ) -> float
				{
					if (!WaterDat)
					{
						return SurfaceZ;
					}
					const float DepthCm = WaterDat->GetOutdoorWaterDepthCm(X, Y, WorldScale);
					if (DepthCm <= KINDA_SMALL_NUMBER)
					{
						return SurfaceZ;
					}
					const float Nz = FMath::Max(0.15f, NormalZ);
					return SurfaceZ - DepthCm / Nz;
				};

				// Cell-partitioned sweeps: ignore outdoor land when not in transit; ignore
				// EnvCells outside the transit set (StabList peek stay draw-only).
				FCollisionQueryParams SweepParams = Params;
				const bool bReuseMovementFilter = bVR && MovementFilterCache.IsSet()
					&& bMovementFilterOutdoor == bTransitHasOutdoorLand
					&& MovementFilterCells.Num() == TransitEnvCells.Num()
					&& MovementFilterCells.Includes(TransitEnvCells);
				if (bReuseMovementFilter) SweepParams = MovementFilterCache.GetValue();
				if (!bReuseMovementFilter && !bTransitHasOutdoorLand)
				{
					for (TActorIterator<AACELandblockActor> It(World); It; ++It)
					{
						if (It->TerrainMesh)
						{
							SweepParams.AddIgnoredComponent(
								static_cast<const UPrimitiveComponent*>(It->TerrainMesh.Get()));
						}
					}
					for (TActorIterator<AACETerrainChunkActor> It(World); It; ++It)
					{
						if (It->TerrainMesh)
						{
							SweepParams.AddIgnoredComponent(
								static_cast<const UPrimitiveComponent*>(It->TerrainMesh.Get()));
						}
					}
				}
				if (!bReuseMovementFilter && (TransitEnvCells.Num() > 0 || WaterDat))
				{
					ACECellTransit::RestrictRoomCollision(*World, TransitEnvCells, SweepParams);
				}
				if (bVR && !bReuseMovementFilter)
				{
					MovementFilterCache = SweepParams;
					MovementFilterCells = TransitEnvCells;
					bMovementFilterOutdoor = bTransitHasOutdoorLand;
				}
				if (!bIndoorPred && bTransitHasOutdoorLand)
					FilterWadingTerrain(*World,WaterDat,P->GetActorLocation(),Desired,WorldScale,SweepParams);

				// OBJECTINFO::get_walkable_z / CTransition::step_up use the
				// walking threshold for a grounded body. LandingZ alone allowed
				// walking through the steep sloped side of a statue pedestal.
				const float BlockingNormalZ = bJumpAirborne ? LandingZ : FloorZ;
				auto IsWallHit = [BlockingNormalZ](const FHitResult& Hit) -> bool
				{
					if (!Hit.bBlockingHit || Hit.Normal.Z >= BlockingNormalZ)
					{
						return false;
					}
					// Steep heightfield faces are walls (retail slide along precipices).
					// Treating land as never-a-wall let the capsule jitter on the lip.
					return true;
				};

				auto SweepCapsule = [&](FHitResult& OutHit, const FVector& From, const FVector& To) -> bool
				{
					const bool Hit = ACEBodySweep::Sweep(*World, OutHit, From, To, Shape, SweepParams);
					if (Hit && IsWallHit(OutHit))
					{
						MovementBlocker = OutHit.GetComponent() ? OutHit.GetComponent()->GetFName() : NAME_None;
						MovementContactNormal = OutHit.Normal; MovementPenetration = OutHit.PenetrationDepth;
					}
					return Hit;
				};
				auto RecoverPenetration = [&](const FVector& From, const FVector& Push, const FHitResult& Original)
				{
					return ACEBodySweep::Recover(*World, From, Push, Original, Shape, SweepParams);
				};

				if (bServerMoveToActive && ServerMoveToTargetGuid != 0)
				{
					for (TActorIterator<AACEWorldEntityActor> It(World); It; ++It)
					{
						if (It->GetACEGuid() == ServerMoveToTargetGuid)
						{
							Params.AddIgnoredActor(*It);
							break;
						}
					}
					// Do NOT ignore landblock terrain here — SampleFeetZ needs outdoor
					// walkables or precipice blocking freezes Use-approach on grade.
				}

				const float FeetNow = Current.Z - CapsuleHalfHeight;
				const float MoveDist2D = FVector::Dist2D(Current, Desired);
				// Preserve distance at low frame rates. Subdivide collision below;
				// capping distance here made run speed proportional to frame rate.
				const float MaxHorizStep = FMath::Max(22.f, CapsuleRadius * 0.65f);
				const bool bIndoorNow = (static_cast<uint32>(Pred.CellId) & 0xFFFFu) >= 0x0100u;
				const float RampClimbCm = bIndoorNow
					? (StepUpCm + 12.f)
					: (StepUpCm + 28.f);
				// Retail CheckWalkable / StepDownHeight (~0.5 AC). Add waterDepth only when wet.
				const float StepDownCm = GetStepDownHeightCm();
				const float WaterDepthDestCm = WaterDat
					? WaterDat->GetOutdoorWaterDepthCm(Desired.X, Desired.Y, WorldScale) : 0.f;
				const float RampDropCm = FMath::Clamp(StepDownCm + 8.f, 20.f, StepDownCm + 20.f)
					+ WaterDepthDestCm;

				// Desktop's step grace can hold feet on a nonexistent floor for
				// almost a second after sliding off furniture. Tracked locomotion
				// must use the actual lower-sphere support at the resolved position.
				const float StepHoldDuration = bVR ? 0.f : bIndoorNow ? 0.85f : 0.35f;

				// Retail CLandCell::find_env_collisions: Collided only when the *landblock*
				// is EntirelyWater (get_block_water_type). Coastal cells that are fully
				// flooded on a PartiallyWater block are walkable at 0.9 AC depth.
				const bool bDestEntirelyWater = !bIndoorNow && WaterDat
					&& WaterDat->GetOutdoorBlockWaterType(Desired.X, Desired.Y, WorldScale) == 2;

				auto CollectWalkableFeet = [&](float X, float Y, float NearFeetZ, float MaxUp, float MaxDown,
					float& BestIndoorZ, bool& bHaveIndoorHit, float& BestOutdoorZ, bool& bHaveOutdoorHit)
				{
					const float TraceTop = NearFeetZ + MaxUp + .5f;
					const FVector TraceStart(X, Y, TraceTop);
					const FVector TraceEnd(X, Y, NearFeetZ - MaxDown - 20.f);
					TArray<FHitResult> Hits;
					if (!World->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, MoveChannel, Params))
					{
						return;
					}
					for (const FHitResult& Hit : Hits)
					{
						// Accept landable slopes (retail LandingZ). FloorZ gates ON_WALKABLE cling.
						const float MinWalkZ = bJumpAirborne ? LandingZ : FloorZ;
						if (!Hit.bBlockingHit || Hit.ImpactNormal.Z < MinWalkZ)
						{
							continue;
						}
						if (Hit.ImpactPoint.Z > NearFeetZ + MaxUp)
						{
							continue;
						}
						if (Hit.ImpactPoint.Z < NearFeetZ - MaxDown)
						{
							continue;
						}
						if (IsBlockingOutdoorTerrain(Hit, X, Y))
						{
							const float WalkZ = ApplyRetailWaterDepth(
								X, Y, Hit.ImpactPoint.Z, Hit.ImpactNormal.Z);
							if (!bHaveOutdoorHit
								|| FMath::Abs(WalkZ - NearFeetZ) < FMath::Abs(BestOutdoorZ - NearFeetZ))
							{
								BestOutdoorZ = WalkZ;
								bHaveOutdoorHit = true;
							}
						}
						else if (!IsOutdoorTerrainHit(Hit))
						{
							// ACEBuildingShell is walkable (Setup stairs/stoops). Skipping it
							// left only the heightfield in front of the riser — TryStepUp died.
							if (!IsBuildingShellHit(Hit))
							{
								// Stab scenery (tent roofs, awnings) only within StepUp —
								// MaxUp/RampClimbCm is for outdoor grade + indoor stairs.
								const float SceneryMaxUp = bIndoorNow ? MaxUp : (StepUpCm + 12.f);
								if (Hit.ImpactPoint.Z > NearFeetZ + SceneryMaxUp)
								{
									continue;
								}
							}
							// Prefer the walkable nearest the feet — max-Z snapped to loft
							// ceilings / second stories and shoved the capsule upward.
							if (!bHaveIndoorHit
								|| FMath::Abs(Hit.ImpactPoint.Z - NearFeetZ)
									< FMath::Abs(BestIndoorZ - NearFeetZ))
							{
								BestIndoorZ = Hit.ImpactPoint.Z;
								bHaveIndoorHit = true;
							}
						}
					}
				};

				auto SampleFeetZ = [&](float X, float Y, float NearFeetZ, float MaxUp, float MaxDown, float& OutFeetZ) -> bool
				{
					float BestIndoorZ = -TNumericLimits<float>::Max();
					float BestOutdoorZ = -TNumericLimits<float>::Max();
					bool bHaveIndoorHit = false;
					bool bHaveOutdoorHit = false;
					CollectWalkableFeet(X, Y, NearFeetZ, MaxUp, MaxDown,
						BestIndoorZ, bHaveIndoorHit, BestOutdoorZ, bHaveOutdoorHit);

					const bool bNeedRing = bIndoorNow
						? !bHaveIndoorHit
						: (!bHaveIndoorHit && (!bHaveOutdoorHit
							|| BestOutdoorZ < NearFeetZ + 2.f));
					if (bNeedRing)
					{
						const float Ring = FMath::Max(6.f, CapsuleRadius * 0.45f);
						const FVector2D Offs[] = {
							FVector2D(Ring, 0.f), FVector2D(-Ring, 0.f),
							FVector2D(0.f, Ring), FVector2D(0.f, -Ring),
							FVector2D(Ring * 0.7f, Ring * 0.7f), FVector2D(-Ring * 0.7f, Ring * 0.7f),
							FVector2D(Ring * 0.7f, -Ring * 0.7f), FVector2D(-Ring * 0.7f, -Ring * 0.7f)
						};
						for (const FVector2D& O : Offs)
						{
							CollectWalkableFeet(X + O.X, Y + O.Y, NearFeetZ, MaxUp, MaxDown,
								BestIndoorZ, bHaveIndoorHit, BestOutdoorZ, bHaveOutdoorHit);
						}
					}

					// Outdoor DAT land+water — retail keeps the heightfield as real support
					// everywhere while outdoor-resident, including under building footprints
					// (the DAT terrain dips into basement mouths; there is no "lid" to fight).
					// Raised walkables (stoops/stairs) still win below via the step preference.
					if (!bIndoorNow && WaterDat)
					{
						float DatFeetZ = NearFeetZ;
						FVector DatNormal;
						if (WaterDat->SampleOutdoorGroundZ(X, Y, WorldScale, DatFeetZ, &DatNormal)
							&& DatNormal.Z >= FloorZ
							&& DatFeetZ <= NearFeetZ + MaxUp
							&& DatFeetZ >= NearFeetZ - MaxDown)
						{
							if (!bHaveOutdoorHit
								|| FMath::Abs(DatFeetZ - NearFeetZ) < FMath::Abs(BestOutdoorZ - NearFeetZ))
							{
								BestOutdoorZ = DatFeetZ;
								bHaveOutdoorHit = true;
							}
						}
					}

					if (bIndoorNow)
					{
						if (bHaveIndoorHit)
						{
							OutFeetZ = BestIndoorZ;
							return true;
						}
						return false;
					}
					// Outdoors: raised Stab scenery (stoops/chests/stairs) only within StepUp —
					// RampClimbCm (≤160) was treating low tent roofs as floors and snapping
					// the capsule onto the canvas when walking through the doorway.
					const float RaisedMaxUp = StepUpCm + 12.f;
					if (bHaveIndoorHit
						&& BestIndoorZ <= NearFeetZ + RaisedMaxUp
						&& BestIndoorZ >= NearFeetZ - MaxDown
						&& (!bHaveOutdoorHit || BestIndoorZ >= BestOutdoorZ + 1.5f))
					{
						OutFeetZ = BestIndoorZ;
						return true;
					}
					if (bHaveOutdoorHit)
					{
						OutFeetZ = BestOutdoorZ;
						return true;
					}
					// Outdoors: never fall back to high Stab hits (tent roofs). Same-level
					// scenery only — raised walkables already handled above via RaisedMaxUp.
					if (bHaveIndoorHit
						&& (bIndoorNow || BestIndoorZ <= NearFeetZ + 8.f))
					{
						OutFeetZ = BestIndoorZ;
						return true;
					}
					return false;
				};

				auto TryStepUp = [&](const FVector& From, const FVector& DesiredEnd, FVector& OutLanded) -> bool
				{
					// Retail Transition.StepUp only while OnWalkable — never mid-jump.
					if (bJumpAirborne || StepUpCm < 1.f)
					{
						return false;
					}
					FVector MoveDir = DesiredEnd - From;
					MoveDir.Z = 0.f;
					if (MoveDir.SizeSquared() < KINDA_SMALL_NUMBER)
					{
						MoveDir = Pred.GetAceForwardVector().GetSafeNormal2D();
						if (MoveDir.IsNearlyZero())
						{
							return false;
						}
					}
					else
					{
						MoveDir.Normalize();
					}

					// Short reaches first — pool rims / curb lips are narrower than the capsule.
					// Long hops land the center in the water void and SampleFeetZ returns grade (Rise≈0).
					const float ReachCandidates[] = {
						CapsuleRadius * 0.22f,
						CapsuleRadius * 0.38f,
						CapsuleRadius * 0.55f,
						CapsuleRadius * 0.75f,
						CapsuleRadius * 1.05f
					};
					const float LiftCandidates[] = {
						12.f,
						FMath::Min(36.f, StepUpCm * 0.35f),
						StepUpCm * 0.50f,
						StepUpCm * 0.70f,
						StepUpCm,
						FMath::Min(StepUpCm, CapsuleRadius * 1.1f)
					};
                    struct FLiftProbe { FVector Position; bool bBlocked=false; };
                    TArray<FLiftProbe,TInlineAllocator<6>> LiftProbes;
                    for (const float LiftCm : LiftCandidates)
                    {
                        auto& Probe=LiftProbes.AddDefaulted_GetRef();
                        Probe.Position=From+FVector(0,0,LiftCm);
                        if (LiftCm<3.f) { Probe.bBlocked=true; continue; }
                        FHitResult UpHit;
                        if (SweepCapsule(UpHit,From,Probe.Position) && IsWallHit(UpHit) && !UpHit.bStartPenetrating)
                        {
                            if (UpHit.ImpactNormal.Z<-.15f)
                            {
                                Probe.Position.Z=UpHit.Location.Z-2.f;
                                Probe.bBlocked=Probe.Position.Z<=From.Z+3.f;
                            }
                            else Probe.bBlocked=true;
                        }
                    }
					const float FeetAtFrom = From.Z - CapsuleHalfHeight;

					// Indoor L/quarter-turn stairs: next tread is offset sideways — forward-only
					// reach lands past the pie and misses. Probe ±45°/±90° with short reaches.
					TArray<FVector, TInlineAllocator<5>> ReachDirs;
					ReachDirs.Add(MoveDir);
					if (bIndoorNow)
					{
						const FVector Right(-MoveDir.Y, MoveDir.X, 0.f);
						ReachDirs.Add((MoveDir + Right * 0.7f).GetSafeNormal2D());
						ReachDirs.Add((MoveDir - Right * 0.7f).GetSafeNormal2D());
					}

					for (const FVector& Dir : ReachDirs)
					{
					for (const float ReachCm : ReachCandidates)
					{
						if (ReachCm < 4.f)
						{
							continue;
						}
						// Lateral dirs: keep reaches short so we don't hop into walls.
						const bool bLateral = FVector::DotProduct(Dir, MoveDir) < 0.95f;
						if (bLateral && ReachCm > CapsuleRadius * 0.55f)
						{
							continue;
						}
                        // Every reach/direction uses the same vertical path. Resolve it
                        // once per height instead of repeating it up to eleven times.
                        for (const auto& Probe : LiftProbes)
                        {
                            if (Probe.bBlocked) continue;
                            const FVector Lifted=Probe.Position;
							const FVector LiftedEnd = Lifted + Dir * ReachCm;
							FHitResult HorizHit;
							FVector AfterHoriz = LiftedEnd;
							if (SweepCapsule(HorizHit, Lifted, LiftedEnd) && IsWallHit(HorizHit))
							{
								if (HorizHit.bStartPenetrating)
								{
									continue;
								}
								const float Progress = FVector::Dist2D(Lifted, HorizHit.Location);
								if (Progress < 2.f)
								{
									continue;
								}
								AfterHoriz = FVector(HorizHit.Location.X, HorizHit.Location.Y, Lifted.Z)
									+ HorizHit.ImpactNormal.GetSafeNormal2D() * WallSkinCm;
							}

							float LandFeetZ = 0.f;
							bool bHaveLand = false;
							const float LandMaxUp = bIndoorNow ? (StepUpCm + 12.f) : (StepUpCm + 28.f);
							const float LandMaxDown = CapsuleHalfHeight + StepUpCm + 16.f;
							auto TryLandAt = [&](float X, float Y) -> bool
							{
								float Z = 0.f;
								if (!bIndoorNow)
								{
									// SampleFeetZ prefers land (nearest to feet) at the XY in
									// front of a riser. For outdoor Setup stairs, take the
									// highest building-shell tread in StepUp — same as indoor.
									const float TreadMaxUp = StepUpCm + 12.f;
									const float TraceTop = FeetAtFrom + LandMaxUp + .5f;
									TArray<FHitResult> StairHits;
									if (World->LineTraceMultiByChannel(
										StairHits, FVector(X, Y, TraceTop),
										FVector(X, Y, FeetAtFrom - LandMaxDown - 20.f),
										MoveChannel, SweepParams))
									{
										float BestZ = -TNumericLimits<float>::Max();
										bool bFound = false;
										for (const FHitResult& Hit : StairHits)
										{
											if (!Hit.bBlockingHit || Hit.ImpactNormal.Z < FloorZ)
											{
												continue;
											}
											if (Hit.ImpactPoint.Z > FeetAtFrom + TreadMaxUp
												|| Hit.ImpactPoint.Z < FeetAtFrom - LandMaxDown)
											{
												continue;
											}
											if (!IsBuildingShellHit(Hit))
											{
												continue;
											}
											if (Hit.ImpactPoint.Z > BestZ)
											{
												BestZ = Hit.ImpactPoint.Z;
												bFound = true;
											}
										}
										if (bFound)
										{
											LandFeetZ = BestZ;
											return true;
										}
									}
								}
								if (bIndoorNow)
								{
									// Nearest-to-feet SampleFeetZ prefers the floor in front of
									// a riser. For step-up, take the highest walkable in range
									// (the tread) instead of the floor.
									const float TraceTop = FeetAtFrom + LandMaxUp + .5f;
									TArray<FHitResult> Hits;
									if (World->LineTraceMultiByChannel(
										Hits, FVector(X, Y, TraceTop),
										FVector(X, Y, FeetAtFrom - LandMaxDown - 20.f),
										MoveChannel, Params))
									{
										float BestZ = -TNumericLimits<float>::Max();
										bool bFound = false;
										for (const FHitResult& Hit : Hits)
										{
											if (!Hit.bBlockingHit || Hit.ImpactNormal.Z < FloorZ)
											{
												continue;
											}
											if (Hit.ImpactPoint.Z > FeetAtFrom + LandMaxUp
												|| Hit.ImpactPoint.Z < FeetAtFrom - LandMaxDown)
											{
												continue;
											}
											if (IsOutdoorTerrainHit(Hit))
											{
												continue;
											}
											if (Hit.ImpactPoint.Z > BestZ)
											{
												BestZ = Hit.ImpactPoint.Z;
												bFound = true;
											}
										}
										if (bFound)
										{
											LandFeetZ = BestZ;
											return true;
										}
									}
								}
								if (SampleFeetZ(X, Y, FeetAtFrom, LandMaxUp, LandMaxDown, Z))
								{
									LandFeetZ = Z;
									return true;
								}
								const FVector DownEnd(X, Y, FeetAtFrom - 8.f);
								FHitResult DownHit;
								if (SweepCapsule(DownHit, FVector(X, Y, AfterHoriz.Z), DownEnd) && DownHit.bBlockingHit
									&& DownHit.ImpactNormal.Z >= FloorZ)
								{
									if (!bIndoorNow || !IsOutdoorTerrainHit(DownHit))
									{
										LandFeetZ = DownHit.ImpactPoint.Z;
										return true;
									}
								}
								return false;
							};
							if (TryLandAt(AfterHoriz.X, AfterHoriz.Y))
							{
								bHaveLand = true;
							}
							else
							{
								// Indoor L/turns and outdoor Setup stairs: next tread can sit
								// beside the capsule center (town-hall / cellar).
								const float R = CapsuleRadius * 0.55f;
								const float Offsets[][2] = {
									{ R, 0.f }, { -R, 0.f }, { 0.f, R }, { 0.f, -R },
									{ R * 0.7f, R * 0.7f }, { -R * 0.7f, R * 0.7f },
									{ R * 0.7f, -R * 0.7f }, { -R * 0.7f, -R * 0.7f }
								};
								for (const auto& O : Offsets)
								{
									if (TryLandAt(AfterHoriz.X + O[0], AfterHoriz.Y + O[1]))
									{
										AfterHoriz.X += O[0];
										AfterHoriz.Y += O[1];
										bHaveLand = true;
										break;
									}
								}
							}
							if (!bHaveLand)
							{
								continue;
							}
							// Must actually climb — landing back on grade through a hollow (pool)
							// is not a step-up.
							const float RiseFeet = LandFeetZ - FeetAtFrom;
							if (RiseFeet < 0.5f || RiseFeet > (bIndoorNow ? StepUpCm + 12.f : StepUpCm + 28.f))
							{
								continue;
							}
							const float LandCenterZ = LandFeetZ + CapsuleHalfHeight;
							FVector Landed(AfterHoriz.X, AfterHoriz.Y, LandCenterZ);
                            // Side probes may find the tread behind us when
                            // leaving a bench. A step must advance in the input
                            // direction, never pull the player back onto it.
                            if (FVector::DotProduct((Landed-From).GetSafeNormal2D(),MoveDir)<=0.f) continue;
							if (!ACEBodySweep::CanTraverseStep(*World, Lifted, AfterHoriz, Landed,
								Shape, SweepParams, FloorZ, &Landed))
							{
								continue;
							}
							if (Landed.Z-From.Z>StepUpCm+12.f) continue;
							// Block second-floor / low-ceiling clips: only reject true overhead
							// ceilings near the head. Do not reject stair climbs when a distant
							// ceiling is in the sweep (previous check killed all indoor steps).
							{
								FHitResult CeilHit;
								const FVector HeadEnd = Landed + FVector(0.f, 0.f, CapsuleHalfHeight + 6.f);
								if (SweepCapsule(CeilHit, Landed, HeadEnd) && CeilHit.bBlockingHit
									&& !CeilHit.bStartPenetrating
									&& CeilHit.ImpactNormal.Z < -0.55f)
								{
									const float HeadClear = CeilHit.ImpactPoint.Z - LandFeetZ;
									if (HeadClear < CapsuleHalfHeight * 2.f + 4.f)
									{
										continue;
									}
								}
								if (SweepCapsule(CeilHit, Landed, Landed + FVector(0.f, 0.f, 1.f))
									&& CeilHit.bStartPenetrating
									&& CeilHit.ImpactNormal.Z < -0.55f)
								{
									continue;
								}
							}
							OutLanded = Landed;
							return true;
						}
					}
					} // ReachDirs
					return false;
				};

				float DestFeetZ = FeetNow;
				bool bHaveRampGround = SampleFeetZ(
					Desired.X, Desired.Y, FeetNow, RampClimbCm, RampDropCm, DestFeetZ);

                if (!bJumpAirborne)
                {
                    float SphereFeetZ=FeetNow;
                    if (ACEBodySweep::FindFootSupport(*World,FVector(Desired.X,Desired.Y,FeetNow),
                        CapsuleRadius,RampDropCm,SweepParams,SphereFeetZ,RampClimbCm)
                        && (!bHaveRampGround || SphereFeetZ>DestFeetZ))
                    {
                        DestFeetZ=SphereFeetZ; bHaveRampGround=true;
                    }
                }
				FVector Start = Current;
				// ACE Position / Pred.Location is the cylinder bottom (feet). The pawn actor
				// is at the capsule center — always add CapsuleHalfHeight when placing End.Z.
				const float DesiredFeetZ = Desired.Z;
				const float DesiredCenterZ = DesiredFeetZ + CapsuleHalfHeight;
				// Retail EdgeSlide / precipice: if destination has no support within StepDown,
				// try sliding along the ledge tangent instead of hard XY freeze.
				// Skip while ServerMoveTo outdoors — SampleFeetZ can miss briefly.
				float EndX = Desired.X;
				float EndY = Desired.Y;
				// Retail CLandCell::find_env_collisions → Collided on EntirelyWater *landblocks*.
				if (bDestEntirelyWater)
				{
					EndX = Current.X;
					EndY = Current.Y;
					bHaveRampGround = SampleFeetZ(
						EndX, EndY, FeetNow, RampClimbCm, RampDropCm, DestFeetZ);
				}
				else if (!bJumpAirborne && !bHaveRampGround && MoveDist2D > 0.5f
					&& !(bServerMoveToActive && !bIndoorNow))
				{
					bMovementLedge = true;
                    const FVector2D Slide = ACELedgeSlide::Resolve(FVector2D(Current.X,Current.Y),
                        FVector2D(Desired.X-Current.X,Desired.Y-Current.Y), [&](const FVector2D& At)
                        {
                            float Z=FeetNow;
                            return SampleFeetZ(At.X,At.Y,FeetNow,RampClimbCm,RampDropCm,Z);
                        });
                    EndX=Slide.X; EndY=Slide.Y;
                    bHaveRampGround=SampleFeetZ(EndX,EndY,FeetNow,RampClimbCm,RampDropCm,DestFeetZ);
				}
				FVector End(EndX, EndY,
					bJumpAirborne
						? DesiredCenterZ
						: (bHaveRampGround ? (DestFeetZ + CapsuleHalfHeight) : Current.Z));

				// Climbing onto higher indoor tread — refresh step-hold so plank gaps don't drop us.
				if (!bJumpAirborne && bHaveRampGround && bIndoorNow && DestFeetZ > FeetNow + 1.f)
				{
					StepHoldMinFeetZ = DestFeetZ;
					StepHoldSeconds = StepHoldDuration;
				}



				if (!bJumpAirborne)
				{
					FHitResult PenHit;
					if (SweepCapsule(PenHit, Start, Start + FVector(0.f, 0.f, 0.1f))
						&& PenHit.bStartPenetrating && IsWallHit(PenHit))
					{
						if (bVR)
						{
							static double LastVRPenetrationLog = 0;
							if (FPlatformTime::Seconds() - LastVRPenetrationLog > 1.)
							{
								LastVRPenetrationLog = FPlatformTime::Seconds();
								UE_LOG(LogTemp, Log, TEXT("ACE VR penetration: actor=%s component=%s cell=%08X normal=%s depth=%.2f feet=%.2f"),
									*GetNameSafe(PenHit.GetActor()), *GetNameSafe(PenHit.GetComponent()), Pred.CellId,
									*PenHit.ImpactNormal.ToString(), PenHit.PenetrationDepth, Start.Z - CapsuleHalfHeight);
							}
						}
						// Ceiling / soffit penetration: never TryStepUp — that shoves the
						// capsule through the floor above. Push along the 3D normal instead.
						if (PenHit.ImpactNormal.Z < -0.15f)
						{
							FVector Push = PenHit.ImpactNormal.GetSafeNormal();
							if (Push.IsNearlyZero())
							{
								Push = FVector(0.f, 0.f, -1.f);
							}
							Start = RecoverPenetration(Start, Push * (PenHit.PenetrationDepth + WallSkinCm), PenHit);
							// Drop a bogus "raised walkable" that was the soffit/roof above us.
							if (bHaveRampGround && DestFeetZ > FeetNow + StepUpCm + 12.f)
							{
								bHaveRampGround = false;
								End.Z = Start.Z;
							}
							else if (!bHaveRampGround)
							{
								End.Z = Start.Z;
							}
						}
						else
						{
							FVector Stepped;
							if (TryStepUp(Start, End, Stepped))
							{
								Start = Stepped;
								End = FVector(EndX, EndY, Stepped.Z);
								StepHoldMinFeetZ = Stepped.Z - CapsuleHalfHeight;
								StepHoldSeconds = StepHoldDuration;
							}
							else
							{
								Start = RecoverPenetration(Start, PenHit.Normal.GetSafeNormal2D()
									* (PenHit.PenetrationDepth + WallSkinCm), PenHit);
								if (!bHaveRampGround)
								{
									End.Z = Start.Z;
								}
							}
						}
					}
				}

				FVector Resolved = End;
				FVector BounceNormalUe = FVector::ZeroVector;
				bool bHaveBounceNormal = false;
				if (!bJumpAirborne) End.Z=FMath::Max(End.Z,Start.Z);
				const FVector PathGoal = End;
				if (bJumpAirborne)
				{
					// A steep contact redirects falling velocity; it is not a stable
					// landing. Ending/restarting the fall each frame loses momentum
					// and turns a roof slide into a long, slow crawl.
					const auto AirMove = ACEBodySweep::MoveAirborne(*World,Start,End,Shape,SweepParams,JumpWorldAceVelocity.Z<=0.f,FloorZ);
					Resolved=AirMove.Position; BounceNormalUe=AirMove.ContactNormal;
					bHaveBounceNormal=!BounceNormalUe.IsNearlyZero();
					if (AirMove.bLanded) FinishJumpLanding();
				}
				else
				{
				FVector PathStart = Start;
				// Move laterally before stepping down. A diagonal chord between
                // two valid sphere supports cuts through a narrow seat edge.
                // The final lower-sphere ground snap resolves descent at the
                // reached XY, after the body has cleared the obstruction.
                if (!bJumpAirborne) End.Z=FMath::Max(End.Z,Start.Z);
				const int32 SweepSteps = FMath::Clamp(FMath::CeilToInt(
					FVector::Dist2D(PathStart, PathGoal) / MaxHorizStep), 1, 32);
				const FVector StepDelta = (PathGoal - PathStart) / SweepSteps;
				for (int32 SweepStep = 0; SweepStep < SweepSteps; ++SweepStep)
				{
					// Continue from the resolved contact position. Re-aiming at the
					// original endpoint would accumulate blocked distance into the
					// next sweep and push through walls or skip narrow stair treads.
					End = PathStart + StepDelta;
					Start = PathStart;
					if (End.Equals(Start, 0.1f))
					{
						continue;
					}
					FHitResult Hit;
					bool bPathContact = SweepCapsule(Hit, Start, End);
                    // A ramp hit can hide a wall later in the same query.
                    // Follow its walkable plane and sweep the remaining path
                    // again; accepting End outright tunnels through the wall.
                    for (int32 RampPass=0; bPathContact && !IsWallHit(Hit)
                        && !Hit.bStartPenetrating && RampPass<3; ++RampPass)
                    {
                        Start=Hit.Location+Hit.Normal*.1f;
                        const FVector Remaining=End-Start;
                        End.Z=FMath::Max(End.Z, Start.Z
                            -(Hit.Normal.X*Remaining.X+Hit.Normal.Y*Remaining.Y)/Hit.Normal.Z);
                        bPathContact=SweepCapsule(Hit,Start,End);
                    }
					if (bPathContact && IsWallHit(Hit))
					{
						{
							static double LastWallLogSec = 0.0;
							const double NowSec = FPlatformTime::Seconds();
							if (NowSec - LastWallLogSec > 0.75)
							{
								LastWallLogSec = NowSec;
								const AActor* HitActor = Hit.GetActor();
								const UPrimitiveComponent* HitComp = Hit.Component.Get();
								UE_LOG(LogTemp, Verbose,
									TEXT("ACEWallBlock: actor=%s comp=%s cell=0x%08X n=(%.2f,%.2f,%.2f) pen=%d"),
									HitActor ? *HitActor->GetName() : TEXT("null"),
									HitComp ? *HitComp->GetName() : TEXT("null"),
									Pred.CellId,
									Hit.ImpactNormal.X, Hit.ImpactNormal.Y, Hit.ImpactNormal.Z,
									Hit.bStartPenetrating ? 1 : 0);
							}
						}
						FVector Stepped;
						if (!bJumpAirborne && Hit.ImpactNormal.Z>=-.15f && !Hit.bStartPenetrating && TryStepUp(Start, End, Stepped))
						{
							Resolved = Stepped;
							StepHoldMinFeetZ = Stepped.Z - CapsuleHalfHeight;
							StepHoldSeconds = StepHoldDuration;
						}
						else if (Hit.bStartPenetrating)
						{
							if (!bJumpAirborne && Hit.ImpactNormal.Z>=-.15f && TryStepUp(Start, End, Stepped))
							{
								Resolved = Stepped;
								StepHoldMinFeetZ = Stepped.Z - CapsuleHalfHeight;
								StepHoldSeconds = StepHoldDuration;
							}
							else
							{
								Resolved = ACEBodySweep::SlideFromPenetration(*World, Start, End, Hit, Shape, SweepParams);
								BounceNormalUe = Hit.ImpactNormal.GetSafeNormal();
								bHaveBounceNormal = true;
							}
						}
						else
						{
							BounceNormalUe = Hit.ImpactNormal.GetSafeNormal();
							bHaveBounceNormal = true;
							const FVector Normal2D = Hit.Normal.GetSafeNormal2D();
							const FVector HitLoc(Hit.Location.X, Hit.Location.Y, Hit.Location.Z);
							FVector SlideOrigin = HitLoc;
							FVector Remainder(End.X - SlideOrigin.X, End.Y - SlideOrigin.Y, 0.f);
							const float IntoWall = FVector::DotProduct(Remainder, Normal2D);
							if (IntoWall < 0.f)
							{
								Remainder -= Normal2D * IntoWall;
							}
							// Keep airborne Z from End (ballistic); ground slides snap feet.
							FVector SlideEnd = SlideOrigin + Remainder;
							SlideEnd.Z = bJumpAirborne ? End.Z : SlideOrigin.Z;
							if (!bJumpAirborne)
							{
								float SlideFeet = SlideOrigin.Z - CapsuleHalfHeight;
								if (SampleFeetZ(SlideEnd.X, SlideEnd.Y, FeetNow, RampClimbCm, RampDropCm, SlideFeet))
								{
									SlideEnd.Z = FMath::Max(SlideOrigin.Z, SlideFeet + CapsuleHalfHeight);
								}
							}
							FHitResult SlideHit;
							const bool bSlideContact=SweepCapsule(SlideHit, SlideOrigin, SlideEnd);
							if (bSlideContact && IsWallHit(SlideHit))
							{
								if (!bJumpAirborne && SlideHit.ImpactNormal.Z>=-.15f && !SlideHit.bStartPenetrating
									&& TryStepUp(SlideOrigin, SlideEnd, Stepped))
								{
									Resolved = Stepped;
									StepHoldMinFeetZ = Stepped.Z - CapsuleHalfHeight;
									StepHoldSeconds = StepHoldDuration;
								}
								else if (SlideHit.bStartPenetrating)
								{
									Resolved = ACEBodySweep::SlideFromPenetration(*World, SlideOrigin, SlideEnd, SlideHit, Shape, SweepParams);
									BounceNormalUe = SlideHit.ImpactNormal.GetSafeNormal();
									bHaveBounceNormal = true;
								}
								else
								{
									Resolved = ACEBodySweep::SlideGrounded(*World, SlideOrigin, SlideEnd, SlideHit, Shape, SweepParams);
									BounceNormalUe = SlideHit.ImpactNormal.GetSafeNormal();
									bHaveBounceNormal = true;
								}
							}
							else
							{
								Resolved = bSlideContact ? (SlideHit.bStartPenetrating ? SlideOrigin : SlideHit.Location) : SlideEnd;
							}
						}
					}
					else
					{
						Resolved = bPathContact ? (Hit.bStartPenetrating ? Start : Hit.Location) : End;
					}
					PathStart = Resolved;
				}

				}

				Desired.X = Resolved.X;
				Desired.Y = Resolved.Y;
				Desired.Z = Resolved.Z - CapsuleHalfHeight; // feet; capsule center after ground snap
				Pred.SetLocationFromUnreal(Desired, WorldScale);

				// Airborne: retail PhysicsObj.handle_all_collisions bounce (DefaultElasticity 0.05).
				if (bJumpAirborne)
				{
					const float BlockedZ = Resolved.Z - PathGoal.Z;
					if (BlockedZ < -2.f && JumpWorldAceVelocity.Z > 0.f)
					{
						// Ceiling: kill upward component (inelastic on Z into soffit).
						JumpWorldAceVelocity.Z = 0.f;
					}
					if (bHaveBounceNormal)
					{
						// AceVectorToUnreal flips X — convert velocity to UE, bounce, convert back.
						FVector VelUe(-JumpWorldAceVelocity.X, JumpWorldAceVelocity.Y, JumpWorldAceVelocity.Z);
						const FVector N = BounceNormalUe.GetSafeNormal();
						const float CollisionAngle = FVector::DotProduct(VelUe, N);
						if (CollisionAngle < 0.f)
						{
							constexpr float Elasticity = 0.05f; // PhysicsGlobals.DefaultElasticity
							VelUe += N * (-CollisionAngle * (Elasticity + 1.f));
							JumpWorldAceVelocity = FVector(-VelUe.X, VelUe.Y, VelUe.Z);
						}
					}

				}
			}

			// CTransition-lite: CellId from portal / building transit + point_in_cell
			// (not only shallow CellBSP BFS). Feet Z after step-up for containment.
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
				{
					// Use capsule center (not feet) so thin doorway CellBSP doesn't flicker
					// outdoor↔indoor every tick and fight shell collision.
					const FVector TestPos(Desired.X, Desired.Y, Desired.Z + CapsuleHalfHeight);
					const FVector PathStart = Current; // already the capsule center
					uint32 ResolvedCell = static_cast<uint32>(Pred.CellId);
					const uint32 CellBeforeTransit = ResolvedCell;
					if (ACECellTransit::ResolvePathCellId(
							*Dat, ResolvedCell, PathStart, TestPos,
							FMath::Max(8.f, CapsuleRadius), WorldScale, ResolvedCell))
					{
						const bool bWasIndoorCell = (CellBeforeTransit & 0xFFFFu) >= 0x0100u;
						const bool bNowIndoorCell = (ResolvedCell & 0xFFFFu) >= 0x0100u;
						// Jumping into a shop: indoor CellId while airborne dropped land
						// collision before EnvCell PhysicsBSP was cooked → stuck in the floor.
						if (!(bJumpAirborne && !bWasIndoorCell && bNowIndoorCell))
						{
							Pred.CellId = static_cast<int32>(ResolvedCell);
							Pred.SetLocationFromUnreal(Desired, WorldScale);
						}
					}
				}
			}

			Pred.NormalizeOutdoorLandblock();
			const bool bIndoor = (static_cast<uint32>(Pred.CellId) & 0xFFFFu) >= 0x0100u;

			// Snap feet to walkable mesh — skipped while airborne so jump arcs aren't crushed.
			float GroundZ = Desired.Z;
			bool bHaveGround = false;
			const float RampClimbCm = bIndoor
				? (GetStepUpHeightCm() + 12.f)
				: (GetStepUpHeightCm() + 28.f);
			float SnapStepDownCm = GetStepDownHeightCm();
			if (UGameInstance* SnapGI = GetGameInstance())
			{
				if (UACEDatSubsystem* SnapDat = SnapGI->GetSubsystem<UACEDatSubsystem>())
				{
					SnapStepDownCm += SnapDat->GetOutdoorWaterDepthCm(Desired.X, Desired.Y, WorldScale);
				}
			}
			const float FeetZ = Desired.Z;

			if (!bJumpAirborne)
			{
			if (UWorld* World = GetWorld())
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ACEGroundSnap), /*bTraceComplex*/ true, P);
				if (bVR) { TArray<AActor*> AttachedPresentation; P->GetAttachedActors(AttachedPresentation, true, true); Params.AddIgnoredActors(AttachedPresentation); }
				// Match sweep transit: ignore EnvCells outside candidates; ignore land when
				// outdoor land is not in the transit set (indoors / building entry).
				if (UGameInstance* SnapGI = GetGameInstance())
				{
					if (UACEDatSubsystem* SnapDat = SnapGI->GetSubsystem<UACEDatSubsystem>())
					{
						TArray<uint32> SnapTransit;
						bool bSnapHitsInterior = false;
						const float SnapBoundR = FMath::Sqrt(
							FMath::Square(CapsuleRadius) + FMath::Square(CapsuleHalfHeight));
						ACECellTransit::FindCellList(*SnapDat, static_cast<uint32>(Pred.CellId),
							FVector(Desired.X, Desired.Y, FeetZ + CapsuleHalfHeight),
							FMath::Max(8.f, SnapBoundR), WorldScale, SnapTransit, &bSnapHitsInterior);
						TSet<uint32> SnapEnv;
						bool bSnapHasLand = false;
						for (uint32 Id : SnapTransit)
						{
							if (ACECellTransit::IsIndoorCell(Id))
							{
								SnapEnv.Add(Id);
							}
							else
							{
								bSnapHasLand = true;
							}
						}
						if (!bSnapHasLand || bIndoor)
						{
							for (TActorIterator<AACELandblockActor> It(World); It; ++It)
							{
								if (It->TerrainMesh)
								{
									Params.AddIgnoredComponent(
										static_cast<const UPrimitiveComponent*>(It->TerrainMesh.Get()));
								}
							}
							for (TActorIterator<AACETerrainChunkActor> It(World); It; ++It)
							{
								if (It->TerrainMesh)
								{
									Params.AddIgnoredComponent(
										static_cast<const UPrimitiveComponent*>(It->TerrainMesh.Get()));
								}
							}
						}
						ACECellTransit::RestrictRoomCollision(*World, SnapEnv, Params);
						(void)bSnapHitsInterior;
					}
				}
				if (!bIndoor)
					FilterWadingTerrain(*World,GetGameInstance()->GetSubsystem<UACEDatSubsystem>(),
						P->GetActorLocation(),Desired,WorldScale,Params);

                // Use the same lower-sphere support as the movement sweep. A
                // second center-only snap must not undo a successful edge step.
                float SphereGroundZ=FeetZ;
                const bool bHaveSphereGround=ACEBodySweep::FindFootSupport(*World,
                    FVector(Desired.X,Desired.Y,FeetZ),CapsuleRadius,SnapStepDownCm,Params,SphereGroundZ);
				const float TraceTop = FeetZ + RampClimbCm + .5f;
				TArray<FHitResult> Hits;
				auto CollectGroundHits = [&](float X, float Y)
				{
					const FVector S(X, Y, TraceTop);
					// Cap depth to StepDownHeight (+ waterDepth at dest) — unlimited traces
					// caused cliff blink-to-ground.
					const FVector E(X, Y, FeetZ - SnapStepDownCm - 40.f);
					TArray<FHitResult> LocalHits;
					if (!World->LineTraceMultiByChannel(LocalHits, S, E, ECC_Pawn, Params))
					{
						return;
					}
					Hits.Append(LocalHits);
				};
				CollectGroundHits(Desired.X, Desired.Y);
				if (bIndoor)
				{
					const float Ring = 18.f;
					CollectGroundHits(Desired.X + Ring, Desired.Y);
					CollectGroundHits(Desired.X - Ring, Desired.Y);
					CollectGroundHits(Desired.X, Desired.Y + Ring);
					CollectGroundHits(Desired.X, Desired.Y - Ring);
				}
				if (Hits.Num() > 0)
				{
					float BestIndoorZ = -TNumericLimits<float>::Max();
					float BestOutdoorZ = -TNumericLimits<float>::Max();
					bool bHaveIndoor = false;
					bool bHaveOutdoor = false;
					UACEDatSubsystem* SnapDat = nullptr;
					if (UGameInstance* GI = GetGameInstance())
					{
						SnapDat = GI->GetSubsystem<UACEDatSubsystem>();
					}
					for (const FHitResult& Hit : Hits)
					{
						if (!Hit.bBlockingHit || Hit.ImpactNormal.Z < 0.6641741f)
						{
							continue;
						}
						if (Hit.ImpactPoint.Z > FeetZ + RampClimbCm)
						{
							continue;
						}
						if (Hit.ImpactPoint.Z < FeetZ - SnapStepDownCm)
						{
							continue;
						}
						const bool bOutdoor = Hit.Component.IsValid()
							&& Hit.Component->ComponentTags.Contains(FName(TEXT("ACEOutdoorTerrain")));
						const bool bBuildingShell = Hit.Component.IsValid()
							&& Hit.Component->ComponentTags.Contains(FName(TEXT("ACEBuildingShell")));
						(void)bBuildingShell;
						if (bOutdoor)
						{
							float WalkZ = Hit.ImpactPoint.Z;
							if (SnapDat)
							{
								const float DepthCm = SnapDat->GetOutdoorWaterDepthCm(
									Desired.X, Desired.Y, WorldScale);
								if (DepthCm > KINDA_SMALL_NUMBER)
								{
									WalkZ -= DepthCm / FMath::Max(0.15f, Hit.ImpactNormal.Z);
								}
							}
							if (!bHaveOutdoor
								|| FMath::Abs(WalkZ - FeetZ) < FMath::Abs(BestOutdoorZ - FeetZ))
							{
								BestOutdoorZ = WalkZ;
								bHaveOutdoor = true;
							}
						}
						else if (!bHaveIndoor
							|| FMath::Abs(Hit.ImpactPoint.Z - FeetZ) < FMath::Abs(BestIndoorZ - FeetZ))
						{
							// Outdoors: ignore Stab roofs / awnings above StepUp (same as SampleFeetZ).
							if (!bIndoor && Hit.ImpactPoint.Z > FeetZ + GetStepUpHeightCm() + 12.f)
							{
								continue;
							}
							BestIndoorZ = Hit.ImpactPoint.Z;
							bHaveIndoor = true;
						}
					}

					if (!bIndoor && SnapDat)
					{
						// Retail: heightfield support applies everywhere outdoors, including
						// under building footprints. Raised walkables win via the step
						// preference below when clearly above grade.
						float DatZ = FeetZ;
						FVector DatNormal;
						if (SnapDat->SampleOutdoorGroundZ(Desired.X, Desired.Y, WorldScale, DatZ, &DatNormal)
							&& DatNormal.Z >= FloorZ
							&& DatZ <= FeetZ + RampClimbCm
							&& DatZ >= FeetZ - SnapStepDownCm)
						{
							if (!bHaveOutdoor
								|| FMath::Abs(DatZ - FeetZ) < FMath::Abs(BestOutdoorZ - FeetZ))
							{
								BestOutdoorZ = DatZ;
								bHaveOutdoor = true;
							}
						}
					}

					if (!bHaveGround && bIndoor && bHaveIndoor)
					{
						GroundZ = BestIndoorZ;
						bHaveGround = true;
					}
					else if (!bHaveGround && bIndoor)
					{
						if (StepHoldSeconds > 0.f)
						{
							GroundZ = StepHoldMinFeetZ;
							bHaveGround = true;
						}
					}
					else if (!bHaveGround && bHaveIndoor
						&& BestIndoorZ <= FeetZ + GetStepUpHeightCm() + 12.f
						&& BestIndoorZ >= FeetZ - SnapStepDownCm
						&& (!bHaveOutdoor || BestIndoorZ >= BestOutdoorZ + 1.5f))
					{
						// Prefer raised walkables (stairs / stoops / chests) within StepUp —
						// not RampClimbCm (that snapped players onto low tent roofs).
						GroundZ = BestIndoorZ;
						bHaveGround = true;
					}
					else if (!bHaveGround && bHaveOutdoor)
					{
						GroundZ = BestOutdoorZ;
						bHaveGround = true;
					}
					else if (!bHaveGround && bHaveIndoor && bIndoor)
					{
						// Indoor-only fallback. Outdoors never snap onto leftover Stab roofs.
						GroundZ = BestIndoorZ;
						bHaveGround = true;
					}
					if (bHaveGround)
					{
						Pred.Location.Z = GroundZ / WorldScale;
					}
				}
                // A narrow edge can support the lower sphere even when every
                // vertical ray misses. Keep that contact until the body clears it.
                if (bHaveSphereGround && (!bHaveGround || SphereGroundZ > GroundZ))
                {
                    GroundZ = SphereGroundZ;
                    bHaveGround = true;
                    Pred.Location.Z = GroundZ / WorldScale;
                }
                // Rays can see the next tread before the body clears the lip
                // of this one. The final downward adjustment is movement too:
                // stop at the full capsule's first contact instead of pulling
                // its lower sphere through that edge.
                if (bHaveGround && GroundZ > FeetZ)
                {
                    // A support ray/sphere does not test headroom. Never apply
                    // its upward correction through the floor above a stair.
                    const FVector Center(Desired.X,Desired.Y,FeetZ+CapsuleHalfHeight);
                    FHitResult RiseHit;
                    if (ACEBodySweep::Sweep(*World,RiseHit,Center,
                        FVector(Center.X,Center.Y,GroundZ+CapsuleHalfHeight),
                        FCollisionShape::MakeCapsule(CapsuleRadius,CapsuleHalfHeight),Params)
                        && RiseHit.ImpactNormal.Z<.6641741f)
                    {
                        GroundZ=RiseHit.bStartPenetrating ? FeetZ : RiseHit.Location.Z-CapsuleHalfHeight;
                        // The clamped crown may leave the foot inside the ramp.
                        // Recover downhill when upward recovery cannot clear
                        // the ceiling, rather than keeping that wedged pose.
                        FVector Clamped(Desired.X,Desired.Y,GroundZ+CapsuleHalfHeight);
                        const auto BodyShape=FCollisionShape::MakeCapsule(CapsuleRadius,CapsuleHalfHeight);
                        const FVector FootCenter=Clamped-FVector(0,0,CapsuleHalfHeight-CapsuleRadius);
                        FHitResult Contact;
                        // Query the foot separately: a time-zero crown hit can
                        // hide its simultaneous ramp contact in a body sweep.
                        if (World->SweepSingleByChannel(Contact,FootCenter,FootCenter-FVector(0,0,.1f),
                            FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(CapsuleRadius),Params)
                            && Contact.bStartPenetrating && Contact.PenetrationDepth>.05f && Contact.Normal.Z>=.6641741f)
                        {
                            Clamped=ACEBodySweep::Recover(*World,Clamped,Contact.Normal*(Contact.PenetrationDepth+.5f),Contact,BodyShape,Params);
                        }
                        Desired.X=Clamped.X;Desired.Y=Clamped.Y;GroundZ=Clamped.Z-CapsuleHalfHeight;
                        Pred.SetLocationFromUnreal(FVector(Desired.X,Desired.Y,GroundZ),WorldScale);
                    }
                }
                if (bHaveGround && GroundZ < FeetZ)
                {
                    FHitResult SnapHit;
                    const FVector Center(Desired.X, Desired.Y, FeetZ + CapsuleHalfHeight);
                    if (bVR)
                    {
                        // A side contact is not a floor. The old single sweep
                        // treated any initial wall overlap as support forever.
                        const FVector SupportCenter(Center.X,Center.Y,GroundZ+CapsuleHalfHeight);
                        const auto Snap = ACEBodySweep::MoveAirborne(*World, Center,
                            SupportCenter,
                            FCollisionShape::MakeCapsule(CapsuleRadius,CapsuleHalfHeight),Params,true);
                        Desired.X=Snap.Position.X; Desired.Y=Snap.Position.Y;
                        GroundZ=Snap.Position.Z-CapsuleHalfHeight;
                        // An unobstructed sweep reaching the already validated
                        // support is grounded too. Requiring an earlier impact
                        // toggled falling on exact end-of-sweep stair contacts.
                        bHaveGround=Snap.bLanded || Snap.Position.Equals(SupportCenter,.01f);
                        Pred.SetLocationFromUnreal(FVector(Desired.X,Desired.Y,GroundZ),WorldScale);
                    }
                    else if (World->SweepSingleByChannel(SnapHit, Center,
                        FVector(Center.X, Center.Y, GroundZ + CapsuleHalfHeight),
                        FQuat::Identity, ECC_Pawn,
                        FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight), Params))
                    {
                        GroundZ = SnapHit.bStartPenetrating ? FeetZ : SnapHit.Location.Z - CapsuleHalfHeight;
                        Pred.Location.Z = GroundZ / WorldScale;
                    }
                }
			}
			}

			// Outdoor heightfield fallback when complex mesh traces miss.
			// Retail: land support is valid everywhere while outdoor-resident.
			if (!bJumpAirborne && !bHaveGround && !bIndoor)
			{
				if (UGameInstance* GI = GetGameInstance())
				{
					if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
					{
						float SampledZ = Desired.Z;
						FVector DatNormal;
						if (Dat->SampleOutdoorGroundZ(Desired.X, Desired.Y, WorldScale, SampledZ, &DatNormal)
							&& DatNormal.Z >= FloorZ)
						{
							const bool bAcceptOutdoor = SampledZ <= FeetZ + RampClimbCm
								&& SampledZ >= FeetZ - SnapStepDownCm;
							if (bAcceptOutdoor)
							{
								GroundZ = SampledZ;
								bHaveGround = true;
								Pred.Location.Z = SampledZ / WorldScale;
							}
						}
					}
				}
			}

			if (bJumpAirborne)
			{
				// Keep predicted feet Z; convert to capsule center below.
				GroundZ = Desired.Z;
				bHaveGround = true;
			}
			else if (!bHaveGround && StepHoldSeconds > 0.f)
			{
				// Just stepped onto a tread — trust capsule step-up for a beat so line-trace
				// misses don't fall through stairs.
				GroundZ = FMath::Max(Desired.Z, StepHoldMinFeetZ);
				bHaveGround = true;
			}
			else if (!bHaveGround)
			{
				bool bHoldForFloor = false;
				if (bIndoor)
				{
					if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
					{
						if (UACETerrainPresenterComponent* Terrain = GM->FindComponentByClass<UACETerrainPresenterComponent>())
						{
							bHoldForFloor = !Terrain->IsPlayerCellCollisionReady();
						}
					}
				}
				if (bHoldForFloor)
				{
					// Hold only while collision is streaming. A loaded indoor cell
					// can contain stairs, balconies and open drops with no support.
					GroundZ = Desired.Z;
					bHaveGround = true;
				}
				else
				{
					// Walking off an edge is airborne even without a Jump action. Keep
					// integrating and reporting this pose when the movement keys are released.
					bJumpAirborne = true;
					bLocalPredicting = true;
					bHavePredictedPose = true;
					JumpAirborneSeconds = 0.f;
					JumpWorldAceVelocity = Pred.GetAceForwardInAcSpace() * F
						* (F < 0.f ? .65f : 1.f) * Client->GetLocomotionSpeed(bRunning)
						+ Pred.GetAceRightInAcSpace() * R * Client->GetSidestepSpeed(bRunning);
					JumpWorldAceVelocity.Z = -9.8f * DeltaTime;
					GroundZ = Desired.Z - .5f * 9.8f * WorldScale * DeltaTime * DeltaTime;
					bForceMovementResend = true;
					Client->SetForcePositionReporting(true);
					if (auto* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
					{
						App->SetSuppressLocoIdleBlend(true);
						App->SetHeldActionMotion(static_cast<int32>(0x40000015));
					}
				}
			}

			// Falling bypasses the normal ground snap. A streamed terrain body or
			// a one-sided triangle missed on a steep descent must not leave the
			// feet underneath the known heightfield, including the submerged DAT
			// support while the rendered water sheet is ignored. This is a floor bound,
			// not a snap-down: jumps keep their arc until they reach the ground.
			if (!bIndoor)
			{
				if (auto* Dat = GetGameInstance()->GetSubsystem<UACEDatSubsystem>())
				{
					float TerrainZ;
					FVector TerrainNormal;
					if (Dat->SampleOutdoorGroundZ(Desired.X, Desired.Y, WorldScale, TerrainZ, &TerrainNormal) && GroundZ < TerrainZ)
					{
						GroundZ = TerrainZ;
						bHaveGround = true;
						if (bJumpAirborne && TerrainNormal.Z >= FloorZ) FinishJumpLanding();
					}
				}
			}

			if (StepHoldSeconds > 0.f)
			{
				StepHoldSeconds = FMath::Max(0.f, StepHoldSeconds - DeltaTime);
			}

			// AC positions and the visual origin are at the surface, without a UE skin offset.
			constexpr float GroundSkinCm = 0.f;
			Desired.Z = GroundZ + CapsuleHalfHeight + GroundSkinCm;

			if (bVR && !VRRoomDelta.IsNearlyZero()) VR->CompensateRoomScale(Desired - P->GetActorLocation());
			if (const auto* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Movement.Debug")); Debug && Debug->GetInt() && bMoving)
            {
                static double LastMoveLog = 0.;
                const double Now = FPlatformTime::Seconds();
                const FVector Requested(BeforeCollision.X-Current.X,BeforeCollision.Y-Current.Y,0);
                const float Along = FVector::DotProduct(Desired-Current,Requested.GetSafeNormal());
                const bool bDetail = Debug->GetInt() >= 2 && (MovementStartCell != uint32(Pred.CellId)
                    || (Requested.Size() > .5f && Along < Requested.Size()*.9f));
                if (Now - LastMoveLog >= (bDetail ? .1 : 1.))
                {
                    LastMoveLog = Now;
                    UE_LOG(LogTemp, Log, TEXT("ACE MoveStep: dt=%.4f forward=%.3f side=%.3f run=%d vr=%d predicted=%d expected=%.2f beforeCollision=%.2f afterCollision=%.2f airborne=%d"),
                        DeltaTime, F, R, bRunning, bVR, bLocalPredicting, F * Client->GetLocomotionSpeed(bRunning) * GetLocalCreatureScale() * DeltaTime * WorldScale,
                        FVector::Dist2D(BeforeCollision, P->GetActorLocation()), FVector::Dist2D(Desired, P->GetActorLocation()), bJumpAirborne);
                    if (Debug->GetInt() >= 2)
                        UE_LOG(LogTemp, Log, TEXT("ACE MoveContact: cell=%08X->%08X feet=%s requested=%s reached=%s along=%.2f blocker=%s normal=%s penetration=%.3f ledge=%d support=%d"),
                            MovementStartCell, uint32(Pred.CellId), *(Current-FVector(0,0,CapsuleHalfHeight)).ToString(),
                            *Requested.ToString(), *(Desired-Current).ToString(), Along, *MovementBlocker.ToString(),
                            *MovementContactNormal.ToString(), MovementPenetration, bMovementLedge, bHaveGround);
                }
            }
            P->SetActorLocation(Desired, false);
			P->SetActorRotation(Pred.ToUnrealQuat());

			// Desired.Z is capsule center; AutoPos must report feet so WithinUseRadius matches.
			Pred.SetLocationFromUnreal(
				FVector(Desired.X, Desired.Y, Desired.Z - CapsuleHalfHeight - GroundSkinCm),
				WorldScale);
			if (bLocalPredicting || bHavePredictedPose)
			{
				PredictedPose = Pred;
				bHavePredictedPose = true;
				if (bLocalPredicting)
				{
					Client->SetReportedPosition(Pred);
				}
				else if (bHaveLastServerPose)
				{
					// Idle: stop owning prediction once we are close enough to the server anchor.
					const FVector PredUe = Pred.ToUnrealLocation(WorldScale);
					const FVector SrvUe = LastServerPose.ToUnrealLocation(WorldScale);
					if (FVector::Dist(PredUe, SrvUe) < 25.f)
					{
						bHavePredictedPose = false;
					}
				}
			}
		}
	}
	} // Movement substeps
}

float AACEPlayerController::GetLocalCreatureScale() const
{
	if (!Client)
	{
		return 1.f;
	}
	const auto Session = Client->GetSession();
	const auto* Self = Session ? Session->GetWorldObjects().Find(Client->GetPlayerGuid()) : nullptr;
	if (Self && FMath::IsFinite(Self->Scale) && Self->Scale > KINDA_SMALL_NUMBER)
	{
		return Self->Scale;
	}
	return 1.f;
}

float AACEPlayerController::GetCameraScaleCm() const
{
	return WorldScale * GetLocalCreatureScale();
}

void AACEPlayerController::ApplyRetailCameraFov()
{
	APawn* P = GetPawn();
	UCameraComponent* Cam = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
	int32 SizeX = 0, SizeY = 0;
	GetViewportSize(SizeX, SizeY);
	ACECameraRetail::ApplyFovToCamera(Cam, SizeX, SizeY);
	if (Cam) Cam->SetFieldOfView(FMath::Clamp(Cam->FieldOfView * ACERuntimeOptions::Get(TEXT("FieldOfView")), 45.f, 140.f));
}

void AACEPlayerController::ApplyRetailCameraPivot(USpringArmComponent* Boom, UCapsuleComponent* Capsule) const
{
	if (!Boom || !Capsule)
	{
		return;
	}
	const float PivotZ = ACECameraRetail::DefaultPivotZAc * GetCameraScaleCm();
	Boom->SetRelativeLocation(FVector(0.f, 0.f, PivotZ - Capsule->GetScaledCapsuleHalfHeight()));
}

void AACEPlayerController::ApplyCameraOffset(USpringArmComponent* Boom, float OffsetYAc, float OffsetZAc,
	float PitchDegrees, bool bKeepYaw)
{
	if (!Boom)
	{
		return;
	}
	const float ScaleCm = GetCameraScaleCm();
	const float ArmLen = ACECameraRetail::OffsetLengthCm(OffsetYAc, OffsetZAc, ScaleCm);
	Boom->TargetArmLength = ArmLen;
	if (!bCameraInHead)
	{
		UserCameraArmLength = ArmLen;
	}
	Boom->SocketOffset = FVector::ZeroVector;
	Boom->TargetOffset = FVector::ZeroVector;
	FRotator Rotation = Boom->GetRelativeRotation();
	Rotation.Pitch = PitchDegrees;
	if (!bKeepYaw)
	{
		Rotation.Yaw = ACECameraRetail::BoomYawToFaceAcForward;
	}
	Rotation.Roll = 0.f;
	Boom->SetRelativeRotation(Rotation);
}

void AACEPlayerController::SetCameraInHead(USpringArmComponent* Boom, bool bInHead)
{
	if (!Boom)
	{
		return;
	}
	if (bInHead && bCameraLookDown)
	{
		SetCameraLookDown(Boom, false);
	}
	if (bInHead == bCameraInHead)
	{
		return;
	}
	bCameraInHead = bInHead;
	Boom->bDoCollisionTest = !bInHead;
	Boom->bEnableCameraLag = !bInHead;
	Boom->bEnableCameraRotationLag = !bInHead;
	if (APawn* P = GetPawn())
	{
		if (UACECharacterAppearanceComponent* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
		{
			App->SetAppearanceVisible(!bInHead);
		}
	}
	if (bInHead)
	{
		Boom->TargetArmLength = 0.f;
		Boom->SocketOffset = FVector(ACECameraRetail::InHeadForwardCm(GetCameraScaleCm()), 0.f, 0.f);
		Boom->TargetOffset = FVector::ZeroVector;
		FRotator Rotation = Boom->GetRelativeRotation();
		Rotation.Pitch = 0.f;
		Rotation.Roll = 0.f;
		Boom->SetRelativeRotation(Rotation);
	}
	else
	{
		Boom->SocketOffset = FVector::ZeroVector;
		ApplyCameraOffset(Boom,
			ACECameraRetail::LeaveHeadYAc,
			ACECameraRetail::LeaveHeadZAc,
			ACECameraRetail::PitchFromOffsetDegrees(
				ACECameraRetail::LeaveHeadYAc, ACECameraRetail::LeaveHeadZAc),
			true);
	}
}

void AACEPlayerController::SetCameraLookDown(USpringArmComponent* Boom, bool bLookDown)
{
	if (!Boom)
	{
		return;
	}
	if (bLookDown == bCameraLookDown)
	{
		return;
	}
	if (bLookDown)
	{
		LookDownSavedRotation = Boom->GetRelativeRotation();
		LookDownSavedArm = UserCameraArmLength >= 0.f ? UserCameraArmLength : Boom->TargetArmLength;
		bLookDownHasSaved = true;
		if (bCameraInHead)
		{
			SetCameraInHead(Boom, false);
		}
		bCameraLookDown = true;
		ApplyCameraOffset(Boom,
			ACECameraRetail::LookDownOffsetYAc,
			ACECameraRetail::LookDownOffsetZAc,
			ACECameraRetail::LookDownPitchDegrees(),
			true);
		Boom->bEnableCameraLag = false;
		Boom->bEnableCameraRotationLag = false;
	}
	else
	{
		bCameraLookDown = false;
		Boom->bEnableCameraLag = true;
		Boom->bEnableCameraRotationLag = true;
		if (bLookDownHasSaved)
		{
			Boom->SetRelativeRotation(LookDownSavedRotation);
			UserCameraArmLength = LookDownSavedArm;
			Boom->TargetArmLength = LookDownSavedArm;
			Boom->SocketOffset = FVector::ZeroVector;
		}
		bLookDownHasSaved = false;
	}
}

void AACEPlayerController::ResetCameraToRetailDefaults(USpringArmComponent* Boom)
{
	if (!Boom)
	{
		return;
	}
	if (bCameraLookDown)
	{
		bCameraLookDown = false;
		bLookDownHasSaved = false;
	}
	if (bCameraInHead)
	{
		SetCameraInHead(Boom, false);
	}
	Boom->bDoCollisionTest = true;
	Boom->bEnableCameraLag = true;
	Boom->bEnableCameraRotationLag = true;
	Boom->SocketOffset = FVector::ZeroVector;
	ApplyCameraOffset(Boom,
		ACECameraRetail::DefaultOffsetYAc,
		ACECameraRetail::DefaultOffsetZAc,
		ACECameraRetail::DefaultPitchDegrees(),
		false);
	if (APawn* P = GetPawn())
	{
		if (UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>())
		{
			ApplyRetailCameraPivot(Boom, Cap);
		}
	}
	DefaultCameraRotation = Boom->GetRelativeRotation();
	DefaultCameraDistance = Boom->TargetArmLength;
	UserCameraArmLength = DefaultCameraDistance;
	bCameraDefaultsCaptured = true;
}

void AACEPlayerController::SetMouseLookActive(bool bActive)
{
	if (bActive && !IsUseMouseTurning() && !bInstantMouseLookHeld) return;
	if (bMouseLookActive == bActive)
	{
		return;
	}
	bMouseLookActive = bActive;
	if (bActive)
	{
		MouseLookTravelPixels = 0.f;
		bMouseLookUsesCapture = IsUseMouseTurning() || bInstantMouseLookHeld;
		if (bMouseLookUsesCapture)
		{
			// Relative input must remain with the viewport while orbiting; otherwise
			// Slate can consume the second mouse button or stop deltas at window edges.
			FInputModeGameOnly Mode;
			Mode.SetConsumeCaptureMouseDown(false);
			SetInputMode(Mode);
		}
		bShowMouseCursor = false;
		if (HoverTooltipWidget)
		{
			HoverTooltipWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
		int32 SizeX = 0, SizeY = 0;
		GetViewportSize(SizeX, SizeY);
		if (SizeX > 0 && SizeY > 0)
		{
			SetMouseLocation(SizeX / 2, SizeY / 2);
		}
	}
	else
	{
		if (bMouseLookUsesCapture)
		{
			SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
			bMouseLookUsesCapture = false;
		}
		bShowMouseCursor = true;
		EnsureRetailMouseCursor();
		SetMouseLocation(FMath::RoundToInt(MouseLookPressX), FMath::RoundToInt(MouseLookPressY));
	}
}

bool AACEPlayerController::UpdateMouseButtons(bool bRightDown, bool bLeftDown,
    bool bInputFocused, bool bOverUI)
{
    // A focus/option change cancels the gesture. Re-enabling it while RMB is
    // still held must not recapture the viewport or turn the release into a click.
    if (bMouseLookActive && ((!IsUseMouseTurning() && !bInstantMouseLookHeld) || bInputFocused))
    {
        bRightClickEligible = false;
        bLeftOrbitEligible = false;
        SetMouseLookActive(false);
    }
    if (bLeftDown && !bLeftMouseWasDown)
        bLeftOrbitEligible = IsUseMouseTurning() && !bInputFocused && !bOverUI;
    if (bRightDown && !bRightMouseWasDown)
    {
        bRightClickEligible = !bInputFocused && !bOverUI;
        bMouseLookMovedPlayer = false;
        MouseLookTravelPixels = 0.f;
        if (bRightClickEligible && (IsUseMouseTurning() || bInstantMouseLookHeld)) SetMouseLookActive(true);
    }
    if (bInputFocused) bRightClickEligible = false;
    bMouseForwardActive = bMouseLookActive && bRightDown && bLeftDown;
    bMouseLookMovedPlayer |= bMouseForwardActive;
    const bool bIdentify = !bRightDown && bRightMouseWasDown && bRightClickEligible
        && !bMouseLookMovedPlayer && MouseLookTravelPixels <= 4.f
        && (bMouseLookActive || !bOverUI);
    if (!bRightDown && !(bLeftDown && bLeftOrbitEligible))
    {
        SetMouseLookActive(false);
        bRightClickEligible = false;
        bMouseLookMovedPlayer = false;
    }
    bRightMouseWasDown = bRightDown;
    bLeftMouseWasDown = bLeftDown;
    if (!bLeftDown) bLeftOrbitEligible = false;
    return bIdentify;
}

void AACEPlayerController::UpdateMouseLook(float DeltaTime, USpringArmComponent* Boom)
{
    (void)DeltaTime;
    const bool bInputFocused = (DatGameplayBinder && DatGameplayBinder->IsChatEntryFocused())
        || (GameHUDWidget && GameHUDWidget->IsChatEntryFocused()) || ACEInputBindings::IsEditing();
    const bool bWasInstant = bInstantMouseLookHeld;
    bInstantMouseLookHeld = !bInputFocused && ACEInputBindings::Down(this,ACEInputBindings::Action(TEXT("CameraInstantMouseLook")));
    const bool bRightDown = IsInputKeyDown(EKeys::RightMouseButton) || bInstantMouseLookHeld;
    const bool bLeftDown = IsInputKeyDown(EKeys::LeftMouseButton);
    float MX = 0.f, MY = 0.f;
    const bool bHaveMouse = GetMousePosition(MX, MY);
    if ((bRightDown && !bRightMouseWasDown) || (bLeftDown && !bLeftMouseWasDown && !bRightDown))
    {
        MouseLookPressX = MX;
        MouseLookPressY = MY;
    }
    else if (bRightMouseWasDown && !bMouseLookActive && bHaveMouse)
    {
        // With camera controls disabled RMB remains an ordinary inspect click.
        MouseLookTravelPixels = FMath::Max(MouseLookTravelPixels,
            FMath::Abs(MX - MouseLookPressX) + FMath::Abs(MY - MouseLookPressY));
    }
    if (UpdateMouseButtons(bRightDown, bLeftDown,
        bInputFocused, IsMouseOverBlockingUI() || (DatGameplayBinder
            && (DatGameplayBinder->IsInventoryDragActive() || DatGameplayBinder->IsSpellDragActive()
                || DatGameplayBinder->IsScrollbarDragActive()))))
    {
        if(!bWasInstant)IdentifyAtScreenPosition(MouseLookPressX, MouseLookPressY);
    }
    // Do not capture an ordinary left click: it still selects objects. Begin
    // free orbit only after a world press turns into a drag.
    if (!bMouseLookActive && bLeftDown && bLeftOrbitEligible && bHaveMouse
        && FMath::Abs(MX - MouseLookPressX) + FMath::Abs(MY - MouseLookPressY) > 4.f)
        SetMouseLookActive(true);
    if (!bMouseLookActive || !Boom) return;
    bShowMouseCursor = false;
    float DeltaX = 0.f, DeltaY = 0.f;
    GetInputMouseDelta(DeltaX, DeltaY);
    // Unreal's MouseY is up-positive; desktop cursor Y is down-positive.
    ApplyMouseLookDelta(DeltaX, -DeltaY, Boom);
}

void AACEPlayerController::ApplyMouseLookDelta(float DeltaX, float DeltaY, USpringArmComponent* Boom)
{
    if (!Boom || (!IsUseMouseTurning() && !bInstantMouseLookHeld) || !bMouseLookActive) return;
    MouseLookTravelPixels += FMath::Abs(DeltaX) + FMath::Abs(DeltaY);
    FRotator Rotation = Boom->GetRelativeRotation();
    const float DegreesPerPixel = ACECameraSettings::GetMouseDegreesPerPixel();
    if (ACECameraSettings::GetInvertMouseX()) DeltaX=-DeltaX;
    if (ACECameraSettings::GetInvertMouseY()) DeltaY=-DeltaY;
    Rotation.Yaw += DeltaX * DegreesPerPixel;
    Rotation.Pitch = FMath::Clamp(Rotation.Pitch - DeltaY * DegreesPerPixel,
        bCameraInHead ? -53.f : -89.f, bCameraInHead ? 53.f : 20.f);
    Rotation.Roll = 0.f;
    Boom->SetRelativeRotation(Rotation);
}

void AACEPlayerController::UpdateOrbitCamera(float DeltaTime)
{
	APawn* P = GetPawn();
	USpringArmComponent* Boom = P ? P->FindComponentByClass<USpringArmComponent>() : nullptr;
	if (!Boom)
	{
		return;
	}

	Boom->CameraLagSpeed = ACECameraRetail::TranslationLagSpeed * ACERuntimeOptions::Get(TEXT("CameraStiffness"));
	ApplyRetailCameraFov();
	if (UCapsuleComponent* Cap = P ? P->FindComponentByClass<UCapsuleComponent>() : nullptr)
	{
		ApplyRetailCameraPivot(Boom, Cap);
	}

	if (!bCameraDefaultsCaptured)
	{
		ResetCameraToRetailDefaults(Boom);
	}

	UpdateMouseLook(DeltaTime, Boom);

	FRotator Rotation = Boom->GetRelativeRotation();
	const bool bOrbitLeft = ACEInputBindings::Down(this, EKeys::NumPadFour);
	const bool bOrbitRight = ACEInputBindings::Down(this, EKeys::NumPadSix);
	const bool bOrbitUp = ACEInputBindings::Down(this, EKeys::NumPadEight);
	const bool bOrbitDown = ACEInputBindings::Down(this, EKeys::NumPadTwo);
	if (bOrbitLeft)
	{
		Rotation.Yaw += ACECameraRetail::NumpadOrbitDegreesPerSecond * DeltaTime;
	}
	if (bOrbitRight)
	{
		Rotation.Yaw -= ACECameraRetail::NumpadOrbitDegreesPerSecond * DeltaTime;
	}
	if (!bCameraInHead)
	{
		if (bOrbitUp)
		{
			Rotation.Pitch -= ACECameraRetail::NumpadOrbitDegreesPerSecond * DeltaTime;
		}
		if (bOrbitDown)
		{
			Rotation.Pitch += ACECameraRetail::NumpadOrbitDegreesPerSecond * DeltaTime;
		}
		Rotation.Pitch = FMath::ClampAngle(Rotation.Pitch, -89.f, 20.f);
	}
	else
	{
		if (bOrbitUp)
		{
			Rotation.Pitch = FMath::Clamp(Rotation.Pitch - ACECameraRetail::NumpadOrbitDegreesPerSecond * DeltaTime, -53.f, 53.f);
		}
		if (bOrbitDown)
		{
			Rotation.Pitch = FMath::Clamp(Rotation.Pitch + ACECameraRetail::NumpadOrbitDegreesPerSecond * DeltaTime, -53.f, 53.f);
		}
	}
	Rotation.Roll = 0.f;
	Boom->SetRelativeRotation(Rotation);

	const float ScaleCm = GetCameraScaleCm();
	const float MinThirdPerson = ACECameraRetail::CloserMinLengthAc * ScaleCm;
	const float MaxArm = ACECameraRetail::FartherMaxXYAc * ScaleCm;
	const float ZoomFactor = ACECameraRetail::AdjustmentSpeed * ACERuntimeOptions::Get(TEXT("CameraAdjustment")) * ACECameraRetail::ZoomRate * DeltaTime;
	if (UserCameraArmLength < 0.f)
	{
		UserCameraArmLength = Boom->TargetArmLength;
	}
	if (ACEInputBindings::Down(this, EKeys::Subtract))
	{
		if (bCameraLookDown)
		{
			SetCameraLookDown(Boom, false);
		}
		if (bCameraInHead)
		{
			SetCameraInHead(Boom, false);
		}
		else
		{
			UserCameraArmLength *= (1.f + ZoomFactor);
		}
	}
	if (ACEInputBindings::Down(this, EKeys::Add))
	{
		if (bCameraLookDown)
		{
			SetCameraLookDown(Boom, false);
		}
		if (!bCameraInHead)
		{
			UserCameraArmLength *= (1.f - ZoomFactor);
			if (UserCameraArmLength < MinThirdPerson)
			{
				SetCameraInHead(Boom, true);
			}
		}
	}
	if (!bCameraInHead)
	{
		SyncUserCameraArmLength(Boom);
	}

	const bool bZeroDown = ACEInputBindings::Down(this, EKeys::NumPadZero);
	if (bZeroDown && !bNumPadZeroWasDown)
	{
		ResetCameraToRetailDefaults(Boom);
	}
	bNumPadZeroWasDown = bZeroDown;

	const bool bFiveDown = ACEInputBindings::Down(this, EKeys::NumPadFive);
	if (bFiveDown && !bNumPadFiveWasDown)
	{
		SetCameraInHead(Boom, !bCameraInHead);
	}
	bNumPadFiveWasDown = bFiveDown;

	const bool bThreeDown = ACEInputBindings::Down(this, EKeys::NumPadThree);
	if (bThreeDown && !bNumPadThreeWasDown)
	{
		SetCameraLookDown(Boom, !bCameraLookDown);
	}
	bNumPadThreeWasDown = bThreeDown;

	UpdateCombatTargetCameraAssist(DeltaTime, Boom);
}

void AACEPlayerController::SyncUserCameraArmLength(USpringArmComponent* Boom)
{
	if (!Boom || bCameraInHead)
	{
		return;
	}
	if (UserCameraArmLength < 0.f)
	{
		UserCameraArmLength = Boom->TargetArmLength;
	}
	const float ScaleCm = GetCameraScaleCm();
	const float MinThirdPerson = ACECameraRetail::CloserMinLengthAc * ScaleCm;
	const float MaxArm = ACECameraRetail::FartherMaxXYAc * ScaleCm;
	UserCameraArmLength = FMath::Clamp(UserCameraArmLength, MinThirdPerson, MaxArm);
	Boom->TargetArmLength = UserCameraArmLength;
}

bool AACEPlayerController::IsUseMouseTurning() const
{
	return Client && Client->IsCharacterOptionSet(0x31);
}

void AACEPlayerController::ApplyCameraWheelZoom(float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta) || IsMouseOverBlockingUI()
		|| ACEInputBindings::IsEditing()
		|| (DatGameplayBinder && DatGameplayBinder->IsChatEntryFocused())
		|| (GameHUDWidget && GameHUDWidget->IsChatEntryFocused()))
	{
		return;
	}
    const auto Mods=FSlateApplication::Get().GetModifierKeys();
    const FInputChord Chord(WheelDelta>0?EKeys::MouseScrollUp:EKeys::MouseScrollDown,Mods.IsShiftDown(),Mods.IsControlDown(),Mods.IsAltDown(),Mods.IsCommandDown());
    if(ACEInputBindings::Matches(EKeys::Add,Chord))AdjustMouseCameraDistance(FMath::Abs(WheelDelta));
    else if(ACEInputBindings::Matches(EKeys::Subtract,Chord))AdjustMouseCameraDistance(-FMath::Abs(WheelDelta));
}

void AACEPlayerController::AdjustMouseCameraDistance(float WheelDelta)
{
    if (!FMath::IsFinite(WheelDelta) || FMath::IsNearlyZero(WheelDelta)) return;
	APawn* P = GetPawn();
	USpringArmComponent* Boom = P ? P->FindComponentByClass<USpringArmComponent>() : nullptr;
	if (!Boom)
	{
		return;
	}
	const float ScaleCm = GetCameraScaleCm();
	const float MinThirdPerson = ACECameraRetail::CloserMinLengthAc * ScaleCm;
	const float MaxArm = ACECameraRetail::FartherMaxXYAc * ScaleCm;
	const float ZoomFactor = ACECameraRetail::AdjustmentSpeed * ACERuntimeOptions::Get(TEXT("CameraAdjustment")) * ACECameraRetail::ZoomRate * (1.f / 60.f)
		* FMath::Clamp(FMath::Abs(WheelDelta), 0.25f, 4.f);
	if (UserCameraArmLength < 0.f)
	{
		UserCameraArmLength = Boom->TargetArmLength;
	}
	if (WheelDelta > 0.f)
	{
		if (bCameraLookDown)
		{
			SetCameraLookDown(Boom, false);
		}
		if (bCameraInHead)
		{
			SetCameraInHead(Boom, false);
		}
		else
		{
			UserCameraArmLength *= (1.f - ZoomFactor);
			if (UserCameraArmLength < MinThirdPerson)
			{
				SetCameraInHead(Boom, true);
			}
		}
	}
	else
	{
		if (bCameraLookDown)
		{
			SetCameraLookDown(Boom, false);
		}
		if (!bCameraInHead)
		{
			UserCameraArmLength *= (1.f + ZoomFactor);
		}
	}
	SyncUserCameraArmLength(Boom);
}

void AACEPlayerController::UpdateCombatTargetCameraAssist(float DeltaTime, USpringArmComponent* Boom)
{
	if (!Client || !Boom || bMouseLookActive || bCameraInHead || bCameraLookDown)
	{
		return;
	}
	if (!Client->IsCharacterOptionSet(0x07))
	{
		return;
	}
	const FACESelectedObject Sel = Client->GetSelectedObject();
	if (Sel.Guid == 0)
	{
		return;
	}
	FACEWorldObject Target;
	if (!Client->GetWorldObject(Sel.Guid, Target) || !Target.IsAttackable() || !Target.bHasPosition)
	{
		return;
	}
	const FACEPosition PlayerPos = bHavePredictedPose ? PredictedPose : Client->GetPlayerPosition();
	if (!PlayerPos.IsValid())
	{
		return;
	}
	const FVector PlayerUe = PlayerPos.ToUnrealLocation(WorldScale);
	const FVector TargetUe = Target.Position.ToUnrealLocation(WorldScale);
	const FVector ToTarget = (TargetUe - PlayerUe).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}
	const FVector CharForward = PlayerPos.GetAceForwardVector().GetSafeNormal2D();
	const FVector CharRight = FVector::CrossProduct(FVector::UpVector, CharForward).GetSafeNormal();
	const float TargetOrbitYaw = FMath::RadiansToDegrees(FMath::Atan2(
		FVector::DotProduct(ToTarget, CharRight),
		FVector::DotProduct(ToTarget, CharForward)));
	FRotator RelRot = Boom->GetRelativeRotation();
	const float CurrentOrbitYaw = RelRot.Yaw - ACECameraRetail::BoomYawToFaceAcForward;
	const float Error = FMath::FindDeltaAngleDegrees(CurrentOrbitYaw, TargetOrbitYaw);
	if (FMath::Abs(Error) <= 1.5f)
	{
		return;
	}
	RelRot.Yaw += FMath::Clamp(Error, -75.f * DeltaTime, 75.f * DeltaTime);
	RelRot.Roll = 0.f;
	Boom->SetRelativeRotation(RelRot);
}

void AACEPlayerController::MoveForward(float Value) { ForwardAxis = Value; }
void AACEPlayerController::MoveRight(float Value) { RightAxis = Value; }
void AACEPlayerController::Turn(float Value) { TurnAxis = Value; }
void AACEPlayerController::JumpPressed() { BeginJumpCharge(); }
void AACEPlayerController::JumpReleased()
{
	if (bJumpCharging)
	{
		// Live keys at release (standing charge zeros loco axes each tick).
		float AimF = 0.f, AimR = 0.f;
		if (ACEInputBindings::Down(this, EKeys::W)) AimF += 1.f;
		if (ACEInputBindings::Down(this, EKeys::S)) AimF -= 1.f;
		if (ACEInputBindings::Down(this, EKeys::E)) AimR += 1.f;
		if (ACEInputBindings::Down(this, EKeys::Q)) AimR -= 1.f;
		if (bStandingJumpLocked)
		{
			AimF = StandingJumpAimF;
			AimR = StandingJumpAimR;
			if (ACEInputBindings::Down(this, EKeys::W)) AimF = 1.f;
			if (ACEInputBindings::Down(this, EKeys::S)) AimF = -1.f;
			if (ACEInputBindings::Down(this, EKeys::E)) AimR = 1.f;
			if (ACEInputBindings::Down(this, EKeys::Q)) AimR = -1.f;
		}
		ReleaseJump(AimF, AimR);
	}
}
void AACEPlayerController::SprintPressed() { bRunning = false; } // Shift = walk
void AACEPlayerController::SprintReleased() { bRunning = true; }

void AACEPlayerController::BeginJumpCharge()
{
	if (!Client || Client->GetSessionState() != EACESessionState::InWorld || bJumpAirborne || bJumpCharging)
	{
		return;
	}
	bJumpCharging = true;
	bJumpChargeSent = false; // force MoveToState with StandingLongJump
	// Retail: jumping releases a held chat-pose emote just like directional movement.
	if (APawn* P = GetPawn())
	{
		if (UACECharacterAppearanceComponent* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
		{
			App->CancelHeldActionMotion();
		}
	}
	JumpChargeExtent = 0.f;
	StandingJumpAimF = 0.f;
	StandingJumpAimR = 0.f;
	// Capture at charge start — pressing WASD mid-charge must not convert a standing jump
	// into a run jump (retail StandingLongJump ignores loco until leave-ground).
	bStandingJumpLocked = FMath::IsNearlyZero(ForwardAxis) && FMath::IsNearlyZero(RightAxis);
	if (DatGameplayBinder)
	{
		DatGameplayBinder->SetJumpChargeFraction(0.f);
	}
}

void AACEPlayerController::ReleaseJump(float Forward, float Right)
{
	if (!Client || !bJumpCharging)
	{
		bJumpCharging = false;
		bStandingJumpLocked = false;
		StandingJumpAimF = 0.f;
		StandingJumpAimR = 0.f;
		if (DatGameplayBinder)
		{
			DatGameplayBinder->SetJumpChargeFraction(0.f);
		}
		return;
	}
	bJumpCharging = false;
	bJumpChargeSent = false;
	const float Extent = FMath::Clamp(JumpChargeExtent, 0.f, 1.f);
	JumpChargeExtent = 0.f;
	if (DatGameplayBinder)
	{
		DatGameplayBinder->SetJumpChargeFraction(0.f);
	}

	// Standing jump: no aim → vertical only. W/S/Q/E at release with run (no Shift) uses
	// run-scale forward jump — retail standing charge + W is a full forward jump, not walk/shift.
	const bool bStandingAim = bStandingJumpLocked
		&& ((FMath::IsNearlyZero(Forward) && FMath::IsNearlyZero(Right)) || !bRunning);

	// MotionInterp.get_state_velocity — local ACE space (X=strafe, Y=forward, Z=up).
	constexpr float WalkAnimSpeed = 3.12f;
	constexpr float RunAnimSpeed = 4.f;
	FVector LocalVel = FVector::ZeroVector;
	if (!FMath::IsNearlyZero(Right))
	{
		LocalVel.X = Client->GetSidestepSpeed(bStandingAim ? false : bRunning)
			* FMath::Clamp(Right, -1.f, 1.f);
	}
	if (Forward > KINDA_SMALL_NUMBER)
	{
		LocalVel.Y = (bStandingAim || !bRunning)
			? (WalkAnimSpeed * FMath::Clamp(Forward, 0.f, 1.f))
			: (RunAnimSpeed * Client->GetRunRate() * FMath::Clamp(Forward, 0.f, 1.f));
	}
	else if (Forward < -KINDA_SMALL_NUMBER)
	{
		LocalVel.Y = -WalkAnimSpeed * 0.65f * FMath::Clamp(-Forward, 0.f, 1.f);
	}
	const float MaxSpeed = bStandingAim
		? WalkAnimSpeed
		: (RunAnimSpeed * Client->GetRunRate());
	if (LocalVel.SizeSquared2D() > FMath::Square(MaxSpeed) && MaxSpeed > KINDA_SMALL_NUMBER)
	{
		LocalVel = LocalVel.GetSafeNormal2D() * MaxSpeed;
	}
	const float Height = Client->GetJumpHeight(Extent);
	LocalVel.Z = FMath::Sqrt(FMath::Max(0.f, Height * 19.6f));
	JumpLocalAceVelocity = LocalVel;

	FACEPosition LaunchPose;
	if (bHavePredictedPose)
	{
		LaunchPose = PredictedPose;
	}
	else
	{
		LaunchPose = Client->GetPlayerPosition();
	}
	// Freeze world-ACE velocity for the full arc (MotionInterp leave-ground).
	JumpWorldAceVelocity = LaunchPose.IsValid()
		? LaunchPose.GetAcQuat().RotateVector(LocalVel)
		: LocalVel;
	if (LaunchPose.IsValid())
	{
		JumpLaunchAcQuat = LaunchPose.GetAcQuat();
		bHaveJumpLaunchFacing = true;
	}
	else
	{
		bHaveJumpLaunchFacing = false;
	}

	Client->SendJump(Extent, LocalVel);
	// Leave-ground MoveToState: Contact=0 so AutoPos / physics stay airborne-owned.
	Client->SendMovementEx(0.f, 0.f, 0.f, bRunning, false, false);
	Client->SetForcePositionReporting(true);
	Client->FlushAutonomousPosition(false);
	bJumpAirborne = true;
	bJumpAirborneSent = true;
	JumpAirborneSeconds = 0.f;
	bLocalPredicting = true;
	bHavePredictedPose = true;
	if (LaunchPose.IsValid())
	{
		PredictedPose = LaunchPose;
	}

	if (APawn* P = GetPawn())
	{
		if (UACECharacterAppearanceComponent* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
		{
			// Jumpup → Falling without returning to Ready (run blends straight into Jumpup).
			App->SetSuppressLocoIdleBlend(true);
			App->QueueHeldActionAfterCurrent(static_cast<int32>(0x40000015), 0);
			App->PlayActionMotion(static_cast<int32>(0x1000004b), 1.f, 0);
		}
	}
}

void AACEPlayerController::ShowGameHUD()
{
	if (!IsLocalController())
	{
		return;
	}

	DestroyCharacterSelectUI();

	if (bUseDatDrivenHud && Client)
	{
		// Portal transit only hides the HUD. Reinitializing its binder here resets
		// the selected panel, inventory position, and chat buffers.
		if (DatCanvasWidget && DatGameplayBinder)
		{
			if (!IsVRActive() && !DatCanvasWidget->IsInViewport()) DatCanvasWidget->AddToPlayerScreen(9000);
			DatCanvasWidget->SetVisibility(ESlateVisibility::Visible);
			EnsureRetailMouseCursor();
			return;
		}
		if (!DatCanvasWidget)
		{
			DatCanvasWidget = CreateWidget<UACEUICanvasWidget>(this, UACEUICanvasWidget::StaticClass());
			if (DatCanvasWidget)
			{
				DatCanvasWidget->InitializeCanvas(Client->GetUIElementManager());
				DatCanvasWidget->SetResourceResolver(Client->GetUIResourceResolver());
			}
		}
		bool bDatHudReady = false;
		if (DatCanvasWidget && Client->GetUIFlow())
		{
			Client->GetUIFlow()->SetMode(ACEUI::EACEUIFlowMode::Gameplay);
			// SetMode loads on a mode change. When portal prefetch already loaded
			// gameplay, reuse that tree rather than rebuilding it at reveal.
			if (UACEUIElementManager* UiMgr = Client->GetUIElementManager())
			{
				const TSharedPtr<FACEUIElement> Root = UiMgr->GetSyntheticRoot();
				bDatHudReady = Root.IsValid() && Root->Children.Num() > 0;
			}
		}
		if (bDatHudReady)
		{
			DatCanvasWidget->TakeWidget();
			if (!DatGameplayBinder)
			{
				DatGameplayBinder = NewObject<UACEUIGameplayBinder>(this);
			}
			if (DatGameplayBinder)
			{
				DatGameplayBinder->Initialize(Client, Client->GetUIElementManager(), DatCanvasWidget, this);
				DatCanvasWidget->SetGameplayBinder(DatGameplayBinder);
			}
			if (!IsVRActive() && !DatCanvasWidget->IsInViewport())
			{
				DatCanvasWidget->AddToPlayerScreen(9000);
			}
			DatCanvasWidget->SetVisibility(ESlateVisibility::Visible);
			if (GameHUDWidget)
			{
				GameHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
			}
			EnsureRetailMouseCursor();
			return;
		}
	}

	if (!GameHUDWidget)
	{
		TSubclassOf<UACEGameHUDWidget> HudClass = GameHUDWidgetClass;
		if (!HudClass)
		{
			HudClass = UACEGameHUDWidget::StaticClass();
		}
		GameHUDWidget = CreateWidget<UACEGameHUDWidget>(this, HudClass);
	}
	if (GameHUDWidget && !GameHUDWidget->IsInViewport())
	{
		// Below the hover tooltip (10050), above the world.
		GameHUDWidget->AddToPlayerScreen(9000);
	}
	if (GameHUDWidget)
	{
		// Containers pass through; buttons / chat entry stay clickable.
		GameHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (DatCanvasWidget)
	{
		DatCanvasWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	EnsureRetailMouseCursor();
}

void AACEPlayerController::EnableGameplayViewportShadows()
{
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (LP->ViewportClient)
		{
			LP->ViewportClient->EngineShowFlags.SetDynamicShadows(true);
			LP->ViewportClient->EngineShowFlags.SetLighting(true);
		}
	}
}

void AACEPlayerController::EnsureRetailMouseCursor()
{
	if (IsVRActive()) { bShowMouseCursor = false; return; }
	if (!IsLocalController() || bRetailCursorInstalled)
	{
		return;
	}
	FACEDatTextureResolver* Resolver = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			if (!Dat->IsDatReady())
			{
				Dat->BeginBackgroundLoad();
				return;
			}
			Resolver = Dat->GetTextureResolver();
		}
	}
	if (!Resolver)
	{
		return;
	}

	RetailCursorDefaultTex = Resolver->GetOrCreateUiTexture(UACEMouseCursorWidget::DefaultCursorDid);
	RetailCursorInteractableTex = Resolver->GetOrCreateUiTexture(UACEMouseCursorWidget::InteractableCursorDid);
	RetailCursorTargetTex = Resolver->GetOrCreateUiTexture(UACEMouseCursorWidget::TargetCursorDid);
	RetailCursorTargetValidTex = Resolver->GetOrCreateUiTexture(UACEMouseCursorWidget::TargetValidCursorDid);
	RetailCursorTargetInvalidTex = Resolver->GetOrCreateUiTexture(UACEMouseCursorWidget::TargetInvalidCursorDid);
	if (!RetailCursorDefaultTex)
	{
		return;
	}
	if (!MouseCursorWidget)
	{
		MouseCursorWidget = CreateWidget<UACEMouseCursorWidget>(this, UACEMouseCursorWidget::StaticClass());
	}
	if (!MouseCursorWidget)
	{
		return;
	}

	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	CurrentMouseCursor = EMouseCursor::Default;
	SetMouseCursorWidget(EMouseCursor::Default, MouseCursorWidget);
	// Let Slate's cursor query select the native window affordance independently
	// of the world-selection cursor, including while a drag retains capture.
	const TPair<EMouseCursor::Type, uint32> WindowCursors[] = {
		{EMouseCursor::CardinalCross, UACEMouseCursorWidget::MoveCursorDid},
		{EMouseCursor::ResizeUpDown, UACEMouseCursorWidget::ResizeVerticalCursorDid},
		{EMouseCursor::ResizeLeftRight, UACEMouseCursorWidget::ResizeHorizontalCursorDid},
		{EMouseCursor::ResizeSouthEast, UACEMouseCursorWidget::ResizeNWSECursorDid},
		{EMouseCursor::ResizeSouthWest, UACEMouseCursorWidget::ResizeNESWCursorDid}
	};
	for (const auto& Cursor : WindowCursors)
	{
		if (auto* Texture = Resolver->GetOrCreateUiTexture(Cursor.Value))
		{
			auto* Widget = CreateWidget<UACEMouseCursorWidget>(this, UACEMouseCursorWidget::StaticClass());
			if (!Widget) continue;
			Widget->SetCursorTexture(Texture, UACEMouseCursorWidget::WindowHotspot, UACEMouseCursorWidget::WindowHotspot);
			SetMouseCursorWidget(Cursor.Key, Widget);
			RetailWindowCursorWidgets.Add(Widget);
		}
	}
	bRetailCursorInstalled = true;
	bRetailCursorInteractable = false;
	ApplyRetailMouseCursor(false);
}

void AACEPlayerController::ApplyRetailMouseCursor(bool bInteractable, bool bCompatible)
{
	if (!MouseCursorWidget)
	{
		return;
	}
	UTexture2D* Tex = bInteractable && RetailCursorInteractableTex
		? RetailCursorInteractableTex.Get()
		: RetailCursorDefaultTex.Get();
	if (bPendingUseTargeting)
		Tex = !bInteractable ? RetailCursorTargetTex.Get()
			: bCompatible ? RetailCursorTargetValidTex.Get() : RetailCursorTargetInvalidTex.Get();
	if (!Tex)
	{
		return;
	}
	const int32 HotX = bPendingUseTargeting ? UACEMouseCursorWidget::TargetHotspot : bInteractable
		? UACEMouseCursorWidget::InteractableHotspotX
		: UACEMouseCursorWidget::DefaultHotspotX;
	const int32 HotY = bPendingUseTargeting ? UACEMouseCursorWidget::TargetHotspot : bInteractable
		? UACEMouseCursorWidget::InteractableHotspotY
		: UACEMouseCursorWidget::DefaultHotspotY;
	MouseCursorWidget->SetCursorTexture(Tex, HotX, HotY);
	bRetailCursorInteractable = bInteractable;
}

void AACEPlayerController::SetPendingUseTargeting(bool bPending)
{
	bPendingUseTargeting = bPending;
	if (bRetailCursorInstalled)
	{
		ApplyRetailMouseCursor(false);
	}
}

void AACEPlayerController::HideGameHUD()
{
	if (GameHUDWidget)
	{
		// Soft-hide so portal transitions keep chat history / open panels / scroll state.
		GameHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (DatCanvasWidget)
	{
		DatCanvasWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void AACEPlayerController::DestroyGameHUD()
{
	DestroyCharacterSelectUI();
	if (DatGameplayBinder)
	{
		DatGameplayBinder->Shutdown();
		DatGameplayBinder = nullptr;
	}
	if (GameHUDWidget)
	{
		GameHUDWidget->RemoveFromParent();
		GameHUDWidget = nullptr;
	}
	if (DatCanvasWidget)
	{
		DatCanvasWidget->SetGameplayBinder(nullptr);
		DatCanvasWidget->SetCharSelectBinder(nullptr);
		DatCanvasWidget->RemoveFromParent();
		DatCanvasWidget = nullptr;
	}
}

void AACEPlayerController::DestroyCharacterSelectUI()
{
	if(DatCharGenBinder){DatCharGenBinder->Shutdown();DatCharGenBinder=nullptr;}
	if(DatCanvasWidget)DatCanvasWidget->SetCharGenBinder(nullptr);
	bPendingCharSelect = false;
	PendingCharSelectCharacters.Reset();
	PendingCharSelectServerName.Reset();
	if (DatCharSelectBinder)
	{
		DatCharSelectBinder->Shutdown();
		DatCharSelectBinder = nullptr;
	}
	if (DatCanvasWidget)
	{
		DatCanvasWidget->SetCharSelectBinder(nullptr);
		// Tear down the canvas when it only hosted character select — leftover overlay
		// text must not paint over the world / portal tunnel.
		if (!DatGameplayBinder)
		{
			DatCanvasWidget->RemoveFromParent();
			DatCanvasWidget = nullptr;
			if (Client && Client->GetUIElementManager())
			{
				Client->GetUIElementManager()->ClearRoots();
			}
			if (Client && Client->GetUIFlow())
			{
				Client->GetUIFlow()->SetMode(ACEUI::EACEUIFlowMode::None);
			}
		}
	}
}

void AACEPlayerController::CollectLayoutTextureIds(const TSharedPtr<FACEUIElement>& Node, TArray<uint32>& OutIds)
{
	if (!Node.IsValid())
	{
		return;
	}
	if (Node->ImageFileId != 0)
	{
		OutIds.AddUnique(Node->ImageFileId);
	}
	if (Node->AlphaFileId != 0)
	{
		OutIds.AddUnique(Node->AlphaFileId);
	}
	for (const TSharedPtr<FACEUIElement>& Child : Node->Children)
	{
		CollectLayoutTextureIds(Child, OutIds);
	}
}

bool AACEPlayerController::TryPrefetchCharacterSelectAssets()
{
	CharacterSelectLoadError.Reset();
	if (!Client)
	{
		return false;
	}
	UGameInstance* GI = GetGameInstance();
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat || !Dat->IsDatReady())
	{
		if (Dat)
		{
			Dat->BeginBackgroundLoad();
			if (!Dat->GetDatLoadError().IsEmpty())
				CharacterSelectLoadError = FString::Printf(TEXT("Cannot load character selection: %s. Check your retail DAT installation, then click Login to retry."), *Dat->GetDatLoadError());
		}
		return false;
	}

	UACEUIResourceResolver* Res = Client->GetUIResourceResolver();
	if (!Res)
	{
		CharacterSelectLoadError = TEXT("Cannot load character selection: UI resources are unavailable.");
		return false;
	}

	if (Client->GetUIFlow())
	{
		Client->GetUIFlow()->SetMode(ACEUI::EACEUIFlowMode::CharacterManagement);
	}
	if (UACEUILayoutResolver* Layout = Client->GetUILayoutResolver())
	{
		if (!Layout->LoadLayout(ACEUI::LayoutId::CharacterManagement))
		{
			CharacterSelectLoadError = TEXT("Cannot load character selection: packaged UI layouts are missing or unreadable. Extract the complete Windows package, then restart the game.");
			return false;
		}
	}

	// Critical chrome — backdrop + Enter + primary buttons (must not show blank first).
	static const uint32 CriticalDids[] = {
		0x06007576u, // CharacterManagementField JPEG backdrop
		0x06004CB2u, 0x06004CB1u, // Enter idle + alpha
		0x06004C9Eu, 0x06004C9Du, // red large idle + alpha
		0x06004CA3u, 0x06004CA2u, // red huge idle + alpha
		0x06004CC3u, 0x06004CC4u, 0x06004CC5u, 0x06004CC6u, 0x06004CC7u, 0x06004CB8u, // list frame
		0x06005EA7u, 0x06005EA9u, 0x06005EABu, // character slot idle
	};
	int32 Resolved = 0;
	for (uint32 Did : CriticalDids)
	{
		if (Res->ResolveTexture(Did))
		{
			++Resolved;
		}
	}
	// Backdrop + Enter are mandatory; allow a couple of optional chrome misses.
	const bool bBackdrop = Res->ResolveTexture(0x06007576u) != nullptr;
	const bool bEnter = Res->ResolveTexture(0x06004CB2u, 0x06004CB1u) != nullptr
		|| Res->ResolveTexture(0x06004CB2u) != nullptr;
	if (!bBackdrop || !bEnter || Resolved < 8)
	{
		CharacterSelectLoadError = TEXT("Cannot load character selection: required retail DAT images are missing or unreadable. Check client_portal.dat, then click Login to retry.");
		return false;
	}

	// Warm fonts used by char-select overlays so names don't pop in a frame later.
	Res->RenderDatText(0x4000000Bu, TEXT("World"), FColor(216, 175, 91));
	Res->RenderDatText(0x40000009u, TEXT("Character"), FColor::White);
	Res->RenderDatText(0x40000012u, TEXT("ENTER"), FColor::White, true);
	return true;
}

void AACEPlayerController::TickPendingCharacterSelect()
{
	if (!bPendingCharSelect || !IsLocalController())
	{
		return;
	}
	if (!TryPrefetchCharacterSelectAssets())
	{
		if (LoginWidget)
		{
			LoginWidget->SetVisibility(ESlateVisibility::Visible);
			LoginWidget->SetStatus(CharacterSelectLoadError.IsEmpty()
				? TEXT("Loading character select...") : CharacterSelectLoadError);
		}
		if (!CharacterSelectLoadError.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("ACE: %s"), *CharacterSelectLoadError);
			bPendingCharSelect = false;
		}
		return;
	}

	const TArray<FACECharacterInfo> Characters = MoveTemp(PendingCharSelectCharacters);
	const FString ServerName = MoveTemp(PendingCharSelectServerName);
	bPendingCharSelect = false;
	PendingCharSelectCharacters.Reset();
	PendingCharSelectServerName.Reset();
	ShowCharacterSelectUI(Characters, ServerName);
}

bool AACEPlayerController::TryPrefetchGameplayHudAssets()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ACE_PrefetchGameplayHud);
	if (bGameplayUiAssetsReady)
	{
		return true;
	}
	if (!Client || !bUseDatDrivenHud)
	{
		bGameplayUiAssetsReady = true;
		return true;
	}
	UGameInstance* GI = GetGameInstance();
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat || !Dat->IsDatReady())
	{
		return false;
	}
	UACEUIResourceResolver* Res = Client->GetUIResourceResolver();
	if (!Res)
	{
		return false;
	}

	static const uint32 MustHave[] = {
		0x06004CC0u, 0x06004CC1u,
		0x06001938u, 0x0600193Au, 0x0600193Cu,
		0x060011FBu,
	};
	static const uint32 CornerDids[] = {
		0x06004C40u, 0x06004C41u, 0x06004C42u, 0x06004C43u,
	};
	if (GameplayUiPrefetchIds.IsEmpty())
	{
		// Preserve an existing gameplay tree across recalls. Load a new one only
		// once at initial entry, then yield before decoding its texture queue.
		if (!DatGameplayBinder)
		{
			if (auto* Layout = Client->GetUILayoutResolver())
				if (!Layout->PrepareLayout(ACEUI::LayoutId::ClassicGameplay)) return false;
			if (auto* Flow = Client->GetUIFlow()) Flow->SetMode(ACEUI::EACEUIFlowMode::Gameplay);
			else if (auto* Layout = Client->GetUILayoutResolver())
			{
				if (!Layout->LoadLayout(ACEUI::LayoutId::ClassicGameplay)) return false;
			}
		}
		if (auto* Manager = Client->GetUIElementManager())
			CollectLayoutTextureIds(Manager->GetSyntheticRoot(), GameplayUiPrefetchIds);
		GameplayUiLayoutTextureCount = GameplayUiPrefetchIds.Num();
		if (GameplayUiLayoutTextureCount == 0) return false;
		GameplayUiPrefetchIds.Append(MustHave, UE_ARRAY_COUNT(MustHave));
		GameplayUiPrefetchIds.Append(CornerDids, UE_ARRAY_COUNT(CornerDids));
		return false;
	}
	// ResolveTexture can decode and upload a DAT image. A cold gameplay layout
	// contains hundreds: draining it in one tick freezes the VR view at Play.
	const double Deadline = FPlatformTime::Seconds() + .002;
	int32 ThisFrame = 0;
	while (GameplayUiPrefetchIndex < GameplayUiPrefetchIds.Num()
		&& ThisFrame < 8 && (ThisFrame == 0 || FPlatformTime::Seconds() < Deadline))
	{
		const int32 Index = GameplayUiPrefetchIndex++;
		const bool bReady = Res->ResolveTexture(GameplayUiPrefetchIds[Index]) != nullptr;
		if (Index < GameplayUiLayoutTextureCount) GameplayUiResolvedCount += bReady ? 1 : 0;
		else if (Index < GameplayUiLayoutTextureCount + UE_ARRAY_COUNT(MustHave)) bGameplayUiRequiredReady &= bReady;
		++ThisFrame;
	}
	if (GameplayUiPrefetchIndex < GameplayUiPrefetchIds.Num()) return false;
	bGameplayUiAssetsReady = bGameplayUiRequiredReady
		&& GameplayUiResolvedCount >= FMath::Max(12, GameplayUiLayoutTextureCount * 3 / 4);
	if (bGameplayUiAssetsReady)
	{
		UE_LOG(LogTemp, Log, TEXT("ACE: gameplay UI textures ready (%d/%d layout DIDs)"), GameplayUiResolvedCount, GameplayUiLayoutTextureCount);
	}
	else
	{
		GameplayUiPrefetchIndex = GameplayUiResolvedCount = 0;
		bGameplayUiRequiredReady = true;
	}
	return bGameplayUiAssetsReady;
}

void AACEPlayerController::ShowCharacterSelectUI(const TArray<FACECharacterInfo>& Characters, const FString& ServerName)
{
	if(DatCharGenBinder){DatCharGenBinder->Shutdown();DatCharGenBinder=nullptr;}
	if(DatCanvasWidget)DatCanvasWidget->SetCharGenBinder(nullptr);
	if (!IsLocalController() || !Client)
	{
		return;
	}

	// Character select must stay light — no landblock / entity streaming until Enter World.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			Dat->SetWorldStreamingAllowed(false);
		}
	}

	// Credentials UI hands off to retail character select — remove so its text can't leak.
	if (LoginWidget)
	{
		LoginWidget->SetVisibility(ESlateVisibility::Collapsed);
		LoginWidget->RemoveFromParent();
		LoginWidget = nullptr;
	}

	if (DatGameplayBinder)
	{
		DatGameplayBinder->Shutdown();
		DatGameplayBinder = nullptr;
	}
	if (DatCanvasWidget)
	{
		DatCanvasWidget->SetGameplayBinder(nullptr);
	}

	if (!DatCanvasWidget)
	{
		DatCanvasWidget = CreateWidget<UACEUICanvasWidget>(this, UACEUICanvasWidget::StaticClass());
		if (DatCanvasWidget)
		{
			DatCanvasWidget->InitializeCanvas(Client->GetUIElementManager());
			DatCanvasWidget->SetResourceResolver(Client->GetUIResourceResolver());
		}
	}
	if (!DatCanvasWidget)
	{
		ShowLoginUI();
		if (LoginWidget)
		{
			LoginWidget->SetVisibility(ESlateVisibility::Visible);
		}
		return;
	}

	bool bLayoutReady = false;
	if (Client->GetUIFlow())
	{
		Client->GetUIFlow()->SetMode(ACEUI::EACEUIFlowMode::CharacterManagement);
	}
	if (UACEUILayoutResolver* Resolver = Client->GetUILayoutResolver())
	{
		bLayoutReady = Resolver->LoadLayout(ACEUI::LayoutId::CharacterManagement);
	}
	if (UACEUIElementManager* UiMgr = Client->GetUIElementManager())
	{
		const TSharedPtr<FACEUIElement> Root = UiMgr->GetSyntheticRoot();
		bLayoutReady = bLayoutReady && Root.IsValid() && Root->Children.Num() > 0;
	}

	if (!bLayoutReady)
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE: CharacterManagement layout failed — falling back to LoginWidget list"));
		ShowLoginUI();
		if (LoginWidget)
		{
			LoginWidget->SetVisibility(ESlateVisibility::Visible);
		}
		return;
	}

	if (!DatCharSelectBinder)
	{
		DatCharSelectBinder = NewObject<UACEUICharSelectBinder>(this);
	}
	// Construct the layer before the binder installs character names and buttons.
	DatCanvasWidget->TakeWidget();
	if (DatCharSelectBinder)
	{
		DatCharSelectBinder->Initialize(Client, Client->GetUIElementManager(), DatCanvasWidget, this,
			Characters, ServerName);
		DatCanvasWidget->SetCharSelectBinder(DatCharSelectBinder);
	}
	if (!IsVRActive() && !DatCanvasWidget->IsInViewport())
	{
		DatCanvasWidget->AddToPlayerScreen(9000);
	}
	DatCanvasWidget->SetVisibility(ESlateVisibility::Visible);
	bShowMouseCursor = true;
	EnsureRetailMouseCursor();
	SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
}

void AACEPlayerController::HandleEnteredWorld(int32 PlayerGuid, const FACEPosition& SpawnPosition)
{
	if (UWorld* World = GetWorld())
	{
		const bool bOnWcMap = World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0;
		if (!bOnWcMap && !ACEPlaySessionRedirect::GPendingWcTravelMapAfterLogin.IsEmpty())
		{
			ACEPlaySessionRedirect::FPendingEnteredWorldAfterTravel Pending;
			Pending.PlayerGuid = PlayerGuid;
			Pending.SpawnPosition = SpawnPosition;
			ACEPlaySessionRedirect::GPendingEnteredWorldAfterTravel = Pending;

			const FString TravelMap = ACEPlaySessionRedirect::GPendingWcTravelMapAfterLogin;
			ACEPlaySessionRedirect::GPendingWcTravelMapAfterLogin.Empty();
			UE_LOG(LogTemp, Warning, TEXT("ACE: Enter World — ClientTravel to WC map '%s'"), *TravelMap);
			ClientTravel(TravelMap, TRAVEL_Absolute);
			return;
		}
	}

	HideLoginUI();
	DestroyCharacterSelectUI();
	// HUD stays off for the entire portal tunnel — shown only after FinishWorldTransition.
	if (!bUseEnterWorldLoadScreen)
	{
		ShowGameHUD();
		EnableGameplayViewportShadows();
	}
	else
	{
		HideGameHUD();
	}
	bLocalPredicting = false;
	bHavePredictedPose = false;
	LastAppliedTeleportSeq = Client ? static_cast<uint16>(Client->GetTeleportSeq()) : 0;
	LastAppliedForcePositionSeq = Client && Client->GetSession() ? Client->GetSession()->GetForcePositionSeq() : 0;
	bHaveLastServerPose = SpawnPosition.IsValid();
	if (bHaveLastServerPose)
	{
		LastServerPose = SpawnPosition;
	}
	if (bSyncPossessedPawn)
	{
		if (APawn* P = GetPawn())
		{
			FVector SpawnLoc = SpawnPosition.ToUnrealLocation(WorldScale);
			float CapsuleHalfHeight = 88.f;
			if (UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>())
			{
				CapsuleHalfHeight = Cap->GetScaledCapsuleHalfHeight();
			}
			// ACE Z is feet; capsule root is center.
			SpawnLoc.Z += CapsuleHalfHeight;
			P->SetActorLocation(SpawnLoc);
			P->SetActorRotation(SpawnPosition.ToUnrealQuat());
			ApplyLocalPawnPkCollision();
		}
	}

	if (bUseEnterWorldLoadScreen)
	{
		bPendingEnterWorldTransition = false;
		if (!bEnterWorldLoading) BeginEnterWorldLoadScreen();
	}
	else if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			// Prefetch portal-space art only once a character is entering the world.
			Dat->PrefetchPortalSpaceSetup(0x02000306, WorldScale);
			Dat->SetWorldStreamingAllowed(true);
		}
	}

	// Defer DAT appearance off the ObjectCreate packet drain — building the local Setup mesh
	// synchronously here used to freeze the game thread before landblocks could stream.
	PendingSelfAppearanceGuid = PlayerGuid;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AACEPlayerController::ApplyPendingSelfAppearance);
	}
}

void AACEPlayerController::PreparePortalScreen()
{
	if (!GetWorld()) return;
	if (!IsValid(LoadingScreenActor))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		LoadingScreenActor = GetWorld()->SpawnActor<AACELoadingScreenActor>(
			AACELoadingScreenActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (!LoadingScreenActor) return;
	LoadingScreenActor->WorldScale = WorldScale;
	LoadingScreenActor->TryBuildMesh();
	LoadingScreenActor->HideAllVisuals();
	LoadingScreenActor->SetActorTickEnabled(false);
}

void AACEPlayerController::BeginWorldTransition(const TCHAR* Reason)
{
	PortalWorldRevealElapsed = -1.f;
	if (PlayerCameraManager) PlayerCameraManager->UnlockFOV();
	bWorldEntryRecoveryAttempted = false;
	bAwaitingRecoveryDestination = false;
	WorldEntryRecoveryElapsed = 0.f;
	WorldEntryFailureReason.Reset();
	bEnterWorldLoading = true;
	bWorldRevealActive = false;
	bGameplayUiAssetsReady = false;
	GameplayUiPrefetchIds.Reset();
	GameplayUiPrefetchIndex = GameplayUiResolvedCount = GameplayUiLayoutTextureCount = 0;
	bGameplayUiRequiredReady = true;
	bDestinationStreamingStarted = false;
	PortalRetirement = EPortalRetirement::ClearScene;
	PortalFirstVisibleFrame = MAX_uint64;
	EnterWorldLoadElapsed = 0.f;
	TunnelVisibleElapsed = 0.f;
	bLoggedTransitionTimeout = false;
	bPortalExitNotified = false;
	bLocalPredicting = false;
	ClearServerMoveTo();

	// Keep ViewTarget on the single LoadingScreenActor — pawn auto-camera steals the view
	// and makes the tunnel look frozen / absent.
	bAutoManageActiveCameraTarget = false;
	PortalSavedControlRotation = GetControlRotation();
	bHavePortalSavedControlRotation = true;

	// No gameplay UI / cursor while the player is inside the portal object.
	HideGameHUD();
	bShowMouseCursor = false;
	if (HoverTooltipWidget)
	{
		HoverTooltipWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	bHoverTooltipVisible = false;

	UWorld* World = GetWorld();

	// Camera follows the tunnel actor — suppress rain sheets / weather PES over that view.
	// Retail SmartBox::Hide: the 3D world (including GameSky) is not drawn in portal space.
	SetSkyWeatherEnabled(false);
	if (World)
	{
		for (TActorIterator<AACESkyDomeActor> It(World); It; ++It)
		{
			if (AACESkyDomeActor* Sky = *It)
			{
				Sky->SetActorHiddenInGame(true);
				Sky->SetWeatherEnabled(false);
			}
		}
	}

	if (Client)
	{
		Client->StopMovement();
		Client->SetForcePositionReporting(false);
	}
	ForwardSent = -999.f;
	RightSent = -999.f;
	TurnSent = -999.f;
	bWasMoving = false;
	bForceMovementResend = true;

	if (APawn* P = GetPawn())
	{
		P->SetActorHiddenInGame(true);
		P->SetActorEnableCollision(false);
		if (UACEScriptComponent* Scripts = P->FindComponentByClass<UACEScriptComponent>())
		{
			Scripts->StopAllEffects();
			Scripts->SetPhysicsHidden(true);
		}
		if (Client)
		{
			const FACEPosition Pose = Client->GetPlayerPosition();
			if (Pose.IsValid())
			{
				FVector Feet = Pose.ToUnrealLocation(WorldScale);
				if (UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>())
				{
					Feet.Z += Cap->GetScaledCapsuleHalfHeight();
				}
				P->SetActorLocation(Feet);
			}
		}
	}

	if (!World)
	{
		return;
	}

	// Stream destination landblocks under the portal at a lightweight budget so the
	// tunnel animation stays smooth. Full-rate streaming resumes at reveal.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			Dat->SetInPortalSpace(true);
			Dat->SetLightweightStreaming(true);
			// Submit a tracked portal frame before installing destination meshes.
			Dat->SetWorldStreamingAllowed(false);
			if (!Dat->IsDatReady())
			{
				Dat->BeginBackgroundLoad();
			}
		}
	}

	// Reset landblock fail marks / force a fresh ring sync around the session player cell.
	if (AGameModeBase* GM = World->GetAuthGameMode())
	{
		if (UACETerrainPresenterComponent* Terrain = GM->FindComponentByClass<UACETerrainPresenterComponent>())
		{
			// Retail CellManager keeps prefetching destination landblocks while the
			// portal viewport is showing. LoadRadius=0 made cell-collision wait 45s.
			Terrain->RestoreProceduralStreamingAfterLogin(5, 6);
		}
	}

	// Exactly one tunnel actor — never spawn a second while one exists.
	if (LoadingScreenActor && !IsValid(LoadingScreenActor))
	{
		LoadingScreenActor = nullptr;
	}
	if (!LoadingScreenActor && World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		LoadingScreenActor = World->SpawnActor<AACELoadingScreenActor>(
			AACELoadingScreenActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		UE_LOG(LogTemp, Log, TEXT("ACE: spawned portal-space tunnel actor"));
	}
	if (LoadingScreenActor)
	{
		LoadingScreenActor->WorldScale = WorldScale;
		LoadingScreenActor->SetActorTickEnabled(true);
		LoadingScreenActor->BeginTunnel();
		if (!IsVRActive() && LoadingScreenActor->Camera)
		{
			SetViewTarget(LoadingScreenActor);
		}
	}

	// Retail portal audio: UI_EnterPortal (SoundTable 0x2000004B type 0x6A) once.
	// Animation 0x030005AC SoundTweaked @ frame 2 starts the portalspace ambient.
	AActor* SoundHost = LoadingScreenActor ? static_cast<AActor*>(LoadingScreenActor.Get()) : GetPawn();
	if (SoundHost)
	{
		UACEScriptComponent* Scripts = SoundHost->FindComponentByClass<UACEScriptComponent>();
		if (!Scripts)
		{
			Scripts = NewObject<UACEScriptComponent>(SoundHost, TEXT("ACEPortalScripts"));
			Scripts->RegisterComponent();
		}
		if (Scripts)
		{
			Scripts->StopAllSounds();
			constexpr int32 UiSoundTable = 0x2000004B;
			constexpr int32 UiEnterPortal = 0x6A;
			Scripts->PlayCenteredSoundFromTable(UiSoundTable, UiEnterPortal, 1.f);
		}
	}

	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	UE_LOG(LogTemp, Log, TEXT("ACE: portal-space transition started (%s)"), Reason ? Reason : TEXT("unknown"));
}

bool AACEPlayerController::FindWorldEntryPlacement(FVector& OutCapsuleCenter) const
{
	const APawn* EntryPawn = GetPawn();
	const UCapsuleComponent* Capsule = EntryPawn ? EntryPawn->FindComponentByClass<UCapsuleComponent>() : nullptr;
	if (!Client || !GetWorld() || !Capsule || !Client->GetPlayerPosition().IsValid()) return false;
	const FACEPosition Pose = Client->GetPlayerPosition();
	const FVector Feet = Pose.ToUnrealLocation(WorldScale);
	UACEDatSubsystem* Dat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	// Match the capsule foot sphere: a narrower floor probe places the full
	// body inside sloping ground even when its center foot point is supported.
	const float FloorRadius = Radius;
	const float StepUp = FMath::Max(2.f, GetStepUpHeightCm());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ACEWorldEntryPlacement), false, EntryPawn);
	const auto Shape = FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - .5f), FMath::Max(Radius, HalfHeight - .5f));
	auto TryPosition = [&](FVector CandidateFeet)
	{
		// Outdoor portal coordinates can predate the current land height (the
		// Khayyaban arrival is below its DAT terrain). Seed from that height,
		// then still require cooked floor support and a clear full body.
		float TerrainZ = 0.f;
		if (!ACECellTransit::IsIndoorCell(Pose.CellId) && Dat
			&& Dat->SampleOutdoorGroundZ(CandidateFeet.X, CandidateFeet.Y, WorldScale, TerrainZ))
			CandidateFeet.Z = FMath::Max(CandidateFeet.Z, double(TerrainZ));
		FHitResult Floor;
		// A free capsule over missing terrain is not a valid placement. Search at most
		// retail's four-unit placement radius down, and only accept walkable support.
		if (!GetWorld()->SweepSingleByChannel(Floor,
			CandidateFeet + FVector(0, 0, StepUp + FloorRadius),
			CandidateFeet + FVector(0, 0, FloorRadius - 4.f * WorldScale), FQuat::Identity,
			ECC_Pawn, FCollisionShape::MakeSphere(FloorRadius), Params)
			|| Floor.bStartPenetrating || Floor.ImpactNormal.Z < .5f) return false;
		CandidateFeet.Z = Floor.Location.Z - FloorRadius + .5f;
		const FVector Center = CandidateFeet + FVector(0, 0, HalfHeight);
		if (GetWorld()->OverlapBlockingTestByChannel(Center, FQuat::Identity, ECC_Pawn, Shape, Params)) return false;
		// Keep a local placement in the server's destination cell. Adjacent room
		// physics may not be active yet, so its apparent free space cannot qualify.
		if (ACECellTransit::IsIndoorCell(Pose.CellId) && (!Dat
			|| !ACECellTransit::SphereIntersectsEnvCell(*Dat, Pose.CellId,
				CandidateFeet + FVector(0, 0, Radius), 0.f, WorldScale))) return false;
		OutCapsuleCenter = Center;
		return true;
	};
	if (TryPosition(Feet)) return true;
	// Transition::FindPlacementPos searches outward up to four AC units. Validate
	// floor and full body for each alternative instead of accepting empty space.
	const float Limit = 4.f * WorldScale;
	const float RingStep = FMath::Max(Radius, 48.f);
	const int32 Rings = FMath::Max(1, FMath::CeilToInt(Limit / RingStep));
	for (int32 Ring = 1; Ring <= Rings; ++Ring)
	{
		const float Distance = Limit * Ring / Rings;
		const int32 Steps = FMath::Max(8, FMath::CeilToInt(2.f * PI * Distance / RingStep));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float Angle = 2.f * PI * Step / Steps;
			if (TryPosition(Feet + FVector(FMath::Sin(Angle) * Distance, FMath::Cos(Angle) * Distance, 0))) return true;
		}
	}
	return false;
}

void AACEPlayerController::ResetWorldEntryDestination()
{
	// F751 precedes the position. A recall can replace the destination while the
	// tunnel is already visible; the old LoginComplete must not acknowledge it.
	bEnterWorldLoading = true;
	bWorldRevealActive = false;
	bPortalExitNotified = false;
	bWaitingForServerUnhide = true;
	bAwaitingRecoveryDestination = false;
	WorldEntryRecoveryElapsed = 0.f;
	EnterWorldLoadElapsed = 0.f;
	bLoggedTransitionTimeout = false;
	bLocalPredicting = false;
	bHavePredictedPose = false;
	ClearServerMoveTo();
	// Arrival has not completed. Resume movement reporting only at the safe cut,
	// otherwise AutoPos can overwrite the server's destination during portal space.
	if (Client) Client->SetForcePositionReporting(false);
}

void AACEPlayerController::FailWorldEntry(const FString& Reason)
{
	WorldEntryFailureReason = Reason;
	bEnterWorldLoading = false;
	bWorldRevealActive = false;
	bAwaitingRecoveryDestination = false;
	if (LoadingScreenActor)
	{
		LoadingScreenActor->Destroy();
		LoadingScreenActor = nullptr;
	}
	UE_LOG(LogTemp, Warning, TEXT("ACE: world entry failed: %s"), *Reason);
	if (Client && Client->GetSession()) Client->GetSession()->Disconnect();
	// Disconnect broadcasts state/log messages synchronously; show the useful reason last.
	if (LoginWidget) LoginWidget->SetStatus(Reason);
}

bool AACEPlayerController::TickWorldEntryRecovery(float DeltaTime, const FString& BlockingGate)
{
	if (UACEDatSubsystem* Dat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr)
	{
		if (!Dat->GetDatLoadError().IsEmpty())
		{
			// A recall cannot repair missing world files. Leave the tunnel with
			// the actionable error instead of waiting for nonexistent collision.
			FailWorldEntry(Dat->GetDatLoadError());
			return true;
		}
	}
	if (bAwaitingRecoveryDestination)
	{
		WorldEntryRecoveryElapsed += DeltaTime;
		if (WorldEntryRecoveryElapsed >= WorldTransitionMaxSeconds)
		{
			FailWorldEntry(TEXT("The spawn location is blocked and the server did not complete lifestone recall. Check that this character is attuned and allowed to recall, then reconnect."));
		}
		return true;
	}
	const bool bUnsafe = BlockingGate == TEXT("cell-collision") || BlockingGate == TEXT("wc-terrain")
		|| BlockingGate == TEXT("spawn-placement");
	if (!bUnsafe || EnterWorldLoadElapsed < WorldTransitionMaxSeconds) return false;
	if (bWorldEntryRecoveryAttempted)
	{
		FailWorldEntry(TEXT("The lifestone destination could not load a safe spawn. Reconnect after checking the client DAT files. The character was not placed in missing or blocked terrain."));
		return true;
	}
	if (!Client || Client->GetSessionState() != EACESessionState::InWorld) return false;
	bWorldEntryRecoveryAttempted = true;
	bAwaitingRecoveryDestination = true;
	WorldEntryRecoveryElapsed = 0.f;
	// Existing TeleToLifestone (0x63): server owns Sanctuary and recall eligibility.
	// Do not spoof an outbound position, force an unsafe reveal, or spam recalls.
	Client->SendTeleToLifestone();
	UE_LOG(LogTemp, Warning, TEXT("ACE: unsafe world entry (%s) -- requesting one lifestone recall"), *BlockingGate);
	return true;
}

bool AACEPlayerController::IsWorldTransitionReady(FString* OutBlockingGate, bool bIgnoreServerUnhide) const
{
	auto Block = [&](const TCHAR* Gate) -> bool
	{
		if (OutBlockingGate)
		{
			*OutBlockingGate = Gate;
		}
		return false;
	};
	if (bAwaitingRecoveryDestination) return Block(TEXT("lifestone-recall"));
	if (!Client || Client->GetSessionState() != EACESessionState::InWorld)
		return Block(TEXT("server-entry"));

	// Prefer the textured tunnel, but permit destination loading if its art fails.
	const bool bTunnelMeshReady = LoadingScreenActor && LoadingScreenActor->bMeshReady;
	const bool bTunnelMeshTimedOut = EnterWorldLoadElapsed >= FMath::Min(WorldTransitionMaxSeconds, 4.f);
	if (!bTunnelMeshReady && !bTunnelMeshTimedOut)
	{
		return Block(TEXT("portal-tunnel-mesh"));
	}
	const float VisibleElapsed = bTunnelMeshReady ? TunnelVisibleElapsed : EnterWorldLoadElapsed;
	if (VisibleElapsed < EnterWorldLoadMinSeconds)
	{
		return Block(TEXT("min-tunnel-time"));
	}

	UGameInstance* GI = GetGameInstance();
	UACEDatSubsystem* Dat = GI ? GI->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (!Dat || !Dat->IsDatReady())
	{
		return Block(TEXT("dat-ready"));
	}
	if (!Dat->IsWorldStreamingAllowed())
	{
		return Block(TEXT("streaming-held"));
	}


	FACEPosition AreaCenter;
	if (Client)
	{
		AreaCenter = Client->GetPlayerPosition();
	}

	if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
	{
		const bool bWcBakedTerrain = GetWorld() && GetWorld()->WorldComposition
			&& GetWorld()->WorldComposition->GetTilesList().Num() > 0;
		if (!bWcBakedTerrain)
		{
			if (UACETerrainPresenterComponent* Terrain = GM->FindComponentByClass<UACETerrainPresenterComponent>())
			{
				// Walkable center cell required; outdoor also waits on LoadRadius ring below.
				if (!Terrain->IsPlayerCellCollisionReady())
				{
					return Block(TEXT("cell-collision"));
				}
				FVector Placement;
				if (!FindWorldEntryPlacement(Placement)) return Block(TEXT("spawn-placement"));
				const bool bIndoor = (static_cast<uint32>(AreaCenter.CellId) & 0xFFFFu) >= 0x0100u;
				if (bIndoor && !bIgnoreServerUnhide)
				{
					// Portal exit: current cell + portal/VisibleCells neighborhood only.
					// Waiting on IsEnvCellSyncComplete loads every EnvCell in the 1-ring LB
					// shell (Town Network / marketplace = thousands) and never leaves portal space.
					if (!Terrain->IsPlayerIndoorNeighborhoodReady())
					{
						return Block(TEXT("envcell-neighborhood"));
					}
				}
				if (!bIgnoreServerUnhide && Terrain->NeedsExteriorTerrain(static_cast<uint32>(AreaCenter.CellId)))
				{
					// Surface interiors also need the landscape visible through their exits.
					if (!Terrain->IsLoadRadiusTerrainReady()) return Block(TEXT("visible-terrain"));
					if (!Terrain->IsLoadRadiusBuildingsReadyForRadius(Terrain->GetFullDetailRadius()))
					{
						return Block(TEXT("load-radius-terrain"));
					}
				}
			}
		}
		else if (UWorld* World = GetWorld())
		{
			FVector SpawnLoc = FVector::ZeroVector;
			if (Client)
			{
				const FACEPosition Pose = Client->GetPlayerPosition();
				if (Pose.IsValid())
				{
					SpawnLoc = Pose.ToUnrealLocation(WorldScale);
				}
			}
			if (!ACEWcTerrainRuntime::IsTerrainReadyAround(World, SpawnLoc, 4))
			{
				return Block(TEXT("wc-terrain"));
			}
		}
	}

	FVector Placement;
	if (!FindWorldEntryPlacement(Placement)) return Block(TEXT("spawn-placement"));
	// Allow nearby objects to arrive while distant terrain and HUD textures warm.
	// Physical support is mandatory; visual readiness still gates the reveal.
	if (bIgnoreServerUnhide) return true;

	// Deadlock guard: ACE only clears Hidden after LoginComplete (OnTeleportComplete).
	// Callers that still need to wait for unhide leave bIgnoreServerUnhide=false; arrival
	// notification uses true so we can send LoginComplete while waiting for SetState.
	if (!bIgnoreServerUnhide && bWaitingForServerUnhide && EnterWorldLoadElapsed < WorldTransitionMaxSeconds)
	{
		return Block(TEXT("server-unhide"));
	}

	// Gameplay HUD textures must be warm before reveal — otherwise chrome pops in after exit.
	if (!bGameplayUiAssetsReady)
	{
		return Block(TEXT("gameplay-ui-assets"));
	}

	// Nearby weenie meshes + outdoor flora beside the pawn. Requires LoginComplete first
	// for ObjectCreate, so skip when bIgnoreServerUnhide — that path only sends arrival
	// once terrain/buildings are ready so appearances can cook under the tunnel.
	if (!bIgnoreServerUnhide)
	{
		if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
		{
			const bool bWcBakedTerrain = GetWorld() && GetWorld()->WorldComposition
				&& GetWorld()->WorldComposition->GetTilesList().Num() > 0;
			if (!bWcBakedTerrain)
			{
				if (UACETerrainPresenterComponent* Terrain = GM->FindComponentByClass<UACETerrainPresenterComponent>())
				{
					if (Terrain->NeedsExteriorTerrain(static_cast<uint32>(AreaCenter.CellId))
						&& !Terrain->IsLoadRadiusSceneryCompleteForRadius(1))
					{
						return Block(TEXT("near-scenery"));
					}
				}
			}
			if (UACEWorldPresenterComponent* WorldPres = GM->FindComponentByClass<UACEWorldPresenterComponent>())
			{
				if (!WorldPres->IsAreaAppearancesReady(AreaCenter, 48.f))
				{
					return Block(TEXT("area-appearances"));
				}
			}
		}
	}

	if (OutBlockingGate)
	{
		OutBlockingGate->Reset();
	}
	return true;
}

void AACEPlayerController::SetSkyWeatherEnabled(bool bEnabled)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AACESkyDomeActor> It(World); It; ++It)
	{
		if (AACESkyDomeActor* Sky = *It)
		{
			Sky->SetWeatherEnabled(bEnabled);
		}
	}
}

void AACEPlayerController::NotifyPortalArrivalIfNeeded()
{
	if (bPortalExitNotified || !Client)
	{
		return;
	}
	Client->NotifyExitedPortalSpace();
	bPortalExitNotified = true;
	UE_LOG(LogTemp, Log, TEXT("ACE: LoginComplete sent (exited portal space)"));
}

bool AACEPlayerController::RetirePortalScene()
{
	auto* Dat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
	if (PortalRetirement == EPortalRetirement::ClearScene)
	{
		PortalRetirementStarted = FPlatformTime::Seconds();
		if (Dat) Dat->LogLandTextureMemory(TEXT("before retirement"));
		if (auto* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
			if (auto* Terrain = GM->FindComponentByClass<UACETerrainPresenterComponent>())
				Terrain->ResetStreamingForTransition();
		if (Dat) Dat->TrimMemoryCaches();
		PortalRetirement = EPortalRetirement::WaitPreviousGC;
	}
	if (PortalRetirement == EPortalRetirement::WaitPreviousGC)
	{
		// A collection already in progress may have marked the old scene reachable.
		// Finish it before requesting a fresh pass over the now-destroyed actors.
		if (IsIncrementalReachabilityAnalysisPending() || IsIncrementalPurgePending()) return false;
		PortalRetirementLastGC = GetLastGCTime();
		GEngine->ForceGarbageCollection(false);
		PortalRetirement = EPortalRetirement::WaitCollection;
		return false;
	}
	if (PortalRetirement == EPortalRetirement::WaitCollection)
	{
		// Engine-end-of-frame GC destroys unreachable resources with its normal time
		// budget. Continue rendering tracked portal frames throughout the purge.
		if (GetLastGCTime() <= PortalRetirementLastGC || IsIncrementalReachabilityAnalysisPending()
			|| IsIncrementalPurgePending()) return false;
		PortalRetirementFence = MakeShared<FRenderCommandFence>();
		PortalRetirementFence->BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
		PortalRetirement = EPortalRetirement::WaitRender;
		return false;
	}
	if (PortalRetirement == EPortalRetirement::WaitRender)
	{
		if (!PortalRetirementFence->IsFenceComplete()) return false;
		PortalRetirementFence.Reset();
		PortalRetirement = EPortalRetirement::Complete;
		if (Dat) Dat->LogLandTextureMemory(TEXT("after retirement"));
		UE_LOG(LogTemp, Log, TEXT("ACE: portal resource retirement completed in %.3fs"),
			FPlatformTime::Seconds() - PortalRetirementStarted);
	}
	return PortalRetirement == EPortalRetirement::Complete;
}

void AACEPlayerController::TickWorldTransition()
{
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	EnterWorldLoadElapsed += Dt;
	SetSkyWeatherEnabled(false);
	if ((!Client || Client->GetSessionState() != EACESessionState::InWorld)
		&& EnterWorldLoadElapsed >= WorldTransitionMaxSeconds)
	{
		FailWorldEntry(TEXT("The server did not finish entering the world. Please reconnect."));
		return;
	}

	if (LoadingScreenActor)
	{
		LoadingScreenActor->TryBuildMesh();
		// Keep the camera locked on the tunnel the entire wait — never peek at the world.
		if (!IsVRActive() && LoadingScreenActor->Camera
			&& (LoadingScreenActor->GetPhase() == EACEPortalTransitionPhase::Tunnel
				|| LoadingScreenActor->GetPhase() == EACEPortalTransitionPhase::Reveal)
			&& GetViewTarget() != LoadingScreenActor)
		{
			SetViewTarget(LoadingScreenActor);
		}
	}

	// Start budgeted destination work once the tunnel is on-camera. Keeping lightweight
	// mode until reveal would prevent the very appearances the readiness gate waits on.
	const bool bTunnelMeshReady = LoadingScreenActor && LoadingScreenActor->bMeshReady;
	const bool bTunnelMeshTimedOut = EnterWorldLoadElapsed >= FMath::Min(WorldTransitionMaxSeconds, 4.f);
	if (bTunnelMeshReady || bTunnelMeshTimedOut)
	{
		if (PortalFirstVisibleFrame == MAX_uint64) PortalFirstVisibleFrame = GFrameCounter;
		if (bTunnelMeshReady) TunnelVisibleElapsed += Dt;
		// Submit a tracked portal frame before the first expensive destination batch.
		if (GFrameCounter <= PortalFirstVisibleFrame) return;
		if (!Client || Client->GetSessionState() != EACESessionState::InWorld)
		{
			return;
		}
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				if (!bDestinationStreamingStarted)
				{
					// Keep submitting tracked portal frames while cooked parents load.
					if (!Dat->PrepareRuntimeMaterials()) return;
					if (!RetirePortalScene()) return;
					// Destination scenery / weenie meshes cook under the tunnel so reveal
					// isn't a pop-in. Keep the camera on the portal until those gates pass.
					bDestinationStreamingStarted = true;
					Dat->SetLightweightStreaming(false);
					Dat->SetWorldStreamingAllowed(true);
					if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
					{
						if (UACEWorldPresenterComponent* WorldPres =
							GM->FindComponentByClass<UACEWorldPresenterComponent>())
						{
							WorldPres->BeginPostPortalAppearanceBoost(16, 40.f);
						}
						if (UACETerrainPresenterComponent* Terrain =
							GM->FindComponentByClass<UACETerrainPresenterComponent>())
						{
							Terrain->RestoreProceduralStreamingAfterLogin(5, 6);
							Terrain->KickLandblockLoginBurst();
						}
					}
					UE_LOG(LogTemp, Log, TEXT("ACE: portal destination streaming started (tunnel ready=%d)"), bTunnelMeshReady);
				}
			}
		}
		if (UWorld* World = GetWorld())
		{
			if (World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0 && Client)
			{
				const FACEPosition Pose = Client->GetPlayerPosition();
				if (Pose.IsValid())
				{
					const FVector SpawnLoc = Pose.ToUnrealLocation(WorldScale);
					ACEWcTerrainRuntime::ShowWcLandscapesAfterLogin(World);
						ACEWcTerrainRuntime::RequestTerrainAround(
						World, SpawnLoc, /*RingTiles*/ 4);
				}
			}
		}
	}
	else if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			if (!Dat->IsDatReady())
			{
				Dat->BeginBackgroundLoad();
			}
		}
	}

	if (!Client || Client->GetSessionState() != EACESessionState::InWorld) return;
	if (PendingSelfAppearanceGuid != 0 && bDestinationStreamingStarted)
	{
		ApplyPendingSelfAppearance();
	}

	// Prefetch ClassicGameplay textures under the tunnel so ShowGameHUD does not pop art later.
	TryPrefetchGameplayHudAssets();

	if (bWorldRevealActive)
	{
		APawn* P = GetPawn();
		const FVector PawnLoc = P ? P->GetActorLocation() : FVector::ZeroVector;
		const bool bRevealDone = LoadingScreenActor
			? LoadingScreenActor->TickReveal(Dt, PawnLoc)
			: true;
		if (bRevealDone)
		{
			FinishWorldTransition();
		}
		return;
	}

	FString BlockingGate;
	const bool bReady = IsWorldTransitionReady(&BlockingGate);
	if (TickWorldEntryRecovery(Dt, BlockingGate)) return;
	if (!bReady)
	{
		static double LastPortalGateLog = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastPortalGateLog >= 2.0)
		{
			LastPortalGateLog = Now;
			const FACEPosition Destination = Client ? Client->GetPlayerPosition() : FACEPosition();
			UE_LOG(LogTemp, Warning,
				TEXT("ACE: portal-space holding (%.1fs, %s, cell=0x%08X pos=%s)"),
				EnterWorldLoadElapsed, BlockingGate.IsEmpty() ? TEXT("unknown") : *BlockingGate,
				static_cast<uint32>(Destination.CellId), *Destination.Location.ToString());
		}
	}
	// Once destination content is ready, tell the server we exited portal space so it can
	// clear Teleporting / Hidden. Waiting for unhide without LoginComplete is a deadlock.
	if (!bPortalExitNotified && IsWorldTransitionReady(nullptr, /*bIgnoreServerUnhide*/ true))
	{
		NotifyPortalArrivalIfNeeded();
	}
	if (!bReady && EnterWorldLoadElapsed >= WorldTransitionMaxSeconds)
	{
		// Force past server-unhide and indoor neighborhood stalls (Town Network).
		// Outdoor landblocks / nearby flora stay held — force-reveal is the pop-in.
		if (BlockingGate == TEXT("server-unhide"))
		{
			if (!bLoggedTransitionTimeout)
			{
				bLoggedTransitionTimeout = true;
				UE_LOG(LogTemp, Warning, TEXT("ACE: portal-space server-unhide stalled after %.1fs — clearing wait"),
					EnterWorldLoadElapsed);
			}
			bWaitingForServerUnhide = false;
		}
		else if (BlockingGate == TEXT("envcell-visual")
			|| BlockingGate == TEXT("envcell-neighborhood")
			|| BlockingGate == TEXT("envcell-landblock")
			|| BlockingGate == TEXT("area-appearances"))
		{
			if (!bLoggedTransitionTimeout)
			{
				bLoggedTransitionTimeout = true;
				UE_LOG(LogTemp, Warning,
					TEXT("ACE: portal-space indoor/appearance ready stalled after %.1fs (%s) — forcing LoginComplete + reveal"),
					EnterWorldLoadElapsed, *BlockingGate);
			}
			NotifyPortalArrivalIfNeeded();
			BeginWorldReveal();
			return;
		}
		else if (!bLoggedTransitionTimeout)
		{
			bLoggedTransitionTimeout = true;
			UE_LOG(LogTemp, Warning, TEXT("ACE: portal-space still loading after %.1fs (blocked by %s) — holding"),
				EnterWorldLoadElapsed, BlockingGate.IsEmpty() ? TEXT("unknown") : *BlockingGate);
		}
	}

	if (IsWorldTransitionReady(&BlockingGate))
	{
		BeginWorldReveal();
	}
}

void AACEPlayerController::BeginWorldReveal()
{
	if (bWorldRevealActive)
	{
		return;
	}
	// Even the visual-load timeout must pass the physical placement check.
	FVector SafePlacement;
	if (!FindWorldEntryPlacement(SafePlacement)) return;

	NotifyPortalArrivalIfNeeded();

	bEnterWorldLoading = false;
	bWorldRevealActive = true;
	bWaitingForServerUnhide = false;
	EnableGameplayViewportShadows();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			Dat->SetLightweightStreaming(false);
			Dat->SetWorldStreamingAllowed(true);
		}
	}

	if (APawn* P = GetPawn())
	{
		// Re-snap to the server feet pose now that floor collision should exist.
		if (Client)
		{
			const FACEPosition Pose = Client->GetPlayerPosition();
			FVector Feet = SafePlacement;
			float CapsuleHalfHeight = 88.f;
			if (UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>())
			{
				CapsuleHalfHeight = Cap->GetScaledCapsuleHalfHeight();
			}
			P->SetActorLocation(Feet);
			P->SetActorRotation(Pose.ToUnrealQuat());
			PredictedPose = Pose;
			PredictedPose.SetLocationFromUnreal(Feet - FVector(0, 0, CapsuleHalfHeight), WorldScale);
			Client->SetReportedPosition(PredictedPose);
			bHavePredictedPose = true;
		}
		// Keep the pawn hidden until FinishWorldTransition cuts to the destination.
		P->SetActorHiddenInGame(true);
		P->SetActorEnableCollision(false);
		if (UACEScriptComponent* Scripts = P->FindComponentByClass<UACEScriptComponent>())
		{
			// Preserve the Hidden particle cloud through the camera cut.
			Scripts->SetPhysicsHidden(true);
		}
		if (!IsVRActive() && LoadingScreenActor && LoadingScreenActor->Camera)
		{
			SetViewTarget(LoadingScreenActor);
		}
	}

	if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
	{
		if (UACEWorldPresenterComponent* WorldPres = GM->FindComponentByClass<UACEWorldPresenterComponent>())
		{
			WorldPres->RehomePlayerChildren(Client ? Client->GetPlayerGuid() : 0);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("ACE: portal-space reveal started (%.2fs in tunnel)"), EnterWorldLoadElapsed);

	// If the bubble actor failed to spawn, finish immediately.
	if (!LoadingScreenActor)
	{
		FinishWorldTransition();
	}
}

void AACEPlayerController::InvalidateMovementAfterTeleport()
{
	// Re-seed MotionStance from CombatMode before StopMovement so MoveToState does not
	// echo NonCombat and wipe HandCombat/Magic after portal arrival.
	if (Client && Client->GetPlayerVitals().bValid)
	{
		const int32 Mode = Client->GetPlayerVitals().CombatMode;
		const TArray<FACEWorldObject> Equipped = Client->GetEquippedItems();
		const uint32 Stance = ACECombatStance::ResolveForCombatMode(Equipped, static_cast<uint32>(Mode));
		if (TSharedPtr<FACESession> Session = Client->GetSession())
		{
			Session->SetCurrentStance(Stance);
		}
		if (APawn* P = GetPawn())
		{
			if (UACECharacterAppearanceComponent* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
			{
				App->SetPreferredStyle(static_cast<int32>(Stance));
			}
		}
	}
	if (Client)
	{
		Client->StopMovement();
		Client->SetForcePositionReporting(true);
	}
	ForwardSent = -999.f;
	RightSent = -999.f;
	TurnSent = -999.f;
	bWasMoving = false;
	bRunningSent = !bRunning;
	bForceMovementResend = true;
	bLocalPredicting = false;
	bJumpCharging = false;
	bJumpAirborne = false;
	bStandingJumpLocked = false;
	JumpWorldAceVelocity = FVector::ZeroVector;
	JumpLocalAceVelocity = FVector::ZeroVector;
	ClearServerMoveTo();
}

void AACEPlayerController::FinishWorldTransition()
{
	FVector SafePlacement;
	if (!FindWorldEntryPlacement(SafePlacement))
	{
		bEnterWorldLoading = true;
		bWorldRevealActive = false;
		if (LoadingScreenActor) LoadingScreenActor->BeginTunnel();
		return;
	}
	bEnterWorldLoading = false;
	bWorldRevealActive = false;
	bWaitingForServerUnhide = false;
	bAutoManageActiveCameraTarget = true;
	SetIgnoreMoveInput(false);
	SetIgnoreLookInput(false);
	ApplyInWorldInputMode();

	if (APawn* P = GetPawn())
	{
		if (UACEScriptComponent* Scripts = P->FindComponentByClass<UACEScriptComponent>())
		{
			Scripts->StopAllSounds();
		}

		// Exit whoosh from the tunnel host (still in-portal; only ForceCentered SFX allowed).
		AActor* SoundHost = LoadingScreenActor ? static_cast<AActor*>(LoadingScreenActor.Get()) : static_cast<AActor*>(P);
		if (UACEScriptComponent* PortalScripts = SoundHost->FindComponentByClass<UACEScriptComponent>())
		{
			PortalScripts->StopAllSounds();
			constexpr int32 UiSoundTable = 0x2000004B;
			constexpr int32 UiExitPortal = 0x6B;
			PortalScripts->PlayCenteredSoundFromTable(UiSoundTable, UiExitPortal, 1.f);
		}

		// Seat pawn + ControlRotation from server pose BEFORE view-target switch so the first
		// gameplay frame isn't a tumbled camera cut that then snaps upright.
		if (Client)
		{
			const FACEPosition Pose = Client->GetPlayerPosition();
			if (Pose.IsValid())
			{
				FVector Feet = SafePlacement;
				float CapsuleHalfHeight = 88.f;
				if (UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>())
				{
					CapsuleHalfHeight = Cap->GetScaledCapsuleHalfHeight();
				}
				P->SetActorLocation(Feet);
				P->SetActorRotation(Pose.ToUnrealQuat());
				PredictedPose = Pose;
				PredictedPose.SetLocationFromUnreal(Feet - FVector(0, 0, CapsuleHalfHeight), WorldScale);
				Client->SetReportedPosition(PredictedPose);
				bHavePredictedPose = true;
			}
		}
		P->SetActorHiddenInGame(false);
		P->SetActorEnableCollision(true);
		if (UACECharacterAppearanceComponent* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
		{
			App->SetAppearanceVisible(true);
		}
		if (UACEScriptComponent* Scripts = P->FindComponentByClass<UACEScriptComponent>())
		{
			// Retail UnHide stops new bubbles and fades the mesh in over 0.75s.
			Scripts->SetPhysicsHidden(false);
		}
		if (AGameModeBase* DropGM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
		{
			if (UACEWorldPresenterComponent* WorldPres = DropGM->FindComponentByClass<UACEWorldPresenterComponent>())
			{
				WorldPres->DropPendingOneShotEffectsForGuid(Client ? Client->GetPlayerGuid() : 0);
			}
		}
		{
			FRotator Face = P->GetActorRotation();
			Face.Pitch = 0.f;
			Face.Roll = 0.f;
			SetControlRotation(Face);
			bHavePortalSavedControlRotation = false;
		}
		if (PlayerCameraManager)
		{
			PlayerCameraManager->SetGameCameraCutThisFrame();
		}
		SetViewTarget(P);
		if (!IsVRActive() && LoadingScreenActor && LoadingScreenActor->IsRevealFinished() && PlayerCameraManager)
		{
			PortalWorldRevealElapsed = 0.f;
			PlayerCameraManager->SetFOV(179.f);
		}
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			Dat->SetInPortalSpace(false);
			Dat->SetLightweightStreaming(false);
			Dat->SetWorldStreamingAllowed(true);
		}
	}
	// Resume deferred streaming. The presenter's first world-camera update restores
	// actor visibility even when the destination finished loading inside the tunnel.
	if (UWorld* RevealWorld = GetWorld())
	{
		if (AGameModeBase* GM = RevealWorld->GetAuthGameMode())
		{
			if (UACETerrainPresenterComponent* Terrain =
				GM->FindComponentByClass<UACETerrainPresenterComponent>())
			{
				Terrain->KickLandblockLoginBurst();
			}
		}
	}

	if (LoadingScreenActor)
	{
		LoadingScreenActor->HideAllVisuals();
		LoadingScreenActor->Destroy();
		LoadingScreenActor = nullptr;
	}

	bool bIndoorDest = false;
	if (Client)
	{
		const FACEPosition Pose = Client->GetPlayerPosition();
		bIndoorDest = (static_cast<uint32>(Pose.CellId) & 0xFFFFu) >= 0x0100u;
	}
	if (UWorld* World = GetWorld())
	{
		SetSkyWeatherEnabled(!bIndoorDest);
		for (TActorIterator<AACESkyDomeActor> It(World); It; ++It)
		{
			if (AACESkyDomeActor* Sky = *It)
			{
				Sky->SetActorHiddenInGame(false);
				Sky->InvalidateAndRebuild();
			}
		}
		if (World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0)
		{
			WcLastStreamTileX = INT32_MIN;
			WcLastStreamTileY = INT32_MIN;
			ACEWcTerrainRuntime::ShowWcLandscapesAfterLogin(World);
			ACEWcTerrainRuntime::MaintainLandscapePresentation(World);
		}
	}

	EnableGameplayViewportShadows();

	ShowGameHUD();
	bShowMouseCursor = true;
	EnsureRetailMouseCursor();

	// Re-seed prediction from the post-teleport server pose and force a fresh MoveToState
	// so held WASD keys don't silently reuse pre-portal movement sequences.
	NotifyPortalArrivalIfNeeded();
	if (Client && bHaveLastServerPose)
	{
		PredictedPose = LastServerPose;
		bHavePredictedPose = true;
		Client->SetReportedPosition(LastServerPose);
		Client->FlushAutonomousPosition(true);
	}
	InvalidateMovementAfterTeleport();
	if (DatGameplayBinder && Client && Client->GetPlayerVitals().bValid)
	{
		// Stance alone is not enough — CombatMode drives spell hotbar visibility / 1–9 cast.
		DatGameplayBinder->SyncCombatModeFromServer(Client->GetPlayerVitals().CombatMode);
	}
	if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
	{
		if (UACEWorldPresenterComponent* WorldPres = GM->FindComponentByClass<UACEWorldPresenterComponent>())
		{
			WorldPres->BeginPostPortalAppearanceBoost(4, 2.f);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("ACE: portal-space transition finished (%.2fs)"), EnterWorldLoadElapsed);
}

void AACEPlayerController::ApplyPendingSelfAppearance()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ACE_ApplySelfAppearance);
	// The PlayerCreate timer can run before a tracked portal frame is submitted.
	// Leave appearance work queued for TickWorldTransition in that case.
	if (bEnterWorldLoading && !bDestinationStreamingStarted) return;
	if (!bApplyLocalAppearance || !Client || PendingSelfAppearanceGuid == 0)
	{
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			if (!Dat->IsDatReady())
			{
				Dat->BeginBackgroundLoad();
				if (UWorld* World = GetWorld())
				{
					FTimerHandle Handle;
					World->GetTimerManager().SetTimer(Handle, this, &AACEPlayerController::ApplyPendingSelfAppearance, 0.25f, false);
				}
				return;
			}
		}
	}

	FACEWorldObject Self;
	if (Client->GetWorldObject(PendingSelfAppearanceGuid, Self))
	{
		Self.bIsSelf = true;
		UACECharacterAppearanceComponent* Comp = EnsurePawnAppearance();
		const bool bHadAppearance = Comp && Comp->HasAppearance();
		const uint64 AppearanceBefore = Comp ? Comp->GetAppliedAppearanceHash() : 0;
		ApplyPlayerCapsuleFromSetup(Self.SetupId);
		if (Comp)
		{
			Comp->ApplyWorldObject(Self, WorldScale);
			// Query-only Visibility so the local player can be hovered / selected / identified.
			Comp->ConfigurePartCollision(/*bBlocking*/ false, /*bQueryVisibilityOnly*/ false);
			ApplyPlayerCapsuleFromSetup(Self.SetupId);
		}
		const bool bAppearanceNoOp = bHadAppearance && Comp
			&& Comp->GetAppliedAppearanceHash() == AppearanceBefore;
		bool bScriptsJustCreated = false;
		if (APawn* P = GetPawn())
		{
			// The pawn is a plain engine pawn — the FX runtime must be created here, or every
			// PlayEffect/PlayScript for the local player guid queues forever (no cast particles).
			UACEScriptComponent* Scripts = P->FindComponentByClass<UACEScriptComponent>();
			if (!Scripts)
			{
				Scripts = NewObject<UACEScriptComponent>(P, TEXT("ACEScripts"));
				Scripts->RegisterComponent();
				Scripts->InitializeFromObject(Self, WorldScale);
				Scripts->NotifyAppearanceReady(/*bIsDoor*/ false);
				bScriptsJustCreated = true;
				UE_LOG(LogTemp, Warning,
					TEXT("ACE: local ScriptComponent ready peTable=0x%08X setup=0x%08X"),
					Self.PhysicsEffectTableId, Self.SetupId);
			}
			else if (Scripts->GetSetupId() != static_cast<uint32>(Self.SetupId) || !Scripts->IsEffectReady())
			{
				Scripts->InitializeFromObject(Self, WorldScale);
				Scripts->NotifyAppearanceReady(/*bIsDoor*/ false);
				bScriptsJustCreated = true;
			}
		}
		if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
		{
			if (UACEWorldPresenterComponent* WorldPres = GM->FindComponentByClass<UACEWorldPresenterComponent>())
			{
				if (!bAppearanceNoOp)
				{
					WorldPres->RehomePlayerChildren(PendingSelfAppearanceGuid);
				}
				if (bScriptsJustCreated || !bAppearanceNoOp)
				{
					WorldPres->DropPendingOneShotEffectsForGuid(PendingSelfAppearanceGuid);
					WorldPres->FlushPendingEffectsForGuid(PendingSelfAppearanceGuid);
				}
			}
		}
	}
	PendingSelfAppearanceGuid = 0;

	// Keep pawn hidden until portal-space finishes so clothing/weapon pop-in isn't visible.
	if (bEnterWorldLoading || bWorldRevealActive)
	{
		if (APawn* P = GetPawn())
		{
			P->SetActorHiddenInGame(true);
			P->SetActorEnableCollision(false);
		}
	}
	else if (APawn* P = GetPawn())
	{
		P->SetActorHiddenInGame(false);
		P->SetActorEnableCollision(true);
		if (UACECharacterAppearanceComponent* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
		{
			App->SetAppearanceVisible(true);
		}
		if (UACEScriptComponent* Scripts = P->FindComponentByClass<UACEScriptComponent>())
		{
			Scripts->RestoreMeshVisuals();
		}
	}
}

void AACEPlayerController::SoftReconcilePredictedTowardServer(const FACEPosition& Server, float DeltaTime, bool bAllowHeading)
{
	if (!Server.IsValid())
	{
		return;
	}
	if (!bHavePredictedPose)
	{
		PredictedPose = Server;
		bHavePredictedPose = true;
		return;
	}

	FVector PredUe = PredictedPose.ToUnrealLocation(WorldScale);
	const FVector SrvUe = Server.ToUnrealLocation(WorldScale);
	// A settled server pose also owns altitude. The old XY-only deadzone could
	// leave a stationary client above the bottom of a cliff indefinitely.
	if (!bLocalPredicting && !bJumpAirborne && FMath::Abs(SrvUe.Z-PredUe.Z) > 25.f)
	{
		PredUe.Z = SrvUe.Z;
		PredictedPose.SetLocationFromUnreal(PredUe, WorldScale);
	}
	const float Err2D = FVector::Dist2D(PredUe, SrvUe);

	// Catastrophic desync (lag spike / admin move) — snap XY, keep local Z for ground.
	if (Err2D >= ServerSnapErrorCm)
	{
		FVector Snapped = SrvUe;
		Snapped.Z = PredUe.Z;
		PredictedPose.SetLocationFromUnreal(Snapped, WorldScale);
		if (bAllowHeading)
		{
			PredictedPose.RotationW = Server.RotationW;
			PredictedPose.RotationXYZ = Server.RotationXYZ;
		}
		return;
	}

	// Small lag is expected while predicting — don't fight the local run.
	const float Deadzone = FMath::Max(0.f, ServerReconcileDeadzoneCm);
	if (Err2D <= Deadzone)
	{
		return;
	}

	const float Rate = FMath::Max(0.f, ServerReconcileRate);
	const float Alpha = (Rate <= KINDA_SMALL_NUMBER)
		? 0.f
		: (1.f - FMath::Exp(-Rate * FMath::Max(DeltaTime, 0.f)));
	if (Alpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Only correct the excess beyond the deadzone so motion stays continuous.
	const FVector Delta2D(SrvUe.X - PredUe.X, SrvUe.Y - PredUe.Y, 0.f);
	const float Excess = Err2D - Deadzone;
	const FVector Correction = Delta2D.GetSafeNormal() * (Excess * Alpha);
	FVector Blended = PredUe + Correction;
	Blended.Z = PredUe.Z;
	PredictedPose.SetLocationFromUnreal(Blended, WorldScale);

	if (bAllowHeading)
	{
		const FQuat BlendedQ = FQuat::Slerp(
			PredictedPose.GetAcQuat(), Server.GetAcQuat(), Alpha).GetNormalized();
		PredictedPose.RotationW = BlendedQ.W;
		PredictedPose.RotationXYZ = FVector(BlendedQ.X, BlendedQ.Y, BlendedQ.Z);
	}
}

void AACEPlayerController::HandlePlayerTeleportStarted()
{
	// Death respawns reuse this pawn. An ordinary Ready echo is not evidence
	// of revival, but the server's teleport is: stop both the falling animation
	// and its held final frame before presenting the lifestone arrival.
	if (APawn* PlayerPawn = GetPawn())
		if (auto* Appearance = PlayerPawn->FindComponentByClass<UACECharacterAppearanceComponent>())
			Appearance->ClearDeathMotion();
	// Retail: SmartBox::HandlePlayerTeleport raises waiting_for_teleport on 0xF751 and the
	// tunnel goes up immediately — the destination UpdatePosition follows. Entering here
	// (instead of on the TeleportSeq bump) means the old world never flashes at the new
	// position before the portal covers it.
	if (bUsePortalTransitionOnTeleport && bUseEnterWorldLoadScreen
		&& !bEnterWorldLoading && !bWorldRevealActive
		&& Client && Client->GetSessionState() == EACESessionState::InWorld)
	{
		BeginWorldTransition(TEXT("teleport"));
	}
}

void AACEPlayerController::HandlePositionUpdate(int32 ObjectGuid, const FACEPosition& Position)
{
	if (!Client || ObjectGuid != Client->GetPlayerGuid() || !bSyncPossessedPawn)
	{
		return;
	}


	LastServerPose = Position;
	bHaveLastServerPose = true;

	const uint16 NewTeleportSeq = Client->GetTeleportSeq();
	const uint16 NewForcePositionSeq = Client->GetSession() ? Client->GetSession()->GetForcePositionSeq() : 0;
	const bool bServerForcedPosition = NewForcePositionSeq != LastAppliedForcePositionSeq;
	LastAppliedForcePositionSeq = NewForcePositionSeq;
	const uint32 NewCell = static_cast<uint32>(Position.CellId);
	const uint32 PredCell = bHavePredictedPose ? static_cast<uint32>(PredictedPose.CellId) : NewCell;
	const bool bTeleportSeqChanged = (NewTeleportSeq != LastAppliedTeleportSeq);
	// Some servers deliver the destination without a preceding teleport event.
	if (bTeleportSeqChanged)
		if (APawn* PlayerPawn = GetPawn())
			if (auto* Appearance = PlayerPawn->FindComponentByClass<UACECharacterAppearanceComponent>())
				Appearance->ClearDeathMotion();
	const bool bCellChanged = (NewCell != PredCell);
	// A delayed room/doorway cell is an ordinary movement acknowledgement.
	// Treating it as an arrival ran the 4m placement search against adjacent
	// room geometry and repeatedly pulled a moving VR body back across portals.
	// Only actual teleport/load transitions use arrival placement. Explicit
	// force-position messages retain their separate authoritative branch below.
	const bool bForceSnap = bTeleportSeqChanged
		|| bEnterWorldLoading || bWorldRevealActive;
	if (bCellChanged)
	{
		if (const auto* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Movement.Debug")); Debug && Debug->GetInt())
		{
			static double LastCorrectionLog = 0.;
			const double Now = FPlatformTime::Seconds();
			if (Now - LastCorrectionLog >= 1.)
			{
				LastCorrectionLog = Now;
				UE_LOG(LogTemp, Log, TEXT("ACE MoveCorrection: localCell=%08X serverCell=%08X arrival=%d forced=%d predicting=%d errorXY=%.2f"),
					PredCell, NewCell, bForceSnap, bServerForcedPosition, bLocalPredicting,
					bHavePredictedPose ? FVector::Dist2D(PredictedPose.ToUnrealLocation(WorldScale), Position.ToUnrealLocation(WorldScale)) : 0.);
			}
		}
	}

	if (bTeleportSeqChanged && bUsePortalTransitionOnTeleport && bUseEnterWorldLoadScreen
		&& !bEnterWorldLoading && !bWorldRevealActive
		&& Client->GetSessionState() == EACESessionState::InWorld)
	{
		BeginWorldTransition(TEXT("teleport"));
	}
	else if (bTeleportSeqChanged && (bEnterWorldLoading || bWorldRevealActive))
	{
		ResetWorldEntryDestination();
	}
	else if (bTeleportSeqChanged)
	{
		// Instant teleport (no tunnel): arrive on the server, then resume MoveToState / AutoPos.
		bPortalExitNotified = false;
		NotifyPortalArrivalIfNeeded();
		InvalidateMovementAfterTeleport();
	}

	APawn* P = GetPawn();
	if (!P)
	{
		LastAppliedTeleportSeq = NewTeleportSeq;
		return;
	}

	const FVector TargetFeet = Position.ToUnrealLocation(WorldScale);
	const FQuat TargetQ = Position.ToUnrealQuat();
	FVector Target = TargetFeet;
	float CapsuleHalfHeight = 88.f;
	if (UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>())
	{
		CapsuleHalfHeight = Cap->GetScaledCapsuleHalfHeight();
	}
	Target.Z += CapsuleHalfHeight;

	FACEPosition Corrected = Position;
	if (!bForceSnap)
	{
		// Retail SmartBox::HandleReceivedPosition retains the player's heading even
		// for FORCE_POSITION_TS corrections. XYZ is authoritative, but the facing
		// in an acknowledgement predates any input during the network roundtrip.
		if (bHavePredictedPose && PredictedPose.IsValid())
		{
			Corrected.RotationW = PredictedPose.RotationW;
			Corrected.RotationXYZ = PredictedPose.RotationXYZ;
		}
		else
		{
			// The packet parser has already replaced Client->GetPlayerPosition().
			// After prediction settles, the pawn is the surviving local heading.
			const FQuat LocalRotation = P->GetActorQuat();
			Corrected.RotationW = LocalRotation.W;
			Corrected.RotationXYZ = FVector(LocalRotation.X, -LocalRotation.Y, -LocalRotation.Z);
		}
	}

	if (bServerForcedPosition && !bForceSnap)
	{
		// Forced corrections are authoritative, including vertical position. They
		// are not portal arrivals and must not run recall placement searches.
		P->SetActorLocation(Target);
		PredictedPose = Corrected;
		Client->SetReportedPosition(Corrected);
		bHavePredictedPose = true;
		bJumpAirborne = false;
		bJumpAirborneSent = false;
		bJumpCharging = false;
		bStandingJumpLocked = false;
		bHaveJumpLaunchFacing = false;
		StandingJumpAimF = StandingJumpAimR = 0.f;
		JumpAirborneSeconds = JumpChargeExtent = 0.f;
		JumpWorldAceVelocity = FVector::ZeroVector;
		JumpLocalAceVelocity = FVector::ZeroVector;
		StepHoldSeconds = 0.f;
		Client->SetForcePositionReporting(false);
		bForceMovementResend = true;
		if (auto* App = P->FindComponentByClass<UACECharacterAppearanceComponent>())
		{
			App->ClearJumpMotionIfAny();
			App->SetSuppressLocoIdleBlend(false);
		}
		return;
	}

	if (bForceSnap)
	{
		// Lifestone / portal recalls often land on the weenie origin (crystal center).
		// If the capsule overlaps world geometry at the snap point, push out along facing.
		{
			float CapsuleRadius = 34.f;
			if (UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>())
			{
				CapsuleRadius = Cap->GetScaledCapsuleRadius();
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(ACERecallDepenetrate), false, P);
			const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(
				CapsuleRadius, CapsuleHalfHeight * 0.9f);
			auto Occupied = [&](const FVector& At) -> bool
			{
				if (!GetWorld())
				{
					return false;
				}
				return GetWorld()->OverlapAnyTestByChannel(
					At, TargetQ, ECC_Pawn, CapsuleShape, Params);
			};
			if (Occupied(Target))
			{
				// Retail Transition::FindPlacementPos: ring of heading steps, adjustDist = 4 AC.
				const float SphereRadCm = FMath::Max(48.f, CapsuleRadius);
				const float AdjustCm = 4.f * WorldScale;
				const int32 NumRings = FMath::Max(2, FMath::CeilToInt(AdjustCm / SphereRadCm));
				const float DistPerRing = AdjustCm / static_cast<float>(NumRings);
				bool bFound = false;
				for (int32 Ring = 1; Ring <= NumRings && !bFound; ++Ring)
				{
					const float Dist = DistPerRing * static_cast<float>(Ring);
					const float RadiansPerStep = PI * DistPerRing / SphereRadCm;
					const int32 Steps = FMath::Max(4, FMath::CeilToInt(RadiansPerStep * static_cast<float>(Ring) * 2.f));
					const float AngleStep = 360.f / static_cast<float>(Steps);
					for (int32 J = 0; J < Steps; ++J)
					{
						const float Rad = FMath::DegreesToRadians(AngleStep * static_cast<float>(J));
						FVector Candidate = Target + FVector(FMath::Sin(Rad) * Dist, FMath::Cos(Rad) * Dist, 0.f);
						Candidate.Z = Target.Z;
						if (!Occupied(Candidate))
						{
							Target = Candidate;
							bFound = true;
							break;
						}
					}
				}
			}
		}
		P->SetActorLocation(Target);
		P->SetActorRotation(TargetQ);
		Client->SetReportedPosition(Position);
		PredictedPose = Position;
		PredictedPose.SetLocationFromUnreal(
			FVector(Target.X, Target.Y, Target.Z - CapsuleHalfHeight), WorldScale);
		bHavePredictedPose = bLocalPredicting && !bEnterWorldLoading && !bWorldRevealActive;
		LastAppliedTeleportSeq = NewTeleportSeq;

		if (bEnterWorldLoading || bWorldRevealActive)
		{
			P->SetActorHiddenInGame(true);
			P->SetActorEnableCollision(false);
		}
		return;
	}

	LastAppliedTeleportSeq = NewTeleportSeq;

	// While predicting (or settling): TickLocalMovement owns the transform and soft-blends
	// toward LastServerPose — never hard-reset the capsule here.
	if (bLocalPredicting || bHavePredictedPose)
	{
		// The packet parser stores the server pose before broadcasting it. Keep
		// the presentation/reporting pose consistent with the body until the next
		// movement tick; LastServerPose above preserves the authoritative anchor.
		FACEPosition LocalPosition;
		if (TryGetLocallyPredictedPosition(LocalPosition)) Client->SetReportedPosition(LocalPosition);
		return;
	}

	// Fully idle: gently ease the capsule toward the server pose (no hard check/reset).
	const FVector Current = P->GetActorLocation();
	const float ErrCm = FVector::Dist(Current, Target);
	const float Alpha = 1.f - FMath::Exp(-ServerReconcileRate * (GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f));
	if (ErrCm >= ServerSnapErrorCm)
	{
		P->SetActorLocation(Target);
	}
	else
	{
		P->SetActorLocation(FMath::Lerp(Current, Target, Alpha));
	}
	Client->SetReportedPosition(Corrected);
}

void AACEPlayerController::HandleCharacterList(const TArray<FACECharacterInfo>& Characters, const FString& ServerName)
{
	bShowMouseCursor = true;

	// Returning from in-world logoff (or first auth): tear down gameplay HUD.
	if (GameHUDWidget || DatGameplayBinder)
	{
		DestroyGameHUD();
	}

	// Do not flip to CharacterManagement until DAT chrome textures resolve — keep login
	// on-screen so the player never sees an empty / half-drawn char-select flash.
	PendingCharSelectCharacters = Characters;
	PendingCharSelectServerName = ServerName;
	bPendingCharSelect = true;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			Dat->SetWorldStreamingAllowed(false);
			if (!Dat->IsDatReady())
			{
				Dat->BeginBackgroundLoad();
			}
		}
	}
	if (!LoginWidget || !LoginWidget->IsInViewport())
	{
		ShowLoginUI();
	}
	if (LoginWidget)
	{
		LoginWidget->SetVisibility(ESlateVisibility::Visible);
		LoginWidget->SetStatus(TEXT("Loading character select..."));
	}
	TickPendingCharacterSelect();
}

void AACEPlayerController::HandleSessionStateChanged(EACESessionState NewState)
{
	if (NewState == EACESessionState::Connecting) WorldEntryFailureReason.Reset();
	if (NewState == EACESessionState::EnteringWorld && bUseEnterWorldLoadScreen)
		bPendingEnterWorldTransition = true; // Outside the Play button's Slate callback.
	if (NewState == EACESessionState::Disconnected || NewState == EACESessionState::Failed
		|| NewState == EACESessionState::CharacterSelect
		|| NewState == EACESessionState::AwaitCharacterList)
	{
		bPendingEnterWorldTransition = false;
		bDestinationStreamingStarted = false;
		if (bEnterWorldLoading || bWorldRevealActive)
		{
			bEnterWorldLoading = bWorldRevealActive = false;
			if (LoadingScreenActor) { LoadingScreenActor->Destroy(); LoadingScreenActor = nullptr; }
			ResetIgnoreMoveInput();
			ResetIgnoreLookInput();
			if (GetPawn()) SetViewTarget(GetPawn());
		}
		PendingSelfAppearanceGuid = 0;
		PortalWorldRevealElapsed = -1.f;
		if (GetGameInstance())
			if (auto* Dat = GetGameInstance()->GetSubsystem<UACEDatSubsystem>()) Dat->SetInPortalSpace(false);
		if (PlayerCameraManager) PlayerCameraManager->UnlockFOV();
		if (APawn* P = GetPawn())
		{
			if (auto* Scripts = P->FindComponentByClass<UACEScriptComponent>())
			{
				Scripts->StopAllSounds();
				Scripts->StopAllEffects();
				Scripts->DestroyComponent();
			}
			if (auto* Appearance = P->FindComponentByClass<UACECharacterAppearanceComponent>()) Appearance->ClearAppearance();
			P->SetActorHiddenInGame(true);
		}
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				Dat->SetWorldStreamingAllowed(false);
			}
		}
	}
	if (NewState == EACESessionState::Disconnected || NewState == EACESessionState::Failed)
	{
		bEnterWorldLoading = false;
		bWorldRevealActive = false;
		bAwaitingRecoveryDestination = false;
		if (LoadingScreenActor)
		{
			LoadingScreenActor->Destroy();
			LoadingScreenActor = nullptr;
		}
		bPendingCharSelect = false;
		PendingCharSelectCharacters.Reset();
		PendingCharSelectServerName.Reset();
		DestroyGameHUD();
		DestroyCharacterSelectUI();
		ShowLoginUI();
		if (LoginWidget && !WorldEntryFailureReason.IsEmpty()) LoginWidget->SetStatus(WorldEntryFailureReason);
	}
}

UACECharacterAppearanceComponent* AACEPlayerController::EnsurePawnAppearance()
{
	APawn* P = GetPawn();
	if (!P)
	{
		return nullptr;
	}
	UACECharacterAppearanceComponent* Comp = P->FindComponentByClass<UACECharacterAppearanceComponent>();
	if (!Comp)
	{
		Comp = NewObject<UACECharacterAppearanceComponent>(P, TEXT("ACEAppearance"));
		Comp->bUseWorldLighting = true;
		Comp->RegisterComponent();
		Comp->bHideOwnerMeshes = true;
	}
	return Comp;
}

void AACEPlayerController::HandleObjectCreated(const FACEWorldObject& Object)
{
	if (!bApplyLocalAppearance || !Object.bIsSelf)
	{
		return;
	}
	// Queue for next tick — never build DAT meshes inside PollSockets.
	PendingSelfAppearanceGuid = Object.Guid;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AACEPlayerController::ApplyPendingSelfAppearance);
	}
}

float AACEPlayerController::GetStepUpHeightCm() const
{
	// Retail PartArray.GetStepUpHeight = Setup.StepUpHeight * Scale.Z (no invented inflation).
	float StepAc = 0.5f;
	if (Client)
	{
		FACEWorldObject Self;
		if (Client->GetWorldObject(Client->GetPlayerGuid(), Self) && Self.SetupId != 0)
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
				{
					float IgnoredH = 0.f, IgnoredR = 0.f;
					uint32 IgnoredAnim = 0;
					float SetupStep = 0.5f;
					if (Dat->TryGetSetupPhysics(static_cast<uint32>(Self.SetupId), SetupStep, IgnoredH, IgnoredR, IgnoredAnim))
					{
						const float Scale = Self.Scale > KINDA_SMALL_NUMBER ? Self.Scale : 1.f;
						StepAc = FMath::Max(0.05f, SetupStep * Scale);
					}
				}
			}
		}
	}
	return FMath::Max(5.f, StepAc * WorldScale);
}

float AACEPlayerController::GetStepDownHeightCm() const
{
	// Retail PartArray.GetStepDownHeight = Setup.StepDownHeight * Scale.Z (~0.5 AC).
	// Used for CheckWalkable / precipice — do not inflate like client StepUp.
	float StepAc = 0.5f;
	if (Client)
	{
		FACEWorldObject Self;
		if (Client->GetWorldObject(Client->GetPlayerGuid(), Self) && Self.SetupId != 0)
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
				{
					float IgnoredUp = 0.f, IgnoredH = 0.f, IgnoredR = 0.f;
					uint32 IgnoredAnim = 0;
					float SetupStepDown = 0.5f;
					if (Dat->TryGetSetupPhysics(static_cast<uint32>(Self.SetupId), IgnoredUp, IgnoredH, IgnoredR, IgnoredAnim, &SetupStepDown))
					{
						const float Scale = Self.Scale > KINDA_SMALL_NUMBER ? Self.Scale : 1.f;
						StepAc = FMath::Clamp(SetupStepDown * Scale, 0.15f, 1.0f);
					}
				}
			}
		}
	}
	return FMath::Clamp(StepAc * WorldScale, 15.f, 100.f);
}

float AACEPlayerController::GetUseCylinderDistanceCm(
	const FACEWorldObject& Target, const FVector& SelfOriginCm, const FVector& TargetOriginCm) const
{
	// Mirror ACE Position.CylinderDistanceNoZ — gap between Setup cylinders (not centers).
	// Slightly under-estimate radii so the client must walk a bit closer than the server allows.
	float SelfScale = 1.f;
	if (Client)
	{
		FACEWorldObject Self;
		if (Client->GetWorldObject(Client->GetPlayerGuid(), Self))
		{
			SelfScale = Self.Scale > KINDA_SMALL_NUMBER ? Self.Scale : 1.f;
		}
	}
	// Full Setup radii — under-estimating left the client short of server UseRadius.
	const float SelfRadiusAc = FMath::Clamp(PlayerSetupRadiusAc * SelfScale, 0.2f, 0.55f);

	const float TargetScale = Target.Scale > KINDA_SMALL_NUMBER ? Target.Scale : 1.f;
	float TargetRadiusAc = 0.25f;
	if (Target.SetupId != 0)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				float Step = 0.f, Height = 0.f, Radius = 0.25f;
				uint32 Anim = 0;
				if (Dat->TryGetSetupPhysics(static_cast<uint32>(Target.SetupId), Step, Height, Radius, Anim))
				{
					TargetRadiusAc = FMath::Clamp(Radius * TargetScale, 0.08f, 2.5f);
				}
			}
		}
	}

	const FVector OffsetAc = (TargetOriginCm - SelfOriginCm) / WorldScale;
	const float ReachAc = OffsetAc.Size2D() - (SelfRadiusAc + TargetRadiusAc);
	return ReachAc * WorldScale;
}

void AACEPlayerController::ApplyPlayerCapsuleFromSetup(int32 SetupId)
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>();
	if (!Cap)
	{
		return;
	}

	float StepAc = 0.5f;
	float HeightAc = PlayerSetupHeightAc;
	float RadiusAc = PlayerSetupRadiusAc;
	uint32 DefaultAnim = 0;
	if (SetupId != 0)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				Dat->TryGetSetupPhysics(static_cast<uint32>(SetupId), StepAc, HeightAc, RadiusAc, DefaultAnim);
			}
		}
	}

	float ObjScale = 1.f;
	if (Client)
	{
		FACEWorldObject Self;
		if (Client->GetWorldObject(Client->GetPlayerGuid(), Self) && Self.Scale > KINDA_SMALL_NUMBER)
		{
			ObjScale = Self.Scale;
		}
	}

	// Full Setup size for Use-radius math only.
	PlayerSetupHeightAc = FMath::Max(0.5f, HeightAc);
	PlayerSetupRadiusAc = FMath::Max(0.15f, RadiusAc);

	// Physics cylinder height (Setup.Height) is feet→crown — NOT mesh bounds. Mesh AABB
	// includes wings/weapons and made the capsule taller than the head (stair soffit hits).
	const float CollisionHeightAc = FMath::Clamp(PlayerSetupHeightAc * ObjScale, 1.5f, 2.2f);
	// Fall back to the setup bound only when no walking sphere is authored.
	float CollisionRadiusAc = FMath::Clamp(PlayerSetupRadiusAc * ObjScale, 0.24f, 0.55f);
	// CPhysicsObj initializes SPHEREPATH from Setup's actual spheres.
	// Setup.Radius is an enclosing bound, not the walking sphere.
	if (auto* Dat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr)
	{
		TArray<FACEDatCollisionShape> Shapes; bool bHasBsp = false;
		if (Dat->GetSetupCollisionShapes(SetupId, Shapes, bHasBsp) && !Shapes.IsEmpty())
		{
			const auto* Lower = &Shapes[0];
			for (const auto& S : Shapes) if (S.Origin.Z < Lower->Origin.Z) Lower = &S;
			if (Lower->Height == 0.f && Lower->Radius > 0.f && Lower->Origin.SizeSquared2D() < .0001f)
				CollisionRadiusAc = FMath::Clamp(Lower->Radius * ObjScale, .08f, 2.5f);
		}
	}
	const float RadiusCm = CollisionRadiusAc * WorldScale;
	// Unreal capsules require half-height >= radius. Match AC cylinder top to the head —
	// only a tiny shave so indoor soffits still clear without losing roof hits.
	constexpr float HeadClearanceCm = 2.f;
	const float HalfHeightCm = FMath::Max(RadiusCm,
		CollisionHeightAc * WorldScale * 0.5f - HeadClearanceCm * 0.5f);

	const float OldHalf = Cap->GetScaledCapsuleHalfHeight();
	const FVector Loc = P->GetActorLocation();
	const float FeetZ = Loc.Z - OldHalf;

	// SetCapsuleSize takes unscaled dimensions. Object.Scale already contributes to
	// the world dimensions above and also scales the owning pawn.
	const float ShapeScale = FMath::Max(KINDA_SMALL_NUMBER, Cap->GetShapeScale());
	Cap->SetCapsuleSize(RadiusCm / ShapeScale, HalfHeightCm / ShapeScale, true);
	Cap->SetUseCCD(true);
	// World hover/select uses Visibility on capsules (not mesh parts).
	Cap->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ApplyLocalPawnPkCollision();

	FVector NewLoc = Loc;
	NewLoc.Z = FeetZ + Cap->GetScaledCapsuleHalfHeight();
	P->SetActorLocation(NewLoc, false);

	if (UACECharacterAppearanceComponent* AppearanceComp =
		P->FindComponentByClass<UACECharacterAppearanceComponent>())
	{
		AppearanceComp->bAlignMeshToCapsuleBottom = true;
		AppearanceComp->RefreshMeshAlignment();
	}
	if (USpringArmComponent* Boom = P->FindComponentByClass<USpringArmComponent>())
	{
		ApplyRetailCameraPivot(Boom, Cap);
	}
}

void AACEPlayerController::ApplyLocalPawnPkCollision()
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>();
	if (!Cap)
	{
		return;
	}
	bool bPk = false;
	if (Client)
	{
		bPk = ACEPlayerKillerStatus::BlocksPlayers(Client->GetPlayerVitals().PlayerKillerStatus);
		if (!bPk)
		{
			FACEWorldObject Self;
			if (Client->GetWorldObject(Client->GetPlayerGuid(), Self))
			{
				bPk = Self.IsPkOrPkLite();
			}
		}
	}
	Cap->SetCollisionResponseToChannel(ECC_Pawn, bPk ? ECR_Block : ECR_Ignore);
}

void AACEPlayerController::HandleVitalsUpdated(const FACEPlayerVitals& /*Vitals*/)
{
	ApplyLocalPawnPkCollision();
}

void AACEPlayerController::ClearServerMoveTo()
{
	bServerMoveToActive = false;
	ServerMoveToTargetGuid = 0;
	ServerMoveToDistance = 0.6f;
	ServerMoveToFailDistance = 0.f;
	bHaveApproachStartPose = false;
	UseApproachPhase = EACEUseApproachPhase::None;
	ApproachTargetDir = FVector::ZeroVector;
	bResendUseWhenInRange = false;
	LastApproachUseTime = 0.0;
	bApproachPickupIntoInventory = false;
	bApproachMoveOnly = false;
	bAwaitingUseDone = false;
	ApproachUseSendCount = 0;
	VendorOpenWaitSeconds = 0.f;
	ApproachStallSeconds = 0.f;
	ApproachStallLastLoc = FVector::ZeroVector;
	ApproachTurnSeconds = 0.f;
	if (Client)
	{
		Client->SetForcePositionReporting(false);
	}
}

void AACEPlayerController::HandleUseDone(uint32 /*Error*/)
{
	bAwaitingUseDone = false;
	if (bServerMoveToActive && !bApproachPickupIntoInventory)
	{
		ClearServerMoveTo();
	}
}

void AACEPlayerController::EndUseApproach()
{
	ClearServerMoveTo();
}

void AACEPlayerController::HandleMoveToFailed(uint32 /*Error*/)
{
	if (!bServerMoveToActive) return;
	ClearServerMoveTo();
	ForwardSent = RightSent = TurnSent = 0.f;
	bWasMoving = false;
	if (Client) Client->StopMovement();
}

void AACEPlayerController::BeginUseApproach(int32 TargetGuid, float DistanceAc, bool bPickupIntoInventory,
	bool bFireActionWhenInRange)
{
	if (IsVRActive())
	{
		FACEWorldObject Target;
		if (!Client || Client->IsUseBusy() || !Client->GetWorldObject(TargetGuid, Target)) return;
		const auto* Capsule = GetPawn()->FindComponentByClass<UCapsuleComponent>();
		const FVector Feet = GetPawn()->GetActorLocation() - FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight());
		if (GetUseCylinderDistanceCm(Target, Feet, Target.Position.ToUnrealLocation(WorldScale)) > DistanceAc * WorldScale + 5.f) return;
		Client->FlushAutonomousPosition(true);
		if (bPickupIntoInventory) Client->SendPutItemInContainer(TargetGuid, Client->GetPlayerGuid(), 0);
		else if (bFireActionWhenInRange) Client->SendUseItem(TargetGuid);
		return;
	}
	if (TargetGuid == 0)
	{
		return;
	}
	if (bServerMoveToActive && ServerMoveToTargetGuid == TargetGuid)
	{
		bResendUseWhenInRange = bFireActionWhenInRange;
		bApproachPickupIntoInventory = bPickupIntoInventory;
		bApproachMoveOnly = !bFireActionWhenInRange;
		// Fresh player-initiated interaction on the same target: allow one more Use send.
		ApproachUseSendCount = 0;
		VendorOpenWaitSeconds = 0.f;
		ServerMoveToDistance = DistanceAc;
		// Re-Use while Holding: resume Walking so we close any remaining gap then Use.
		if (UseApproachPhase == EACEUseApproachPhase::Holding
			|| UseApproachPhase == EACEUseApproachPhase::None)
		{
			UseApproachPhase = EACEUseApproachPhase::Walking;
			ApproachStallSeconds = 0.f;
		}
		if (Client)
		{
			Client->SetForcePositionReporting(true);
		}
		return;
	}
	bServerMoveToActive = true;
	ServerMoveToTargetGuid = TargetGuid;
	ServerMoveToDistance = DistanceAc;
	// Use / pickup: no FailDistance (server default is MaxValue). Melee charge sets it via BeginServerMoveTo.
	ServerMoveToFailDistance = 0.f;
	UseApproachPhase = EACEUseApproachPhase::Turning;
	ApproachTargetDir = FVector::ZeroVector;
	bResendUseWhenInRange = bFireActionWhenInRange;
	bApproachPickupIntoInventory = bPickupIntoInventory;
	bApproachMoveOnly = !bFireActionWhenInRange;
	ApproachUseSendCount = 0;
	VendorOpenWaitSeconds = 0.f;
	ApproachStallSeconds = 0.f;
	ApproachStallLastLoc = FVector::ZeroVector;
	ApproachTurnSeconds = 0.f;
	if (Client)
	{
		Client->SetForcePositionReporting(true);
		FACEPosition Seed = bHavePredictedPose ? PredictedPose : Client->GetPlayerPosition();
		if (Seed.IsValid())
		{
			PredictedPose = Seed;
			bHavePredictedPose = true;
			ApproachStartPose = Seed;
			bHaveApproachStartPose = true;
		}
		FACEWorldObject Target;
		if (Client->GetWorldObject(TargetGuid, Target) && Target.bHasPosition && GetPawn())
		{
			// Protocol pose = cylinder bottom (same as tick approach); pawn location is capsule center.
			const FACEPosition SelfPose = bHavePredictedPose ? PredictedPose : Client->GetPlayerPosition();
			const FVector SelfLoc = SelfPose.IsValid()
				? SelfPose.ToUnrealLocation(WorldScale)
				: GetPawn()->GetActorLocation();
			const FVector TargetLoc = Target.Position.ToUnrealLocation(WorldScale);
			FVector To = TargetLoc - SelfLoc;
			To.Z = 0.f;
			if (!To.IsNearlyZero())
			{
				ApproachTargetDir = To.GetSafeNormal();
			}
			// Already inside UseRadius (cylinder gap) — Use immediately; never F=1 into a wall.
			const float UseRadiusCm = ServerMoveToDistance * WorldScale;
			const float Sp = FMath::Max(5.f, 0.05f * WorldScale);
			if (GetUseCylinderDistanceCm(Target, SelfLoc, TargetLoc) <= UseRadiusCm + Sp)
			{
				UseApproachPhase = EACEUseApproachPhase::Holding;
				if (bFireActionWhenInRange)
				{
					if (bPickupIntoInventory)
					{
						Client->SendPutItemInContainer(TargetGuid, Client->GetPlayerGuid(), 0);
					}
					else
					{
						Client->SendUseItem(TargetGuid);
						bAwaitingUseDone = true;
					}
					++ApproachUseSendCount;
					bResendUseWhenInRange = Target.IsVendor();
				}
				else
				{
					// Give already in range — report pose; server MoveToChain finishes the hand-off.
					if (bHavePredictedPose)
					{
						Client->SetReportedPosition(PredictedPose);
					}
					Client->FlushAutonomousPosition(true);
				}
			}
		}
	}
}

void AACEPlayerController::InteractWithSelectedObject()
{
	if (!Client)
	{
		return;
	}
	const FACESelectedObject Sel = Client->GetSelectedObject();
	if (!Sel.bValid || Sel.Guid == 0)
	{
		return;
	}
	InteractWithObject(Sel.Guid);
}

void AACEPlayerController::InteractWithObject(int32 ObjectGuid)
{
	if (!Client || ObjectGuid == 0)
	{
		return;
	}
	if (DatGameplayBinder && DatGameplayBinder->TryCompletePendingUseWithTarget(ObjectGuid))
	{
		return;
	}
	FACEWorldObject Obj;
	if (!Client->GetWorldObject(ObjectGuid, Obj))
	{
		if (Client->IsUseBusy())
		{
			return;
		}
		Client->SendUseItem(ObjectGuid);
		return;
	}

	// Authored UseRadius as-is (negative portals = overlap). Parser default is already 0.6.
	const float DistAc = Obj.UseRadius;
	const int32 PlayerGuid = Client->GetPlayerGuid();

	// Other players: open secure trade (give is via inventory drag).
	if (Obj.bIsPlayer && ObjectGuid != PlayerGuid)
	{
		Client->SendOpenTrade(ObjectGuid);
		UE_LOG(LogTemp, Log, TEXT("ACE: OpenTrade guid=0x%08X name='%s'"), ObjectGuid, *Obj.Name);
		return;
	}

	// F and the hand toolbar share the same server-confirmed inventory operation.
	if (Client->SortInventoryItem(ObjectGuid)) return;

	// Retail F/hand on an item in an external pack/chest: PutItemInContainer → player main pack
	// at placement 0 (server merges matching stacks, then places leftovers in the first free slot).
	if (Obj.ContainerId != 0 && Obj.CurrentWieldedLocation == 0 && Obj.WielderId == 0
		&& Obj.ParentGuid == 0 && Obj.ContainerId != PlayerGuid)
	{
		Client->SendPutItemInContainer(ObjectGuid, PlayerGuid, 0);
		UE_LOG(LogTemp, Log, TEXT("ACE: Pack→main PutInContainer guid=0x%08X from=0x%08X"),
			ObjectGuid, Obj.ContainerId);
		return;
	}

	if (Obj.IsWorldLootable())
	{
		// Retail hand/F on ground loot: approach, then PutItemInContainer at range.
		BeginUseApproach(ObjectGuid, DistAc, /*bPickupIntoInventory*/ true);
		UE_LOG(LogTemp, Log, TEXT("ACE: Pickup approach guid=0x%08X name='%s'"), ObjectGuid, *Obj.Name);
		return;
	}

	// Attackable creatures: select only (double-click already selected). Do not Use/run-to.
	if ((Obj.ItemType & ACEItemType::Creature) != 0 && Obj.IsAttackable() && !Obj.IsVendor())
	{
		UE_LOG(LogTemp, Log, TEXT("ACE: Select-only attackable guid=0x%08X name='%s'"), ObjectGuid, *Obj.Name);
		return;
	}

	// Stuck scenery (signs, etc.): identify/select only — no Use. Stuck + Usable (fountains,
	// levers, Font of Jojii) must still SendUseItem / approach.
	if ((Obj.ObjectDescriptionFlags & ACEObjectDescFlag::Stuck) != 0
		&& !Obj.IsDoor() && !Obj.IsOpenable() && !Obj.IsLifeStone() && !Obj.IsCorpse()
		&& !Obj.IsVendor() && !Obj.IsGameBoard()
		&& (Obj.ItemType & (ACEItemType::Creature | ACEItemType::Portal)) == 0
		&& !ACEItemUseable::IsSourceUsable(Obj.ItemUseable))
	{
		UE_LOG(LogTemp, Log, TEXT("ACE: Stuck non-usable guid=0x%08X name='%s'"), ObjectGuid, *Obj.Name);
		return;
	}

	if (Obj.ShouldClientApproachForUse())
	{
		if (Client->IsUseBusy())
		{
			return;
		}
		// Client walks in (F=1 MoveToState). Send Use only once in range — any later
		// MoveToState (including axes-0 / StopMovement) cancels server CreateMoveToChain.
		BeginUseApproach(ObjectGuid, DistAc, /*bPickupIntoInventory*/ false);
		UE_LOG(LogTemp, Log, TEXT("ACE: Use approach guid=0x%08X name='%s'"), ObjectGuid, *Obj.Name);
		return;
	}

	if (Client->IsUseBusy())
	{
		return;
	}
	Client->SendUseItem(ObjectGuid);
	UE_LOG(LogTemp, Log, TEXT("ACE: Use guid=0x%08X name='%s'"), ObjectGuid, *Obj.Name);
}

void AACEPlayerController::FaceWorldTarget(int32 TargetGuid)
{
	if (IsVRActive()) return;
	if (!Client || TargetGuid == 0 || TargetGuid == Client->GetPlayerGuid())
	{
		return;
	}
	FACEWorldObject Target;
	if (!Client->GetWorldObject(TargetGuid, Target) || !Target.bHasPosition)
	{
		return;
	}
	FACEPosition Pose = bHavePredictedPose ? PredictedPose : Client->GetPlayerPosition();
	if (!Pose.IsValid())
	{
		return;
	}
	const FVector Self = Pose.ToUnrealLocation(WorldScale);
	const FVector Dest = Target.Position.ToUnrealLocation(WorldScale);
	FVector To = Dest - Self;
	To.Z = 0.f;
	if (To.IsNearlyZero())
	{
		return;
	}
	Pose.SetAceFacingFromUnrealDir2D(To.GetSafeNormal());
	PredictedPose = Pose;
	bHavePredictedPose = true;
	if (APawn* Possessed = GetPawn())
	{
		Possessed->SetActorRotation(Pose.ToUnrealQuat());
	}
	Client->SetReportedPosition(PredictedPose);
	Client->FlushAutonomousPosition(true);
}

void AACEPlayerController::BeginServerMoveTo(const FACEObjectMotionState& Motion)
{
	if (IsVRActive()) return;
	if (bServerMoveToActive && ServerMoveToTargetGuid == Motion.MoveToTargetGuid)
	{
		// Repeated server MoveTo updates must not restart Turning after the straight run begins.
		ServerMoveToDistance = Motion.MoveToDistance > 0.f ? Motion.MoveToDistance : ServerMoveToDistance;
		ServerMoveToFailDistance = Motion.MoveToFailDistance;
		return;
	}
	// Preserve client Use/pickup/give intent across a server MoveTo echo.
	// GiveObjectRequest arrives as MoveTo with neither flag — approach only (no SendUseItem).
	// Never re-arm Use while MoveOnly (give): Use runs StopExistingMoveToChains and cancels the turn-in.
	const bool bKeepPickup = bApproachPickupIntoInventory;
	const bool bKeepMoveOnly = bApproachMoveOnly && !bKeepPickup;
	const bool bFire = bKeepMoveOnly ? false : (bResendUseWhenInRange || bKeepPickup);
	// MoveToDistance can be negative (portal overlap UseRadius) — pass through unchanged.
	BeginUseApproach(Motion.MoveToTargetGuid, Motion.MoveToDistance, bKeepPickup, bFire);
	ServerMoveToFailDistance = Motion.MoveToFailDistance;
}

void AACEPlayerController::HandleMotionUpdate(int32 ObjectGuid, const FACEObjectMotionState& Motion)
{
	if (!Client || ObjectGuid != Client->GetPlayerGuid())
	{
		return;
	}
	// TurnToObject is also sent for sticky melee. Apply it to local prediction;
	// otherwise the next autonomous position overwrites the server's rotation.
	if (Motion.MovementType == 8 && Motion.MoveToTargetGuid != 0)
	{
		FaceWorldTarget(Motion.MoveToTargetGuid);
	}
	else if (Motion.MovementType == 9)
	{
		FACEPosition Pose = bHavePredictedPose ? PredictedPose : Client->GetPlayerPosition();
		if (Pose.IsValid())
		{
			Pose.SetAceFacingFromUnrealDir2D(FACEPosition::UnrealDirFromAceHeadingDegrees(Motion.MoveToDesiredHeading));
			PredictedPose = Pose;
			bHavePredictedPose = true;
			if (APawn* ControlledPawn = GetPawn()) ControlledPawn->SetActorRotation(Pose.ToUnrealQuat());
			Client->SetReportedPosition(Pose);
			Client->FlushAutonomousPosition(true);
		}
	}
	else if (Motion.MovementType == 6 && Motion.MoveToTargetGuid != 0)
	{
		BeginServerMoveTo(Motion);
	}
	else if (Motion.MovementType == 7 && bServerMoveToActive)
	{
		// MoveToPosition echo while already Use-approaching — keep target, refresh stop distance.
		// A give sends its own MoveToPosition (server CreateMoveToChain for GiveObjectRequest);
		// flipping to Use here cancelled the give chain with ActionCancelled.
		ServerMoveToDistance = Motion.MoveToDistance;
		bResendUseWhenInRange = !bApproachMoveOnly;
		ServerMoveToFailDistance = Motion.MoveToFailDistance;
	}
	else if (Motion.MovementType != 6 && Motion.MovementType != 7 && !Motion.bMoving)
	{
		// Charge cancelled / finished (YouChargedTooFar) — stop local run.
		// Only cancel when FailDistance was set (server MoveTo / melee charge).
		// Client Use approach keeps FailDistance=0 and must survive idle motion echoes.
		if (bServerMoveToActive && ServerMoveToFailDistance > 0.f
			&& (UseApproachPhase == EACEUseApproachPhase::Walking
				|| UseApproachPhase == EACEUseApproachPhase::Turning))
		{
			ClearServerMoveTo();
		}
	}

	// Server CurrentStyle / cast-attack Commands apply to the local pawn appearance too.
	// Ignore transient NonCombat from SwitchCombatStyles while CombatMode is still armed —
	// adopting it flashes magic→standing and MoveToState then locks peace on the server.
	if (APawn* PossessedPawn = GetPawn())
	{
		if (UACECharacterAppearanceComponent* App = PossessedPawn->FindComponentByClass<UACECharacterAppearanceComponent>())
		{
			if (Motion.CurrentStyle != 0)
			{
				const uint32 Style = static_cast<uint32>(Motion.CurrentStyle);
				const bool bStyleNonCombat = Style == ACEMotion::StanceNonCombat;
				const int32 Mode = Client->GetPlayerVitals().CombatMode;
				const bool bCombatModeActive = Client->GetPlayerVitals().bValid
					&& Mode != static_cast<int32>(ACECombatMode::NonCombat)
					&& Mode != 0;
				if (!bStyleNonCombat || !bCombatModeActive)
				{
					App->SetPreferredStyle(Motion.CurrentStyle);
				}
			}
			if (Motion.ActionCommand != 0)
			{
				const uint32 Act = static_cast<uint32>(Motion.ActionCommand);
				const uint32 Full = Act <= 0xFFFFu
					? ACEMotion::ExpandPackedCommand(static_cast<uint16>(Act))
					: Act;
				const uint32 Hi = Full & 0xFF000000u;
				// ChatPose / soul emotes hold final frame until locomotion (retail).
				const bool bHoldEmote = Hi == 0x13000000u || Hi == 0x43000000u || Hi == 0x42000000u;
				App->PlayActionMotion(Motion.ActionCommand, Motion.ActionSpeed, Motion.CurrentStyle, bHoldEmote);
				if (!bHoldEmote)
				{
					for (int32 Followup : Motion.ActionFollowups)
					{
						App->QueueActionMotion(Followup, Motion.ActionSpeed, Motion.CurrentStyle, false);
					}
				}
			}
		}
	}
}

void AACEPlayerController::HandlePhysicsStateUpdate(int32 ObjectGuid, int32 PhysicsState)
{
	// Door opened (Ethereal) — end approach. bResendUseWhenInRange stays true while Holding
	// and retrying Use, so do not gate ClearServerMoveTo on that flag.
	if (bServerMoveToActive && ObjectGuid == ServerMoveToTargetGuid
		&& (PhysicsState & ACEPhysicsState::Ethereal) != 0)
	{
		ClearServerMoveTo();
	}

	// Retail portal completion clears the Hidden physics bit on the local player.
	if (Client && ObjectGuid == Client->GetPlayerGuid())
	{
		const bool bPhysicsHidden = (PhysicsState & ACEPhysicsState::Hidden) != 0;
		if (bPhysicsHidden && (bEnterWorldLoading || bWorldRevealActive || bUsePortalTransitionOnTeleport))
		{
			bWaitingForServerUnhide = true;
		}
		else if (!bPhysicsHidden)
		{
			bWaitingForServerUnhide = false;
		}
	}
}

bool AACEPlayerController::TryGetLocallyPredictedPosition(FACEPosition& OutPosition) const
{
	if (!bHavePredictedPose || !PredictedPose.IsValid() || bEnterWorldLoading || bWorldRevealActive
		|| !Client || Client->GetSessionState() != EACESessionState::InWorld) return false;
	const auto Session = Client->GetSession();
	// Presentation delegates may run before this controller's packet delegate.
	// Never mask a teleport/forced position that has not been applied yet.
	if (!Session || Session->GetTeleportSeq() != LastAppliedTeleportSeq
		|| Session->GetForcePositionSeq() != LastAppliedForcePositionSeq) return false;
	OutPosition = PredictedPose;
	return true;
}

int32 AACEPlayerController::GetEffectiveCellId() const
{
	if (bHavePredictedPose && PredictedPose.IsValid())
	{
		return PredictedPose.CellId;
	}
	if (Client)
	{
		const FACEPosition Pos = Client->GetPlayerPosition();
		if (Pos.IsValid())
		{
			return Pos.CellId;
		}
	}
	return 0;
}

bool AACEPlayerController::GetHoverTooltip(FString& OutText, FVector2D& OutCursorPixels) const
{
	if (!bHoverTooltipVisible || HoverTooltipText.IsEmpty())
	{
		return false;
	}
	OutText = HoverTooltipText;
	OutCursorPixels = HoverCursorPixels;
	return true;
}

void AACEPlayerController::EnsureHoverTooltipWidget()
{
	if (HoverTooltipWidget)
	{
		return;
	}
	HoverTooltipWidget = CreateWidget<UACEHoverTooltipWidget>(this, UACEHoverTooltipWidget::StaticClass());
	if (!HoverTooltipWidget)
	{
		return;
	}
	// Above login (9999) and any other Slate chrome so the blurb is never buried.
	HoverTooltipWidget->AddToPlayerScreen(10050);
	HoverTooltipWidget->SetVisibility(ESlateVisibility::Collapsed);
}

bool AACEPlayerController::IsMouseOverBlockingUI() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	// Must use absolute desktop coords — GetMousePosition() is viewport-local and made
	// LocateWindowUnderMouse always hit the wrong Visible chrome (blocking all world picks).
	const FVector2D ScreenPos = FSlateApplication::Get().GetCursorPos();

	// DAT-driven HUD: full-screen canvas uses SelfHitTestInvisible image chrome, so Slate
	// path often misses SBorder. Hit-test the element tree so spell/panel clicks don't
	// fall through to PollObjectClick (which clears the selected target).
	if (bUseDatDrivenHud && DatCanvasWidget
		&& DatCanvasWidget->GetVisibility() != ESlateVisibility::Collapsed
		&& DatCanvasWidget->GetVisibility() != ESlateVisibility::Hidden
		&& Client)
	{
		if (UACEUIElementManager* Mgr = Client->GetUIElementManager())
		{
			const FGeometry& Geo = DatCanvasWidget->GetCachedGeometry();
			const FVector2D Local = Geo.AbsoluteToLocal(ScreenPos);
			int32 Cx = 0, Cy = 0;
			if (Mgr->ViewportToCanvas(Local, Geo.GetLocalSize(), Cx, Cy)
				&& Mgr->IsCanvasOverUI(Cx, Cy))
			{
				return true;
			}
		}
	}

	FWidgetPath WidgetPath = FSlateApplication::Get().LocateWindowUnderMouse(
		ScreenPos, FSlateApplication::Get().GetInteractiveTopLevelWindows(), false);
	if (!WidgetPath.IsValid())
	{
		return false;
	}

	auto IsInteractiveControl = [](const SWidget& W) -> bool
	{
		const FString Type = W.GetTypeAsString();
		// SBorder/SImage: HUD chrome (AddArt/AddPanel) binds mouse handlers — must block
		// world pick when the cursor is over those panels.
		return Type == TEXT("SButton")
			|| Type == TEXT("SBorder")
			|| Type == TEXT("SImage")
			|| Type == TEXT("SScrollBar")
			|| Type == TEXT("SScrollBox")
			|| Type.Contains(TEXT("EditableText"))
			|| Type.Contains(TEXT("SCheckBox"))
			|| Type.Contains(TEXT("SSlider"))
			|| Type.Contains(TEXT("SCombo"));
	};

	for (int32 i = WidgetPath.Widgets.Num() - 1; i >= 0; --i)
	{
		const TSharedRef<SWidget>& Widget = WidgetPath.Widgets[i].Widget;
		const EVisibility Vis = Widget->GetVisibility();
		if (Vis == EVisibility::Collapsed || Vis == EVisibility::Hidden
			|| Vis == EVisibility::HitTestInvisible || Vis == EVisibility::SelfHitTestInvisible)
		{
			continue;
		}
		if (Vis == EVisibility::Visible && IsInteractiveControl(*Widget))
		{
			return true;
		}
	}
	return false;
}

namespace
{
	struct FACEVisibilityPick
	{
		AACEWorldEntityActor* Entity = nullptr;
		bool bHitSelf = false;
	};

	AACEWorldEntityActor* AsWorldEntity(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		if (AACEWorldEntityActor* Entity = Cast<AACEWorldEntityActor>(Actor))
		{
			return Entity;
		}
		return Cast<AACEWorldEntityActor>(Actor->GetOwner());
	}

	FACEVisibilityPick ResolveVisibilityPick(UWorld& World, const FVector& Start, const FVector& End, APawn* SelfPawn)
	{
		AActor* Actor = ACEVisibleObjectPick::Trace(World, Start, End, SelfPawn);
		FACEVisibilityPick Out;
		Out.bHitSelf = Actor && Actor == SelfPawn;
		Out.Entity = Out.bHitSelf ? nullptr : AsWorldEntity(Actor);
		return Out;
	}
}

void AACEPlayerController::PollObjectHover()
{
	bHoverTooltipVisible = false;
	HoverTooltipText.Reset();

	auto HideTip = [this]()
	{
		if (HoverTooltipWidget)
		{
			HoverTooltipWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (bRetailCursorInstalled && bRetailCursorInteractable)
		{
			ApplyRetailMouseCursor(false);
		}
	};

	if (!Client || Client->GetSessionState() != EACESessionState::InWorld || bMouseLookActive)
	{
		HideTip();
		return;
	}

	EnsureHoverTooltipWidget();

	float MouseX = 0.f, MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		HideTip();
		return;
	}
	HoverCursorPixels = FVector2D(MouseX, MouseY);
	if (DatGameplayBinder && FSlateApplication::IsInitialized()
		&& DatGameplayBinder->GetStatTooltipAt(FSlateApplication::Get().GetCursorPos(), HoverTooltipText))
	{
		bHoverTooltipVisible = true;
		if (HoverTooltipWidget)
		{
			HoverTooltipWidget->SetTooltipText(HoverTooltipText);
			HoverTooltipWidget->SetTooltipScreenPosition(HoverCursorPixels);
			HoverTooltipWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}

	if (IsMouseOverBlockingUI())
	{
		HideTip();
		if (bPendingUseTargeting && DatGameplayBinder && FSlateApplication::IsInitialized())
		{
			const int32 Target=DatGameplayBinder->GetInventoryTargetAt(FSlateApplication::Get().GetCursorPos());
			ApplyRetailMouseCursor(Target!=0,DatGameplayBinder->IsPendingUseTargetCompatible(Target));
		}
		return;
	}

	FVector WorldOrigin, WorldDir;
	if (!DeprojectScreenPositionToWorld(MouseX, MouseY, WorldOrigin, WorldDir))
	{
		HideTip();
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		HideTip();
		return;
	}

	// Use the same rendered-polygon ordering for hover, click, identify, and drag.
	const FVector TraceEnd = WorldOrigin + WorldDir * 200000.f;

	APawn* SelfPawn = GetPawn();
	const FACEVisibilityPick Pick = ResolveVisibilityPick(*World, WorldOrigin, TraceEnd, SelfPawn);
	AACEWorldEntityActor* Entity = Pick.Entity;
	const bool bHitSelf = Pick.bHitSelf;
	if (!Entity && !bHitSelf)
	{
		HideTip();
		return;
	}

	FString Name;
	if (bHitSelf)
	{
		FACEWorldObject SelfObj;
		if (Client->GetWorldObject(Client->GetPlayerGuid(), SelfObj))
		{
			Name = SelfObj.Name;
		}
		if (Name.IsEmpty())
		{
			Name = TEXT("You");
		}
	}
	else
	{
		Name = Entity->ACEName;
		if (Name.IsEmpty())
		{
			FACEWorldObject Obj;
			if (Client->GetWorldObject(Entity->GetACEGuid(), Obj))
			{
				Name = Obj.Name;
			}
		}
		if (Name.IsEmpty())
		{
			Name = FString::Printf(TEXT("0x%08X"), Entity->GetACEGuid());
		}
	}

	HoverTooltipText = Name;
	bHoverTooltipVisible = true;

	if (bRetailCursorInstalled && (!bRetailCursorInteractable || bPendingUseTargeting))
	{
		const int32 Target=bHitSelf ? Client->GetPlayerGuid() : Entity->GetACEGuid();
		ApplyRetailMouseCursor(true,!DatGameplayBinder || !bPendingUseTargeting
			|| DatGameplayBinder->IsPendingUseTargetCompatible(Target));
	}

	if (HoverTooltipWidget)
	{
		HoverTooltipWidget->SetTooltipText(Name);
		HoverTooltipWidget->SetTooltipScreenPosition(HoverCursorPixels);
		HoverTooltipWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void AACEPlayerController::CycleNearbyTarget(bool bEnemies, int32 Direction)
{
	if (!Client) return;
	const FVector Origin = Client->GetPlayerPosition().ToUnrealLocation(WorldScale);
	TArray<FACEWorldObject> Objects = Client->GetWorldObjects();
	Objects.RemoveAll([&](const FACEWorldObject& Obj)
	{
		return !Obj.IsSelectableWorldObject() || !Client->IsWorldObjectVisible(Obj)
			|| Obj.Guid == Client->GetPlayerGuid() || Obj.bIsPlayer
			|| (bEnemies ? !Obj.IsAttackable() : (Obj.ItemType & ACEItemType::Creature) != 0)
			|| FVector::DistSquared(Origin, Obj.Position.ToUnrealLocation(WorldScale)) > FMath::Square(60.f * WorldScale);
	});
	Objects.Sort([&](const FACEWorldObject& A, const FACEWorldObject& B)
	{
		const double DA = FVector::DistSquared(Origin, A.Position.ToUnrealLocation(WorldScale));
		const double DB = FVector::DistSquared(Origin, B.Position.ToUnrealLocation(WorldScale));
		return DA == DB ? static_cast<uint32>(A.Guid) < static_cast<uint32>(B.Guid) : DA < DB;
	});
	if (Objects.IsEmpty()) return;
	const int32 Current = Objects.IndexOfByPredicate([&](const FACEWorldObject& O) { return O.Guid == Client->GetSelectedObject().Guid; });
	const int32 Next = Current == INDEX_NONE || Direction == 0 ? 0 : (Current + Direction + Objects.Num()) % Objects.Num();
	Client->SelectObject(Objects[Next].Guid);
}

void AACEPlayerController::IdentifyAtScreenPosition(float MouseX, float MouseY)
{
	if (!Client || Client->GetSessionState() != EACESessionState::InWorld)
	{
		return;
	}
	if (IsMouseOverBlockingUI())
	{
		return;
	}
	FVector WorldOrigin, WorldDir;
	if (!DeprojectScreenPositionToWorld(MouseX, MouseY, WorldOrigin, WorldDir))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector TraceEnd = WorldOrigin + WorldDir * 200000.f;
	APawn* SelfPawn = GetPawn();
	const FACEVisibilityPick Pick = ResolveVisibilityPick(*World, WorldOrigin, TraceEnd, SelfPawn);
	const int32 SelfGuid = Client->GetPlayerGuid();
	if (Pick.Entity)
	{
		Client->SelectObject(Pick.Entity->GetACEGuid());
		Client->SendIdentifyObject(Pick.Entity->GetACEGuid());
	}
	else if (Pick.bHitSelf && SelfGuid != 0)
	{
		Client->SelectObject(SelfGuid);
		Client->SendIdentifyObject(SelfGuid);
	}
}

void AACEPlayerController::PollObjectClick()
{
	if (!Client || Client->GetSessionState() != EACESessionState::InWorld || bMouseLookActive)
	{
		return;
	}

	const bool bLeft = WasInputKeyJustPressed(EKeys::LeftMouseButton);
	if (!bLeft)
	{
		return;
	}

	float MouseX = 0.f, MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	if (IsMouseOverBlockingUI())
	{
		return;
	}

	if (DatGameplayBinder)
	{
		DatGameplayBinder->ClearChatEntryFocus();
	}

	FVector WorldOrigin, WorldDir;
	if (!DeprojectScreenPositionToWorld(MouseX, MouseY, WorldOrigin, WorldDir))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector TraceEnd = WorldOrigin + WorldDir * 200000.f;

	APawn* SelfPawn = GetPawn();
	const FACEVisibilityPick Pick = ResolveVisibilityPick(*World, WorldOrigin, TraceEnd, SelfPawn);
	AACEWorldEntityActor* Entity = Pick.Entity;
	const bool bHitSelf = Pick.bHitSelf;

	const int32 SelfGuid = Client->GetPlayerGuid();
	if (!Entity && !bHitSelf)
	{
		if (bLeft && DatGameplayBinder && DatGameplayBinder->GetPendingUseWithSourceGuid() != 0)
		{
			DatGameplayBinder->CancelPendingUseWith();
		}
		Client->SelectObject(0);
		LastLeftClickGuid = 0;
		LastLeftClickTime = 0.0;
		return;
	}

	const int32 Guid = Entity ? Entity->GetACEGuid() : (bHitSelf ? SelfGuid : 0);
	if (Guid == 0)
	{
		return;
	}

	// Dual-use (mana stone / key): single click completes UseWithTarget on self or world object.
	if (bLeft && bPendingUseTargeting && DatGameplayBinder)
	{
		Client->SelectObject(Guid);
		if (DatGameplayBinder->TryCompletePendingUseWithTarget(Guid))
		{
			LastLeftClickGuid = 0;
			LastLeftClickTime = 0.0;
			return;
		}
	}

	const FACESelectedObject PriorSel = Client->GetSelectedObject();
	const double Now = FPlatformTime::Seconds();
	// True double-click only — two presses on the same guid within the window.
	// Do NOT treat "already selected" as a free second click (that made any later click Use).
	constexpr double DoubleClickSeconds = 0.5;
	const bool bDouble = (Guid == LastLeftClickGuid)
		&& ((Now - LastLeftClickTime) < DoubleClickSeconds);
	LastLeftClickGuid = Guid;
	LastLeftClickTime = Now;

	// First click: select (QueryHealth keeps server target for the health bar).
	Client->SelectObject(Guid);
	if (bHitSelf && !Entity)
	{
		return;
	}
	if (bDouble)
	{
		InteractWithObject(Guid);
		// Consume the pair so a third click is a fresh select, not another Use.
		LastLeftClickGuid = 0;
		LastLeftClickTime = 0.0;
	}
	(void)PriorSel;
}

AACEWorldEntityActor* AACEPlayerController::PickWorldEntityUnderCursor() const
{
	float MouseX = 0.f, MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return nullptr;
	}
	return PickWorldEntityAtScreenPosition(MouseX, MouseY);
}

namespace ACEPickTargeting
{
	bool IsSameFloorPickTarget(
		const FACEPosition& PlayerPos, const FACEWorldObject& Obj, float WorldScale,
		float OutdoorZFudgeCm = 180.f)
	{
		if (!PlayerPos.IsValid() || !Obj.bHasPosition)
		{
			return true;
		}
		const uint32 PC = static_cast<uint32>(PlayerPos.CellId);
		const uint32 OC = static_cast<uint32>(Obj.Position.CellId);
		const bool bPlayerIndoor = (PC & 0xFFFFu) >= 0x0100u;
		const bool bObjIndoor = (OC & 0xFFFFu) >= 0x0100u;
		if (bPlayerIndoor && bObjIndoor)
		{
			return PC == OC;
		}
		const float Dz = FMath::Abs(
			PlayerPos.ToUnrealLocation(WorldScale).Z - Obj.Position.ToUnrealLocation(WorldScale).Z);
		return Dz <= OutdoorZFudgeCm;
	}
}

AACEWorldEntityActor* AACEPlayerController::PickWorldEntityAtScreenPosition(float ScreenX, float ScreenY) const
{
	if (IsVRActive()) { FVector Impact; return GetPawn()->FindComponentByClass<UACEVRComponent>()->AimTarget(Impact); }
	FVector WorldOrigin, WorldDir;
	if (!DeprojectScreenPositionToWorld(ScreenX, ScreenY, WorldOrigin, WorldDir))
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	AACEWorldEntityActor* Candidate = ResolveVisibilityPick(*World, WorldOrigin,
		WorldOrigin + WorldDir * 200000.f, GetPawn()).Entity;
	if (!Candidate || Candidate->GetParentGuid() != 0) return nullptr;
	return Candidate;
}

void AACEPlayerController::ShowCharacterCreationUI()
{
    if(!Client||!Client->GetSession()||!DatCanvasWidget||Client->GetSessionState()!=EACESessionState::CharacterSelect)return;
    if(Client->GetCharacters().Num()>=Client->GetSession()->GetCharacterSlotCount())return;
    if(DatCharSelectBinder){DatCharSelectBinder->Shutdown();DatCharSelectBinder=nullptr;}
    DatCanvasWidget->SetCharSelectBinder(nullptr);
    Client->GetUIFlow()->SetMode(ACEUI::EACEUIFlowMode::CharacterCreation);
    DatCharGenBinder=NewObject<UACEUICharGenBinder>(this);
    if(!DatCharGenBinder->Initialize(Client,DatCanvasWidget,this))
    {DatCharGenBinder->Shutdown();DatCharGenBinder=nullptr;ShowCharacterSelectUI(Client->GetCharacters(),Client->GetSession()->GetServerName());return;}
    DatCanvasWidget->SetCharGenBinder(DatCharGenBinder);
    // The existing canvas stays attached to the VR panel, so UpdatePanels will
    // not perform another input-mode handoff when character creation opens.
    if(IsVRActive()){ApplyInWorldInputMode();return;}
    FInputModeGameAndUI Mode;Mode.SetWidgetToFocus(DatCanvasWidget->TakeWidget());Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);bShowMouseCursor=true;
}
