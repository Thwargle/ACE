#include "ACEWcTerrainRuntime.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/WorldComposition.h"
#include "Engine/LevelStreaming.h"
#include "Misc/PackageName.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr float WcTileHalfExtentCm = 306000.f;
	constexpr float WcTileWidthCm = WcTileHalfExtentCm * 2.f;
	/** Diagonal tile center from a corner is ~866k cm — stay above that + margin. */
	constexpr int32 WcLandscapeStreamingDistanceCm = 950000;
	static const FName ACEOutdoorTerrainTag(TEXT("ACEOutdoorTerrain"));

	bool IsLandscapeTileAssetName(const FString& AssetName)
	{
		TArray<FString> Parts;
		AssetName.ParseIntoArray(Parts, TEXT("_"));
		return Parts.Num() == 3 && Parts[0] == TEXT("Tile") && Parts[1].StartsWith(TEXT("x")) && Parts[2].StartsWith(TEXT("y"));
	}

	ULevelStreaming* FindStreamingLevelForPackage(UWorld* World, const FName& PackageName)
	{
		if (!World)
		{
			return nullptr;
		}
		if (UWorldComposition* WC = World->WorldComposition)
		{
			for (ULevelStreaming* SL : WC->TilesStreaming)
			{
				if (SL && SL->GetWorldAssetPackageFName() == PackageName)
				{
					return SL;
				}
			}
		}
		for (ULevelStreaming* SL : World->GetStreamingLevels())
		{
			if (SL && SL->GetWorldAssetPackageFName() == PackageName)
			{
				return SL;
			}
		}
		return nullptr;
	}

	void PrepareWcLandscapeProxy(ALandscapeProxy* Landscape)
	{
		if (!Landscape)
		{
			return;
		}
		if (!Landscape->ActorHasTag(ACEOutdoorTerrainTag))
		{
			Landscape->Tags.AddUnique(ACEOutdoorTerrainTag);
		}
		Landscape->SetActorHiddenInGame(false);
		Landscape->SetActorEnableCollision(true);
		UACEDatSubsystem* Dat = nullptr;
		if (UWorld* World = Landscape->GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				Dat = GI->GetSubsystem<UACEDatSubsystem>();
			}
		}
		TArray<ULandscapeComponent*> Components;
		Landscape->GetComponents(Components);
		for (ULandscapeComponent* LC : Components)
		{
			if (!LC)
			{
				continue;
			}
			if (!LC->GetVisibleFlag()) LC->SetVisibility(true, true);
			LC->SetHiddenInGame(false);
			LC->SetComponentTickEnabled(true);
			LC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			LC->SetCollisionObjectType(ECC_WorldStatic);
			LC->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			LC->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			LC->SetCastShadow(false);
			LC->bCastDynamicShadow = false;
			LC->bCastFarShadow = false;
			if (Dat)
			{
				const int32 NumMats = LC->GetNumMaterials();
				for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
				{
					UMaterialInterface* Mat = LC->GetMaterial(MatIdx);
					if (UMaterialInstanceConstant* Mic = Cast<UMaterialInstanceConstant>(Mat))
					{
						UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Mic, Landscape);
						if (Mid)
						{
							LC->SetMaterial(MatIdx, Mid);
							Mat = Mid;
						}
					}
					if (Mat)
					{
						Dat->ApplyDistanceFogToMaterial(Mat);
					}
				}
			}
		}
		// Component setters already invalidate the changed state. Recreating every
		// landscape proxy here also ran from MaintainLandscapePresentation each frame.
	}

	int32 TileIndexFromWorldCoord(float WorldCoord)
	{
		const float TileCoord = WorldCoord / WcTileWidthCm;
		return (TileCoord < 0.f)
			? FMath::CeilToInt(TileCoord - 0.5f)
			: FMath::FloorToInt(TileCoord + 0.5f);
	}

	FBox BuildWcNeighborhoodBox(const FVector& WorldLocation, int32 RingTiles)
	{
		const float Extent = WcTileHalfExtentCm * static_cast<float>(2 * FMath::Max(1, RingTiles) + 1);
		return FBox::BuildAABB(WorldLocation, FVector(Extent, Extent, Extent * 2.f));
	}

	void ForceLoadWcTilesInBox(UWorld* World, const FVector& WorldLocation, int32 RingTiles, bool bBlockOnLoad)
	{
		UWorldComposition* WC = World ? World->WorldComposition : nullptr;
		if (!WC)
		{
			return;
		}
		ACEWcTerrainRuntime::ReleaseWcStreamingFlagsHold(World);

		const int32 CenterTileX = TileIndexFromWorldCoord(WorldLocation.X);
		const int32 CenterTileY = TileIndexFromWorldCoord(WorldLocation.Y);
		const int32 Ring = FMath::Max(1, RingTiles);

		for (const FWorldCompositionTile& Tile : WC->GetTilesList())
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Tile.PackageName.ToString());
			if (!IsLandscapeTileAssetName(AssetName))
			{
				continue;
			}
			const FVector TileCenter = Tile.Info.Bounds.GetCenter();
			const int32 TileX = TileIndexFromWorldCoord(TileCenter.X);
			const int32 TileY = TileIndexFromWorldCoord(TileCenter.Y);
			const bool bInChebyshevRing = FMath::Abs(TileX - CenterTileX) <= Ring
				&& FMath::Abs(TileY - CenterTileY) <= Ring;
			if (!bInChebyshevRing)
			{
				continue;
			}
			if (ULevelStreaming* SL = FindStreamingLevelForPackage(World, Tile.PackageName))
			{
				SL->bDisableDistanceStreaming = false;
				if (bBlockOnLoad)
				{
					SL->bShouldBlockOnLoad = true;
				}
				SL->SetShouldBeLoaded(true);
				SL->SetShouldBeVisible(true);
			}
		}
	}

	void ReleaseBlockOnLoadFlags(UWorld* World)
	{
		if (!World || !World->WorldComposition)
		{
			return;
		}
		for (ULevelStreaming* SL : World->WorldComposition->TilesStreaming)
		{
			if (SL)
			{
				SL->bShouldBlockOnLoad = false;
			}
		}
	}

	bool IsLandscapeReady(ALandscapeProxy* Landscape)
	{
		if (!Landscape || Landscape->IsHidden() || !Landscape->GetActorEnableCollision())
		{
			return false;
		}
		TArray<ULandscapeComponent*> Components;
		Landscape->GetComponents(Components);
		if (Components.Num() == 0)
		{
			return false;
		}
		for (ULandscapeComponent* LC : Components)
		{
			if (!LC || !LC->IsVisible() || LC->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				|| LC->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
			{
				return false;
			}
		}
		return true;
	}

	bool IsWcTerrainReadyInBox(UWorld* World, const FBox& Neighborhood)
	{
		UWorldComposition* WC = World ? World->WorldComposition : nullptr;
		if (!WC || WC->GetTilesList().Num() == 0)
		{
			return true;
		}

		int32 RequiredTiles = 0;
		int32 ReadyTiles = 0;
		for (const FWorldCompositionTile& Tile : WC->GetTilesList())
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Tile.PackageName.ToString());
			if (!IsLandscapeTileAssetName(AssetName) || !Tile.Info.Bounds.Intersect(Neighborhood))
			{
				continue;
			}
			++RequiredTiles;

			bool bTileReady = false;
			if (ULevelStreaming* SL = FindStreamingLevelForPackage(World, Tile.PackageName))
			{
				if (SL->IsLevelLoaded())
				{
					if (ULevel* Level = SL->GetLoadedLevel())
					{
						for (AActor* Actor : Level->Actors)
						{
							if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(Actor))
							{
								PrepareWcLandscapeProxy(Landscape);
								if (IsLandscapeReady(Landscape))
								{
									bTileReady = true;
									break;
								}
							}
						}
					}
				}
			}
			if (!bTileReady)
			{
				const FBox TileBounds = Tile.Info.Bounds;
				for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
				{
					ALandscapeProxy* Landscape = *It;
					if (!Landscape || !TileBounds.Intersect(Landscape->GetComponentsBoundingBox()))
					{
						continue;
					}
					PrepareWcLandscapeProxy(Landscape);
					if (IsLandscapeReady(Landscape))
					{
						bTileReady = true;
						break;
					}
				}
			}
			if (bTileReady)
			{
				++ReadyTiles;
			}
		}

		return RequiredTiles > 0 && ReadyTiles >= RequiredTiles;
	}
}

