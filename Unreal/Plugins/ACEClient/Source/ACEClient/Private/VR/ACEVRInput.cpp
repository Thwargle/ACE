#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRRetailSurface.h"
#include "VR/ACEVRWidgetInteraction.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACESession.h"
#include "Components/InputComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/WidgetComponent.h"
#include "MotionControllerComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

void UACEVRComponent::BindInput()
{
	if (bInputBound || !PC || !PC->InputComponent) return;
	auto* I = PC->InputComponent.Get();
	I->BindAxis(TEXT("VRMoveX"), this, &UACEVRComponent::MoveX);
	I->BindAxis(TEXT("VRMoveY"), this, &UACEVRComponent::MoveY);
	I->BindAxis(TEXT("VRTurnX"), this, &UACEVRComponent::TurnX);
	I->BindAxis(TEXT("VRScrollY"), this, &UACEVRComponent::ScrollY);
	I->BindAction(TEXT("VRInventory"), IE_Pressed, this, &UACEVRComponent::ToggleInventory);
	I->BindAction(TEXT("VRCombat"), IE_Pressed, this, &UACEVRComponent::ToggleCombat);
	I->BindAction(TEXT("VRSettings"), IE_Pressed, this, &UACEVRComponent::ToggleSettings);
	I->BindAction(TEXT("VRSpellWheel"), IE_Pressed, this, &UACEVRComponent::ToggleSpellWheel);
	I->BindAction(TEXT("VRSelect"), IE_Pressed, this, &UACEVRComponent::SelectPressed);
	I->BindAction(TEXT("VRPrevious"), IE_Pressed, this, &UACEVRComponent::PreviousSpell);
	I->BindAction(TEXT("VRLeftTrigger"), IE_Pressed, this, &UACEVRComponent::LeftTriggerDown);
	I->BindAction(TEXT("VRLeftTrigger"), IE_Released, this, &UACEVRComponent::LeftTriggerUp);
	I->BindAction(TEXT("VRRightTrigger"), IE_Pressed, this, &UACEVRComponent::RightTriggerDown);
	I->BindAction(TEXT("VRRightTrigger"), IE_Released, this, &UACEVRComponent::RightTriggerUp);
	I->BindAction(TEXT("VRLeftGrip"), IE_Pressed, this, &UACEVRComponent::LeftGripDown);
	I->BindAction(TEXT("VRLeftGrip"), IE_Released, this, &UACEVRComponent::LeftGripUp);
	I->BindAction(TEXT("VRRightGrip"), IE_Pressed, this, &UACEVRComponent::RightGripDown);
	I->BindAction(TEXT("VRRightGrip"), IE_Released, this, &UACEVRComponent::RightGripUp);
	I->BindAction(TEXT("VRJump"), IE_Pressed, this, &UACEVRComponent::JumpDown);
	I->BindAction(TEXT("VRJump"), IE_Released, this, &UACEVRComponent::JumpUp);
	bInputBound = true;
}

