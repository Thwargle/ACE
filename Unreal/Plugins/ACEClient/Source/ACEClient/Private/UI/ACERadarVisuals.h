#pragma once
#include "CoreMinimal.h"

namespace ACERadarVisuals
{
    // gmRadarUI::DrawPoint/DrawEdges/DrawCorners/DrawSelected paint pixels,
    // not DAT sprites. Retain those masks inside a common seven-pixel canvas.
    inline bool Pixel(int32 Shape, bool Selected, int32 X, int32 Y)
    {
        const int32 AX=FMath::Abs(X), AY=FMath::Abs(Y);
        if (Selected && ((AX==3 && AY<=2) || (AY==3 && AX<=2))) return true;
        const bool Center=AX==0 && AY==0, Edges=AX+AY==1, Corners=AX==1 && AY==1;
        switch (Shape)
        {
        case 1: return Center;
        case 2: return Edges || Corners;
        case 3: return Center || Corners;
        case 4: return Center || Edges;
        case 5: return Center || (Y==1 && AX<=1);
        case 6: return Center || (Y==-1 && AX<=1);
        case 7: return Center || Edges || Corners;
        case 8: return Edges; // player: hollow green diamond
        default: return false;
        }
    }

    // VividTargetIndicator's eight compass images, clockwise from screen-up.
    inline uint32 ArrowEnum(FVector2D Direction)
    {
        const double Degrees=FMath::RadiansToDegrees(FMath::Atan2(Direction.X,-Direction.Y));
        const int32 Octant=(FMath::RoundToInt(Degrees/45.)+8)%8;
        constexpr uint32 Images[]={6,7,9,12,11,10,8,5};
        return Images[Octant];
    }
    inline FVector2D EdgePosition(FVector2D Direction, FVector2D ViewSize, FVector2D ImageSize)
    {
        if (Direction.IsNearlyZero()) Direction=FVector2D(0,1);
        const FVector2D Half=(ViewSize-ImageSize)*.5-FVector2D(8,8);
        const double T=FMath::Min(FMath::Max(0.,Half.X)/FMath::Max(.00001,FMath::Abs(Direction.X)),
            FMath::Max(0.,Half.Y)/FMath::Max(.00001,FMath::Abs(Direction.Y)));
        return ViewSize*.5+Direction*T-ImageSize*.5;
    }
}
