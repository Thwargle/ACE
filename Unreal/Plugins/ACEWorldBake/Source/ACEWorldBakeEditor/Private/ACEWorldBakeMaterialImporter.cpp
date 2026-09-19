#include "ACEWorldBakeMaterialImporter.h"
#include "ACEWorldBakeEditor.h"
#include "ACEWorldBakeDatImageTexture.h"
#include "ACEWorldBakeDatRegion.h"
#include "ACEWorldBakeDatSurface.h"
#include "ACEWorldBakePortalFile.h"
#include "ACEWorldBakeMaterialImporter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "BlueprintMaterialTextureNodesBPLibrary.h"
#include "ComponentReregisterContext.h"
#include "Factories/MaterialImportHelpers.h"
#include "Factories/MaterialFactoryNew.h"
#include "Misc/App.h"
#include "Misc/ScopedSlowTask.h"
#include "PackageTools.h"

#define LOCTEXT_NAMESPACE "ACEWorldBakeMaterialImporter"

static bool ACEWorldBakeMaterialShouldShowSlowTaskDialog()
{
	return !FApp::IsUnattended();
}

static UMaterial* ACEWorldBakeCreateStubParentMaterial(const FString& PackagePath, const FString& AssetName)
{
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
	if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ObjectPath))
	{
		return Existing;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(PackagePath / AssetName, TEXT(""), UniquePackageName, UniqueAssetName);

	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(UniqueAssetName, FPackageName::GetLongPackagePath(UniquePackageName), UMaterial::StaticClass(), Factory);
	return Cast<UMaterial>(NewAsset);
}

bool FACEWorldBakeMaterialImporter::EnsureStubParentMaterials()
{
	const bool bCreatedDiffuse = ACEWorldBakeCreateStubParentMaterial(TEXT("/Game/Materials"), TEXT("M_Diffuse")) != nullptr;
	const bool bCreatedDiffuseMasked = ACEWorldBakeCreateStubParentMaterial(TEXT("/Game/Materials"), TEXT("M_DiffuseMasked")) != nullptr;
	const bool bCreatedPaletted = ACEWorldBakeCreateStubParentMaterial(TEXT("/Game/Materials"), TEXT("M_Paletted")) != nullptr;
	const bool bCreatedPalettedMasked = ACEWorldBakeCreateStubParentMaterial(TEXT("/Game/Materials"), TEXT("M_PalettedMasked")) != nullptr;
	const bool bCreatedLandscape = ACEWorldBakeCreateStubParentMaterial(TEXT("/Game/Landblocks"), TEXT("LandscapeMaster")) != nullptr;
	return bCreatedDiffuse && bCreatedDiffuseMasked && bCreatedPaletted && bCreatedPalettedMasked && bCreatedLandscape;
}

// copy of UBlueprintMaterialTextureNodesBPLibrary::CreateMIC_EditorOnly
UMaterialInstanceConstant* FACEWorldBakeMaterialImporter::CreateMIC(UMaterialInterface* Material, FString InName, bool bSyncBrowser)
{
	TArray<UObject*> ObjectsToSync;

	if (Material != nullptr)
	{
		// Create an appropriate and unique name 
		FString Name;
		FString PackageName;
		IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		//Use asset name only if directories are specified, otherwise full path
		if (!InName.Contains(TEXT("/")))
		{
			FString AssetName = Material->GetOutermost()->GetName();
			const FString SanitizedBasePackageName = UPackageTools::SanitizePackageName(AssetName);
			const FString PackagePath = FPackageName::GetLongPackagePath(SanitizedBasePackageName) + TEXT("/");
			AssetTools.CreateUniqueAssetName(PackagePath, InName, PackageName, Name);
		}
		else
		{
			InName.RemoveFromStart(TEXT("/"));
			InName.RemoveFromStart(TEXT("Content/"));
			InName.StartsWith(TEXT("Game/")) == true ? InName.InsertAt(0, TEXT("/")) : InName.InsertAt(0, TEXT("/Game/"));
			AssetTools.CreateUniqueAssetName(InName, TEXT(""), PackageName, Name);
		}

		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = Material;

		UObject* NewAsset = AssetTools.CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), UMaterialInstanceConstant::StaticClass(), Factory);

		if (bSyncBrowser)
		{
			ObjectsToSync.Add(NewAsset);
			GEditor->SyncBrowserToObjects(ObjectsToSync);
		}

		UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(NewAsset);

		return MIC;
	}

	return nullptr;
}

