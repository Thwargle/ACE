#pragma once

#include "CoreMinimal.h"

/** Saves dirty content packages; works in unattended commandlet (unlike FEditorFileUtils::SaveDirtyPackages). */
void ACEWorldBakeSaveDirtyContentPackages();

/** Saves dirty map and content packages; works in unattended commandlet. */
void ACEWorldBakeSaveAllDirtyPackages();

/** Imports T3D actor text into the world's current level without clipboard or EDIT PASTE. */
bool ACEWorldBakeImportT3DText(UWorld* World, const FString& T3DText, TArray<AActor*>& OutActors);

/** Creates an empty map file on disk when the package does not already exist. */
bool ACEWorldBakeEnsureLevelFileExists(const FString& MapPackageName);
