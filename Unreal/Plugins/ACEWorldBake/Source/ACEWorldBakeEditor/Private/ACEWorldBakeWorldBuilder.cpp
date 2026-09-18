#include "ACEWorldBakeWorldBuilder.h"

#include "ACEWorldBakeBuildWorldCommandlet.h"
#include "ACEWorldBakeEditor.h"
#include "ACEWorldBakeEditorInternal.h"
#include "EngineUtils.h"
#include "ACEWorldBakeEditorFunctionLibrary.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeEdEngine.h"
#include "ACEWorldBakeMaterialImporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Commandlets/Commandlet.h"
#include "Engine/WorldComposition.h"
#include "FileHelpers.h"
#include "EditorLevelUtils.h"
#include "LevelUtils.h"
#include "LandscapeInfo.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/PlayerStart.h"
#include "Landscape.h"
#include "LandscapeEditorModule.h"
#include "LandscapeFileFormatInterface.h"
#include "LandscapeStreamingProxy.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "HAL/FileManager.h"
#include "UnrealEd.h"

#define LOCTEXT_NAMESPACE "ACEWorldBakeWorldBuilder"

namespace ACEWorldBakeMap
{
	FVector GetLandscapeScale()
	{
		return FVector(2400.f, 2400.f, 12800.f);
	}

	FIntPoint GetTileCoordOffset()
	{
		return FIntPoint(-4, -4);
	}
}

static bool ACEWorldBakeIsLandscapeTileAssetName(const FString& AssetName)
{
	TArray<FString> Parts;
	AssetName.ParseIntoArray(Parts, TEXT("_"));
	return Parts.Num() == 3 && Parts[0] == TEXT("Tile") && Parts[1].StartsWith(TEXT("x")) && Parts[2].StartsWith(TEXT("y"));
}

static bool ACEWorldBakeIsSubLevelAssetName(const FString& AssetName)
{
	return AssetName.EndsWith(TEXT("_Landblocks"))
		|| (AssetName.Contains(TEXT("_Scenes")) && AssetName.StartsWith(TEXT("Tile_")));
}

static void ACEWorldBakeSyncWorldCompositionStreamingLevels(UWorld* World)
{
	if (World && World->WorldComposition)
	{
		World->SetStreamingLevels(World->WorldComposition->TilesStreaming);
	}
}

static bool ACEWorldBakeMoveMapPackageFiles(const FString& SourceFilePath, const FString& DestFilePath)
{
	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.FileExists(*SourceFilePath))
	{
		return false;
	}

	if (FileManager.FileExists(*DestFilePath))
	{
		FileManager.Delete(*DestFilePath, false, true);
	}

	const FString SourceBase = FPaths::GetBaseFilename(SourceFilePath);
	const FString DestBase = FPaths::GetPath(DestFilePath) / SourceBase;
	static const TCHAR* MapSidecarExtensions[] = { TEXT(".umap"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl") };

	bool bMovedAny = false;
	for (const TCHAR* Extension : MapSidecarExtensions)
	{
		const FString SourceFile = FPaths::ChangeExtension(SourceFilePath, Extension);
		if (!FileManager.FileExists(*SourceFile))
		{
			continue;
		}

		const FString DestFile = DestBase + Extension;
		if (FileManager.FileExists(*DestFile))
		{
			FileManager.Delete(*DestFile, false, true);
		}

		if (!FileManager.Move(*DestFile, *SourceFile, true, true, false, true))
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to move %s -> %s"), *SourceFile, *DestFile);
			return false;
		}

		bMovedAny = true;
	}

	return bMovedAny;
}

static void ACEWorldBakeFindSubLevelPackagesForLandscapeTile(int32 TileX, int32 TileY, TArray<FString>& OutPackageNames)
{
	const FString SearchDir = FPackageName::LongPackageNameToFilename(ACEWorldBakeMap::SubLevelContentRoot, TEXT(""));
	const FString Prefix = FString::Printf(TEXT("Tile_x%d_y%d"), TileX, TileY);

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *SearchDir, TEXT("*.umap"), true, false);
	for (const FString& FilePath : FoundFiles)
	{
		const FString BaseName = FPaths::GetBaseFilename(FilePath);
		const bool bIsLandblocks = BaseName == Prefix + TEXT("_Landblocks");
		const bool bIsScenes = BaseName.StartsWith(Prefix + TEXT("_")) && BaseName.EndsWith(TEXT("_Scenes"));
		if (!bIsLandblocks && !bIsScenes)
		{
			continue;
		}

		FString PackageName;
		if (FPackageName::TryConvertFilenameToLongPackageName(FilePath, PackageName))
		{
			OutPackageNames.Add(PackageName);
		}
	}
}

static ULevelStreaming* ACEWorldBakeAddSubLevelToWorld(UWorld* World, const FString& SubLevelPackageName)
{
	if (!World)
	{
		return nullptr;
	}

	ULevelStreaming* LevelStreaming = FLevelUtils::FindStreamingLevel(World, *SubLevelPackageName);
	if (!LevelStreaming)
	{
		ULevelStreamingDynamic* DynamicLevel = NewObject<ULevelStreamingDynamic>(World, NAME_None, RF_NoFlags);
		DynamicLevel->SetWorldAssetByPackageName(FName(*SubLevelPackageName));
		DynamicLevel->LevelColor = FLinearColor::MakeRandomColor();
		DynamicLevel->bInitiallyLoaded = false;
		DynamicLevel->bInitiallyVisible = false;
		DynamicLevel->SetShouldBeLoaded(false);
		DynamicLevel->SetShouldBeVisible(false);
		DynamicLevel->bShouldBlockOnLoad = false;
		World->AddStreamingLevel(DynamicLevel);
		World->MarkPackageDirty();
		LevelStreaming = DynamicLevel;
	}

	return LevelStreaming;
}

