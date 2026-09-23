#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "Components/CanvasPanel.h"
#include "Components/Border.h"

bool UACEUIGameplayBinder::GetVRPointerFeedback(FVector2D Point, FBox2D& Bounds, FString& Label, FString& Hint, int32& Guid) const
{
	Guid = 0; Label.Empty(); Hint.Empty();
	if (!Canvas || !Manager) return false;
	const auto Geometry = Canvas->GetCachedGeometry();
	const FVector2D Absolute = Geometry.LocalToAbsolute(Point);
	auto Items = [&](const auto& Widgets, const auto& Guids, const TCHAR* Action)
	{
		for (int32 I = 0; I < Widgets.Num() && I < Guids.Num(); ++I)
			if (Guids[I] && Canvas->IsWidgetExposedAt(Widgets[I], Absolute))
			{
				const auto ItemGeometry = Widgets[I]->GetCachedGeometry();
				Bounds = FBox2D(Geometry.AbsoluteToLocal(ItemGeometry.GetAbsolutePosition()),
					Geometry.AbsoluteToLocal(ItemGeometry.LocalToAbsolute(ItemGeometry.GetLocalSize())));
				Guid = Guids[I]; FACEWorldObject Item;
				if (Client && Client->GetWorldObject(Guid, Item)) Label = Item.Name;
				const bool Selected = Client && Client->GetSelectedObject().Guid == Guid;
				if (Selected) Label = TEXT("Selected: ") + Label;
				Hint = Action; return true;
			}
		return false;
	};
	if (Items(ExtItemSlots, ExtItemGuids, TEXT("Trigger: select / A: take / B: inspect / Hold trigger: drag"))
		|| Items(InventorySlots, InventorySlotGuids, TEXT("Trigger: select / A: use or equip / B: inspect / Hold trigger: drag"))
		|| Items(PackSlots, PackSlotGuids, TEXT("Trigger: select / A: open / B: inspect / Hold trigger: drag"))
		|| Items(VendorItemSlots, VendorItemGuids, TEXT("Trigger: select / B: inspect / Double click: add to cart"))
		|| Items(TradeSelfSlots, TradeSelfGuids, TEXT("Trigger: select / B: inspect / Hold trigger: drag"))
		|| Items(TradeOtherSlots, TradeOtherGuids, TEXT("Trigger: select / B: inspect"))) return true;
	if (Client)
		if (const int32 Index = HitTestShortcutSlot(Point); Index != INDEX_NONE)
		{
			const auto El = Manager->FindElementByName(FString::Printf(TEXT("ShortcutBar%s_Shortcut%dButton"), Index < 9 ? TEXT("") : TEXT("2"), Index % 9 + 1));
			const FIntPoint Origin = El->GetScreenOrigin();
			Bounds = FBox2D(Canvas->LayoutToViewport(FVector2D(Origin)), Canvas->LayoutToViewport(FVector2D(Origin + FIntPoint(El->Width, El->Height))));
			Guid = Client->GetShortcutObject(Index);
			FACEWorldObject Item;
			if (Client->GetWorldObject(Guid, Item)) Label = Item.Name;
			Hint = TEXT("Trigger: select / A: use / B: inspect / Hold trigger: move shortcut");
			return true;
		}
	const FVector2D Layout = Canvas->ViewportToLayout(Point);
	const auto Element = Manager->HitTestCanvas(FMath::FloorToInt(Layout.X), FMath::FloorToInt(Layout.Y));
	if (!Element) return false;
	const FIntPoint Origin = Element->GetScreenOrigin();
	Bounds = FBox2D(Canvas->LayoutToViewport(FVector2D(Origin)), Canvas->LayoutToViewport(FVector2D(Origin + FIntPoint(Element->Width, Element->Height))));
	if (Element->ElementName.Contains(TEXT("Close"))) { Label = TEXT("Close window"); Hint = TEXT("Trigger: close / X: dismiss menus"); }
	else { Hint = TEXT("Trigger: click / Hold title bar: move / X: dismiss menus"); }
	return Bounds.GetSize().X > 0 && Bounds.GetSize().Y > 0;
}

float UACEUIGameplayBinder::GetVRSecondClickFraction(int32 Guid) const
{
	return Guid != 0 && Guid == LastInvClickGuid ? FMath::Clamp(float(1.0 - (FPlatformTime::Seconds() - LastInvClickTime) / InventoryDoubleClickSeconds()), 0.f, 1.f) : 0.f;
}

bool UACEUIGameplayBinder::ActivateVRPointerItem(FVector2D Point)
{
	// Reuse the same source, ownership, loot and use rules as a double click.
	if (!TryBeginInventoryDrag(Point)) return false;
	bInvDoubleClickPending = true;
	return TryFinishInventoryDrag(Point);
}

bool UACEUIGameplayBinder::InspectVRPointerItem(FVector2D Point)
{
	if (!PlayerController || !PlayerController->IsVRActive() || !Client || !Canvas || bInvDragPending || bInvDragActive || bSpellDragPending || bSpellDragActive) return false;
	if (InspectSpellAt(Point)) return true;
	FBox2D Bounds; FString Label, Hint; int32 Guid = 0;
	GetVRPointerFeedback(Point, Bounds, Label, Hint, Guid);
	if (!Guid && ActivePanelPage == TEXT("InventoryPanel_Field") && Canvas->GetElementLayer())
	{
		FString Slot; int64 Mask = 0;
		if (HitTestDollSlot(Canvas->GetElementLayer()->GetCachedGeometry().LocalToAbsolute(Point), Slot, Mask))
			Guid = FindUpperEquippedItem(Mask);
	}
	if (!Guid) return false;
	SelectInventoryGuid(Guid);
	Client->SendIdentifyObject(Guid);
	return true;
}