bool FACEWorldBakeMaterialImporter::ImportSurfaceToMaterial(uint32 SurfaceId, UMaterial* DiffuseParentMat, UMaterial* DiffuseMaskedParentMat, FName DiffuseParamName, UMaterial* PalettedParentMat, UMaterial* PalettedMaskedParentMat, FName IndexedParamName, FName PaletteParamName)
{
	IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	if (nullptr != ACEWorldBakeDatToolsModule)
	{
		FACEWorldBakeDatFilePtr PortalDatFile = ACEWorldBakeDatToolsModule->GetDatFile(EACEWorldBakeDatFile::Portal);
		if (!PortalDatFile.IsValid())
		{
			return false;
		}

		FACEWorldBakeDatFilePtr HighresDatFile = ACEWorldBakeDatToolsModule->GetDatFile(EACEWorldBakeDatFile::Highres);
		if (!HighresDatFile.IsValid())
		{
			return false;
		}

		FACEWorldBakePortalFile PortalFile(PortalDatFile);
		FACEWorldBakePortalFile HighresFile(HighresDatFile);

		FACEWorldBakePortalSurface Surface;
		const bool SurfaceOk = PortalFile.GetResource(SurfaceId, Surface) || HighresFile.GetResource(SurfaceId, Surface);
		if (SurfaceOk)
		{
			UMaterial* ParentMaterial = DiffuseParentMat;

			uint32 PaletteId = 0;
			uint32 DiffuseId = 0;
			bool bUseMasked = false;
			{
				FACEWorldBakePortalImageTexture PortalTexture;
				const bool PortalTextureOk = PortalFile.GetResource(Surface.TextureID, PortalTexture) || HighresFile.GetResource(Surface.TextureID, PortalTexture);
				if (PortalTextureOk && 0 < PortalTexture.ImageColorIDs.Num())
				{
					FACEWorldBakePortalImage PortalImage;
					const bool PortalImageOk = PortalFile.GetResource(PortalTexture.ImageColorIDs[0], PortalImage) || HighresFile.GetResource(PortalTexture.ImageColorIDs[0], PortalImage);
					if (PortalImageOk)
					{
						PaletteId = PortalImage.IsIndexed() ? PortalImage.PaletteId : 0; // is sometimes 0x06......?
						bUseMasked = PortalImage.HasAlphaChannel();
						DiffuseId = PortalTexture.ImageColorIDs[0];
					}
				}
			}

			if (bUseMasked)
			{
				ParentMaterial = (0 != PaletteId) ? PalettedMaskedParentMat  : DiffuseMaskedParentMat;
			}
			else
			{
				ParentMaterial = (0 != PaletteId) ? PalettedParentMat : DiffuseParentMat;
			}

			if (ParentMaterial)
			{
				FString MaterialPackage = FString::Printf(TEXT("/Game/Surfaces/0x%08X"), SurfaceId);

				UMaterialInstanceConstant* MaterialInstance = CreateMIC(ParentMaterial, MaterialPackage);
				if (MaterialInstance)
				{
					if (DiffuseParamName != NAME_None && 0 == PaletteId && 0 != DiffuseId)
					{
						FSoftObjectPath DiffuseAssetPath(FString::Printf(TEXT("/Game/Images/0x%08X"), DiffuseId));
						UTexture* DiffuseTexture = Cast<UTexture>(DiffuseAssetPath.TryLoad());
						if (DiffuseTexture)
						{
							MaterialInstance->SetTextureParameterValueEditorOnly(DiffuseParamName, DiffuseTexture);
						}
					}

					if (IndexedParamName != NAME_None && 0 != PaletteId && 0 != DiffuseId)
					{
						FSoftObjectPath IndexedAssetPath(FString::Printf(TEXT("/Game/Images/0x%08X"), DiffuseId));
						UTexture* IndexedTexture = Cast<UTexture>(IndexedAssetPath.TryLoad());
						if (IndexedTexture)
						{
							MaterialInstance->SetTextureParameterValueEditorOnly(IndexedParamName, IndexedTexture);
						}
					}

					if (PaletteParamName != NAME_None && 0 != PaletteId)
					{
						FSoftObjectPath PaletteAssetPath(FString::Printf(TEXT("/Game/Palettes/0x%08X"), PaletteId));
						UTexture* PaletteTexture = Cast<UTexture>(PaletteAssetPath.TryLoad());
						if (PaletteTexture)
						{
							MaterialInstance->SetTextureParameterValueEditorOnly(PaletteParamName, PaletteTexture);
						}
					}

					// todo: material primitives

					MaterialInstance->InitStaticPermutation();
					MaterialInstance->PostEditChange();
					MaterialInstance->MarkPackageDirty();
				}
			}

			// make sure that any static meshes, etc using this material will stop using the FMaterialResource of the original 
			// material, and will use the new FMaterialResource created when we make a new UMaterial in place
			FGlobalComponentReregisterContext RecreateComponents;

			return true;
		}
	}

	return false;
}

