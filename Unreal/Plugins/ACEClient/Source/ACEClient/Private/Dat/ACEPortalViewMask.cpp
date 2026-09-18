#include "Dat/ACEPortalViewMask.h"

FACEPortalViewMask FACEPortalViewMask::Build(const FVector& Eye,
    const TArray<ACEOutdoorPortalPlan::FAdmittedAperture>& Apertures)
{
    FACEPortalViewMask Result;
    Result.Origin = Eye;
    for (const auto& Aperture : Apertures)
    {
        if (Aperture.WorldVerts.Num() < 3) continue;
        FVector Center = FVector::ZeroVector;
        for (const FVector& V : Aperture.WorldVerts) Center += V - Eye;
        Center /= Aperture.WorldVerts.Num();
        FVector Normal = Aperture.WorldNormal.GetSafeNormal();
        if (Normal.IsNearlyZero()) continue;
        if (FVector::DotProduct(Normal, Center) < 0) Normal = -Normal;
        TArray<FVector4f> Planes;
        // Keep only points beyond the doorway, not floors between the eye and door.
        Planes.Add(FVector4f(FVector3f(-Normal), -FVector::DotProduct(Normal, Aperture.WorldVerts[0] - Eye)));
        for (int32 I = 0; I < Aperture.WorldVerts.Num(); ++I)
        {
            const FVector A = Aperture.WorldVerts[I] - Eye;
            const FVector B = Aperture.WorldVerts[(I+1) % Aperture.WorldVerts.Num()] - Eye;
            FVector N = FVector::CrossProduct(A, B).GetSafeNormal();
            if (N.IsNearlyZero()) continue;
            if (FVector::DotProduct(N, Center) > 0) N = -N;
            Planes.Add(FVector4f(FVector3f(N), 0));
        }
        if (Planes.Num() < 4) continue;
        Result.Records.Add(FVector4f(Planes.Num(), 0, 0, 0));
        Result.Records.Append(Planes);
        ++Result.ViewCount;
    }
    return Result;
}

bool FACEPortalViewMask::Contains(const FVector& WorldPoint) const
{
    return ContainsFromEye(WorldPoint, Origin);
}

bool FACEPortalViewMask::ContainsFromEye(const FVector& WorldPoint, const FVector& ViewEye) const
{
    const FVector3f P(WorldPoint - Origin);
    const FVector3f Eye(ViewEye - Origin);
    int32 Record = 0;
    for (int32 View = 0; View < ViewCount; ++View)
    {
        const int32 Count = static_cast<int32>(Records[Record++].X);
        const FVector4f Front = Records[Record];
        bool bInside = true;
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector4f& Plane = Records[Record++];
            if (I == 0) bInside &= FVector3f::DotProduct(P, FVector3f(Plane)) <= Plane.W + 0.001f;
            else
            {
                const FVector3f N(Plane), F(Front);
                const float D=FVector3f::DotProduct(F,N);
                const FVector3f A=(F-N*D)*Front.W/FMath::Max(.000001f,1.f-D*D);
                FVector3f EdgeNormal=FVector3f::CrossProduct(FVector3f::CrossProduct(F,N),A-Eye);
                if(FVector3f::DotProduct(EdgeNormal,N)<0)EdgeNormal=-EdgeNormal;
                bInside &= FVector3f::DotProduct(EdgeNormal,P-Eye)<=.001f;
            }
        }
        if (bInside) return true;
    }
    return false;
}
