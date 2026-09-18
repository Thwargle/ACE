#include "ACELoginStreamingWorldSubsystem.h"
#include "ACEWcTerrainRuntime.h"
#include "ACEDatSubsystem.h"
#include "Engine/WorldComposition.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/GameInstance.h"
#include "WorldPartition/WorldPartition.h"

namespace ACELoginStreaming
{
	static void ApplyWcFlagsHold(UWorld* World)
	{
		if (!World || !World->WorldComposition)
		{
			return;
		}
		for (ULevelStreaming* SL : World->WorldComposition->TilesStreaming)
		{
			if (!SL)
			{
				continue;
			}
			// Do not undo portal preload / distance streaming on tiles already pinned loaded.
			if (SL->IsLevelLoaded() || SL->ShouldBeLoaded())
			{
				SL->bDisableDistanceStreaming = false;
				continue;
			}
			SL->bDisableDistanceStreaming = true;
			SL->bShouldBlockOnUnload = false;
		}
	}
}

bool UACELoginStreamingWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UACELoginStreamingWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApplyLoginStreamingHold();
}

void UACELoginStreamingWorldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldPartition* WP = World->GetWorldPartition())
		{
			if (!bWpStreamingRestored)
			{
				WP->EnableStreamingIn();
				bWpStreamingRestored = true;
			}
		}
		if (World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0 && !bWcDistanceStreamingRestored)
		{
			ACEWcTerrainRuntime::SetWcDistanceStreamingEnabled(World, true);
			ACEWcTerrainRuntime::ReleaseWcStreamingFlagsHold(World);
		}
	}
	bLoginHoldActive = false;
	bWcFlagsHoldApplied = false;
	Super::Deinitialize();
}

void UACELoginStreamingWorldSubsystem::ApplyLoginStreamingHold()
{
	if (bLoginHoldActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bAppliedWp = false;
	if (UWorldPartition* WP = World->GetWorldPartition())
	{
		if (WP->CanStream() && WP->IsStreamingInEnabled())
		{
			// Pause requests without changing the world's baked partition configuration.
			WP->DisableStreamingIn();
			bAppliedWp = true;
			bWpStreamingRestored = false;
		}
	}

	bool bAppliedWc = false;
	const int32 NumTiles = World->WorldComposition ? World->WorldComposition->GetTilesList().Num() : 0;
	if (NumTiles > 0)
	{
		ACELoginStreaming::ApplyWcFlagsHold(World);
		ACEWcTerrainRuntime::HideWcLandscapesForLogin(World);
		bAppliedWc = true;
		bWcFlagsHoldApplied = true;
		bWcDistanceStreamingRestored = false;
	}

	if (bAppliedWp || bAppliedWc)
	{
		bLoginHoldActive = true;
		UE_LOG(LogTemp, Warning, TEXT("ACE: login streaming hold applied early (WP=%d WC=%d tiles=%d, flags-only)"),
			bAppliedWp ? 1 : 0,
			bAppliedWc ? 1 : 0,
			NumTiles);
	}
}

void UACELoginStreamingWorldSubsystem::MaintainLoginStreamingHold()
{
	if (!bLoginHoldActive)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (bWcFlagsHoldApplied)
		{
			ACELoginStreaming::ApplyWcFlagsHold(World);
		}
	}
}

void UACELoginStreamingWorldSubsystem::RestoreWorldStreamingIfAllowed()
{
	UWorld* World = GetWorld();
	if (!World || !bLoginHoldActive)
	{
		return;
	}

	bool bAllowWorldStream = true;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
		{
			bAllowWorldStream = Dat->IsWorldStreamingAllowed();
		}
	}
	if (!bAllowWorldStream)
	{
		MaintainLoginStreamingHold();
		return;
	}

	if (UWorldPartition* WP = World->GetWorldPartition())
	{
		if (!bWpStreamingRestored)
		{
			WP->EnableStreamingIn();
			bWpStreamingRestored = true;
			UE_LOG(LogTemp, Log, TEXT("ACE: World Partition streaming restored after login"));
		}
	}

	if (World->WorldComposition && World->WorldComposition->GetTilesList().Num() > 0 && !bWcDistanceStreamingRestored)
	{
		ACEWcTerrainRuntime::SetWcDistanceStreamingEnabled(World, true);
		ACEWcTerrainRuntime::ReleaseWcStreamingFlagsHold(World);
		ACEWcTerrainRuntime::ShowWcLandscapesAfterLogin(World);
		bWcDistanceStreamingRestored = true;
		UE_LOG(LogTemp, Log, TEXT("ACE: WC distance streaming + landscapes restored after login"));
	}

	if (bWpStreamingRestored || bWcDistanceStreamingRestored)
	{
		bLoginHoldActive = false;
		bWcFlagsHoldApplied = false;
	}
}

void UACELoginStreamingWorldSubsystem::RequestWcTerrainAround(const FVector& WorldLocation, int32 RingTiles)
{
	ACEWcTerrainRuntime::RequestTerrainAround(GetWorld(), WorldLocation, RingTiles);
}

bool UACELoginStreamingWorldSubsystem::IsWcTerrainReadyAround(const FVector& WorldLocation, int32 RingTiles) const
{
	return ACEWcTerrainRuntime::IsTerrainReadyAround(GetWorld(), WorldLocation, RingTiles);
}

void UACELoginStreamingWorldSubsystem::MaintainWcLandscapePresentation()
{
	ACEWcTerrainRuntime::MaintainLandscapePresentation(GetWorld());
}
