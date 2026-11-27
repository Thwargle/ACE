#pragma once
#include "CoreMinimal.h"

struct FACEVRPose
{
	uint32 Sequence = 0, Cell = 0, Teleport = 0, Flags = 0;
	uint32 Version = 1;
	int32 Weapon = 0, Ammo = 0;
	FVector Root = FVector::ZeroVector; // authoritative server feet, world meters
	float EyeHeight = 1.65f, Draw = 0;
	FTransform Poses[5] = {FTransform::Identity,FTransform::Identity,FTransform::Identity,FTransform::Identity,FTransform::Identity};
	double ReceivedAt = 0;
};

struct FACERemoteVRPose
{
	FACEVRPose Previous, Current;
	// At 20 Hz a 75 ms render delay needs more than the latest two packets.
	// Bound storage even when a peer sends poses faster than expected.
	TArray<FACEVRPose, TInlineAllocator<8>> History;
	bool Add(const FACEVRPose& Pose)
	{
		if (Current.ReceivedAt > 0 && int32(Pose.Sequence - Current.Sequence) <= 0) return false;
		if (Pose.Version != Current.Version || Pose.Teleport != Current.Teleport
			|| ((Pose.Flags ^ Current.Flags) & 4u) || Pose.ReceivedAt - Current.ReceivedAt >= .5) History.Reset();
		// Recovery can dispatch a late older sequence immediately before a
		// newer sequence that arrived earlier. Keep the sampling axis monotonic.
		while (!History.IsEmpty() && History.Last().ReceivedAt >= Pose.ReceivedAt)
			History.Pop(EAllowShrinking::No);
		Previous = History.IsEmpty() ? Pose : History.Last();
		Current = Pose;
		if (History.Num() == 8) History.RemoveAt(0);
		History.Add(Pose);
		return true;
	}
	FACEVRPose Sample(double Now) const
	{
		const double RenderTime = Now - .075;
		if (History.IsEmpty()) return Current;
		if (RenderTime <= History[0].ReceivedAt) return History[0];
		for (int32 I = 1; I < History.Num(); ++I)
		{
			const auto& A = History[I-1]; const auto& B = History[I];
			if (RenderTime > B.ReceivedAt) continue;
			const double Span = B.ReceivedAt - A.ReceivedAt;
			const float Alpha = Span > .0001 ? float(FMath::Clamp((RenderTime-A.ReceivedAt)/Span,0.,1.)) : 1.f;
			FACEVRPose Result = B;
			Result.ReceivedAt = FMath::Lerp(A.ReceivedAt, B.ReceivedAt, double(Alpha));
			// Equipment changes must not discard the walking/head/hand timeline.
			// Only the newly equipped object's transform starts at its new pose.
			for (int32 J = 0; J < 5; ++J)
				if (!((J==1 && ((A.Flags ^ B.Flags) & 1u)) || (J==2 && ((A.Flags ^ B.Flags) & 2u))
					|| (J==3 && A.Weapon!=B.Weapon) || (J==4 && A.Ammo!=B.Ammo)))
					Result.Poses[J].Blend(A.Poses[J], B.Poses[J], Alpha);
			Result.Root = FMath::Lerp(A.Root, B.Root, Alpha);
			Result.Draw = FMath::Lerp(A.Draw, B.Draw, Alpha);
			return Result;
		}
		// Hold on packet loss; extrapolating hands/root can send them through floors.
		return Current;
	}
};