static void ACEWorldBakeSetStreamingLevelUnloaded(ULevelStreaming* LevelStreaming)
{
	if (ULevelStreamingDynamic* DynamicLevel = Cast<ULevelStreamingDynamic>(LevelStreaming))
	{
		DynamicLevel->SetShouldBeLoaded(false);
		DynamicLevel->SetShouldBeVisible(false);
		DynamicLevel->bShouldBlockOnLoad = false;
		DynamicLevel->bInitiallyLoaded = false;
		DynamicLevel->bInitiallyVisible = false;
	}
}

/** Remove streaming level subobjects from a tile world package (not just clear the TArray). */
static int32 ACEWorldBakeRemoveAllStreamingLevelsFromWorld(UWorld* TileWorld)
{
	if (!TileWorld)
	{
		return 0;
	}

	int32 Removed = 0;
	const TArray<ULevelStreaming*> Snapshot = TileWorld->GetStreamingLevels();
	for (ULevelStreaming* SL : Snapshot)
	{
		if (SL)
		{
			TileWorld->RemoveStreamingLevel(SL);
			SL->MarkAsGarbage();
			++Removed;
		}
	}
	TileWorld->SetStreamingLevels(TArray<ULevelStreaming*>());

	TArray<ULevelStreaming*> Orphans;
	TArray<UObject*> ObjectsInWorld;
	GetObjectsWithOuter(TileWorld, ObjectsInWorld, false);
	for (UObject* Obj : ObjectsInWorld)
	{
		if (ULevelStreaming* SL = Cast<ULevelStreaming>(Obj))
		{
			Orphans.Add(SL);
		}
	}

	for (ULevelStreaming* SL : Orphans)
	{
		SL->MarkAsGarbage();
		++Removed;
	}

	return Removed;
}

