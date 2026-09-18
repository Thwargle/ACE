#include "VR/ACEVRRetailSurface.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEClientSubsystem.h"
#include "Engine/GameInstance.h"
#include "Components/WidgetComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SOverlay.h"
#include "Styling/CoreStyle.h"

void UACEVRRetailSurface::SetSource(UWidgetComponent* Component, UACEUICanvasWidget* Canvas, FName Window)
{
	Source = Component; Retail = Canvas; WindowName = Window;
}

bool UACEVRRetailSurface::RefreshSurface()
{
	if (RecoveryLabel && VitalsSource.IsValid())
	{
		const FString Text = VitalsSource->GetCombatTimerText();
		if (RecoveryLabel->GetText().ToString() != Text) RecoveryLabel->SetText(FText::FromString(Text));
	}
	if (!Source || !Retail || !Retail->GetManager() || !Source->GetRenderTarget()) return false;
	auto Element = Retail->GetManager()->FindElementByName(WindowName.ToString());
	if (!Element || !Element->bVisible || Element->Width <= 0 || Element->Height <= 0) return false;
	const FIntPoint Origin = Element->GetScreenOrigin();
	const FVector2D Scale = Retail->GetLastScale2D();
	const FVector2D Size = Source->GetDrawSize();
	if (VitalsSource.IsValid())
	{
		const int32 Mode = VitalsSource->GetCombatMode();
		const TCHAR* Name = Mode == 8 ? TEXT("MagicModeButton") : Mode == 4 ? TEXT("MissileModeButton")
			: Mode == 2 ? TEXT("MeleeModeButton") : TEXT("PeaceModeButton");
		if (const auto Stance = Retail->GetManager()->FindElementByName(Name))
		{
			const FVector2D P = FVector2D(Stance->GetScreenOrigin()) * Scale;
			const FBox2f UV(FVector2f(P / Size), FVector2f((P + FVector2D(Stance->Width, Stance->Height) * Scale) / Size));
			StanceBrush.SetResourceObject(Source->GetRenderTarget());
			StanceBrush.DrawAs = ESlateBrushDrawType::Image; StanceBrush.ImageSize = FVector2f(36,36);
			StanceBrush.SetUVRegion(UV);
		}
	}
	const FBox2D NewRect(FVector2D(Origin) * Scale, FVector2D(Origin + FIntPoint(Element->Width, Element->Height)) * Scale);
	const FBox2f NewUV(FVector2f(NewRect.Min / Size), FVector2f(NewRect.Max / Size));
	if (SourceRect == NewRect && Brush.GetResourceObject() == Source->GetRenderTarget() && CachedRenderSize == Size) return true;
	SourceRect = NewRect; CachedRenderSize = Size;
	Brush.SetResourceObject(Source->GetRenderTarget());
	Brush.DrawAs = ESlateBrushDrawType::Image; Brush.ImageSize = FVector2f(SourceRect.GetSize());
	Brush.SetUVRegion(NewUV);
	InvalidateLayoutAndVolatility();
	return true;
}

