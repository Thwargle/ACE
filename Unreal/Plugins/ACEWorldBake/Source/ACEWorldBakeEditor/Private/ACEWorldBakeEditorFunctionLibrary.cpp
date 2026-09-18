#include "ACEWorldBakeEditorFunctionLibrary.h"
#include "ACEWorldBakeEditor.h"
#include "ACEWorldBakeEditorInternal.h"
#include "ACEWorldBakeWorldBuilder.h"
#include "ACEWorldBakeDatFile.h"
#include "ACEWorldBakeEdEngine.h"
#include "ACEWorldBakeMaterialImporter.h"
#include "AssetToolsModule.h"
#include "BodySetupEnums.h"
#include "Developer/DesktopPlatform/Public/IDesktopPlatform.h"
#include "Developer/DesktopPlatform/Public/DesktopPlatformModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InterchangeManager.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeInfoMap.h"
#include "LandscapeStreamingProxy.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/App.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/FbxFactory.h"
#include "PhysicsEngine/BodySetup.h"
#include "UnrealEd.h"

#define LOCTEXT_NAMESPACE "ACEWorldBakeEditorFunctionLibrary"

static bool ACEWorldBakeShouldShowSlowTaskDialog()
{
	return !FApp::IsUnattended();
}

static bool ACEWorldBakeHasSurfaceMaterialsInContent()
{
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game/Surfaces"));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
	TArray<FAssetData> Assets;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	return Assets.Num() > 0;
}

static UFbxFactory* ACEWorldBakeCreateFbxFactory(bool bLinkSurfaceMaterials)
{
	UFbxFactory* FbxFactory = NewObject<UFbxFactory>(GetTransientPackage(), UFbxFactory::StaticClass(), NAME_None, RF_NoFlags);
	FbxFactory->ResetState();
	FbxFactory->SetDetectImportTypeOnImport(false);
	FbxFactory->ImportUI->bImportMaterials = bLinkSurfaceMaterials;
	FbxFactory->ImportUI->bImportTextures = bLinkSurfaceMaterials;
	FbxFactory->ImportUI->bImportAsSkeletal = false;
	FbxFactory->ImportUI->bImportMesh = true;
	FbxFactory->ImportUI->StaticMeshImportData->bAutoGenerateCollision = false;
	FbxFactory->ImportUI->StaticMeshImportData->bOneConvexHullPerUCX = false;
	FbxFactory->ImportUI->StaticMeshImportData->bCombineMeshes = true;
	if (bLinkSurfaceMaterials)
	{
		FbxFactory->ImportUI->TextureImportData->MaterialSearchLocation = EMaterialSearchLocation::AllAssets;
	}
	FbxFactory->ImportUI->SetMeshTypeToImport();
	return FbxFactory;
}

static void ACEWorldBakePostProcessImportedModel(UStaticMesh* ImportedModel)
{
	if (!ImportedModel)
	{
		return;
	}

	if (ImportedModel->GetBodySetup() && ImportedModel->GetName().Contains(TEXT("0x0D"), ESearchCase::IgnoreCase))
	{
		ImportedModel->GetBodySetup()->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	}

	ImportedModel->MarkPackageDirty();
}

static void SaveContent()
{
	ACEWorldBakeSaveDirtyContentPackages();
}

int32 UACEWorldBakeEditorFunctionLibrary::HexStringToInt32(const FString& HexString)
{
	return FCString::Strtoi(*HexString, nullptr, 16);
}

bool UACEWorldBakeEditorFunctionLibrary::OpenDirectoryDialog(const FString& DialogTitle, const FString& DefaultPath, FString& OutFolderName)
{
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		return DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, DialogTitle, DefaultPath, OutFolderName);
	}

	return false;
}

