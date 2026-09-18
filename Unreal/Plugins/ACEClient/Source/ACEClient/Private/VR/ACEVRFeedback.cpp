#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRRetailSurface.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/WidgetComponent.h"
#include "MotionControllerComponent.h"
#include "EngineUtils.h"

bool UACEVRComponent::ShouldShowVitalsControls() const
{
	return IsMenuOpen() || VitalsDragHand != INDEX_NONE || (VitalsPanel &&
		((LeftPointer && LeftPointer->GetHoveredWidgetComponent() == VitalsPanel) ||
		 (RightPointer && RightPointer->GetHoveredWidgetComponent() == VitalsPanel)));
}

void UACEVRComponent::UpdateWorldSelectionHighlights(AACEWorldEntityActor* Selection, AACEWorldEntityActor* Hover)
{
	if (Selection && (Selection->IsHidden() || !Selection->IsCellVisible())) Selection=nullptr;
	if (Hover && (Hover->IsHidden() || !Hover->IsCellVisible())) Hover=nullptr;
	for (auto Previous : {HighlightedSelection,HighlightedHover})
		if (auto* Actor=Previous.Get(); Actor && Actor!=Selection && Actor!=Hover) Actor->SetSelectionHighlight(0.f);
	HighlightedSelection=Selection; HighlightedHover=Hover;
	if (Hover && Hover!=Selection) Hover->SetSelectionHighlight(.45f);
	if (Selection) Selection->SetSelectionHighlight(1.f);
}

void UACEVRComponent::UpdateInteractionFeedback(bool Available)
{
	FocusPanel->SetVisibility(false); FocusOutline->SetVisibility(false);
	if (PC->DatCanvasWidget) PC->DatCanvasWidget->ClearVRPointerFeedback();
	if (!Available || !Client) { UpdateWorldSelectionHighlights(nullptr,nullptr); return; }
	const auto Selection = Client->GetSelectedObject();
	if (LastFocusGuid != Selection.Guid)
	{
		LastFocusGuid = Selection.Guid; SelectedWorldTarget.Reset(); FACEWorldObject Object;
		if (Client->GetWorldObject(Selection.Guid, Object) && Object.bHasPosition)
			for (TActorIterator<AACEWorldEntityActor> It(GetWorld()); It; ++It)
				if (It->GetACEGuid() == Selection.Guid) { SelectedWorldTarget = *It; break; }
	}
	auto* Pointer = FeedbackHand == 0 ? LeftPointer.Get() : RightPointer.Get();
	if (!Pointer->IsOverHitTestVisibleWidget()) Pointer = FeedbackHand == 0 ? RightPointer.Get() : LeftPointer.Get();
	if (Pointer->IsOverHitTestVisibleWidget())
	{
		UpdateWorldSelectionHighlights(SelectedWorldTarget.Get(),nullptr);
		if (Pointer->GetHoveredWidgetComponent() == RetailPanel && PC->DatCanvasWidget)
		{
			PC->DatCanvasWidget->SetVRPointerFeedback(Pointer->Get2DHitLocation(), Pointer == LeftPointer ? bLeftPointerPressed : bRightPointerPressed);
			FocusTitle = PC->DatCanvasWidget->GetVRPointerLabel(); FocusHint = PC->DatCanvasWidget->GetVRPointerHint();
			FocusPanel->SetVisibility(!FocusTitle.IsEmpty());
			PlaceFocusPopup(Pointer->GetLastHitResult().ImpactPoint, Pointer->Get2DHitLocation().X > RetailPanel->GetDrawSize().X * .7f);
		}
		return;
	}
	if (IsMenuOpen()) { UpdateWorldSelectionHighlights(SelectedWorldTarget.Get(),nullptr); return; }
	// In combat the attack preview owns the highlight. Peace-mode interaction
	// help must not cover the projectile endpoint or overwrite that highlight.
	if (GetCombatMode() != ACECombatMode::NonCombat) return;

	FVector Impact = FVector::ZeroVector;
	auto* Hover = AimTarget(Impact);
	// Keep a speaking NPC unlit until the pointer leaves it. Loot remains selected.
	if (Hover && Hover->GetACEGuid() == ConversationHoverGuid) Hover = nullptr;
	else ConversationHoverGuid = 0;
	UpdateWorldSelectionHighlights(SelectedWorldTarget.Get(),Hover);
	auto* Target = Hover ? Hover : SelectedWorldTarget.Get();
	FACEWorldObject Object;
	if (!Target || Target->IsHidden() || !Target->IsCellVisible() || !Client->GetWorldObject(Target->GetACEGuid(),Object)) return;
	FocusTitle = Object.Name;
	if (GetCombatMode() == ACECombatMode::NonCombat)
	{
		const FVector Feet = GetOwner()->GetActorLocation() - FVector(0,0,GetOwner()->FindComponentByClass<UCapsuleComponent>()->GetScaledCapsuleHalfHeight());
		const float Distance = PC->GetUseCylinderDistanceCm(Object,Feet,Target->GetActorLocation());
		const float Reach = FMath::Max(0.f,Object.UseRadius * PC->WorldScale);
		FocusHint = Distance <= Reach + 5.f ? TEXT("Trigger: interact  |  A: select")
			: FString::Printf(TEXT("Move %.1f m closer to interact  |  A: select"),FMath::Max(0.f,Distance-Reach)/PC->WorldScale);
	}
	else FocusHint = TEXT("Target selected");
	PlaceFocusPopup(Hover ? Impact : Target->GetActorLocation()+FVector(0,0,Target->GetMeleeBodyHeight()*.5f),false);
	FocusPanel->SetVisibility(true);
}

void UACEVRComponent::PlaceFocusPopup(const FVector& Point, bool ToLeft)
{
	const FVector Ray = Point - Head->GetComponentLocation();
	const float Distance = FMath::Clamp(float(Ray.Size()) - 6.f, 60.f, 160.f);
	const float Scale = .085f * Distance / 100.f;
	const FVector Offset = Head->GetRightVector() * ((ToLeft ? -1.f : 1.f) * 310.f * Scale)
		+ Head->GetUpVector() * (85.f * Scale);
	const FVector Location = Head->GetComponentLocation() + Ray.GetSafeNormal() * Distance + Offset;
	FocusPanel->SetWorldScale3D(FVector(Scale));
	FocusPanel->SetWorldLocationAndRotation(Location, (Head->GetComponentLocation() - Location).Rotation());
}
