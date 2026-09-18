#pragma once
#include "CoreMinimal.h"

namespace ACEVRMath
{
	// Same launch-speed scaling and gravity as Player.FireVRMissile.
	inline FVector BallisticOffset(const FVector& Direction, float Speed, float Power, float Seconds, float WorldScale)
	{
		return Direction.GetSafeNormal()*Speed*(.35f+.65f*FMath::Clamp(Power,0.f,1.f))*Seconds*WorldScale
			- FVector(0,0,.5f*9.8f*Seconds*Seconds*WorldScale);
	}
	inline FTransform BodyFromHead(const FTransform& Head, const FTransform& HeadBind,
		const FVector& EyeLocal, float Scale)
	{
		const FQuat Facing = FRotator(0, Head.Rotator().Yaw - 90.f, 0).Quaternion();
		const FQuat HeadRotation = Head.GetRotation() * FRotator(0,-90,0).Quaternion() * HeadBind.GetRotation();
		const FVector Neck = Head.GetLocation() - HeadRotation.RotateVector(EyeLocal * Scale);
		return FTransform(Facing, Neck - Facing.RotateVector(HeadBind.GetLocation() * Scale), FVector(Scale));
	}
	inline FVector2D DeadZone(FVector2D V, float Zone, float Outer = 1.f)
	{
		if (!FMath::IsFinite(V.X) || !FMath::IsFinite(V.Y)) return FVector2D::ZeroVector;
		const double Length = V.Size();
		if (Length <= Zone) return FVector2D::ZeroVector;
		return V / Length * FMath::Clamp((Length - Zone) / FMath::Max(.01f, Outer - Zone), 0.f, 1.f);
	}
	inline FVector2D AssistForward(FVector2D V, float Degrees)
	{
		if (V.IsNearlyZero() || Degrees <= 0.f) return V;
		const double Angle = FMath::Atan2(FMath::Abs(V.X), FMath::Abs(V.Y));
		const double Snap = FMath::DegreesToRadians(FMath::Clamp(Degrees, 0.f, 20.f));
		const double Diagonal = PI / 4.;
		if (Angle >= Diagonal) return V;
		// Preserve magnitude and true diagonals, with a continuous transition
		// out of the forward/back sector. Radial dead zones alone leave small
		// lateral thumb deflections as an unwanted strafe even at full forward.
		const double Adjusted = FMath::Max(0., Angle-Snap) * Diagonal / (Diagonal-Snap);
		return FVector2D(FMath::Sign(V.X)*FMath::Sin(Adjusted), FMath::Sign(V.Y)*FMath::Cos(Adjusted)) * V.Size();
	}
	inline FString KeyboardCharacter(TCHAR Key, bool Shift)
	{
		if (Shift)
		{
			const FString Plain = TEXT("`1234567890-=[]\\;',./");
			const FString Upper = TEXT("~!@#$%^&*()_+{}|:\"<>?");
			int32 Index;
			if (Plain.FindChar(Key, Index)) Key = Upper[Index];
			else Key = FChar::ToUpper(Key);
		}
		return FString(1, &Key);
	}

	/** Two-bone solution with a stable elbow pole and reachable wrist. */
	inline void SolveArm(const FVector& Shoulder, const FVector& Hand, const FVector& Pole,
		float Upper, float Lower, FVector& Elbow, FVector& Wrist)
	{
		Upper = FMath::Max(1.f, Upper); Lower = FMath::Max(1.f, Lower);
		const FVector Dir = (Hand - Shoulder).GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
		const float Distance = FMath::Clamp(float(FVector::Distance(Hand, Shoulder)),
			FMath::Abs(Upper - Lower) + .01f, Upper + Lower - .01f);
		const float Along = (Upper * Upper - Lower * Lower + Distance * Distance) / (2.f * Distance);
		FVector Bend = FVector::VectorPlaneProject(Pole - Shoulder, Dir).GetSafeNormal();
		if (Bend.IsNearlyZero()) Bend = FVector::VectorPlaneProject(FVector::UpVector, Dir).GetSafeNormal();
		if (Bend.IsNearlyZero()) Bend = FVector::RightVector;
		Elbow = Shoulder + Dir * Along + Bend * FMath::Sqrt(FMath::Max(0.f, Upper * Upper - Along * Along));
		Wrist = Shoulder + Dir * Distance;
	}

