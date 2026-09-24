#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACEVRSettings.generated.h"

/** Per-user settings, saved independently of retail character options. Distances are centimeters. */
UCLASS(Config = ACEVR, DefaultConfig)
class ACECLIENT_API UACEVRSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category = "VR") float EyeHeightOffset = 0.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bMatchCharacterHeight = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bEnabled = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bLeftHanded = false;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bSnapTurn = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float SnapDegrees = 30.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float SmoothTurnDegreesPerSecond = 60.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float StickDeadZone = .2f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float MovementScale = 1.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bRun = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float MovementSmoothing = .12f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float WristSmoothing = .07f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bPinMenuToView = false;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bPinHotbarToView = false;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bShowWristSpellBar = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bShowCompass = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bPinVitalsToView = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bPinChatToView = false;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bVitalsLocked = true;
	/** 0: view, 1: stable body frame, 2: world position until recentered. */
	UPROPERTY(Config, EditAnywhere, Category = "VR") int32 VitalsAnchorMode = 1;
	UPROPERTY(Config, EditAnywhere, Category = "VR") FVector VitalsViewOffset = FVector(100, -24, -22);
	UPROPERTY(Config) int32 SettingsVersion = 0;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bHeadRelativeMovement = true;
	/** -1 imports the legacy preference; 0=head, 1=hand, 2=playspace/stick only. */
	UPROPERTY(Config, EditAnywhere, Category = "VR") int32 MovementDirection = -1;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bSeated = false;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float SeatedEyeHeight = 165.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bShowBody = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") bool bHaptics = true;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float PanelDistance = 90.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float PanelScale = .11f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float WristScale = .06f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float VitalsScale = .0935f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float ChatScale = .0715f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float ForwardAssistDegrees = 10.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float MeleeMinSpeed = 250.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float BowFullDraw = 60.f;
	/** Natural cheek-side anchor, mirrored with handedness; centimeters. */
	UPROPERTY(Config, EditAnywhere, Category = "VR") float BowAnchorOffset = 10.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float HandPitch = 10.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") float RenderScale = 100.f;
	UPROPERTY(Config, EditAnywhere, Category = "VR") int32 MSAASamples = 4;
	static bool SupportsMSAASettings();
	void ApplyEdgeSmoothing() const;
	/** UE 5.8's HMD render-target scale, shared by OpenXR on PC and Android. */
	void ApplyRenderScale() const;
	void Sanitize();
	void Persist();
};
