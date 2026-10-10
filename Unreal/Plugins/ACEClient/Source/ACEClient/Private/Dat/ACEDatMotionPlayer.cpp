#include "Dat/ACEDatMotionPlayer.h"
#include "Dat/ACEDatCursor.h"
#include "ACETypes.h"

const FACEDatAnimation* FACEDatMotionPlayer::LoadAnimation(uint32 AnimId) const
{
    if (const FACEDatAnimation* Cached = AnimCache.Find(AnimId)) return Cached;
    if (!Portal) return nullptr;
    TArray<uint8> Blob;
    if (!Portal->ReadFile(AnimId, Blob)) return nullptr;
    FACEDatCursor Cur(Blob);
    FACEDatAnimation Parsed;
    if (!ACEDatUnpack::UnpackAnimation(Cur, Parsed)) return nullptr;
    return &AnimCache.Add(AnimId, MoveTemp(Parsed));
}

const FACEDatMotionData* FACEDatMotionPlayer::FindCycle(uint32 MotionCommand, uint32 PreferredStyle) const
{
	if (MotionTable->Id == 0 && MotionTable->Cycles.Num() == 0)
	{
		return nullptr;
	}

	constexpr uint32 NonCombat = 0x8000003Du;
	constexpr uint32 HandCombat = 0x8000003Cu;
	constexpr uint32 SwordCombat = 0x8000003Eu;
	constexpr uint32 Magic = 0x80000049u;
	TArray<uint32, TInlineAllocator<8>> Styles;
	auto AddStyle = [&Styles](uint32 S)
	{
		if (S == 0)
		{
			return;
		}
		for (uint32 Existing : Styles)
		{
			if (Existing == S)
			{
				return;
			}
		}
		Styles.Add(S);
	};
	AddStyle(PreferredStyle);
	AddStyle(MotionTable->DefaultStyle);
	// Always fall back through common stances. Preferring HandCombat alone often misses
	// RunForward cycles that only exist under NonCombat / DefaultStyle.
	AddStyle(NonCombat);
	AddStyle(HandCombat);
	AddStyle(SwordCombat);
	AddStyle(Magic);

	const uint32 CmdMasked = MotionCommand & 0xFFFFFu;
	const uint32 CmdAlt = MotionCommand & 0xFFFFFFu;
	for (uint32 Style : Styles)
	{
		const FACEDatMotionData* Cycle = MotionTable->Cycles.Find((Style << 16) | CmdMasked);
		if ((!Cycle || Cycle->Anims.Num() == 0) && CmdAlt != CmdMasked)
		{
			Cycle = MotionTable->Cycles.Find((Style << 16) | CmdAlt);
		}
		if (Cycle && Cycle->Anims.Num() > 0 && Cycle->Anims[0].AnimId != 0)
		{
			return Cycle;
		}
	}
	return nullptr;
}

bool FACEDatMotionPlayer::FindCycleAnims(uint32 Command, TArray<FACEDatAnimData>& Out, uint32 Style) const
{
    Out.Reset();
    if (const auto* Cycle = FindCycle(Command, Style)) { Out = Cycle->Anims; return true; }
    return false;
}

bool FACEDatMotionPlayer::GetCycleVelocity(uint32 Command, uint32 Style, FVector& Out) const
{
    const auto* Cycle = FindCycle(Command, Style);
    if (!Cycle || !Cycle->bHasVelocity) return false;
    Out = FVector(Cycle->Velocity);
    return !Out.ContainsNaN();
}

