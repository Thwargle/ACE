#include "VR/ACEVRComponent.h"
#include "ACEVRNativeHUD.h"
#include "ACEClientBuild.h"
#include "ACEVRUIStyle.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRWidget.h"
#include "VR/ACEVRRetailSurface.h"
#include "VR/ACEVRWidgetInteraction.h"
#include "UI/ACEUIGameplayBinder.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACESession.h"
#include "ACELoadingScreenActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MotionControllerComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "RenderTimer.h"

static TAutoConsoleVariable<int32> CVarVRFrameTiming(TEXT("ace.VR.FrameTiming"), PLATFORM_ANDROID ? 1 : 0,
	TEXT("Log active VR frame, game, render and RHI timings every five seconds."));

bool AACEPlayerController::IsVRActive() const
{
	const auto* VR = GetPawn() ? GetPawn()->FindComponentByClass<UACEVRComponent>() : nullptr;
	return VR && VR->IsActive();
}

UACEVRComponent::UACEVRComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Update the rig after movement/animation, before the camera manager builds its view.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UACEVRComponent::BeginPlay()
{
	Super::BeginPlay();
	Settings = GetMutableDefault<UACEVRSettings>(); Settings->Sanitize();
	Client = GetWorld()->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
}

void UACEVRComponent::ActivateRig()
{
	if (bActive || !PC || !PC->IsLocalController() || !Client || !Settings) return;
	// Keep the same reference space for every posture. Switching Local/Stage
	// while recentering mixes poses from two different floors for one frame.
	UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Stage);
	TrackingCalibrationFrames = 3;
	if (auto* KeyboardMode = IConsoleManager::Get().FindConsoleVariable(TEXT("Android.NewKeyboard"))) KeyboardMode->Set(1, ECVF_SetByCode);
	DesktopCamera = GetOwner()->FindComponentByClass<UCameraComponent>();
	AddTickPrerequisiteActor(PC);
	if (auto* Appearance = GetOwner()->FindComponentByClass<UACECharacterAppearanceComponent>()) AddTickPrerequisiteComponent(Appearance);
	auto Scene = [this](USceneComponent* C, USceneComponent* Parent)
	{
		C->GetOwner()->AddInstanceComponent(C);
		C->SetupAttachment(Parent); C->RegisterComponent();
	};
	TrackingOrigin = NewObject<USceneComponent>(GetOwner(), TEXT("VRTrackingOrigin"));
	Scene(TrackingOrigin, GetOwner()->GetRootComponent());
	// Body heading and server rotate-to-target must never rotate the user's playspace.
	TrackingOrigin->SetUsingAbsoluteRotation(true);
	TrackingOrigin->SetUsingAbsoluteLocation(true);
	Head = NewObject<UCameraComponent>(GetOwner(), TEXT("VRHead")); Scene(Head, TrackingOrigin);
	Head->bLockToHmd = true; Head->bUsePawnControlRotation = false;
	if (DesktopCamera)
	{
		Head->PostProcessSettings = DesktopCamera->PostProcessSettings;
		Head->PostProcessBlendWeight = 1.f;
		DesktopCamera->Deactivate();
	}
	Head->Activate();
	InitializeComfortCurtain();
	// Desktop login/loading can disable the pawn's collision. Panels need their own
	// actor so their query bodies remain interactive without enabling player collision.
	FActorSpawnParameters PresentationParams;
	PresentationParams.Owner = GetOwner();
	PresentationParams.ObjectFlags |= RF_Transient;
	PresentationActor = GetWorld()->SpawnActor<AActor>(PresentationParams);
	auto* VisualRoot = NewObject<USceneComponent>(PresentationActor, TEXT("VRPresentationRoot"));
	PresentationActor->SetRootComponent(VisualRoot); Scene(VisualRoot, TrackingOrigin);
	auto Controller = [&](const TCHAR* Name, const TCHAR* Source)
	{
		auto* C = NewObject<UMotionControllerComponent>(PresentationActor, Name);
		C->SetTrackingMotionSource(Source); C->SetAssociatedPlayerIndex(0);
		Scene(C, VisualRoot); C->Activate(true); AddTickPrerequisiteComponent(C); return C;
	};
	LeftGrip = Controller(TEXT("VRLeftGrip"), TEXT("Left"));
	RightGrip = Controller(TEXT("VRRightGrip"), TEXT("Right"));
	LeftAim = Controller(TEXT("VRLeftAim"), TEXT("LeftAim"));
	RightAim = Controller(TEXT("VRRightAim"), TEXT("RightAim"));
	auto Pointer = [&](const TCHAR* Name, USceneComponent* Parent, int32 Index)
	{
		auto* P = NewObject<UACEVRWidgetInteraction>(PresentationActor, Name);
		P->VirtualUserIndex = 1; P->PointerIndex = Index; P->InteractionDistance = 500.f;
		P->PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
		P->AddTickPrerequisiteComponent(this);
		P->TraceChannel = ECC_Visibility; Scene(P, Parent); P->Activate(true); return P;
	};
	LeftPointer = Pointer(TEXT("VRLeftPointer"), LeftAim, 0);
	RightPointer = Pointer(TEXT("VRRightPointer"), RightAim, 1);
	auto Panel = [&](const TCHAR* Name, USceneComponent* Parent, FVector2D Size)
	{
		auto* P = NewObject<UWidgetComponent>(PresentationActor, Name);
		P->SetWidgetSpace(EWidgetSpace::World); P->SetDrawSize(Size); P->SetTwoSided(true);
		P->SetBlendMode(EWidgetBlendMode::Transparent); P->SetTickWhenOffscreen(true);
		// Redraw UI content independently of tracked panel/pointer motion.
		P->SetRedrawTime(PLATFORM_ANDROID ? 1.f / 36.f : 1.f / 60.f);
		P->SetCollisionEnabled(ECollisionEnabled::QueryOnly); P->SetCollisionResponseToAllChannels(ECR_Ignore);
		P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Scene(P, Parent); return P;
	};
	RetailPanel = Panel(TEXT("VRRetailPanel"), VisualRoot, FVector2D(1280, 960));
	SettingsPanel = Panel(TEXT("VRSettingsPanel"), VisualRoot, FVector2D(720, 1000));
	// Smoothed UI has its own transform, outside controller render-thread late update.
	WristPanel = Panel(TEXT("VRLeftWristSpells"), VisualRoot, FVector2D(500, 100));
	VitalsPanel = Panel(TEXT("VRPinnedVitals"), VisualRoot, FVector2D(480, 240));
	CompassPanel = Panel(TEXT("VRCompass"), VisualRoot, FVector2D(400, 500));
	FellowshipPanel = Panel(TEXT("VRFellowship"),VisualRoot,FVector2D(480,760));
	FellowshipPanel->SetManuallyRedraw(true);FellowshipPanel->SetCastShadow(false);
	FellowshipPanel->SetSlateWidget(SAssignNew(NativeFellowship,SACEVRFellowship).Rig(this).OnSelect([Weak=TWeakObjectPtr<UACEClientSubsystem>(Client)](int32 Guid)
		{if(Weak.IsValid())Weak->SelectObject(Guid);}));
	MenuControlsPanel = Panel(TEXT("VRMenuControls"), RetailPanel, FVector2D(480, 48));
	OptionsControlsPanel = Panel(TEXT("VROptionsControls"), SettingsPanel, FVector2D(480, 48));
	MenuControlsPanel->SetSlateWidget(SNew(SACEVRPanelControls).Rig(this).Panel("Menu"));
	OptionsControlsPanel->SetSlateWidget(SNew(SACEVRPanelControls).Rig(this).Panel("Options"));
	for (auto* Controls : {MenuControlsPanel.Get(), OptionsControlsPanel.Get()})
	{
		Controls->SetManuallyRedraw(true); Controls->SetCastShadow(false);
		Controls->SetTranslucentSortPriority(45);
	}
	CompassPanel->SetManuallyRedraw(true); CompassPanel->SetCastShadow(false);
	CompassPanel->SetTranslucentSortPriority(10);
	CompassPanel->SetRelativeLocationAndRotation(FVector(110,38,-16),FRotator(0,180,0));
	CompassPanel->SetRelativeScale3D(FVector(.065f));
	CompassPanel->SetSlateWidget(SAssignNew(NativeCompass,SACEVRCompass).Rig(this).OnSelect([Weak=TWeakObjectPtr<UACEClientSubsystem>(Client)](int32 Guid)
		{if(Weak.IsValid())Weak->SelectObject(Guid);}));
	ChatPanel = Panel(TEXT("VRPinnedChat"), VisualRoot, FVector2D(500, 200));
	JumpPanel = Panel(TEXT("VRJumpCharge"), Head, FVector2D(300, 50));
	KeyboardPanel = Panel(TEXT("VRKeyboard"), VisualRoot, FVector2D(1200, 400));
	FocusPanel = Panel(TEXT("VRInteractionFeedback"), PresentationActor->GetRootComponent(), FVector2D(540, 200));
	// Draw order and controller hit priority share these layers.
	RetailPanel->SetTranslucentSortPriority(0);
	ChatPanel->SetTranslucentSortPriority(5);
	VitalsPanel->SetTranslucentSortPriority(10);
	WristPanel->SetTranslucentSortPriority(20);
	SettingsPanel->SetTranslucentSortPriority(40);
	KeyboardPanel->SetTranslucentSortPriority(50);
	JumpPanel->SetTranslucentSortPriority(60);
	FocusPanel->SetTranslucentSortPriority(100);
	FocusPanel->SetManuallyRedraw(true);
	FocusPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FocusPanel->SetRelativeLocationAndRotation(FVector(85, 0, -14), FRotator(0, 180, 0));
	FocusPanel->SetWorldScale3D(FVector(.055f));
	PaintFocusPopup();
	FocusOutline = Panel(TEXT("VRWorldTargetFrame"), VisualRoot, FVector2D(300, 300));
	// Moving a panel transforms its mesh; its unchanged border texture only
	// needs painting once. This avoids a render-target pass every Quest frame.
	FocusOutline->SetManuallyRedraw(true);
	FocusOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FocusOutline->SetSlateWidget(SNew(SBorder).Visibility(EVisibility::HitTestInvisible)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("FocusRectangle"))).BorderBackgroundColor(FLinearColor(.95f, .73f, .33f)));
	FocusOutline->RequestRedraw();FocusPanel->RequestRedraw();
	// The keyboard's virtual window must not take focus from the text field
	// in another widget component when a non-focusable key is clicked.
	KeyboardPanel->SetWindowFocusable(false);
	MenuWidget = CreateWidget<UACEVRWidget>(PC); MenuWidget->VR = this;
	SettingsPanel->SetWidget(MenuWidget);
	WristWidget = CreateWidget<UACEVRWidget>(PC); WristWidget->VR = this; WristWidget->bWrist = true;
	WristRetail = CreateWidget<UACEVRRetailSurface>(PC); WristRetail->SetStatusSource(this); WristPanel->SetWidget(WristRetail);
	VitalsPanel->SetSlateWidget(SAssignNew(NativeVitals,SACEVRVitals).Rig(this));
	VitalsPanel->SetManuallyRedraw(true); VitalsPanel->SetCastShadow(false);
	ChatRetail = CreateWidget<UACEVRRetailSurface>(PC); ChatPanel->SetWidget(ChatRetail);
	JumpRetail = CreateWidget<UACEVRRetailSurface>(PC); JumpPanel->SetWidget(JumpRetail);
	if (!UsesPlatformKeyboard())
	{
		KeyboardWidget = CreateWidget<UACEVRWidget>(PC); KeyboardWidget->VR = this; KeyboardWidget->bKeyboard = true;
		KeyboardPanel->SetWidget(KeyboardWidget);
	}
	WristPanel->SetRelativeLocation(FVector(-6.f, 0.f, 12.f));
	WristPanel->SetRelativeRotation(FRotator(65.f, 180.f, 0.f));
	// Packaged fallback geometry is referenced directly, independent of template content.
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ACE/RuntimeMaterials/M_ACEVertexColor_v1.M_ACEVertexColor_v1"));
	auto Mesh = [&](const TCHAR* Name, UStaticMesh* Asset)
	{
		auto* M = NewObject<UStaticMeshComponent>(PresentationActor, Name); M->SetStaticMesh(Asset);
		if (Unlit) M->SetMaterial(0, Unlit);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision); M->SetCastShadow(false);
		Scene(M, VisualRoot); M->SetVisibility(false); return M;
	};
	for (int32 I = 0; I < 6; ++I)
		FallbackArms.Add(Mesh(*FString::Printf(TEXT("VRFallbackArm%d"), I), I % 3 == 2 ? Sphere : Cylinder));
	Arrow = Mesh(TEXT("VRNockedArrow"), Cylinder);
	for (int32 I = 0; I < 2; ++I)
	{
		BowStrings.Add(Mesh(*FString::Printf(TEXT("VRBowString%d"), I), Cylinder));
		PointerBeams.Add(Mesh(*FString::Printf(TEXT("VRPointerBeam%d"), I), Cylinder));
		PointerTips.Add(Mesh(*FString::Printf(TEXT("VRPointerTip%d"), I), Sphere));
	}
	if (auto* App = GetOwner()->FindComponentByClass<UACECharacterAppearanceComponent>())
		AddTickPrerequisiteComponent(App);
	bActive = true;
	ACEClientBuild::UpdateWindowTitle(GetWorld(), true);
	Client->OnVendorOpened.AddUniqueDynamic(this, &UACEVRComponent::RevealRetailDialog);
	Client->OnExternalContainerOpened.AddUniqueDynamic(this, &UACEVRComponent::RevealRetailDialog);
	Client->OnTradeStateChanged.AddUniqueDynamic(this, &UACEVRComponent::TradeChanged);
	Client->OnChatMessage.AddUniqueDynamic(this, &UACEVRComponent::CombatMessage);
	if (auto Session = Client->GetSession())
	{
		Session->OnNPCSpeech.AddUObject(this, &UACEVRComponent::NPCSpeech);
		Session->OnCombatFeedback.AddUObject(this, &UACEVRComponent::CombatFeedback);
		Session->OnHealthFeedback.AddUObject(this, &UACEVRComponent::HealthFeedback);
		Session->OnObjectHealth.AddUObject(this, &UACEVRComponent::EnemyHealth);
		Session->OnVitalsUpdated.AddUObject(this, &UACEVRComponent::VitalsFeedback);
	}
	ResetTrackingOrigin(); BindInput(); PositionPanel(RetailPanel); PositionPanel(SettingsPanel);
	PC->bAutoManageActiveCameraTarget = false;
	PC->SetViewTarget(GetOwner());
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
	Settings->ApplyRenderScale();
	Settings->ApplyEdgeSmoothing();
	UE_LOG(LogTemp, Log, TEXT("ACE VR: OpenXR rig active; left-stick movement, %s turning"), Settings->bSnapTurn ? TEXT("snap") : TEXT("smooth"));
}

void UACEVRComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(Dt, TickType, ThisTick);
	if (!PC) if (auto* Pawn = Cast<APawn>(GetOwner())) PC = Cast<AACEPlayerController>(Pawn->GetController());
	if (!PC || !PC->IsLocalController()) return;
	if (!bActive && Settings->bEnabled && UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()) ActivateRig();
	if (!bActive) return;
	const FVector PawnDelta = GetOwner()->GetActorLocation() - LastPawnLocation;
	const bool SmoothFloor = Client && Client->GetSessionState() == EACESessionState::InWorld
		&& !PC->bJumpAirborne && !PC->bEnterWorldLoading && !PC->bWorldRevealActive
		&& PawnDelta.SizeSquared() < FMath::Square(50.f);
	// Never accumulate a large floor lag while running along a slope. Retain
	// at most 3cm of step smoothing, and follow jumps/teleports exactly.
	const float NewVerticalOffset = SmoothFloor ? FMath::Clamp(FMath::FInterpTo(VerticalComfortOffset - PawnDelta.Z, 0.f, Dt, 12.f), -3.f, 3.f) : 0.f;
	if (!bPortalViewActive) TrackingOrigin->AddWorldOffset(PawnDelta + FVector(0, 0, NewVerticalOffset - VerticalComfortOffset));
	VerticalComfortOffset = NewVerticalOffset;
	LastPawnLocation = GetOwner()->GetActorLocation();
	BindInput();
	bool UsesFocus = false, HasFocus = true;
	UHeadMountedDisplayFunctionLibrary::GetVRFocusState(UsesFocus, HasFocus);
	// Quest's system keyboard owns input focus while the stereo scene remains
	// visible. Do not treat that overlay as an unworn headset and fade to black.
	const bool Tracked = (!UsesFocus || HasFocus || (bTextKeyboardOpen && UsesPlatformKeyboard())) && UHeadMountedDisplayFunctionLibrary::HasValidTrackingPosition()
		&& UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()
		&& UHeadMountedDisplayFunctionLibrary::GetHMDWornState() != EHMDWornState::NotWorn;
	if (Tracked && !bTracking)
	{
		// The camera's last view can still contain the startup pose when OpenXR
		// first gains focus. Seed the actual pose before positioning lobby UI.
		FRotator Orientation; FVector Position;
		UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(Orientation, Position);
		Head->SetRelativeLocationAndRotation(Position, Orientation);
	}
	UpdateTrackingState(Tracked);
	UpdateInventoryHold(Dt);
	if (Tracked && TrackingCalibrationFrames > 0 && --TrackingCalibrationFrames == 0) ResetTrackingOrigin();
	const bool Loading = PC->bEnterWorldLoading || PC->bWorldRevealActive;
	if (Loading != bWasLoading)
	{
		CancelGestures(); bWasLoading = Loading;
		bPortalViewActive = false;
		if (!Loading) ResetTrackingOrigin();
	}
	PC->SetViewTarget(GetOwner());
	PC->PortalWorldRevealElapsed = -1.f;
	if (PC->PlayerCameraManager) PC->PlayerCameraManager->UnlockFOV();
	// The desktop loading flow hides the entire pawn. The rig must keep rendering its camera/UI.
	if (GetOwner()->IsHidden()) GetOwner()->SetActorHiddenInGame(false);
	PC->bShowMouseCursor = false;
	if (Client && Client->GetSessionState() == EACESessionState::InWorld)
	{
		const int32 Player = Client->GetPlayerGuid(), Teleport = Client->GetTeleportSeq();
		if (Player != LastPlayer || Teleport != LastTeleport)
		{
			CancelGestures(); ResetTrackingOrigin();
			if (Player != LastPlayer) { SelectedSpell = 0; LastHello = -100; bInventoryOpen = bSettingsOpen = false; }
			LastPlayer = Player; LastTeleport = Teleport;
			WorldRefreshRemaining = 2; NextWorldRefresh = GetWorld()->GetTimeSeconds() + 2.f;
		}
		if (auto Session = Client->GetSession(); Session && !Session->SupportsVRCombat()
			&& GetWorld()->GetTimeSeconds() - LastHello >= 5.f)
		{ Session->RequestVRCapabilities(); LastHello = GetWorld()->GetTimeSeconds(); }
		if (!Loading && bTracking) Client->SetForcePositionReporting(true);
		if (!Loading && WorldRefreshRemaining > 0 && GetWorld()->GetTimeSeconds() >= NextWorldRefresh)
		{
			if (auto Session = Client->GetSession()) Session->RequestVRCapabilities();
			--WorldRefreshRemaining; NextWorldRefresh = GetWorld()->GetTimeSeconds() + 5.f;
		}
	}
	else if (LastPlayer != 0)
	{
		LastPlayer = 0; ResetTrackingOrigin();
	}
	if (bSpellWheelOpen) UpdateSpellWheel();
	else if (bWheelTurnNeutral)
	{
		// A stick held over a wheel sector must not become an immediate turn.
		if (TurnStick.Size()<.25f) { bWheelTurnNeutral=false; bTurnReady=true; }
	}
	else if (!IsInputBlocked())
	{
		const float X = ACEVRMath::DeadZone(TurnStick, Settings->StickDeadZone).X;
		if (Settings->bSnapTurn)
		{
			if (FMath::Abs(X) < .25f) bTurnReady = true;
			if (bTurnReady && FMath::Abs(X) > .7f) { RotateTracking(FMath::Sign(X) * Settings->SnapDegrees); bTurnReady = false; }
		}
		else if (FMath::Abs(X) > 0.f) RotateTracking(X * Settings->SmoothTurnDegreesPerSecond * FMath::Min(Dt, .05f));
	}
	else if (PanelEditHand == INDEX_NONE && FMath::Abs(TurnStick.Y) > .45f && GetWorld()->GetTimeSeconds() - LastScroll > .08f)
	{
		auto* P = RightPointer->IsOverHitTestVisibleWidget() ? RightPointer.Get() : LeftPointer.Get();
		P->ScrollWheel(TurnStick.Y > 0.f ? 1.f : -1.f); LastScroll = GetWorld()->GetTimeSeconds();
	}
	UpdatePortalView();
	UpdateHandContacts(Dt); UpdateTwoHandUse(Dt); UpdatePanels(Dt); UpdateArms(Dt); UpdateCombat(Dt); UpdatePortalEquipmentVisibility(); UpdateComfort(Dt);
	UpdateWorldNotices();
	if (Client && bTracking && !Loading && GetWorld()->GetTimeSeconds() >= NextPoseSendTime)
	{
		NextPoseSendTime = GetWorld()->GetTimeSeconds() + .05;
		if (const auto Session = Client->GetSession(); Session && Session->SupportsVRPoses())
		{
			FACEVRPose Pose; Pose.Cell = PC->GetEffectiveCellId();
			Pose.Flags = 4u | (LeftGrip->IsTracked() ? 1u : 0u) | (RightGrip->IsTracked() ? 2u : 0u)
				| (Settings->bLeftHanded ? 8u : 0u) | (bDrawing ? 16u : 0u);
			Pose.EyeHeight = AvatarEyeHeight / PC->WorldScale; Pose.Draw = bDrawing ? BowFraction : 0.f;
			const FVector Feet = GetOwner()->GetActorLocation() - FVector(0,0,GetOwner()->FindComponentByClass<UCapsuleComponent>()->GetScaledCapsuleHalfHeight());
			USceneComponent* Parts[] = {Head.Get(), LeftGrip.Get(), RightGrip.Get()};
			for (int32 I = 0; I < 3; ++I)
			{
				const FTransform Visual = I == 0 ? Parts[I]->GetComponentTransform() : GetAvatarGrip(I == 1);
				Pose.Poses[I] = FTransform(Visual.GetRotation(), (Visual.GetLocation()-Feet) / PC->WorldScale);
			}
			TArray<AActor*> Equipment; GetOwner()->GetAttachedActors(Equipment,true,true);
			for (auto* Actor:Equipment)
				if (auto* Item=Cast<AACEWorldEntityActor>(Actor); Item && Item->GetACEGuid()==EquippedMissileWeapon().Guid && !Item->IsHidden())
				{
					Pose.Weapon=Item->GetACEGuid();
					Pose.Poses[3]=FTransform(Item->GetActorQuat(),(Item->GetActorLocation()-Feet)/PC->WorldScale);break;
				}
			if (AmmoActor && !AmmoActor->IsHidden())
			{
				Pose.Ammo=AmmoVisualGuid;
				Pose.Poses[4]=FTransform(AmmoActor->GetActorQuat(),(AmmoActor->GetActorLocation()-Feet)/PC->WorldScale);
			}
			Session->SendVRPose(Pose);
		}
	}
	const double TimingNow = FPlatformTime::Seconds();
	const FVector TimingPosition = GetOwner()->GetActorLocation();
	const double TimingDelta = LastTimingFrameSeconds > 0 ? TimingNow - LastTimingFrameSeconds : 0.;
	LastTimingFrameSeconds = TimingNow;
	if (bTracking && !Loading && CVarVRFrameTiming.GetValueOnGameThread())
	{
		// World delta can be capped by the game; measure actual wall time.
		if (TimingDelta > .5) { TimingSeconds = TimingGame = TimingRender = TimingRHI = 0.; TimingFrames = 0; }
		else TimingSeconds += TimingDelta;
		++TimingFrames;
        TimingWorldSeconds += Dt;
        const double Distance = FVector::Dist2D(TimingPosition, LastTimingPosition);
        if (TimingDelta > 0 && TimingDelta < .25 && Distance < 200. && SmoothedMoveStick.Y > .85f && !IsInputBlocked())
        { TimingMoveSeconds += TimingDelta; TimingMoveDistance += Distance; }
		TimingGame += FPlatformTime::ToMilliseconds(GGameThreadTime);
		TimingRender += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		TimingRHI += FPlatformTime::ToMilliseconds(GRHIThreadTime);
		if (TimingSeconds >= 5.f)
		{
			UE_LOG(LogTemp, Log, TEXT("ACE VR frame timings: fps=%.1f frame=%.2fms game=%.2fms render=%.2fms rhi=%.2fms world=%d menus=%d"),
				TimingFrames / TimingSeconds, TimingSeconds * 1000. / TimingFrames, TimingGame / TimingFrames,
				TimingRender / TimingFrames, TimingRHI / TimingFrames,
				Client && Client->GetSessionState() == EACESessionState::InWorld, bInventoryOpen);
if (TimingMoveSeconds > .1 && Client)
                UE_LOG(LogTemp, Log, TEXT("ACE VR movement: actual=%.2fm/s requested=%.2fm/s run=%d rawStick=%.2f axis=%.2f scale=%.2f bodyScale=%.2f sim/wall=%.3f moving=%.2fs"),
                    TimingMoveDistance / TimingMoveSeconds / PC->WorldScale, Client->GetLocomotionSpeed(Settings->bRun) * PC->GetLocalCreatureScale(),
                    Settings->bRun, MoveStick.Y, SmoothedMoveStick.Y, Settings->MovementScale, PC->GetLocalCreatureScale(),
                    TimingSeconds > 0 ? TimingWorldSeconds / TimingSeconds : 0., TimingMoveSeconds);
            TimingMoveSeconds = TimingMoveDistance = TimingWorldSeconds = 0.;
            TimingSeconds = TimingGame = TimingRender = TimingRHI = 0.; TimingFrames = 0;
		}
	}
	LastTimingPosition = TimingPosition;
	if (FocusTitle!=PaintedFocusTitle || FocusHint!=PaintedFocusHint)
	{
		PaintedFocusTitle=FocusTitle;PaintedFocusHint=FocusHint;
		PaintFocusPopup();
	}
	const int32 TrackingStatus = (bTracking ? 1 : 0) | (LeftGrip->IsTracked() ? 2 : 0)
		| (RightGrip->IsTracked() ? 4 : 0) | (LeftAim->IsTracked() ? 8 : 0) | (RightAim->IsTracked() ? 16 : 0);
	if (TrackingStatus != LastTrackingStatus)
	{
		LastTrackingStatus = TrackingStatus;
		UE_LOG(LogTemp, Log, TEXT("ACE VR tracking: head=%d focus=%d left(grip=%d aim=%d) right(grip=%d aim=%d) localOwner=%d inputBound=%d panelCollision=%d"),
			bTracking, HasFocus, LeftGrip->IsTracked(), LeftAim->IsTracked(), RightGrip->IsTracked(), RightAim->IsTracked(),
			PresentationActor->HasLocalNetOwner(), bInputBound, static_cast<int32>(RetailPanel->GetCollisionEnabled()));
	}
}

void UACEVRComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	UpdateWorldSelectionHighlights(nullptr,nullptr);
	CancelGestures();
	DismissTextEntry();
	if (bActive && PC && PC->PlayerCameraManager) PC->PlayerCameraManager->StopCameraFade();
	if (Client)
	{
		Client->SetVRFellowshipUpdates(false);
		if (auto Session = Client->GetSession())
		{
			Session->OnNPCSpeech.RemoveAll(this); Session->OnCombatFeedback.RemoveAll(this); Session->OnHealthFeedback.RemoveAll(this); Session->OnVitalsUpdated.RemoveAll(this); Session->OnObjectHealth.RemoveAll(this);
		}
		Client->OnVendorOpened.RemoveDynamic(this, &UACEVRComponent::RevealRetailDialog);
		Client->OnExternalContainerOpened.RemoveDynamic(this, &UACEVRComponent::RevealRetailDialog);
		Client->OnTradeStateChanged.RemoveDynamic(this, &UACEVRComponent::TradeChanged);
		Client->OnChatMessage.RemoveDynamic(this, &UACEVRComponent::CombatMessage);
	}
	if (PresentationActor) PresentationActor->Destroy();
	if (AmmoActor) AmmoActor->Destroy();
	Super::EndPlay(Reason);
}

