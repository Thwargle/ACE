#pragma once

#include "CoreMinimal.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatFileTypes.h"

/** Evaluates MotionTable cycles (idle / walk / run) into part transforms. */
class ACECLIENT_API FACEDatMotionPlayer
{
public:
	FACEDatMotionPlayer(FACEDatDatabase* InPortal)
		: Portal(InPortal)
	{
	}

	bool SetMotionTable(uint32 MotionTableId);
	bool GetCycleVelocity(uint32 MotionCommand, uint32 Style, FVector& Out) const;
	int32 GetCachedMotionTableCount() const { return MotionTableCache.Num(); }

	/** DefaultStyle idle/ready cycle. OutAnimatedPartCount = parts written from the anim frame. */
	bool EvaluateIdle(float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
		const float* PreviousTimeSeconds = nullptr, TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr) const;

	/** Specific MotionCommand cycle. PreferredStyle tried first for Cycles key.
	 *  bLoop=false clamps to the final frame and sets bOutFinished (cast / emote holds). */
	bool EvaluateMotion(uint32 MotionCommand, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
		const float* PreviousTimeSeconds = nullptr, TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr, uint32 PreferredStyle = 0,
		bool bLoop = true, bool* bOutFinished = nullptr) const;

	/** Links[from]→to one-shot. bOutFinished is true once TimeSeconds past the last frame.
	 *  PreferredStyle (MotionStance) is tried first; 0 = MotionTable DefaultStyle only. */
	bool EvaluateLink(uint32 FromCommand, uint32 ToCommand, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount, bool& bOutFinished,
		const float* PreviousTimeSeconds = nullptr, TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr, uint32 PreferredStyle = 0) const;

	/** Direct Animation DID playback (looping). Portalspace uses Framerate=40, LowFrame=1. */
	bool EvaluateAnimation(uint32 AnimationId, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount, bool bLoop = true,
		const float* PreviousTimeSeconds = nullptr, TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr,
		float Framerate = 30.f, int32 LowFrame = 0) const;

	uint32 GetMotionTableId() const { return MotionTable->Id; }
	bool IsReady() const { return bReady; }

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FACERetailStreamingCostTest;
#endif
	const FACEDatAnimation* LoadAnimation(uint32 AnimId) const;
	const FACEDatMotionData* FindCycle(uint32 MotionCommand, uint32 PreferredStyle) const;
	bool FindCycleAnims(uint32 MotionCommand, TArray<FACEDatAnimData>& OutAnims, uint32 PreferredStyle = 0) const;
	bool FindLinkAnims(uint32 FromCommand, uint32 ToCommand, TArray<FACEDatAnimData>& OutAnims, uint32 PreferredStyle = 0) const;
	float GetAnimDataDuration(const FACEDatAnimData& AnimData) const;
	bool EvaluateAnimSequence(const TArray<FACEDatAnimData>& Anims, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
		bool bLoop = true, bool* bOutFinished = nullptr, const float* PreviousTimeSeconds = nullptr,
		TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr) const;
	bool EvaluateAnimData(const FACEDatAnimData& AnimData, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
		bool bLoop = true, bool* bOutFinished = nullptr, const float* PreviousTimeSeconds = nullptr,
		TArray<FACEDatAnimationHook>* OutCrossedHooks = nullptr, bool bInterpolateKeyframes = true) const;

	FACEDatDatabase* Portal = nullptr;
	TSharedPtr<FACEDatMotionTable> MotionTable = MakeShared<FACEDatMotionTable>();
	TMap<uint32, TSharedPtr<FACEDatMotionTable>> MotionTableCache;
	mutable TMap<uint32, FACEDatAnimation> AnimCache;
	FACEDatAnimData IdleAnim;
	bool bReady = false;
};