static FIntPoint ExtractTileCoordinates(const FString& BaseFilename)
{
	FIntPoint ResultPosition(-1, -1);

	const int32 XPos = BaseFilename.Find(TEXT("_x"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	const int32 YPos = BaseFilename.Find(TEXT("_y"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (XPos != INDEX_NONE && YPos != INDEX_NONE && XPos < YPos)
	{
		const FString XCoord = BaseFilename.Mid(XPos + 2, YPos - (XPos + 2));
		const FString YCoord = BaseFilename.Mid(YPos + 2, BaseFilename.Len() - (YPos + 2));

		if (XCoord.IsNumeric() && YCoord.IsNumeric())
		{
			TTypeFromString<int32>::FromString(ResultPosition.X, *XCoord);
			TTypeFromString<int32>::FromString(ResultPosition.Y, *YCoord);
		}
	}

	return ResultPosition;
}

static bool ReadHeightmapFile(TArray<uint16>& Result, const FString& Filename, int32 ExpectedWidth, int32 ExpectedHeight)
{
	ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");
	const ILandscapeHeightmapFileFormat* HeightmapFormat = LandscapeEditorModule.GetHeightmapFormatByExtension(*FPaths::GetExtension(Filename, true));

	if (!HeightmapFormat)
	{
		return false;
	}

	const FLandscapeHeightmapImportData ImportData = HeightmapFormat->Import(*Filename, NAME_None, { static_cast<uint32>(ExpectedWidth), static_cast<uint32>(ExpectedHeight) });
	if (ImportData.ResultCode != ELandscapeImportResult::Error)
	{
		Result = ImportData.Data;
		return true;
	}

	UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to read heightmap %s: %s"), *Filename, *ImportData.ErrorMessage.ToString());
	Result.Empty();
	return false;
}

static UMaterialInterface* LoadMaterialIfExists(const FString& ObjectPath)
{
	return Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ObjectPath));
}

static bool ACEWorldBakeEnableWorldComposition(UWorld* World)
{
	if (!World || World->WorldType != EWorldType::Editor)
	{
		return false;
	}

	AWorldSettings* WorldSettings = World->GetWorldSettings();
	if (WorldSettings)
	{
		WorldSettings->Modify();
		WorldSettings->bEnableWorldComposition = true;
		WorldSettings->bEnableWorldBoundsChecks = false;
	}

	if (World->WorldComposition == nullptr)
	{
		const FString RootPackageName = World->GetOutermost()->GetName();
		if (!FPackageName::DoesPackageExist(RootPackageName))
		{
			UE_LOG_ACEWORLDBAKEED(Error, TEXT("Cannot enable World Composition — map package not saved: %s"), *RootPackageName);
			return false;
		}

		UWorldComposition* WorldComposition = NewObject<UWorldComposition>(World);
		World->WorldComposition = WorldComposition;
		WorldComposition->Rescan();
		UWorldComposition::WorldCompositionChangedEvent.Broadcast(World);
	}
	else
	{
		World->WorldComposition->Rescan();
	}

	return World->WorldComposition != nullptr;
}

static ULevel* ACEWorldBakeLoadOrCreateTileLevel(UWorld* EditorWorld, const FString& MapPackageName, const FString& MapFilePath)
{
	if (!EditorWorld)
	{
		return nullptr;
	}

	if (!FPaths::FileExists(MapFilePath))
	{
		UWorld* TempWorld = UWorld::CreateWorld(EWorldType::None, false);
		if (!TempWorld || !FEditorFileUtils::SaveLevel(TempWorld->PersistentLevel, *MapFilePath))
		{
			if (TempWorld)
			{
				TempWorld->DestroyWorld(false);
			}
			return nullptr;
		}
		TempWorld->DestroyWorld(false);

		if (EditorWorld->WorldComposition)
		{
			EditorWorld->WorldComposition->Rescan();
		}
	}

	ULevelStreaming* LevelStreaming = FLevelUtils::FindStreamingLevel(EditorWorld, *MapPackageName);
	if (!LevelStreaming)
	{
		LevelStreaming = UEditorLevelUtils::AddLevelToWorld(
			EditorWorld,
			*MapPackageName,
			ULevelStreamingDynamic::StaticClass());

		if (EditorWorld->WorldComposition)
		{
			EditorWorld->WorldComposition->Rescan();
		}
	}

	if (!LevelStreaming)
	{
		return nullptr;
	}

	if (ULevelStreamingDynamic* DynamicLevel = Cast<ULevelStreamingDynamic>(LevelStreaming))
	{
		DynamicLevel->SetShouldBeLoaded(true);
		DynamicLevel->SetShouldBeVisible(true);
		DynamicLevel->bShouldBlockOnLoad = true;
		DynamicLevel->bInitiallyLoaded = true;
		DynamicLevel->bInitiallyVisible = true;

		if (!EditorWorld->GetStreamingLevels().Contains(LevelStreaming))
		{
			EditorWorld->AddStreamingLevel(DynamicLevel);
		}

		if (EditorWorld->WorldComposition && !EditorWorld->WorldComposition->TilesStreaming.Contains(LevelStreaming))
		{
			EditorWorld->WorldComposition->TilesStreaming.Add(LevelStreaming);
		}

		EditorWorld->FlushLevelStreaming();
		DynamicLevel->bShouldBlockOnLoad = false;
	}

	return LevelStreaming->GetLoadedLevel();
}

bool FACEWorldBakeWorldBuilder::ParseOptionsFromCommandLine(const FString& Params, FACEWorldBakeWorldBuildOptions& OutOptions)
{
	TArray<FString> Switches;
	TArray<FString> Tokens;
	const FString ParamsLower = Params.ToLower();
	UCommandlet::ParseCommandLine(*ParamsLower, Tokens, Switches);

	if (Switches.Contains(TEXT("exportonly")))
	{
		OutOptions.bImportTextures = false;
		OutOptions.bImportModels = false;
		OutOptions.bImportSetups = false;
		OutOptions.bImportSurfaces = false;
		OutOptions.bImportLandscapeMaterials = false;
		OutOptions.bCreateDerethMap = false;
		OutOptions.bImportTiledLandscape = false;
		OutOptions.bImportLandscapeMaterialInstances = false;
		OutOptions.bImportLandblockT3D = false;
		OutOptions.bUpdateDefaultMap = false;
		OutOptions.bSaveAll = false;
	}

	if (Switches.Contains(TEXT("importonly")))
	{
		OutOptions.bExportResources = false;
		OutOptions.bExportLandblocks = false;
	}

	if (Switches.Contains(TEXT("skiplandscape")))
	{
		OutOptions.bImportTiledLandscape = false;
		OutOptions.bImportLandscapeMaterialInstances = false;
	}

	if (Switches.Contains(TEXT("skipsetup")))
	{
		OutOptions.bImportSetups = false;
	}

	if (Switches.Contains(TEXT("skipmodels")))
	{
		OutOptions.bImportModels = false;
	}

	if (Switches.Contains(TEXT("skiplandblocks")))
	{
		OutOptions.bImportLandblockT3D = false;
	}

	if (Switches.Contains(TEXT("reconfigurewc")))
	{
		OutOptions = FACEWorldBakeWorldBuildOptions{};
		OutOptions.bCreateDerethMap = true;
		OutOptions.bReconfigureWorldComposition = true;
		OutOptions.bUpdateDefaultMap = false;
	}

	if (FParse::Param(*Params, TEXT("NODEFAULTMAP")))
	{
		OutOptions.bUpdateDefaultMap = false;
	}

	FString DatPath;
	if (FParse::Value(*Params, TEXT("DATPATH="), DatPath) && DatPath.Len() > 0)
	{
		OutOptions.DatDirectory = DatPath;
	}

	return true;
}

bool FACEWorldBakeWorldBuilder::Run(const FACEWorldBakeWorldBuildOptions& Options)
{
	if (!GEditor)
	{
		UE_LOG_ACEWORLDBAKEED(Error, TEXT("Build world requires an editor context (IsEditor commandlet or editor session)."));
		return false;
	}

	if (Options.DatDirectory.Len() > 0)
	{
		UACEWorldBakeEditorFunctionLibrary::SetDatFileDirectory(Options.DatDirectory);
	}

	UE_LOG_ACEWORLDBAKEED(Log, TEXT("ACEWorldBake: starting full world build"));

	if (Options.bReconfigureWorldComposition)
	{
		if (!MigrateSubLevelPackagesOutOfWorldComposition())
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Sub-level package migration had failures — continuing"));
		}

		if (!ClearWorldCompositionTileInfoOnSubLevels())
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Clearing WC tile info on sub-levels had failures — continuing"));
		}

		if (!StripSubLevelStreamingFromLandscapeTiles())
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Stripping nested sub-level streaming refs had failures — continuing"));
		}

		if (!EnsureDerethPersistentMap(Options))
		{
			return false;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World && World->WorldComposition)
		{
			FinalizeWorldCompositionForEditor(World);

			const FString MapFilePath = FPackageName::LongPackageNameToFilename(
				ACEWorldBakeMap::MapPackage, FPackageName::GetMapPackageExtension());
			if (!FEditorFileUtils::SaveLevel(World->PersistentLevel, *MapFilePath))
			{
				UE_LOG_ACEWORLDBAKEED(Error, TEXT("Failed to save Dereth after WC reconfigure"));
				return false;
			}
		}

		UE_LOG_ACEWORLDBAKEED(Log, TEXT("ACEWorldBake: world composition reconfigured (64 WC landscape tiles, sub-level streaming stripped from tiles)"));
		return true;
	}

	if (Options.bExportResources || Options.bExportLandblocks)
	{
		if (!ExportPhase(Options))
		{
			return false;
		}
	}

	if (Options.bImportTextures || Options.bImportModels || Options.bImportSetups || Options.bImportSurfaces
		|| Options.bImportLandscapeMaterials || Options.bCreateDerethMap || Options.bImportTiledLandscape
		|| Options.bImportLandscapeMaterialInstances || Options.bImportLandblockT3D)
	{
		if (!ImportContentPhase(Options))
		{
			return false;
		}
	}

	if (Options.bUpdateDefaultMap)
	{
		UpdateDefaultMapSetting();
	}

	UE_LOG_ACEWORLDBAKEED(Log, TEXT("ACEWorldBake: world build finished"));
	return true;
}