void UACEVRComponent::UpdateTrackingState(bool Tracked)
{
	if (Tracked == bTracking) return;
	HeldControllerActions.Reset();
	InventoryReleased();
	CancelGestures(); bTracking = Tracked;
	if (Tracked && Client && Client->GetSessionState() != EACESessionState::InWorld)
	{
		// The login surface was initially created before a worn headset had a pose.
		// Reposition once on focus acquisition; do not make it chase head turns.
		PositionPanel(RetailPanel); PositionPanel(SettingsPanel);
		UE_LOG(LogTemp, Log, TEXT("ACE VR lobby positioned from tracked head: head=%s panel=%s"),
			*Head->GetComponentLocation().ToString(), *RetailPanel->GetComponentLocation().ToString());
	}
}

bool UACEVRComponent::IsInputBlocked() const
{
	return !bTracking || !PC || !Client || Client->GetSessionState() != EACESessionState::InWorld
		|| PC->bEnterWorldLoading || PC->bWorldRevealActive || IsMenuOpen() || bTextKeyboardOpen || VitalsDragHand != INDEX_NONE || PanelEditHand != INDEX_NONE
		|| (Client->GetPlayerVitals().bValid && Client->GetPlayerVitals().Health <= 0);
}

FVector UACEVRComponent::GetBodyForward() const
{
	const auto* Source = Settings->MovementDirection == 2 ? TrackingOrigin.Get()
		: Settings->MovementDirection == 1 ? static_cast<USceneComponent*>(LeftAim.Get()) : Head.Get();
	FVector F = Source ? Source->GetForwardVector() : FVector::ForwardVector; F.Z = 0.f;
	return F.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
}

