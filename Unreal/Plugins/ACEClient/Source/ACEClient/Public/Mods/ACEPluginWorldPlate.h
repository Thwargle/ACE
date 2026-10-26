#pragma once

#include "CoreMinimal.h"
#include "VR/ACEVRWidgetComponent.h"
#include "Dom/JsonObject.h"
#include "ACEPluginWorldPlate.generated.h"

struct FACEPluginPlateVital
{
    double Current=-1, Maximum=-1, Fraction=-1;
    bool IsKnown() const { return Fraction>=0 || (Current>=0 && Maximum>0); }
    double Ratio() const { return Fraction>=0 ? Fraction : Maximum>0 ? Current/Maximum : 0; }
};
struct FACEPluginPlateEffect { uint32 Spell=0, Icon=0; double Expires=0; };
struct FACEPluginWorldPlate
{
    int32 Id=0, Level=0;
    FString Name, Role;
    FLinearColor Color=FLinearColor(1,.8f,.15f);
    bool SelectionOnly=true, Refresh=true;
    // Opacity is applied once to the composed plate, independently of mesh selection tint.
    double Opacity=1, BackgroundOpacity=1, VROpacity=-1, VRBackgroundOpacity=-1;
    double OpacityFor(bool VR) const { return VR && VROpacity>=0 ? VROpacity : Opacity; }
    double BackgroundOpacityFor(bool VR) const { return VR && VRBackgroundOpacity>=0 ? VRBackgroundOpacity : BackgroundOpacity; }
    FACEPluginPlateVital Health, Stamina, Mana;
    TArray<FACEPluginPlateEffect> Effects;
};

namespace ACEPluginWorldPlate
{
    bool Parse(const TArray<TSharedPtr<FJsonValue>>& Values,TArray<FACEPluginWorldPlate>& Out,FString& Error,double Now);
    FString ObjectType(const struct FACEWorldObject& Object);
}

/** A native world-space billboard: late frame positioning, stereo rendering,
 * no collision, no Lua camera polling, and a bounded transparent render target. */
UCLASS()
class ACECLIENT_API UACEPluginWorldPlateComponent : public UACEVRWidgetComponent
{
    GENERATED_BODY()
public:
    UACEPluginWorldPlateComponent();
    void SetPlate(const FACEPluginWorldPlate& Value,class UACEPluginSubsystem* InHost);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    FString PluginId;
    uint32 Revision=MAX_uint32;
    const FACEPluginWorldPlate& GetPlate() const { return Plate; }
private:
    friend class FACEPluginWorldPlateTest;
    void UpdatePresentation(bool VR);
    bool VRPresentation=false;
    FACEPluginWorldPlate Plate;
    TWeakObjectPtr<class UACEPluginSubsystem> Host;
    TSharedPtr<class SACEPluginWorldPlate> Surface;
    int64 LastPaintSecond=0;
    FVector2D LastBody=FVector2D::ZeroVector;
};