bool FACEWorldBakeWorldBuilder::ExportPhase(const FACEWorldBakeWorldBuildOptions& Options)
{
	IACEWorldBakeDatToolsModule* DatTools = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	if (!DatTools)
	{
		UE_LOG_ACEWORLDBAKEED(Error, TEXT("ACEWorldBakeDatTools module not loaded."));
		return false;
	}

	if (Options.bExportResources)
	{
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Exporting portal resources..."));
		for (const EACEWorldBakeResource::Type ResourceType : {
			EACEWorldBakeResource::Model,
			EACEWorldBakeResource::Setup,
			EACEWorldBakeResource::Animation,
			EACEWorldBakeResource::Palette,
			EACEWorldBakeResource::ImageTexture,
			EACEWorldBakeResource::ImageColor,
			EACEWorldBakeResource::Surface,
			EACEWorldBakeResource::MotionTable,
			EACEWorldBakeResource::Sound,
			EACEWorldBakeResource::Environment,
			EACEWorldBakeResource::Scene,
			EACEWorldBakeResource::Region,
			EACEWorldBakeResource::SoundTable,
			EACEWorldBakeResource::EnumMapper,
			EACEWorldBakeResource::ParticleEmitter,
			EACEWorldBakeResource::PhysicsScript,
			EACEWorldBakeResource::PhysicsScriptTable })
		{
			DatTools->ExportResources(ResourceType, 0x0000FFFF);
		}
	}

	if (Options.bExportLandblocks)
	{
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Exporting landblocks, heightmaps, and T3D..."));
		DatTools->ExportLandblocks();
	}

	return true;
}

bool FACEWorldBakeWorldBuilder::ImportContentPhase(const FACEWorldBakeWorldBuildOptions& Options)
{
	if (Options.bImportTextures)
	{
		const FString ImagesDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Images"));
		const FString LandblocksDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Landblocks"));
		const FString PalettesDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Palettes"));
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Importing textures from Saved/Images, Saved/Landblocks, and Saved/Palettes (synchronous in unattended mode)"));
		UACEWorldBakeEditorFunctionLibrary::ImportTextures(ImagesDir, TEXT("/Game/Images"));
		UACEWorldBakeEditorFunctionLibrary::ImportTextures(LandblocksDir, TEXT("/Game/Landblocks"));
		UACEWorldBakeEditorFunctionLibrary::ImportTextures(PalettesDir, TEXT("/Game/Palettes"));
		ACEWorldBakeSaveDirtyContentPackages();
	}

	UE_LOG_ACEWORLDBAKEED(Log, TEXT("Ensuring stub parent materials exist under /Game/Materials and /Game/Landblocks"));
	FACEWorldBakeMaterialImporter::EnsureStubParentMaterials();
	ACEWorldBakeSaveDirtyContentPackages();

	UMaterial* DiffuseParent = Cast<UMaterial>(LoadMaterialIfExists(Options.DiffuseParentMatPath));
	UMaterial* DiffuseMaskedParent = Cast<UMaterial>(LoadMaterialIfExists(Options.DiffuseMaskedParentMatPath));
	UMaterial* PalettedParent = Cast<UMaterial>(LoadMaterialIfExists(Options.PalettedParentMatPath));
	UMaterial* PalettedMaskedParent = Cast<UMaterial>(LoadMaterialIfExists(Options.PalettedMaskedParentMatPath));
	UMaterialInterface* LandscapeParent = LoadMaterialIfExists(Options.LandscapeParentMatPath);

	if (Options.bImportLandscapeMaterials)
	{
		if (LandscapeParent)
		{
			UE_LOG_ACEWORLDBAKEED(Log, TEXT("Importing landscape parent materials"));
			UACEWorldBakeEditorFunctionLibrary::ImportLandscapeMaterials(LandscapeParent);
		}
		else
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Skipping landscape materials — parent not found at %s"), *Options.LandscapeParentMatPath);
		}
	}

	if (Options.bImportSurfaces && DiffuseParent && DiffuseMaskedParent && PalettedParent && PalettedMaskedParent)
	{
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Importing surface materials"));
		UACEWorldBakeEditorFunctionLibrary::ImportSurfaces(0x0000FFFF, DiffuseParent, DiffuseMaskedParent, TEXT("Diffuse"), PalettedParent, PalettedMaskedParent, TEXT("Indexed"), TEXT("Palette"));
	}
	else if (Options.bImportSurfaces)
	{
		UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Skipping surface materials — parent materials missing under /Game/Materials/"));
	}

	if (Options.bImportModels)
	{
		const FString ModelsDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Models"));
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Importing models (after surfaces)"));
		UACEWorldBakeEditorFunctionLibrary::ImportModels(ModelsDir, TEXT("/Game/Models"));
		ACEWorldBakeSaveDirtyContentPackages();
	}

	if (Options.bCreateDerethMap)
	{
		if (!EnsureDerethPersistentMap(Options))
		{
			return false;
		}
	}

	if (Options.bImportTiledLandscape)
	{
		if (!ImportTiledLandscapeFromSaved())
		{
			UE_LOG_ACEWORLDBAKEED(Error, TEXT("Tiled landscape import failed."));
		}
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World && World->WorldComposition && (Options.bImportTiledLandscape || Options.bImportLandblockT3D))
	{
		ConfigureWorldCompositionStreaming(World);
	}

	if (Options.bImportLandscapeMaterialInstances)
	{
		UMaterialInterface* LandscapeMat = LoadMaterialIfExists(Options.LandscapeMaterialPath);
		if (!LandscapeMat)
		{
			LandscapeMat = LandscapeParent;
		}

		if (LandscapeMat)
		{
			UE_LOG_ACEWORLDBAKEED(Log, TEXT("Importing per-tile landscape material instances"));
			UACEWorldBakeEditorFunctionLibrary::ImportLandscapeMaterialInstances(LandscapeMat);
		}
		else
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Skipping landscape MICs — no landscape material found"));
		}
	}

	if (Options.bImportSetups)
	{
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Importing setups (this can take a long time)"));
		UACEWorldBakeEditorFunctionLibrary::ImportSetups(0x0000FFFF);
		if (UACEWorldBakeEdEngine* BakeEd = Cast<UACEWorldBakeEdEngine>(GEditor))
		{
			BakeEd->PumpBlueprintImportQueueUntilIdle(0.0);
		}
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Saving setup blueprints"));
		ACEWorldBakeSaveDirtyContentPackages();
	}

	if (Options.bImportLandblockT3D)
	{
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Queueing landblock and scene T3D imports"));
		QueueAllLandblockImports();
		if (UACEWorldBakeEdEngine* BakeEd = Cast<UACEWorldBakeEdEngine>(GEditor))
		{
			BakeEd->PumpT3DImportQueueUntilIdle(0.0);
		}

		if (World && World->WorldComposition)
		{
			MigrateSubLevelPackagesOutOfWorldComposition();
			ClearWorldCompositionTileInfoOnSubLevels();
			StripSubLevelStreamingFromLandscapeTiles();
			EnsureDerethPersistentMap(Options);
			World = GEditor->GetEditorWorldContext().World();
			if (World && World->WorldComposition)
			{
				FinalizeWorldCompositionForEditor(World);
			}
		}
	}

	if (World && World->WorldComposition)
	{
		FinalizeWorldCompositionForEditor(World);

		const FString MapFilePath = FPackageName::LongPackageNameToFilename(
			ACEWorldBakeMap::MapPackage, FPackageName::GetMapPackageExtension());
		if (!FEditorFileUtils::SaveLevel(World->PersistentLevel, *MapFilePath))
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to save Dereth persistent level with unloaded WC tiles"));
		}
		else
		{
			UE_LOG_ACEWORLDBAKEED(Log, TEXT("Saved Dereth persistent level — all WC tiles unloaded for editor open"));
		}
	}

	if (Options.bSaveAll)
	{
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Saving packages"));
		ACEWorldBakeSaveAllDirtyPackages();
	}

	return true;
}