	struct FSwing
	{
		FVector Previous = FVector::ZeroVector, Start = FVector::ZeroVector;
		FVector StrikeDirection = FVector::ZeroVector, RecoveryStart = FVector::ZeroVector;
		bool bValid = false, bArmed = true;
		float Travel = 0.f, Elapsed = 0.f, Rest = 0.f;
		void Reset() { bValid = false; bArmed = true; Travel = Elapsed = Rest = 0.f; }
		bool Sample(const FVector& LocalTip, float Dt, float MinSpeed, bool bTracked, float& OutSpeed)
		{
			OutSpeed = 0.f;
			if (!bTracked || LocalTip.ContainsNaN() || Dt <= 0.f || Dt > .1f) { Reset(); return false; }
			if (!bValid) { Previous = LocalTip; bValid = true; return false; }
			const float Distance = FVector::Distance(LocalTip, Previous);
			const FVector Last = Previous;
			Previous = LocalTip;
			// Discontinuities (recenter, teleport or tracking reacquisition) cannot be attacks.
			if (Distance > 100.f) { Reset(); return false; }
			OutSpeed = Distance / Dt;
			if (!bArmed && FVector::DotProduct(LocalTip-RecoveryStart, StrikeDirection) > 0.f) RecoveryStart = LocalTip;
			// A deliberate backhand is a new stroke; it need not stop for 150ms.
			// Measure net recovery, not accumulated jitter or continued follow-through.
			if (!bArmed && FVector::DotProduct(LocalTip-RecoveryStart, StrikeDirection) <= -12.f)
			{
				bArmed = true; Start = Last; Elapsed = Travel = 0.f;
			}
			if (OutSpeed < MinSpeed * .45f)
			{
				Rest += Dt;
				if (Rest >= .15f) bArmed = true;
				Travel = Elapsed = 0.f; return false;
			}
			Rest = 0.f;
			if (Elapsed == 0.f || Elapsed + Dt > .25f) { Start = Last; Elapsed = 0.f; }
			Elapsed += Dt; Travel = FVector::Distance(Start, LocalTip);
			// Net travel rejects oscillating tracking noise. A deliberate strike
			// needs time, distance and speed; one quiet frame cannot re-arm it.
			return bArmed && OutSpeed >= MinSpeed && Elapsed >= .04f && Travel >= 18.f
				&& Travel / Elapsed >= MinSpeed;
		}
		void Commit() { bArmed = false; Rest = 0.f; StrikeDirection = (Previous-Start).GetSafeNormal(); RecoveryStart = Previous; }
		bool HasDeliberateHandMotion(const FVector& GripStart, const FVector& GripNow) const
		{
			// Pointing a long blade can move its tip quickly while the hand barely
			// moves. A strike needs sustained arm motion as well as tip speed.
			const float HandTravel = FVector::Distance(GripStart, GripNow);
			return Elapsed >= .04f && Travel >= 18.f && HandTravel >= 8.f
				&& HandTravel / Elapsed >= 100.f;
		}
	};
	inline bool BowDraw(const FVector& Bow, const FVector& String, const FVector& Forward,
		float FullDraw, float& Fraction)
	{
		const FVector Pull = Bow - String;
		// Two hands define a bow's aim. The support controller's pointing ray
		// can be vertical while grasping the riser; it must not invalidate a draw.
		const float Length = Pull.Size();
		Fraction = FMath::Clamp((Length - 12.f) / FMath::Max(18.f, FullDraw - 12.f), 0.f, 1.f);
		// FullDraw calibrates power, not the maximum permitted arm span.
		return !Pull.ContainsNaN() && Length >= 12.f && Length <= 180.f;
	}
}
