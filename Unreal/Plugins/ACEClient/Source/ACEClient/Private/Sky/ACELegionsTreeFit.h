#pragma once

#include "CoreMinimal.h"

namespace ACELegionsSky
{
	// Find the trunk at a low horizontal slice, then compare the crown outline
	// around that trunk. Both games use a Z-up mesh after DAT axis conversion.
	struct FTreeSilhouette
	{
		static constexpr int32 Samples = 24;
		FVector2D Trunk = FVector2D::ZeroVector;
		double Radius[Samples]{};

		template<typename VisitTriangles>
		FTreeSilhouette(const FBox& Box, VisitTriangles Visit)
		{
			const double Height = Box.Max.Z - FMath::Max(0., Box.Min.Z);
			const double Slice = FMath::Max(0., Box.Min.Z) + .08 * Height;
			FBox2D CrossSection(ForceInit);
			Visit([&](const FVector& A, const FVector& B, const FVector& C)
			{
				const FVector V[] = {A,B,C};
				for (int32 I=0; I<3; ++I)
				{
					const FVector& P = V[I];
					const FVector& Q = V[(I+1)%3];
					if ((P.Z < Slice) != (Q.Z < Slice))
						CrossSection += FVector2D(FMath::Lerp(P,Q,(Slice-P.Z)/(Q.Z-P.Z)));
				}
			});
			if (CrossSection.bIsValid) Trunk = CrossSection.GetCenter();
			FVector2D Directions[Samples];
			for (int32 I=0; I<Samples; ++I)
			{
				const double Angle = 2.*UE_PI*I/Samples;
				Directions[I] = FVector2D(FMath::Cos(Angle),FMath::Sin(Angle));
			}
			Visit([&](const FVector& A, const FVector& B, const FVector& C)
			{
				for (const FVector& P : {A,B,C})
					if (P.Z >= Box.Max.Z - .55*Height)
						for (int32 I=0; I<Samples; ++I)
							Radius[I] = FMath::Max(Radius[I],FVector2D::DotProduct(FVector2D(P)-Trunk,Directions[I]));
			});
			double Sum = 0;
			for (double R : Radius) Sum += R;
			if (Sum > UE_SMALL_NUMBER)
				for (double& R : Radius) R *= Samples/Sum;
		}

		FQuat RotationFrom(const FTreeSilhouette& Source) const
		{
			int32 Best = 0;
			double BestError = TNumericLimits<double>::Max();
			for (int32 Shift=0; Shift<Samples; ++Shift)
			{
				double Error = 0;
				for (int32 I=0; I<Samples; ++I)
					Error += FMath::Square(Radius[I]-Source.Radius[(I+Samples-Shift)%Samples]);
				// Keep the authored orientation if the improvement is negligible.
				if (Error + 1.e-5 < BestError) { BestError = Error; Best = Shift; }
			}
			return FQuat(FVector::UpVector,2.*UE_PI*Best/Samples);
		}
	};
}
