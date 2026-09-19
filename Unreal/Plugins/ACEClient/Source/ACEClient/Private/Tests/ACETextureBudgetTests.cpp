#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dat/ACEDatTextureResolver.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeExit.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "ACERuntimeOptions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACETextureBudgetTest, "ACE.Packaging.TextureBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACETextureBudgetTest::RunTest(const FString& Parameters)
{
	auto* Limit = IConsoleManager::Get().FindConsoleVariable(TEXT("ace.Texture.MaxWorldSize"));
	if (!TestNotNull(TEXT("Runtime texture limit is registered"), Limit)) return false;
	const int32 SavedLimit = Limit->GetInt();
	ON_SCOPE_EXIT { Limit->Set(SavedLimit, ECVF_SetByCode); };
	Limit->Set(512, ECVF_SetByCode);
	TArray<FColor> Pixels; Pixels.Init(FColor(80, 120, 160, 200), 1024 * 512);
	TStrongObjectPtr<UTexture2D> World(FACEDatTextureResolver::CreateTransientRgbaWithMips(1024, 512, Pixels, true));
	if (!TestNotNull(TEXT("Limited texture builds"), World.Get())) return false;
	TestEqual(TEXT("World texture respects the mobile size budget"), World->GetSizeX(), 512);
	TestEqual(TEXT("World texture retains aspect ratio"), World->GetSizeY(), 256);
	TestEqual(TEXT("Reduced world texture keeps the mip chain"), World->GetNumMips(), 10);
	TestEqual(TEXT("World textures retain the native-tested trilinear filtering"), World->Filter.GetValue(), TF_Trilinear);
	TStrongObjectPtr<UTexture2D> UI(FACEDatTextureResolver::CreateTransientRgbaWithMips(1024, 512, Pixels, true,
		TA_Wrap, TA_Wrap, false, true, 0, TF_Trilinear, false));
	TestEqual(TEXT("UI atlas coordinates retain their authored dimensions"), UI->GetSizeX(), 1024);
	Limit->Set(0, ECVF_SetByCode);
	TStrongObjectPtr<UTexture2D> Desktop(FACEDatTextureResolver::CreateTransientRgbaWithMips(1024, 512, Pixels, true));
	TestEqual(TEXT("Desktop preserves full world texture resolution"), Desktop->GetSizeX(), 1024);

	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UACEDatSubsystem> Owner(NewObject<UACEDatSubsystem>(Instance.Get()));
	TestFalse(TEXT("World cutouts retain the native-tested coverage"), Owner->EnsureAceOutdoorLitMaskedMaterialBase()->GetMaterial()->bUseAlphaToCoverage != 0);
	TestFalse(TEXT("Unlit cutouts retain the native-tested coverage"), Owner->EnsureAceUnlitMaskedMaterialBase()->GetMaterial()->bUseAlphaToCoverage != 0);
	FACEDatTextureResolver Resolver(nullptr, nullptr); Resolver.SetGcOwner(Owner.Get());
	// The strong pointer models a visible material retaining the texture while
	// the bounded cache releases it. No database is available to reload it.
	Resolver.TextureObjects.Add(123, World.Get());
	Resolver.BindRuntimeTexture(World.Get());
	Resolver.TextureLastUsed.Add(123, 0.);
	for (uint32 Key = 1000; Key < 1064; ++Key)
	{ Resolver.TextureObjects.Add(Key, nullptr); Resolver.TextureLastUsed.Add(Key, FPlatformTime::Seconds()); }
	Resolver.TrimCaches(64, 64, 64ll << 20);
	TestFalse(TEXT("LRU releases the stale strong cache entry"), Resolver.TextureObjects.Contains(123));
	TestTrue(TEXT("Evicted texture is remembered without pinning it"), Resolver.RetainedWorldTextures.Contains(123));
	TestTrue(TEXT("Visible texture is reused after eviction instead of duplicated"), Resolver.GetOrCreateUTexture(123, Owner.Get()) == World.Get());
	Resolver.DiscardNonSkyRuntimeTextures();
	TestEqual(TEXT("Explicit invalidation discards retained identities"), Resolver.RetainedWorldTextures.Num(), 0);
	TArray<FColor> LandPixels; LandPixels.Init(FColor::Green, 32 * 32);
	TStrongObjectPtr<UMaterialInterface> Land(Owner->GetOrCreateLandMaterial(0x12345678, LandPixels, 32, 32));
	if (!TestNotNull(TEXT("Terrain material builds"), Land.Get())) return false;
	const FVector ClipEye(100,200,170);
	Owner->SetLandLookOutClip(true,ClipEye,{});
	FLinearColor ClipCamera;
	auto* LandMid=Cast<UMaterialInstanceDynamic>(Land.Get());
	LandMid->GetVectorParameterValue(TEXT("LookOutCam"),ClipCamera);
	TestTrue(TEXT("An enabled portal mask follows the actual camera"),ClipCamera.Equals(FLinearColor(ClipEye)));
	Owner->SetLandLookOutClip(false,ClipEye,{});
	Owner->SetLandLookOutClip(false,ClipEye+FVector(10000,0,0),{});
	LandMid->GetVectorParameterValue(TEXT("LookOutCam"),ClipCamera);
	TestTrue(TEXT("Outdoor motion cannot dirty unused portal camera uniforms"),ClipCamera.Equals(FLinearColor(FVector::ZeroVector)));
	Owner->SetLandLookOutClip(true,ClipEye,{});
	LandMid->GetVectorParameterValue(TEXT("LookOutCam"),ClipCamera);
	TestTrue(TEXT("Re-entering a doorway restores the current camera immediately"),ClipCamera.Equals(FLinearColor(ClipEye)));
	TWeakObjectPtr<UMaterialInterface> OldLand = Land.Get();
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("Visible terrain reuses the same material after collection"),
		Owner->GetOrCreateLandMaterial(0x12345678, LandPixels, 32, 32) == Land.Get());
	Land.Reset();
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Departed terrain is not pinned forever by the material cache"), OldLand.IsValid());
	Owner->TrimTextureResolverCaches();
	TStrongObjectPtr<UMaterialInterface> Replacement(Owner->GetOrCreateLandMaterial(0x12345678, LandPixels, 32, 32));
	TestNotNull(TEXT("Returning to terrain recreates collected resources"), Replacement.Get());
	TStrongObjectPtr<UMaterialInstanceDynamic> Lit(Cast<UMaterialInstanceDynamic>(Owner->GetWorldObjectMaterial(Replacement.Get())));
	if (!TestNotNull(TEXT("World lighting variant builds"), Lit.Get())) return false;
	TWeakObjectPtr<UMaterialInterface> OldSource = Replacement.Get();
	Replacement.Reset(); CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Lighting cache does not retain its departed source material"), OldSource.IsValid());
	Owner->TrimTextureResolverCaches();
	Owner->SetWorldEmissiveScale(.21f, .37f, FLinearColor::Green);
	float Emission = 0.f; Lit->GetScalarParameterValue(TEXT("EmissiveStrength"), Emission);
	TestEqual(TEXT("Live variant still follows daylight after source collection"), Emission, .37f);
	TWeakObjectPtr<UMaterialInstanceDynamic> OldLit = Lit.Get(); Lit.Reset(); CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Lighting cache releases departed appearances"), OldLit.IsValid());
	const float OldMaster=ACERuntimeOptions::Get(TEXT("MasterVolume")), OldEffects=ACERuntimeOptions::Get(TEXT("EffectsVolume"));
	const float OldAmbient=ACERuntimeOptions::Get(TEXT("AmbientVolume")), OldFocus=ACERuntimeOptions::Get(TEXT("ActiveSoundOnly"));
	ACERuntimeOptions::Set(TEXT("ActiveSoundOnly"),0);ACERuntimeOptions::Set(TEXT("MasterVolume"),.5f);
	ACERuntimeOptions::Set(TEXT("EffectsVolume"),.8f);ACERuntimeOptions::Set(TEXT("AmbientVolume"),.2f);
	TestEqual(TEXT("Cached sound settings preserve independent effects volume"),ACERuntimeOptions::SoundGain(false),.4f);
	TestEqual(TEXT("Cached sound settings preserve independent ambient volume"),ACERuntimeOptions::SoundGain(true),.1f);
	ACERuntimeOptions::Set(TEXT("MasterVolume"),.25f);
	TestEqual(TEXT("Changing a slider invalidates sound gains within the same frame"),ACERuntimeOptions::SoundGain(false),.2f);
	ACERuntimeOptions::Set(TEXT("MasterVolume"),OldMaster);ACERuntimeOptions::Set(TEXT("EffectsVolume"),OldEffects);
	ACERuntimeOptions::Set(TEXT("AmbientVolume"),OldAmbient);ACERuntimeOptions::Set(TEXT("ActiveSoundOnly"),OldFocus);
	return true;
}
#endif
