#include "ACEEffectLightSubsystem.h"
#include "ACELandblockActor.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/ActorComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<int32> CVarEffectLightBudget(TEXT("ace.FXLightBudget"), 8,
	TEXT("Maximum simultaneous particle/script scene lights per desktop world (0 disables them, capped at 64)."));
static TAutoConsoleVariable<int32> CVarEffectLightBudgetMobile(TEXT("ace.FXLightBudgetMobile"), 4,
	TEXT("Maximum simultaneous particle/script scene lights per mobile world (0 disables them, capped at 64)."));
static TAutoConsoleVariable<float> CVarEffectLightMaxDistance(TEXT("ace.FXLightMaxDistance"), 3500.f,
	TEXT("Maximum camera distance in cm for particle/script scene lights; fades over the outer 20 percent."));

namespace
{
	constexpr double SelectionInterval = .05;
	constexpr double ParticleSubmissionLifetime = .25;
	constexpr float OverlapIntensityBudget = 12.f;

	bool IsValidRequest(const FACEEffectLightRequest& Request)
	{
		return !Request.Position.ContainsNaN() && FMath::IsFinite(Request.Color.R)
			&& FMath::IsFinite(Request.Color.G) && FMath::IsFinite(Request.Color.B) && FMath::IsFinite(Request.Color.A)
			&& FMath::IsFinite(Request.Intensity) && Request.Intensity > 0.f
			&& FMath::IsFinite(Request.Radius) && Request.Radius > 0.f;
	}

	float DistanceFade(const FVector& Position, const FVector* ViewPosition, float MaxDistance)
	{
		if (MaxDistance <= 0.f) return 0.f;
		if (!ViewPosition) return 1.f;
		const float Distance = FVector::Distance(Position, *ViewPosition);
		const float T = FMath::Clamp((MaxDistance - Distance) / (MaxDistance * .2f), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
}

bool UACEEffectLightSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UACEEffectLightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldPostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(
		this, &UACEEffectLightSubsystem::OnWorldPostActorTick);
}

void UACEEffectLightSubsystem::OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	// Tickable subsystems run before TG_PostUpdateWork, where particle poses are
	// refreshed. Apply transforms and overlap limits only after those submissions.
	if (World == GetWorld() && (TickType == LEVELTICK_All || TickType == LEVELTICK_ViewportsOnly))
		RefreshLights(DeltaSeconds);
}

void UACEEffectLightSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(WorldPostActorTickHandle);
	WorldPostActorTickHandle.Reset();
	for (UPointLightComponent* Light : LightPool)
		if (IsValid(Light)) Light->DestroyComponent();
	LightPool.Reset();
	Assignments.Reset();
	Sources.Reset();
	Super::Deinitialize();
}

UACEEffectLightSubsystem::FSourceState& UACEEffectLightSubsystem::FindOrAddSource(UActorComponent* Source)
{
	FSourceState& State = Sources.FindOrAdd(TWeakObjectPtr<UActorComponent>(Source));
	if (State.Order == 0)
	{
		State.Order = NextSourceOrder++;
		bSelectionDirty = true;
	}
	return State;
}

bool UACEEffectLightSubsystem::IsSourceUsable(UActorComponent* Source) const
{
	if (!IsValid(Source) || Source->IsBeingDestroyed() || Source->GetWorld() != GetWorld()) return false;
	const AActor* Owner = Source->GetOwner();
	return IsValid(Owner) && !Owner->IsActorBeingDestroyed() && !Owner->IsHidden();
}