bool FACEWorldBakeMaterialImporter::ImportLandscapeMaterials(UMaterialInterface* LandscapeParentMat)
{
	IACEWorldBakeDatToolsModule* ACEWorldBakeDatToolsModule = FModuleManager::LoadModulePtr<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	if (nullptr != ACEWorldBakeDatToolsModule)
	{
		FACEWorldBakeDatFilePtr PortalDatFile = ACEWorldBakeDatToolsModule->GetDatFile(EACEWorldBakeDatFile::Portal);
		if (!PortalDatFile.IsValid())
		{
			return false;
		}

		FACEWorldBakeDatFilePtr HighresDatFile = ACEWorldBakeDatToolsModule->GetDatFile(EACEWorldBakeDatFile::Highres);
		if (!HighresDatFile.IsValid())
		{
			return false;
		}

		FACEWorldBakePortalFile PortalFile(PortalDatFile);

		FACEWorldBakePortalRegion PortalRegion;
		if (!PortalFile.GetResource(EACEWorldBakeResource_Native::kRegion, PortalRegion))
		{
			return false;
		}

		const FString FileDir = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("/Landblocks/"));
		const FString FilePath = FString::Printf(TEXT("%s/Tile.uniquegcfs"), *FileDir);

		TArray<FString> FileContents;
		if (LandscapeParentMat && FFileHelper::LoadFileToStringArray(FileContents, *FilePath))
		{
			const int32 WorkItems_ImportLandscapeMaterials = FileContents.Num();
			FScopedSlowTask SlowTask_ImportLandscapeMaterials(static_cast<float>(WorkItems_ImportLandscapeMaterials), LOCTEXT("ImportLandscapeMaterials", "Import Landscape Materials"));
			if (ACEWorldBakeMaterialShouldShowSlowTaskDialog())
			{
				SlowTask_ImportLandscapeMaterials.MakeDialog();
			}

			for (const FString& FileLine : FileContents)
			{
				SlowTask_ImportLandscapeMaterials.EnterProgressFrame();

				FString GCFHashString, TerrainArrayString;
				if (FileLine.Split(TEXT(":"), &GCFHashString, &TerrainArrayString))
				{
					GCFHashString.TrimStartAndEndInline();
					TerrainArrayString.TrimStartAndEndInline();

					const uint32 GCFHash = static_cast<uint32>(FCString::Atoi(*GCFHashString));

					TArray<uint32> TerrainArray;
					{
						TArray<FString> TerrainStringArray;
						TerrainArrayString.ParseIntoArray(TerrainStringArray, TEXT(","));
						for (const FString& TerrainString : TerrainStringArray)
						{
							TerrainArray.Add(static_cast<uint32>(FCString::Atoi(*TerrainString)));
						}
					}

					FString MaterialPackage = FString::Printf(TEXT("/Game/Landblocks/LandscapeGCF_%d"), GCFHash);

					UMaterialInstanceConstant* MaterialInstance = CreateMIC(LandscapeParentMat, MaterialPackage);
					if (MaterialInstance)
					{
						FStaticParameterSet StaticParameters;
						MaterialInstance->GetStaticParameterValues(StaticParameters);

						for (int32 TerrainArrayIndex = 0; TerrainArrayIndex < TerrainArray.Num(); ++TerrainArrayIndex)
						{
							const uint32 Terrain = TerrainArray[TerrainArrayIndex];

							if (!PortalRegion.TerrainTextures.IsValidIndex(Terrain))
								continue;

							const FACEWorldBakePortalRegionTerrainTexture& TerrainTexture = PortalRegion.TerrainTextures[Terrain];

							FACEWorldBakePortalImageTexture DiffusePortalTexture;
							const bool DiffuseTextureOk = PortalFile.GetResource(TerrainTexture.TerrainTexResourceId, DiffusePortalTexture);
							if (DiffuseTextureOk && DiffusePortalTexture.ImageColorIDs.Num())
							{
								FSoftObjectPath DiffuseAssetPath(FString::Printf(TEXT("/Game/Images/0x%08X"), DiffusePortalTexture.ImageColorIDs[0]));
								UTexture* DiffuseTexture = Cast<UTexture>(DiffuseAssetPath.TryLoad());
								if (DiffuseTexture)
								{
									const FName DiffuseParamName = *FString::Printf(TEXT("Diffuse%d"), TerrainArrayIndex + 1);
									MaterialInstance->SetTextureParameterValueEditorOnly(DiffuseParamName, DiffuseTexture);
								}

								const FName DiffuseEnabledParamName = *FString::Printf(TEXT("Diffuse%d_Enabled"), TerrainArrayIndex + 1);
								{
									for (FStaticSwitchParameter& SwitchParameter : StaticParameters.StaticSwitchParameters)
									{
										if (SwitchParameter.ParameterInfo.Name == DiffuseEnabledParamName)
										{
											SwitchParameter.bOverride = true;
											SwitchParameter.Value = true;
											break;
										}
									}
								}
							}

							FACEWorldBakePortalImageTexture DetailPortalTexture;
							const bool DetailTextureOk = PortalFile.GetResource(TerrainTexture.DetailTexId, DetailPortalTexture);
							if (DetailTextureOk && DetailPortalTexture.ImageColorIDs.Num())
							{
								const FName DetailParamName = *FString::Printf(TEXT("Diffuse%d_Detail_0x%08X"), TerrainArrayIndex + 1, DetailPortalTexture.ImageColorIDs[0]);
								{
									for (FStaticSwitchParameter& SwitchParameter : StaticParameters.StaticSwitchParameters)
									{
										if (SwitchParameter.ParameterInfo.Name == DetailParamName)
										{
											SwitchParameter.bOverride = true;
											SwitchParameter.Value = true;
											break;
										}
									}
								}
							}

							//todo constant scalar for TerrainTexture.detailTexTiling;
						}

						MaterialInstance->UpdateStaticPermutation(StaticParameters);
						MaterialInstance->InitStaticPermutation();
						MaterialInstance->PostEditChange();
						MaterialInstance->MarkPackageDirty();
					}
				}
			}

			return true;
		}
	}

	return false;
}

