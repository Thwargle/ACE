#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Dat/ACEPCodeTileCache.h"
#include "Dat/ACELandTextureMipProvider.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "RenderingThread.h"
#include "TextureResource.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELandscapeTextureFidelityTest, "ACE.Packaging.LandscapeTextureFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FACELandscapeTextureFidelityTest::RunTest(const FString& Parameters)
{
	FACELandblockMeshBuilder Builder(nullptr, nullptr, nullptr);
	Builder.TexMerge.BaseTexSize = 1024;
	FACEDatDecodedSurface Surface;
	Surface.Width = Surface.Height = 512;
	Surface.bHasPixels = true;
	Surface.Pixels.SetNumUninitialized(512 * 512);
	for (int32 Y = 0; Y < 512; ++Y)
		for (int32 X = 0; X < 512; ++X)
			Surface.Pixels[Y * 512 + X] = FColor(X & 255, Y & 255, (X + Y) & 255, 255);
	Builder.TerrainSurfCache.Add(0, Surface);
	Builder.TerrainTilingCache.Add(0, 2);
	TArray<FColor> Pixels;
	int32 Width = 0, Height = 0;
	if (!TestTrue(TEXT("Retail-size terrain bake succeeds"), Builder.BakePCode(0, Pixels, Width, Height))) return false;
	TestEqual(TEXT("The authored 1024-wide blend is retained"), Width, 1024);
	TestEqual(TEXT("The authored 1024-high blend is retained"), Height, 1024);
	TestEqual(TEXT("First source texel is preserved with mirrored terrain UVs"), Pixels[1023], Surface.Pixels[0]);
	TestEqual(TEXT("Two authored repeats retain full source texel density"), Pixels[511], Surface.Pixels[0]);
	TestEqual(TEXT("Neighboring source texels are not averaged"), Pixels[1022], Surface.Pixels[1]);

	auto* Limit = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Texture.MaxWorldSize"));
	if (!TestNotNull(TEXT("Object texture limit is available"), Limit)) return false;
	const int32 SavedLimit = Limit->GetInt();
	ON_SCOPE_EXIT { Limit->Set(SavedLimit, ECVF_SetByCode); };
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UACEDatSubsystem> Owner(NewObject<UACEDatSubsystem>(Instance.Get()));
	for (int32 ObjectLimit : { 512, 0 })
	{
		Limit->Set(ObjectLimit, ECVF_SetByCode);
		TStrongObjectPtr<UMaterialInterface> Material(Owner->GetOrCreateLandMaterial(ObjectLimit, Pixels, Width, Height));
		auto* MID = Cast<UMaterialInstanceDynamic>(Material.Get());
		if (!TestNotNull(TEXT("Landscape material builds"), MID)) return false;
		UTexture* Bound = nullptr;
		MID->GetTextureParameterValue(TEXT("ACETexture"), Bound);
		auto* Texture = Cast<UTexture2D>(Bound);
		if (!TestNotNull(TEXT("Landscape material binds its texture"), Texture)) return false;
		TestEqual(TEXT("Both platform limits preserve the authored width"), Texture->GetSizeX(), 1024);
		TestEqual(TEXT("Both platform limits preserve the authored height"), Texture->GetSizeY(), 1024);
		TestEqual(TEXT("Landscape GPU format is uncompressed BGRA8"), Texture->GetPixelFormat(), PF_B8G8R8A8);
		TestEqual(TEXT("Full mip chain remains for stable distant terrain"), Texture->GetNumMips(), 11);
		TestEqual(TEXT("Terrain retains trilinear filtering"), Texture->Filter.GetValue(), TF_Trilinear);
		for (const auto& Mip : Texture->GetPlatformData()->Mips)
			TestEqual(TEXT("GPU upload does not retain a duplicate CPU mip chain"),Mip.BulkData.GetBulkDataSize(),int64(0));
		auto* Provider=Texture->GetAssetUserData<UACELandTextureMipProvider>();
		if (!TestNotNull(TEXT("Terrain has a recreatable source provider"),Provider)) return false;
		for(int32 Pass=0;Pass<2;++Pass)
		{
			TArray<void*> Uploads; Uploads.SetNumZeroed(Texture->GetNumMips());
			TArray<int64> Sizes; Sizes.SetNumZeroed(Uploads.Num());
			TestTrue(TEXT("All mips can be uploaded again after resource recreation"),Texture->GetInitialMipData(0,Uploads,Sizes));
			TestTrue(TEXT("Uploaded terrain exactly preserves every baked texel"),Uploads[0] && Sizes[0]==Pixels.Num()*sizeof(FColor)
				&& FMemory::Memcmp(Uploads[0],Pixels.GetData(),Sizes[0])==0);
			TArray<FColor> Expected=Pixels;
			int32 W=Width,H=Height;
			for(int32 M=1;M<Uploads.Num();++M)
			{
				TArray<FColor> Next; FACEDatTextureResolver::BuildOpaqueMip(Expected,W,H,Next);
				Expected=MoveTemp(Next); W=FMath::Max(1,W/2); H=FMath::Max(1,H/2);
				TestTrue(TEXT("Provider preserves the existing mip filter"),Uploads[M] && Sizes[M]==Expected.Num()*sizeof(FColor)
					&& FMemory::Memcmp(Uploads[M],Expected.GetData(),Sizes[M])==0);
			}
			for(void* Data:Uploads) FMemory::Free(Data);
			Texture->UpdateResource(); FlushRenderingCommands();
			TestTrue(TEXT("Recreated terrain has a live GPU resource"),Texture->GetResource() && Texture->GetResource()->TextureRHI.IsValid());
		}
	}

	// Fixed results from retail ImgTex::MergeTexture's byte blend. A linear-light
	// midpoint would be about 188, creating a visibly different transition band.
	FACEDatDecodedSurface Overlay, Alpha;
	Overlay.bHasPixels = Alpha.bHasPixels = true;
	Overlay.Width = Overlay.Height = 1; Overlay.Pixels = { FColor::White };
	Alpha.Width = Alpha.Height = 2;
	Alpha.Pixels = { FColor(0,0,0), FColor(128,128,128), FColor(129,129,129), FColor(255,255,255) };
	TArray<FColor> Blended; Blended.Init(FColor::Black, 4);
	Builder.MergeOverlay(Blended, 2, Overlay, 1, Alpha, 0);
	TestTrue(TEXT("Retail byte blend retains endpoints, midpoint and above-midpoint rounding"),
		Blended == TArray<FColor>{ FColor(127,127,127), FColor::White, FColor::Black, FColor(125,125,125) });
	Blended.Init(FColor::Black, 4);
	Builder.MergeOverlay(Blended, 2, Overlay, 1, Alpha, 1);
	TestTrue(TEXT("Blend masks retain rotation and terrain U mirroring"),
		Blended == TArray<FColor>{ FColor::Black, FColor(127,127,127), FColor(125,125,125), FColor::White });

	// The disk cache remains lossless and rejects the older linear-light bakes.
	const uint64 Fingerprint = 0xF1DE117100000000ull | FPlatformProcess::GetCurrentProcessId();
	const FString CachePath = ACEPCodeTileCache::MakeCacheFilePath(0, Fingerprint);
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*CachePath); };
	TestTrue(TEXT("Full-resolution blend is saved losslessly"), ACEPCodeTileCache::Save(0, Fingerprint, Width, Height, Pixels));
	TArray<FColor> Reloaded; int32 ReloadedWidth = 0, ReloadedHeight = 0;
	TestTrue(TEXT("Full-resolution blend cache loads"), ACEPCodeTileCache::Load(0, Fingerprint, ReloadedWidth, ReloadedHeight, Reloaded));
	TestTrue(TEXT("Cache retains exact dimensions and texels"), ReloadedWidth == Width && ReloadedHeight == Height && Reloaded == Pixels);
	TArray<uint8> OldCache;
	if (FFileHelper::LoadFileToArray(OldCache, *CachePath) && OldCache.Num() >= 8)
	{
		const uint32 OldSchema = 1;
		FMemory::Memcpy(OldCache.GetData() + 4, &OldSchema, sizeof(OldSchema));
		FFileHelper::SaveArrayToFile(OldCache, *CachePath);
		TestFalse(TEXT("Old blend colors cannot be silently reused"), ACEPCodeTileCache::Load(0, Fingerprint, ReloadedWidth, ReloadedHeight, Reloaded));
	}
	else AddError(TEXT("Could not read the blend cache fixture"));
	return !HasAnyErrors();
}
#endif