TSharedRef<SWidget> UACEVRRetailSurface::RebuildWidget()
{
	SetVisibility(ESlateVisibility::Visible);
	RecoveryLabel = NewObject<UACERetailTextBlock>(this);
	RecoveryElement = MakeShared<FACEUIElement>(); RecoveryElement->FontId = 0x40000006;
	auto* Client = GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEClientSubsystem>() : nullptr;
	auto* Resources = Client ? Client->GetUIResourceResolver() : nullptr;
	FACEDatFont Font;
	const float FontScale = Resources && Resources->ResolveFont(RecoveryElement->FontId, Font) ? 20.f/FMath::Max(1,int32(Font.MaxCharHeight)) : 1.f;
	RecoveryLabel->SetRetailElement(Resources, RecoveryElement, FVector2D(FontScale), 0.f, false);
	RecoveryLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",15));
	RecoveryLabel->SetText(FText::FromString(TEXT("Swing ready")));
	RecoveryLabel->SetColorAndOpacity(FLinearColor(FColor(255,242,204)));
	return SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride_Lambda([this]() { return SourceRect.GetSize().Y; })
			[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SImage).Image(&Brush)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[SNew(SBox).Visibility_Lambda([this]() { return VitalsSource.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					.WidthOverride(44.f).HeightOverride(36.f).Padding(FMargin(4,0))
					[SNew(SImage).Image(&StanceBrush)]]]]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox)
			.HeightOverride_Lambda([this]() { return VitalsSource.IsValid() ? 32.f : 0.f; })
			.Visibility_Lambda([this]() { return VitalsSource.IsValid() && VitalsSource->HasCombatTimer() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
			[SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SProgressBar)
					.FillColorAndOpacity(FLinearColor(.52f,.33f,.08f,1.f))
					.Percent_Lambda([this]() { return VitalsSource.IsValid() ? VitalsSource->GetCombatTimerProgress() : 1.f; })]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[RecoveryLabel->TakeWidget()]]]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride_Lambda([this]() { return StatusSource.IsValid() ? 48.f : VitalsSource.IsValid() ? 36.f : 0.f; })
			[SNew(SBorder).Visibility_Lambda([this]()
				{
					// Reserve the footer space so hover never shifts the bars or hit target.
					return VitalsSource.IsValid() && !VitalsSource->ShouldShowVitalsControls()
						? EVisibility::Hidden : EVisibility::HitTestInvisible;
				})
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.035f, .027f, .014f, .95f))
			[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 14)).AutoWrapText(true)
				.Text_Lambda([this]()
				{
					if (VitalsSource.IsValid()) return FText::FromString(VitalsSource->GetSettings()->bVitalsLocked
						? TEXT("Locked / Select here to unlock") : TEXT("Drag bar to move / Select here to lock"));
					return FText::FromString(StatusSource.IsValid() ? StatusSource->GetCastFeedback() : FString());
				})]]];
}

FPointerEvent UACEVRRetailSurface::Translate(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	const FGeometry SourceGeometry = Retail->GetCachedGeometry();
	return FPointerEvent(Event,
		SourceGeometry.LocalToAbsolute(ToCanvas(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()))),
		SourceGeometry.LocalToAbsolute(ToCanvas(Geometry.AbsoluteToLocal(Event.GetLastScreenSpacePosition()))));
}

FReply UACEVRRetailSurface::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!Retail) return FReply::Unhandled();
	if (VitalsSource.IsValid())
	{
		const float Y = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).Y;
		if (Y >= SourceRect.GetSize().Y + 32.f && VitalsSource->ShouldShowVitalsControls()) VitalsSource->ToggleVitalsLock();
		else if (Y < SourceRect.GetSize().Y) VitalsSource->BeginVitalsDrag(Event.GetPointerIndex() == 0);
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	if (Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).Y >= SourceRect.GetSize().Y) return FReply::Handled();
	Retail->NativeOnMouseButtonDown(Retail->GetCachedGeometry(), Translate(Geometry, Event));
	// A wrist press is an immediate stationary click. Never carry the retail
	// hotbar's pending reorder gesture across frames or into another surface.
	if (StatusSource.IsValid()) Retail->NativeOnMouseButtonUp(Retail->GetCachedGeometry(), Translate(Geometry, Event));
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UACEVRRetailSurface::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (VitalsSource.IsValid()) { VitalsSource->EndVitalsDrag(); return FReply::Handled().ReleaseMouseCapture(); }
	if (StatusSource.IsValid()) return FReply::Handled().ReleaseMouseCapture();
	if (!Retail) return FReply::Unhandled();
	Retail->NativeOnMouseButtonUp(Retail->GetCachedGeometry(), Translate(Geometry, Event));
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UACEVRRetailSurface::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Retail && !StatusSource.IsValid() && !VitalsSource.IsValid()) Retail->NativeOnMouseMove(Retail->GetCachedGeometry(), Translate(Geometry, Event));
	return FReply::Handled();
}

FReply UACEVRRetailSurface::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (VitalsSource.IsValid()) return FReply::Handled();
	if (Retail) Retail->NativeOnMouseWheel(Retail->GetCachedGeometry(), Translate(Geometry, Event));
	return FReply::Handled();
}