bool FACEDatMotionPlayer::SetMotionTable(uint32 MotionTableId)
{
	if (bReady && MotionTable->Id == MotionTableId) return true;
    bReady = false;
    IdleAnim = FACEDatAnimData();
    if (!Portal || MotionTableId == 0) return false;
    if (const auto* Cached = MotionTableCache.Find(MotionTableId)) MotionTable = *Cached;
    else
    {
        TArray<uint8> Blob;
        if (!Portal->ReadFile(MotionTableId, Blob)) return false;
        FACEDatCursor Cur(Blob);
        auto Parsed = MakeShared<FACEDatMotionTable>();
        if (!ACEDatUnpack::UnpackMotionTable(Cur, *Parsed)) return false;
        MotionTableCache.Add(MotionTableId, Parsed);
        MotionTable = MoveTemp(Parsed);
    }

	uint32 Motion = 0;
	if (const uint32* Found = MotionTable->StyleDefaults.Find(MotionTable->DefaultStyle))
	{
		Motion = *Found;
	}
	else
	{
		Motion = 0x41000003; // MotionCommand.Ready
	}

	TArray<FACEDatAnimData> IdleAnims;
	if (!FindCycleAnims(Motion, IdleAnims))
	{
		if (!FindCycleAnims(0x41000003, IdleAnims))
		{
			UE_LOG(LogTemp, Warning, TEXT("ACEDat: no idle cycle on MotionTable 0x%08X"), MotionTableId);
			return false;
		}
	}
	IdleAnim = IdleAnims[0];

	bReady = true;
	return true;
}

const FACEDatMotionData* FACEDatMotionPlayer::FindLink(uint32 FromCommand, uint32 ToCommand, uint32 PreferredStyle) const
{
	if (MotionTable->Links.Num() == 0)
	{
		return nullptr;
	}

	const uint32 FromMasked = FromCommand & 0xFFFFFu;
	const uint32 ToFull = ToCommand;
	constexpr uint32 NonCombat = 0x8000003Du;
	constexpr uint32 HandCombat = 0x8000003Cu;
	constexpr uint32 SwordCombat = 0x8000003Eu;
	constexpr uint32 Magic = 0x80000049u;
	TArray<uint32, TInlineAllocator<8>> Styles;
	auto AddStyle = [&Styles](uint32 S)
	{
		if (S == 0)
		{
			return;
		}
		for (uint32 Existing : Styles)
		{
			if (Existing == S)
			{
				return;
			}
		}
		Styles.Add(S);
	};
	AddStyle(PreferredStyle);
	// Magic windups / cast gestures only exist under Magic stance Links — try Magic before
	// NonCombat so a PreferredStyle miss cannot leave us on a wrong ToMasked collision.
	const bool bMagicMotion = (ToCommand >= 0x1000006Fu && ToCommand <= 0x10000078u)
		|| (ToCommand >= 0x1000012Bu && ToCommand <= 0x10000134u)
		|| (ToCommand >= 0x4000002Bu && ToCommand <= 0x40000039u)
		|| ToCommand == 0x400000D3u || ToCommand == 0x400000E0u || ToCommand == 0x400000E1u;
	if (bMagicMotion)
	{
		AddStyle(Magic);
	}
	AddStyle(MotionTable->DefaultStyle);
	AddStyle(NonCombat);
	AddStyle(HandCombat);
	AddStyle(SwordCombat);
	AddStyle(Magic);

	auto TryKey = [&](uint32 OuterKey, uint32 InnerKey) -> const FACEDatMotionData*
	{
		const TMap<uint32, FACEDatMotionData>* Inner = MotionTable->Links.Find(OuterKey);
		if (!Inner)
		{
			return nullptr;
		}
		const FACEDatMotionData* Data = Inner->Find(InnerKey);
		if (!Data || Data->Anims.Num() == 0 || Data->Anims[0].AnimId == 0)
		{
			return nullptr;
		}
		return Data;
	};

	for (uint32 Style : Styles)
	{
		// Retail MotionTable::GetObjectLinkage uses the FULL motion id as the link key.
		// ToMasked cross-family fallbacks (CastSpell → Attack*) made monster casts look melee.
		for (uint32 Key : {(Style << 16) | FromMasked, (Style << 16) | (FromCommand & 0xFFFFFFu), Style << 16})
			if (const auto* Data = TryKey(Key, ToFull)) return Data;
		if (const uint32* DefaultMotion = MotionTable->StyleDefaults.Find(Style))
		{
			const uint32 DefMasked = (*DefaultMotion) & 0xFFFFFu;
			if (const auto* Data = TryKey((Style << 16) | DefMasked, ToFull)) return Data;
		}
	}
	return nullptr;
}

