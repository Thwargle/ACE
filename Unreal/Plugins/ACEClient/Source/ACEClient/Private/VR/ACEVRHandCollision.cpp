#include "VR/ACEVRHandCollision.h"
#include "Engine/World.h"

namespace
{
	constexpr float ContactSkin = .15f; // centimeters, independent of frame travel
	bool SweepWorld(UWorld* World, FHitResult& Hit, const FTransform& A, const FVector& End,
		const FVector& Extent, const FCollisionQueryParams& Query)
	{
		bool Blocked=World->SweepSingleByChannel(Hit,A.GetLocation(),End,A.GetRotation(),ECC_Pawn,
			FCollisionShape::MakeBox(Extent),Query);
		// Chaos can report its shallow contact skin at time zero even when the
		// hand moves parallel to or away from the wall. Retry with only that skin
		// removed, never by ignoring the wall component: it may contain the next
		// corner too. Real penetration and inward travel remain blocked.
		if (Blocked && Hit.Time<=KINDA_SMALL_NUMBER && Hit.PenetrationDepth<=.05f
			&& FVector::DotProduct(End-A.GetLocation(),Hit.Normal)>=-.001f)
			Blocked=World->SweepSingleByChannel(Hit,A.GetLocation(),End,A.GetRotation(),ECC_Pawn,
				FCollisionShape::MakeBox((Extent-FVector(.1f)).ComponentMax(FVector(.1f))),Query);
		return Blocked;
	}
	FTransform Between(const FTransform& A, const FTransform& B, float Alpha)
	{
		return FTransform(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha).GetNormalized(),
			FMath::Lerp(A.GetLocation(), B.GetLocation(), Alpha));
	}

	// Continuous box/triangle SAT. Animation only changes the visible triangles'
	// transforms; it needs no cooked physics meshes or additional draw calls.
	bool TriangleSweep(const FACEVRContactTriangle& T, const FTransform& Box, const FVector& End,
		const FVector& Extent, float& Time, FVector& Normal, bool Stationary = false, float* Depth = nullptr)
	{
		const FVector A=Box.InverseTransformPosition(T.A), B=Box.InverseTransformPosition(T.B), C=Box.InverseTransformPosition(T.C);
		const FVector V=Box.InverseTransformVectorNoScale(End-Box.GetLocation());
		const FVector Edges[]={B-A,C-B,A-C};
		FVector Axes[13]={FVector::XAxisVector,FVector::YAxisVector,FVector::ZAxisVector,FVector::CrossProduct(B-A,C-A)};
		for(int E=0;E<3;++E) for(int I=0;I<3;++I) Axes[4+E*3+I]=FVector::CrossProduct(Edges[E],Axes[I]);
		double Enter=-DBL_MAX, Exit=DBL_MAX, MinDepth=DBL_MAX; FVector HitNormal=FVector::ZeroVector;
		for(const FVector& Axis:Axes)
		{
			if(Axis.SizeSquared()<1.e-12) continue;
			const double P=FVector::DotProduct(A,Axis), Q=FVector::DotProduct(B,Axis), R=FVector::DotProduct(C,Axis);
			const double Radius=FVector::DotProduct(Extent,Axis.GetAbs());
			const double Low=FMath::Min3(P,Q,R)-Radius, High=FMath::Max3(P,Q,R)+Radius;
			const double Speed=FVector::DotProduct(V,Axis);
			if (Stationary)
			{
				if (Low>0 || High<0) return false;
				const double Push=FMath::Min(-Low,High)/Axis.Size();
				if (Push<MinDepth) { MinDepth=Push; HitNormal=Box.TransformVectorNoScale(Axis.GetSafeNormal())*(-Low<High ? -1. : 1.); }
				continue;
			}
			if(FMath::Abs(Speed)<1.e-9) { if(Low>0 || High<0) return false; continue; }
			double First=Low/Speed, Last=High/Speed; if(First>Last) Swap(First,Last);
			if(First>Enter) { Enter=First; HitNormal=Box.TransformVectorNoScale(Axis.GetSafeNormal())*(Speed>0?-1.:1.); }
			Exit=FMath::Min(Exit,Last); if(Enter>Exit) return false;
		}
		if(Exit<0 || Enter>1 || (!Stationary && Exit<=1.e-6 && Enter<0)) return false;
		Time=FMath::Clamp(float(Enter),0.f,1.f); Normal=HitNormal;
		if (Depth) *Depth=float(MinDepth);
		return true;
	}

	// Contact can start fractionally inside a moving surface or after changing
	// equipment. Resolve locally instead of resetting the hand to the torso.
	bool Depenetrate(UWorld* World, FTransform& Pose, const TArray<FACEVRContactShape>& Shapes,
		const FCollisionQueryParams& Query, const TArray<FACEVRContactTriangle>* Mesh, float Limit)
	{
		const FTransform Original=Pose;
		for (int32 Pass=0; Pass<8; ++Pass)
		{
			float Depth=0.f; FVector Normal=FVector::ZeroVector;
			for (const auto& Shape:Shapes)
			{
				const FTransform Box=Shape.Local*Pose;
				FHitResult Hit;
				if (World->SweepSingleByChannel(Hit,Box.GetLocation(),Box.GetLocation(),Box.GetRotation(),ECC_Pawn,
					FCollisionShape::MakeBox(Shape.Extent),Query) && Hit.bStartPenetrating && Hit.PenetrationDepth>Depth)
				{ Depth=Hit.PenetrationDepth; Normal=Hit.Normal; }
				if (Mesh)
				{
					const FBox Broad=FBox(-Shape.Extent,Shape.Extent).TransformBy(Box);
					for (const auto& T:*Mesh)
					{
						float Time, Push; FVector N;
						if (Broad.Intersect(T.Bounds) && TriangleSweep(T,Box,Box.GetLocation(),Shape.Extent,Time,N,true,&Push) && Push>=Depth)
						{ Depth=Push; Normal=N; }
					}
				}
			}
			if (Normal.IsNearlyZero()) break;
			Pose.AddToTranslation(Normal*(Depth+.15f));
			if (FVector::Distance(Pose.GetLocation(),Original.GetLocation())>Limit) { Pose=Original; return false; }
		}
		if (!ACEVRHandCollision::Overlaps(World,Pose,Shapes,Query,Mesh)) return true;
		Pose=Original; return false;
	}

	FTransform Advance(UWorld* World, const FTransform& From, const FTransform& To,
		const TArray<FACEVRContactShape>& Shapes, const FCollisionQueryParams& Query, const TArray<FACEVRContactTriangle>* Mesh, FVector& Normal, bool& Blocked)
	{
		float Fraction = 1.f;
		for (const auto& Shape : Shapes)
		{
			const FTransform A = Shape.Local * From, B = Shape.Local * To;
			FHitResult Hit;
			// Sweeping each part also covers translation of an offset weapon when
			// the controller rotates. Angular subdivision handles the curved path.
			if (SweepWorld(World,Hit,A,B.GetLocation(),Shape.Extent,Query) && Hit.Time <= Fraction)
			{
				Fraction = FMath::Max(0.f, Hit.Time - ContactSkin/FMath::Max(ContactSkin,float(FVector::Distance(A.GetLocation(),B.GetLocation()))));
				Normal = Hit.Normal; Blocked = true;
			}
			if(Mesh)
			{
				FBox Broad=FBox(-Shape.Extent,Shape.Extent).TransformBy(A);
				Broad+=Broad.ShiftBy(B.GetLocation()-A.GetLocation());
				for(const auto& Triangle:*Mesh)
				{
					float Time; FVector MeshNormal;
					if(Broad.Intersect(Triangle.Bounds) && TriangleSweep(Triangle,A,B.GetLocation(),Shape.Extent,Time,MeshNormal) && Time<=Fraction)
					{
						Fraction=FMath::Max(0.f,Time-ContactSkin/FMath::Max(ContactSkin,float(FVector::Distance(A.GetLocation(),B.GetLocation()))));
						Normal=MeshNormal; Blocked=true;
					}
				}
			}

		}
		FTransform Result = Between(From, To, Fraction);
		// A box can rotate into a wall without its centre moving. Find the last
		// clear orientation; a translation-only sweep misses this case entirely.
		if (ACEVRHandCollision::Overlaps(World, Result, Shapes, Query, Mesh))
		{
			float Low = 0.f, High = Fraction;
			for (int32 I=0; I<8; ++I)
			{
				const float Mid = (Low + High) * .5f;
				if (ACEVRHandCollision::Overlaps(World, Between(From, To, Mid), Shapes, Query, Mesh)) High = Mid;
				else Low = Mid;
			}
			Result = Between(From, To, Low); Blocked = true;
			// A rotation-only impact has no translational sweep normal. Obtain a
			// surface normal from its penetrating pose and leave a fixed clearance;
			// the next tangential sweep must not begin exactly on that surface.
			FTransform Clear=Between(From,To,FMath::Min(Fraction,High+.001f));
			if (Depenetrate(World,Clear,Shapes,Query,Mesh,ContactSkin*4.f))
			{
				const FVector Push=Clear.GetLocation()-Between(From,To,FMath::Min(Fraction,High+.001f)).GetLocation();
				if (!Push.IsNearlyZero())
				{
					Normal=Push.GetSafeNormal();
					FTransform Spaced=Result; Spaced.AddToTranslation(Normal*ContactSkin);
					if (!ACEVRHandCollision::Overlaps(World,Spaced,Shapes,Query,Mesh)) Result=Spaced;
				}
			}
		}
		return Result;
	}

	void Translate(UWorld* World, FTransform& Pose, const FVector& Destination,
		const TArray<FACEVRContactShape>& Shapes, const FCollisionQueryParams& Query,
		const TArray<FACEVRContactTriangle>* Mesh)
	{
		FVector Planes[3]; int32 NumPlanes = 0;
		for (int32 Attempt=0; Attempt<8; ++Attempt)
		{
			FVector Remaining = Destination - Pose.GetLocation();
			// Keep outward motion. Projecting the entire vector onto the last
			// contact plane can pin a withdrawing hand to a building corner.
			for (int32 Pass=0; Pass<2; ++Pass)
				for (int32 P=0; P<NumPlanes; ++P)
					Remaining -= Planes[P] * FMath::Min(0., FVector::DotProduct(Remaining, Planes[P]));
			if (Remaining.IsNearlyZero(.01f)) break;
			bool Blocked = false; FVector Normal = FVector::ZeroVector;
			Pose = Advance(World, Pose, FTransform(Pose.GetRotation(), Pose.GetLocation()+Remaining),
				Shapes, Query, Mesh, Normal, Blocked);
			if (!Blocked || Normal.IsNearlyZero()) break;
			Normal.Normalize();
			bool Duplicate=false;
			for (int32 P=0;P<NumPlanes;++P) Duplicate |= FVector::DotProduct(Planes[P],Normal)>.995f;
			if (!Duplicate && NumPlanes<3) Planes[NumPlanes++] = Normal;
		}
	}

	void Rotate(UWorld* World, FTransform& Pose, const FQuat& Rotation,
		const TArray<FACEVRContactShape>& Shapes, const FCollisionQueryParams& Query,
		const TArray<FACEVRContactTriangle>* Mesh)
	{
		const FTransform Start = Pose, End(Rotation, Pose.GetLocation());
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::RadiansToDegrees(
			Start.GetRotation().AngularDistance(Rotation))/4.f), 1, 48);
		for (int32 I=1; I<=Steps; ++I)
		{
			bool Blocked = false; FVector Normal = FVector::ZeroVector;
			const FQuat Next=FQuat::Slerp(Start.GetRotation(),Rotation,float(I)/Steps).GetNormalized();
			Pose = Advance(World, Pose, FTransform(Next,Pose.GetLocation()), Shapes, Query, Mesh, Normal, Blocked);
			if (Blocked)
			{
				FTransform Released(Next,Pose.GetLocation());
				if (!Depenetrate(World,Released,Shapes,Query,Mesh,10.f)
					|| FVector::Distance(Released.GetLocation(),Start.GetLocation())>12.f) break;
				// Translate with the last clear orientation before adopting the
				// new one. This lets a shield turn while sliding off a corner.
				bool TranslationBlocked=false;
				const auto Slide=Advance(World,Pose,FTransform(Pose.GetRotation(),Released.GetLocation()),Shapes,Query,Mesh,Normal,TranslationBlocked);
				if (!Slide.GetLocation().Equals(Released.GetLocation(),.02f)) break;
				Pose=Released;
			}
		}
	}
}