void ACEWcTerrainRuntime::ReleaseWcStreamingFlagsHold(UWorld* World)
{
	if (!World || !World->WorldComposition)
	{
		return;
	}
	for (ULevelStreaming* SL : World->WorldComposition->TilesStreaming)
	{
		if (SL)
		{
			SL->bDisableDistanceStreaming = false;
		}
	}
}

void ACEWcTerrainRuntime::SetWcDistanceStreamingEnabled(UWorld* World, bool bEnabled)
{
	UWorldComposition* WC = World ? World->WorldComposition : nullptr;
	if (!WC)
	{
		return;
	}
	for (FWorldCompositionTile& Tile : WC->GetTilesList())
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(Tile.PackageName.ToString());
		if (!IsLandscapeTileAssetName(AssetName))
		{
			continue;
		}
		FWorldTileInfo Info = Tile.Info;
		if (bEnabled)
		{
			Info.Layer.DistanceStreamingEnabled = true;
			Info.Layer.StreamingDistance = WcLandscapeStreamingDistanceCm;
			Tile.Info = Info;
		}
		else if (Info.Layer.DistanceStreamingEnabled)
		{
			Info.Layer.DistanceStreamingEnabled = false;
			Tile.Info = Info;
		}
	}
}

void ACEWcTerrainRuntime::HideWcLandscapesForLogin(UWorld* World)
{
	if (!World)
	{
		return;
	}
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		Landscape->SetActorHiddenInGame(true);
		Landscape->SetActorEnableCollision(false);
		TArray<ULandscapeComponent*> Components;
		Landscape->GetComponents(Components);
		for (ULandscapeComponent* LC : Components)
		{
			if (LC)
			{
				LC->SetVisibility(false, true);
				LC->SetHiddenInGame(true);
				LC->SetComponentTickEnabled(false);
			}
		}
	}
}