bool FACEDatMotionPlayer::FindTransitionAnims(uint32 From, uint32 To, TArray<FACEDatAnimData>& Out, uint32 PreferredStyle) const
{
	Out.Reset();
	if (From == To) return false;
	// CMotionTable::get_link checks the actual source and then the style-wide
	// link. A Ready->target fallback alone discards the source pose's exit clips.
	for (uint32 Style : {PreferredStyle != 0 ? PreferredStyle : MotionTable->DefaultStyle, MotionTable->DefaultStyle})
	{
		auto Link = [&](uint32 Source, uint32 Target) -> const FACEDatMotionData*
		{
			if (Source == Target) return nullptr;
			for (uint32 Key : {(Style << 16) | (Source & 0xFFFFFFu), Style << 16})
				if (const auto* Links = MotionTable->Links.Find(Key))
					if (const auto* Data = Links->Find(Target); Data && !Data->Anims.IsEmpty()) return Data;
			return nullptr;
		};
		if (const auto* Direct = Link(From, To)) { Out = Direct->Anims; return true; }
		if (const uint32* Ready = MotionTable->StyleDefaults.Find(Style))
		{
			if (const auto* Exit = Link(From, *Ready)) Out.Append(Exit->Anims);
			if (const auto* Enter = Link(*Ready, To)) Out.Append(Enter->Anims);
			if (!Out.IsEmpty()) return true;
		}
		if (Style == MotionTable->DefaultStyle) break;
	}
	return false;
}

bool FACEDatMotionPlayer::EvaluateTransition(uint32 From, uint32 To, float TimeSeconds, int32 NumParts,
	TArray<FTransform>& Out, float WorldScale, int32& Count, bool& Finished,
	const float* PreviousTime, TArray<FACEDatAnimationHook>* Hooks, uint32 Style) const
{
	TArray<FACEDatAnimData> Anims;
	Count = 0; Finished = false;
	return FindTransitionAnims(From, To, Anims, Style)
		&& EvaluateAnimSequence(Anims, TimeSeconds, NumParts, Out, WorldScale, Count, false, &Finished, PreviousTime, Hooks);
}

float FACEDatMotionPlayer::GetAnimDataDuration(const FACEDatAnimData& AnimData) const
{
	if (AnimData.AnimId == 0)
	{
		return 0.f;
	}
	const FACEDatAnimation* CachedAnim = LoadAnimation(AnimData.AnimId);
	if (!CachedAnim || CachedAnim->PartFrames.Num() == 0) return 0.f;
	const FACEDatAnimation& Anim = *CachedAnim;
	int32 Low = AnimData.LowFrame;
	int32 High = AnimData.HighFrame;
	if (High < 0 || High >= Anim.PartFrames.Num())
	{
		High = Anim.PartFrames.Num() - 1;
	}
	Low = FMath::Clamp(Low, 0, High);
	const int32 FrameCount = High - Low + 1;
	if (FrameCount <= 0)
	{
		return 0.f;
	}
	const float Rate = FMath::Abs(AnimData.Framerate);
	// CSequence::update_internal leaves zero-rate records on their initial frame.
	// These are held poses, not missing frame rates.
	return Rate > SMALL_NUMBER ? static_cast<float>(FrameCount) / Rate : MAX_flt;
}

