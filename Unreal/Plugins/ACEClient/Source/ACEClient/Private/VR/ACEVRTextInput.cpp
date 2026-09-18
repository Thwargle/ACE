#include "VR/ACEVRComponent.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACERetailTextEntry.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraComponent.h"
#include "Components/EditableTextBox.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/WidgetComponent.h"
#include "VR/ACEVRRetailSurface.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "HAL/PlatformApplicationMisc.h"
#include "VR/ACEVRPlatformTextEntry.h"

bool UACEVRComponent::UsesPlatformKeyboard()
{
	return FPlatformApplicationMisc::RequiresVirtualKeyboard();
}

void UACEVRComponent::FocusTextEntry(UWidget* Entry)
{
	if (!bActive || !Entry || !RightPointer || !Head) return;
	if (UsesPlatformKeyboard() && PlatformTextEntry && FocusedTextEntry == Entry && bTextKeyboardOpen) return;
	const bool Opening = !bTextKeyboardOpen;
	FocusedTextEntry = Entry;
	// The VR pointer owns focus, and our session owns the native keyboard. Letting
	// both Slate's pointer handler and this method open it toggles Android Hide.
	auto& Slate = FSlateApplication::Get();
	const uint32 User = Slate.FindOrCreateVirtualUser(RightPointer->VirtualUserIndex)->GetUserIndex();
	Slate.SetUserFocus(User, Entry->TakeWidget(), EFocusCause::SetDirectly);
	if (UsesPlatformKeyboard())
	{
		if (Cast<UEditableTextBox>(Entry) || Cast<UACERetailTextEntry>(Entry))
		{
			auto TextSession = MakeShared<FACEVRPlatformTextEntry>(Entry, [WeakThis = TWeakObjectPtr<UACEVRComponent>(this)]
			{ if (WeakThis.IsValid()) WeakThis->DismissTextEntry(); });
			TextSession->EnableNativeSubmit();
			PlatformTextEntry = TextSession;
			Slate.ShowVirtualKeyboard(true, User, PlatformTextEntry);
		}
	}
	// Hardware input stays with the game. Both controller pointers type through
	// their shared virtual Slate user, so a chat field cannot swallow X/trigger.
	PC->ApplyInWorldInputMode();
	bTextKeyboardOpen = true;
	bKeyboardOpen = !UsesPlatformKeyboard();
	if (Opening)
	{
		const FRotator Facing(0, Head->GetComponentRotation().Yaw, 0);
		TextKeyboardTransform = FTransform(FRotator(15, Facing.Yaw + 180, 0),
			Head->GetComponentLocation() + Facing.Vector() * 100.f - FVector(0, 0, 20), FVector(.08f));
	}
}

void UACEVRComponent::UpdateTextEntryFocus()
{
	// A virtual window may restore its last text focus when its title bar,
	// close button or background is clicked. Focus alone is not typing intent.
	if (bTextKeyboardOpen && (!FocusedTextEntry.IsValid() || !FocusedTextEntry->IsVisible())) DismissTextEntry();
	if (bTextKeyboardOpen && UsesPlatformKeyboard() && !PlatformTextEntry && FocusedTextEntry.IsValid())
	{
		const auto Widget = FocusedTextEntry->GetCachedWidget();
		if (!Widget.IsValid() || (!Widget->HasAnyUserFocus() && !Widget->HasFocusedDescendants())) DismissTextEntry();
	}
}

UWidget* UACEVRComponent::TextEntryUnderPointer(UWidgetInteractionComponent* Pointer) const
{
	if (!Client || Client->GetSessionState() != EACESessionState::InWorld || !PC->DatCanvasWidget
		|| !PC->DatCanvasWidget->WidgetTree) return nullptr;
	auto* Panel = Pointer->GetHoveredWidgetComponent();
	if (Panel != RetailPanel && Panel != ChatPanel) return nullptr;
	FVector2D Point = Pointer->Get2DHitLocation();
	if (Panel == ChatPanel) Point = ChatRetail->ToCanvas(Point);
	const auto HitPath = RetailPanel->GetHitWidgetPath(Point, false);
	TArray<UWidget*> Widgets;
	PC->DatCanvasWidget->WidgetTree->GetAllWidgets(Widgets);
	for (auto* Widget : Widgets)
	{
		if (!Cast<UEditableTextBox>(Widget) && !Cast<UACERetailTextEntry>(Widget)) continue;
		const auto Slate = Widget->GetCachedWidget();
		if (Widget->IsVisible() && Slate.IsValid() && HitPath.ContainsByPredicate([&](const FWidgetAndPointer& Hit) { return Hit.Widget == Slate; })) return Widget;
	}
	return nullptr;
}

void UACEVRComponent::DismissTextEntry()
{
	if (PC && PC->DatGameplayBinder) PC->DatGameplayBinder->CancelPendingChatRefocus();
	if (!bTextKeyboardOpen && !FocusedTextEntry.IsValid()) return;
	// Clear only our virtual user, without disturbing captured item drags or
	// the physical viewport's input routing. Clearing focus never submits chat.
	if (RightPointer && FSlateApplication::IsInitialized())
	{
		auto& Slate = FSlateApplication::Get();
		if (UsesPlatformKeyboard()) Slate.ShowVirtualKeyboard(false, Slate.FindOrCreateVirtualUser(RightPointer->VirtualUserIndex)->GetUserIndex());
		Slate.ClearUserFocus(Slate.FindOrCreateVirtualUser(RightPointer->VirtualUserIndex)->GetUserIndex(), EFocusCause::Cleared);
	}
	FocusedTextEntry.Reset();
	PlatformTextEntry.Reset();
	bTextKeyboardOpen = bKeyboardOpen = false;
	if (PC) PC->ApplyInWorldInputMode();
}

FString UACEVRComponent::GetTextInputPreview() const
{
	if (const auto* Entry = Cast<UEditableTextBox>(FocusedTextEntry.Get()))
		return Entry->GetIsPassword() ? TEXT("Password entry / X closes keyboard") : Entry->GetText().ToString().Right(80) + TEXT("  |  X closes keyboard");
	if (const auto* Entry = Cast<UACERetailTextEntry>(FocusedTextEntry.Get()))
		return Entry->GetText().ToString().Right(80) + TEXT("  |  X closes keyboard");
	return TEXT("Click a text field with a trigger, then select keys");
}