void UACEVRComponent::PrepareMovement(float Dt)
{
	if (!bActive) return;
	if (bJumpHeld)
	{
		if (IsInputBlocked())
		{
			bJumpHeld = false; JumpHoldSeconds = 0.f;
			PC->bJumpCharging = false; PC->JumpChargeExtent = 0.f;
		}
		else if (PC->bJumpCharging)
		{
			JumpHoldSeconds += FMath::Max(0.f, Dt);
			PC->JumpChargeExtent = FMath::Clamp(JumpHoldSeconds, 0.f, 1.f);
		}
		if (PC->DatGameplayBinder) PC->DatGameplayBinder->SetJumpChargeFraction(PC->JumpChargeExtent);
	}
	if (UHeadMountedDisplayFunctionLibrary::HasValidTrackingPosition())
	{
		FRotator Rotation; FVector Position;
		UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(Rotation, Position);
		Head->SetRelativeLocationAndRotation(Position, Rotation);
	}
	// Touch sticks commonly stop around 0.93 at the gate. Reach the retail
	// maximum at the outer 10%, without increasing the character's run speed.
	const FVector2D Desired = IsInputBlocked() ? FVector2D::ZeroVector : ACEVRMath::AssistForward(
		ACEVRMath::DeadZone(MoveStick, Settings->StickDeadZone, .9f), Settings->ForwardAssistDegrees);
	const float Alpha = Settings->MovementSmoothing <= 0.f ? 1.f : 1.f - FMath::Exp(-FMath::Min(Dt, .1f) / Settings->MovementSmoothing);
	SmoothedMoveStick = IsInputBlocked() ? FVector2D::ZeroVector : FMath::Lerp(SmoothedMoveStick, Desired, Alpha);
	if (Desired.IsNearlyZero() && SmoothedMoveStick.SizeSquared() < .0001f) SmoothedMoveStick = FVector2D::ZeroVector;
}

