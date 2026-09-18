#include "ACEWorldBakeEditorInternal.h"

#include "ACEWorldBakeEditor.h"
#include "FileHelpers.h"
#include "Factories/LevelFactory.h"
#include "UnrealEd.h"

void ACEWorldBakeSaveDirtyContentPackages()
{
	const bool bSaved = UEditorLoadingAndSavingUtils::SaveDirtyPackages(false, true);
	if (!bSaved)
	{
		UE_LOG_ACEWORLDBAKEED(Warning, TEXT("SaveDirtyContentPackages failed or was cancelled"));
	}
}

void ACEWorldBakeSaveAllDirtyPackages()
{
	const bool bSaved = UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);
	if (!bSaved)
	{
		UE_LOG_ACEWORLDBAKEED(Warning, TEXT("SaveAllDirtyPackages failed or was cancelled"));
	}
}

bool ACEWorldBakeImportT3DText(UWorld* World, const FString& T3DText, TArray<AActor*>& OutActors)
{
	OutActors.Reset();

	if (!World || T3DText.IsEmpty())
	{
		return false;
	}

	ULevel* TargetLevel = World->GetCurrentLevel();
	if (!TargetLevel)
	{
		return false;
	}

	const TCHAR* PasteStart = *T3DText;
	const TCHAR* PasteEnd = PasteStart + T3DText.Len();

	TStrongObjectPtr<ULevelFactory> Factory(NewObject<ULevelFactory>());
	Factory->LevelFactoryCreateText(
		ULevel::StaticClass(),
		TargetLevel,
		TargetLevel->GetFName(),
		RF_Transactional,
		nullptr,
		TEXT("paste"),
		PasteStart,
		PasteEnd,
		GWarn,
		&OutActors);

	return OutActors.Num() > 0;
}

bool ACEWorldBakeEnsureLevelFileExists(const FString& MapPackageName)
{
	const FString MapFilePath = FPackageName::LongPackageNameToFilename(MapPackageName, FPackageName::GetMapPackageExtension());
	if (FPaths::FileExists(MapFilePath))
	{
		return true;
	}

	UWorld* TempWorld = UWorld::CreateWorld(EWorldType::None, false);
	if (!TempWorld)
	{
		return false;
	}

	const bool bSaved = FEditorFileUtils::SaveLevel(TempWorld->PersistentLevel, *MapFilePath);
	TempWorld->DestroyWorld(false);
	return bSaved;
}