void UACEVRComponent::Trigger(bool bLeft, bool bPressed)
{
	if (!bActive) return;
	const int32 HandIndex=bLeft ? 0 : 1;
	if (!bPressed && bWheelTriggerConsumed[HandIndex]) { bWheelTriggerConsumed[HandIndex]=false; return; }
	if (bSpellWheelOpen)
	{
		if (bPressed) { bWheelTriggerConsumed[HandIndex]=true; if (bLeft==Settings->bLeftHanded) ConfirmWheelSpell(); }
		return;
	}
	auto* Pointer = bLeft ? LeftPointer.Get() : RightPointer.Get();
	if (auto* VRPointer = Cast<UACEVRWidgetInteraction>(Pointer)) VRPointer->RefreshHit();
	bool& Held = bLeft ? bLeftPointerPressed : bRightPointerPressed;
	if (!bPressed)
	{
		if (bLeft == Settings->bLeftHanded && bTriggerDrawing)
		{
			bTriggerDrawing = false; Grip(bLeft, false);
		}
		if (VitalsDragHand == (bLeft ? 0 : 1)) EndVitalsDrag();
		UIAimOverride = bLeft ? LeftAim : RightAim;
		if (Held && PC->DatGameplayBinder)
		{
			auto* Binder = PC->DatGameplayBinder.Get();
			auto* Hovered = Pointer->GetHoveredWidgetComponent();
			FVector2D Drop = Pointer->Get2DHitLocation();
			bool RetailDrop = Hovered == RetailPanel;
			if (Hovered == WristPanel || Hovered == VitalsPanel) Binder->CancelPointerGestures();
			if (Hovered == ChatPanel) { Drop = ChatRetail->ToCanvas(Drop); RetailDrop = true; }
			if (RetailDrop)
			{
				// A captured Slate pointer keeps the source window's geometry. Use
				// the actual destination when crossing from the canvas to a pinned window.
				Binder->UpdateSpellDrag(Drop); Binder->UpdateInventoryDrag(Drop);
				if (Binder->IsSpellDragActive()) Binder->TryFinishSpellDrag(Drop);
				if (Binder->IsInventoryDragActive()) Binder->TryFinishInventoryDrag(Drop);
			}
			else if (!Hovered && bTracking && (bLeft ? LeftAim : RightAim)->IsTracked())
			{
				// A fast drag can leave the source quad before it receives a move.
				Binder->UpdateInventoryDrag(FVector2D(-1000, -1000));
				if (Binder->IsInventoryDragActive()) Binder->TryFinishInventoryDrag(FVector2D(-1000, -1000));
				Binder->CancelPointerGestures();
			}
			else Binder->CancelPointerGestures();
		}
		if (Held) Pointer->ReleasePointerKey(EKeys::LeftMouseButton);
		if (Held && PC->DatCanvasWidget) PC->DatCanvasWidget->ResetPointerOwnership();
		UIAimOverride.Reset();
		Held = false; UpdateInteractionFeedback(bTracking && !bKeyboardOpen && !bSettingsOpen); return;
	}
	const auto* Aim = bLeft ? LeftAim.Get() : RightAim.Get();
	if (Client)
		UE_LOG(LogTemp, Log, TEXT("ACE VR trigger: hand=%s head=%d aim=%d hit=%s clickable=%d"),
			bLeft ? TEXT("left") : TEXT("right"), bTracking, Aim->IsTracked(), *GetNameSafe(Pointer->GetHoveredWidgetComponent()), Pointer->IsOverHitTestVisibleWidget());
	if (!bTracking || !Aim->IsTracked() || PC->bEnterWorldLoading || PC->bWorldRevealActive) return;
	auto* TextEntry = TextEntryUnderPointer(Pointer);
	if (UsesPlatformKeyboard() && TextEntry)
	{
		// Native editing provides caret/selection. Do not also send a Slate mouse
		// press: its permanent keyboard entry competes with the native session.
		FocusTextEntry(TextEntry); Pulse(bLeft, .2f); return;
	}
	if (bTextKeyboardOpen && Pointer->GetHoveredWidgetComponent() != KeyboardPanel)
	{
		DismissTextEntry(); UpdatePanels();
		if (!UsesPlatformKeyboard()) return;
	}
	if (Pointer->IsOverHitTestVisibleWidget())
	{
		if (bLeftPointerPressed || bRightPointerPressed) return;
		FeedbackHand = bLeft ? 0 : 1;
		auto* Entry = TextEntryUnderPointer(Pointer);
		Pointer->PressPointerKey(EKeys::LeftMouseButton); Held = true;
		Pulse(bLeft, .2f); UpdateInteractionFeedback(true);
		if (Entry) FocusTextEntry(Entry);
		return;
	}
	if (IsPointerNearPanel(bLeft)) return;
	if (bLeft != Settings->bLeftHanded) return;
	if (GetCombatMode() == ACECombatMode::Magic) { FireSpell(); return; }
	if (IsInputBlocked()) return;
	if (GetCombatMode() == ACECombatMode::Missile)
	{
		if (MissileStyle() == 0x20) FireCrossbow();
		else { Grip(bLeft, true); bTriggerDrawing = bDrawing; }
	}
	else if (GetCombatMode() == ACECombatMode::NonCombat)
	{
		FVector Hit;
		if (auto* Target = AimTarget(Hit))
		{
			Client->SelectObject(Target->GetACEGuid());
			Pulse(bLeft, .25f);
			// Only interact within reach; do not trigger automatic walking in VR.
			FACEWorldObject Object;
			if (Client->GetWorldObject(Target->GetACEGuid(), Object))
			{
				const FVector TargetFeet = Object.Position.ToUnrealLocation(PC->WorldScale);
				const FVector Feet = WeaponAim()->GetComponentLocation() - FACEPosition::AceVectorToUnreal(ToAceOffset(WeaponAim()->GetComponentLocation()), PC->WorldScale);
				if (PC->GetUseCylinderDistanceCm(Object, Feet, TargetFeet) <= Object.UseRadius * PC->WorldScale + 5.f)
					PC->InteractWithObject(Target->GetACEGuid());
			}
		}
		else { Client->SelectObject(0); Pulse(bLeft, .12f); }
	}
}