bool FACEDatMotionPlayer::EvaluateAnimSequence(
	const TArray<FACEDatAnimData>& Anims, float TimeSeconds, int32 NumParts,
	TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
	bool bLoop, bool* bOutFinished, const float* PreviousTimeSeconds,
	TArray<FACEDatAnimationHook>* OutCrossedHooks) const
{
	OutAnimatedPartCount = 0;
	if (bOutFinished)
	{
		*bOutFinished = false;
	}
	if (Anims.Num() == 0)
	{
		return false;
	}
	if (Anims.Num() == 1)
	{
		return EvaluateAnimData(Anims[0], TimeSeconds, NumParts, OutPartTransforms, WorldScale, OutAnimatedPartCount,
			bLoop, bOutFinished, PreviousTimeSeconds, OutCrossedHooks);
	}

	TArray<float, TInlineAllocator<8>> Durations;
	float Total = 0.f;
	Durations.Reserve(Anims.Num());
	for (const FACEDatAnimData& Anim : Anims)
	{
		const float Dur = FMath::Max(GetAnimDataDuration(Anim), KINDA_SMALL_NUMBER);
		Durations.Add(Dur);
		Total += Dur;
	}

	float EvalTime = TimeSeconds;
	if (bLoop && Total > KINDA_SMALL_NUMBER)
	{
		EvalTime = FMath::Fmod(EvalTime, Total);
		if (EvalTime < 0.f)
		{
			EvalTime += Total;
		}
	}

	float Cursor = 0.f;
	for (int32 i = 0; i < Anims.Num(); ++i)
	{
		const float SegDur = Durations[i];
		const bool bLast = (i == Anims.Num() - 1);
		if (!bLast && EvalTime >= Cursor + SegDur)
		{
			Cursor += SegDur;
			continue;
		}
		const float LocalT = EvalTime - Cursor;
		float PrevLocalStorage = 0.f;
		const float* PrevLocal = nullptr;
		if (PreviousTimeSeconds)
		{
			float Prev = *PreviousTimeSeconds;
			if (bLoop && Total > KINDA_SMALL_NUMBER)
			{
				Prev = FMath::Fmod(Prev, Total);
				if (Prev < 0.f)
				{
					Prev += Total;
				}
			}
			if (Prev >= Cursor && Prev <= Cursor + SegDur + KINDA_SMALL_NUMBER)
			{
				PrevLocalStorage = Prev - Cursor;
				PrevLocal = &PrevLocalStorage;
			}
		}
		bool* Fin = (!bLoop && bLast) ? bOutFinished : nullptr;
		const bool bOk = EvaluateAnimData(Anims[i], LocalT, NumParts, OutPartTransforms, WorldScale, OutAnimatedPartCount,
			/*bLoop*/ false, Fin, PrevLocal, OutCrossedHooks);
		if (bOk && !bLoop && bLast && bOutFinished && TimeSeconds >= Total - KINDA_SMALL_NUMBER)
		{
			*bOutFinished = true;
		}
		return bOk;
	}
	return EvaluateAnimData(Anims.Last(), Durations.Last(), NumParts, OutPartTransforms, WorldScale, OutAnimatedPartCount,
		false, bOutFinished, nullptr, OutCrossedHooks);
}
bool FACEDatMotionPlayer::BuildTransitionRootTrack(uint32 From, uint32 To, uint32 Style, ACELandingMotion::FRootTrack& Out) const
{
	Out.Reset();
	TArray<FACEDatAnimData> Clips;
	if (!FindTransitionAnims(From, To, Clips, Style)) return false;
	return BuildRootTrack(Clips, Out);
}