void UACEVRComponent::GetMovement(float& Forward, float& Right, bool& Running) const
{
	// The controller's normal movement path applies current Run skill and burden.
	// Walk/run is a persistent preference, independent of combat grip gestures.
	Forward = Right = 0.f; Running = Settings->bRun;
	if (IsInputBlocked()) return;
	const FVector2D V = SmoothedMoveStick * Settings->MovementScale;
	Forward = V.Y; Right = V.X;
}

FVector UACEVRComponent::GetRoomScaleDelta() const
{
	if (IsInputBlocked() || MoveStick.Size() > Settings->StickDeadZone || PC->bJumpAirborne) return FVector::ZeroVector;
	FVector Delta = Head->GetComponentLocation() - GetOwner()->GetActorLocation(); Delta.Z = 0.f;
	return Delta.GetClampedToMaxSize(20.f);
}

void UACEVRComponent::CompensateRoomScale(const FVector& ActualDelta)
{
	if (!TrackingOrigin) return;
	FVector Delta = ActualDelta; Delta.Z = 0.f;
	TrackingOrigin->AddWorldOffset(-Delta);
}

void UACEVRComponent::RotateTracking(float Degrees)
{
	const FVector Pivot = Head->GetComponentLocation();
	const FQuat Rotation(FVector::UpVector, FMath::DegreesToRadians(Degrees));
	const FVector NewLocation = Pivot + Rotation.RotateVector(TrackingOrigin->GetComponentLocation() - Pivot);
	TrackingOrigin->SetWorldLocationAndRotation(NewLocation, Rotation * TrackingOrigin->GetComponentQuat());
	if (bVitalsAnchorReady && Settings->VitalsAnchorMode == 1)
	{
		// Stick turns transport the HUD with the playspace, just like walking.
		// Only physical head turns use the stabilized anchor's dead zone.
		VitalsAnchorFrame.SetLocation(Pivot + Rotation.RotateVector(VitalsAnchorFrame.GetLocation() - Pivot));
		VitalsAnchorFrame.SetRotation(Rotation * VitalsAnchorFrame.GetRotation());
	}
	if (bCompassAnchorReady && Settings->CompassAnchorMode == 1)
	{
		CompassAnchorFrame.SetLocation(Pivot + Rotation.RotateVector(CompassAnchorFrame.GetLocation() - Pivot));
		CompassAnchorFrame.SetRotation(Rotation * CompassAnchorFrame.GetRotation());
	}
	if (bFellowshipAnchorReady && Settings->FellowshipAnchorMode==1)
	{
		FellowshipAnchorFrame.SetLocation(Pivot+Rotation.RotateVector(FellowshipAnchorFrame.GetLocation()-Pivot));
		FellowshipAnchorFrame.SetRotation(Rotation*FellowshipAnchorFrame.GetRotation());
	}
	Swing.Reset(); bPreviousContactBlade = false;
	for (auto& Punch : Punches) Punch.Gesture.Reset();
}

