#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"

/**
 * Retail CameraSet / SmartBox / Render FOV constants.
 * Viewer offset is AC local (Y forward, Z up). WorldScale converts AC → cm.
 */
namespace ACECameraRetail
{
	/** SmartBox::m_fGameFOV — stored as 90° in radians, then divided by (aspect − 0.1). */
	constexpr float GameFovRadians = 1.5707964f;
	constexpr float DefaultPivotZAc = 1.5f;
	constexpr float DefaultOffsetYAc = -2.5f;
	constexpr float DefaultOffsetZAc = 0.75f;
	constexpr float InHeadOffsetYAc = 0.18000001f;
	constexpr float CloserMinLengthAc = 0.5f;
	constexpr float LeaveHeadYAc = -0.6f;
	constexpr float LeaveHeadZAc = 0.5f;
	constexpr float LookDownOffsetYAc = -2.0f;
	constexpr float LookDownOffsetZAc = 0.75f;
	/** CameraSet::SetMapMode uses -450 along the look-down camera frame. */
	constexpr float MapOffsetYAc = -450.f;
	/** CameraManager target direction while looking down: (0, 0.5, -1.8). */
	constexpr float LookDownDirY = 0.5f;
	constexpr float LookDownDirZ = -1.8f;
	constexpr float FartherMaxXYAc = 10.f;
	// CameraManager::UseTime: translation alpha = stiffness * dt * 10.
	constexpr float TranslationalStiffness = 0.45f;
	constexpr float TranslationLagSpeed = TranslationalStiffness * 10.f;
	constexpr float AdjustmentSpeed = 40.f;
	constexpr float ZoomRate = 0.2f;
	/** SpringArm defaults along −X; AC facing is pawn +Y, so boom yaw +90. */
	constexpr float BoomYawToFaceAcForward = 90.f;
	constexpr float NumpadOrbitDegreesPerSecond = 75.f;
	/** CameraSet::Raise/Lower translate by 0.2 units per call in look-down mode.
	 * Normalize the 60 Hz step to our shared keyboard/mouse orbit delta. */
	constexpr float LookDownUnitsPerOrbitDegree = 0.2f * 60.f / NumpadOrbitDegreesPerSecond;
	/** Optional camera controls requested by the player: orbit at rest, follow on movement. */
	inline float FollowTurnDegrees(float BoomYaw, float DeltaSeconds, bool bMoving, bool bFaceCamera = false)
	{
		if (!bMoving) return 0.f;
		const float Angle = FMath::UnwindDegrees(BoomYaw - BoomYawToFaceAcForward);
		// Both mouse buttons face the view before translating, without an initial arc.
		return bFaceCamera ? Angle : Angle * (1.f - FMath::Exp(-8.f * FMath::Max(0.f, DeltaSeconds)));
	}

	inline float HorizontalFovDegrees(float AspectWH)
	{
		const float Aspect = FMath::Max(AspectWH, 0.2f);
		const float Denom = FMath::Max(Aspect - 0.1f, 0.2f);
		const float VRad = GameFovRadians / Denom;
		return FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(VRad * 0.5f) * Aspect));
	}

	inline void ApplyFovToCamera(UCameraComponent* Camera, int32 SizeX, int32 SizeY)
	{
		if (!Camera || SizeY <= 0 || SizeX <= 0)
		{
			return;
		}
		Camera->SetConstraintAspectRatio(false);
		Camera->SetFieldOfView(HorizontalFovDegrees(static_cast<float>(SizeX) / static_cast<float>(SizeY)));
	}

	inline float OffsetLengthCm(float OffsetYAc, float OffsetZAc, float ScaleCm)
	{
		const float Y = OffsetYAc * ScaleCm;
		const float Z = OffsetZAc * ScaleCm;
		return FMath::Sqrt(Y * Y + Z * Z);
	}

	inline float PitchFromOffsetDegrees(float OffsetYAc, float OffsetZAc)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(-OffsetZAc, FMath::Abs(OffsetYAc)));
	}

	inline float DefaultArmLengthCm(float ScaleCm)
	{
		return OffsetLengthCm(DefaultOffsetYAc, DefaultOffsetZAc, ScaleCm);
	}

	inline float DefaultPitchDegrees()
	{
		return PitchFromOffsetDegrees(DefaultOffsetYAc, DefaultOffsetZAc);
	}

	inline float LookDownPitchDegrees()
	{
		return FMath::RadiansToDegrees(FMath::Atan2(LookDownDirZ, LookDownDirY));
	}

	inline float InHeadForwardCm(float ScaleCm)
	{
		return InHeadOffsetYAc * ScaleCm;
	}
}