bool FACEDatMotionPlayer::BuildWalkingLink(int8 From, int8 To, uint32 Style, ACEWalkingMotion::FLink& Out) const
{
	Out = {};
	if (Style == 0) Style = MotionTable->DefaultStyle;
	constexpr uint32 Ready = 0x41000003u, Walk = 0x45000005u;
	uint32 Source = From ? Walk : Ready, Target = To ? Walk : Ready;
	// CMotionTable::get_link reverses the lookup for negative movement. Entry
	// uses WalkBackwards' -0.65 rate; returning to Ready uses -1.
	const bool Reverse = From < 0 || To < 0;
	if (Reverse) Swap(Source, Target);
	const FACEDatMotionData* Data = nullptr;
	for (uint32 Key : {(Style << 16) | (Source & 0xFFFFFFu), Style << 16})
		if (const auto* Links = MotionTable->Links.Find(Key))
			if ((Data = Links->Find(Target))) break;
	// No cross-stance fallback: combat styles can intentionally omit walk links.
	if (!Data || Data->Anims.IsEmpty()) return false;
	Out.Clips = Data->Anims;
	const float Rate = To < 0 ? -.65f : Reverse ? -1.f : 1.f;
	for (auto& Clip : Out.Clips) Clip.Framerate *= Rate;
	return BuildRootTrack(Out.Clips, Out.Root);
}

bool FACEDatMotionPlayer::EvaluateWalkingLink(const ACEWalkingMotion::FLink& Link, int32 Parts,
	TArray<FTransform>& Out, float Scale, int32& Count, TArray<FACEDatAnimationHook>& Hooks) const
{
	return EvaluateAnimSequence(Link.Clips, Link.Root.Time, Parts, Out, Scale, Count, false, nullptr, &Link.PreviousTime, &Hooks);
}

bool FACEDatMotionPlayer::BuildRootTrack(const TArray<FACEDatAnimData>& Clips, ACELandingMotion::FRootTrack& Out) const
{
	Out.Reset();
	FTransform Pose = FTransform::Identity;
	float Time = 0.f;
	Out.Keys.Add({Time,Pose});
	for (const auto& Clip : Clips)
	{
		const FACEDatAnimation* Anim = LoadAnimation(Clip.AnimId);
		const float Rate = FMath::Abs(Clip.Framerate);
		if (!Anim || Anim->NumFrames <= 0 || Rate <= SMALL_NUMBER) { Out.Reset(); return false; }
		const int32 High = Clip.HighFrame < 0 ? int32(Anim->NumFrames)-1 : FMath::Min(Clip.HighFrame,int32(Anim->NumFrames)-1);
		const int32 Low = FMath::Clamp(Clip.LowFrame,0,High);
		for (int32 I=0; I<=High-Low; ++I)
		{
			const int32 Frame = Clip.Framerate < 0.f ? High-I : Low+I;
			FTransform Delta = Anim->PositionFrames.IsValidIndex(Frame)
				? FTransform(Anim->PositionFrames[Frame]) : FTransform::Identity;
			// CSequence::update_internal combines forward frames, subtracts reverse frames.
			if (Clip.Framerate < 0.f) Delta = Delta.Inverse();
			Pose.SetTranslation(Pose.GetTranslation()+Pose.GetRotation().RotateVector(Delta.GetTranslation()));
			Pose.SetRotation((Pose.GetRotation()*Delta.GetRotation()).GetNormalized());
			Time += 1.f/Rate;
			Out.Keys.Add({Time,Pose});
		}
	}
	return Out.IsActive();
}