bool FACEWorldBakeWorldBuilder::EnsureDerethPersistentMap(const FACEWorldBakeWorldBuildOptions& Options)
{
	const FString MapPackage = ACEWorldBakeMap::MapPackage;
	const FString MapFilePath = FPackageName::LongPackageNameToFilename(MapPackage, FPackageName::GetMapPackageExtension());

	if (!FPaths::FileExists(MapFilePath))
	{
		UE_LOG_ACEWORLDBAKEED(Log, TEXT("Creating Dereth persistent map at %s"), *MapPackage);
		UWorld* TemplateWorld = UEditorLoadingAndSavingUtils::NewMapFromTemplate(TEXT("/Engine/MapTemplates/Template_Default"), true);
		if (!TemplateWorld)
		{
			UE_LOG_ACEWORLDBAKEED(Error, TEXT("Failed to create map from template."));
			return false;
		}

		if (!FEditorFileUtils::SaveLevel(TemplateWorld->PersistentLevel, *MapFilePath))
		{
			UE_LOG_ACEWORLDBAKEED(Error, TEXT("Failed to save new Dereth map."));
			return false;
		}
	}

	if (!FEditorFileUtils::LoadMap(MapFilePath, false, true))
	{
		UE_LOG_ACEWORLDBAKEED(Error, TEXT("Failed to load Dereth map."));
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return false;
	}

	if (!ACEWorldBakeEnableWorldComposition(World))
	{
		UE_LOG_ACEWORLDBAKEED(Error, TEXT("Failed to enable World Composition on Dereth map."));
		return false;
	}

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		It->SetActorLocation(FVector(800000.f, -1000000.f, 9500.f));
		break;
	}

	if (!FEditorFileUtils::SaveLevel(World->PersistentLevel, *MapFilePath))
	{
		UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to save Dereth map after enabling World Composition."));
	}

	World->MarkPackageDirty();
	return true;
}