void UACEVRComponent::SelectPressed()
{
	if (bSpellWheelOpen) { ChangeWheelTab(1); return; }
	if (bLeftPointerPressed || bRightPointerPressed) return;
	if (!bTracking || PC->bEnterWorldLoading || PC->bWorldRevealActive) return;
	if (RightAim->IsTracked() && RightPointer->IsOverHitTestVisibleWidget())
	{
		if (RightPointer->GetHoveredWidgetComponent() == RetailPanel && PC->DatGameplayBinder
			&& PC->DatGameplayBinder->ActivateVRPointerItem(RightPointer->Get2DHitLocation()))
		{ FeedbackHand = 1; Pulse(false, .5f); UpdateInteractionFeedback(true); return; }
		Trigger(false, true); Trigger(false, false); return;
	}
	if (IsInputBlocked()) return;
	if (GetCombatMode() == ACECombatMode::Magic) { CycleSpell(1); return; }
	FVector Impact;
	if (auto* Target = AimTarget(Impact)) { Client->SelectObject(Target->GetACEGuid()); Pulse(Settings->bLeftHanded); }
	else Client->SelectObject(0);
}

void UACEVRComponent::PreviousSpell()
{
	if (bSpellWheelOpen) { ChangeWheelTab(-1); return; }
	// B is contextual in VR. Inspect the pointed item without a mouse press,
	// drag, use action, or a change to the selected spell.
	if (!bActive || !bTracking || !PC || PC->bEnterWorldLoading || PC->bWorldRevealActive
		|| bLeftPointerPressed || bRightPointerPressed || bTextKeyboardOpen || bSettingsOpen) return;
	for (const bool Left : {FeedbackHand == 0, FeedbackHand != 0})
	{
		auto* Aim = Left ? LeftAim.Get() : RightAim.Get();
		auto* Pointer = Left ? LeftPointer.Get() : RightPointer.Get();
		if (!Aim || !Aim->IsTracked() || !Pointer) continue;
		if (auto* VRPointer = Cast<UACEVRWidgetInteraction>(Pointer)) VRPointer->RefreshHit();
		if (Pointer->GetHoveredWidgetComponent() != RetailPanel) continue;
		if (PC->DatGameplayBinder && PC->DatGameplayBinder->InspectVRPointerItem(Pointer->Get2DHitLocation()))
		{
			FeedbackHand = Left ? 0 : 1; Pulse(Left, .35f); UpdateInteractionFeedback(true);
		}
		return; // Empty slots and window chrome do not change the combat hotbar.
	}
	if (!IsMenuOpen() && GetCombatMode()==ACECombatMode::NonCombat)
	{
		FVector Hit;
		if (auto* Target=AimTarget(Hit))
		{
			Client->SelectObject(Target->GetACEGuid());
			Client->SendIdentifyObject(Target->GetACEGuid()); Pulse(Settings->bLeftHanded,.3f);
		}
	}
	else if (!IsMenuOpen()) CycleSpell(-1);
}