void UACEEffectLightSubsystem::SubmitParticles(UActorComponent* Source, TConstArrayView<FACEEffectLightRequest> Requests)
{
	if (!IsValid(Source) || Source->IsBeingDestroyed() || Source->GetWorld() != GetWorld()) return;
	if (Requests.IsEmpty()) { RemoveParticles(Source); return; }
	FSourceState& State = FindOrAddSource(Source);
	if (State.Particles.Num() != Requests.Num()) bSelectionDirty = true;
	else for (int32 I = 0; I < Requests.Num(); ++I)
		if (State.Particles[I].EmitterId != Requests[I].EmitterId) { bSelectionDirty = true; break; }
	State.Particles.Reset(Requests.Num());
	for (const FACEEffectLightRequest& Request : Requests)
		if (IsValidRequest(Request)) State.Particles.Add(Request);
	State.SubmittedAt = ClockSeconds;
	State.bSubmittedSinceRefresh = true;
	// Deleted slots must disappear before the next scheduled selection.
	for (int32 I = 0; I < Assignments.Num(); ++I)
		if (Assignments[I].Source.Get() == Source && !Assignments[I].bScript && !FindRequest(Assignments[I]))
		{
			ReleaseSlot(I);
			bSelectionDirty = true;
		}
}

void UACEEffectLightSubsystem::ReleaseSlot(int32 Index)
{
	if (LightPool.IsValidIndex(Index) && LightPool[Index]) LightPool[Index]->SetVisibility(false);
	if (Assignments.IsValidIndex(Index)) Assignments[Index] = {};
}

void UACEEffectLightSubsystem::ReleaseSourceSlots(UActorComponent* Source, bool bParticles, bool bScript, const uint32* EmitterId)
{
	for (int32 I = 0; I < Assignments.Num(); ++I)
	{
		const FLightKey& Key = Assignments[I];
		if (Key.Source.Get() == Source && (Key.bScript ? bScript : bParticles)
			&& (!EmitterId || Key.EmitterId == *EmitterId))
		{
			ReleaseSlot(I);
			bSelectionDirty = true;
		}
	}
}

void UACEEffectLightSubsystem::RemoveParticles(UActorComponent* Source)
{
	const TWeakObjectPtr<UActorComponent> Key(Source);
	if (FSourceState* State = Sources.Find(Key))
	{
		if (State->Particles.IsEmpty()) return;
		ReleaseSourceSlots(Source, true, false);
		State->Particles.Reset();
		bSelectionDirty = true;
		if (!State->bScriptEnabled) Sources.Remove(Key);
	}
}

void UACEEffectLightSubsystem::RemoveEmitter(UActorComponent* Source, uint32 EmitterId)
{
	const TWeakObjectPtr<UActorComponent> Key(Source);
	if (FSourceState* State = Sources.Find(Key))
	{
		if (State->Particles.RemoveAll([EmitterId](const auto& Request) { return Request.EmitterId == EmitterId; }) == 0) return;
		ReleaseSourceSlots(Source, true, false, &EmitterId);
		bSelectionDirty = true;
		if (State->Particles.IsEmpty() && !State->bScriptEnabled) Sources.Remove(Key);
	}
}

void UACEEffectLightSubsystem::RemoveSource(UActorComponent* Source)
{
	if (Sources.Remove(TWeakObjectPtr<UActorComponent>(Source)) > 0)
	{
		ReleaseSourceSlots(Source, true, true);
		bSelectionDirty = true;
	}
}

void UACEEffectLightSubsystem::SetScriptLight(UActorComponent* Source, bool bEnabled, const FACEEffectLightRequest& Request)
{
	if (!IsValid(Source) || Source->IsBeingDestroyed() || Source->GetWorld() != GetWorld()) return;
	const TWeakObjectPtr<UActorComponent> Key(Source);
	if (!bEnabled || !IsValidRequest(Request))
	{
		if (FSourceState* State = Sources.Find(Key))
		{
			if (!State->bScriptEnabled) return;
			ReleaseSourceSlots(Source, false, true);
			State->bScriptEnabled = false;
			bSelectionDirty = true;
			if (State->Particles.IsEmpty()) Sources.Remove(Key);
		}
		return;
	}
	FSourceState& State = FindOrAddSource(Source);
	bSelectionDirty |= !State.bScriptEnabled || State.Script.bPortal != Request.bPortal;
	State.Script = Request;
	State.Script.EmitterId = 0;
	State.bScriptEnabled = true;
}

const FACEEffectLightRequest* UACEEffectLightSubsystem::FindRequest(const FLightKey& Key) const
{
	const FSourceState* State = Sources.Find(Key.Source);
	if (!State) return nullptr;
	if (Key.bScript) return State->bScriptEnabled ? &State->Script : nullptr;
	return State->Particles.FindByPredicate([&Key](const auto& Request) { return Request.EmitterId == Key.EmitterId; });
}