bool FACEWorldBakeWorldBuilder::ImportTiledLandscapeFromSaved()
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !World->WorldComposition)
	{
		UE_LOG_ACEWORLDBAKEED(Error, TEXT("Dereth map must have World Composition enabled before importing landscape tiles."));
		return false;
	}

	const FString LandblocksDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Landblocks"));
	TArray<FString> HeightmapFiles;
	IFileManager::Get().FindFiles(HeightmapFiles, *LandblocksDir, TEXT("r16"));

	HeightmapFiles.RemoveAll([](const FString& File)
	{
		return File.Contains(TEXT("_All"), ESearchCase::IgnoreCase);
	});

	if (HeightmapFiles.Num() == 0)
	{
		UE_LOG_ACEWORLDBAKEED(Error, TEXT("No Tile_x*_y*.r16 heightmaps found in %s"), *LandblocksDir);
		return false;
	}

	const FVector LandscapeScale = ACEWorldBakeMap::GetLandscapeScale();
	const int32 SizeX = ACEWorldBakeMap::TileSizeVerts;
	const int32 SizeY = ACEWorldBakeMap::TileSizeVerts;

	FGuid LandscapeGuid;
	ALandscape* MainLandscape = nullptr;
	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		MainLandscape = *It;
		break;
	}

	if (!MainLandscape)
	{
		LandscapeGuid = FGuid::NewGuid();
		MainLandscape = World->SpawnActor<ALandscape>();
		MainLandscape->SetActorTransform(FTransform(FQuat::Identity, FVector::ZeroVector, LandscapeScale));
		MainLandscape->ComponentSizeQuads = ACEWorldBakeMap::TileQuadsPerSection * ACEWorldBakeMap::TileSectionsPerComponent;
		MainLandscape->NumSubsections = ACEWorldBakeMap::TileSectionsPerComponent;
		MainLandscape->SubsectionSizeQuads = ACEWorldBakeMap::TileQuadsPerSection;
		MainLandscape->SetLandscapeGuid(LandscapeGuid);
		MainLandscape->CreateLandscapeInfo();
		MainLandscape->RegisterAllComponents();
	}
	else
	{
		LandscapeGuid = MainLandscape->GetLandscapeGuid();
		if (ULandscapeInfo* LandscapeInfo = MainLandscape->CreateLandscapeInfo())
		{
			LandscapeInfo->RegisterActor(MainLandscape);
		}
	}

	FScopedSlowTask SlowTask(static_cast<float>(HeightmapFiles.Num()), LOCTEXT("ImportTiledLandscape", "Importing tiled landscape"));
	if (!FApp::IsUnattended())
	{
		SlowTask.MakeDialog();
	}

	for (const FString& HeightmapFile : HeightmapFiles)
	{
		SlowTask.EnterProgressFrame();

		const FString HeightmapPath = FPaths::Combine(LandblocksDir, HeightmapFile);
		const FString TileName = FPaths::GetBaseFilename(HeightmapFile);
		const FIntPoint TileCoordinate = ExtractTileCoordinates(TileName);
		if (TileCoordinate.X < 0 || TileCoordinate.Y < 0)
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Skipping heightmap with unrecognized tile name: %s"), *HeightmapFile);
			continue;
		}

		const FString MapPackageName = FString::Printf(TEXT("%s/%s"), *ACEWorldBakeMap::TileContentRoot, *TileName);
		const FString MapFilePath = FPackageName::LongPackageNameToFilename(MapPackageName, FPackageName::GetMapPackageExtension());

		TArray<uint16> HeightData;
		if (!ReadHeightmapFile(HeightData, HeightmapPath, SizeX, SizeY))
		{
			continue;
		}

		ULevel* TileLevel = ACEWorldBakeLoadOrCreateTileLevel(World, MapPackageName, MapFilePath);
		if (!TileLevel)
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to load tile level %s"), *MapPackageName);
			continue;
		}

		for (AActor* Actor : TileLevel->Actors)
		{
			if (ALandscapeStreamingProxy* ExistingProxy = Cast<ALandscapeStreamingProxy>(Actor))
			{
				World->DestroyActor(ExistingProxy);
			}
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.OverrideLevel = TileLevel;
		ALandscapeStreamingProxy* LandscapeProxy = World->SpawnActor<ALandscapeStreamingProxy>(SpawnParameters);
		if (!LandscapeProxy)
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to spawn landscape proxy for %s"), *TileName);
			continue;
		}

		LandscapeProxy->SetActorTransform(FTransform(FQuat::Identity, FVector::ZeroVector, LandscapeScale));
		LandscapeProxy->SetLandscapeGuid(LandscapeGuid);
		LandscapeProxy->SetLandscapeActor(MainLandscape);

		TMap<FGuid, TArray<uint16>> HeightmapDataPerLayers;
		TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
		HeightmapDataPerLayers.Add(FGuid(), MoveTemp(HeightData));
		MaterialLayerDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

		LandscapeProxy->Import(
			LandscapeGuid,
			0, 0, SizeX - 1, SizeY - 1,
			ACEWorldBakeMap::TileSectionsPerComponent,
			ACEWorldBakeMap::TileQuadsPerSection,
			HeightmapDataPerLayers,
			*HeightmapPath,
			MaterialLayerDataPerLayer,
			ELandscapeImportAlphamapType::Additive,
			TArrayView<const FLandscapeLayer>());

		LandscapeProxy->ReimportHeightmapFilePath = HeightmapPath;

		const FIntRect LandscapeRect = LandscapeProxy->GetBoundingRect();
		const float WidthX = LandscapeRect.Width() * LandscapeScale.X;
		const float WidthY = LandscapeRect.Height() * LandscapeScale.Y;
		const FIntPoint TileCoords = TileCoordinate + ACEWorldBakeMap::GetTileCoordOffset();
		const FIntPoint TileOffset(
			FMath::RoundToInt(TileCoords.X * WidthX),
			FMath::RoundToInt(TileCoords.Y * WidthY));

		FWorldTileInfo TileInfo;
		TileInfo.Bounds = LandscapeProxy->GetComponentsBoundingBox();
		TileInfo.AbsolutePosition = FIntVector(TileOffset.X, TileOffset.Y, 0);
		TileInfo.Position = TileInfo.AbsolutePosition;

		World->WorldComposition->OnTileInfoUpdated(FName(*MapPackageName), TileInfo);

		if (!FEditorFileUtils::SaveLevel(TileLevel, *MapFilePath))
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to save tile level %s"), *MapPackageName);
		}

		if (ULevelStreaming* TileStreaming = FLevelUtils::FindStreamingLevel(World, *MapPackageName))
		{
			if (ULevelStreamingDynamic* DynamicTile = Cast<ULevelStreamingDynamic>(TileStreaming))
			{
				DynamicTile->SetShouldBeVisible(false);
			}
			World->FlushLevelStreaming();
		}

		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	FEditorFileUtils::SaveLevel(World->PersistentLevel, *FPackageName::LongPackageNameToFilename(ACEWorldBakeMap::MapPackage, FPackageName::GetMapPackageExtension()));

	World->WorldComposition->Rescan();
	World->MarkPackageDirty();
	return true;
}

bool FACEWorldBakeWorldBuilder::ConfigureWorldCompositionStreaming(UWorld* World)
{
	if (!World || !World->WorldComposition)
	{
		return false;
	}

	FWorldTileLayer StreamLayer;
	StreamLayer.Name = TEXT("Stream7000");
	StreamLayer.StreamingDistance = static_cast<int32>(ACEWorldBakeMap::StreamingDistanceCm);
	StreamLayer.DistanceStreamingEnabled = true;

	UWorldComposition::FTilesList& Tiles = World->WorldComposition->GetTilesList();
	for (FWorldCompositionTile& Tile : Tiles)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(Tile.PackageName.ToString());
		if (!ACEWorldBakeIsLandscapeTileAssetName(AssetName))
		{
			continue;
		}

		FWorldTileInfo Info = Tile.Info;
		Info.Layer = StreamLayer;
		World->WorldComposition->OnTileInfoUpdated(Tile.PackageName, Info);
	}

	return true;
}

