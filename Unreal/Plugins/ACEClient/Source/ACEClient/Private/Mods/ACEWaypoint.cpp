#include "Mods/ACEWaypoint.h"
#include "Internationalization/Regex.h"

namespace ACEWaypoint
{
TArray<FLink> FindCoordinates(const FString& Text)
{
    static const FRegexPattern Pattern(TEXT("(?i)(?<![\\w.+-])([0-9]{1,3}(?:\\.[0-9]{1,6})?)\\s*([NS])\\s*[,; ]\\s*([0-9]{1,3}(?:\\.[0-9]{1,6})?)\\s*([EW])(?![\\w])"));
    TArray<FLink> Result;
    FRegexMatcher Match(Pattern,Text.Left(16384));
    while(Result.Num()<64 && Match.FindNext())
    {
        double NS=FCString::Atod(*Match.GetCaptureGroup(1)),EW=FCString::Atod(*Match.GetCaptureGroup(3));
        if(NS>102 || EW>102)continue;
        if(Match.GetCaptureGroup(2).Equals(TEXT("S"),ESearchCase::IgnoreCase))NS=-NS;
        if(Match.GetCaptureGroup(4).Equals(TEXT("W"),ESearchCase::IgnoreCase))EW=-EW;
        Result.Add({Match.GetMatchBeginning(),Match.GetMatchEnding(),FVector2D(EW,NS)});
    }
    return Result;
}
bool Parse(const FString& Text,FVector2D& Out)
{
    const FString Trimmed=Text.TrimStartAndEnd();const auto Links=FindCoordinates(Trimmed);
    if(Links.Num()!=1 || Links[0].Begin!=0 || Links[0].End!=Trimmed.Len())return false;
    Out=Links[0].Coordinates;return true;
}
FString Format(FVector2D P)
{
    return FString::Printf(TEXT("%.2f%s, %.2f%s"),FMath::Abs(P.Y),P.Y<0?TEXT("S"):TEXT("N"),FMath::Abs(P.X),P.X<0?TEXT("W"):TEXT("E"));
}
FVector2D Coordinates(const FACEPosition& P)
{
    const uint32 Cell=uint32(P.CellId);
    return FVector2D((((Cell>>24)&255)*192.+P.Location.X)/240.-101.95,(((Cell>>16)&255)*192.+P.Location.Y)/240.-101.95);
}
double RelativeBearing(FVector2D From,FVector2D To,FVector2D Forward)
{
    const auto D=To-From;
    return FMath::Atan2(Forward.Y*D.X-Forward.X*D.Y,FVector2D::DotProduct(Forward,D));
}
}