void UACEVRComponent::ResetTrackingOrigin()
{
	if (!bActive) return;
	ResetHandContacts();
	EndPanelEdit(); EndVitalsDrag();
	bVitalsAnchorReady = false; bCompassAnchorReady = false; bFellowshipAnchorReady=false;
	DismissTextEntry();
	FRotator Orientation; FVector Position;
	UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(Orientation, Position);
	// Seed the new camera before placing menus; its first view update has not run at activation.
	Head->SetRelativeLocationAndRotation(Position, Orientation);
	const float Half = GetOwner()->FindComponentByClass<UCapsuleComponent>()->GetScaledCapsuleHalfHeight();
	const FRotator Yaw(0.f, GetOwner()->GetActorRotation().Yaw + 90.f - Orientation.Yaw, 0.f);
	float CharacterEyeHeight = 175.75f;
	if (auto* App = GetOwner()->FindComponentByClass<UACECharacterAppearanceComponent>())
	{
		FTransform Bind;
		if (App->GetPartBindTransform(16, Bind))
			CharacterEyeHeight = Bind.TransformPosition(FVector(0, -.08f, .17f) * PC->WorldScale).Z;
	}
	const float TargetHeight = Settings->bSeated ? Settings->SeatedEyeHeight
		: Settings->bMatchCharacterHeight ? CharacterEyeHeight : float(Position.Z);
	RecenterHeight = TargetHeight - Position.Z + Settings->EyeHeightOffset;
	AvatarEyeHeight = FMath::Clamp(float(Position.Z) + RecenterHeight, 100.f, 210.f);
	UE_LOG(LogTemp, Log, TEXT("ACE VR recenter: trackedHeight=%.1f eyeHeight=%.1f seated=%d capsuleHalf=%.1f"), Position.Z, AvatarEyeHeight, Settings->bSeated, Half);
	TrackingOrigin->SetWorldRotation(Yaw);
	LastPawnLocation = GetOwner()->GetActorLocation();
	// Desktop login puts the pawn at world zero, inside the template floor. Keep
	// the VR lobby above that geometry; world entry restores the real player floor.
	const float LobbyHeight = Client && Client->GetSessionState() == EACESessionState::InWorld ? 0.f : 1000.f;
	VerticalComfortOffset = 0.f; bWristPoseReady = false; SmoothedMoveStick = FVector2D::ZeroVector;
	TrackingOrigin->SetWorldLocation(GetOwner()->GetActorLocation() + FVector(0, 0, -Half + RecenterHeight + LobbyHeight)
		- Yaw.RotateVector(FVector(Position.X, Position.Y, 0.f)));
	CancelGestures();
	if (bSettingsOpen) SettingsLayoutFrame = FTransform(FRotator(0,Head->GetComponentRotation().Yaw,0),Head->GetComponentLocation());
	PositionPanel(RetailPanel); PositionPanel(SettingsPanel);
}

void UACEVRComponent::UpdatePortalView()
{
	if (!bWasLoading || !PC->LoadingScreenActor || !PC->LoadingScreenActor->bMeshReady) return;
	auto* Portal = PC->LoadingScreenActor.Get();
	Portal->SetTrackedView(true);
	if (!bPortalViewActive)
	{
		// Retain the retail tunnel geometry/animation, with an independently
		// tracked HMD. Do not force its desktop camera roll or FOV on the eyes.
		FRotator Orientation; FVector Position;
		UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(Orientation, Position);
		const FQuat Facing = FRotator(0, Portal->Camera->GetComponentRotation().Yaw - Orientation.Yaw, 0).Quaternion();
		PortalTrackingTransform = FTransform(Facing, Portal->Camera->GetComponentLocation() - Facing.RotateVector(Position));
		bPortalViewActive = true;
	}
	TrackingOrigin->SetWorldTransform(PortalTrackingTransform);
	Portal->SetActorHiddenInGame(false);
}

bool UACEVRComponent::HideEquipmentForPortal(AACEWorldEntityActor* Item)
{
	if (!Item || !PC || !(PC->bEnterWorldLoading || PC->bWorldRevealActive)) return false;
	if (!PortalEquipmentVisibility.Contains(Item)) PortalEquipmentVisibility.Add(Item, Item->IsHidden());
	Item->SetActorHiddenInGame(true);
	return true;
}