bool FACEWorldBakeWorldBuilder::UnloadAllWorldCompositionTiles(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	for (ULevelStreaming* LevelStreaming : World->GetStreamingLevels())
	{
		ACEWorldBakeSetStreamingLevelUnloaded(LevelStreaming);
	}

	if (World->WorldComposition)
	{
		for (ULevelStreaming* LevelStreaming : World->WorldComposition->TilesStreaming)
		{
			ACEWorldBakeSetStreamingLevelUnloaded(LevelStreaming);
		}
	}

	World->FlushLevelStreaming();
	return true;
}

void FACEWorldBakeWorldBuilder::FinalizeWorldCompositionForEditor(UWorld* World)
{
	if (!World || !World->WorldComposition)
	{
		return;
	}

	World->WorldComposition->Rescan(true);
	ConfigureWorldCompositionStreaming(World);
	ACEWorldBakeSyncWorldCompositionStreamingLevels(World);
	UnloadAllWorldCompositionTiles(World);
	World->FlushLevelStreaming();
}

bool FACEWorldBakeWorldBuilder::MigrateSubLevelPackagesOutOfWorldComposition()
{
	const FString SourceDir = FPackageName::LongPackageNameToFilename(ACEWorldBakeMap::TileContentRoot, TEXT(""));
	const FString DestDir = FPackageName::LongPackageNameToFilename(ACEWorldBakeMap::SubLevelContentRoot, TEXT(""));
	IFileManager::Get().MakeDirectory(*DestDir, true);

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *SourceDir, TEXT("*.umap"), true, false);

	int32 MovedCount = 0;
	for (const FString& SourceFile : FoundFiles)
	{
		const FString BaseName = FPaths::GetBaseFilename(SourceFile);
		if (!ACEWorldBakeIsSubLevelAssetName(BaseName))
		{
			continue;
		}

		const FString DestFile = DestDir / (BaseName + FPackageName::GetMapPackageExtension());
		if (ACEWorldBakeMoveMapPackageFiles(SourceFile, DestFile))
		{
			++MovedCount;
		}
	}

	UE_LOG_ACEWORLDBAKEED(Log, TEXT("Moved %d landblock/scene map packages to %s"), MovedCount, *ACEWorldBakeMap::SubLevelContentRoot);
	return true;
}

bool FACEWorldBakeWorldBuilder::ClearWorldCompositionTileInfoOnSubLevels()
{
	// Sub-levels under DerethBake are outside the WC scan tree and do not need tile info stripped.
	// Only handle stragglers still present under Dereth/Tile (e.g. partial prior migration).
	const FString SearchDir = FPackageName::LongPackageNameToFilename(ACEWorldBakeMap::TileContentRoot, TEXT(""));

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *SearchDir, TEXT("*.umap"), true, false);

	int32 ClearedCount = 0;
	for (const FString& FilePath : FoundFiles)
	{
		const FString BaseName = FPaths::GetBaseFilename(FilePath);
		if (!ACEWorldBakeIsSubLevelAssetName(BaseName))
		{
			continue;
		}

		FWorldTileInfo TileInfo;
		if (!FWorldTileInfo::Read(FilePath, TileInfo))
		{
			continue;
		}

		FString PackageName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(FilePath, PackageName))
		{
			continue;
		}

		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (!Package)
		{
			UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to load straggler sub-level package: %s"), *PackageName);
			continue;
		}

		Package->SetWorldTileInfo(TUniquePtr<FWorldTileInfo>());
		Package->MarkPackageDirty();

		UWorld* LevelWorld = UWorld::FindWorldInPackage(Package);
		if (!LevelWorld)
		{
			LevelWorld = UWorld::FollowWorldRedirectorInPackage(Package);
		}

		if (LevelWorld && LevelWorld->PersistentLevel)
		{
			if (!FEditorFileUtils::SaveLevel(LevelWorld->PersistentLevel, *FilePath))
			{
				UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to save straggler sub-level after clearing WC tile info: %s"), *PackageName);
				continue;
			}
		}

		++ClearedCount;
	}

	UE_LOG_ACEWORLDBAKEED(Log, TEXT("Cleared WC tile info on %d straggler sub-level packages under Dereth/Tile"), ClearedCount);
	return true;
}

