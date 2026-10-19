#pragma once
#include "CoreMinimal.h"
#include "ACETypes.h"

struct FACESupplyEntry
{
    int32 ClassId=0, Icon=0, Overlay=0, Underlay=0, Effects=0, ItemType=0;
    FString Name;
    int64 Count=0;
    bool Matches(const FACEWorldObject& Item) const
    { return ClassId ? ClassId==Item.WeenieClassId : Name.Equals(Item.Name,ESearchCase::IgnoreCase); }
    void CopyAppearance(const FACEWorldObject& Item)
    { Icon=Item.IconId;Overlay=Item.IconOverlayId;Underlay=Item.IconUnderlayId;Effects=Item.UiEffects;ItemType=Item.ItemType; }
};

namespace ACELedger
{
    inline constexpr double HeaderHeight=24., SupplyEditorHeight=68., HuntEditorHeight=50.;
    inline bool IsOwned(const FACEWorldObject& Item,int32 Player,const TMap<int32,FACEWorldObject>& Objects)
    {
        if(!Player||Item.Guid==Player)return false;
        if(Item.WielderId==Player)return true;
        int32 Parent=Item.ContainerId;
        TSet<int32> Seen;
        while(Parent&&Parent!=Player)
        {
            if(Seen.Contains(Parent))return false;Seen.Add(Parent);
            const auto* Pack=Objects.Find(Parent);if(!Pack)return false;Parent=Pack->ContainerId;
        }
        return Parent==Player;
    }
    inline void Count(TArray<FACESupplyEntry>& Entries,int32 Player,const TMap<int32,FACEWorldObject>& Objects)
    {
        for(auto& E:Entries)E.Count=0;
        for(const auto& Pair:Objects)if(IsOwned(Pair.Value,Player,Objects))
            for(auto& E:Entries)if(E.Matches(Pair.Value))E.Count+=FMath::Max(1,Pair.Value.StackSize);
    }
}

/** A fixed-size history; observing every server vital update preserves gains around spending. */
struct FACEHuntLedger
{
    struct FBucket { int64 Second=-1, XP=0, Lum=0; };
    FBucket Buckets[301];
    int64 XP=0, Lum=0, PreviousXP=0, PreviousLum=0;
    bool HasXP=false, HasLum=false;
    double Started=0;
    void Reset(double Now) { *this=FACEHuntLedger(); Started=Now; }
    void Observe(const FACEPlayerVitals& V,double Now)
    {
        if(!V.bValid)return;
        int64 GainXP=0,GainLum=0;
        if(V.QualityInt64s.Contains(1))
        { if(HasXP)GainXP=FMath::Max<int64>(0,V.TotalExperience-PreviousXP); PreviousXP=V.TotalExperience;HasXP=true; }
        // Login can omit a zero balance. Wait for a complete description (or an
        // explicit balance), not just bValid: attribute packets can arrive first.
        if(V.bHasLuminanceBalance||V.QualityInt64s.Contains(6))
        { if(HasLum)GainLum=FMath::Max<int64>(0,V.AvailableLuminance-PreviousLum);PreviousLum=V.AvailableLuminance;HasLum=true; }
        XP+=GainXP;Lum+=GainLum;
        if(!GainXP&&!GainLum)return;
        const int64 Second=FMath::FloorToInt64(Now);
        auto& B=Buckets[Second%301];if(B.Second!=Second)B={Second,0,0};B.XP+=GainXP;B.Lum+=GainLum;
    }
    FBucket LastFiveMinutes(double Now) const
    {
        FBucket Result;const int64 Second=FMath::FloorToInt64(Now);
        for(const auto& B:Buckets)if(B.Second>=0&&B.Second>Second-300&&B.Second<=Second){Result.XP+=B.XP;Result.Lum+=B.Lum;}
        return Result;
    }
    double Elapsed(double Now) const { return FMath::Max(0.,Now-Started); }
    int64 PerHour(int64 Total,double Now) const {return Elapsed(Now)>=1 ? int64(double(Total)*3600./Elapsed(Now)) : 0;}
    // Use the unrounded run rate; spending XP does not change earned XP.
    double SecondsToLevel(int64 Needed,double Now) const
    { return Needed<=0 ? 0. : XP>0&&Elapsed(Now)>=1 ? double(Needed)*Elapsed(Now)/double(XP) : -1.; }
};
