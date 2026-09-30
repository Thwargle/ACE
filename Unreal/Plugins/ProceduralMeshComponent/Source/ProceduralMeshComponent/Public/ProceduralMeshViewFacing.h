#pragma once

#include "CoreMinimal.h"

// Retail CPhysicsPart::calc_draw_frame: the draw frame is independent of the
// simulation frame. Never rotate collision/attachments to face a camera.
namespace ProceduralMeshViewFacing
{
inline bool IsViewFacing(uint32 Mode) { return Mode >= 2 && Mode <= 5; }

inline FTransform DrawTransform(const FTransform& Simulation, const FVector& SortCenter,
    uint32 Mode, const FVector& Eye)
{
    if (!IsViewFacing(Mode)) return Simulation;
    FVector Heading = (Simulation.TransformPosition(SortCenter) - Eye).GetSafeNormal();
    if (Heading.IsNearlyZero()) Heading = FVector::UpVector;
    const FQuat Rotation = Simulation.GetRotation();
    if (Mode >= 3)
    {
        // Frame::rotate_around_axis_to_vector retains the authored other axis
        // when the viewing ray is parallel to the constrained axis.
        const FVector Axis = Simulation.GetUnitAxis(EAxis::Type(Mode - 2));
        if ((Heading - Axis * FVector::DotProduct(Heading, Axis)).IsNearlyZero(.0002)) return Simulation;
    }
    FTransform Draw = Simulation;
    switch (Mode)
    {
    case 2: Draw.SetRotation(FRotationMatrix::MakeFromYZ(Heading, FVector::UpVector).ToQuat()); break;
    case 3: Draw.SetRotation(FRotationMatrix::MakeFromXZ(Rotation.GetAxisX(), Heading).ToQuat()); break;
    case 4: Draw.SetRotation(FRotationMatrix::MakeFromYX(Rotation.GetAxisY(), -Heading).ToQuat()); break;
    case 5: Draw.SetRotation(FRotationMatrix::MakeFromZY(Rotation.GetAxisZ(), Heading).ToQuat()); break;
    }
    return Draw;
}
}