UPointLightComponent* UACEEffectLightSubsystem::FindParticleLight(const UActorComponent* Source, uint32 EmitterId) const
{
	const FLightKey Key{const_cast<UActorComponent*>(Source), EmitterId, false};
	for (int32 I = 0; I < Assignments.Num(); ++I)
		if (Assignments[I] == Key && LightPool[I] && LightPool[I]->IsVisible()) return LightPool[I];
	return nullptr;
}

UPointLightComponent* UACEEffectLightSubsystem::FindScriptLight(const UActorComponent* Source) const
{
	const FLightKey Key{const_cast<UActorComponent*>(Source), 0, true};
	for (int32 I = 0; I < Assignments.Num(); ++I)
		if (Assignments[I] == Key && LightPool[I] && LightPool[I]->IsVisible()) return LightPool[I];
	return nullptr;
}

bool UACEEffectLightSubsystem::IsParticleSelected(const UActorComponent* Source, uint32 EmitterId) const
{
	return FindParticleLight(Source, EmitterId) != nullptr;
}

void UACEEffectLightSubsystem::UpdateParticlePosition(UActorComponent* Source, uint32 EmitterId, const FVector& Position)
{
	if (Position.ContainsNaN()) return;
	if (FSourceState* State = Sources.Find(TWeakObjectPtr<UActorComponent>(Source)))
		if (FACEEffectLightRequest* Request = State->Particles.FindByPredicate(
			[EmitterId](const auto& Candidate) { return Candidate.EmitterId == EmitterId; })) Request->Position = Position;
	if (UPointLightComponent* Light = FindParticleLight(Source, EmitterId)) Light->SetWorldLocation(Position);
}

int32 UACEEffectLightSubsystem::GetVisibleLightCount(const UActorComponent* Source) const
{
	int32 Count = 0;
	for (int32 I = 0; I < LightPool.Num(); ++I)
		if (LightPool[I] && LightPool[I]->IsVisible() && LightPool[I]->Intensity > 0.f
			&& (!Source || Assignments[I].Source.Get() == Source)) ++Count;
	return Count;
}

bool UACEEffectLightSubsystem::IsPortalOccluded(const FChoice& Choice) const
{
	if (!Choice.Request.bPortal) return false;
	UWorld* World = GetWorld();
	if (!World) return false;
	const UActorComponent* Source = Choice.Key.Source.Get();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ACEPortalLightOcclude), false, Source ? Source->GetOwner() : nullptr);
	FHitResult Hit;
	const FVector Position = Choice.Request.Position;
	return World->LineTraceSingleByChannel(Hit, Position + FVector(0, 0, 900), Position + FVector(0, 0, 20),
		ECC_WorldStatic, Params) && Cast<AACELandblockActor>(Hit.GetActor()) && Hit.ImpactPoint.Z > Position.Z + 40.f;
}

UPointLightComponent* UACEEffectLightSubsystem::CreatePoolLight()
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
	Light->SetVisibility(false);
	Light->SetIntensity(0.f);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetCastShadows(false);
	Light->SetUseInverseSquaredFalloff(false);
	Light->SetIntensityUnits(ELightUnits::Unitless);
	Light->SetLightFalloffExponent(2.f);
	Light->SetSourceRadius(36.f);
	Light->SetSoftSourceRadius(64.f);
	Light->SetSpecularScale(0.f);
	Light->SetAffectTranslucentLighting(false);
	Light->SetLightingChannels(true, true, false);
	Light->SetVolumetricScatteringIntensity(0.f);
	Light->RegisterComponentWithWorld(GetWorld());
	return Light;
}