bool FACEWorldBakeWorldBuilder::NestSubLevelsInLandscapeTiles()
{
	FScopedSlowTask SlowTask(
		static_cast<float>(ACEWorldBakeMap::LandscapeTileCount * ACEWorldBakeMap::LandscapeTileCount),
		LOCTEXT("NestSubLevels", "Nesting landblock/scene sub-levels under landscape tiles"));
	if (!FApp::IsUnattended())
	{
		SlowTask.MakeDialog();
	}

	int32 NestedTileCount = 0;
	for (int32 TileY = 0; TileY < ACEWorldBakeMap::LandscapeTileCount; ++TileY)
	{
		for (int32 TileX = 0; TileX < ACEWorldBakeMap::LandscapeTileCount; ++TileX)
		{
			SlowTask.EnterProgressFrame(1.f);

			const FString TilePackageName = FString::Printf(
				TEXT("%s/Tile_x%d_y%d"), *ACEWorldBakeMap::TileContentRoot, TileX, TileY);
			const FString TileFilePath = FPackageName::LongPackageNameToFilename(
				TilePackageName, FPackageName::GetMapPackageExtension());
			if (!FPaths::FileExists(TileFilePath))
			{
				continue;
			}

			TArray<FString> SubLevelPackages;
			ACEWorldBakeFindSubLevelPackagesForLandscapeTile(TileX, TileY, SubLevelPackages);
			if (SubLevelPackages.Num() == 0)
			{
				continue;
			}

			UPackage* TilePackage = LoadPackage(nullptr, *TilePackageName, LOAD_None);
			if (!TilePackage)
			{
				UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to load landscape tile package %s for sub-level nesting"), *TilePackageName);
				continue;
			}

			UWorld* TileWorld = UWorld::FindWorldInPackage(TilePackage);
			if (!TileWorld)
			{
				TileWorld = UWorld::FollowWorldRedirectorInPackage(TilePackage);
			}
			if (!TileWorld || !TileWorld->PersistentLevel)
			{
				UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Landscape tile package %s has no world"), *TilePackageName);
				continue;
			}

			TileWorld->SetStreamingLevels(TArray<ULevelStreaming*>());

			for (const FString& SubLevelPackage : SubLevelPackages)
			{
				ACEWorldBakeAddSubLevelToWorld(TileWorld, SubLevelPackage);
			}

			for (ULevelStreaming* StreamingLevel : TileWorld->GetStreamingLevels())
			{
				if (StreamingLevel)
				{
					StreamingLevel->ClearFlags(RF_Standalone);
				}
			}

			TilePackage->MarkPackageDirty();
			if (!FEditorFileUtils::SaveLevel(TileWorld->PersistentLevel, *TileFilePath))
			{
				UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to save landscape tile %s after nesting sub-levels"), *TilePackageName);
				continue;
			}

			++NestedTileCount;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	}

	UE_LOG_ACEWORLDBAKEED(Log, TEXT("Nested sub-levels under %d landscape tiles"), NestedTileCount);

	if (NestedTileCount == 0)
	{
		UE_LOG_ACEWORLDBAKEED(Warning, TEXT("No landscape tiles were updated with nested sub-levels"));
	}

	return true;
}

bool FACEWorldBakeWorldBuilder::StripSubLevelStreamingFromLandscapeTiles()
{
	FScopedSlowTask SlowTask(
		static_cast<float>(ACEWorldBakeMap::LandscapeTileCount * ACEWorldBakeMap::LandscapeTileCount),
		LOCTEXT("StripSubLevelStreaming", "Stripping nested sub-level streaming refs from landscape tiles"));
	if (!FApp::IsUnattended())
	{
		SlowTask.MakeDialog();
	}

	int32 StrippedTileCount = 0;
	for (int32 TileY = 0; TileY < ACEWorldBakeMap::LandscapeTileCount; ++TileY)
	{
		for (int32 TileX = 0; TileX < ACEWorldBakeMap::LandscapeTileCount; ++TileX)
		{
			SlowTask.EnterProgressFrame(1.f);

			const FString TilePackageName = FString::Printf(
				TEXT("%s/Tile_x%d_y%d"), *ACEWorldBakeMap::TileContentRoot, TileX, TileY);
			const FString TileFilePath = FPackageName::LongPackageNameToFilename(
				TilePackageName, FPackageName::GetMapPackageExtension());
			if (!FPaths::FileExists(TileFilePath))
			{
				continue;
			}

			UPackage* TilePackage = LoadPackage(nullptr, *TilePackageName, LOAD_None);
			if (!TilePackage)
			{
				UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to load landscape tile package %s for streaming strip"), *TilePackageName);
				continue;
			}

			UWorld* TileWorld = UWorld::FindWorldInPackage(TilePackage);
			if (!TileWorld)
			{
				TileWorld = UWorld::FollowWorldRedirectorInPackage(TilePackage);
			}
			if (!TileWorld || !TileWorld->PersistentLevel)
			{
				UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Landscape tile package %s has no world"), *TilePackageName);
				continue;
			}

			const int32 RemovedStreams = ACEWorldBakeRemoveAllStreamingLevelsFromWorld(TileWorld);
			if (RemovedStreams == 0)
			{
				continue;
			}

			TilePackage->MarkPackageDirty();
			if (!FEditorFileUtils::SaveLevel(TileWorld->PersistentLevel, *TileFilePath))
			{
				UE_LOG_ACEWORLDBAKEED(Warning, TEXT("Failed to save landscape tile %s after stripping sub-level streaming"), *TilePackageName);
				continue;
			}

			UE_LOG_ACEWORLDBAKEED(Log, TEXT("Stripped %d streaming level object(s) from %s"), RemovedStreams, *TilePackageName);
			++StrippedTileCount;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	}

	UE_LOG_ACEWORLDBAKEED(Log, TEXT("Stripped nested sub-level streaming refs from %d landscape tiles"), StrippedTileCount);
	return true;
}

bool FACEWorldBakeWorldBuilder::QueueAllLandblockImports()
{
	const int32 LandblockCount = 256;
	for (int32 LandblockY = 0; LandblockY < LandblockCount; ++LandblockY)
	{
		for (int32 LandblockX = 0; LandblockX < LandblockCount; ++LandblockX)
		{
			UACEWorldBakeEditorFunctionLibrary::ImportLandblockInfo(LandblockX, LandblockY, false);
		}
	}

	for (int32 LandblockY = 0; LandblockY < LandblockCount; LandblockY += kSceneLandblockWidth)
	{
		for (int32 LandblockX = 0; LandblockX < LandblockCount; LandblockX += kSceneLandblockWidth)
		{
			UACEWorldBakeEditorFunctionLibrary::ImportLandblockScene(LandblockX, LandblockY, false);
		}
	}

	return true;
}

void FACEWorldBakeWorldBuilder::PumpImportQueueUntilIdle(double MaxSeconds)
{
	if (UACEWorldBakeEdEngine* BakeEd = Cast<UACEWorldBakeEdEngine>(GEditor))
	{
		BakeEd->PumpImportQueueUntilIdle(MaxSeconds);
	}
}

void FACEWorldBakeWorldBuilder::UpdateDefaultMapSetting()
{
	if (!GConfig)
	{
		return;
	}

	const FString ConfigFile = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
	GConfig->SetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), *ACEWorldBakeMap::MapPackage, ConfigFile);
	GConfig->Flush(false, ConfigFile);
	UE_LOG_ACEWORLDBAKEED(Log, TEXT("Set GameDefaultMap to %s (EditorStartupMap unchanged — open Dereth manually to avoid loading all tiles)"), *ACEWorldBakeMap::MapPackage);
}

UACEWorldBakeBuildWorldCommandlet::UACEWorldBakeBuildWorldCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UACEWorldBakeBuildWorldCommandlet::Main(const FString& Params)
{
	FACEWorldBakeWorldBuildOptions Options;
	FACEWorldBakeWorldBuilder::ParseOptionsFromCommandLine(Params, Options);

	const bool bSuccess = FACEWorldBakeWorldBuilder::Run(Options);
	return bSuccess ? 0 : 1;
}

#undef LOCTEXT_NAMESPACE