void UACEVRComponent::Grip(bool bLeft, bool bPressed)
{
	if (bSpellWheelOpen) { if (bPressed) ChangeWheelPage(bLeft ? -1 : 1); return; }
	(bLeft ? bLeftGripHeld : bRightGripHeld) = bPressed;
	if (!bActive || bLeft != Settings->bLeftHanded) return;
	if (!bPressed)
	{
		if (bDrawing)
		{
			ReleaseArrow();
		}
		return;
	}
	if (IsInputBlocked() || GetCombatMode() != ACECombatMode::Missile || !WeaponGrip()->IsTracked()
		|| (IsAmmoLauncher() && !BowGrip()->IsTracked())) return;
	if (MissileStyle() == 0x20) return; // A crossbow is ready when bolts are equipped; no draw gesture.
	if (bDrawing) return;
	const bool BowLike = IsAmmoLauncher();
	if (BowLike && EquippedAmmo().Guid == 0) { SetCastFeedback(TEXT("Equip compatible arrows or bolts first.")); return; }
	const FVector Nock = GetPhysicalGrip(!Settings->bLeftHanded).GetLocation() - GetPhysicalAim(!Settings->bLeftHanded).GetUnitAxis(EAxis::X) * 12.f;
	if (BowLike && FVector::Distance(GetPhysicalGrip(Settings->bLeftHanded).GetLocation(), Nock) <= 30.f)
	{
		bDrawing = true; BowFraction = BowHoldTime = 0.f; Pulse(bLeft);
		SetCastFeedback(TEXT("Arrow nocked. Pull back, then release the grip or trigger."));
	}
	else if (!BowLike) { bDrawing = true; BowFraction = 1.f; BowHoldTime = 0.f; }
	else SetCastFeedback(TEXT("Bring your weapon hand to the arrow at the bow, then hold grip or trigger."));
}

void UACEVRComponent::JumpDown()
{
	if (!bActive || IsInputBlocked() || bSpellWheelOpen || bJumpHeld || PC->bJumpAirborne) return;
	bJumpHeld = true; JumpHoldSeconds = 0.f; PC->BeginJumpCharge();
}

void UACEVRComponent::JumpUp()
{
	if (!bActive || !bJumpHeld) return;
	bJumpHeld = false;
	if (!IsInputBlocked())
	{
		float Forward, Right; GetMovement(Forward, Right, PC->bRunning);
		UE_LOG(LogTemp, Log, TEXT("ACE VR jump released: hold=%.3f charge=%.3f"), JumpHoldSeconds, PC->JumpChargeExtent);
		PC->ReleaseJump(Forward, Right);
	}
	else { PC->bJumpCharging = false; PC->JumpChargeExtent = 0.f; }
	JumpHoldSeconds = 0.f;
}

void UACEVRComponent::CancelGestures()
{
	CloseSpellWheel();
	EndVitalsDrag();
	if (PC && PC->DatGameplayBinder) PC->DatGameplayBinder->CancelPointerGestures();
	if (LeftPointer && bLeftPointerPressed) LeftPointer->ReleasePointerKey(EKeys::LeftMouseButton);
	if (RightPointer && bRightPointerPressed) RightPointer->ReleasePointerKey(EKeys::LeftMouseButton);
	bLeftPointerPressed = bRightPointerPressed = false;
	bDrawing = bTriggerDrawing = false; BowFraction = BowHoldTime = 0.f;
	Swing.Reset();
	for (auto& Punch : Punches) Punch.Gesture.Reset();
	bPreviousContactBlade = false;
	bLeftGripHeld = bRightGripHeld = false;
	if (PC && bJumpHeld) { PC->bJumpCharging = false; PC->JumpChargeExtent = 0.f; }
	bJumpHeld = false; JumpHoldSeconds = 0.f;
}

void UACEVRComponent::Pulse(bool bLeft, float Strength)
{
	if (!Settings->bHaptics || !PC) return;
	PC->SetHapticsByValue(.6f, Strength, bLeft ? EControllerHand::Left : EControllerHand::Right);
	FTimerHandle Handle;
	TWeakObjectPtr<AACEPlayerController> WeakPC = PC;
	GetWorld()->GetTimerManager().SetTimer(Handle, [WeakPC, bLeft]()
	{ if (WeakPC.IsValid()) WeakPC->SetHapticsByValue(0.f, 0.f, bLeft ? EControllerHand::Left : EControllerHand::Right); }, .05f, false);
}