bool FACEDatMotionPlayer::EvaluateAnimData(
	const FACEDatAnimData& AnimData, float TimeSeconds, int32 NumParts,
	TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
	bool bLoop, bool* bOutFinished, const float* PreviousTimeSeconds,
	TArray<FACEDatAnimationHook>* OutCrossedHooks, bool bInterpolateKeyframes) const
{
	OutAnimatedPartCount = 0;
	if (OutCrossedHooks)
	{
		OutCrossedHooks->Reset();
	}
	if (bOutFinished)
	{
		*bOutFinished = false;
	}
	OutPartTransforms.SetNum(NumParts);
	for (int32 i = 0; i < NumParts; ++i)
	{
		OutPartTransforms[i] = FTransform::Identity;
	}
	if (AnimData.AnimId == 0)
	{
		return false;
	}

	const FACEDatAnimation* CachedAnim = LoadAnimation(AnimData.AnimId);
	if (!CachedAnim || CachedAnim->PartFrames.Num() == 0) return false;
	const FACEDatAnimation& Anim = *CachedAnim;

	int32 Low = AnimData.LowFrame;
	int32 High = AnimData.HighFrame;
	if (High < 0 || High >= Anim.PartFrames.Num())
	{
		High = Anim.PartFrames.Num() - 1;
	}
	Low = FMath::Clamp(Low, 0, High);
	const int32 FrameCount = High - Low + 1;
	if (FrameCount <= 0)
	{
		return false;
	}

	const float SignedRate = AnimData.Framerate;
	const float Rate = FMath::Abs(SignedRate);
	const float Duration = Rate > SMALL_NUMBER ? static_cast<float>(FrameCount) / Rate : MAX_flt;
	float FrameF = Low + TimeSeconds * Rate;
	if (SignedRate < 0.f)
	{
		FrameF = High - TimeSeconds * Rate;
	}

	if (OutCrossedHooks)
	{
		auto RawFrameAtTime = [Low, High, SignedRate](float T)
		{
			return SignedRate < 0.f
				? static_cast<float>(High) + T * SignedRate
				: static_cast<float>(Low) + T * SignedRate;
		};
		auto WrapFrame = [Low, FrameCount](int32 LogicalFrame)
		{
			int32 Offset = (LogicalFrame - Low) % FrameCount;
			if (Offset < 0)
			{
				Offset += FrameCount;
			}
			return Low + Offset;
		};
		auto AppendFrameHooks = [&](int32 LogicalFrame, bool bForward)
		{
			const int32 FrameIndex = bLoop
				? WrapFrame(LogicalFrame)
				: FMath::Clamp(LogicalFrame, Low, High);
			if (!Anim.PartFrames.IsValidIndex(FrameIndex))
			{
				return;
			}
			for (const FACEDatAnimationHook& Hook : Anim.PartFrames[FrameIndex].Hooks)
			{
				const bool bDirectionMatches = Hook.Direction == EACEAnimationHookDirection::Both
					|| (bForward && Hook.Direction == EACEAnimationHookDirection::Forward)
					|| (!bForward && Hook.Direction == EACEAnimationHookDirection::Backward);
				if (bDirectionMatches)
				{
					OutCrossedHooks->Add(Hook);
				}
			}
		};

		float StartRaw = PreviousTimeSeconds ? RawFrameAtTime(*PreviousTimeSeconds) : RawFrameAtTime(0.f);
		float EndRaw = RawFrameAtTime(TimeSeconds);
		const bool bForward = EndRaw >= StartRaw;
		const int32 InitialFrame = SignedRate < 0.f ? High : Low;
		if (!PreviousTimeSeconds)
		{
			// Starting-frame hooks are edge events too, even when the first game tick is late.
			AppendFrameHooks(InitialFrame, bForward);
		}

		if (bLoop)
		{
			// A hitch may cross many cycles. Emit at most one cycle, keeping the most recent
			// edges; on startup the initial frame was emitted explicitly, so do not repeat it.
			const float MaxTravel = static_cast<float>(FMath::Max(FrameCount - (PreviousTimeSeconds ? 0 : 1), 0));
			if (FMath::Abs(EndRaw - StartRaw) > MaxTravel)
			{
				StartRaw = EndRaw + (bForward ? -MaxTravel : MaxTravel);
			}
		}
		else
		{
			StartRaw = FMath::Clamp(StartRaw, static_cast<float>(Low), static_cast<float>(High));
			EndRaw = FMath::Clamp(EndRaw, static_cast<float>(Low), static_cast<float>(High));
		}

		if (EndRaw > StartRaw + KINDA_SMALL_NUMBER)
		{
			const int32 First = FMath::FloorToInt(StartRaw) + 1;
			const int32 Last = FMath::FloorToInt(EndRaw);
			for (int32 LogicalFrame = First; LogicalFrame <= Last; ++LogicalFrame)
			{
				AppendFrameHooks(LogicalFrame, true);
			}
		}
		else if (EndRaw < StartRaw - KINDA_SMALL_NUMBER)
		{
			const int32 First = FMath::FloorToInt(StartRaw) - 1;
			const int32 Last = FMath::FloorToInt(EndRaw);
			for (int32 LogicalFrame = First; LogicalFrame >= Last; --LogicalFrame)
			{
				AppendFrameHooks(LogicalFrame, false);
			}
		}
	}
	if (bLoop)
	{
		// TimeSeconds is the lifetime of the current motion, not a frame delta.
		// Subtracting one cycle at a time made steady scenery/idle animations
		// progressively more expensive the longer a player stayed in a region.
		// Retail advances a bounded frame cursor; reduce to that same interval
		// in constant time, preserving the unwrapped times above for hook edges.
		FrameF = Low + FMath::Fmod(FrameF - Low, static_cast<float>(FrameCount));
		if (FrameF < Low) FrameF += FrameCount;
	}
	else
	{
		if (TimeSeconds >= Duration - KINDA_SMALL_NUMBER)
		{
			// Reverse-playback links (door Off) end at Low; forward links end at High.
			FrameF = (SignedRate < 0.f)
				? static_cast<float>(Low)
				: static_cast<float>(High);
			if (bOutFinished)
			{
				*bOutFinished = true;
			}
		}
		FrameF = FMath::Clamp(FrameF, static_cast<float>(Low), static_cast<float>(High));
	}
	const int32 FrameIndex = FMath::Clamp(FMath::FloorToInt(FrameF), Low, High);
	int32 NextIndex = FrameIndex + 1;
	if (bLoop)
	{
		if (NextIndex > High)
		{
			NextIndex = Low;
		}
	}
	else
	{
		NextIndex = FMath::Clamp(NextIndex, Low, High);
	}
	float FrameAlpha = 0.f;
	if (bInterpolateKeyframes)
	{
		FrameAlpha = FrameF - static_cast<float>(FrameIndex);
		if (!bLoop && FrameIndex >= High)
		{
			FrameAlpha = 0.f;
		}
	}
	const FACEDatAnimationFrame& Frame = Anim.PartFrames[FrameIndex];
	const FACEDatAnimationFrame& NextFrame = Anim.PartFrames[NextIndex];

	OutAnimatedPartCount = FMath::Min(NumParts, Frame.PartFrames.Num());
	for (int32 i = 0; i < OutAnimatedPartCount; ++i)
	{
		const FTransform3f& T0 = Frame.PartFrames[i];
		const FQuat4f R0 = T0.GetRotation();
		FTransform A(
			FACEPosition::AceQuatToUnreal(R0.W, FVector(R0.X, R0.Y, R0.Z)),
			FACEPosition::AceVectorToUnreal(FVector(T0.GetTranslation().X, T0.GetTranslation().Y, T0.GetTranslation().Z), WorldScale),
			FVector(1.f));
		if (FrameAlpha > KINDA_SMALL_NUMBER && NextFrame.PartFrames.IsValidIndex(i))
		{
			const FTransform3f& T1 = NextFrame.PartFrames[i];
			const FQuat4f R1 = T1.GetRotation();
			FTransform B(
				FACEPosition::AceQuatToUnreal(R1.W, FVector(R1.X, R1.Y, R1.Z)),
				FACEPosition::AceVectorToUnreal(FVector(T1.GetTranslation().X, T1.GetTranslation().Y, T1.GetTranslation().Z), WorldScale),
				FVector(1.f));
			FTransform Blended;
			Blended.Blend(A, B, FrameAlpha);
			OutPartTransforms[i] = Blended;
		}
		else
		{
			OutPartTransforms[i] = A;
		}
	}
	return OutAnimatedPartCount > 0;
}