bool FACEWorldBakeMaterialImporter::ImportLandscapeMaterialInstances(const FString& LandscapeParentMatName, int32 TileX, int32 TileY)
{
	FString LandscapeParentPackage = FString::Printf(TEXT("/Game/Landblocks/%s"), *LandscapeParentMatName);

	FText FindMaterialError;
	UMaterialInterface* LandscapeParentMat = UMaterialImportHelpers::FindExistingMaterialFromSearchLocation(LandscapeParentMatName, TEXT("/Game/"), EMaterialSearchLocation::AllAssets, FindMaterialError);
	if (LandscapeParentMat)
	{
		FString MaterialPackage = FString::Printf(TEXT("/Game/Landblocks/%s_Tile_x%d_y%d"), *LandscapeParentMatName, TileX, TileY);

		UMaterialInstanceConstant* MaterialInstance = CreateMIC(LandscapeParentMat, MaterialPackage);
		if (MaterialInstance)
		{
			FSoftObjectPath Mask0AssetPath(FString::Printf(TEXT("/Game/Landblocks/Tile_x%d_y%d_Mask0"), TileX, TileY));
			UTexture* Mask0Texture = Cast<UTexture>(Mask0AssetPath.TryLoad());
			if (Mask0Texture)
			{
				MaterialInstance->SetTextureParameterValueEditorOnly(TEXT("Mask0"), Mask0Texture);
			}

			FSoftObjectPath Mask1AssetPath(FString::Printf(TEXT("/Game/Landblocks/Tile_x%d_y%d_Mask1"), TileX, TileY));
			UTexture* Mask1Texture = Cast<UTexture>(Mask1AssetPath.TryLoad());
			if (Mask1Texture)
			{
				MaterialInstance->SetTextureParameterValueEditorOnly(TEXT("Mask1"), Mask1Texture);
			}

			FSoftObjectPath Mask2AssetPath(FString::Printf(TEXT("/Game/Landblocks/Tile_x%d_y%d_Mask2"), TileX, TileY));
			UTexture* Mask2Texture = Cast<UTexture>(Mask2AssetPath.TryLoad());
			if (Mask2Texture)
			{
				MaterialInstance->SetTextureParameterValueEditorOnly(TEXT("Mask2"), Mask2Texture);
			}

			//MaterialInstance->PreEditChange(NULL);
			MaterialInstance->PostEditChange();
			MaterialInstance->MarkPackageDirty();
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