bool ACEVRHandCollision::Overlaps(UWorld* World, const FTransform& Pose,
	const TArray<FACEVRContactShape>& Shapes, const FCollisionQueryParams& Query, const TArray<FACEVRContactTriangle>* Mesh)
{
	for (const auto& Shape : Shapes)
	{
		const FTransform Box = Shape.Local * Pose;
		if (World->OverlapBlockingTestByChannel(Box.GetLocation(), Box.GetRotation(), ECC_Pawn,
			FCollisionShape::MakeBox(Shape.Extent), Query)) return true;
		if(Mesh)
		{
			const FBox Broad=FBox(-Shape.Extent,Shape.Extent).TransformBy(Box);
			for(const auto& Triangle:*Mesh)
			{
				float Time; FVector Normal;
				if(Broad.Intersect(Triangle.Bounds) && TriangleSweep(Triangle,Box,Box.GetLocation(),Shape.Extent,Time,Normal,true)) return true;
			}
		}

	}
	return false;
}

void ACEVRHandCollision::Move(UWorld* World, FACEVRContactState& State, const FTransform& Desired,
	const FVector& SafeOrigin, const TArray<FACEVRContactShape>& Shapes, const FCollisionQueryParams& Query, const TArray<FACEVRContactTriangle>* Mesh)
{
	State.bContact = false;
	if (!World || Desired.ContainsNaN() || Shapes.IsEmpty()) { State.Reset(); return; }
	if (State.bValid && Overlaps(World,State.Pose,Shapes,Query,Mesh))
		Depenetrate(World,State.Pose,Shapes,Query,Mesh,3.f);
	// Recover from tracking/portal discontinuities at the body, rather than
	// accepting a controller pose which may already be on the far side of a wall.
	if (!State.bValid || FVector::DistSquared(State.Pose.GetLocation(), SafeOrigin) > FMath::Square(220.f)
		|| Overlaps(World, State.Pose, Shapes, Query, Mesh))
	{
		State.bValid = false;
		for (int32 Attempt=0; Attempt<5 && !State.bValid; ++Attempt)
		{
			// A freshly equipped long weapon may not fit pointing at a nearby
			// wall. Fold it upright/back before trying to turn toward tracking.
			const FQuat Rotation = Attempt == 0 ? Desired.GetRotation()
				: (FRotator(Attempt == 1 ? 90 : Attempt == 2 ? -90 : 0,
					Attempt == 3 ? 90 : Attempt == 4 ? -90 : 0,0).Quaternion() * Desired.GetRotation());
			for (int32 I=0; I<5; ++I)
			{
				const FTransform Seed(Rotation, SafeOrigin - FVector(0,0,I*18.f));
				if (!Overlaps(World, Seed, Shapes, Query, Mesh)) { State.Pose = Seed; State.bValid = true; break; }
			}
		}
		if (!State.bValid) { State.Pose = FTransform(Desired.GetRotation(), SafeOrigin); State.bContact = true; return; }
	}
	const FTransform Start = State.Pose;
	const float Angle = FMath::RadiansToDegrees(Start.GetRotation().AngularDistance(Desired.GetRotation()));
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Max(Angle / 4.f,
		float(FVector::Dist(Start.GetLocation(), Desired.GetLocation()) / 20.f))), 1, 48);
	FVector Normal = FVector::ZeroVector;
	for (int32 I=1; I<=Steps; ++I)
	{
		bool Blocked = false;
		State.Pose = Advance(World, State.Pose, Between(Start, Desired, float(I)/Steps), Shapes, Query, Mesh, Normal, Blocked);
		if (Blocked) { State.bContact = true; break; }
	}
	// A blocked rotation must not prevent withdrawal or sliding, and a blocked
	// translation must not prevent turning the tip away. Solve them separately
	// after the coupled sweep, then retry once if one frees the other. Every
	// recovery path still sweeps; there is no snap through the wall to tracking.
	for (int32 I=0; State.bContact && I<2; ++I)
	{
		const FTransform Before = State.Pose;
		Translate(World, State.Pose, Desired.GetLocation(), Shapes, Query, Mesh);
		Rotate(World, State.Pose, Desired.GetRotation(), Shapes, Query, Mesh);
		if (Before.Equals(State.Pose, .0001f)) break;
	}
	// Once the real hand has rounded a corner, a stale constrained orientation
	// must not keep it latched there. Reacquire only along a verified clear path
	// from the player's side, so this cannot snap across a wall.
	if (State.bContact && !Overlaps(World,Desired,Shapes,Query,Mesh))
	{
		const FTransform Seed(Desired.GetRotation(),SafeOrigin);
		if (!Overlaps(World,Seed,Shapes,Query,Mesh))
		{
			bool Blocked=false;
			const FTransform Recovered=Advance(World,Seed,Desired,Shapes,Query,Mesh,Normal,Blocked);
			if (!Blocked && Recovered.Equals(Desired,.001f)) State.Pose=Desired;
		}
	}
	State.bContact = !State.Pose.GetLocation().Equals(Desired.GetLocation(), .02f)
		|| State.Pose.GetRotation().AngularDistance(Desired.GetRotation()) > .001f;
}
