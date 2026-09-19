#include "VR/ACEVRSettings.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "SceneUtils.h"
#include "RHI.h"

bool UACEVRSettings::SupportsMSAASettings()
{
	return GetDefaultAntiAliasingMethod(GMaxRHIFeatureLevel) == AAM_MSAA;
}

void UACEVRSettings::ApplyEdgeSmoothing() const
{
	// The deferred PC renderer keeps its existing temporal AA. Forward OpenXR
	// renderers expose the sample count; 4x is the supported Quest quality default.
	if (SupportsMSAASettings())
		if (auto* Samples = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MSAACount")))
			// ProjectSetting outranks GameSetting, so the previous assignment was
			// ignored. A deliberate user choice must override the project default.
			Samples->Set(MSAASamples == 2 ? 2 : 4, ECVF_SetByCode);
}

void UACEVRSettings::ApplyRenderScale() const
{
	// UE 5.8 no longer registers vr.PixelDensity. Primary screen percentage is
	// also ineffective for a fixed-size mobile LDR eye buffer.
	if (auto* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("xr.SecondaryScreenPercentage.HMDRenderTarget")))
		Scale->Set(FMath::IsFinite(RenderScale) ? FMath::Clamp(RenderScale, 60.f, 150.f) : 100.f, ECVF_SetByGameSetting);
}

void UACEVRSettings::Sanitize()
{
	if (MovementDirection < 0) MovementDirection = bHeadRelativeMovement ? 0 : 1;
	MovementDirection = FMath::Clamp(MovementDirection, 0, 2);
	if (SettingsVersion < 1)
	{
		if (FMath::IsNearlyEqual(WristScale, .045f)) WristScale = .06f;
		SettingsVersion = 1;
	}
	if (SettingsVersion < 2)
	{
		if (FMath::IsNearlyEqual(MovementScale, .65f)) MovementScale = 1.f;
		SettingsVersion = 2;
	}
	auto Clamp = [](float& V, float Default, float Low, float High)
	{ V = FMath::IsFinite(V) ? FMath::Clamp(V, Low, High) : Default; };
	Clamp(SnapDegrees, 30.f, 15.f, 90.f);
	Clamp(SmoothTurnDegreesPerSecond, 60.f, 15.f, 360.f);
	Clamp(StickDeadZone, .2f, .05f, .5f);
	Clamp(MovementScale, 1.f, .25f, 1.f);
	Clamp(MovementSmoothing, .12f, 0.f, .35f);
	Clamp(WristSmoothing, .07f, 0.f, .2f);
	Clamp(SeatedEyeHeight, 165.f, 100.f, 210.f);
	Clamp(EyeHeightOffset, 0.f, -30.f, 30.f);
	Clamp(PanelDistance, 90.f, 70.f, 180.f);
	Clamp(PanelScale, .11f, .05f, .15f);
	Clamp(WristScale, .06f, .03f, .085f);
	Clamp(VitalsScale, .0935f, .05f, .18f);
	Clamp(ChatScale, .0715f, .04f, .15f);
	Clamp(ForwardAssistDegrees, 10.f, 0.f, 20.f);
	if (VitalsViewOffset.ContainsNaN()) VitalsViewOffset = FVector(100, -24, -22);
	VitalsAnchorMode = FMath::Clamp(VitalsAnchorMode, 0, 2);
	VitalsViewOffset.X = FMath::Clamp(VitalsViewOffset.X, 60., 160.);
	VitalsViewOffset.Y = FMath::Clamp(VitalsViewOffset.Y, -VitalsViewOffset.X * .8, VitalsViewOffset.X * .8);
	VitalsViewOffset.Z = FMath::Clamp(VitalsViewOffset.Z, -VitalsViewOffset.X * .65, VitalsViewOffset.X * .65);
	Clamp(MeleeMinSpeed, 250.f, 250.f, 400.f);
	Clamp(BowFullDraw, 60.f, 30.f, 90.f);
	Clamp(BowAnchorOffset, 10.f, 0.f, 20.f);
	Clamp(HandPitch, 10.f, -30.f, 30.f);
	Clamp(RenderScale, 100.f, 60.f, 150.f);
	if (MSAASamples != 2 && MSAASamples != 4) MSAASamples = 4;
}

void UACEVRSettings::Persist()
{
	Sanitize();
	// Explicit generated config path: never overwrite DefaultACEVR.ini in a packaged game.
	const FString Filename = GConfig->GetConfigFilename(TEXT("ACEVR"));
	SaveConfig(CPF_Config, *Filename);
}
