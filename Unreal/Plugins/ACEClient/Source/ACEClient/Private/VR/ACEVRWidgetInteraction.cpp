#include "VR/ACEVRWidgetInteraction.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Actor.h"

UACEVRWidgetInteraction::FWidgetTraceResult UACEVRWidgetInteraction::PerformTrace() const
{
	FWidgetTraceResult Result;
	Result.LineStartLocation = GetComponentLocation();
	const FVector Direction = GetForwardVector();
	Result.LineEndLocation = Result.LineStartLocation + Direction * InteractionDistance;
	TInlineComponentArray<UWidgetComponent*> Panels(GetOwner());
	float Nearest = InteractionDistance; bool MainWindow = false; int32 Layer = MIN_int32;
	for (auto* Panel : Panels)
	{
		if (!Panel->IsVisible() || Panel->GetCollisionEnabled() == ECollisionEnabled::NoCollision) continue;
		const FVector Normal = Panel->GetForwardVector();
		const double Denom = FVector::DotProduct(Direction, Normal);
		if (FMath::Abs(Denom) < .0001) continue;
		const double Distance = FVector::DotProduct(Panel->GetComponentLocation() - Result.LineStartLocation, Normal) / Denom;
		if (Distance < 0 || Distance > InteractionDistance) continue;
		const FVector Position = Result.LineStartLocation + Direction * Distance;
		FVector2D Local; Panel->GetLocalHitLocation(Position, Local);
		const FVector2D Size = Panel->GetDrawSize();
		if (Local.X < 0 || Local.Y < 0 || Local.X >= Size.X || Local.Y >= Size.Y) continue;
		bool IsMainWindow = false;
		if (auto* Canvas = Cast<UACEUICanvasWidget>(Panel->GetWidget()); Canvas && Canvas->GetManager())
		{
			const FVector2D Point = Canvas->ViewportToLayout(Local);
			IsMainWindow = Canvas->GetManager()->FindWindowAtCanvas(FMath::FloorToInt(Point.X), FMath::FloorToInt(Point.Y)).IsValid();
		}
		// Interaction must select the same surface that is drawn on top.
		const int32 CandidateLayer = Panel->TranslucencySortPriority;
		if (CandidateLayer < Layer) continue;
		if (CandidateLayer == Layer)
		{
			if (MainWindow && !IsMainWindow) continue;
			if (Distance >= Nearest && !(IsMainWindow && !MainWindow)) continue;
		}
		Nearest = Distance; MainWindow = IsMainWindow; Layer = CandidateLayer;
		Result.bWasHit = true; Result.HitWidgetComponent = Panel; Result.LocalHitLocation = Local;
		Result.HitResult = FHitResult(GetOwner(), Panel, Position, Normal);
		Result.HitResult.bBlockingHit = true; Result.HitResult.Distance = Distance;
		Result.HitResult.TraceStart = Result.LineStartLocation; Result.HitResult.TraceEnd = Result.LineEndLocation;
	}
	if (Result.bWasHit) Result.HitWidgetPath = FindHoveredWidgetPath(Result);
	return Result;
}