void UACEEffectLightSubsystem::SelectLights(int32 Budget, const FVector* ViewPosition, float MaxDistance)
{
	TArray<FChoice> Candidates;
	for (const auto& Pair : Sources)
	{
		if (!IsSourceUsable(Pair.Key.Get())) continue;
		auto Add = [&](const FACEEffectLightRequest& Request, bool bScript)
		{
			if (!IsValidRequest(Request) || DistanceFade(Request.Position, ViewPosition, MaxDistance) <= 0.f) return;
			FChoice Choice;
			Choice.Key = {Pair.Key, Request.EmitterId, bScript};
			Choice.Request = Request;
			Choice.Order = Pair.Value.Order;
			Choice.Rank = ViewPosition ? FVector::Distance(Request.Position, *ViewPosition) : 0.;
			if (Assignments.Contains(Choice.Key)) Choice.Rank *= .85;
			Candidates.Add(Choice);
		};
		// An explicit authored light overrides the inferred material hint on the
		// same source, regardless of which hook happened to submit first.
		if (Pair.Value.bScriptEnabled) Add(Pair.Value.Script, true);
		else for (const auto& Request : Pair.Value.Particles) Add(Request, false);
	}
	Candidates.Sort([](const FChoice& A, const FChoice& B)
	{
		if (A.Rank != B.Rank) return A.Rank < B.Rank;
		if (A.Order != B.Order) return A.Order < B.Order;
		if (A.Key.bScript != B.Key.bScript) return A.Key.bScript;
		return A.Key.EmitterId < B.Key.EmitterId;
	});
	TArray<FChoice, TInlineAllocator<64>> Chosen;
	TSet<TWeakObjectPtr<UActorComponent>> ChosenSources;
	for (const FChoice& Choice : Candidates)
	{
		if (Chosen.Num() >= Budget) break;
		if (ChosenSources.Contains(Choice.Key.Source) || IsPortalOccluded(Choice)) continue;
		Chosen.Add(Choice);
		ChosenSources.Add(Choice.Key.Source);
	}
	for (int32 I = 0; I < Assignments.Num(); ++I)
		if (!Chosen.ContainsByPredicate([&](const FChoice& Choice) { return Choice.Key == Assignments[I]; })) ReleaseSlot(I);
	for (const FChoice& Choice : Chosen)
	{
		if (Assignments.Contains(Choice.Key)) continue;
		int32 Slot = Assignments.IndexOfByPredicate([](const FLightKey& Key) { return !Key.Source.IsValid(); });
		if (Slot == INDEX_NONE)
		{
			if (LightPool.Num() >= Budget) continue;
			Slot = LightPool.Add(CreatePoolLight());
			Assignments.AddDefaulted();
		}
		Assignments[Slot] = Choice.Key;
	}
}

void UACEEffectLightSubsystem::UpdateSelectedLights(const FVector* ViewPosition, float MaxDistance)
{
	TArray<FACEEffectLightRequest, TInlineAllocator<64>> Requests;
	Requests.SetNum(Assignments.Num());
	TArray<bool, TInlineAllocator<64>> Active;
	Active.Init(false, Assignments.Num());
	for (int32 I = 0; I < Assignments.Num(); ++I)
	{
		const FACEEffectLightRequest* Request = FindRequest(Assignments[I]);
		if (!Request || !IsSourceUsable(Assignments[I].Source.Get())
			|| !IsValidRequest(*Request) || DistanceFade(Request->Position, ViewPosition, MaxDistance) <= 0.f)
		{
			if (Assignments[I].Source.IsValid()) bSelectionDirty = true;
			ReleaseSlot(I);
			continue;
		}
		Requests[I] = *Request;
		Active[I] = true;
	}
	for (int32 I = 0; I < Assignments.Num(); ++I)
	{
		if (!Active[I] || !LightPool[I]) continue;
		const FACEEffectLightRequest& Request = Requests[I];
		double OverlapIntensity = 0.;
		for (int32 J = 0; J < Assignments.Num(); ++J)
		{
			if (!Active[J]) continue;
			const double IntersectionDistance = double(Request.Radius) + Requests[J].Radius;
			const double Margin = FMath::Max(1., .25 * FMath::Min(double(Request.Radius), double(Requests[J].Radius)));
			const double DistanceSquared = FVector::DistSquared(Request.Position, Requests[J].Position);
			if (DistanceSquared >= FMath::Square(IntersectionDistance + Margin)) continue;
			double Weight = 1.;
			if (DistanceSquared > FMath::Square(IntersectionDistance))
			{
				// Begin dimming before the spheres meet, without weakening the
				// full intensity bound anywhere their actual lighting can overlap.
				const double T = FMath::Clamp((IntersectionDistance + Margin - FMath::Sqrt(DistanceSquared)) / Margin, 0., 1.);
				Weight = T * T * (3. - 2. * T);
			}
			OverlapIntensity += Requests[J].Intensity * Weight;
		}
		const float Scale = float(FMath::Min(1., double(OverlapIntensityBudget) / FMath::Max(OverlapIntensity, .001)));
		UPointLightComponent* Light = LightPool[I];
		Light->SetWorldLocation(Request.Position);
		Light->SetLightColor(Request.Color);
		Light->SetAttenuationRadius(Request.Radius);
		Light->SetIntensity(Request.Intensity * Scale * DistanceFade(Request.Position, ViewPosition, MaxDistance));
		Light->SetVisibility(true);
	}
}