bool FACEDatMotionPlayer::EvaluateIdle(float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
	const float* PreviousTimeSeconds, TArray<FACEDatAnimationHook>* OutCrossedHooks) const
{
	OutAnimatedPartCount = 0;
	if (OutCrossedHooks)
	{
		OutCrossedHooks->Reset();
	}
	if (!bReady)
	{
		return false;
	}
	return EvaluateAnimData(IdleAnim, TimeSeconds, NumParts, OutPartTransforms, WorldScale, OutAnimatedPartCount,
		true, nullptr, PreviousTimeSeconds, OutCrossedHooks);
}

bool FACEDatMotionPlayer::EvaluateMotion(uint32 MotionCommand, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount,
	const float* PreviousTimeSeconds, TArray<FACEDatAnimationHook>* OutCrossedHooks, uint32 PreferredStyle, bool bLoop, bool* bOutFinished) const
{
	OutAnimatedPartCount = 0;
	if (OutCrossedHooks)
	{
		OutCrossedHooks->Reset();
	}
	if (bOutFinished)
	{
		*bOutFinished = false;
	}
	if (!bReady)
	{
		return false;
	}
	const FACEDatMotionData* Cycle = FindCycle(MotionCommand, PreferredStyle);
	if (!Cycle)
	{
		// Let the caller choose a directional/reversed fallback. Returning an idle pose as
		// success made SideStepLeft and WalkBackwards silently appear motionless.
		return false;
	}
	// Evaluation only reads these records. Borrow the cached motion table
	// instead of allocating/copying its animation list for every actor tick.
	return EvaluateAnimSequence(Cycle->Anims, TimeSeconds, NumParts, OutPartTransforms, WorldScale, OutAnimatedPartCount,
		bLoop, bOutFinished, PreviousTimeSeconds, OutCrossedHooks);
}