void ACEWcTerrainRuntime::ShowWcLandscapesAfterLogin(UWorld* World)
{
	if (!World)
	{
		return;
	}
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		PrepareWcLandscapeProxy(*It);
	}
}

void ACEWcTerrainRuntime::RequestTerrainAround(UWorld* World, const FVector& WorldLocation, int32 RingTiles)
{
	if (!World || !World->WorldComposition || World->WorldComposition->GetTilesList().Num() == 0)
	{
		return;
	}
	const FBox Neighborhood = BuildWcNeighborhoodBox(WorldLocation, RingTiles);
	ForceLoadWcTilesInBox(World, WorldLocation, RingTiles, /*bBlockOnLoad*/ false);
	World->FlushLevelStreaming(EFlushLevelStreamingType::Visibility);
}

void ACEWcTerrainRuntime::RequestTerrainAroundBlocking(UWorld* World, const FVector& WorldLocation, int32 RingTiles)
{
	if (!World || !World->WorldComposition || World->WorldComposition->GetTilesList().Num() == 0)
	{
		return;
	}
	const FBox Neighborhood = BuildWcNeighborhoodBox(WorldLocation, RingTiles);
	ForceLoadWcTilesInBox(World, WorldLocation, RingTiles, /*bBlockOnLoad*/ true);
	World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	ReleaseBlockOnLoadFlags(World);
}

bool ACEWcTerrainRuntime::IsTerrainReadyAround(UWorld* World, const FVector& WorldLocation, int32 RingTiles)
{
	if (!World || !World->WorldComposition || World->WorldComposition->GetTilesList().Num() == 0)
	{
		return true;
	}
	const FBox Neighborhood = BuildWcNeighborhoodBox(WorldLocation, RingTiles);
	return IsWcTerrainReadyInBox(World, Neighborhood);
}

void ACEWcTerrainRuntime::MaintainLandscapePresentation(UWorld* World)
{
	if (!World || !World->WorldComposition || World->WorldComposition->GetTilesList().Num() == 0)
	{
		return;
	}
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		PrepareWcLandscapeProxy(*It);
	}
}