void UACEWorldBakeEditorFunctionLibrary::GetDatFileDirectory(FString& OutPath, bool& OutHasCell, bool& OutHasHighRes, bool& OutHasLocale, bool& OutHasPortal)
{
	if (IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools"))
	{
		ACEWorldBakeDatToolsModule->GetDatFileDirectory(OutPath, OutHasCell, OutHasHighRes, OutHasLocale, OutHasPortal);
	}
}

void UACEWorldBakeEditorFunctionLibrary::SetDatFileDirectory(const FString& Path)
{
	if (IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools"))
	{
		ACEWorldBakeDatToolsModule->SetDatFileDirectory(Path);
	}
}

void UACEWorldBakeEditorFunctionLibrary::ExportLandblocks()
{
	if (IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools"))
	{
		ACEWorldBakeDatToolsModule->ExportLandblocks();
	}
}

void UACEWorldBakeEditorFunctionLibrary::ExportPortalResource(EACEWorldBakeResource::Type ResourceType, int32 Identifier)
{
	if (IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools"))
	{
		ACEWorldBakeDatToolsModule->ExportResource(ResourceType, Identifier);
	}
}

void UACEWorldBakeEditorFunctionLibrary::ExportPortalResources(EACEWorldBakeResource::Type ResourceType, int32 Mask)
{
	if (IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools"))
	{
		ACEWorldBakeDatToolsModule->ExportResources(ResourceType, Mask);
	}
}

void UACEWorldBakeEditorFunctionLibrary::DumpDatFileStatsToLog(EACEWorldBakeDatFile::Type DatFileType)
{
	if (IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools"))
	{
		ACEWorldBakeDatToolsModule->DumpDatFileStatsToLog(DatFileType, true);
	}
}

static void ACEWorldBakeWaitForInterchangeIdle()
{
	int32 MaxWaitIterations = 100000;
	while (UInterchangeManager::GetInterchangeManager().IsInterchangeActive() && MaxWaitIterations-- > 0)
	{
		FPlatformProcess::Sleep(0.01f);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}
	}
}

void UACEWorldBakeEditorFunctionLibrary::ImportTextures(const FString& SourceFolder, const FString& TargetSubPath)
{
	if (!FPaths::DirectoryExists(SourceFolder))
	{
		return;
	}

	TArray<FString> TextureFiles;
	{
		IFileManager& FileManager = IFileManager::Get();
		FileManager.FindFiles(TextureFiles, *(SourceFolder / "*.dxt"), true, false);
		FileManager.FindFiles(TextureFiles, *(SourceFolder / "*.png"), true, false);

		const FString FilenamePrefix = (SourceFolder.EndsWith(TEXT("/")) || SourceFolder.EndsWith(TEXT("\\"))) ? SourceFolder : SourceFolder + TEXT("/");
		for (FString& FileName : TextureFiles)
		{
			FileName.InsertAt(0, *FilenamePrefix);
		}
	}

	if (TextureFiles.Num() == 0)
	{
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	IACEWorldBakeDatToolsModule* DatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");

	const bool bImportSynchronously = FApp::IsUnattended();
	const int32 BatchSize = bImportSynchronously ? 16 : TextureFiles.Num();

	FScopedSlowTask SlowTask(static_cast<float>(TextureFiles.Num()), LOCTEXT("ImportTextures", "Importing Textures"));
	if (ACEWorldBakeShouldShowSlowTaskDialog())
	{
		SlowTask.MakeDialog();
	}

	TArray<UObject*> AllImportedAssets;

	for (int32 BatchStart = 0; BatchStart < TextureFiles.Num(); BatchStart += BatchSize)
	{
		const int32 BatchEnd = FMath::Min(BatchStart + BatchSize, TextureFiles.Num());
		TArray<FString> BatchFiles;
		for (int32 Index = BatchStart; Index < BatchEnd; ++Index)
		{
			BatchFiles.Add(TextureFiles[Index]);
		}

		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->GroupName = TargetSubPath;
		ImportData->Filenames = BatchFiles;
		ImportData->DestinationPath = TargetSubPath;
		ImportData->bReplaceExisting = true;
		ImportData->bSkipReadOnly = false;

		const TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssetsAutomated(ImportData);

		if (bImportSynchronously)
		{
			ACEWorldBakeWaitForInterchangeIdle();
		}

		AllImportedAssets.Append(ImportedAssets);

		for (int32 Index = BatchStart; Index < BatchEnd; ++Index)
		{
			SlowTask.EnterProgressFrame();
		}
	}

	if (!bImportSynchronously)
	{
		ACEWorldBakeWaitForInterchangeIdle();
	}

	if (AllImportedAssets.Num() > 0)
	{
		if (DatToolsModule)
		{
			FScopedSlowTask SlowTask_ImportCorrectTextureSettings(static_cast<float>(AllImportedAssets.Num()), LOCTEXT("ImportCorrectTextureSettings", "Correcting Texture Settings"));
			if (ACEWorldBakeShouldShowSlowTaskDialog())
			{
				SlowTask_ImportCorrectTextureSettings.MakeDialog();
			}

			for (UObject* ImportedAsset : AllImportedAssets)
			{
				SlowTask_ImportCorrectTextureSettings.EnterProgressFrame();

				if (UTexture2D* ImportedTexture = Cast<UTexture2D>(ImportedAsset))
				{
					DatToolsModule->CorrectPortalImageTextureSettings(ImportedTexture);
					ImportedAsset->MarkPackageDirty();
				}
			}
		}

		SaveContent();
	}
}

void UACEWorldBakeEditorFunctionLibrary::ImportModels(const FString& SourceFolder, const FString& TargetSubPath)
{
	if (!FPaths::DirectoryExists(SourceFolder))
	{
		UE_LOG_ACEWORLDBAKEED(Warning, TEXT("ImportModels: folder not found: %s"), *SourceFolder);
		return;
	}

	TArray<FString> ModelFiles;
	{
		IFileManager& FileManager = IFileManager::Get();
		FileManager.FindFiles(ModelFiles, *(SourceFolder / "*.fbx"), true, false);

		const FString FilenamePrefix = (SourceFolder.EndsWith(TEXT("/")) || SourceFolder.EndsWith(TEXT("\\"))) ? SourceFolder : SourceFolder + TEXT("/");
		for (FString& FileName : ModelFiles)
		{
			FileName.InsertAt(0, *FilenamePrefix);
		}
	}

	if (ModelFiles.Num() == 0)
	{
		UE_LOG_ACEWORLDBAKEED(Warning, TEXT("ImportModels: no .fbx files in %s"), *SourceFolder);
		return;
	}

	const bool bLinkSurfaceMaterials = ACEWorldBakeHasSurfaceMaterialsInContent();
	if (!bLinkSurfaceMaterials)
	{
		UE_LOG_ACEWORLDBAKEED(Warning, TEXT("ImportModels: no /Game/Surfaces materials yet — importing mesh/collision only (run ImportSurfaces first for material links)"));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
	const bool bImportOneByOne = FApp::IsUnattended();

	FScopedSlowTask SlowTask(static_cast<float>(ModelFiles.Num()), LOCTEXT("ImportModels", "Importing Models"));
	if (ACEWorldBakeShouldShowSlowTaskDialog())
	{
		SlowTask.MakeDialog();
	}

	int32 SuccessCount = 0;
	int32 FailCount = 0;

	if (bImportOneByOne)
	{
		UFbxFactory* FbxFactory = ACEWorldBakeCreateFbxFactory(bLinkSurfaceMaterials);

		for (const FString& ModelFile : ModelFiles)
		{
			SlowTask.EnterProgressFrame();

			UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
			ImportData->GroupName = TargetSubPath;
			ImportData->Filenames = { ModelFile };
			ImportData->DestinationPath = TargetSubPath;
			ImportData->FactoryName = FbxFactory->GetName();
			ImportData->bReplaceExisting = true;
			ImportData->bSkipReadOnly = false;
			ImportData->Factory = FbxFactory;

			const TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssetsAutomated(ImportData);
			if (ImportedAssets.Num() == 0)
			{
				UE_LOG_ACEWORLDBAKEED(Warning, TEXT("FBX import failed: %s"), *ModelFile);
				FailCount++;
				continue;
			}

			for (UObject* ImportedAsset : ImportedAssets)
			{
				ACEWorldBakePostProcessImportedModel(Cast<UStaticMesh>(ImportedAsset));
			}

			SuccessCount++;
		}
	}
	else
	{
		UFbxFactory* FbxFactory = ACEWorldBakeCreateFbxFactory(bLinkSurfaceMaterials);

		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->GroupName = TargetSubPath;
		ImportData->Filenames = ModelFiles;
		ImportData->DestinationPath = TargetSubPath;
		ImportData->FactoryName = FbxFactory->GetName();
		ImportData->bReplaceExisting = true;
		ImportData->bSkipReadOnly = false;
		ImportData->Factory = FbxFactory;

		const TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssetsAutomated(ImportData);
		for (UObject* ImportedAsset : ImportedAssets)
		{
			SlowTask.EnterProgressFrame();
			ACEWorldBakePostProcessImportedModel(Cast<UStaticMesh>(ImportedAsset));
		}

		SuccessCount = ImportedAssets.Num();
		FailCount = ModelFiles.Num() - SuccessCount;
	}

	UE_LOG_ACEWORLDBAKEED(Log, TEXT("ImportModels finished: %d ok, %d failed (of %d files)"), SuccessCount, FailCount, ModelFiles.Num());

	if (SuccessCount > 0)
	{
		ACEWorldBakeWaitForInterchangeIdle();
		SaveContent();
	}
}

bool UACEWorldBakeEditorFunctionLibrary::CorrectPortalImageTextureSettings(UTexture2D* Texture, int32 Identifier)
{
	if (IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools"))
	{
		return ACEWorldBakeDatToolsModule->CorrectPortalImageTextureSettings(Texture, Identifier);
	}

	return false;
}

void UACEWorldBakeEditorFunctionLibrary::ImportScene(int32 Identifier)
{
	const FString FileName = FString::Printf(TEXT("0x%08X"), Identifier);
	const FString FileDir = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("Scenes"));
	const FString FilePath = FString::Printf(TEXT("%s/%s.t3d"), *FileDir, *FileName);

	ImportT3D(FilePath, TEXT(""), false); // todo: empty level name here is wrong
}

void UACEWorldBakeEditorFunctionLibrary::ImportSetup(int32 Identifier)
{
	if (0 == (0xFF000000 & Identifier))
	{
		Identifier |= EACEWorldBakeResource_Native::kSetup;
	}

	const FString FileName = FString::Printf(TEXT("0x%08X"), Identifier);
	const FString FileDir = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("Setups"));
	const FString FilePath = FString::Printf(TEXT("%s/%s.t3d"), *FileDir, *FileName);
	const FString AssetPath = FString::Printf(TEXT("/Game/Setups/%s"), *FileName);

	ImportT3DToBlueprint(FilePath, FName(AssetPath));
}

void UACEWorldBakeEditorFunctionLibrary::ImportSetups(int32 Mask)
{
	const int32 FirstIdentifier = EACEWorldBakeResource_Native::kSetup;
	const int32 LastIdentifer = FirstIdentifier | (0x0000FFFF & Mask);

	//const int32 WorkItems_ImportSurfaces = LastIdentifer - FirstIdentifier;
	//FScopedSlowTask SlowTask_ImportSurfaces(static_cast<float>(WorkItems_ImportSurfaces), LOCTEXT("ImportSurfaces", "Import Surface Materials"));
	//SlowTask_ImportSurfaces.MakeDialog();

	for (int32 Identifier = FirstIdentifier; Identifier < LastIdentifer; ++Identifier)
	{
		ImportSetup(Identifier);

		if (UACEWorldBakeEdEngine* BakeEd = Cast<UACEWorldBakeEdEngine>(GEditor))
		{
			if (BakeEd->HasPendingBlueprintImports() && ((Identifier - FirstIdentifier) & 63) == 63)
			{
				BakeEd->PumpBlueprintImportQueueUntilIdle(0.0);
			}
		}
	}
}

bool UACEWorldBakeEditorFunctionLibrary::ImportSurface(int32 SurfaceId, UMaterial* DiffuseParentMat, UMaterial* DiffuseMaskedParentMat, FName DiffuseParamName, UMaterial* PalettedParentMat, UMaterial* PalettedMaskedParentMat, FName IndexedParamName, FName PaletteParamName)
{
	return FACEWorldBakeMaterialImporter::ImportSurfaceToMaterial(SurfaceId, DiffuseParentMat, DiffuseMaskedParentMat, DiffuseParamName, PalettedParentMat, PalettedMaskedParentMat, IndexedParamName, PaletteParamName);
}

void UACEWorldBakeEditorFunctionLibrary::ImportSurfaces(int32 Mask, UMaterial* DiffuseParentMat, UMaterial* DiffuseMaskedParentMat, FName DiffuseParamName, UMaterial* PalettedParentMat, UMaterial* PalettedMaskedParentMat, FName IndexedParamName, FName PaletteParamName)
{
	const int32 FirstIdentifier = EACEWorldBakeResource_Native::kSurface;
	const int32 LastIdentifer = FirstIdentifier | (0x0000FFFF & Mask);

	const int32 WorkItems_ImportSurfaces = LastIdentifer - FirstIdentifier;
	FScopedSlowTask SlowTask_ImportSurfaces(static_cast<float>(WorkItems_ImportSurfaces), LOCTEXT("ImportSurfaces", "Import Surface Materials"));
	SlowTask_ImportSurfaces.MakeDialog();

	for (int32 Identifier = FirstIdentifier; Identifier < LastIdentifer; ++Identifier)
	{
		SlowTask_ImportSurfaces.EnterProgressFrame();

		ImportSurface(Identifier, DiffuseParentMat, DiffuseMaskedParentMat, DiffuseParamName, PalettedParentMat, PalettedMaskedParentMat, IndexedParamName, PaletteParamName);
	}
}

bool UACEWorldBakeEditorFunctionLibrary::ImportLandscapeMaterials(UMaterialInterface* LandscapeParentMat)
{
	return FACEWorldBakeMaterialImporter::ImportLandscapeMaterials(LandscapeParentMat);
}

bool UACEWorldBakeEditorFunctionLibrary::ImportLandscapeMaterialInstances(UMaterialInterface* LandscapeParentMat)
{
	for (int32 TileY = 0; TileY < 8; ++TileY)
	{
		for (int32 TileX = 0; TileX < 8; ++TileX)
		{
			TArray<int32> TileGCFs = GetLandscapeTileGCFs(TileX, TileY);
			TSet<int32> TileUniqueGCFs;
			TileUniqueGCFs.Append(TileGCFs);

			const int32 WorkItems_ImportLandscapeMaterialInstances = TileUniqueGCFs.Num();
			FText ImportLandscapeMaterialInstancesText = FText::Format(LOCTEXT("ExportResourcesType", "Import Landscape Material Instances {0}, {1}"), TileX, TileY);
			FScopedSlowTask SlowTask_ImportLandscapeMaterialInstances(static_cast<float>(WorkItems_ImportLandscapeMaterialInstances), ImportLandscapeMaterialInstancesText);
			SlowTask_ImportLandscapeMaterialInstances.MakeDialog();

			for (int32 UniqueGCF : TileUniqueGCFs.Array())
			{
				SlowTask_ImportLandscapeMaterialInstances.EnterProgressFrame();

				FACEWorldBakeMaterialImporter::ImportLandscapeMaterialInstances(FString::Printf(TEXT("LandscapeGCF_%d"), UniqueGCF), TileX, TileY);
			}
		}
	}

	return true;
}

void UACEWorldBakeEditorFunctionLibrary::ExportLandblockInfo(int32 LandblockX, int32 LandblockY)
{
	if (IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools"))
	{
		ACEWorldBakeDatToolsModule->ExportLandblockInfo(LandblockX, LandblockY);

		static volatile bool bImport = true;
		if (bImport)
		{
			ImportLandblockInfo(LandblockX, LandblockY, false);
		}
	}
}

void UACEWorldBakeEditorFunctionLibrary::ImportT3D(const FString& FilePath, const FString& LevelName, bool FocusImportedActor)
{
	FString FileContent;
	if (FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		UACEWorldBakeEdEngine* ACEWorldBakeEd = Cast<UACEWorldBakeEdEngine>(GEditor);
		if (ACEWorldBakeEd)
		{
			ACEWorldBakeEd->ImportT3D(FilePath, FileContent, FocusImportedActor, LevelName);
		}
		else // fallback to immediate paste-import
		{
			FPlatformApplicationMisc::ClipboardCopy(*FileContent);
			if (GEditor && GEditor->GetEditorWorldContext().World())
			{
				GEditor->Exec(GEditor->GetEditorWorldContext().World(), TEXT("EDIT PASTE"));
			}
		}
	}
}

void UACEWorldBakeEditorFunctionLibrary::ImportT3DToBlueprint(const FString& FilePath, const FName AssetName)
{
	FString FileContent;
	if (FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		UACEWorldBakeEdEngine* ACEWorldBakeEd = Cast<UACEWorldBakeEdEngine>(GEditor);
		if (ACEWorldBakeEd)
		{
			ACEWorldBakeEd->ImportT3DToBlueprint(FilePath, FileContent, AssetName);
		}
		else // fallback to immediate paste-import
		{
			FPlatformApplicationMisc::ClipboardCopy(*FileContent);
			if (GEditor && GEditor->GetEditorWorldContext().World())
			{
				GEditor->Exec(GEditor->GetEditorWorldContext().World(), TEXT("EDIT PASTE"));

				// todo: select current actor and call FKismetEditorUtilities::CreateBlueprint
			}
		}
	}
}

void UACEWorldBakeEditorFunctionLibrary::ImportLandblockInfo(int32 LandblockX, int32 LandblockY, bool FocusImportedActor)
{
	const int32 UnrealTileX = LandblockX / 32;
	const int32 UnrealTileY = 7 - (LandblockY / 32);
	const FString LevelName = FString::Printf(TEXT("Tile_x%d_y%d_Landblocks"), UnrealTileX, UnrealTileY);

	const FString FileName = FString::Printf(TEXT("%03d_%03d_%02X%02XFFFE"), LandblockX, LandblockY, LandblockX, LandblockY);
	const FString FileDir = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("LandblockInfos"));
	const FString FilePath = FString::Printf(TEXT("%s/%s.t3d"), *FileDir, *FileName);

	if (!FPaths::FileExists(FilePath))
	{
		return;
	}

	ImportT3D(FilePath, LevelName, FocusImportedActor);
}

void UACEWorldBakeEditorFunctionLibrary::ImportLandblockScene(int32 LandblockX, int32 LandblockY, bool FocusImportedActor)
{
	const int32 UnrealTileX = LandblockX / 32;
	const int32 UnrealTileY = 7 - (LandblockY / 32);
	const FString LevelName = FString::Printf(TEXT("Tile_x%d_y%d_%03d_%03d_Scenes"), UnrealTileX, UnrealTileY, LandblockX, LandblockY);

	const FString FileName = FString::Printf(TEXT("%03d_%03d_%03d_%03d_Scenes"), LandblockX, LandblockY, LandblockX + kSceneLandblockWidth, LandblockY + kSceneLandblockWidth);
	const FString FileDir = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("LandblockInfos"));
	const FString FilePath = FString::Printf(TEXT("%s/%s.t3d"), *FileDir, *FileName);

	if (!FPaths::FileExists(FilePath))
	{
		return;
	}

	ImportT3D(FilePath, LevelName, FocusImportedActor);
}

void UACEWorldBakeEditorFunctionLibrary::MarkLandscapeComponentDirty(ULandscapeComponent* LandscapeComponent)
{
	if (LandscapeComponent)
	{
		LandscapeComponent->MarkPackageDirty();
	}
}

void UACEWorldBakeEditorFunctionLibrary::UpdateLandscapeStreamingLayerInfoMap(ALandscapeStreamingProxy* LandscapeProxy)
{
	if (LandscapeProxy)
	{
		ULandscapeInfo* LandscapeInfo = nullptr;

		check(GIsEditor);
		UWorld* OwningWorld = LandscapeProxy->GetWorld();

		if (OwningWorld != nullptr && !OwningWorld->IsGameWorld())
		{
			auto& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(OwningWorld);
			LandscapeInfo = LandscapeInfoMap.Map.FindRef(LandscapeProxy->GetLandscapeGuid());
		}

		if (LandscapeInfo)
		{
			LandscapeInfo->UpdateLayerInfoMap(LandscapeProxy, true);
		}
	}
}

void UACEWorldBakeEditorFunctionLibrary::StopImports()
{
	UACEWorldBakeEdEngine* ACEWorldBakeEd = Cast<UACEWorldBakeEdEngine>(GEditor);
	if (ACEWorldBakeEd)
	{
		ACEWorldBakeEd->ClearImportQueue();
	}
}

void UACEWorldBakeEditorFunctionLibrary::StopImportFocus()
{
	UACEWorldBakeEdEngine* ACEWorldBakeEd = Cast<UACEWorldBakeEdEngine>(GEditor);
	if (ACEWorldBakeEd)
	{
		ACEWorldBakeEd->ClearFocusQueue();
	}
}

void UACEWorldBakeEditorFunctionLibrary::StartImportFocus()
{
	UACEWorldBakeEdEngine* ACEWorldBakeEd = Cast<UACEWorldBakeEdEngine>(GEditor);
	if (ACEWorldBakeEd)
	{
		ACEWorldBakeEd->StartFocusImportActors();
	}
}

TArray<int32> UACEWorldBakeEditorFunctionLibrary::GetLandscapeTileGCFs(int32 TileX, int32 TileY)
{
	TArray<int32> TileGCFs;

	const FString FileDir = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("/Landblocks/"));
	const FString FilePath = FString::Printf(TEXT("%s/Tile_x%d_y%d.gcfs"), *FileDir, TileX, TileY);

	FString FileContent;
	if (FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		TArray<FString> TerrainStringArray;
		FileContent.ParseIntoArray(TerrainStringArray, TEXT(","));
		for (const FString& TerrainString : TerrainStringArray)
		{
			TileGCFs.Add(static_cast<int32>(FCString::Atoi(*TerrainString)));
		}
	}

	return TileGCFs;
}

void UACEWorldBakeEditorFunctionLibrary::MoveViewportCamerasToActors(const TArray<AActor*>& Actors, bool bActiveViewportOnly)
{
	if (GEditor)
	{
		GEditor->MoveViewportCamerasToActor(Actors, bActiveViewportOnly);
	}
}

bool UACEWorldBakeEditorFunctionLibrary::BuildEntireWorld()
{
	FACEWorldBakeWorldBuildOptions Options;
	return FACEWorldBakeWorldBuilder::Run(Options);
}

void UACEWorldBakeEditorFunctionLibrary::QueueAllLandblockImports()
{
	FACEWorldBakeWorldBuilder::QueueAllLandblockImports();
}

bool UACEWorldBakeEditorFunctionLibrary::IsAsyncImportOrExportPending()
{
	return UInterchangeManager::GetInterchangeManager().IsInterchangeActive();
}

void UACEWorldBakeEditorFunctionLibrary::SetMinDrawDistance(UPrimitiveComponent* Component, float Distance)
{
	if (Component)
	{
		Component->MinDrawDistance = Distance;
	}
}

#undef LOCTEXT_NAMESPACE