void UACEEffectLightSubsystem::RefreshLights(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ACE_EffectLightBudget);
	if (!GetWorld()) return;
	ClockSeconds += FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
	for (auto It = Sources.CreateIterator(); It; ++It)
	{
		UActorComponent* Source = It.Key().Get();
		if (!IsValid(Source) || Source->IsBeingDestroyed() || Source->GetWorld() != GetWorld() || !IsValid(Source->GetOwner())
			|| Source->GetOwner()->IsActorBeingDestroyed())
		{
			for (int32 I = 0; I < Assignments.Num(); ++I)
				if (Assignments[I].Source == It.Key()) ReleaseSlot(I);
			It.RemoveCurrent();
			bSelectionDirty = true;
			continue;
		}
		FSourceState& State = It.Value();
		if (State.bSubmittedSinceRefresh)
		{
			// A long frame must not expire data submitted during that very frame.
			State.SubmittedAt = ClockSeconds;
			State.bSubmittedSinceRefresh = false;
		}
		if (!State.Particles.IsEmpty() && ClockSeconds - State.SubmittedAt > ParticleSubmissionLifetime)
		{
			State.Particles.Reset();
			ReleaseSourceSlots(Source, true, false);
		}
		if (State.bScriptEnabled) State.Script.Position = Source->GetOwner()->GetActorLocation();
		if (State.Particles.IsEmpty() && !State.bScriptEnabled) { It.RemoveCurrent(); bSelectionDirty = true; }
	}
	const bool bMobile = GetWorld()->GetFeatureLevel() <= ERHIFeatureLevel::ES3_1;
	const int32 Budget = FMath::Clamp(bMobile ? CVarEffectLightBudgetMobile.GetValueOnGameThread()
		: CVarEffectLightBudget.GetValueOnGameThread(), 0, 64);
	// Runtime budget reductions must bound the pool itself as well as visible lights.
	while (LightPool.Num() > Budget)
	{
		const int32 Index = LightPool.Num() - 1;
		ReleaseSlot(Index);
		if (IsValid(LightPool[Index])) LightPool[Index]->DestroyComponent();
		LightPool.RemoveAt(Index);
		Assignments.RemoveAt(Index);
		bSelectionDirty = true;
	}
	const float ConfiguredDistance = CVarEffectLightMaxDistance.GetValueOnGameThread();
	const float MaxDistance = FMath::IsFinite(ConfiguredDistance) ? FMath::Max(0.f, ConfiguredDistance) : 3500.f;
	if (Budget != LastBudget || MaxDistance != LastMaxDistance)
	{
		bSelectionDirty = true;
		LastBudget = Budget;
		LastMaxDistance = MaxDistance;
	}
	FVector ViewPosition;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FVector* View = nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewPosition, ViewRotation);
		View = &ViewPosition;
	}
	if (bSelectionDirty || ClockSeconds >= NextSelectionAt)
	{
		bSelectionDirty = false;
		NextSelectionAt = ClockSeconds + SelectionInterval;
		SelectLights(Budget, View, MaxDistance);
	}
	UpdateSelectedLights(View, MaxDistance);
}