bool FACEDatMotionPlayer::EvaluateLink(uint32 FromCommand, uint32 ToCommand, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount, bool& bOutFinished,
	const float* PreviousTimeSeconds, TArray<FACEDatAnimationHook>* OutCrossedHooks, uint32 PreferredStyle) const
{
	OutAnimatedPartCount = 0;
	bOutFinished = false;
	if (OutCrossedHooks)
	{
		OutCrossedHooks->Reset();
	}
	if (!bReady)
	{
		return false;
	}
	const auto* Link = FindLink(FromCommand, ToCommand, PreferredStyle);
	if (!Link)
	{
		return false;
	}
	// The cached table owns these immutable clips; casting/death poses need no per-frame copy.
	return EvaluateAnimSequence(Link->Anims, TimeSeconds, NumParts, OutPartTransforms, WorldScale, OutAnimatedPartCount,
		false, &bOutFinished, PreviousTimeSeconds, OutCrossedHooks);
}

bool FACEDatMotionPlayer::EvaluateAnimation(uint32 AnimationId, float TimeSeconds, int32 NumParts, TArray<FTransform>& OutPartTransforms, float WorldScale, int32& OutAnimatedPartCount, bool bLoop,
	const float* PreviousTimeSeconds, TArray<FACEDatAnimationHook>* OutCrossedHooks, float Framerate, int32 LowFrame) const
{
	OutAnimatedPartCount = 0;
	if (OutCrossedHooks)
	{
		OutCrossedHooks->Reset();
	}
	FACEDatAnimData AnimData;
	AnimData.AnimId = AnimationId;
	AnimData.LowFrame = LowFrame;
	AnimData.HighFrame = -1;
	AnimData.Framerate = Framerate;
	return EvaluateAnimData(AnimData, TimeSeconds, NumParts, OutPartTransforms, WorldScale, OutAnimatedPartCount,
		bLoop, nullptr, PreviousTimeSeconds, OutCrossedHooks, /*bInterpolateKeyframes*/ true);
}