void UACEVRComponent::UpdatePortalEquipmentVisibility()
{
	const bool Loading = PC && (PC->bEnterWorldLoading || PC->bWorldRevealActive);
	if (Loading)
	{
		TArray<AActor*> Attached;
		GetOwner()->GetAttachedActors(Attached, true, true);
		for (auto* Actor : Attached)
			if (auto* Item = Cast<AACEWorldEntityActor>(Actor))
			{
				// Equipment can arrive or be reattached while the destination loads.
				HideEquipmentForPortal(Item);
			}
		return;
	}
	for (const auto& Entry : PortalEquipmentVisibility)
		if (auto* Item = Entry.Key.Get()) Item->SetActorHiddenInGame(Entry.Value);
	PortalEquipmentVisibility.Reset();
}

void UACEVRComponent::InitializeComfortCurtain()
{
	// Mobile LDR skips the camera manager's post-process fade. This overlay
	// covers penetrated geometry without enabling the expensive HDR pipeline.
	auto* Dat = GetWorld()->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
	if (auto* Base = Dat ? Dat->GetVRComfortMaterial() : nullptr)
	{
		ComfortCurtain=NewObject<UProceduralMeshComponent>(GetOwner(),TEXT("VRComfortCurtain"));
		GetOwner()->AddInstanceComponent(ComfortCurtain);ComfortCurtain->SetupAttachment(Head);ComfortCurtain->RegisterComponent();
		ComfortCurtain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ComfortCurtain->SetCastShadow(false);ComfortCurtain->SetTranslucentSortPriority(32000);
		ComfortCurtain->CreateMeshSection(0,{FVector(20,-200,-200),FVector(20,200,-200),FVector(20,200,200),FVector(20,-200,200)},
			{0,2,1,0,3,2},{},{},{},{},false);
		ComfortMaterial=UMaterialInstanceDynamic::Create(Base,this);
		ComfortMaterial->SetScalarParameterValue(TEXT("OpacityMul"),1.f);
		// Prime the cover during rig startup, then fade into the tracked view.
		// Its first GPU use must precede gameplay occlusion, not coincide with it.
		ComfortCurtain->SetMaterial(0,ComfortMaterial);ComfortCurtain->SetVisibility(true);
		Fade=1.f;
	}
}

void UACEVRComponent::UpdateComfort(float Dt)
{
	bool Occluded = false;
	if (bTracking && Client && Client->GetSessionState() == EACESessionState::InWorld && !bWasLoading)
	{
		FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(VRHeadCollision), false, GetOwner());
		TArray<AActor*> Attached; GetOwner()->GetAttachedActors(Attached, true, true);
		for (auto* Actor : Attached) Q.AddIgnoredActor(Actor);
		const FVector Center = GetOwner()->GetActorLocation();
		Occluded = GetWorld()->SweepSingleByChannel(Hit, Center, Head->GetComponentLocation(), FQuat::Identity,
			ECC_Camera, FCollisionShape::MakeSphere(9.f), Q);
		// Water shares the outdoor heightfield with land. Its surface is not a
		// solid ceiling: retail settles the player below it by water depth.
		// Ignore only a wet terrain hit, then repeat to retain walls/rocks beyond it.
		auto* Dat = GetWorld()->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
		for (int32 Attempt=0; Occluded && Attempt<4; ++Attempt)
		{
			auto* Surface=Hit.GetComponent();
			if (!Dat || !Surface || !Surface->ComponentHasTag(TEXT("ACEOutdoorTerrain"))
				|| Dat->GetOutdoorWaterDepthCm(Hit.ImpactPoint.X,Hit.ImpactPoint.Y,PC->WorldScale)<=0.f) break;
			Q.AddIgnoredComponent(Surface);
			Occluded=GetWorld()->SweepSingleByChannel(Hit,Center,Head->GetComponentLocation(),FQuat::Identity,
				ECC_Camera,FCollisionShape::MakeSphere(9.f),Q);
		}
		// A heightfield is one-sided. If a low frame rate or a room-scale lean
		// puts both trace endpoints below it, a sweep can miss its back face.
		// Check the eyes themselves, and never apply outdoor height to interiors.
		if (!Occluded && (uint32(PC->GetEffectiveCellId()) & 0xffffu) < 0x100u)
		{
			const FVector Eye = Head->GetComponentLocation();
			float GroundZ;
			if (Dat && Dat->SampleOutdoorGroundZ(Eye.X, Eye.Y, PC->WorldScale, GroundZ))
				Occluded = Eye.Z < GroundZ + 12.f;
		}
	}
	const float Desired = !bTracking || (bWasLoading && !bPortalViewActive) || Occluded ? 1.f : 0.f;
	// Do not expose several frames of the void after crossing solid geometry.
	Fade = Occluded ? 1.f : FMath::FInterpConstantTo(Fade, Desired, Dt, Desired > Fade ? 12.f : 3.f);
	if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetManualCameraFade(Fade, FLinearColor::Black, false);
	if (ComfortCurtain && ComfortMaterial)
	{
		ComfortCurtain->SetVisibility(Fade > .001f);
		ComfortMaterial->SetScalarParameterValue(TEXT("OpacityMul"),Fade);
	}
}

void UACEVRComponent::PaintFocusPopup()
{
	FocusPanel->SetSlateWidget(ACEVRUIStyle::Frame(SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ACEVRUIStyle::Text(Client, FocusTitle, 34, 506, ACEVRUIStyle::Gold)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[ACEVRUIStyle::Text(Client, FocusHint, 28, 506)]));
	FocusPanel->RequestRedraw();
}
